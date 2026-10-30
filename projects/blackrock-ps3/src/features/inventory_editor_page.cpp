 
 
 
 
 
 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

#include "app/engine.h"
#include "app/registry.h"
#include "app/settings.h"
#include "core/error.h"
#include "core/runtime.h"
#include "features/inventory_shared.h"
#include "game/inventory_addr.h"
#include "game/inventory_editor.h"
#include "game/world_mem.h"
#include "ui/widgets.h"

namespace brps3::invui {

using namespace inventory;
using namespace ui;

namespace {

constexpr const char* kReadJob = "Read Live Configuration";
constexpr const char* kApplyJob = "Apply editor change";
constexpr const char* kUndoJob = "Undo Last Edit";
constexpr const char* kPresetDir = "inventory/presets";

Settings& settings() { return Engine::get().settings(); }

 
struct Ledger {
    uint64_t character = 0;
    std::array<uint8_t, 160> equipped{};
    std::array<uint8_t, 64> aggregate{};
    std::map<uint64_t, std::vector<uint16_t>> contributions;
};

struct Ed {
    std::mutex m;                        
    std::shared_ptr<const EditorSnapshot> snap;
    std::string status = "Read live configuration to begin";
    std::string error;
    std::vector<Applied> applied;        
    Ledger ledger;
     
