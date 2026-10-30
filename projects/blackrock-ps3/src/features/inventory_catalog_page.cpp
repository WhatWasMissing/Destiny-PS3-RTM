 
 
 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>

#include "app/engine.h"
#include "app/registry.h"
#include "app/settings.h"
#include "features/inventory_shared.h"
#include "game/names_catalog.h"
#include "game/world_mem.h"
#include "ui/widgets.h"

namespace brps3::invui {

using namespace inventory;
using namespace ui;

namespace {

constexpr float kRowH = 30;

struct CatState {
    int mode = 0;                        
    std::string query, perkQuery;
    int rarity = 0;                      
    int kind = 0;                        
    bool ownedOnly = false;
    bool namedOnly = true;
    std::vector<uint16_t> view;
    bool dirty = true;
    std::string builtQuery;
    uint64_t ownedFor = ~0ull;
    std::map<uint16_t, int> owned;       
    int selected = -1;                   
    bool scrollTo = false;
    std::vector<uint16_t> perkView;
    bool perkDirty = true;
    int perkSelected = -1;
    ImGuiTableColumnSortSpecs sort{};
    bool sorted = false;
};

CatState& cs() {
    static CatState s;
    return s;
}

ImVec4 rarityColor(int r) {
    switch (r) {
    case 1: return rgb(0xC9C4BC);
    case 2: return rgb(0x5BAF6A);
    case 3: return rgb(0x5B8FD6);
    case 4: return rgb(0xA071D6);
    case 5: return rgb(0xE6C445);
    default: return pal::Muted;
    }
}

void refreshOwned(CatState& s) {
    Shared& S = shared();
    std::shared_ptr<const Snapshot> snap;
    uint64_t gen;
    {
        std::lock_guard<std::mutex> lock(S.m);
        snap = S.snap;
        gen = S.generation;
    }
    if (gen == s.ownedFor) return;
    s.ownedFor = gen;
    s.owned.clear();
    if (snap)
        for (const Item& it : snap->items) s.owned[it.def] += int(it.soid ? 1 : it.quantity);
    if (s.ownedOnly) s.dirty = true;
}

void rebuild(CatState& s) {
    const Catalog& cat = catalog();
    s.view.clear();
    std::vector<std::string> words;
    {
        std::string cur;
        for (char c : lowerAscii(s.query)) {
            if (c == ' ') {
                if (!cur.empty()) words.push_back(cur);
                cur.clear();
            } else {
                cur += c;
            }
        }
        if (!cur.empty()) words.push_back(cur);
    }
    for (const ItemDef& d : cat.items()) {
        if (s.namedOnly && d.name.empty() && s.query.empty()) continue;
        if (s.rarity == 1 && d.rarity != 5) continue;
        if (s.rarity == 2 && d.rarity != 4) continue;
        if (s.rarity == 3 && d.rarity != 3) continue;
        if (s.rarity == 4 && d.rarity != 1 && d.rarity != 2) continue;
        if (s.kind && (bucketGroup(d.home) != (s.kind == 3 ? 0 : s.kind))) continue;
        if (s.ownedOnly && !s.owned.count(d.def)) continue;
        bool ok = true;
        for (const std::string& w : words)
            if (cat.hay(d.def).find(w) == std::string::npos) {
                ok = false;
                break;
            }
        if (ok) s.view.push_back(d.def);
    }
    if (s.sorted) {
        const ImGuiID col = s.sort.ColumnUserID;
        const bool asc = s.sort.SortDirection != ImGuiSortDirection_Descending;
        std::stable_sort(s.view.begin(), s.view.end(), [&](uint16_t a, uint16_t b) {
            const ItemDef& x = cat.items()[a];
            const ItemDef& y = cat.items()[b];
            int c = 0;
            switch (col) {
            case 0: c = int(favourite(y.hash)) - int(favourite(x.hash)); break;
            case 2: c = x.type.compare(y.type); break;
            case 3: c = int(x.rarity == 255 ? 0 : x.rarity) - int(y.rarity == 255 ? 0 : y.rarity); break;
            case 4: c = bucketName(x.home).compare(bucketName(y.home)); break;
            case 5: c = x.hash < y.hash ? -1 : x.hash > y.hash ? 1 : 0; break;
            case 6: c = int(x.def) - int(y.def); break;
            case 7: {
                auto ox = s.owned.find(x.def), oy = s.owned.find(y.def);
                c = (ox == s.owned.end() ? 0 : ox->second) - (oy == s.owned.end() ? 0 : oy->second);
                break;
            }
            default: c = lowerAscii(x.label()).compare(lowerAscii(y.label())); break;
            }
            return asc ? c < 0 : c > 0;
        });
    }
    s.dirty = false;
}

void itemDetail(const ItemDef& d, float width, float height) {
    const Catalog& cat = catalog();
    CatState& s = cs();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, pal::Raised);
    ImGui::PushStyleColor(ImGuiCol_Border, pal::Border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(kRowRounding));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(16), px(14)));
    ImGui::BeginChild("##catdetail", ImVec2(width, height), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    const std::string rt = rarityText(d.rarity);
    if (!rt.empty()) {
        pill(rt.c_str(), rarityColor(d.rarity));
        ImGui::SameLine(0, px(6));
    }
    pill(bucketName(d.home).c_str(), pal::Muted);
    ImGui::SameLine();
    alignRight(px(30));
    const bool fav = favourite(d.hash);
    if (iconButton("##fav", fav ? icon::StarFill : icon::Star, fav ? "Remove from favourites" : "Add to favourites", false, 30, fav ? pal::Accent : pal::Muted))
        toggleFavourite(d.hash);
    gap(sp::S);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + availWidth());
    title(d.label().c_str());
    ImGui::PopTextWrapPos();
    caption((d.type.empty() ? std::string("Item") : d.type).c_str(), pal::Muted);
    if (!d.alias.empty()) caption(("Display name: " + d.alias).c_str(), pal::Faint);
    gap(sp::M);
    {
        const float w = std::floor((availWidth() - px(8)) / 2);
        statTile("stack", "STACKING", d.perInstance ? std::string("Instance") : "Stack of " + std::to_string(d.maxStack),
                 d.uniq != kDefaultUniq ? "unique group " + world::hex8(d.uniq) : "", w);
        ImGui::SameLine(0, px(8));
        auto owned = s.owned.find(d.def);
        statTile("owned", "OWNED", owned == s.owned.end() ? std::string("0") : std::to_string(owned->second),
                 current() ? "in the last snapshot" : "no snapshot yet", w, owned == s.owned.end() ? pal::Text : pal::Success);
    }
    gap(sp::M);
    fieldLabel("IDENTITY");
    gap(sp::XS + 2);
    auto row = [](const char* name, const std::string& value, bool copy) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        drawText(ImGui::GetWindowDrawList(), FontKind::Body, type::Small, ImVec2(p.x, p.y + px(3)), u32(pal::Faint), name);
        ImGui::SetCursorScreenPos(ImVec2(p.x + px(110), p.y));
        if (copy) copyable(value.c_str(), name, pal::Muted);
        else {
            ImGui::SetCursorScreenPos(ImVec2(p.x + px(110), p.y + px(2)));
            label(value.c_str(), pal::Muted);
        }
    };
    row("Definition", std::to_string(d.def), false);
    row("Item Hash", world::hex8(d.hash), true);
    row("Item Tag", world::hex8(d.tag) + " (Xbox id)", true);
    row("Home Bucket", bucketName(d.home) + " (" + std::to_string(d.home) + ")", false);
    if (d.fallback != 255) row("Fallback", bucketName(d.fallback), false);
    {
        std::string route = "-";
        try {
            if (const Bucket* b = catalog().bucket(d.home); b && !b->account) {
                const Route r = resolveRoute(cat, d, true, b->first);
                route = bucketName(r.source.key) + " \xE2\x86\x92 " + bucketName(r.destination.key);
            }
        } catch (const std::exception& e) {
            route = e.what();
        }
        {
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            drawText(ImGui::GetWindowDrawList(), FontKind::Body, type::Small, ImVec2(p0.x, p0.y + px(3)), u32(pal::Faint), "Vault Route");
            ImGui::SetCursorScreenPos(ImVec2(p0.x + px(110), p0.y + px(2)));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + availWidth());
            wrapped(route.c_str(), pal::Muted, type::Small);
            ImGui::PopTextWrapPos();
        }
    }
    if (!d.intrinsic.empty()) {
        gap(sp::M);
        fieldLabel("INTRINSIC PERKS");
        gap(sp::XS + 2);
        for (uint16_t p : d.intrinsic)
            if (const Perk* pk = cat.perk(p)) {
                label(pk->label().c_str(), pk->enabled ? pal::Text : pal::Faint);
                if (!pk->description.empty()) wrapped(pk->description.c_str(), pal::Muted, type::Small);
            }
    }
    if (d.grid != 0xFFFF) {
        if (const auto* g = cat.rollGrid(d.grid)) {
            gap(sp::M);
            char hdr[64];
            std::snprintf(hdr, sizeof hdr, "POSSIBLE ROLLS  (GRID %u)", d.grid);
            fieldLabel(hdr);
            gap(sp::XS + 2);
            for (const auto& [node, n] : *g) {
                std::string names;
                std::set<std::string> seen;
                for (const auto& [variant, text] : n.variants)
                    for (std::string part : splitChar(text, ';')) {
                        while (!part.empty() && part.front() == ' ') part.erase(part.begin());
                        if (!part.empty() && seen.insert(part).second) names += (names.empty() ? "" : ", ") + part;
                    }
                if (names.empty()) continue;
                char lead[32];
                std::snprintf(lead, sizeof lead, "Node %u%s", node, n.selector >= 0 ? " (rolled)" : "");
                caption(lead, n.selector >= 0 ? pal::Accent : pal::Faint);
                wrapped(names.c_str(), pal::Muted, type::Small);
                gap(sp::XS);
            }
        }
    }
     
    if (const auto snap = current()) {
        std::vector<const Item*> mine;
        for (const Item& it : snap->items)
            if (it.def == d.def) mine.push_back(&it);
        if (!mine.empty()) {
            gap(sp::M);
            fieldLabel("IN YOUR INVENTORY");
            gap(sp::XS + 2);
            for (const Item* it : mine) {
                ImGui::PushID(int(it->slot) + (it->owner == Owner::Account ? 1000 : 0));
                std::string text = locationName(*it) + " \xC2\xB7 " + bucketName(it->bucket) + (it->soid ? "  " + hex16(it->soid) : "  x" + std::to_string(it->quantity));
                if (button(text.c_str(), {.tone = Tone::Ghost, .icon = icon::Follow, .small = true, .tooltip = "Select it on the Inventory tab"}))
                    revealItem(selectionOf(*it));
                ImGui::PopID();
            }
        }
    }
    ImGui::EndChild();
}

