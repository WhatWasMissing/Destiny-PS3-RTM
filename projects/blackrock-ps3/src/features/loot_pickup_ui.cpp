 
 
 
 
 
 
 
 
 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "app/engine.h"
#include "app/registry.h"
#include "app/settings.h"
#include "core/error.h"
#include "core/runtime.h"
#include "features/loot_common.h"
#include "game/loot_addr.h"
#include "game/loot_pickup.h"
#include "ui/widgets.h"

namespace brps3::lootui {

using namespace ui;
using namespace loot;
using pickup::Preset;
using pickup::Rule;

namespace {

constexpr const char* kSection = "Pickup filter";
constexpr const char* kToggleId = "loot.pickup";
constexpr const char* kPatch = "loot.pickup_filter";
constexpr const char* kStubName = "loot.pickup.v1";
constexpr const char* kPresetKey = "loot.pickup.preset";
constexpr const char* kFavKey = "loot.pickup.favs";
constexpr const char* kDefaultPreset = "Legendary loot only";
constexpr const char* kApplyJob = "Pickup filter: apply preset";
constexpr float kRowH = 28;

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

 

struct Live {                           
    std::mutex m;
    bool installed = false;
    std::string preset;
    uint32_t kept = 0;
    std::string tableLine;              
    std::string error;
};
Live& live() {
    static Live l;
    return l;
}

struct Session {                        
    bool verified = false;
    pickup::TableCheck check;
    uint32_t data = 0, stub = 0;
};

std::string armedName(Settings& s) {
    std::string n = s.str(kPresetKey, kDefaultPreset);
    if (!pickup::PresetStore::get().find(n)) n = kDefaultPreset;
    return n;
}

pickup::Bitmap compileNamed(const std::string& name, uint32_t* kept) {
    const auto& cat = pickup::definitions();
    if (!cat.error.empty()) throw Error("Pickup definitions unavailable: " + cat.error);
    const auto p = pickup::PresetStore::get().find(name);
    if (!p) throw Error("No pickup preset called \"" + name + "\"");
    const pickup::Bitmap b = pickup::compile(*p, cat);
    if (kept) *kept = pickup::countAllowed(b);
    return b;
}

Session& prepare(Ctx& c) {
    Session& s = c.state<Session>("loot.pickup.session");
    c.require(kPickupFingerprints, "Pickup filter (native pickup check)");
    if (!s.verified) {
        RuntimeConsole con(c.rt());
        Resolver res(con);
        s.check = pickup::verifyItemTable(con, res);
        s.verified = true;
        std::lock_guard<std::mutex> lock(live().m);
        live().tableLine = fmt("Item table %s verified: %s definitions, SHA-256 %s...", hex(s.check.tableTag).c_str(),
                               grouped(s.check.count).c_str(), s.check.sha256.substr(0, 12).c_str());
        c.log("[pickup] " + live().tableLine);
    }
    if (!s.data) {
        const std::vector<uint8_t> init = pickup::encodeData(pickup::Mode::Allow, pickup::Bitmap{});
        s.data = c.rt().alloc(pickup::kDataSize, init.data(), init.size());
    }
    const uint32_t data = s.data;
    s.stub = c.rt().sharedCode(kStubName, [data](uint32_t base) { return pickup::stubProgram(base, data); }, pickup::kStubSize);
    return s;
}

void writeBitmap(Ctx& c, Session& s, const pickup::Bitmap& b) {
    RuntimeConsole con(c.rt());
     
    con.w32(s.data + pickup::kDataMode, uint32_t(pickup::Mode::Allow));
    const std::vector<uint8_t> enc = pickup::encodeData(pickup::Mode::Bitmap, b);
    con.write(s.data + pickup::kDataMasks, std::vector<uint8_t>(enc.begin() + pickup::kDataMasks, enc.end()));
    con.w32(s.data + pickup::kDataMode, uint32_t(pickup::Mode::Bitmap));
}

void install(Ctx& c, const std::string& name) {
    uint32_t kept = 0;
    const pickup::Bitmap b = compileNamed(name, &kept);
    Session& s = prepare(c);
    writeBitmap(c, s, b);
    if (!c.rt().applied(kPatch)) {
        Runtime::Word w;
        w.addr = PICKUP_SITE;
        w.original = world::be32(PICKUP_SITE_ORIGINAL);
        w.replacement = world::be32(pickup::hookWord(s.stub));
        c.rt().apply(kPatch, {w});
    }
    {
        std::lock_guard<std::mutex> lock(live().m);
        live().installed = true;
        live().preset = name;
        live().kept = kept;
        live().error.clear();
    }
    c.log(fmt("[pickup] filter on: %s (%s of %s definitions kept)", name.c_str(), grouped(kept).c_str(), grouped(ITEM_DEFINITIONS).c_str()));
    feed("Pickup filter", fmt("On: %s (%s of %s kept)", name.c_str(), grouped(kept).c_str(), grouped(ITEM_DEFINITIONS).c_str()));
}

void setFilter(Ctx& c, bool on) {
    if (on) {
        try {
            install(c, armedName(c.settings()));
        } catch (const std::exception& e) {
            feed("Pickup filter", e.what(), false);
            std::lock_guard<std::mutex> lock(live().m);
            live().error = e.what();
            throw;
        }
        return;
    }
    feed("Pickup filter", "Off: the game decides every pickup again");
    c.rt().revert(kPatch);    
    Session& s = c.state<Session>("loot.pickup.session");
    if (s.data) {
        try {
            RuntimeConsole(c.rt()).w32(s.data + pickup::kDataMode, uint32_t(pickup::Mode::Allow));
        } catch (...) {
        }
    }
    std::lock_guard<std::mutex> lock(live().m);
    live().installed = false;
}

void applyJob(Ctx& c, const std::string& name) {
    {
        std::lock_guard<std::mutex> lock(live().m);
        if (!live().installed) return;
    }
    install(c, name);
    c.toast("Pickup filter: " + name);
}

 

struct Editor {
    bool loaded = false;
    std::string selected;               
    std::string search;
    uint64_t revision = ~0ull;
    std::vector<Preset> builtIns, user;
     
