 
 
 
 
 
 
 
 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

#include "app/engine.h"
#include "app/registry.h"
#include "core/error.h"
#include "features/spawn_shared.h"
#include "features/spawn_ui.h"
#include "game/names_catalog.h"
#include "game/spawn_catalog.h"
#include "game/world_tags.h"
#include "ui/widgets.h"

namespace brps3::spawnui {
namespace {

using namespace ui;

 
 
 

struct Row {
    uint32_t tag = 0;
    int type = -1;                       
    const spawn::CatalogRow* cat = nullptr;
};

 
struct TypeGroup {
    const char* label;
    bool (*match)(int type);
};
const TypeGroup kTypeGroups[] = {
    {"All Types", [](int) { return true; }},
    {"Bipeds", [](int t) { return t == 9; }},
    {"Interactive", [](int t) { return t == 8; }},
    {"Weapons", [](int t) { return t == 11; }},
    {"Vehicles & Turrets", [](int t) { return t == 12 || t == 13; }},
    {"Creatures", [](int t) { return t == 10; }},
    {"Props", [](int t) { return t >= 1 && t <= 6; }},
    {"Gear", [](int t) { return t == 19; }},
    {"Hop-Ons & Attachments", [](int t) { return t >= 20 && t <= 23; }},
    {"Items", [](int t) { return t == 17 || t == 18; }},
    {"Emitters & Projectiles", [](int t) { return t == 14 || t == 15; }},
    {"System", [](int t) { return t == 24; }},
};
constexpr int kTypeGroupCount = int(sizeof(kTypeGroups) / sizeof(kTypeGroups[0]));

enum Source { All = 0, Named, Favourites, Recent, Loaded };

struct CatPage {
    bool built = false;
    std::vector<Row> rows;               
    std::vector<std::string> hay;        
    bool hayBuilt = false;
    std::vector<int> view;               
    bool viewDirty = true;
    std::string filter;
    int source = All;
    int typeGroup = 0;
    uint64_t seenRev = ~0ull;
    size_t seenFavs = ~size_t(0);
    ImGuiTableColumnSortSpecs sortSpec{};
    bool sorted = false;
    bool scrollToSelected = true;
    bool lastSearchActive = false;
    float detailH = 0;
    std::unordered_map<uint32_t, int> index;    
     
    uint32_t labelFor = 0;
    std::string labelEdit;
    uint32_t checkedFor = 0;
    double selectedAt = 0;
     
    std::string manualTag = "8108F764";
    std::string manualLabel = "Sword relic (Crota's End, pkg 447)";
     
    std::string openerFilter;
    int openerGroup = 0;
};

CatPage& pg() {
    static CatPage p;
    return p;
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void ensureRows() {
    CatPage& p = pg();
    if (p.built) return;
    p.built = true;
    const auto& cat = spawn::catalog();
    p.rows.reserve(cat.size() + 64);
    for (const spawn::CatalogRow& r : cat.rows()) {
        p.index[r.tag] = int(p.rows.size());
        p.rows.push_back({r.tag, r.type, &r});
    }
}

const std::string& hayOf(int i) {
    CatPage& p = pg();
    if (!p.hayBuilt) {
        p.hay.resize(p.rows.size());
        for (size_t k = 0; k < p.rows.size(); ++k)
            p.hay[k] = (p.rows[k].cat ? spawn::searchText(*p.rows[k].cat) : lower(spawn::tagText(p.rows[k].tag) + " loaded uncatalogued")) + " " +
                       lower(knownName(p.rows[k].tag));
        p.hayBuilt = true;
    }
    if (p.hay.size() < p.rows.size()) {
        for (size_t k = p.hay.size(); k < p.rows.size(); ++k) p.hay.push_back(lower(spawn::tagText(p.rows[k].tag) + " loaded uncatalogued"));
    }
    return p.hay[size_t(i)];
}

 
void addLoadedExtras(const ResidencyView& rv) {
    CatPage& p = pg();
    for (uint32_t tag : rv.resident) {
        if (p.index.count(tag)) continue;
        p.index[tag] = int(p.rows.size());
        p.rows.push_back({tag, -1, nullptr});
    }
}

int rowOf(uint32_t tag) {
    auto it = pg().index.find(tag);
    return it == pg().index.end() ? -1 : it->second;
}

 
int categoryRank(int type) {
    switch (type) {
    case 9: return 0;
    case 8: return 1;
    case 11: return 2;
    case 12: return 3;
    case 13: return 4;
    case 10: return 5;
    case 17: case 18: return 6;
    case 2: case 3: case 4: case 5: return 7;
    case 19: return 8;
    case 20: case 21: case 22: case 23: return 9;
    case 1: return 10;
    case 6: return 11;
    case 14: return 12;
    case 15: return 13;
    case 24: return 14;
    default: return 15;
    }
}

void rebuildView() {
    CatPage& p = pg();
    const ResidencyView& rv = residency();
    addLoadedExtras(rv);
    p.view.clear();
    const auto words = spawn::queryWords(p.filter);
    std::vector<uint32_t> list;
    if (p.source == Favourites) list = favourites();
    if (p.source == Recent) list = recents();
    auto accept = [&](int i) {
        const Row& r = p.rows[size_t(i)];
        if (!kTypeGroups[p.typeGroup].match(r.type)) return false;
        if (p.source == Named && knownName(r.tag).empty()) return false;
        if (p.source == Loaded && rv.of(r.tag) != Res::Loaded) return false;
        return words.empty() || spawn::matches(hayOf(i), words);
    };
    if (p.source == Favourites || p.source == Recent) {
        for (uint32_t tag : list) {
            int i = rowOf(tag);
            if (i < 0) {                                        
                p.index[tag] = int(p.rows.size());
                p.rows.push_back({tag, -1, nullptr});
                i = int(p.rows.size()) - 1;
            }
            if (accept(i)) p.view.push_back(i);
        }
    } else {
        for (int i = 0; i < int(p.rows.size()); ++i)
            if (accept(i)) p.view.push_back(i);
    }
     
    const bool keepOrder = p.source == Recent && !p.sorted;
    if (!keepOrder) {
        const ImGuiID col = p.sorted ? p.sortSpec.ColumnUserID : 99;
        const bool asc = !p.sorted || p.sortSpec.SortDirection != ImGuiSortDirection_Descending;
        std::unordered_map<uint32_t, std::string> nameCache;
        auto name = [&](uint32_t tag) -> const std::string& {
            auto it = nameCache.find(tag);
            if (it == nameCache.end()) {
                std::string k = lower(knownName(tag));
                k.erase(0, std::min(k.size(), k.find_first_not_of("\"' ([-_.")));    
                it = nameCache.emplace(tag, k.empty() ? "~" + lower(unnamedLabel(tag)) : k).first;
            }
            return it->second;
        };
        const auto favs = favourites();
        auto isFav = [&](uint32_t t) { return std::find(favs.begin(), favs.end(), t) != favs.end(); };
        if (col == 99) {
            std::unordered_map<uint32_t, bool> named;
            for (int i : p.view) named[p.rows[size_t(i)].tag] = !knownName(p.rows[size_t(i)].tag).empty();
            std::stable_sort(p.view.begin(), p.view.end(), [&](int a, int b) {
                const Row& x = p.rows[size_t(a)];
                const Row& y = p.rows[size_t(b)];
                const bool na = named[x.tag], nb = named[y.tag];
                if (na != nb) return na;
                const int ra = categoryRank(x.type), rb = categoryRank(y.type);
                if (ra != rb) return ra < rb;
                if (na) {
                    const int c = name(x.tag).compare(name(y.tag));
                    if (c) return c < 0;
                }
                return x.tag < y.tag;
            });
        } else {
            std::stable_sort(p.view.begin(), p.view.end(), [&](int a, int b) {
                const Row& x = p.rows[size_t(a)];
                const Row& y = p.rows[size_t(b)];
                int c = 0;
                switch (col) {
                case 0: c = int(isFav(y.tag)) - int(isFav(x.tag)); break;
                case 1: c = int(rv.of(x.tag)) == int(rv.of(y.tag)) ? 0 : (rv.of(x.tag) == Res::Loaded ? -1 : rv.of(y.tag) == Res::Loaded ? 1 : int(rv.of(y.tag)) - int(rv.of(x.tag))); break;
                case 2: c = name(x.tag).compare(name(y.tag)); break;
                case 3: c = x.type - y.type; break;
                case 4: c = lower(location(x.tag)).compare(lower(location(y.tag))); break;
                default: c = x.tag < y.tag ? -1 : x.tag > y.tag ? 1 : 0; break;
                }
                return asc ? c < 0 : c > 0;
            });
        }
    }
    p.viewDirty = false;
}

void selectRow(int i) {
    CatPage& p = pg();
    if (i < 0 || i >= int(p.rows.size())) return;
    select(p.rows[size_t(i)].tag);
    p.scrollToSelected = true;
    p.selectedAt = ImGui::GetTime();
}

 
 
 

void drawResidencyPill(uint32_t tag) {
    const Res r = residency().of(tag);
    State& s = st();
    TagCheck check;
    bool haveCheck = false;
    {
        std::lock_guard<std::mutex> lock(s.m);
        auto it = s.checks.find(tag);
        if (it != s.checks.end()) {
            check = it->second;
            haveCheck = true;
        }
    }
    pill(resLabel(r), r == Res::Loaded ? pal::Success : r == Res::PackageLoaded ? pal::Warning : pal::Muted,
         r == Res::Loaded ? icon::Check : r == Res::Unknown ? icon::Info : nullptr);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
        std::string tip = resHelp(r);
        if (haveCheck && !check.resident && !check.why.empty()) tip += "\nLast check: " + check.why;
        ImGui::SetTooltip("%s", tip.c_str());
    }
}

void drawLabelEditor(uint32_t tag) {
    CatPage& p = pg();
    if (p.labelFor != tag) {
        p.labelFor = tag;
        const auto own = names::Labels::get().find(tag);
        p.labelEdit = own ? own->name : std::string();
    }
    fieldLabel("Your Label");
    gap(sp::XS + 2);
    const auto own = names::Labels::get().find(tag);
    const float bw = buttonWidth("Save", icon::Check, true);
    inputText("##label", &p.labelEdit, "Name It", (availWidth() - bw - px(8)) / scale());
    const bool enter = ImGui::IsItemDeactivated() && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter));
    ImGui::SameLine(0, px(8));
    const bool changed = p.labelEdit != (own ? own->name : std::string());
    {
        DisabledScope none(!changed, "Type a new label first");
        if (button("Save##label", {.tone = Tone::Ghost, .icon = icon::Check, .small = true, .tooltip = "Saved to names_user.tsv; empty removes it"}) ||
            (enter && changed)) {
            if (p.labelEdit.empty()) names::Labels::get().remove(tag);
            else {
                names::Label l = own.value_or(names::Label{});
                l.name = p.labelEdit;
                names::Labels::get().set(tag, l);
            }
            pg().hayBuilt = false;
            pg().viewDirty = true;
            notify(p.labelEdit.empty() ? "Label removed" : "Label saved", Level::Good);
        }
    }
    if (const std::string err = names::Labels::get().error(); !err.empty()) caption(err.c_str(), pal::Warning);
}

