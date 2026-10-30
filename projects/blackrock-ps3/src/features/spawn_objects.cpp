 
 
 
 
 
 
 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "app/engine.h"
#include "app/registry.h"
#include "core/error.h"
#include "features/spawn_shared.h"
#include "features/spawn_ui.h"
#include "game/names_catalog.h"
#include "game/spawn_catalog.h"
#include "ui/widgets.h"

namespace brps3::spawnui {
namespace {

using namespace ui;

struct ObjPage {
    std::string filter;
    int type = 0;                        
    bool hideNoise = true;
    bool mineOnly = false;
    bool taggedOnly = false;
    uint32_t selected = 0;               
    bool scrollTo = false;
    Vec3 moveTo;
    uint32_t moveFor = 0;
    ImGuiTableColumnSortSpecs sort{};
    bool sorted = false;
    float detailH = 0;
};

ObjPage& op() {
    static ObjPage p;
    return p;
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string objectName(const LiveRow& r) {
    if (r.tag) return displayName(r.tag);
    char buf[48];
    std::snprintf(buf, sizeof buf, "%s #%u", spawn::categoryName(r.o.type), r.o.index);
    return buf;
}

std::string hex(uint32_t v) { return world::hex8(v); }

void postRefresh() { post(kJobObjects, [](Ctx& c) { refreshObjects(c, true); }); }

 
 
 

void kv(const char* k, const std::string& v, const ImVec4& color = pal::Text, bool monoValue = true) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float kw = px(108);
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float h = px(22);
    drawText(d, FontKind::Body, type::Small, ImVec2(p.x, textTop(FontKind::Body, type::Small, p.y + h * 0.5f)), u32(pal::Faint), k);
    ImGui::SetCursorScreenPos(ImVec2(p.x + kw, p.y + (h - ImGui::GetTextLineHeight()) * 0.5f));
    if (monoValue) {
        ImGui::PushID(k);
        copyable(v.c_str(), k, color);
        ImGui::PopID();
    } else {
        FontScope f(FontKind::Body, type::Small);
        ImGui::TextColored(color, "%s", v.c_str());
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
    ImGui::Dummy(ImVec2(0, 0));
}

void drawInspector(Ui& ui, const LiveRow* row, uint32_t me, float width, float height) {
    ObjPage& p = op();
    const bool child = height > 0;
    if (child) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, pal::Raised);
        ImGui::PushStyleColor(ImGuiCol_Border, pal::Border);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(kRowRounding));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(16), px(14)));
        ImGui::SetNextWindowSizeConstraints(ImVec2(width, height), ImVec2(width, FLT_MAX));
        ImGui::BeginChild("##inspector", ImVec2(width, 0),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AutoResizeY);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
    } else {
        beginCard("##inspectorcard", nullptr, {.raised = true});
    }
    if (!row) {
        emptyState(icon::View, "Pick an Object", "Select a Row to Inspect, Move, Despawn or Clone It",
                   child ? std::max(220.0f, height / scale() - 40) : 220);
    } else {
        const world::SObject& o = row->o;
        heading(objectName(*row).c_str());
        gap(sp::XS);
        hashLabel(o.handle, "handle", pal::Accent);
        ImGui::SameLine(0, px(10));
        pill(spawn::categoryName(o.type), pal::Info);
        if (row->mine) {
            ImGui::SameLine(0, px(6));
            pill("Spawned by Me", pal::Success, icon::Check);
        }
        if (row->dist >= 0) {
            ImGui::SameLine(0, px(6));
            char d[32];
            std::snprintf(d, sizeof d, "%.1f m", double(row->dist));
            pill(d, pal::Muted);
        }
        gap(sp::M);
        kv("Definition Tag", row->tag ? spawn::tagText(row->tag) : std::string("not resolved"), row->tag ? pal::Text : pal::Faint, row->tag != 0);
        kv("Entry", hex(o.addr) + "  (index " + std::to_string(o.index) + ")");
        kv("Position +B0", vec3Text(o.position, 2));
        kv("Position +90", vec3Text(o.pos90, 2), world::distance(o.pos90, o.position) < 0.05f ? pal::Muted : pal::Warning);
        kv("Vector +40", vec3Text(o.vector40, 2), pal::Muted);
        char q[96];
        std::snprintf(q, sizeof q, "%.3f, %.3f, %.3f, %.3f", double(o.rot2.x), double(o.rot2.y), double(o.rot2.z), double(o.rot2.w));
        kv("Rotation +A0", q, pal::Muted);
        std::snprintf(q, sizeof q, "%.3f  (+BC %.3f)", double(o.scale), double(o.scale2));
        kv("Scale +9C", q, pal::Muted);
        kv("Flags +4", hex(o.flags), pal::Muted);
        kv("Parent +2C", world::validHandle(o.parent) ? hex(o.parent) : std::string("none"), pal::Muted, world::validHandle(o.parent));
        kv("Components +3C", world::validHandle(o.componentHead) ? hex(o.componentHead) : std::string("none"), pal::Muted,
           world::validHandle(o.componentHead));
        kv("Attachment +64", o.hasAttachment() ? hex(o.attachment) : std::string("none"), pal::Muted, o.hasAttachment());
        if (world::validHandle(o.parent)) {
            if (button("Select Parent", {.tone = Tone::Ghost, .icon = icon::ArrowUp, .small = true, .tooltip = "Inspect the owner of this object"})) {
                p.selected = o.parent;
                p.scrollTo = true;
            }
            gap(sp::S);
        }
        divider(sp::M);
         
        if (p.moveFor != o.handle) {
            p.moveFor = o.handle;
            p.moveTo = o.position;
        }
        fieldLabel("Move to  (X / Y / Z, Z Up)");
        gap(sp::XS + 2);
        {
            const float w = (availWidth() - px(16)) / 3.0f / scale();
            float* f[] = {&p.moveTo.x, &p.moveTo.y, &p.moveTo.z};
            for (int i = 0; i < 3; ++i) {
                if (i) ImGui::SameLine(0, px(8));
                ImGui::PushID(i);
                inputFloat("##mv", f[i], "%.2f", w);
                ImGui::PopID();
            }
        }
        gap(sp::S);
        const uint32_t h = o.handle;
        {
            auto offline = requireConnection(ui);
            const bool busy = ui.busy(kJobMove);
            if (button("Move", {.tone = Tone::Primary, .icon = icon::Send, .small = true,
                                .tooltip = "Write the position copies (+0x90 / +0xB0, and +0x40 when it held the same position) and re-read them",
                                .busy = busy})) {
                const Vec3 to = p.moveTo;
                post(kJobMove, [h, to](Ctx& c) {
                    const auto r = moveObject(c, h, to);
                    c.toast(r.held ? "Moved to " + vec3Text(to) : "Written, but the engine moved it back (physics or AI owns it)");
                });
            }
            ImGui::SameLine(0, px(6));
            if (button("To Me", {.tone = Tone::Ghost, .icon = icon::Home, .small = true, .tooltip = "Move it to your position", .busy = busy}))
                post(kJobMove, [h](Ctx& c) {
                    const Vec3 me = readPlayerPosition(c, placement().source);
                    moveObject(c, h, me + Vec3{0, 0, 0.2f});
                    c.toast("Moved to you");
                });
            ImGui::SameLine(0, px(6));
            if (button("To Crosshair", {.tone = Tone::Ghost, .icon = icon::Follow, .small = true,
                                        .tooltip = "Move it where you aim, at the placement distance", .busy = busy}))
                post(kJobMove, [h](Ctx& c) {
                    const spawn::Placement pl = placement();
                    const auto cam = readCamera(c, pl.screenX, pl.screenY);
                    if (!cam) throw Error("The rendered camera is not usable right now");
                    moveObject(c, h, cam->position + cam->forward * pl.distance);
                    c.toast("Moved to the crosshair");
                });
            ImGui::SameLine(0, px(6));
            if (button("Up 1 m", {.tone = Tone::Ghost, .icon = icon::ArrowUp, .small = true, .tooltip = "Nudge it up 1 m", .busy = busy})) {
                const Vec3 to = o.position + Vec3{0, 0, 1};
                post(kJobMove, [h, to](Ctx& c) { moveObject(c, h, to); });
            }
        }
        divider(sp::M);
         
        {
            auto offline = requireConnection(ui);
            const uint32_t tag = row->tag;
            const Vec3 at = o.position;
            {
                DisabledScope noTag(!tag, "Its definition tag is not resolved (Refresh resolves tags)");
                if (button("Clone Here", {.icon = icon::Copy, .small = true, .tooltip = "Spawn another one of these at its position", .busy = spawnBusy()})) {
                    spawn::Placement pl = placement();
                    pl.mode = spawn::Mode::Coordinates;
                    pl.coords = at;
                    pl.count = 1;
                    const std::string label = displayName(tag);
                    post(spawnJobLabel(tag, 1), [tag, pl, label](Ctx& c) { runSpawn(c, tag, pl, label); });
                }
                ImGui::SameLine(0, px(6));
                if (button("Clone at Crosshair", {.icon = icon::Follow, .small = true,
                                                  .tooltip = "Spawn another one of these where you aim (placement distance and count)",
                                                  .busy = spawnBusy()}))
                    startSpawn(tag, spawn::Mode::Crosshair, displayName(tag));
            }
            gap(sp::S);
            const uint32_t sel = selectedTag();
            {
                DisabledScope none(!sel, "Select an entity on the Spawn tab first");
                const std::string tip = sel ? "Spawn " + displayName(sel) + " at this object's position" : std::string("Spawn the catalogue selection here");
                if (button("Spawn Selection Here", {.icon = icon::Play, .small = true, .tooltip = tip.c_str(), .busy = spawnBusy()})) {
                    spawn::Placement pl = placement();
                    pl.mode = spawn::Mode::Coordinates;
                    pl.coords = at;
                    const std::string label = displayName(sel);
                    post(spawnJobLabel(sel, pl.count), [sel, pl, label](Ctx& c) { runSpawn(c, sel, pl, label); });
                }
            }
        }
        gap(sp::S);
        if (button("Use as Spawn Coordinates", {.tone = Tone::Ghost, .icon = icon::Pin, .small = true,
                                                .tooltip = "Placement mode Coordinates, set to this object's position"})) {
            spawn::Placement pl = placement();
            pl.mode = spawn::Mode::Coordinates;
            pl.coords = o.position;
            setPlacement(pl);
            notify("Spawn coordinates set to " + vec3Text(o.position), Level::Good);
        }
        ImGui::SameLine(0, px(6));
        if (row->tag && button("Select in Catalogue", {.tone = Tone::Ghost, .icon = icon::List, .small = true})) {
            select(row->tag);
            gotoTab(kTabSpawn);
        }
        gap(sp::S);
        {
            auto offline = requireConnection(ui);
            const bool isPlayer = me && me == o.handle;
            DisabledScope you(isPlayer, "That is you");
            if (button("Despawn", {.tone = Tone::Danger, .icon = icon::Delete, .small = true, .tooltip = "Remove it from the world (asks first)",
                                   .busy = ui.busy(kJobDespawn)})) {
                const std::string name = objectName(*row);
                confirm("Despawn " + name + "?",
                        "Handle " + hex(h) + ". The call is guarded: it is refused if the object died or its slot was reused meanwhile.", "Despawn",
                        [h] { post(kJobDespawn, [h](Ctx& c) { runDespawn(c, {h}); }); });
            }
        }
    }
    if (child) {
        gap(sp::XS);
        p.detailH = ImGui::GetCursorPosY() + px(14);
        ImGui::EndChild();
    } else {
        endCard();
    }
}

 
 
 