    std::string previewFor;
    uint64_t previewRev = ~0ull;
    int previewEdit = -1;
    pickup::Bitmap bitmap{};
    std::vector<int> decided;           
    uint32_t kept = 0;
    std::array<uint32_t, size_t(pickup::ItemClass::Count)> keptByClass{}, totalByClass{};
     
    std::string defSearch;
    int defFilter = 1;                  
    std::vector<int> view;
    bool viewDirty = true;
     
    int pickerRule = -1;
    std::string pickerSearch;
    int editCount = 0;                  
    std::string lastArmed;              
};
Editor& ed() {
    static Editor e;
    return e;
}

void reloadStore() {
    Editor& e = ed();
    auto& store = pickup::PresetStore::get();
    e.builtIns = store.builtIns();
    e.user = store.user();
    e.revision = store.revision();
}

Preset* findEditable(const std::string& name) {
    for (Preset& p : ed().user)
        if (p.name == name) return &p;
    return nullptr;
}

const Preset* findAny(const std::string& name) {
    for (const Preset& p : ed().builtIns)
        if (p.name == name) return &p;
    for (const Preset& p : ed().user)
        if (p.name == name) return &p;
    return nullptr;
}

void saveUser() {
    Editor& e = ed();
    try {
        pickup::PresetStore::get().setUser(e.user);
        e.revision = pickup::PresetStore::get().revision();
        ++e.editCount;
    } catch (const std::exception& ex) {
        notify(ex.what(), Level::Warn);
    }
}

bool isFav(Settings& s, const std::string& name) { return s.inList(kFavKey, name); }

void ensureEditor(Ui& ui) {
    Editor& e = ed();
    if (!e.loaded || e.revision != pickup::PresetStore::get().revision()) {
        reloadStore();
        if (!e.loaded) e.selected = armedName(ui.settings());
        e.loaded = true;
        ++e.editCount;
    }
    if (const std::string armed = armedName(ui.settings()); armed != e.lastArmed) {
        e.lastArmed = armed;
        e.selected = armed;
        e.viewDirty = true;
    }
    if (!findAny(e.selected)) e.selected = kDefaultPreset;
}

void refreshPreview() {
    Editor& e = ed();
    const Preset* p = findAny(e.selected);
    if (!p) return;
    if (e.previewFor == e.selected && e.previewEdit == e.editCount) return;
    const auto& cat = pickup::definitions();
    e.previewFor = e.selected;
    e.previewEdit = e.editCount;
    e.bitmap = pickup::compile(*p, cat);
    e.kept = pickup::countAllowed(e.bitmap);
    e.decided.assign(cat.size(), -1);
    e.keptByClass.fill(0);
    e.totalByClass.fill(0);
    for (const auto& d : cat.all()) {
        e.decided[d.index] = pickup::decidingRule(*p, d);
        ++e.totalByClass[size_t(d.cls)];
        if (pickup::allowed(e.bitmap, d.index)) ++e.keptByClass[size_t(d.cls)];
    }
    e.viewDirty = true;
}

void selectPreset(const std::string& name) {
    ed().selected = name;
    ed().viewDirty = true;
}

 
void usePreset(Ui& ui, const std::string& name) {
    ui.settings().set(std::string(kPresetKey), name);
    bool installed;
    {
        std::lock_guard<std::mutex> lock(live().m);
        installed = live().installed;
    }
    if (installed && ui.connected()) {
        noteQueued(kApplyJob);
        ui.post(kApplyJob, [name](Ctx& c) { applyJob(c, name); });
    } else {
        notify("\"" + name + "\" selected: turn the Pickup filter switch on to install it", Level::Info);
    }
}

 

void drawStatus(Ui& ui) {
    bool installed;
    std::string preset, table, error;
    uint32_t kept;
    {
        std::lock_guard<std::mutex> lock(live().m);
        installed = live().installed;
        preset = live().preset;
        kept = live().kept;
        table = live().tableLine;
        error = live().error;
    }
    if (demo() && table.empty()) table = "Item table 80A5F241 verified: 6,557 definitions, SHA-256 a6d0c8d75185...";
    const std::string armed = armedName(ui.settings());
    if (installed) {
        checkRow(fmt("On: %s", preset.c_str()).c_str(), Mark::Ok, fmt("%s of %s definitions kept", grouped(kept).c_str(), grouped(ITEM_DEFINITIONS).c_str()).c_str());
    } else {
        checkRow(fmt("Off. The switch installs \"%s\"", armed.c_str()).c_str(), Mark::Skip, "the game decides every pickup");
    }
    if (!table.empty()) checkRow(table.c_str(), Mark::Ok);
    if (!error.empty()) checkRow(error.c_str(), Mark::Bad);
}

 

void drawPresetList(Ui& ui, float width, float height) {
    Editor& e = ed();
    Settings& set = ui.settings();
    const std::string armed = armedName(set);
    std::string installedName;
    {
        std::lock_guard<std::mutex> lock(live().m);
        if (live().installed) installedName = live().preset;
    }
    searchField("##presetsearch", &e.search, "Search Presets", width / scale());
    gap(sp::S);
    std::vector<std::pair<const Preset*, bool>> rows;    
    const std::string q = lower(e.search);
    auto take = [&](const std::vector<Preset>& v) {
        for (const Preset& p : v)
            if (q.empty() || lower(p.name + " " + p.note).find(q) != std::string::npos) rows.push_back({&p, isFav(set, p.name)});
    };
    take(e.builtIns);
    take(e.user);
    std::stable_sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    ImGui::PushStyleColor(ImGuiCol_ChildBg, pal::Input);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(kRowRounding));
    ImGui::BeginChild("##presets", ImVec2(width, height), ImGuiChildFlags_Borders);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    std::string toggleFav;
    for (const auto& [p, fav] : rows) {
        ImGui::PushID(p->name.c_str());
        const bool sel = p->name == e.selected;
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        const float h = px(48);
        ImGui::SetNextItemAllowOverlap();
        if (ImGui::InvisibleButton("##row", ImVec2(w, h))) selectPreset(p->name);
        const bool hov = ImGui::IsItemHovered();
        if (hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) usePreset(ui, p->name);
        ImDrawList* d = ImGui::GetWindowDrawList();
        if (sel || hov) d->AddRectFilled(pos + ImVec2(px(4), px(2)), pos + ImVec2(w - px(4), h - px(2)), u32(sel ? pal::RaisedHi : pal::Raised), px(7));
        if (sel) d->AddRectFilled(pos + ImVec2(px(4), px(10)), pos + ImVec2(px(7), h - px(10)), u32(pal::Accent), px(2));
        const float x = pos.x + px(16);
         
        const ImVec2 starPos(pos.x + w - px(30), pos.y + px(12));
        ImGui::SetCursorScreenPos(starPos);
        if (ImGui::InvisibleButton("##fav", ImVec2(px(22), px(22)))) toggleFav = p->name;
        const bool sh = ImGui::IsItemHovered();
        const char* g = fav ? icon::StarFill : icon::Star;
        drawText(d, FontKind::Icons, 12, starPos + ImVec2(px(5), px(4)), u32(fav ? pal::Accent : sh ? pal::Text : pal::Decor), g);
        if (sh) ImGui::SetTooltip(fav ? "Remove from favourites" : "Favourite (listed first)");
        const std::string name = ellipsize(p->name.c_str(), w - px(80), FontKind::Semibold, type::Label);
        drawText(d, FontKind::Semibold, type::Label, ImVec2(x, pos.y + px(7)), u32(sel ? pal::Text : pal::Soft), name.c_str());
        std::string sub = p->builtIn ? "Built-in" : "Yours";
        if (p->name == installedName) sub += "  \xC2\xB7  on the console";
        else if (p->name == armed) sub += "  \xC2\xB7  selected for the switch";
        drawText(d, FontKind::Body, type::Small, ImVec2(x, pos.y + px(26)), u32(p->name == installedName ? pal::Success : pal::Faint), sub.c_str());
        ImGui::SetCursorScreenPos(pos + ImVec2(0, h));
        ImGui::PopID();
    }
    if (rows.empty()) {
        gap(sp::L);
        caption("  No Preset Matches", pal::Faint);
    }
    ImGui::EndChild();
    if (!toggleFav.empty()) set.toggleInList(kFavKey, toggleFav);
    gap(sp::S);
    if (button("New", {.tone = Tone::Ghost, .icon = icon::Add, .small = true, .tooltip = "A new preset of your own (keeps nothing until you add rules)"})) {
        Preset p;
        p.name = pickup::PresetStore::get().uniqueName("My preset");
        p.note = "";
        p.keepOthers = false;
        Rule r;
        r.keep = true;
        r.classes = 1u << unsigned(pickup::ItemClass::Weapon);
        r.tiers = Rule::tierBit(4) | Rule::tierBit(5);
        p.rules.push_back(r);
        e.user.push_back(p);
        saveUser();
        selectPreset(p.name);
    }
    ImGui::SameLine(0, px(6));
    if (button("Duplicate", {.tone = Tone::Ghost, .icon = icon::Copy, .small = true, .tooltip = "Copy the selected preset into one you can edit"})) {
        if (const Preset* src = findAny(e.selected)) {
            Preset p = *src;
            p.builtIn = false;
            p.name = pickup::PresetStore::get().uniqueName(src->name + " (copy)");
            e.user.push_back(p);
            saveUser();
            selectPreset(p.name);
        }
    }
}

 

bool chip(const char* text, bool on, const char* tip = nullptr) {
    return button(text, {.tone = Tone::Ghost, .small = true, .tooltip = tip, .selected = on});
}

void itemPicker(Rule& r) {
    Editor& e = ed();
    ImGui::SetNextWindowSize(ImVec2(px(460), 0));
    if (!ImGui::BeginPopup("##itempicker")) return;
    fieldLabel("ADD AN ITEM");
    gap(sp::XS + 2);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    searchField("##pick", &e.pickerSearch, "Name, Type or Hash", 0, true);
    gap(sp::S);
    const auto hits = pickup::definitions().search(e.pickerSearch.size() >= 2 ? e.pickerSearch : "~~nothing~~", 40);
    if (e.pickerSearch.size() < 2) caption("Type at least two characters.", pal::Faint);
    else if (hits.empty()) caption("No Definition Matches", pal::Faint);
    ImGui::BeginChild("##hits", ImVec2(0, std::min(px(320), px(30) * float(hits.size()) + px(4))));
    for (const auto* d : hits) {
        ImGui::PushID(d->index);
        const bool already = std::find(r.items.begin(), r.items.end(), d->hash) != r.items.end();
        if (ImGui::Selectable("##h", false, already ? ImGuiSelectableFlags_Disabled : 0, ImVec2(0, px(26)))) {
            r.items.push_back(d->hash);
            saveUser();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine(px(8));
        label(d->label().c_str(), already ? pal::Faint : pal::Text);
        ImGui::SameLine();
        const std::string meta = fmt("%s  %s  #%u", pickup::tierName(d->tier), d->type.empty() ? pickup::className(d->cls) : d->type.c_str(), d->index);
        alignRight(textSize(FontKind::Body, type::Small, meta.c_str()).x + px(4));
        caption(meta.c_str(), pal::Faint);
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::EndPopup();
}

void bucketPicker(Rule& r) {
    ImGui::SetNextWindowSize(ImVec2(px(300), 0));
    if (!ImGui::BeginPopup("##buckets")) return;
    fieldLabel("BUCKETS (NONE = ANY)");
    gap(sp::XS + 2);
    ImGui::BeginChild("##bl", ImVec2(0, px(300)));
    bool changed = false;
    for (uint8_t b : pickup::knownBuckets()) {
        bool on = std::find(r.buckets.begin(), r.buckets.end(), b) != r.buckets.end();
        const std::string l = pickup::bucketName(b) + "##b" + std::to_string(b);
        if (checkbox(l.c_str(), &on)) {
            if (on) r.buckets.push_back(b);
            else r.buckets.erase(std::remove(r.buckets.begin(), r.buckets.end(), b), r.buckets.end());
            changed = true;
        }
    }
    ImGui::EndChild();
    if (changed) saveUser();
    ImGui::EndPopup();
}

 
bool drawRule(Rule& r, int index, int count, bool editable, int* move, int* remove) {
    ImGui::PushID(index);
    beginCard("##rule", nullptr, {.raised = true});
    bool changed = false;
    {
        DisabledScope ro(!editable, "Built-in presets are read-only: Duplicate to edit");
         
        captionf("%d", index + 1);
        ImGui::SameLine(0, px(10));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(7));
        int act = r.keep ? 0 : 1;
        if (pillTabs("##act", {"Keep", "Drop"}, &act)) {
            r.keep = act == 0;
            changed = true;
        }
        ImGui::SameLine();
        alignRight(px(28) * 3 + px(8));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(3));
        if (iconButton("##up", icon::ChevronUp, "Move up (earlier rules win)", false, 28) && index > 0) *move = -1;
        ImGui::SameLine(0, px(4));
        if (iconButton("##down", icon::ChevronDown, "Move down", false, 28) && index + 1 < count) *move = 1;
        ImGui::SameLine(0, px(4));
        if (iconButton("##del", icon::Delete, "Delete this rule", false, 28, pal::Danger)) *remove = index;
        gap(sp::S);
         
        caption("What", pal::Faint);
        ImGui::SameLine(px(76));
        for (int i = 0; i < int(pickup::ItemClass::Count); ++i) {
            if (i) ImGui::SameLine(0, px(4));
            const bool on = r.classes & (1u << i);
            const std::string l = std::string(pickup::classLabel(pickup::ItemClass(i))) + "##c" + std::to_string(i);
            if (chip(l.c_str(), on)) {
                r.classes ^= 1u << i;
                changed = true;
            }
        }
        if (!r.classes) {
            ImGui::SameLine(0, px(8));
            caption("(any)", pal::Faint);
        }
         
        caption("Tier", pal::Faint);
        ImGui::SameLine(px(76));
        const uint8_t tiers[] = {1, 2, 3, 4, 5, 6, 0};
        const char* tierLabels[] = {"Common", "Uncommon", "Rare", "Legendary", "Exotic", "Tier 6", "None"};
        for (int i = 0; i < 7; ++i) {
            if (i) ImGui::SameLine(0, px(4));
            const uint32_t bit = tiers[i] == 0 ? (Rule::tierBit(0) | Rule::tierBit(255)) : Rule::tierBit(tiers[i]);
            const bool on = (r.tiers & bit) == bit;
            const std::string l = std::string(tierLabels[i]) + "##t" + std::to_string(i);
            if (chip(l.c_str(), on, tiers[i] == 0 ? "No tier (0 and 255)" : nullptr)) {
                if (on) r.tiers &= ~bit;
                else r.tiers |= bit;
                changed = true;
            }
        }
        if (!r.tiers) {
            ImGui::SameLine(0, px(8));
            caption("(any)", pal::Faint);
        }
         
        caption("Where", pal::Faint);
        ImGui::SameLine(px(76));
        std::string bl;
        for (size_t i = 0; i < r.buckets.size(); ++i) bl += (i ? ", " : "") + pickup::bucketName(r.buckets[i]);
        if (bl.empty()) bl = "Any Bucket";
        const std::string bucketLabel = ellipsize(bl.c_str(), px(220), FontKind::Body, type::Label) + "##bk";
        if (button(bucketLabel.c_str(), {.tone = Tone::Secondary, .icon = icon::Filter, .small = true, .tooltip = "Limit to inventory buckets"}))
            ImGui::OpenPopup("##buckets");
        bucketPicker(r);
        ImGui::SameLine(0, px(10));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(3));
        std::string name = r.name;
        inputText("##name", &name, "Name Contains...", 200);
        tooltip("Only definitions whose name contains this text (any case)");
        if (name != r.name) {
            r.name = name;
            ++ed().editCount;            
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
         
        caption("Items", pal::Faint);
        ImGui::SameLine(px(76));
        int removeItem = -1;
        for (size_t i = 0; i < r.items.size(); ++i) {
            const auto* d = pickup::definitions().byHash(r.items[i]);
            const std::string l = (d ? d->label() + "  #" + std::to_string(d->index) : hex(r.items[i])) + "  " + icon::Close + "##i" + std::to_string(i);
            if (button(l.c_str(), {.tone = Tone::Secondary, .small = true,
                                   .tooltip = d ? fmt("%s, #%u, %s. Click to remove.", pickup::tierName(d->tier), d->index, hex(d->hash).c_str()).c_str()
                                                : "Click to remove"}))
                removeItem = int(i);
            ImGui::SameLine(0, px(4));
            if (ImGui::GetContentRegionAvail().x < px(140)) {
                ImGui::NewLine();
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + px(76) - ImGui::GetStyle().WindowPadding.x);
            }
        }
        if (button("Add Item##ai", {.tone = Tone::Ghost, .icon = icon::Add, .small = true, .tooltip = "Pick specific items by name"})) {
            ed().pickerSearch.clear();
            ImGui::OpenPopup("##itempicker");
        }
        itemPicker(r);
        if (removeItem >= 0) {
            r.items.erase(r.items.begin() + removeItem);
            changed = true;
        }
        gap(sp::XS);
        caption(r.describe().c_str(), r.keep ? pal::Success : pal::Danger);
    }
    endCard();
    ImGui::PopID();
    return changed;
}