    int page = 0, slot = 6, ability = 0;
    uint64_t item = 0;
    EditRequest draft;
    std::string draftKey;                
    std::string itemSearch, presetName;
    std::vector<std::string> presets;
    bool presetsLoaded = false;
    bool showChanges = true;
    bool demoSeeded = false;
};

Ed& ed() {
    static Ed e;
    return e;
}

std::filesystem::path presetDir() { return state_dir() / kPresetDir; }

void loadPresets(Ed& e) {
    e.presets.clear();
    e.presetsLoaded = true;
    std::error_code ec;
    for (const auto& f : std::filesystem::directory_iterator(presetDir(), ec))
        if (f.path().extension() == ".txt") e.presets.push_back(f.path().stem().string());
    std::sort(e.presets.begin(), e.presets.end());
}

void journal(const Applied& a, const char* what) {
    std::error_code ec;
    std::filesystem::create_directories(state_dir() / "inventory", ec);
    std::ofstream f(state_dir() / "inventory" / "editor-journal.txt", std::ios::app);
    if (!f) return;
    f << a.when << " " << what << " " << editModeName(a.plan.request.mode) << " character " << hex16(a.character) << "\n";
    for (size_t i = 0; i < a.plan.changes.size(); ++i) {
        const Change& c = a.plan.changes[i];
        f << "  " << (i < a.written ? "written " : "pending ") << world::hex8(c.addr) << " " << c.name << " " << hexBytes(c.before) << " -> "
          << hexBytes(c.after) << "\n";
    }
}

 

void requireEditor(Ctx& c) {
    c.require(kReadFingerprints, "Inventory reader");
    c.require(kEditorFingerprints, "Live editor");
}

std::shared_ptr<const EditorSnapshot> readLive(Ctx& c) {
    requireEditor(c);
    const EditorCatalog& ec = editorCatalog();
    if (!ec.error.empty()) throw Error("editor catalogue unavailable: " + ec.error);
    Io& io = c.state<Io>("inventory.io");
    auto s = std::make_shared<EditorSnapshot>();
    {
        std::lock_guard<std::mutex> lock(io.m);
        world::RuntimeReader mem(c.rt());
        *s = editorRead(mem, io.cache, ec, catalog());
    }
    Ed& e = ed();
    std::lock_guard<std::mutex> lock(e.m);
    if (e.ledger.character == s->characterSoid && e.ledger.equipped == s->equipped && e.ledger.aggregate == s->aggregateKey())
        s->contributions = e.ledger.contributions;
    e.snap = s;
    e.error.clear();
    e.status = "Live configuration read (" + std::to_string(s->items.size()) + " item details, " + std::to_string(s->profiles.size()) +
               " profile copies)";
    return s;
}

size_t revertAll(Ctx& c, const char* why) {
    Ed& e = ed();
    std::vector<Applied> list;
    {
        std::lock_guard<std::mutex> lock(e.m);
        list.swap(e.applied);
    }
    if (list.empty()) return 0;
    world::RuntimeReader mem(c.rt());
    WriteFn write = [&c](uint32_t a, const std::vector<uint8_t>& b) { c.rt().write(a, b.data(), b.size(), true); };
    size_t restored = 0;
    std::vector<std::string> skipped;
    for (size_t i = list.size(); i-- > 0;) {
        restored += editorRevert(mem, write, list[i], &skipped);
        journal(list[i], why);
    }
    c.log("[editor] " + std::string(why) + ": restored " + std::to_string(restored) + " field(s)" +
          (skipped.empty() ? "" : ", left " + std::to_string(skipped.size()) + " alone (no longer ours)"));
    for (const std::string& s : skipped) c.log("[editor]   skipped " + s);
    std::lock_guard<std::mutex> lock(e.m);
    e.status = std::string(why) + ": restored " + std::to_string(restored) + " field(s)";
    return restored;
}

void registerTeardown(Ctx& c) {
     
    struct Flag {
        bool done = false;
    };
    Flag& f = c.state<Flag>("inventory.editor.teardown");
    c.onPanic("inventory.editor", [](Ctx& pc) {
        if (Engine::get().settings().flag(kRevertKey, true)) revertAll(pc, "Panic");
    });
    if (f.done) return;
    f.done = true;
    c.onDisconnect([](Ctx& dc) {
        if (Engine::get().settings().flag(kRevertKey, true)) {
            try {
                revertAll(dc, "Disconnect");
            } catch (const std::exception& ex) {
                dc.log(std::string("[editor] restore on disconnect failed: ") + ex.what());
            }
        }
    });
}

void applyJob(Ctx& c, std::shared_ptr<const EditorSnapshot> snap, EditRequest draft) {
    requireEditor(c);
    if (!snap) throw Error("read the live configuration first");
    const EditorCatalog& ec = editorCatalog();
    const Catalog& cat = catalog();
    const EditPlan plan = editorPlan(*snap, draft, ec, cat);
    if (plan.changes.empty()) throw Error("nothing to apply: the draft matches the live values");
    registerTeardown(c);
    Io& io = c.state<Io>("inventory.io");
    WriteFn write = [&c](uint32_t a, const std::vector<uint8_t>& b) { c.rt().write(a, b.data(), b.size(), true); };
    Applied done, partial;
    try {
        std::lock_guard<std::mutex> lock(io.m);
        world::RuntimeReader mem(c.rt());
        done = editorCommit(mem, write, io.cache, *snap, plan, ec, cat, &partial);
    } catch (...) {
        if (partial.written) {
            Ed& e = ed();
            std::lock_guard<std::mutex> lock(e.m);
            e.applied.push_back(partial);    
            journal(partial, "partial");
        }
        throw;
    }
    journal(done, "applied");
    {
        Ed& e = ed();
        std::lock_guard<std::mutex> lock(e.m);
        e.applied.push_back(done);
         
        if (draft.mode == EditMode::Equipment && aggregateSlot(draft.slot)) {
            e.ledger.contributions = snap->contributions;
            e.ledger.contributions[draft.soid] = nonEmpty(draft.perks);
        } else if (draft.mode == EditMode::Character) {
            e.ledger.contributions.clear();
        }
    }
    c.log("[editor] applied " + std::to_string(done.written) + " field(s): " + std::string(editModeName(draft.mode)));
    const auto fresh = readLive(c);
    {
        Ed& e = ed();
        std::lock_guard<std::mutex> lock(e.m);
        e.ledger.character = fresh->characterSoid;
        e.ledger.equipped = fresh->equipped;
        e.ledger.aggregate = fresh->aggregateKey();
        e.status = "Applied and read back " + std::to_string(done.written) + " field(s). Re-equip or switch weapons if gameplay needs a rebuild.";
    }
    c.toast("Editor: applied " + std::to_string(done.written) + " field(s)");
}

void undoJob(Ctx& c) {
    Ed& e = ed();
    Applied last;
    {
        std::lock_guard<std::mutex> lock(e.m);
        if (e.applied.empty()) throw Error("no edit to undo in this session");
        last = e.applied.back();
        e.applied.pop_back();
    }
    world::RuntimeReader mem(c.rt());
    WriteFn write = [&c](uint32_t a, const std::vector<uint8_t>& b) { c.rt().write(a, b.data(), b.size(), true); };
    std::vector<std::string> skipped;
    const size_t n = editorRevert(mem, write, last, &skipped);
    journal(last, "undone");
    for (const std::string& s : skipped) c.log("[editor] undo skipped " + s);
    if (!skipped.empty() && n == 0) throw Error("undo refused: " + skipped.front());
    readLive(c);
    std::lock_guard<std::mutex> lock(e.m);
    e.status = "Undone: restored " + std::to_string(n) + " field(s)" + (skipped.empty() ? "" : " (" + std::to_string(skipped.size()) + " left alone)");
}

 

struct PickItem {
    int value = 0;
    std::string label, hay;
};

const std::vector<PickItem>& perkItems() {
    static std::vector<PickItem> v;
    if (v.empty())
        for (const Perk& p : catalog().perks())
            if (!p.name.empty()) v.push_back({p.ordinal, std::to_string(p.ordinal) + "  " + p.name, lowerAscii(p.name + " " + p.description + " " + std::to_string(p.ordinal))});
    return v;
}
const std::vector<PickItem>& patternItems() {
    static std::vector<PickItem> v;
    if (v.empty())
        for (const Pattern& p : editorCatalog().patterns) v.push_back({p.index, p.label, lowerAscii(p.label + " " + p.search)});
    return v;
}
const std::vector<PickItem>& artItems() {
    static std::vector<PickItem> v;
    if (v.empty())
        for (const Art& a : editorCatalog().art) v.push_back({a.index, a.label, lowerAscii(a.label + " " + a.search)});
    return v;
}
const std::vector<PickItem>& gridItems() {
    static std::vector<PickItem> v;
    if (v.empty())
        for (const Grid& g : editorCatalog().grids) v.push_back({g.index, g.label, lowerAscii(g.label)});
    return v;
}
const std::vector<PickItem>& abilityItems() {
    static std::vector<PickItem> v;
    if (v.empty())
        for (const Choice& c : editorCatalog().abilities) v.push_back({int(c.key), c.label, lowerAscii(c.label)});
    return v;
}
const std::vector<PickItem>& modifierItems() {
    static std::vector<PickItem> v;
    if (v.empty())    
        for (const Choice& c : editorCatalog().modifiers) v.push_back({int(c.key), world::hex8(c.key), lowerAscii(world::hex8(c.key) + " " + c.label)});
    return v;
}

std::string pickLabel(const std::vector<PickItem>& items, int value, int emptyValue, const char* emptyLabel) {
    if (value == emptyValue) return emptyLabel;
    for (const PickItem& p : items)
        if (p.value == value) return p.label;
    char b[40];
    if (value < 0 || value > 0xFFFF) std::snprintf(b, sizeof b, "%08X (not in the catalogue)", uint32_t(value));
    else std::snprintf(b, sizeof b, "%d (not in the catalogue)", value);
    return b;
}

 
bool picker(const char* id, int* value, const std::vector<PickItem>& items, int emptyValue, const char* emptyLabel, float width) {
    static std::unordered_map<ImGuiID, std::string> filters;
    ImGui::PushID(id);
    const ImGuiID pid = ImGui::GetID("##pick");
    const std::string text = pickLabel(items, *value, emptyValue, emptyLabel);
    const float w = width > 0 ? px(width) : availWidth();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = px(30);
    const bool clicked = ImGui::InvisibleButton("##btn", ImVec2(w, h));
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(p, p + ImVec2(w, h), u32(hov ? pal::RaisedHi : pal::Input), px(kFrameRounding));
    d->AddRect(p, p + ImVec2(w, h), u32(hov ? pal::FillHover : pal::InputBorder), px(kFrameRounding), 0, 1.0f);
    const std::string shown = ellipsize(text.c_str(), w - px(34), FontKind::Body, type::Small);
    drawText(d, FontKind::Body, type::Small, ImVec2(p.x + px(10), textTop(FontKind::Body, type::Small, p.y + h * 0.5f)),
             u32(*value == emptyValue ? pal::Faint : pal::Text), shown.c_str());
    const ImVec2 cs = textSize(FontKind::Icons, 10, icon::ChevronDown);
    drawText(d, FontKind::Icons, 10, ImVec2(p.x + w - cs.x - px(10), p.y + (h - cs.y) * 0.5f), u32(pal::Muted), icon::ChevronDown);
    if (hov && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && shown != text) ImGui::SetTooltip("%s", text.c_str());
    if (clicked) {
        filters[pid].clear();
        ImGui::OpenPopup(pid);
    }
    bool changed = false;
    ImGui::SetNextWindowPos(ImVec2(p.x, p.y + h + px(4)), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(std::max(w, px(320)), px(380)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(8), px(8)));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, px(10));
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableRounding, px(6));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, pal::RaisedFill);
    ImGui::PushStyleColor(ImGuiCol_Border, pal::Border);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, pal::RaisedHi);
    if (ImGui::BeginPopupEx(pid, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar)) {
        std::string& f = filters[pid];
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        searchField("##filter", &f, "Type a Name or Index", -1, true);
        gap(sp::XS);
        const std::string q = lowerAscii(f);
        std::vector<const PickItem*> shownItems;
        for (const PickItem& it : items)
            if (q.empty() || it.hay.find(q) != std::string::npos) shownItems.push_back(&it);
        ImGui::BeginChild("##list", ImVec2(0, 0));
        FontScope fs(FontKind::Body, type::Small);
        if (q.empty() && ImGui::Selectable(emptyLabel, *value == emptyValue)) {
            *value = emptyValue;
            changed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGuiListClipper clip;
        clip.Begin(int(shownItems.size()));
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                const PickItem& it = *shownItems[size_t(i)];
                ImGui::PushID(i);
                if (ImGui::Selectable(it.label.c_str(), *value == it.value)) {
                    *value = it.value;
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
        if (shownItems.empty()) caption("No Match", pal::Faint);
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) && shownItems.size() == 1) {
            *value = shownItems.front()->value;
            changed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(3);
    ImGui::PopID();
    return changed;
}

 
bool perkGrid(const char* id, std::vector<uint16_t>& perks, size_t count) {
    while (perks.size() < count) perks.push_back(0xFFFF);
    bool changed = false;
    ImGui::PushID(id);
    const float colW = (availWidth() - px(12)) * 0.5f;
    for (size_t i = 0; i < count; ++i) {
        ImGui::PushID(int(i));
        if (i % 2) ImGui::SameLine(0, px(12));
        ImGui::BeginGroup();
        char n[8];
        std::snprintf(n, sizeof n, "%02zu", i + 1);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        drawText(ImGui::GetWindowDrawList(), FontKind::Mono, type::MonoSmall, ImVec2(p.x, textTop(FontKind::Mono, type::MonoSmall, p.y + px(15))),
                 u32(pal::Faint), n);
        ImGui::SetCursorScreenPos(ImVec2(p.x + px(26), p.y));
        int v = perks[i];
        if (picker("##p", &v, perkItems(), 0xFFFF, "Empty", (colW - px(26)) / scale())) {
            perks[i] = uint16_t(v);
            changed = true;
        }
        ImGui::EndGroup();
        ImGui::PopID();
    }
    ImGui::PopID();
    return changed;
}

std::string itemLabelOf(const EditorSnapshot& s, uint64_t soid, uint16_t def) {
    (void)s;
    (void)soid;
    const ItemDef* d = catalog().item(def);
    return d ? d->label() : "Definition " + std::to_string(def);
}

 

void listRow(const char* id, const std::string& title, const std::string& sub, bool selected, bool* clicked, float width) {
    ImGui::PushID(id);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = px(44);
    *clicked = ImGui::InvisibleButton("##row", ImVec2(width, h));
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* d = ImGui::GetWindowDrawList();
    if (selected || hov) d->AddRectFilled(p, p + ImVec2(width, h), u32(selected ? pal::RaisedHi : pal::Raised), px(kRowRounding));
    if (selected) d->AddRectFilled(p + ImVec2(0, px(10)), p + ImVec2(px(3), h - px(10)), u32(pal::Accent), px(2));
    drawText(d, FontKind::Semibold, type::Small, p + ImVec2(px(12), px(6)), u32(selected ? pal::Text : pal::Soft), title.c_str());
    const std::string s2 = ellipsize(sub.c_str(), width - px(20), FontKind::Body, type::Tiny);
    drawText(d, FontKind::Body, type::Tiny, p + ImVec2(px(12), px(24)), u32(pal::Muted), s2.c_str());
    ImGui::PopID();
}

void pageEquipment(Ed& e, const EditorSnapshot& s, float height) {
    const float listW = px(250);
    ImGui::BeginChild("##slots", ImVec2(listW, height));
    for (int i = 0; i < 20; ++i) {
        const SlotState& st = s.slots[size_t(i)];
        const std::string sub = st.soid ? itemLabelOf(s, st.soid, st.def) : std::string("Empty");
        bool clicked = false;
        listRow(kSlotNames[i], kSlotNames[i], sub, e.slot == i, &clicked, listW - px(10));
        if (clicked) e.slot = i;
    }
    ImGui::EndChild();
    ImGui::SameLine(0, px(16));
    ImGui::BeginChild("##slotcfg", ImVec2(0, height));
    const SlotState& st = s.slots[size_t(e.slot)];
    heading(kSlotNames[e.slot]);
    if (!st.soid) {
        caption("This slot is empty: nothing to edit.", pal::Faint);
        ImGui::EndChild();
        return;
    }
    captionf("%s  \xC2\xB7  %s", itemLabelOf(s, st.soid, st.def).c_str(), hex16(st.soid).c_str());
    gap(sp::M);
    const float half = (availWidth() - px(12)) * 0.5f / scale();
    ImGui::BeginGroup();
    fieldLabel("PATTERN");
    gap(sp::XS);
    int pattern = e.draft.pattern;
    if (picker("##pattern", &pattern, patternItems(), 0xFFFF, "Empty / FFFF", half)) e.draft.pattern = uint16_t(pattern);
    ImGui::EndGroup();
    ImGui::SameLine(0, px(12));
    ImGui::BeginGroup();
    fieldLabel("ART ARRANGEMENT / MODEL");
    gap(sp::XS);
    int art = e.draft.art;
    if (picker("##art", &art, artItems(), 0xFFFF, "Empty / FFFF", half)) e.draft.art = uint16_t(art);
    ImGui::EndGroup();
    gap(sp::M);
    if (weaponSlot(e.slot)) {
        fieldLabel("WEAPON PERKS  (16)");
        gap(sp::XS + 2);
        perkGrid("##wp", e.draft.perks, 16);
    } else if (aggregateSlot(e.slot)) {
        fieldLabel("PERK CONTRIBUTION");
        tooltip("Replaces this item's share of the 32 character perks; the other items' perks are kept.");
        gap(sp::XS + 2);
        perkGrid("##cp", e.draft.perks, 16);
    }
    if (s.item(st.soid)) {
        gap(sp::M);
        if (button("Edit This Item's Talents", {.tone = Tone::Ghost, .icon = icon::Sliders, .small = true})) {
            e.page = 3;
            e.item = st.soid;
        }
    }
    ImGui::EndChild();
}

void pageAbility(Ed& e, const EditorSnapshot& s, float height) {
    const float listW = px(250);
    ImGui::BeginChild("##abl", ImVec2(listW, height));
    for (int i = 0; i < 5; ++i) {
        const int sel = s.abilities[size_t(i)][0];
        const std::string sub = sel == 255 ? std::string("No Ability") : pickLabel(abilityItems(), sel, 255, "No Ability");
        bool clicked = false;
        listRow(kAbilityNames[i], kAbilityNames[i], sub, e.ability == i, &clicked, listW - px(10));
        if (clicked) e.ability = i;
    }
    ImGui::EndChild();
    ImGui::SameLine(0, px(16));
    ImGui::BeginChild("##ablcfg", ImVec2(0, height));
    heading(kAbilityNames[e.ability]);
    gap(sp::M);
    fieldLabel("ABILITY SELECTOR");
    gap(sp::XS);
    int sel = e.draft.selector == 255 ? 0xFFFF : e.draft.selector;
    if (picker("##sel", &sel, abilityItems(), 0xFFFF, "No Ability / FF", 360)) e.draft.selector = uint8_t(sel == 0xFFFF ? 255 : sel);
    tooltip("Updates the selected ability in every owned profile copy. Subclass talent selections are edited under Item talents.");
    gap(sp::M);
    fieldLabel("MODIFIER KEYS  (16)");
    gap(sp::XS + 2);
    const float colW = (availWidth() - px(12)) * 0.5f / scale();
    for (int i = 0; i < 16; ++i) {
        ImGui::PushID(i);
        if (i % 2) ImGui::SameLine(0, px(12));
        int v = int(e.draft.modifiers[size_t(i)]);
        if (picker("##m", &v, modifierItems(), int(kDefaultUniq), "811C9DC5  (None)", colW)) e.draft.modifiers[size_t(i)] = uint32_t(v);
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void pageCharacter(Ed& e, float height) {
    ImGui::BeginChild("##chr", ImVec2(0, height));
    perkGrid("##char", e.draft.perks, 32);
    ImGui::EndChild();
}

void pageTalents(Ed& e, const EditorSnapshot& s, float height) {
    const Catalog& cat = catalog();
    const EditorCatalog& ec = editorCatalog();
    const float listW = px(280);
    ImGui::BeginGroup();
    searchField("##itemsearch", &e.itemSearch, "Search Items", listW / scale() - 10, false);
    gap(sp::XS);
    ImGui::BeginChild("##items", ImVec2(listW, height - px(42)));
    const std::string q = lowerAscii(e.itemSearch);
    for (const EditorItem& it : s.items) {
        const std::string name = itemLabelOf(s, it.soid, it.def);
        if (!q.empty() && lowerAscii(name + " " + hex16(it.soid)).find(q) == std::string::npos) continue;
        const std::string sub = hex16(it.soid) + (it.slot >= 0 ? std::string("  \xC2\xB7  ") + kSlotNames[it.slot] : std::string());
        bool clicked = false;
        listRow(hex16(it.soid).c_str(), name, sub, e.item == it.soid, &clicked, listW - px(10));
        if (clicked) e.item = it.soid;
    }
    ImGui::EndChild();
    ImGui::EndGroup();
    ImGui::SameLine(0, px(16));
    ImGui::BeginChild("##talcfg", ImVec2(0, height));
    const EditorItem* item = s.item(e.draft.soid);
    if (!item) {
        caption("Pick an Item on the Left", pal::Faint);
        ImGui::EndChild();
        return;
    }
    heading(itemLabelOf(s, item->soid, item->def).c_str());
    captionf("%s  \xC2\xB7  detail row %s", hex16(item->soid).c_str(), world::hex8(item->addr).c_str());
    gap(sp::M);
    fieldLabel("TALENT GRID");
    gap(sp::XS);
    int grid = e.draft.talents.grid;
    if (picker("##grid", &grid, gridItems(), 0xFFFF, "No Grid / FFFF", 420)) {
        e.draft.talents.grid = uint16_t(grid);
        e.draft.talents.nodes.fill(0);
        for (auto& t : e.draft.talents.abilities) t = {255, 255, 255};
    }
    gap(sp::M);
    Talents& t = e.draft.talents;
    if (t.grid != 0xFFFF && t.grid < ec.grids.size()) {
        fieldLabel("NODES  (rank: 0 = locked)");
        gap(sp::XS + 2);
        for (const GridNode& n : ec.grids[t.grid].nodes) {
            if (n.index < 0 || n.index >= 36) continue;
            ImGui::PushID(n.index);
            int rank = t.nodes[size_t(n.index)];
            char lbl[16];
            std::snprintf(lbl, sizeof lbl, "Node %d", n.index);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            drawText(ImGui::GetWindowDrawList(), FontKind::Semibold, type::Small, ImVec2(p.x, textTop(FontKind::Semibold, type::Small, p.y + px(17))),
                     u32(rank ? pal::Text : pal::Muted), lbl);
            ImGui::SetCursorScreenPos(ImVec2(p.x + px(70), p.y));
            if (inputInt("##rank", &rank, 0, int(n.groups.size()), 120)) t.nodes[size_t(n.index)] = uint8_t(rank);
            ImGui::SameLine(0, px(12));
            std::string perks;
            try {
                const int vi = nodeVariant(n, t);
                std::set<uint16_t> seen;
                for (const GridGroup& g : n.groups)
                    if (vi < int(g.steps.size()))
                        for (uint16_t pk : g.steps[size_t(vi)].perks)
                            if (const Perk* pp = cat.perk(pk); pp && !pp->name.empty() && seen.insert(pk).second)
                                perks += (perks.empty() ? "" : ", ") + pp->name;
            } catch (const std::exception& ex) {
                perks = ex.what();
            }
            caption(perks.empty() ? "(no named perks)" : perks.c_str(), perks.empty() ? pal::Faint : pal::Muted);
            ImGui::PopID();
        }
    }
    gap(sp::M);
    fieldLabel("ROLL SELECTORS  (8 bytes)");
    gap(sp::XS + 2);
    for (int i = 0; i < 8; ++i) {
        ImGui::PushID(100 + i);
        if (i % 4) ImGui::SameLine(0, px(8));
        int v = t.rolls[size_t(i)];
        if (inputInt("##roll", &v, 0, 255, 110)) t.rolls[size_t(i)] = uint8_t(v);
        ImGui::PopID();
    }
    gap(sp::M);
    fieldLabel("SUBCLASS ABILITY TRIPLES  (node, group, record; FF = none)");
    gap(sp::XS + 2);
    for (int i = 0; i < 5; ++i) {
        ImGui::PushID(200 + i);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        drawText(ImGui::GetWindowDrawList(), FontKind::Body, type::Small, ImVec2(p.x, textTop(FontKind::Body, type::Small, p.y + px(17))),
                 u32(pal::Muted), kAbilityNames[i]);
        ImGui::SetCursorScreenPos(ImVec2(p.x + px(70), p.y));
        for (int k = 0; k < 3; ++k) {
            ImGui::PushID(k);
            if (k) ImGui::SameLine(0, px(6));
            int v = t.abilities[size_t(i)][size_t(k)];
            if (inputInt("##t", &v, 0, 255, 110)) t.abilities[size_t(i)][size_t(k)] = uint8_t(v);
            ImGui::PopID();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void changesPreview(Ed& e, const EditorSnapshot& s, bool* canApply, size_t* count) {
    *canApply = false;
    *count = 0;
    EditPlan plan;
    std::string error;
    try {
        plan = editorPlan(s, e.draft, editorCatalog(), catalog());
    } catch (const std::exception& ex) {
        error = ex.what();
    }
    if (!error.empty()) {
        pill(error.c_str(), pal::Warning, icon::Warning);
        return;
    }
    *count = plan.changes.size();
    *canApply = !plan.changes.empty();
    if (plan.changes.empty()) {
        caption("No changes: the draft matches the live values.", pal::Faint);
        return;
    }
    char hdr[64];
    std::snprintf(hdr, sizeof hdr, "PLANNED WRITES  (%zu)", plan.changes.size());
    fieldLabel(hdr);
    ImGui::SameLine();
    alignRight(px(30));
    if (iconButton("##showchg", e.showChanges ? icon::ChevronUp : icon::ChevronDown, e.showChanges ? "Hide the byte changes" : "Show the byte changes", false, 26))
        e.showChanges = !e.showChanges;
    if (!e.showChanges) return;
    gap(sp::XS);
    for (const Change& c : plan.changes) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = availWidth();
        const float h = px(48);
        ImDrawList* d = ImGui::GetWindowDrawList();
        d->AddRectFilled(p, p + ImVec2(w, h), u32(pal::Input), px(6));
        drawText(d, FontKind::Semibold, type::Small, p + ImVec2(px(12), px(7)), u32(pal::Text), c.name.c_str());
        const std::string where = world::hex8(c.addr) + "  \xC2\xB7  " + std::to_string(c.after.size()) + " bytes";
        const ImVec2 ws = textSize(FontKind::Mono, type::MonoSmall, where.c_str());
        drawText(d, FontKind::Mono, type::MonoSmall, ImVec2(p.x + w - ws.x - px(12), p.y + px(8)), u32(pal::Muted), where.c_str());
        std::string diff = hexBytes(c.before).substr(0, 32) + (c.before.size() > 16 ? "..." : "") + "  \xE2\x86\x92  " + hexBytes(c.after).substr(0, 32) +
                           (c.after.size() > 16 ? "..." : "");
        drawText(d, FontKind::Mono, type::MonoSmall, p + ImVec2(px(12), px(27)), u32(pal::Faint), ellipsize(diff.c_str(), w - px(24), FontKind::Mono, type::MonoSmall).c_str());
        ImGui::Dummy(ImVec2(w, h + px(4)));
    }
}

void seedEditorDemo(Ed& e) {
     
    if (e.demoSeeded) return;
    e.demoSeeded = true;
    const Catalog& cat = catalog();
    auto s = std::make_shared<EditorSnapshot>();
    s->characterSoid = 0x2305843009213694ull;
    Profile p;
    p.addr = 0x600A8A78;
    p.raw.assign(off::PROFILE_SIZE, 0xFF);
    const uint8_t selectors[5] = {1, 0, 9, 14, 20};
    for (size_t i = 0; i < 5; ++i) {
        auto& a = s->abilities[i];
        a.fill(0);
        a[0] = selectors[i];
        for (size_t k = 0; k < 16; ++k) {
            const uint32_t key = k == 0 && i < editorCatalog().modifiers.size() ? editorCatalog().modifiers[i].key : kDefaultUniq;
            for (size_t b = 0; b < 4; ++b) a[4 + k * 4 + b] = uint8_t(key >> (24 - 8 * b));
        }
        std::copy(a.begin(), a.end(), p.raw.begin() + std::ptrdiff_t(off::P_ABILITY + i * 68));
    }
    s->profiles.push_back(p);
    s->characterPerks.assign(32, 0xFFFF);
    const std::pair<int, const char*> gear[] = {{1, "Graviton Forfeit"}, {2, "Crest of Alpha Lupi"}, {6, "Lord High Fixer"}, {7, "Two To The Morgue"},
                                                {8, "Gjallarhorn"}, {11, "Generalist Shell"}};
    uint64_t soid = 0x60000019DA700000ull;
    for (auto [slot, name] : gear) {
        for (const ItemDef& d : cat.items())
            if (d.name == name && d.perInstance) {
                SlotState& st = s->slots[size_t(slot)];
                st.soid = (soid += 0x1D3F);
                st.def = d.def;
                if (weaponSlot(slot)) {
                    st.perks = {12, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
                    auto& raw = s->profiles[0].raw;    
                    raw[off::P_WEAPON_PERKS + size_t(slot - 6) * 32] = 0;
                    raw[off::P_WEAPON_PERKS + size_t(slot - 6) * 32 + 1] = 12;
                }
                EditorItem it;
                it.soid = st.soid;
                it.def = d.def;
                it.slot = slot;
                it.addr = 0x60119000 + uint32_t(slot) * 168;
                it.talents.grid = d.grid;
                it.talents.nodes.fill(1);
                for (auto& a : it.talents.abilities) a = {255, 255, 255};
                s->items.push_back(it);
                for (int i = 0; i < 8; ++i) s->equipped[size_t(slot * 8 + i)] = uint8_t(st.soid >> (56 - 8 * i));
                break;
            }
    }
    s->characterPerks[0] = 6;
    s->characterPerks[1] = 12;
    std::lock_guard<std::mutex> lock(e.m);
    e.snap = s;
    e.status = "Sample configuration (review mode, not read from a console)";
}

void drawEditor(Ui& ui) {
    Ed& e = ed();
    const EditorCatalog& ec = editorCatalog();
    if (!ec.error.empty()) {
        emptyState(icon::Warning, "Editor Catalogue Unavailable", ec.error.c_str(), 200);
        return;
    }
    if (demoMode()) seedEditorDemo(e);
    std::shared_ptr<const EditorSnapshot> snap;
    std::string status, error;
    size_t undoable;
    {
        std::lock_guard<std::mutex> lock(e.m);
        snap = e.snap;
        status = e.status;
        error = e.error;
        undoable = e.applied.size();
    }
    const bool reading = ui.busy(kReadJob), applying = ui.busy(kApplyJob), undoing = ui.busy(kUndoJob);
     
    const ImVec2 rowStart = ImGui::GetCursorScreenPos();
    {
        auto offline = requireConnection(ui);
        if (button("Read Live", {.tone = Tone::Primary, .icon = icon::Refresh, .tooltip = "Read the equipped gear, profile copies and item details",
                                 .busy = reading, .successKey = "editor.read"})) {
            noteQueued(kReadJob);
            ui.post(kReadJob, [](Ctx& c) { readLive(c); },
                    [](bool ok, const std::string& err) {
                        Ed& x = ed();
                        std::lock_guard<std::mutex> lock(x.m);
                        if (ok) flashSuccess("editor.read");
                        else x.error = err;
                    });
        }
        ImGui::SameLine(0, px(8));
        DisabledScope none(undoable == 0, "No edit to undo in this session");
        if (button("Undo Last Edit", {.tone = Tone::Ghost, .icon = icon::Undo, .tooltip = "Put the bytes of the last applied edit back (while they are still ours)",
                                      .busy = undoing}))
            ui.post(kUndoJob, undoJob);
    }
    ImGui::SameLine(0, px(14));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(9));
    caption(status.c_str(), pal::Muted);
    {
        bool revert = settings().flag(kRevertKey, true);
        const char* text = "Restore on Disconnect / Panic";
        const float tw = px(22) + px(8) + textSize(FontKind::Body, type::Small, text).x;
        ImGui::SameLine();
        alignRight(tw);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(3));
        const ImVec2 at = ImGui::GetCursorScreenPos();
        if (toggleSwitch("##revert", &revert)) settings().set(std::string(kRevertKey), revert);
        tooltip("On: disconnecting or Panic puts back the original bytes of every edit made this session. Off: edits stay until the game rebuilds them.");
        drawText(ImGui::GetWindowDrawList(), FontKind::Body, type::Small, ImVec2(at.x + px(30), textTop(FontKind::Body, type::Small, at.y + px(11))),
                 u32(revert ? pal::Text : pal::Muted), text);
        ImGui::SetCursorScreenPos(ImVec2(rowStart.x, std::max(at.y + px(22), rowStart.y + px(kControlH))));
        ImGui::Dummy(ImVec2(1, 1));
    }
    if (!error.empty()) {
        gap(sp::XS);
        pill(error.c_str(), pal::Warning, icon::Warning);
    }
    gap(sp::M);
    if (!snap) {
        emptyState(icon::Sliders, "Read the Live Configuration",
                   "Loads the current character (orbit or an activity): equipped gear with pattern, art and perks, ability selectors, the 32 "
                   "character perks and every item's talent state. Nothing changes until you Apply.",
                   240);
        return;
    }
     
    gap(sp::S);
    const int oldPage = e.page;
    pillTabs("##edpage", {"Equipped Gear", "Abilities", "Character Perks", "Item Talents"}, &e.page);
    gap(sp::M);
     
    if (e.page == 3 && e.item == 0)
        for (const EditorItem& it : snap->items) {
            e.item = it.soid;
            break;
        }
    const std::string key = std::to_string(snap->stamp) + "/" + std::to_string(e.page) + "/" + std::to_string(e.slot) + "/" + std::to_string(e.ability) +
                            "/" + hex16(e.item);
    if (key != e.draftKey || oldPage != e.page) {
        e.draftKey = key;
        const EditMode mode = e.page == 0 ? EditMode::Equipment : e.page == 1 ? EditMode::Ability : e.page == 2 ? EditMode::Character : EditMode::Talents;
        e.draft = draftFor(*snap, mode, e.page == 1 ? e.ability : e.slot, e.item);
        if (e.page == 3) e.item = e.draft.soid;
    }
    const float h = std::clamp(ImGui::GetWindowHeight() - px(430), px(320), px(640));
    switch (e.page) {
    case 0: pageEquipment(e, *snap, h); break;
    case 1: pageAbility(e, *snap, h); break;
    case 2: pageCharacter(e, h); break;
    default: pageTalents(e, *snap, h); break;
    }
    gap(sp::M);
    divider(sp::S);
    bool canApply = false;
    size_t changes = 0;
    changesPreview(e, *snap, &canApply, &changes);
    gap(sp::M);
    {
        auto offline = DisabledScope(!ui.connected() || !canApply, !ui.connected() ? kConnectFirst : "No valid changes in the draft");
        if (button(changes ? ("Apply " + std::to_string(changes) + (changes == 1 ? " Change" : " Changes")).c_str() : "Apply",
                   {.tone = Tone::Primary, .icon = icon::Check, .tooltip = "Rebuilds the plan on a fresh read, re-checks each target, writes it and reads it back",
                    .busy = applying, .successKey = "editor.apply"})) {
            auto s = snap;
            const EditRequest draft = e.draft;
            confirm("Apply " + std::to_string(changes) + (changes == 1 ? " Change?" : " Changes?"),
                    std::string("Writes the planned fields into live memory after re-checking them. ") +
                        (settings().flag(kRevertKey, true) ? "They are put back on disconnect or Panic; Undo works until then."
                                                           : "They stay until the game rebuilds them; Undo works this session."),
                    "Apply",
                    [s, draft] {
                        noteQueued(kApplyJob);
                        Engine::get().post(kApplyJob, [s, draft](Ctx& c) { applyJob(c, s, draft); },
                                           [](bool ok, const std::string& err) {
                                               Ed& x = ed();
                                               std::lock_guard<std::mutex> lock(x.m);
                                               if (ok) flashSuccess("editor.apply");
                                               else x.error = err;
                                           });
                    },
                    false);
        }
    }
    ImGui::SameLine(0, px(8));
    if (button("Reset Draft", {.tone = Tone::Ghost, .icon = icon::Undo, .tooltip = "Back to the live values"})) e.draftKey.clear();
     
    if (!e.presetsLoaded) loadPresets(e);
    ImGui::SameLine(0, px(20));
    inputText("##presetname", &e.presetName, "Combination Name", 200);
    ImGui::SameLine(0, px(6));
    if (button("Save", {.icon = icon::Add, .tooltip = "Save this draft as a combination (pattern, perks, talents... for this tab)"})) {
        std::string name = e.presetName;
        const bool bad = name.empty() || name.size() > 60 || name.find_first_of("/\\:<>|?*\".") != std::string::npos;
        if (bad) {
            notify("Choose a simple combination name (letters, digits, spaces)", Level::Warn);
        } else {
            std::error_code ec2;
            std::filesystem::create_directories(presetDir(), ec2);
            std::ofstream f(presetDir() / (name + ".txt"), std::ios::binary);
            f << serialiseRequest(e.draft);
            if (f) {
                notify("Saved combination " + name, Level::Good);
                loadPresets(e);
            } else {
                notify("Could not write the combination file", Level::Error);
            }
        }
    }
    ImGui::SameLine(0, px(6));
    {
        std::vector<std::string> names;
        std::vector<EditRequest> reqs;
        for (const std::string& n : e.presets) {
            try {
                std::ifstream f(presetDir() / (n + ".txt"), std::ios::binary);
                std::stringstream ss;
                ss << f.rdbuf();
                const EditRequest r = parseRequest(ss.str());
                if (r.mode == e.draft.mode) {
                    names.push_back(n);
                    reqs.push_back(r);
                }
            } catch (const std::exception&) {
            }
        }
        DisabledScope none(names.empty(), "No saved combination for this tab yet");
        if (button(names.empty() ? "Load" : ("Load  (" + std::to_string(names.size()) + ")").c_str(), {.tone = Tone::Ghost, .icon = icon::Folder}))
            ImGui::OpenPopup("##loadpreset");
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(6), px(6)));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, pal::RaisedFill);
        if (ImGui::BeginPopup("##loadpreset")) {
            for (size_t i = 0; i < names.size(); ++i)
                if (ImGui::Selectable(names[i].c_str())) {
                    EditRequest r = reqs[i];
                    r.soid = e.draft.soid;    
                    r.slot = e.draft.slot;
                    if (r.mode == EditMode::Talents) r.soid = e.draft.soid;
                    e.draft = r;
                    e.presetName = names[i];
                }
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }
}

 
 
void resetEditorPanic() {
    Ed& e = ed();
    std::lock_guard<std::mutex> lock(e.m);
    e.error.clear();
}

 
void resetEditor() {
    Ed& e = ed();
    std::lock_guard<std::mutex> lock(e.m);
    e.error.clear();
    if (!demoMode()) e.snap.reset();
    e.applied.clear();
    e.status = "Read live configuration to begin";
}

}   

}   

namespace brps3 {

BR_FEATURES(inventory_editor_page) {
    using namespace invui;
    r.section({.id = "vault.editor", .page = Page::Vault, .tab = "Editor", .section = "Live editor", .order = 10, .draw = drawEditor,
               .onDisconnect = resetEditor, .onPanic = resetEditorPanic});
    r.action({.id = "vault.editor_read", .page = Page::Vault, .tab = "Editor", .section = "Live editor", .label = "Read Live Configuration",
              .help = "Read the equipped gear, ability selectors, character perks and item talents for the live editor.",
              .run = [](Ctx& c) { readLive(c); }});
    r.action({.id = "vault.editor_undo", .page = Page::Vault, .tab = "Editor", .section = "Live editor", .label = "Undo Last Edit",
              .help = "Put back the bytes of the last live-editor edit (only while they are still the edited bytes).", .run = undoJob});
    ui::styleSection("Live editor", {.fullWidth = true,
                                     .icon = ui::icon::Sliders,
                                     .ownsActions = true});
}

}   