void drawDetail(Ui& ui, float width, float height) {
    CatPage& p = pg();
    const bool child = height > 0;
    if (child) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, pal::Raised);
        ImGui::PushStyleColor(ImGuiCol_Border, pal::Border);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(kRowRounding));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(16), px(14)));
        ImGui::SetNextWindowSizeConstraints(ImVec2(width, height), ImVec2(width, FLT_MAX));
        ImGui::BeginChild("##detail", ImVec2(width, 0),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AutoResizeY);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
    } else {
        beginCard("##detailcard", nullptr, {.raised = true});
    }
    const uint32_t tag = selectedTag();
    if (!tag) {
        emptyState(icon::Spawner, "Pick Something to Spawn",
                   "Search the Catalogue (Press /), Pick a Row, Then Spawn",
                   child ? std::max(220.0f, height / scale() - 40) : 220);
    } else {
        const int etype = catalogType(tag);
        const spawn::CatalogRow* row = spawn::catalog().find(tag);
         
        {
            const float starW = px(30);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + availWidth() - starW - px(8));
            const std::string known = knownName(tag);
            heading(known.empty() ? unnamedLabel(tag).c_str() : known.c_str());
            ImGui::PopTextWrapPos();
            const ImVec2 after = ImGui::GetCursorScreenPos();
            ImGui::SameLine();
            alignRight(starW);
            const bool fav = favourite(tag);
            if (iconButton("##fav", fav ? icon::StarFill : icon::Star, fav ? "Remove from favourites" : "Add to favourites (they show first in Ctrl+K)",
                           false, 30, fav ? pal::Accent : pal::Muted)) {
                toggleFavourite(tag);
                p.viewDirty = true;
            }
            ImGui::SetCursorScreenPos(after);
        }
        gap(sp::XS);
        hashLabel(tag, "tag", pal::Accent);
        ImGui::SameLine(0, px(10));
        pill(etype >= 0 ? spawn::categoryName(uint8_t(etype)) : "Uncatalogued", etype >= 0 ? pal::Info : pal::Muted);
        tooltip(row ? spawn::usageNote(*row) : "Not in the catalogue: found loaded on the console (or typed by hand). Spawned with its live type.");
        ImGui::SameLine(0, px(6));
        drawResidencyPill(tag);
        const std::string loc = location(tag);
        if (!loc.empty()) {
            gap(sp::XS);
            caption(loc.c_str(), pal::Muted);
        }
        if (row && row->squads) {
            gap(sp::XS);
            char buf[64];
            std::snprintf(buf, sizeof buf, "Used by %u authored squad%s: ", unsigned(row->squads), row->squads == 1 ? "" : "s");
            wrapped((buf + row->squadNames).c_str(), pal::Muted, type::Small);
        }
        if (row && !row->candidates.empty()) {
            gap(sp::XS);
            wrapped(("Other names: " + row->candidates).c_str(), pal::Faint, type::Small);
        }
        divider(sp::M);

         
        spawn::Placement place = placement();
        fieldLabel("Count");
        gap(sp::XS + 2);
        int count = place.count;
        if (inputInt("##count", &count, 1, spawn::kMaxCount, 140)) {
            place.count = count;
            setPlacement(place);
        }
        tooltip("How many to spawn, on a grid (spacing in the Placement card)");
        for (int q : {1, 3, 5, 10}) {
            ImGui::SameLine(0, px(6));
            const std::string l = std::to_string(q) + "##q";
            if (button(l.c_str(), {.tone = Tone::Ghost, .small = true, .selected = place.count == q})) {
                place.count = q;
                setPlacement(place);
            }
        }
        gap(sp::M);
        const bool busy = spawnBusy();
        const float menuW = px(kControlH);
        const bool refused = row && row->refused;
        {
            auto offline = requireConnection(ui);
            DisabledScope garbage(refused, "The spawn factory refuses garbage props");
            const std::string tip = "Spawn with the placement below (Ctrl+Enter). " + spawn::summary(place);
            if (button("Spawn", {.tone = Tone::Primary, .icon = icon::Play, .width = (availWidth() - menuW - px(8)) / scale(), .tooltip = tip.c_str(),
                                 .busy = busy, .successKey = "spawn.go"}))
                startSpawn(tag);
        }
        ImGui::SameLine(0, px(8));
        if (iconButton("##spawnmenu", icon::More, "Pin \"Spawn Selected Entity\" to Home or give it a hotkey", false, kControlH))
            openItemMenu(kActSpawnSelected);
        gap(sp::XS + 2);
        caption(spawn::summary(place).c_str(), pal::Faint);
        gap(sp::S);
        {
            auto offline = requireConnection(ui);
            DisabledScope garbage(refused, "The spawn factory refuses garbage props");
            const float half = (availWidth() - px(8)) * 0.5f;
            if (button("At Crosshair", {.tone = Tone::Ghost, .icon = icon::Follow, .width = half / scale(), .small = true,
                                        .tooltip = "Spawn where you aim, at the placement distance (ignores the mode above)"}))
                startSpawn(tag, spawn::Mode::Crosshair);
            ImGui::SameLine(0, px(8));
            if (button("At My Position", {.tone = Tone::Ghost, .icon = icon::Home, .width = half / scale(), .small = true,
                                          .tooltip = "Spawn at your feet plus the placement offset"}))
                startSpawn(tag, spawn::Mode::Player);
        }
         
        std::string progress, last;
        bool lastOk = true;
        {
            State& s = st();
            std::lock_guard<std::mutex> lock(s.m);
            progress = s.progress;
            last = s.lastSpawn;
            lastOk = s.lastSpawnOk;
        }
        if (busy) {
            gap(sp::M);
            progressBar(-1, 0, 4);
            gap(sp::XS);
            caption(progress.empty() ? "Queued..." : progress.c_str(), pal::Muted);
            ImGui::SameLine();
            alignRight(buttonWidth("Stop", icon::Stop, true));
            if (button("Stop##spawnjob", {.tone = Tone::Danger, .icon = icon::Stop, .small = true, .tooltip = "Stop after the current frame's batch"}))
                Engine::get().cancel();
        } else if (!last.empty()) {
            gap(sp::M);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + availWidth());
            pill(last.size() > 64 ? (last.substr(0, 62) + "..").c_str() : last.c_str(), lastOk ? pal::Success : pal::Danger,
                 lastOk ? icon::Check : icon::Warning);
            ImGui::PopTextWrapPos();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && last.size() > 64) ImGui::SetTooltip("%s", last.c_str());
        }
        divider(sp::M);
        drawLabelEditor(tag);
        gap(sp::M);
        {
            auto offline = requireConnection(ui);
            if (button("Check Now", {.tone = Tone::Ghost, .icon = icon::Refresh, .small = true,
                                     .tooltip = "Read whether this definition is loaded right now (4 reads)", .busy = ui.busy(kJobCheck)}))
                post(kJobCheck, [tag](Ctx& c) {
                    const TagCheck k = checkTag(c, tag);
                    c.log("[residency] " + spawn::tagText(tag) + (k.resident ? ": loaded (type " + std::to_string(k.type) + ")" : ": " + k.why));
                });
        }
        ImGui::SameLine(0, px(6));
        if (button("Load It", {.tone = Tone::Ghost, .icon = icon::OpenExternal, .small = true,
                               .tooltip = "Open World load with this tag (load it into the world, or arm it for the next transition)"}))
            loadTagOnWorldTab(tag);
        ImGui::SameLine(0, px(6));
        if (button("Copy", {.tone = Tone::Ghost, .icon = icon::Copy, .small = true, .tooltip = "Copy the tag"}))
            copyToClipboard(spawn::tagText(tag), "tag");
    }
    if (child) {
        gap(sp::XS);
        p.detailH = ImGui::GetCursorPosY() + px(14);
        ImGui::EndChild();
    } else {
        endCard();
    }
}

 
 
 