void drawObjects(Ui& ui) {
    ObjPage& p = op();
    seedDemo();
    static bool demoPick = false;
    if (!demoPick && demoMode()) {
        demoPick = true;
        p.selected = 0x00440014;
    }
    std::vector<LiveRow> rows;
    uint32_t me = 0;
    world::PoolHeader pool;
    double at = 0;
    std::string err;
    {
        State& s = st();
        std::lock_guard<std::mutex> lock(s.m);
        rows = s.objects;
        me = s.playerHandle;
        pool = s.pool;
        at = s.objectsAt;
        err = s.objectsError;
    }
     
    {
        auto offline = requireConnection(ui);
        if (button("Refresh", {.tone = Tone::Primary, .icon = icon::Refresh, .tooltip = "Read the object pool and resolve definition tags (F5 while hovered)",
                               .busy = ui.busy(kJobObjects)}))
            postRefresh();
    }
    ImGui::SameLine(0, px(10));
    const float searchW = std::min(px(260), availWidth() * 0.26f);
    searchField("##ofilter", &p.filter, "Search Name, Tag, Handle", searchW / scale());
    ImGui::SameLine(0, px(10));
    {
        static std::vector<std::string> labels;
        static std::vector<const char*> ptrs;
        if (labels.empty()) {
            labels.push_back("All Types");
            for (int t = 0; t < world::TYPE_COUNT; ++t) labels.push_back(spawn::categoryName(uint8_t(t)));
            for (const auto& l : labels) ptrs.push_back(l.c_str());
        }
        combo("##otype", &p.type, ptrs.data(), int(ptrs.size()), 170);
    }
    ImGui::SameLine(0, px(12));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(8));
    checkbox("Hide Noise", &p.hideNoise);
    tooltip("Hides emitters, projectiles, static meshes, speedtrees, garbage props, systems and inherited: most of the pool, rarely the target");
    ImGui::SameLine(0, px(12));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(8));
    checkbox("Mine", &p.mineOnly);
    tooltip("Only what you spawned this session");
    ImGui::SameLine(0, px(12));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(8));
    checkbox("Named", &p.taggedOnly);
    tooltip("Only objects whose definition tag is resolved");
    gap(sp::S);
     
    const auto words = spawn::queryWords(p.filter);
    std::vector<const LiveRow*> view;
    for (const LiveRow& r : rows) {
        if (p.type > 0 && r.o.type != uint8_t(p.type - 1)) continue;
        if (p.hideNoise && p.type == 0 && spawn::noiseType(r.o.type)) continue;
        if (p.mineOnly && !r.mine) continue;
        if (p.taggedOnly && !r.tag) continue;
        if (!words.empty()) {
            const std::string hay = lower(objectName(r) + " " + hex(r.o.handle) + " " + std::to_string(r.o.index) + " " + spawn::categoryName(r.o.type) +
                                          (r.tag ? " " + spawn::tagText(r.tag) : std::string()));
            if (!spawn::matches(hay, words)) continue;
        }
        view.push_back(&r);
    }
    {
        const ImGuiID col = p.sorted ? p.sort.ColumnUserID : 0;
        const bool asc = !p.sorted || p.sort.SortDirection != ImGuiSortDirection_Descending;
        std::stable_sort(view.begin(), view.end(), [&](const LiveRow* a, const LiveRow* b) {
            int c = 0;
            switch (col) {
            case 0: {
                const float da = a->dist < 0 ? 1e30f : a->dist, db = b->dist < 0 ? 1e30f : b->dist;
                c = da < db ? -1 : da > db ? 1 : 0;
                break;
            }
            case 1: c = lower(objectName(*a)).compare(lower(objectName(*b))); break;
            case 2: c = int(a->o.type) - int(b->o.type); break;
            case 3: c = a->o.handle < b->o.handle ? -1 : a->o.handle > b->o.handle ? 1 : 0; break;
            default: c = int(a->o.index) - int(b->o.index); break;
            }
            return asc ? c < 0 : c > 0;
        });
    }
    {
        char buf[160];
        if (at > 0)
            std::snprintf(buf, sizeof buf, "%zu shown of %zu live  \xC2\xB7  pool %u / %u  \xC2\xB7  read %s", view.size(), rows.size(), pool.count, pool.capacity,
                          ago(at).c_str());
        else std::snprintf(buf, sizeof buf, "Not read yet");
        caption(buf, pal::Faint);
        if (!err.empty()) {
            ImGui::SameLine(0, px(10));
            caption(err.c_str(), pal::Warning);
        }
    }
    gap(sp::S);

    const float avail = availWidth();
    const bool split = avail >= px(760);
    const float detailW = split ? std::clamp(avail * 0.36f, px(320), px(420)) : avail;
    const float tableW = split ? avail - detailW - px(16) : avail;
    const float viewH = ImGui::GetWindowHeight();
    const float baseH = std::clamp(viewH - px(300), px(360), px(760));
    const float tableH = split ? std::max(baseH, std::min(p.detailH, px(900))) : baseH;
    const LiveRow* selected = nullptr;
    for (const LiveRow& r : rows)
        if (r.o.handle == p.selected) selected = &r;
     
    const ImVec2 tablePos = ImGui::GetCursorScreenPos();
    if (ImGui::IsMouseHoveringRect(tablePos, tablePos + ImVec2(tableW, tableH)) && ImGui::IsKeyPressed(ImGuiKey_F5, false) && ui.connected() &&
        !ui.busy(kJobObjects))
        postRefresh();

    ImGui::BeginGroup();
    if (rows.empty()) {
        beginCard("##noobjects", nullptr, {.width = tableW, .raised = true});
        emptyState(icon::List, at > 0 ? "No Live Objects" : "Read the Object Pool",
                   at > 0 ? "The Pool Is Empty (Menus or Loading?)"
                          : "Refresh Reads Every Live sObject, Nearest First",
                   tableH / scale() - 2 * sp::Card - 4);
        endCard();
    } else if (beginTable("##objects", 5, tableH / scale(), ImGuiTableFlags_Sortable | ImGuiTableFlags_BordersOuter, tableW / scale())) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Dist", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultSort, px(52), 0);
        ImGui::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthStretch, 0, 1);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, px(100), 2);
        ImGui::TableSetupColumn("Handle", ImGuiTableColumnFlags_WidthFixed, px(78), 3);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, px(38), 4);
        tableHeaders();
        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsDirty) {
            p.sorted = specs->SpecsCount > 0;
            if (p.sorted) p.sort = specs->Specs[0];
            specs->SpecsDirty = false;
        }
        ImGuiListClipper clip;
        clip.Begin(int(view.size()), px(kRowH));
        int scrollTo = -1;
        if (p.scrollTo) {
            for (int i = 0; i < int(view.size()); ++i)
                if (view[size_t(i)]->o.handle == p.selected) scrollTo = i;
            if (scrollTo >= 0) clip.IncludeItemByIndex(scrollTo);
            p.scrollTo = false;
        }
        ImGui::PushFont(font(FontKind::Body), fontSize(FontKind::Body, type::Label));
        while (clip.Step()) {
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                const LiveRow& r = *view[size_t(i)];
                const bool sel = r.o.handle == p.selected;
                const bool isMe = me && me == r.o.handle;
                ImGui::TableNextRow(0, px(kRowH));
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(int(r.o.handle));
                const float y = ImGui::GetCursorPosY();
                if (tableRowSelectable("##row", sel, nullptr, kRowH - 2 * ImGui::GetStyle().CellPadding.y / scale())) p.selected = r.o.handle;
                if (i == scrollTo) ImGui::SetScrollHereY(0.4f);
                ImGui::SetCursorPosY(y);
                char d[24];
                if (r.dist >= 0) std::snprintf(d, sizeof d, "%.0f", double(r.dist));
                else std::snprintf(d, sizeof d, "-");
                cellText(d, r.dist >= 0 && r.dist < 15 ? pal::Success : pal::Muted, FontKind::Body, type::Label, kRowH);
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                const std::string name = isMe ? "You" : objectName(r);
                cellText(name.c_str(), sel ? pal::Accent : r.mine ? pal::Success : r.tag ? pal::Text : pal::Soft, FontKind::Body, type::Label, kRowH);
                ImGui::TableSetColumnIndex(2);
                cellText(spawn::categoryName(r.o.type), pal::Muted, FontKind::Body, type::Label, kRowH);
                ImGui::TableSetColumnIndex(3);
                cellText(hex(r.o.handle).c_str(), sel ? pal::Accent : pal::Faint, FontKind::Mono, type::MonoSmall, kRowH);
                ImGui::TableSetColumnIndex(4);
                cellText(std::to_string(r.o.index).c_str(), pal::Faint, FontKind::Body, type::Small, kRowH);
            }
        }
        ImGui::PopFont();
        if (view.empty()) {
            ImGui::TableNextRow(0, px(100));
            ImGui::TableSetColumnIndex(1);
            gap(sp::L);
            caption("Nothing matches: clear the search, pick All Types or untick Hide Noise.", pal::Muted);
        }
        endTable();
    }
    ImGui::EndGroup();
    if (split) {
        ImGui::SameLine(0, px(16));
        drawInspector(ui, selected, me, detailW, tableH);
    } else {
        gap(sp::M);
        drawInspector(ui, selected, me, detailW, 0);
    }
}

}   
}   