void drawEditor(Ui& ui) {
    Editor& e = ed();
    Preset* editable = findEditable(e.selected);
    const Preset* p = editable ? editable : findAny(e.selected);
    if (!p) return;
    const bool connected = ui.connected();
    const std::string armed = armedName(ui.settings());
    bool installed;
    std::string installedName;
    {
        std::lock_guard<std::mutex> lock(live().m);
        installed = live().installed;
        installedName = live().preset;
    }
     
    if (editable) {
        std::string name = editable->name;
        const float w = std::max(px(200), availWidth() - px(330));
        if (inputText("##pname", &name, "Preset Name", w / scale(), ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit()) {
            if (!name.empty() && name != editable->name) {
                if (findAny(name)) {
                    notify("A preset is already called \"" + name + "\"", Level::Warn);
                } else {
                    const bool wasArmed = armed == editable->name;
                    editable->name = name;
                    e.selected = name;
                    saveUser();
                    if (wasArmed) ui.settings().set(std::string(kPresetKey), name);
                }
            }
        }
    } else {
        heading(p->name.c_str());
    }
    ImGui::SameLine();
    const bool isInstalled = installed && installedName == p->name;
    const float useW = buttonWidth("Use This Preset", icon::Check);
    const float delW = editable ? buttonWidth("Delete", icon::Delete) + px(6) : 0;
    alignRight(useW + delW);
    if (editable) {
        if (button("Delete", {.tone = Tone::Ghost, .icon = icon::Delete, .tooltip = "Delete this preset (asks first)"})) {
            const std::string name = p->name;
            confirm("Delete Preset", "Delete \"" + name + "\"? This cannot be undone.", "Delete", [name] {
                Editor& ee = ed();
                ee.user.erase(std::remove_if(ee.user.begin(), ee.user.end(), [&](const Preset& x) { return x.name == name; }), ee.user.end());
                saveUser();
                selectPreset(kDefaultPreset);
            });
        }
        ImGui::SameLine(0, px(6));
    }
    if (button(isInstalled ? "In Use##use" : "Use This Preset", {.tone = isInstalled ? Tone::Success : Tone::Primary, .icon = icon::Check,
                                                                 .tooltip = installed ? "Rewrite the console filter with this preset now"
                                                                                      : "Select it for the switch (the switch installs it)",
                                                                 .busy = ui.busy(kApplyJob)}) &&
        !isInstalled)
        usePreset(ui, p->name);
    (void)connected;
    gap(sp::XS);
    if (editable) {
        std::string note = editable->note;
        inputText("##note", &note, "Note (What This Preset Is For)", 0);
        if (ImGui::IsItemDeactivatedAfterEdit() && note != editable->note) {
            editable->note = note;
            saveUser();
        }
    } else {
        wrapped(p->note.empty() ? "Built-in preset." : p->note.c_str(), pal::Muted);
        tooltip("Built-in presets are read-only: Duplicate it to change the rules.");
    }
    gap(sp::M);
     
    fieldLabel("RULES  (THE FIRST MATCH DECIDES)");
    gap(sp::XS + 2);
    if (!editable) {
         
        for (size_t i = 0; i < p->rules.size(); ++i) {
            const Rule& r = p->rules[i];
            ImGui::PushID(int(i));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(3));
            captionf("%zu", i + 1);
            ImGui::SameLine(0, px(10));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(3));
            pill(r.keep ? "Keep" : "Drop", r.keep ? pal::Success : pal::Danger);
            ImGui::SameLine(0, px(10));
            std::string d = r.describe();
            d = d.substr(d.find(' ') + 1);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(3));
            wrapText(d.c_str(), availWidth(), pal::Text, type::Label);
            if (!r.items.empty()) {
                std::string names;
                for (size_t k = 0; k < r.items.size(); ++k) {
                    const auto* def = pickup::definitions().byHash(r.items[k]);
                    names += (k ? ", " : "") + (def ? def->label() + " #" + std::to_string(def->index) : hex(r.items[k]));
                }
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + px(26));
                wrapText(names.c_str(), availWidth() - px(26), pal::Muted);
            }
            gap(sp::XS);
            ImGui::PopID();
        }
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(3));
        caption(p->rules.empty() ? "Everything" : "Everything Else", pal::Faint);
        ImGui::SameLine(0, px(10));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(3));
        pill(p->keepOthers ? "Keep" : "Drop", p->keepOthers ? pal::Success : pal::Muted);
        return;
    }
    Preset& rp = *editable;
    int move = 0, moveIndex = -1, remove = -1;
    bool changed = false;
    for (int i = 0; i < int(rp.rules.size()); ++i) {
        int mv = 0;
        if (drawRule(rp.rules[size_t(i)], i, int(rp.rules.size()), true, &mv, &remove)) changed = true;
        if (mv) {
            move = mv;
            moveIndex = i;
        }
        gap(sp::S);
    }
    if (rp.rules.empty()) caption("No rules: everything follows \"Everything Else\" below.", pal::Faint);
    {
        if (button("Add Rule", {.tone = Tone::Ghost, .icon = icon::Add, .small = true})) {
            Rule r;
            r.keep = !rp.keepOthers;
            rp.rules.push_back(r);
            changed = true;
        }
        ImGui::SameLine(0, px(16));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(4));
        caption("Everything Else", pal::Muted);
        ImGui::SameLine(0, px(10));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(4));
        int others = rp.keepOthers ? 0 : 1;
        if (pillTabs("##others", {"Keep", "Drop"}, &others)) {
            rp.keepOthers = others == 0;
            changed = true;
        }
    }
    if (move && moveIndex >= 0) {
        std::swap(rp.rules[size_t(moveIndex)], rp.rules[size_t(moveIndex + move)]);
        changed = true;
    }
    if (remove >= 0 && remove < int(rp.rules.size())) {
        rp.rules.erase(rp.rules.begin() + remove);
        changed = true;
    }
    if (changed) saveUser();
}

 