void starHeader() {
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers, px(30));
    const int n = ImGui::TableGetColumnCount();
    FontScope f(FontKind::Semibold, type::Small - 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, pal::Muted);
    for (int c = 0; c < n; ++c) {
        if (!ImGui::TableSetColumnIndex(c)) continue;
        ImGui::TableHeader(ImGui::TableGetColumnName(c));
        if (c == 0 || c == 1) {
            const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
            const float cx = ImGui::TableGetCellBgRect(ImGui::GetCurrentTable(), c).Min.x + ImGui::GetStyle().CellPadding.x + px(c == 0 ? 8 : 6);
            if (c == 0) {
                const ImVec2 is = textSize(FontKind::Icons, 11, icon::Star);
                drawText(ImGui::GetWindowDrawList(), FontKind::Icons, 11, ImVec2(cx - is.x * 0.5f, (mn.y + mx.y - is.y) * 0.5f), u32(pal::Muted), icon::Star);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("Sort favourites first");
            } else {
                ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(cx, (mn.y + mx.y) * 0.5f), px(3.5f), u32(pal::Muted), 12);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("Sort loaded first");
            }
        }
    }
    ImGui::PopStyleColor();
}

void drawCatalog(Ui& ui) {
    CatPage& p = pg();
    seedDemo();
    ensureRows();
    const auto& cat = spawn::catalog();
    if (!cat.error.empty() && p.rows.empty()) {
        emptyState(icon::Warning, "Spawn Catalogue Unavailable", cat.error.c_str(), 220);
        return;
    }
    const ResidencyView& rv = residency();
     
    {
        static double lastTry = -100;
        if (rv.pagesAt == 0 && Engine::get().connected() && !ui.busy(kJobPages) && ImGui::GetTime() - lastTry > 15) {
            lastTry = ImGui::GetTime();
             
            Engine::get().post(kJobPages, [](Ctx& c) {
                try {
                    refreshPages(c);
                } catch (const std::exception& e) {
                    c.log(std::string("[residency] loaded packages not read yet: ") + e.what());
                }
            });
        }
    }
    const auto favs = favourites();
    if (rv.rev != p.seenRev || favs.size() != p.seenFavs) {
        if (p.source == Loaded || p.source == Favourites || (p.sorted && (p.sortSpec.ColumnUserID == 0 || p.sortSpec.ColumnUserID == 1)) ||
            !rv.resident.empty())
            p.viewDirty = true;
        p.seenRev = rv.rev;
        p.seenFavs = favs.size();
    }
    ImGuiIO& io = ImGui::GetIO();
    const float avail = availWidth();
     
    ImGuiID searchInput = 0;
    {
        const float rowRight = ImGui::GetCursorScreenPos().x + avail;
        const float searchW = std::min(px(330), avail * 0.34f);
        if (searchField("##search", &p.filter, "Search Name, Tag, Place, Squad", searchW / scale(), true)) p.viewDirty = true;
        ImGui::PushID("##search");
        searchInput = ImGui::GetID("##input");
        ImGui::PopID();
        ImGui::SameLine(0, px(10));
        std::vector<std::string> sources = {"All", "Named", favs.empty() ? std::string("Favourites") : "Favourites  " + std::to_string(favs.size()),
                                            "Recent", "Loaded"};
        if (pillTabs("##source", sources, &p.source)) {
            p.viewDirty = true;
            p.scrollToSelected = true;
        }
        if (p.source == Loaded) {
            ImGui::SameLine(0, px(6));
            const bool scanning = st().scanning.load() || ui.busy(kJobScan);
            auto offline = requireConnection(ui);
            if (iconButton("##rescan", icon::Refresh, scanning ? "Reading the PS3's tag table..." : "Re-read what is loaded on the PS3 now",
                           scanning, kControlH) && !scanning)
                post(kJobScan, [](Ctx& c) { scanEntries(c); });
        }
        ImGui::SameLine(0, px(10));
        if (ImGui::GetCursorScreenPos().x + px(190) > rowRight) {    
            ImGui::NewLine();
            gap(sp::XS);
        }
        static const char* groupLabels[kTypeGroupCount];
        for (int i = 0; i < kTypeGroupCount; ++i) groupLabels[i] = kTypeGroups[i].label;
        if (combo("##types", &p.typeGroup, groupLabels, kTypeGroupCount, 190)) p.viewDirty = true;
        if (p.viewDirty) rebuildView();
        char buf[64];
        snprintf(buf, sizeof buf, "%zu of %zu", p.view.size(), cat.size());
        const ImVec2 ts = textSize(FontKind::Body, type::Small, buf);
        if (ImGui::GetCursorPosX() + ts.x < avail) {
            ImGui::SameLine();
            alignRight(ts.x);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(8));
            caption(buf, pal::Faint);
        }
    }
    gap(sp::M);

    const bool split = avail >= px(780);
    const float detailW = split ? std::clamp(avail * 0.38f, px(320), px(440)) : avail;
    const float tableW = split ? avail - detailW - px(16) : avail;
    const bool compact = tableW < px(560);
    const float viewH = ImGui::GetWindowHeight();
    const float baseH = std::clamp(viewH - px(290), px(380), px(760));
    const float tableH = split ? std::max(baseH, std::min(p.detailH, px(900))) : baseH;

     
    const ImVec2 tablePos = ImGui::GetCursorScreenPos();
    const bool tableHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseHoveringRect(tablePos, tablePos + ImVec2(tableW, tableH));
    const bool searchActive = ImGui::GetActiveID() == searchInput;
    const bool popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    const uint32_t selected = selectedTag();
    const int selRow = rowOf(selected);
    if (!popup && !p.view.empty() && (searchActive || (tableHovered && !io.WantTextInput))) {
        int pos = -1;
        for (int i = 0; i < int(p.view.size()); ++i)
            if (p.view[size_t(i)] == selRow) pos = i;
        const int last = int(p.view.size()) - 1;
        const int page = std::max(1, int(tableH / px(kRowH)) - 2);
        int next = pos;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) next = std::min(last, pos + 1);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) next = std::max(0, pos - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) next = std::min(last, std::max(0, pos) + page);
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) next = std::max(0, pos - page);
        if (next != pos && next >= 0) selectRow(p.view[size_t(next)]);
    }
    if (p.lastSearchActive && !searchActive && !p.view.empty() &&
        (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) &&
        std::find(p.view.begin(), p.view.end(), selRow) == p.view.end())
        selectRow(p.view.front());
    p.lastSearchActive = searchActive;
    if (!popup && selected && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Enter) && (searchActive || !io.WantTextInput) && !spawnBusy())
        startSpawn(selected);

     
    ImGui::BeginGroup();
    if (beginTable("##catalog", compact ? 4 : 6, tableH / scale(), ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate | ImGuiTableFlags_BordersOuter,
                   tableW / scale())) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##fav", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, px(16), 0);
        ImGui::TableSetupColumn("##res", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, px(12), 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0, 2);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, px(compact ? 92 : 104), 3);
        if (!compact) {
            ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthFixed, px(150), 4);
            ImGui::TableSetupColumn("Tag", ImGuiTableColumnFlags_WidthFixed, px(78), 5);
        }
        starHeader();
        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsDirty) {
            p.sorted = specs->SpecsCount > 0;
            if (p.sorted) p.sortSpec = specs->Specs[0];
            specs->SpecsDirty = false;
            rebuildView();
        }
        ImGuiListClipper clip;
        clip.Begin(int(p.view.size()), px(kRowH));
        int scrollTo = -1;
        if (p.scrollToSelected) {
            for (int i = 0; i < int(p.view.size()); ++i)
                if (p.view[size_t(i)] == selRow) scrollTo = i;
            if (scrollTo >= 0) clip.IncludeItemByIndex(scrollTo);
            p.scrollToSelected = false;
        }
        uint32_t toggleFav = 0;
        ImGui::PushFont(font(FontKind::Body), fontSize(FontKind::Body, type::Label));
        while (clip.Step()) {
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
                const int idx = p.view[size_t(r)];
                const Row& row = p.rows[size_t(idx)];
                const bool sel = row.tag == selected;
                ImGui::TableNextRow(0, px(kRowH));
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(idx);
                const float cellY = ImGui::GetCursorPosY();
                if (tableRowSelectable("##row", sel, nullptr, kRowH - 2 * ImGui::GetStyle().CellPadding.y / scale())) selectRow(idx);
                if (r == scrollTo) ImGui::SetScrollHereY(0.4f);
                ImGui::SetCursorPosY(cellY - px(3));
                const bool fav = favourite(row.tag);
                const ImVec2 spos = ImGui::GetCursorScreenPos() - ImVec2(px(4), 0);
                ImGui::SetCursorScreenPos(spos);
                if (ImGui::InvisibleButton("##star", ImVec2(px(24), px(24)))) toggleFav = row.tag;
                const bool sh = ImGui::IsItemHovered();
                const char* g = fav ? icon::StarFill : icon::Star;
                const ImVec2 is = textSize(FontKind::Icons, 12, g);
                drawText(ImGui::GetWindowDrawList(), FontKind::Icons, 12, spos + (ImVec2(px(24), px(24)) - is) * 0.5f,
                         u32(fav ? pal::Accent : sh ? pal::Text : pal::Decor), g);
                if (sh) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip(fav ? "Remove from favourites" : "Add to favourites");
                }
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                ImGui::SetCursorPosY(cellY);
                resDot(rv.of(row.tag), kRowH);
                ImGui::TableSetColumnIndex(2);
                const std::string known = knownName(row.tag);
                const std::string shown = !known.empty() ? known : compact ? unnamedLabel(row.tag) + "  " + spawn::tagText(row.tag) : unnamedLabel(row.tag);
                cellText(shown.c_str(), sel ? pal::Accent : !known.empty() ? pal::Text : pal::Faint, FontKind::Body, type::Label, kRowH);
                ImGui::TableSetColumnIndex(3);
                cellText(row.type >= 0 ? spawn::categoryName(uint8_t(row.type)) : "loaded", pal::Muted, FontKind::Body, type::Label, kRowH);
                if (!compact) {
                    ImGui::TableSetColumnIndex(4);
                    const std::string loc = location(row.tag);
                    cellText(loc.empty() ? "-" : loc.c_str(), loc.empty() ? pal::Decor : pal::Muted, FontKind::Body, type::Label, kRowH);
                    ImGui::TableSetColumnIndex(5);
                    cellText(spawn::tagText(row.tag).c_str(), sel ? pal::Accent : pal::Faint, FontKind::Mono, type::MonoSmall, kRowH);
                }
            }
        }
        ImGui::PopFont();
        if (p.view.empty()) {
            ImGui::TableNextRow(0, px(140));
            ImGui::TableSetColumnIndex(2);
            gap(sp::XL);
            const char* why = p.source == Favourites && favs.empty() ? "No favourites yet: click the star next to an entity."
                              : p.source == Recent && recents().empty() ? "Nothing spawned yet: your recent spawns collect here."
                              : p.source == Loaded && rv.resident.empty()
                                  ? "Reading what is loaded on the PS3..."
                                  : "No entities match. Clear the search or pick All Types.";
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + px(360));
            caption(why, pal::Muted);
            ImGui::PopTextWrapPos();
        }
        endTable();
        if (toggleFav) {
            toggleFavourite(toggleFav);
            p.viewDirty = p.source == Favourites || p.sorted;
        }
    }
    ImGui::EndGroup();
    if (split) {
        ImGui::SameLine(0, px(16));
        drawDetail(ui, detailW, tableH);
    } else {
        gap(sp::M);
        drawDetail(ui, detailW, 0);
    }
}

 
 
 