void drawItems(Ui&) {
    CatState& s = cs();
    const Catalog& cat = catalog();
    refreshOwned(s);
    const float avail = availWidth();
    {
        const float searchW = std::min(px(340), avail * 0.30f);
        if (searchField("##catsearch", &s.query, "Search Name, Type, Hash or Definition", searchW / scale(), true)) s.dirty = true;
        const std::vector<std::string> rar = {"Any", "Exotic", "Legendary", "Rare", "Common"}, kinds = {"All", "Weapons", "Armor", "General"};
        auto width = [](const std::vector<std::string>& v) {
            float w = px(8);
            for (const std::string& t : v) w += textSize(FontKind::Semibold, type::Label, t.c_str()).x + px(30);
            return w;
        };
        const float rw = width(rar), kw = width(kinds);
        float x = searchW + px(12) + rw;
        if (x <= avail) {
            ImGui::SameLine(0, px(12));
        } else {
            gap(sp::S);
            x = rw;
        }
        if (pillTabs("##catrarity", rar, &s.rarity)) s.dirty = true;
        if (x + px(12) + kw <= avail) ImGui::SameLine(0, px(12));
        else gap(sp::S);
        if (pillTabs("##catkind", kinds, &s.kind)) s.dirty = true;
    }
    gap(sp::S);
    {
        bool owned = s.ownedOnly, named = s.namedOnly;
        const ImVec2 a = ImGui::GetCursorScreenPos();
        if (toggleSwitch("##owned", &owned)) {
            s.ownedOnly = owned;
            s.dirty = true;
        }
        drawText(ImGui::GetWindowDrawList(), FontKind::Body, type::Small, ImVec2(a.x + px(30), textTop(FontKind::Body, type::Small, a.y + px(11))),
                 u32(owned ? pal::Text : pal::Muted), "Owned Only");
        ImGui::SetCursorScreenPos(ImVec2(a.x + px(124), a.y));
        if (toggleSwitch("##named", &named)) {
            s.namedOnly = named;
            s.dirty = true;
        }
        drawText(ImGui::GetWindowDrawList(), FontKind::Body, type::Small, ImVec2(a.x + px(154), textTop(FontKind::Body, type::Small, a.y + px(11))),
                 u32(named ? pal::Text : pal::Muted), "Named Only");
        ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + px(22)));
        ImGui::Dummy(ImVec2(1, 1));
        if (s.dirty) rebuild(s);
        char buf[64];
        std::snprintf(buf, sizeof buf, "%zu of %zu definitions", s.view.size(), cat.size());
        ImGui::SameLine();
        alignRight(textSize(FontKind::Body, type::Small, buf).x);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(14));
        caption(buf, pal::Faint);
    }
    gap(sp::M);
    const bool split = avail >= px(900);
    const float detailW = split ? std::clamp(avail * 0.34f, px(320), px(440)) : avail;
    const float tableW = split ? avail - detailW - px(14) : avail;
    const float h = std::clamp(ImGui::GetWindowHeight() - px(280), px(380), px(900));
    if (s.selected < 0 && !s.view.empty()) s.selected = s.view.front();
     
    if (!s.view.empty() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        int pos = -1;
        for (int i = 0; i < int(s.view.size()); ++i)
            if (s.view[size_t(i)] == s.selected) pos = i;
        int next = pos;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) next = std::min(int(s.view.size()) - 1, pos + 1);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) next = std::max(0, pos - 1);
        if (next != pos && next >= 0) {
            s.selected = s.view[size_t(next)];
            s.scrollTo = true;
        }
    }
    if (beginTable("##catalog", 8, h / scale(), ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate | ImGuiTableFlags_BordersOuter, tableW / scale())) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##fav", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, px(16), 0);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.8f, 1);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 1.4f, 2);
        ImGui::TableSetupColumn("Rarity", ImGuiTableColumnFlags_WidthFixed, px(78), 3);
        ImGui::TableSetupColumn("Bucket", ImGuiTableColumnFlags_WidthFixed, px(108), 4);
        ImGui::TableSetupColumn("Hash", ImGuiTableColumnFlags_WidthFixed, px(80), 5);
        ImGui::TableSetupColumn("Def", ImGuiTableColumnFlags_WidthFixed, px(44), 6);
        ImGui::TableSetupColumn("Owned", ImGuiTableColumnFlags_WidthFixed, px(52), 7);
        tableHeaders();
        if (ImGuiTableSortSpecs* sp = ImGui::TableGetSortSpecs(); sp && sp->SpecsDirty) {
            s.sorted = sp->SpecsCount > 0;
            if (s.sorted) s.sort = sp->Specs[0];
            sp->SpecsDirty = false;
            rebuild(s);
        }
        ImGuiListClipper clip;
        clip.Begin(int(s.view.size()), px(kRowH));
        int scrollTo = -1;
        if (s.scrollTo) {
            for (int i = 0; i < int(s.view.size()); ++i)
                if (s.view[size_t(i)] == s.selected) scrollTo = i;
            if (scrollTo >= 0) clip.IncludeItemByIndex(scrollTo);
            s.scrollTo = false;
        }
        ImGui::PushFont(font(FontKind::Body), fontSize(FontKind::Body, type::Label));
        while (clip.Step()) {
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
                const ItemDef& d = cat.items()[s.view[size_t(r)]];
                const bool sel = s.selected == d.def;
                ImGui::TableNextRow(0, px(kRowH));
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(int(d.def));
                const float cellY = ImGui::GetCursorPosY();
                if (tableRowSelectable("##row", sel, nullptr, kRowH - 2 * ImGui::GetStyle().CellPadding.y / scale())) s.selected = d.def;
                if (r == scrollTo) ImGui::SetScrollHereY(0.4f);
                ImGui::SetCursorPosY(cellY);
                cellText(favourite(d.hash) ? icon::StarFill : "", pal::Accent, FontKind::Icons, 11, kRowH);
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                cellText(d.label().c_str(), sel ? pal::Accent : d.name.empty() ? pal::Faint : pal::Text, FontKind::Body, type::Label, kRowH);
                ImGui::TableSetColumnIndex(2);
                cellText(d.type.c_str(), pal::Muted, FontKind::Body, type::Label, kRowH);
                ImGui::TableSetColumnIndex(3);
                cellText(rarityText(d.rarity).c_str(), rarityColor(d.rarity), FontKind::Body, type::Label, kRowH);
                ImGui::TableSetColumnIndex(4);
                cellText(bucketName(d.home).c_str(), pal::Muted, FontKind::Body, type::Small, kRowH);
                ImGui::TableSetColumnIndex(5);
                cellText(world::hex8(d.hash).c_str(), pal::Muted, FontKind::Mono, type::MonoSmall, kRowH);
                ImGui::TableSetColumnIndex(6);
                cellText(std::to_string(d.def).c_str(), pal::Faint, FontKind::Body, type::Small, kRowH);
                ImGui::TableSetColumnIndex(7);
                auto o = s.owned.find(d.def);
                cellText(o == s.owned.end() ? "" : std::to_string(o->second).c_str(), pal::Success, FontKind::Body, type::Label, kRowH);
            }
        }
        ImGui::PopFont();
        if (s.view.empty()) {
            ImGui::TableNextRow(0, px(110));
            ImGui::TableSetColumnIndex(1);
            gap(sp::XL);
            caption(s.ownedOnly && s.owned.empty() ? "Nothing owned yet: read your inventory on the Inventory tab." : "No Items Match", pal::Muted);
        }
        endTable();
    }
    if (split) ImGui::SameLine(0, px(14));
    else gap(sp::M);
    if (const ItemDef* d = s.selected >= 0 ? cat.item(uint32_t(s.selected)) : nullptr) itemDetail(*d, detailW, split ? h : px(520));
}