void rebuildView() {
    Editor& e = ed();
    e.view.clear();
    const auto& cat = pickup::definitions();
    std::vector<const pickup::Definition*> hits;
    if (!e.defSearch.empty()) hits = cat.search(e.defSearch);
    auto consider = [&](const pickup::Definition& d) {
        const bool kept = pickup::allowed(e.bitmap, d.index);
        if (e.defFilter == 1 && !kept) return;
        if (e.defFilter == 2 && kept) return;
        e.view.push_back(d.index);
    };
    if (e.defSearch.empty())
        for (const auto& d : cat.all()) consider(d);
    else
        for (const auto* d : hits) consider(*d);
    e.viewDirty = false;
}

void drawPreview() {
    Editor& e = ed();
    const auto& cat = pickup::definitions();
    if (!cat.error.empty()) {
        caption(cat.error.c_str(), pal::Danger);
        return;
    }
    refreshPreview();
     
    {
        FontScope f(FontKind::Semibold, type::Stat);
        ImGui::TextColored(pal::Text, "%s", grouped(e.kept).c_str());
    }
    ImGui::SameLine(0, px(8));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(5));
    captionf("of %s definitions picked up", grouped(cat.size()).c_str());
    gap(sp::XS);
    const float w = availWidth();
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = px(6);
        ImGui::Dummy(ImVec2(w, h));
        ImDrawList* d = ImGui::GetWindowDrawList();
        d->AddRectFilled(p, p + ImVec2(w, h), u32(pal::FillActive), h);
        const ImVec4 colors[] = {pal::Accent, pal::Info, rgb(0xB48CF2), pal::Success, pal::Muted};
        float x = p.x;
        for (size_t i = 0; i < size_t(pickup::ItemClass::Count); ++i) {
            const float seg = w * float(e.keptByClass[i]) / float(std::max<size_t>(1, cat.size()));
            if (seg > 0.5f) d->AddRectFilled(ImVec2(x, p.y), ImVec2(x + seg, p.y + h), u32(colors[i]), 0);
            x += seg;
        }
        gap(sp::XS);
        float lineX = 0;
        for (size_t i = 0; i < size_t(pickup::ItemClass::Count); ++i) {
            const std::string t = fmt("%s %s / %s", pickup::classLabel(pickup::ItemClass(i)), grouped(e.keptByClass[i]).c_str(),
                                      grouped(e.totalByClass[i]).c_str());
            const float itemW = px(14) + textSize(FontKind::Body, type::Small, t.c_str()).x;
            if (i && lineX + px(14) + itemW <= w) {
                ImGui::SameLine(0, px(14));
                lineX += px(14);
            } else if (i) {
                lineX = 0;
            }
            lineX += itemW;
            statusDot(colors[i]);
            ImGui::SameLine(0, px(5));
            caption(t.c_str(), pal::Muted);
        }
    }
    gap(sp::M);
    if (searchField("##defs", &e.defSearch, "Search Definitions (Name, Type, Tier, Bucket, Hash)", std::min(px(380), w * 0.55f) / scale())) e.viewDirty = true;
    ImGui::SameLine(0, px(10));
    if (pillTabs("##deff", {"All", "Picked Up", "Left Behind"}, &e.defFilter)) e.viewDirty = true;
    if (e.viewDirty) rebuildView();
    ImGui::SameLine();
    const std::string count = grouped(e.view.size()) + " shown";
    alignRight(textSize(FontKind::Body, type::Small, count.c_str()).x);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(8));
    caption(count.c_str(), pal::Faint);
    gap(sp::S);
    if (beginTable("##defs", 5, 300, ImGuiTableFlags_BordersOuter)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Decision", ImGuiTableColumnFlags_WidthFixed, px(118));
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Tier", ImGuiTableColumnFlags_WidthFixed, px(78));
        ImGui::TableSetupColumn("Bucket", ImGuiTableColumnFlags_WidthFixed, px(120));
        ImGui::TableSetupColumn("Hash", ImGuiTableColumnFlags_WidthFixed, px(84));
        tableHeaders();
        ImGuiListClipper clip;
        clip.Begin(int(e.view.size()), px(kRowH));
        while (clip.Step()) {
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
                const auto* d = cat.at(uint32_t(e.view[size_t(r)]));
                if (!d) continue;
                ImGui::TableNextRow(0, px(kRowH));
                const bool kept = pickup::allowed(e.bitmap, d->index);
                const int rule = e.decided.empty() ? -1 : e.decided[d->index];
                ImGui::TableSetColumnIndex(0);
                const std::string dec = std::string(kept ? "Pick up" : "Leave") + (rule >= 0 ? fmt(" (rule %d)", rule + 1) : std::string(" (else)"));
                cellText(dec.c_str(), kept ? pal::Success : pal::Faint, FontKind::Body, type::Small, kRowH);
                ImGui::TableSetColumnIndex(1);
                cellText(d->label().c_str(), kept ? pal::Text : pal::Muted, FontKind::Body, type::Label, kRowH);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("%s\n%s, %s, definition #%u", d->label().c_str(), d->type.empty() ? pickup::className(d->cls) : d->type.c_str(),
                                      pickup::bucketName(d->bucket).c_str(), d->index);
                ImGui::TableSetColumnIndex(2);
                const ImVec4 tc = d->tier == 5 ? rgb(0xF2C14E) : d->tier == 4 ? rgb(0xB48CF2) : d->tier == 3 ? pal::Info : d->tier == 2 ? pal::Success : pal::Muted;
                cellText(pickup::tierName(d->tier), tc, FontKind::Body, type::Small, kRowH);
                ImGui::TableSetColumnIndex(3);
                cellText(pickup::bucketName(d->bucket).c_str(), pal::Muted, FontKind::Body, type::Small, kRowH);
                ImGui::TableSetColumnIndex(4);
                cellText(hex(d->hash).c_str(), pal::Faint, FontKind::Mono, type::MonoSmall, kRowH);
            }
        }
        endTable();
    }
}