bool vec3Fields(const char* id, Vec3* v, const char* format = "%.2f") {
    ImGui::PushID(id);
    const float w = (availWidth() - px(16)) / 3.0f / scale();
    bool changed = false;
    const char* names3[] = {"X", "Y", "Z"};
    float* f[] = {&v->x, &v->y, &v->z};
    for (int i = 0; i < 3; ++i) {
        if (i) ImGui::SameLine(0, px(8));
        ImGui::PushID(i);
        if (inputFloat("##v", f[i], format, w)) changed = true;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s (Z is up)", names3[i]);
        ImGui::PopID();
    }
    ImGui::PopID();
    return changed;
}

void labelled(const char* text) {
    fieldLabel(text);
    gap(sp::XS + 2);
}

void drawPlacement(Ui& ui) {
    seedDemo();
    spawn::Placement p = placement();
    bool changed = false;
    int mode = int(p.mode);
    if (pillTabs("##mode", {"Crosshair", "Player", "Coordinates"}, &mode)) {
        p.mode = spawn::Mode(mode);
        changed = true;
    }
    gap(sp::M);
    switch (p.mode) {
    case spawn::Mode::Crosshair: {
        labelled("Distance");
        float d = p.distance;
        const Edit e = slider("##distance", &d, 1.0f, 40.0f, 0.5f, (availWidth() - px(96)) / scale());
        tooltip("Along your aim from the rendered camera, at a distance. No world raycast: a near wall swallows it.");
        ImGui::SameLine(0, px(10));
        if (inputFloat("##distv", &d, "%.1f m", 86)) changed = true;
        if (e != Edit::None || d != p.distance) {
            p.distance = d;
            changed = true;
        }
        gap(sp::S);
        labelled("Reticle on Screen  (0..1, Centre 0.5)");
        const float w = (availWidth() - px(8) - buttonWidth("Centre", nullptr, true) - px(8)) * 0.5f / scale();
        if (inputFloat("##sx", &p.screenX, "x %.3f", w)) changed = true;
        tooltip("0 = left edge, 1 = right edge");
        ImGui::SameLine(0, px(8));
        if (inputFloat("##sy", &p.screenY, "y %.3f", w)) changed = true;
        tooltip("0 = top, 1 = bottom. A lowered reticle needs a larger y");
        ImGui::SameLine(0, px(8));
        if (button("Centre", {.tone = Tone::Ghost, .small = true, .selected = p.screenX == 0.5f && p.screenY == 0.5f})) {
            p.screenX = p.screenY = 0.5f;
            changed = true;
        }
        break;
    }
    case spawn::Mode::Player: {
        labelled("Position Source");
        int src = int(p.source);
        static const char* const sources[] = {"Engine Position  (sObject +0xB0, Verified)", "Player Element  (+0x2810, Likely)"};
        if (combo("##source", &src, sources, 2, -1)) {
            p.source = spawn::PlayerSource(src);
            changed = true;
        }
        tooltip("Both are the player's position. +0xB0 is what the engine's position getter returns; +0x2810 is the Xbox tool's "
                "source and still needs a live check on PS3 (TESTING > Port checks).");
        break;
    }
    case spawn::Mode::Coordinates: {
        labelled("X / Y / Z");
        if (vec3Fields("##coords", &p.coords)) changed = true;
        gap(sp::S);
        {
            auto offline = requireConnection(ui);
            if (button("Use My Position", {.tone = Tone::Ghost, .icon = icon::Home, .small = true, .tooltip = "Read your position into the fields",
                                           .busy = ui.busy("Read my position")})) {
                const spawn::PlayerSource src = p.source;
                post("Read my position", [src](Ctx& c) {
                    const Vec3 v = readPlayerPosition(c, src);
                    spawn::Placement q = placement();
                    q.coords = v;
                    setPlacement(q);
                    c.log("[spawn] coordinates set to my position " + vec3Text(v, 2));
                });
            }
        }
        ImGui::SameLine(0, px(6));
        if (button("Copy", {.tone = Tone::Ghost, .icon = icon::Copy, .small = true, .tooltip = "Copy as \"x, y, z\""}))
            copyToClipboard(vec3Text(p.coords, 3), "coordinates");
        ImGui::SameLine(0, px(6));
        if (button("Paste", {.tone = Tone::Ghost, .icon = icon::Clipboard, .small = true, .tooltip = "Paste \"x, y, z\" (any separators)"})) {
            const char* clip = ImGui::GetClipboardText();
            float v[3];
            if (clip && std::sscanf(clip, " %f%*[ ,;\t]%f%*[ ,;\t]%f", &v[0], &v[1], &v[2]) == 3) {
                p.coords = {v[0], v[1], v[2]};
                changed = true;
            } else {
                notify("The clipboard does not hold three numbers", Level::Warn);
            }
        }
        break;
    }
    }
    if (p.mode != spawn::Mode::Coordinates) {
        gap(sp::S);
        labelled("Offset  (Metres; Z Up)");
        if (vec3Fields("##offset", &p.offset, "%.1f")) changed = true;
    }
    divider(sp::M);
     
    {
        const float colW = (availWidth() - px(12)) * 0.5f;
        ImGui::BeginGroup();
        labelled("Count");
        if (inputInt("##count", &p.count, 1, spawn::kMaxCount, colW / scale())) changed = true;
        ImGui::EndGroup();
        ImGui::SameLine(0, px(12));
        ImGui::BeginGroup();
        labelled("Spacing");
        DisabledScope one(p.count <= 1, "Only for a count above 1");
        if (inputFloat("##spacing", &p.spacing, "%.1f m", colW / scale())) changed = true;
        ImGui::EndGroup();
        gap(sp::S);
        ImGui::BeginGroup();
        labelled("Scale");
        if (inputFloat("##scale", &p.scale, "%.2f", colW / scale())) changed = true;
        tooltip("Descriptor scale (1 = normal, 0.05 .. 20)");
        ImGui::EndGroup();
        ImGui::SameLine(0, px(12));
        ImGui::BeginGroup();
        labelled("Facing");
        int f = int(p.facing);
        static const char* const facings[] = {"Face Me", "Face Along Aim", "World Axes"};
        if (combo("##facing", &f, facings, 3, colW / scale())) {
            p.facing = spawn::Facing(f);
            changed = true;
        }
        tooltip("Face me: turn toward the camera (upright). Along aim: as the Xbox crosshair spawn. World axes: no rotation.");
        ImGui::EndGroup();
    }
    if (changed) setPlacement(p);
    gap(sp::M);
     
    {
        auto offline = requireConnection(ui);
        if (button("Where Would It Go?", {.tone = Tone::Ghost, .icon = icon::View, .small = true,
                                           .tooltip = "Resolve the placement now (reads the camera / your position) without spawning",
                                           .busy = ui.busy("Preview placement")})) {
            const spawn::Placement q = placement();
            post("Preview placement", [q](Ctx& c) {
                world::RuntimeReader mem(c.rt());
                spawn::PlacementInputs in;
                if (const auto cam = mem.tryBytes(spawn::kViewMatrix, spawn::kCameraBytes)) in.camera = spawn::parseMatrices(*cam);
                if (q.mode == spawn::Mode::Player || q.facing == spawn::Facing::TowardMe) {
                    try {
                        in.player = readPlayerPosition(c, q.source);
                    } catch (const std::exception&) {
                    }
                }
                const auto pts = spawn::resolvePoints(q, in);
                std::string text = "First point " + vec3Text(pts.front().position, 2);
                if (pts.size() > 1) text += ", last " + vec3Text(pts.back().position, 2);
                if (in.player) {
                    char d[48];
                    std::snprintf(d, sizeof d, " (%.1f m from you)", double(world::distance(*in.player, pts.front().position)));
                    text += d;
                }
                c.log("[spawn] placement preview: " + text);
                c.toast(text);
            });
        }
    }
    ImGui::SameLine(0, px(10));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(6));
    caption(spawn::summary(p).c_str(), pal::Faint);
}

 
 
 