namespace brps3 {

BR_FEATURES(spawn_objects) {
    using namespace spawnui;
    const Page P = Page::Spawner;
    r.toggle({.id = kToggleObjectsAuto, .page = P, .tab = kTabObjects, .section = kSecObjects, .label = "Auto-Refresh",
              .help = "Re-read the object pool every 2 seconds (and resolve new tags). Stops on disconnect and Panic.",
              .set = [](Ctx& c, bool on) {
                  if (on) c.startLoop(kLoopObjects, 2000, [](Ctx& lc) { refreshObjects(lc, true); });
                  else c.stopLoop(kLoopObjects);
              }});
    r.section({.id = "spawn.objects", .page = P, .tab = kTabObjects, .section = kSecObjects, .order = 10, .draw = drawObjects});
    r.action({.id = kActObjectsRefresh, .page = P, .tab = kTabObjects, .section = kSecObjects, .label = "Refresh Live Objects",
              .help = "Read every live sObject (type, handle, position) and resolve their definition tags.",
              .run = [](Ctx& c) {
                  refreshObjects(c, true);
                  State& s = st();
                  std::lock_guard<std::mutex> lock(s.m);
                  c.toast(std::to_string(s.objects.size()) + " live objects");
              }});
    ui::styleSection(kSecObjects, {.fullWidth = true, .icon = ui::icon::List, .ownsActions = true});
}

}   