void drawFilter(Ui& ui) {
    ensureEditor(ui);
    drawStatus(ui);
    if (const std::string err = pickup::PresetStore::get().error(); !err.empty()) checkRow(err.c_str(), Mark::Bad);
    gap(sp::M);
    const float avail = availWidth();
    const bool split = avail >= px(900);
    const float listW = split ? std::clamp(avail * 0.28f, px(240), px(320)) : avail;
    const float rightW = split ? avail - listW - px(16) : avail;
    ImGui::BeginGroup();
    fieldLabel("PRESETS");
    gap(sp::XS + 2);
    drawPresetList(ui, listW, split ? px(520) : px(260));
    ImGui::EndGroup();
    if (split) ImGui::SameLine(0, px(16));
    else gap(sp::M);
    beginPanel("##editor", rightW);
    drawEditor(ui);
    divider(sp::L);
    fieldLabel("PREVIEW");
    gap(sp::XS + 2);
    drawPreview();
    endPanel();
}

void resetOnDisconnect() {
    std::lock_guard<std::mutex> lock(live().m);
    live().installed = false;
    live().tableLine.clear();
}

}   

Summary pickupSummary() {
    Summary s;
    std::lock_guard<std::mutex> lock(live().m);
    if (live().installed) {
        s.active = true;
        s.color = pal::Success;
        s.value = "On";
        s.sub = fmt("%s, %s kept", live().preset.c_str(), grouped(live().kept).c_str());
    } else {
        s.value = "Off";
        s.sub = "The game decides every pickup";
    }
    return s;
}

}   