void drawSpawned(Ui& ui) {
    seedDemo();
    std::vector<SpawnedRow> rows;
    {
        State& s = st();
        std::lock_guard<std::mutex> lock(s.m);
        rows = s.spawned;
    }
    size_t live = 0;
    for (const SpawnedRow& r : rows) live += r.gone ? 0 : 1;
    {
        auto offline = requireConnection(ui);
        DisabledScope none(live == 0, "Nothing spawned this session is still there");
        if (button("Despawn All", {.tone = Tone::Danger, .icon = icon::Delete, .small = true,
                                   .tooltip = "Despawn everything listed here (asks first)", .busy = ui.busy(kJobDespawn)})) {
            confirm("Despawn Everything You Spawned?",
                    std::to_string(live) + " object" + (live == 1 ? "" : "s") + " spawned this session will be removed from the world.",
                    "Despawn All", [] { post(kJobDespawn, [](Ctx& c) { despawnMine(c); }); });
        }
    }
    ImGui::SameLine(0, px(6));
    {
        auto offline = requireConnection(ui);
        DisabledScope none(rows.empty(), "Nothing spawned yet");
        if (button("Check", {.tone = Tone::Ghost, .icon = icon::Refresh, .small = true, .tooltip = "Re-read which of them are still live",
                             .busy = ui.busy("Check spawned objects")}))
            post("Check spawned objects", [](Ctx& c) { checkSpawnedAlive(c); });
    }
    ImGui::SameLine(0, px(6));
    {
        DisabledScope none(rows.empty(), "Nothing to clear");
        if (button("Clear List", {.tone = Tone::Ghost, .small = true, .tooltip = "Forget the list (the objects stay in the world)"})) {
            State& s = st();
            std::lock_guard<std::mutex> lock(s.m);
            s.spawned.clear();
        }
    }
    if (!rows.empty()) {
        char buf[48];
        snprintf(buf, sizeof buf, "%zu live of %zu", live, rows.size());
        const ImVec2 ts = textSize(FontKind::Body, type::Small, buf);
        ImGui::SameLine();
        alignRight(ts.x);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(5));
        caption(buf, pal::Faint);
    }
    gap(sp::M);
    if (rows.empty()) {
        emptyState(icon::History, "Nothing Spawned Yet", "What You Spawn This Session Lists Here", 150);
        return;
    }
    const float tableH = std::min(330.0f, 46.0f + float(rows.size()) * 39.0f);
    if (beginTable("##spawned", 4, tableH, ImGuiTableFlags_BordersOuter)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Handle", ImGuiTableColumnFlags_WidthFixed, px(78));
        ImGui::TableSetupColumn("When", ImGuiTableColumnFlags_WidthFixed, px(70));
        ImGui::TableSetupColumn("##act", ImGuiTableColumnFlags_WidthFixed, px(64));
        tableHeaders();
        ImGui::PushFont(font(FontKind::Body), fontSize(FontKind::Body, type::Label));
        for (size_t k = rows.size(); k-- > 0;) {
            const SpawnedRow& r = rows[k];
            ImGui::TableNextRow(0, px(38));
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(int(k));
            const float y = ImGui::GetCursorPosY();
            if (tableRowSelectable("##sel", false, nullptr, 38 - 2 * ImGui::GetStyle().CellPadding.y / scale())) {
                select(r.tag);
                pg().scrollToSelected = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("Select %s in the catalogue\nAt %s", spawn::tagText(r.tag).c_str(), vec3Text(r.position).c_str());
            ImGui::SetCursorPosY(y + px(1));
            {
                ImDrawList* d = ImGui::GetWindowDrawList();
                const ImVec2 p0 = ImGui::GetCursorScreenPos();
                const float cw = ImGui::GetContentRegionAvail().x;
                const std::string name = r.label.empty() ? displayName(r.tag) : r.label;
                const std::string cut = ellipsize(name.c_str(), cw, FontKind::Body, type::Label);
                drawText(d, FontKind::Body, type::Label, p0, u32(r.gone ? pal::Faint : pal::Text), cut.c_str());
                const std::string sub = std::string(spawn::categoryName(r.type)) + "  \xC2\xB7  " + vec3Text(r.position) + (r.gone ? "  \xC2\xB7  gone" : "");
                drawText(d, FontKind::Body, type::Tiny, p0 + ImVec2(0, px(17)), u32(pal::Faint), ellipsize(sub.c_str(), cw, FontKind::Body, type::Tiny).c_str());
            }
            ImGui::TableSetColumnIndex(1);
            cellText(world::hex8(r.handle).c_str(), r.gone ? pal::Decor : pal::Muted, FontKind::Mono, type::MonoSmall, 38);
            ImGui::TableSetColumnIndex(2);
            cellText(ago(r.at).c_str(), pal::Faint, FontKind::Body, type::Small, 38);
            ImGui::TableSetColumnIndex(3);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(2));
            {
                auto offline = requireConnection(ui);
                DisabledScope gone(r.gone, "Already gone");
                if (iconButton("##despawn", icon::Delete, "Despawn this one", false, 28, pal::Danger)) {
                    const uint32_t handle = r.handle;
                    post(kJobDespawn, [handle](Ctx& c) { runDespawn(c, {handle}); });
                }
            }
            ImGui::SameLine(0, px(2));
            if (iconButton("##copy", icon::Copy, "Copy the handle", false, 28)) copyToClipboard(world::hex8(r.handle), "handle");
            ImGui::PopID();
        }
        ImGui::PopFont();
        endTable();
    }
}

 
 
 