void drawPerks(Ui&) {
    CatState& s = cs();
    const Catalog& cat = catalog();
    if (searchField("##perksearch", &s.perkQuery, "Search Perk Name or Description", 340, false)) s.perkDirty = true;
    if (s.perkDirty) {
        s.perkView.clear();
        const std::string q = lowerAscii(s.perkQuery);
        for (const Perk& p : cat.perks()) {
            if (p.name.empty() && q.empty()) continue;
            if (!q.empty() && lowerAscii(p.name + " " + p.description + " " + std::to_string(p.ordinal)).find(q) == std::string::npos) continue;
            s.perkView.push_back(p.ordinal);
        }
        s.perkDirty = false;
    }
    char buf[64];
    std::snprintf(buf, sizeof buf, "%zu of %zu perks", s.perkView.size(), cat.perks().size());
    ImGui::SameLine();
    alignRight(textSize(FontKind::Body, type::Small, buf).x);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(8));
    caption(buf, pal::Faint);
    gap(sp::M);
    const float h = std::clamp(ImGui::GetWindowHeight() - px(280), px(380), px(900));
    if (beginTable("##perks", 4, h / scale(), ImGuiTableFlags_BordersOuter)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Ordinal", ImGuiTableColumnFlags_WidthFixed, px(64));
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, px(220));
        ImGui::TableSetupColumn("Runtime", ImGuiTableColumnFlags_WidthFixed, px(70));
        ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
        tableHeaders();
        ImGuiListClipper clip;
        clip.Begin(int(s.perkView.size()), px(kRowH));
        ImGui::PushFont(font(FontKind::Body), fontSize(FontKind::Body, type::Label));
        while (clip.Step())
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
                const Perk& p = cat.perks()[s.perkView[size_t(r)]];
                ImGui::TableNextRow(0, px(kRowH));
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(int(p.ordinal));
                const float cellY = ImGui::GetCursorPosY();
                if (tableRowSelectable("##row", s.perkSelected == p.ordinal, nullptr, kRowH - 2 * ImGui::GetStyle().CellPadding.y / scale()))
                    s.perkSelected = p.ordinal;
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !p.description.empty()) ImGui::SetTooltip("%s", p.description.c_str());
                ImGui::SetCursorPosY(cellY);
                cellText(std::to_string(p.ordinal).c_str(), pal::Muted, FontKind::Mono, type::MonoSmall, kRowH);
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                cellText(p.label().c_str(), p.name.empty() ? pal::Faint : pal::Text, FontKind::Body, type::Label, kRowH);
                ImGui::TableSetColumnIndex(2);
                cellText(p.enabled ? "yes" : "no", p.enabled ? pal::Success : pal::Faint, FontKind::Body, type::Small, kRowH);
                ImGui::TableSetColumnIndex(3);
                cellText(p.description.c_str(), pal::Muted, FontKind::Body, type::Small, kRowH);
            }
        ImGui::PopFont();
        endTable();
    }
}