namespace brps3 {

BR_FEATURES(loot_pickup) {
    using namespace lootui;
    r.toggle({.id = kToggleId, .page = Page::Loot, .tab = kTabPickup, .section = kSection, .label = "Pickup Filter",
              .help = "Only pick up what the selected preset keeps (e.g. legendary loot only). Off gives the decision back to the game.",
              .set = setFilter});
    r.section({.id = "loot.pickup.presets", .page = Page::Loot, .tab = kTabPickup, .section = kSection, .order = 10, .draw = drawFilter,
               .onDisconnect = resetOnDisconnect});
     
    static const char* const kBuiltIns[] = {"Legendary loot only", "Legendary and exotic", "Exotics only", "Imago Loop only", "Morgue only",
                                            "Saterienne only", "Morgue and Saterienne", "No blues or greens", "Engrams only", "Legendary weapons only"};
    for (const char* name : kBuiltIns) {
        const std::string id = std::string("loot.pickup.preset.") + name;
        const std::string n = name;
        r.action({.id = id, .page = Page::Loot, .tab = kTabPickup, .section = kSection, .label = std::string("Pickup Preset: ") + name,
                  .help = "Select this pickup filter preset; when the filter is on, the console bitmap is rewritten at once.",
                  .run = [n](Ctx& c) {
                      c.settings().set(std::string(kPresetKey), n);
                      bool installed;
                      {
                          std::lock_guard<std::mutex> lock(live().m);
                          installed = live().installed;
                      }
                      if (installed) install(c, n);
                      else c.toast("\"" + n + "\" selected: turn the Pickup filter switch on to install it");
                  }});
    }
    ui::styleSection(kSection, {.fullWidth = true,
                                .icon = ui::icon::Filter,
                                .ownsActions = true});
}

}   