struct Preset {
    uint32_t tag;
    uint8_t type;
    const char* label;
};
const Preset kPresets[] = {
    {0x8108F764, 11, "Sword relic (Crota's End, pkg 447)"},
    {0x80D4CA80, 11, "Sword relic (Moon missions, pkg 2A6)"},
    {0x80FE7214, 11, "Aegis relic (Venus / VoG, pkg 3F3)"},
    {0x8119435A, 8, "PoE chest model 8119435A"},
    {0x81194359, 8, "PoE o_chest definition"},
    {0x81194354, 8, "PoE o_chest_1 definition"},
    {0x8119435B, 8, "PoE o_chest_2 definition"},
    {0x81194356, 8, "PoE o_chest_controller definition"},
    {0x80CCB26F, 8, "Spinmetal node (known-good control)"},
    {0x8108E107, 9, "Thrall (known-good control)"},
};

void drawManual(Ui& ui) {
    CatPage& p = pg();
    labelled("Tag");
    const float bw = buttonWidth("Spawn", icon::Play) + buttonWidth("Check", icon::Refresh) + px(16);
    inputMono("##mtag", &p.manualTag, "80C7A2F0", std::max(120.0f, (availWidth() - bw) / scale() - 8));
    uint32_t tag = 0;
    const bool valid = spawn::parseTag(p.manualTag, tag);
    const bool content = valid && world::isPackagePage(world::tagPage(names::toPs3Tag(tag)));
    ImGui::SameLine(0, px(8));
    {
        auto offline = requireConnection(ui);
        DisabledScope bad(!content, "Enter a content tag (8 hex digits, page 0x400 or above)");
        if (button("Spawn##manual", {.tone = Tone::Primary, .icon = icon::Play, .tooltip = "Spawn this tag with the current placement", .busy = spawnBusy()}))
            startSpawn(tag, std::nullopt, knownName(tag).empty() ? p.manualLabel : std::string());
        ImGui::SameLine(0, px(8));
        if (button("Check##manual", {.tone = Tone::Ghost, .icon = icon::Refresh, .tooltip = "Is it loaded right now?", .busy = ui.busy(kJobCheck)}))
            post(kJobCheck, [tag](Ctx& c) {
                const TagCheck k = checkTag(c, tag);
                c.toast(spawn::tagText(tag) + (k.resident ? ": loaded, type " + std::to_string(k.type) + " (" + spawn::categoryName(k.type) + ")" : ": " + k.why));
            });
    }
    gap(sp::XS + 2);
    if (!p.manualTag.empty() && !valid) caption("Not a tag: 8 hex digits, e.g. 80C7A2F0", pal::Warning);
    else if (valid && !content) caption("That is a runtime page, not content (page below 0x400)", pal::Warning);
    else if (valid) {
        const int etype = catalogType(tag);
        const std::string nm = knownName(tag);
        const std::string line = etype >= 0 ? std::string(spawn::categoryName(uint8_t(etype))) + "  \xC2\xB7  " + (nm.empty() ? std::string("no name yet") : nm)
                                            : std::string("Not in the catalogue: spawned with its live type");
        caption(line.c_str(), pal::Faint);
        ImGui::SameLine(0, px(8));
        if (button("Select", {.tone = Tone::Ghost, .small = true, .tooltip = "Select it in the catalogue (detail panel, label, favourites)"})) {
            select(tag);
            p.scrollToSelected = true;
            p.viewDirty = true;
        }
    }
    gap(sp::M);
    fieldLabel("Label");
    gap(sp::XS + 2);
    {
        const float saveW = buttonWidth("Save as Name", icon::Check, true) + px(8);
        inputText("##mlabel", &p.manualLabel, "What to Call It (Log, Spawned List)", (availWidth() - saveW) / scale());
        ImGui::SameLine(0, px(8));
        DisabledScope bad(!content || p.manualLabel.empty(), "Enter a content tag and a label");
        if (button("Save as Name", {.tone = Tone::Ghost, .icon = icon::Check, .small = true,
                                    .tooltip = "Save it as your label for this tag (shows everywhere; names_user.tsv)"})) {
            names::Label l = names::Labels::get().find(tag).value_or(names::Label{});
            l.name = p.manualLabel;
            names::Labels::get().set(tag, l);
            p.hayBuilt = false;
            p.viewDirty = true;
            notify("Saved " + spawn::tagText(tag) + " as \"" + p.manualLabel + "\"", Level::Good);
        }
    }
    gap(sp::M);
    fieldLabel("Presets  (Xbox Tool)");
    gap(sp::XS + 2);
    for (const Preset& pr : kPresets) {
        ImGui::PushID(int(pr.tag));
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float w = availWidth();
        const float h = px(28);
        ImGui::SetNextItemAllowOverlap();
        const bool clicked = ImGui::InvisibleButton("##preset", ImVec2(w, h));
        const bool hov = ImGui::IsItemHovered();
        ImDrawList* d = ImGui::GetWindowDrawList();
        if (hov) d->AddRectFilled(pos, pos + ImVec2(w, h), u32(pal::RaisedHi), px(6));
        drawText(d, FontKind::Mono, type::MonoSmall, ImVec2(pos.x + px(8), textTop(FontKind::Mono, type::MonoSmall, pos.y + h * 0.5f)), u32(pal::Accent),
                 spawn::tagText(pr.tag).c_str());
        drawText(d, FontKind::Body, type::Small, ImVec2(pos.x + px(84), textTop(FontKind::Body, type::Small, pos.y + h * 0.5f)), u32(pal::Text), pr.label);
        const char* cat = spawn::categoryName(pr.type);
        const ImVec2 cs = textSize(FontKind::Body, type::Tiny, cat);
        drawText(d, FontKind::Body, type::Tiny, ImVec2(pos.x + w - cs.x - px(8), textTop(FontKind::Body, type::Tiny, pos.y + h * 0.5f)), u32(pal::Faint), cat);
        if (hov) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            tooltip("Put it in the tag field (then Spawn)");
        }
        if (clicked) {
            p.manualTag = spawn::tagText(pr.tag);
            p.manualLabel = pr.label;
        }
        ImGui::PopID();
    }
}

 
 
 