void drawCatalog(Ui& ui) {
    CatState& s = cs();
    const Catalog& cat = catalog();
    if (!cat.error.empty()) {
        emptyState(icon::Warning, "Item Catalogue Unavailable", cat.error.c_str(), 200);
        return;
    }
    pillTabs("##catmode", {"Items", "Perks"}, &s.mode);
    gap(sp::M);
    if (s.mode == 0) drawItems(ui);
    else drawPerks(ui);
}

void paletteDefs(const std::string& query, std::vector<PaletteEntry>& out) {
    if (query.size() < 3) return;
    const Catalog& cat = catalog();
    const std::string q = lowerAscii(query);
    int n = 0;
    for (const ItemDef& d : cat.items()) {
        if (d.name.empty() || cat.hay(d.def).find(q) == std::string::npos) continue;
        if (++n > 12) break;
        PaletteEntry e;
        e.label = d.label();
        e.where = "Item catalog  \xE2\x80\xBA  " + (d.type.empty() ? std::string("item") : d.type) + "  \xC2\xB7  " + world::hex8(d.hash);
        e.icon = icon::List;
        e.badge = rarityText(d.rarity);
        const int def = d.def;
        e.run = [def] {
            CatState& s = cs();
            s.mode = 0;
            s.query.clear();
            s.rarity = s.kind = 0;
            s.ownedOnly = false;
            s.namedOnly = false;
            s.dirty = true;
            s.selected = def;
            s.scrollTo = true;
            openVaultTab("Catalog");
        };
        out.push_back(std::move(e));
    }
}

}   

}   

namespace brps3 {

BR_FEATURES(inventory_catalog_page) {
    using namespace invui;
    r.section({.id = "vault.catalog", .page = Page::Vault, .tab = "Catalog", .section = "Item catalog", .order = 10, .draw = drawCatalog});
    ui::styleSection("Item catalog", {.fullWidth = true, .icon = ui::icon::List});
    ui::addPaletteProvider(paletteDefs);
}

}   