ImVec4 verdictColor(const std::string& group) {
    if (group == "main") return pal::Accent;
    if (group == "normal") return pal::Info;
    if (group == "broken") return pal::Danger;
    return pal::Muted;
}

void drawOpeners(Ui& ui) {
    CatPage& p = pg();
    const auto& ops = spawn::openers();
    if (ops.empty()) {
        emptyState(icon::Warning, "Openers List Unavailable", spawn::openersError().c_str(), 160);
        return;
    }
    int counts[4] = {0, 0, 0, 0};
    const char* keys[4] = {"main", "normal", "broken", "nonui"};
    for (const spawn::Opener& o : ops)
        for (int i = 0; i < 4; ++i)
            if (o.group == keys[i]) ++counts[i];
    const float searchW = std::min(px(280), availWidth() * 0.32f);
    searchField("##ofilter", &p.openerFilter, "Search Vendors", searchW / scale());
    ImGui::SameLine(0, px(10));
    std::vector<std::string> tabs = {"Hidden  " + std::to_string(counts[0]), "Vendors  " + std::to_string(counts[1]),
                                     "Chain Broken  " + std::to_string(counts[2]), "No UI  " + std::to_string(counts[3])};
    pillTabs("##ogroup", tabs, &p.openerGroup);
    static const char* const notes[] = {
        "Spawn the entity, walk up and interact as with any vendor. Owner-verified where marked [O]; the rest are read from package bytes.",
        "Regular vendors and kiosks: they open their normal screens (useful away from their usual spot).",
        "Spawnable, but their launch chain leads nowhere: nothing opens. Kept for reference.",
        "Interactables without a UI (jukebox, fan, relic pickup...)."};
    tooltip(notes[p.openerGroup]);
    gap(sp::S);
    const auto words = spawn::queryWords(p.openerFilter);
    std::vector<const spawn::Opener*> view;
    for (const spawn::Opener& o : ops) {
        if (o.group != keys[p.openerGroup]) continue;
        if (!words.empty()) {
            std::string hay = lower(o.name + " " + o.where + " " + o.opens + " " + o.note);
            for (uint32_t t : o.tags) hay += " " + lower(spawn::tagText(t));
            if (!spawn::matches(hay, words)) continue;
        }
        view.push_back(&o);
    }
    const ResidencyView& rv = residency();
    const float rowH = 46;
    const float h = std::min(420.0f, 36.0f + float(std::max<size_t>(view.size(), 1)) * rowH);
    if (beginTable("##openers", 5, h, ImGuiTableFlags_BordersOuter)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##res", ImGuiTableColumnFlags_WidthFixed, px(12));
        ImGui::TableSetupColumn("Entity", ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableSetupColumn("Where to Be", ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableSetupColumn("Opens", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("##act", ImGuiTableColumnFlags_WidthFixed, px(118));
        tableHeaders();
        ImGui::PushFont(font(FontKind::Body), fontSize(FontKind::Body, type::Label));
        for (size_t k = 0; k < view.size(); ++k) {
            const spawn::Opener& o = *view[k];
            const uint32_t tag = o.tags.front();
            ImGui::TableNextRow(0, px(rowH));
            ImGui::PushID(int(k));
            ImGui::TableSetColumnIndex(0);
            resDot(rv.of(tag), rowH);
            ImGui::TableSetColumnIndex(1);
            {
                ImDrawList* d = ImGui::GetWindowDrawList();
                const ImVec2 p0 = ImGui::GetCursorScreenPos();
                const float cw = ImGui::GetContentRegionAvail().x;
                drawText(d, FontKind::Semibold, type::Label, p0 + ImVec2(0, px(2)), u32(pal::Text), ellipsize(o.name.c_str(), cw, FontKind::Semibold, type::Label).c_str());
                std::string tags;
                for (uint32_t t : o.tags) tags += (tags.empty() ? "" : "  ") + spawn::tagText(t);
                drawText(d, FontKind::Mono, type::MonoSmall - 1, p0 + ImVec2(0, px(21)), u32(verdictColor(o.group)),
                         ellipsize(tags.c_str(), cw, FontKind::Mono, type::MonoSmall - 1).c_str());
                ImGui::Dummy(ImVec2(cw, px(rowH) - 2 * ImGui::GetStyle().CellPadding.y));
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                    std::string tip = o.name + (o.verdict.empty() ? "" : "\nVerdict: " + o.verdict);
                    if (!o.note.empty()) tip += "\n" + o.note;
                    ImGui::SetTooltip("%s", tip.c_str());
                }
            }
            ImGui::TableSetColumnIndex(2);
            cellText(o.where.c_str(), pal::Muted, FontKind::Body, type::Small, rowH);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", o.where.c_str());
            ImGui::TableSetColumnIndex(3);
            cellText(o.opens.empty() ? "-" : o.opens.c_str(), o.opens.empty() ? pal::Decor : pal::Muted, FontKind::Body, type::Small, rowH);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !o.opens.empty()) ImGui::SetTooltip("%s", o.opens.c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (px(rowH) - 2 * ImGui::GetStyle().CellPadding.y - px(kSmallControlH)) * 0.5f);
            {
                auto offline = requireConnection(ui);
                if (button("Spawn", {.tone = o.group == "main" ? Tone::Primary : Tone::Secondary, .icon = icon::Play, .small = true,
                                     .tooltip = "Spawn it with the current placement (crosshair by default)", .busy = spawnBusy()}))
                    startSpawn(tag);
            }
            ImGui::SameLine(0, px(4));
            if (iconButton("##sel", icon::List, "Select it in the catalogue", false, 28)) {
                select(tag);
                p.scrollToSelected = true;
            }
            ImGui::PopID();
        }
        ImGui::PopFont();
        if (view.empty()) {
            ImGui::TableNextRow(0, px(60));
            ImGui::TableSetColumnIndex(1);
            gap(sp::M);
            caption("Nothing matches the search.", pal::Muted);
        }
        endTable();
    }
}

 
 
 

void paletteEntries(const std::string& query, std::vector<PaletteEntry>& out) {
    ensureRows();
    auto add = [&](uint32_t tag, const char* why, int boost, const char* glyph) {
        PaletteEntry pe;
        pe.label = "Spawn " + displayName(tag);
        const std::string loc = location(tag);
        pe.where = std::string("Spawner  \xE2\x80\xBA  ") + spawn::tagText(tag) + (loc.empty() ? "" : "  \xC2\xB7  " + loc) + (why ? std::string("  \xC2\xB7  ") + why : "");
        pe.icon = glyph;
        const int etype = catalogType(tag);
        pe.badge = etype >= 0 ? spawn::categoryName(uint8_t(etype)) : "tag";
        pe.boost = boost;
        pe.run = [tag] {
            select(tag);
            pg().scrollToSelected = true;
            pg().viewDirty = true;
            gotoTab(kTabSpawn);
        };
        pe.alt = [tag] { startSpawn(tag); };
        pe.altHint = "Spawn Now";
        out.push_back(std::move(pe));
    };
    std::set<uint32_t> seen;
    for (uint32_t t : favourites())
        if (seen.insert(t).second) add(t, "favourite", 300, icon::StarFill);
    for (const spawn::Opener& o : spawn::openers())
        if (o.group == "main" && seen.insert(o.tags.front()).second) add(o.tags.front(), "hidden vendor", 120, icon::Spawner);
    int n = 0;
    for (uint32_t t : recents()) {
        if (n >= 6) break;
        if (seen.insert(t).second) {
            add(t, "recent", 80, icon::History);
            ++n;
        }
    }
    if (query.size() < 2) return;
    for (const spawn::Opener& o : spawn::openers())
        if (o.group == "normal" && seen.insert(o.tags.front()).second && spawn::matches(lower(o.name), spawn::queryWords(query)))
            add(o.tags.front(), "vendor", 60, icon::Spawner);
    const auto words = spawn::queryWords(query);
    CatPage& p = pg();
    n = 0;
    for (int i = 0; i < int(p.rows.size()) && n < 40; ++i) {
        const uint32_t t = p.rows[size_t(i)].tag;
        if (seen.count(t) || knownName(t).empty()) continue;    
        if (spawn::matches(hayOf(i), words)) {
            add(t, nullptr, 0, icon::Spawner);
            ++n;
        }
    }
}

}   
}   

namespace brps3 {

BR_FEATURES(spawn_page) {
    using namespace spawnui;
    const Page P = Page::Spawner;
    r.section({.id = "spawn.catalog", .page = P, .tab = kTabSpawn, .section = kSecCatalog, .order = 10, .draw = drawCatalog,
               .onDisconnect = resetSession});
    r.section({.id = "spawn.placement", .page = P, .tab = kTabSpawn, .section = kSecPlacement, .order = 20, .draw = drawPlacement,
               .badge = kBadgeNeedsCheck});
    r.section({.id = "spawn.spawned", .page = P, .tab = kTabSpawn, .section = kSecSpawned, .order = 30, .draw = drawSpawned});
    r.section({.id = "spawn.manual", .page = P, .tab = kTabSpawn, .section = kSecManual, .order = 40, .draw = drawManual});
    r.section({.id = "spawn.openers", .page = P, .tab = kTabSpawn, .section = kSecOpeners, .order = 50, .draw = drawOpeners});
     
    r.action({.id = kActSpawnSelected, .page = P, .tab = kTabSpawn, .section = kSecCatalog, .label = "Spawn Selected Entity",
              .help = "Spawn the entity selected on the Spawner page with the current placement and count.",
              .run = [](Ctx& c) { spawnSelected(c); }});
    r.action({.id = kActSpawnCrosshair, .page = P, .tab = kTabSpawn, .section = kSecCatalog, .label = "Spawn Selected at Crosshair",
              .help = "Spawn the selected entity where you aim (rendered camera + distance), whatever the placement mode.",
              .run = [](Ctx& c) { spawnSelected(c, spawn::Mode::Crosshair); }, .badge = kBadgeNeedsCheck});
    r.action({.id = kActSpawnPlayer, .page = P, .tab = kTabSpawn, .section = kSecCatalog, .label = "Spawn Selected at My Position",
              .help = "Spawn the selected entity at your position plus the placement offset.",
              .run = [](Ctx& c) { spawnSelected(c, spawn::Mode::Player); }, .badge = kBadgeNeedsCheck});
    r.action({.id = "spawn.mode_crosshair", .page = P, .tab = kTabSpawn, .section = kSecPlacement, .label = "Placement: At Crosshair",
              .help = "Spawn where you aim (rendered camera + distance).", .run = [](Ctx&) {}, .badge = kBadgeNeedsCheck, .immediate = [] {
                  spawn::Placement q = placement();
                  q.mode = spawn::Mode::Crosshair;
                  setPlacement(q);
                  ui::notify("Placement: crosshair", ui::Level::Good);
              }});
    r.action({.id = "spawn.mode_player", .page = P, .tab = kTabSpawn, .section = kSecPlacement, .label = "Placement: At My Position",
              .help = "Spawn at your position plus the offset.", .run = [](Ctx&) {}, .badge = kBadgeNeedsCheck, .immediate = [] {
                  spawn::Placement q = placement();
                  q.mode = spawn::Mode::Player;
                  setPlacement(q);
                  ui::notify("Placement: at my position", ui::Level::Good);
              }});
    r.action({.id = "spawn.mode_coords", .page = P, .tab = kTabSpawn, .section = kSecPlacement, .label = "Placement: At Coordinates",
              .help = "Spawn at the coordinates in the Placement card.", .run = [](Ctx&) {}, .immediate = [] {
                  spawn::Placement q = placement();
                  q.mode = spawn::Mode::Coordinates;
                  setPlacement(q);
                  ui::notify("Placement: coordinates " + vec3Text(q.coords), ui::Level::Good);
              }});
    r.action({.id = kActDespawnLast, .page = P, .tab = kTabSpawn, .section = kSecSpawned, .label = "Despawn Last Spawned",
              .help = "Despawn the newest object you spawned this session that is still there (guarded: refuses stale handles).",
              .run = despawnLast});
    r.action({.id = "spawn.check_spawned", .page = P, .tab = kTabSpawn, .section = kSecSpawned, .label = "Check Which Spawned Objects Are Live",
              .help = "Re-read the handles of everything spawned this session and mark the ones that are gone.", .run = [](Ctx& c) {
                  checkSpawnedAlive(c);
                  size_t live = 0, all = 0;
                  {
                      State& s = st();
                      std::lock_guard<std::mutex> lock(s.m);
                      all = s.spawned.size();
                      for (const SpawnedRow& row : s.spawned) live += row.gone ? 0 : 1;
                  }
                  c.toast(std::to_string(live) + " of " + std::to_string(all) + " spawned objects are still live");
              }});
    r.action({.id = kActDespawnMine, .page = P, .tab = kTabSpawn, .section = kSecSpawned, .label = "Despawn Everything I Spawned",
              .help = "Despawn every object you spawned this session.", .run = despawnMine, .dangerous = true});
    ui::styleSection(kSecCatalog, {.fullWidth = true, .icon = ui::icon::Spawner, .ownsActions = true});
    ui::styleSection(kSecPlacement, {.icon = ui::icon::Follow, .ownsActions = true});
    ui::styleSection(kSecSpawned, {.icon = ui::icon::History, .ownsActions = true});
    ui::styleSection(kSecManual, {.icon = ui::icon::Tag});
    ui::styleSection(kSecOpeners, {.fullWidth = true, .icon = ui::icon::Lightbulb});
    ui::addPaletteProvider(paletteEntries);
}

}   
