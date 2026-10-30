 
 
 
 
 
 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <thread>

#include "app/engine.h"
#include "app/registry.h"
#include "app/settings.h"
#include "core/error.h"
#include "core/runtime.h"
#include "features/movement_bridge.h"
#include "features/movement_shared.h"
#include "features/movement_widgets.h"
#include "game/movement_addr.h"
#include "game/movement_teleport.h"
#include "game/world_addr.h"
#include "game/world_player.h"
#include "ui/widgets.h"

namespace brps3::mvui {

using namespace ui;
using movement::Slot;
using world::Vec3;

namespace {

constexpr const char* kPosLoop = "movement.position";
constexpr const char* kPanelId = "movement.tp.panel";
constexpr float kSlotRowH = 46;

enum class Mode { To, By, Edge, Forward };

struct TpState {
    std::mutex m;                        
    bool loaded = false;
    std::vector<Slot> slots;
    std::optional<Vec3> quick;
    movement::History history;
     
    std::string filter;
    float coords[3] = {0, 0, 0};
    float offset[3] = {0, 0, 5};
    std::string newName;
    int menuFor = -1;
    int renameFor = -1;
    std::string renameText, renameNote;
    bool openRename = false;
    bool live = false;
};
TpState& T() {
    static TpState t;
    return t;
}

std::string encQuick(const std::optional<Vec3>& q) {
    if (!q) return {};
    movement::History h;
    h.points.push_back(*q);
    return h.encode();
}

void ensureLoaded() {
    TpState& t = T();
    std::lock_guard<std::mutex> lock(t.m);
    if (t.loaded) return;
    t.loaded = true;
    Settings& s = Engine::get().settings();
    if (demo()) {
        const int64_t base = int64_t(std::time(nullptr));
        t.slots = {{"Tower vault", {-412.4f, 1186.0f, 54.7f}, "Tower, behind the vault", true, base - 300, base - 9000},
                   {"Crota bridge", {2210.6f, -128.9f, -611.2f}, "Crota's End, bridge", true, base - 6000, base - 8000},
                   {"Cosmodrome wall", {-1290.0f, 377.5f, 112.0f}, "Steppes, top of the wall", false, base - 86400, base - 90000},
                   {"Moon Hellmouth", {640.2f, 2911.3f, -88.0f}, "", false, 0, base - 70000},
                   {"VoG start", {12.0f, -30.5f, 205.0f}, "Vault of Glass entrance", false, base - 3600 * 30, base - 60000}};
        t.quick = Vec3{-401.0f, 1180.5f, 54.7f};
        for (int i = 0; i < 3; ++i) t.history.push({-412.0f + 50 * i, 1186.0f, 54.7f});
        return;
    }
    t.slots = movement::decodeSlots(s.str(key::Slots));
    t.history = movement::History::decode(s.str(key::History));
    const auto q = movement::History::decode(s.str(key::Quick));
    if (!q.points.empty()) t.quick = q.points.front();
}

void saveSlotsLocked(TpState& t) {
    if (!demo()) Engine::get().settings().set(std::string(key::Slots), movement::encodeSlots(t.slots));
}
void saveHistoryLocked(TpState& t) {
    if (!demo()) Engine::get().settings().set(std::string(key::History), t.history.encode());
}
void saveQuickLocked(TpState& t) {
    if (!demo()) Engine::get().settings().set(std::string(key::Quick), encQuick(t.quick));
}

int route() { return std::clamp(Engine::get().settings().integer(key::Route, 0), 0, 1); }
float blinkDistance() { return std::clamp(Engine::get().settings().number(key::Blink, 10.0f), 1.0f, 200.0f); }

void publishPosition(const world::LocalPlayer& p, const Vec3& pos, const std::string& note) {
    const double at = now();
    publish([&](Snapshot& s) {
        s.havePos = pos.finite();
        s.pos = pos;
        s.forward = p.forward;
        s.handle = p.handle;
        s.posNote = note;
        s.posAt = at;
    });
}

 

void refreshPosition(Ctx& c) {
    c.require(world::kFingerprints, "World readers");
    world::RuntimeReader mem(c.rt());
    const world::PlayerLookup look = world::findLocalPlayer(mem);
    if (!look.player) {
        publish([&](Snapshot& s) {
            s.havePos = false;
            s.posNote = look.message;
            s.posAt = now();
        });
        return;
    }
    publishPosition(*look.player, look.player->position, std::string());
}

 
void teleport(Ctx& c, Mode mode, Vec3 arg, bool remember, const std::string& what) {
    c.require(world::kFingerprints, "World readers");
    world::RuntimeReader mem(c.rt());
    movement::TeleportResult r;
    if (route() == 0) {
         
         
        const auto w = writer(c, false);
        switch (mode) {
        case Mode::To: r = movement::teleportTo(mem, w, arg); break;
        case Mode::By: r = movement::teleportBy(mem, w, arg); break;
        case Mode::Edge: r = movement::teleportEdge(mem, w, movement::d1::TP_EDGE); break;
        case Mode::Forward: r = movement::teleportForward(mem, w, arg.x); break;
        }
    } else {
         
        const world::LocalPlayer p = world::requireLocalPlayer(mem);
        r.before = world::readEnginePosition(mem, p);
        switch (mode) {
        case Mode::To: r.target = arg; break;
        case Mode::By: r.target = r.before + arg; break;
        case Mode::Edge: r.target = r.before + Vec3{0, 0, movement::d1::TP_EDGE}; break;
        case Mode::Forward: {
            const auto dir = movement::horizontal(p.forward);
            if (!dir) throw Error("The camera is looking straight up or down: turn and try again.");
            r.target = r.before + *dir * arg.x;
            break;
        }
        }
        std::string why;
        if (!movement::validTarget(r.target, &why)) throw Error(why);
        session(c).tick.warp(c.rt(), mem, r.target, 3);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        const Vec3 after = world::readEnginePosition(mem, p);
        r.verified = world::distance(after, r.target) < 3.0f;
        r.message = r.verified ? "Moved to " + movement::formatVec(r.target) + " (physics body)."
                               : "The body was moved, but the engine position is " + movement::formatVec(after) + ": check in game.";
    }
    if (remember && r.before.finite()) {
        TpState& t = T();
        std::lock_guard<std::mutex> lock(t.m);
        t.history.push(r.before);
        saveHistoryLocked(t);
    }
    publish([&](Snapshot& s) {
        s.lastTeleport = what.empty() ? r.message : what + ": " + r.message;
        s.lastTeleportOk = r.verified;
        s.havePos = true;
        s.pos = r.target;
        s.posAt = now();
    });
    c.log("[teleport] " + (what.empty() ? std::string() : what + ": ") + r.message);
    if (!r.verified) c.toast(r.message);
}

void saveQuick(Ctx& c) {
    c.require(world::kFingerprints, "World readers");
    world::RuntimeReader mem(c.rt());
    const world::LocalPlayer p = world::requireLocalPlayer(mem);
    const Vec3 pos = world::readPosition(mem, p);
    if (!pos.finite() || !movement::validTarget(pos))
        throw Error("The position at element +0x2810 is not plausible: run TESTING > Port checks > Movement > Teleport target.");
    {
        TpState& t = T();
        ensureLoaded();
        std::lock_guard<std::mutex> lock(t.m);
        t.quick = pos;
        saveQuickLocked(t);
    }
    publishPosition(p, pos, std::string());
    c.toast("Saved " + movement::formatVec(pos));
}

void gotoQuick(Ctx& c) {
    ensureLoaded();
    std::optional<Vec3> q;
    {
        TpState& t = T();
        std::lock_guard<std::mutex> lock(t.m);
        q = t.quick;
    }
    if (!q) throw Error("No saved position yet: Save position first (or bind it to the controller).");
    teleport(c, Mode::To, *q, true, "Quick slot");
}

void goBack(Ctx& c) {
    ensureLoaded();
    std::optional<Vec3> back;
    {
        TpState& t = T();
        std::lock_guard<std::mutex> lock(t.m);
        back = t.history.pop();
        saveHistoryLocked(t);
    }
    if (!back) throw Error("Nothing to return to: the history fills as you teleport.");
    try {
        teleport(c, Mode::To, *back, false, "Return");
    } catch (...) {
        TpState& t = T();                              
        std::lock_guard<std::mutex> lock(t.m);
        t.history.push(*back);
        saveHistoryLocked(t);
        throw;
    }
}

void post(const std::string& label, std::function<void(Ctx&)> job) {
    if (!Engine::get().connected()) {
        notify(std::string(kConnectFirst) + " to " + label, Level::Warn);
        return;
    }
    noteQueued(label);
    Engine::get().post(label, std::move(job), [label](bool ok, const std::string& err) {
        if (ok) flashSuccess(label);
        else publish([&](Snapshot& s) {
            s.lastTeleport = err;
            s.lastTeleportOk = false;
        });
    });
}

void gotoSlot(const Slot& slot) {
    const Vec3 pos = slot.pos;
    const std::string name = slot.name;
    {
        TpState& t = T();
        std::lock_guard<std::mutex> lock(t.m);
        for (Slot& s : t.slots)
            if (s.name == name) s.used = int64_t(std::time(nullptr));
        saveSlotsLocked(t);
    }
    post("Teleport to " + name, [pos, name](Ctx& c) { teleport(c, Mode::To, pos, true, name); });
}

void setLive(bool on) {
    TpState& t = T();
    if (t.live == on) return;
    t.live = on;
    if (!Engine::get().connected()) return;
    Engine::get().post(on ? "Teleport: live position on" : "Teleport: live position off", [on](Ctx& c) {
        if (!on) {
            c.stopLoop(kPosLoop);
            return;
        }
        c.startLoop(kPosLoop, 500, [](Ctx& lc) {
            lc.require(world::kFingerprints, "World readers");
            world::RuntimeReader mem(lc.rt());
            const auto look = world::findLocalPlayer(mem);
            if (!look.player) {
                publish([&](Snapshot& s) {
                    s.havePos = false;
                    s.posNote = look.message;
                    s.posAt = now();
                });
                return;
            }
            publishPosition(*look.player, look.player->position, std::string());
        });
    });
}

 

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

 
void axisInputs(const char* id, float v[3], float width) {
    static const char* const names[3] = {"X", "Y", "Z"};
    const float labelW = px(14), gapW = px(10);
    const float fieldW = std::max(px(60), (width - 3 * (labelW + px(4)) - 2 * gapW) / 3.0f);
    for (int i = 0; i < 3; ++i) {
        if (i) ImGui::SameLine(0, gapW);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(labelW, px(kControlH)));
        const ImVec2 ts = textSize(FontKind::Semibold, type::Small, names[i]);
        drawText(ImGui::GetWindowDrawList(), FontKind::Semibold, type::Small,
                 ImVec2(p.x + (labelW - ts.x) * 0.5f, textTop(FontKind::Semibold, type::Small, p.y + px(kControlH) * 0.5f)), u32(pal::Faint), names[i]);
        ImGui::SameLine(0, px(4));
        char key[32];
        std::snprintf(key, sizeof key, "##%s%d", id, i);
        inputFloat(key, &v[i], "%.2f", fieldW / scale());
    }
}

void drawPosition(Ui& ui, const Snapshot& s) {
    TpState& t = T();
    fieldLabel("Your Position");
    gap(sp::XS + 2);
    const bool have = s.havePos && ui.connected();
    {
        FontScope f(FontKind::Mono, 18);
        const std::string text = have ? movement::formatVec(s.pos) : std::string("--, --, --");
        if (have) copyable(text.c_str(), "position", pal::Text);
        else mono(text.c_str(), pal::Faint, 18);
    }
    if (have) {
        char heading[48] = "";
        if (const auto dir = movement::horizontal(s.forward)) {
            float deg = std::atan2(dir->y, dir->x) * 57.29578f;
            if (deg < 0) deg += 360.0f;
            std::snprintf(heading, sizeof heading, "  \xC2\xB7  facing %.0f\xC2\xB0", double(deg));
        }
        char b[128];
        std::snprintf(b, sizeof b, "element +0x2810%s  \xC2\xB7  %s%s", heading, ago(s.posAt).c_str(), t.live ? "  \xC2\xB7  live" : "");
        caption(b, pal::Faint);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Facing: the camera forward (player row +0x110) as a compass angle from +X towards +Y.");
    } else {
        caption(!ui.connected() ? "Connect to read your position." : s.posNote.empty() ? "Press Refresh (or turn on Live)." : s.posNote.c_str(), pal::Faint);
    }
    gap(sp::S);
    {
        auto offline = requireConnection(ui);
        if (button("Refresh", {.tone = Tone::Ghost, .icon = icon::Refresh, .small = true, .busy = ui.busy("Read your position")}))
            post("Read your position", refreshPosition);
        ImGui::SameLine(0, px(6));
        if (button("Live", {.tone = Tone::Ghost, .icon = t.live ? icon::Pause : icon::Play, .small = true,
                                              .tooltip = "Read the position twice a second", .selected = t.live}))
            setLive(!t.live);
        ImGui::SameLine(0, px(6));
        DisabledScope noPos(!have, "Read your position first");
        if (button("Copy to Coordinates", {.tone = Tone::Ghost, .icon = icon::ArrowDown, .small = true})) {
            t.coords[0] = s.pos.x;
            t.coords[1] = s.pos.y;
            t.coords[2] = s.pos.z;
        }
    }
}

void drawQuick(Ui& ui) {
    TpState& t = T();
    std::optional<Vec3> quick;
    size_t hist;
    {
        std::lock_guard<std::mutex> lock(t.m);
        quick = t.quick;
        hist = t.history.points.size();
    }
    fieldLabel("Quick");
    gap(sp::XS + 2);
    auto offline = requireConnection(ui);
    if (button("Save", {.icon = icon::Pin, .tooltip = "Remember where you stand (the quick slot; bindable: \"Save Position\")",
                        .busy = ui.busy("Save Position"), .successKey = "Save Position"}))
        post("Save Position", saveQuick);
    ImGui::SameLine(0, px(6));
    {
        DisabledScope none(!quick, "Save a position first");
        if (button("Go Back to It", {.icon = icon::Send, .tooltip = "Teleport to the quick slot", .busy = ui.busy("Teleport to Saved Position"),
                                     .successKey = "Teleport to Saved Position"}))
            post("Teleport to Saved Position", gotoQuick);
    }
    ImGui::SameLine(0, px(6));
    {
        DisabledScope none(hist == 0, "Nothing to return to yet");
        if (button("Return", {.icon = icon::Undo, .tooltip = "Back to where you were before the last teleport", .busy = ui.busy("Return to Previous Position"),
                              .successKey = "Return to Previous Position"}))
            post("Return to Previous Position", goBack);
    }
    gap(sp::XS);
    const float bl = blinkDistance();
    if (button("Blink", {.tone = Tone::Ghost, .icon = icon::ChevronRight, .tooltip = "Forward along the camera by this many metres, same height "
                                                                                    "(bindable: \"Blink Forward\")",
                         .busy = ui.busy("Blink Forward"), .successKey = "Blink Forward"}))
        post("Blink Forward", [bl](Ctx& c) { teleport(c, Mode::Forward, {bl, 0, 0}, true, "Blink"); });
    ImGui::SameLine(0, px(4));
    {
        int d = int(bl);
        if (inputInt("##blinkd", &d, 1, 200, 124, 5)) Engine::get().settings().set(std::string(key::Blink), float(d));
        tooltip("Blink distance in metres");
    }
    ImGui::SameLine(0, px(6));
    if (button("Edge: Up 500 m", {.tone = Tone::Danger, .icon = icon::Warning,
                                  .tooltip = "Straight up 500 m: lethal on landing (the Xbox \"TP Edge (suicide)\")"}))
        confirm("Teleport 500 m Up?", "You will fall and die when you land (the Xbox TP Edge). Use Return to come back.", "Go Up",
                [] { post("Edge teleport", [](Ctx& c) { teleport(c, Mode::Edge, {}, true, "Edge"); }); });
    gap(sp::XS);
    char b[160];
    if (quick) std::snprintf(b, sizeof b, "Quick slot %s  \xC2\xB7  %zu to return to", movement::formatVec(*quick).c_str(), hist);
    else std::snprintf(b, sizeof b, "Quick slot empty  \xC2\xB7  %zu to return to", hist);
    caption(b, pal::Faint);
}

void drawCoordinates(Ui& ui, const Snapshot& s) {
    TpState& t = T();
    const float w = std::min(availWidth() - px(kControlH) - px(8), px(460));
    fieldLabel("Go to Coordinates");
    gap(sp::XS + 2);
    axisInputs("to", t.coords, w);
    ImGui::SameLine(0, px(8));
    if (iconButton("##paste", icon::Clipboard, "Paste \"x, y, z\" from the clipboard", false, kControlH)) {
        const char* clip = ImGui::GetClipboardText();
        if (auto v = movement::parseVec(clip ? clip : "")) {
            t.coords[0] = v->x;
            t.coords[1] = v->y;
            t.coords[2] = v->z;
        } else {
            notify("The clipboard does not hold three numbers", Level::Warn);
        }
    }
    gap(sp::S);
    {
        auto offline = requireConnection(ui);
        const Vec3 to{t.coords[0], t.coords[1], t.coords[2]};
        std::string why;
        const bool ok = movement::validTarget(to, &why);
        DisabledScope bad(!ok, why.c_str());
        if (button("Teleport", {.tone = Tone::Primary, .icon = icon::Send, .tooltip = "Saves where you are to the return history first",
                                .busy = ui.busy("Teleport to coordinates"), .successKey = "Teleport to coordinates"}))
            post("Teleport to coordinates", [to](Ctx& c) { teleport(c, Mode::To, to, true, std::string()); });
        if (s.havePos) {
            ImGui::SameLine(0, px(10));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(9));
            char d[64];
            std::snprintf(d, sizeof d, "%.1f m away", double(world::distance(s.pos, to)));
            caption(d, pal::Faint);
        }
    }
    gap(sp::M);
    fieldLabel("Move by (World Axes)");
    gap(sp::XS + 2);
    axisInputs("by", t.offset, w);
    gap(sp::S);
    {
        auto offline = requireConnection(ui);
        const Vec3 by{t.offset[0], t.offset[1], t.offset[2]};
        if (button("Move", {.icon = icon::Send, .busy = ui.busy("Move by offset"), .successKey = "Move by offset"}))
            post("Move by offset", [by](Ctx& c) { teleport(c, Mode::By, by, true, std::string()); });
        for (const float z : {5.0f, 25.0f, -5.0f}) {
            ImGui::SameLine(0, px(6));
            char l[24];
            std::snprintf(l, sizeof l, "%+.0f Up##n%.0f", double(z), double(z));
            if (button(l, {.tone = Tone::Ghost, .small = true, .tooltip = "Move straight up / down now"}))
                post("Move by offset", [z](Ctx& c) { teleport(c, Mode::By, {0, 0, z}, true, std::string()); });
        }
    }
    gap(sp::M);
    fieldLabel("Route");
    gap(sp::XS + 2);
    int r = route();
    if (pillTabs("##route", {"Position +0x2810", "Physics Body"}, &r)) Engine::get().settings().set(std::string(key::Route), r);
    tooltip(r == 0 ? "Writes your descriptor position and reads it back (the Xbox route). If you snap back, try Physics Body."
                   : "Moves your Havok body's centre of mass for 3 frames from the movement task, then checks the engine position.");
    if (!s.lastTeleport.empty()) {
        gap(sp::S);
        pill(s.lastTeleport.c_str(), s.lastTeleportOk ? pal::Success : pal::Warning, s.lastTeleportOk ? icon::Check : icon::Info);
    }
}

void drawRename() {
    TpState& t = T();
    if (t.openRename) {
        ImGui::OpenPopup("##movement.slotrename");
        t.openRename = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(px(380), 0), ImGuiCond_Always);
    if (!ImGui::BeginPopup("##movement.slotrename")) return;
    heading("Edit Slot");
    gap(sp::S);
    fieldLabel("Name");
    gap(sp::XS + 2);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    inputText("##rn", &t.renameText, "Name", 0);
    gap(sp::S);
    fieldLabel("Note");
    gap(sp::XS + 2);
    inputText("##rnote", &t.renameNote, "Activity / Where It Is (Optional)", 0);
    gap(sp::M);
    const std::string clean = movement::cleanName(t.renameText);
    {
        DisabledScope empty(clean.empty(), "Give it a name");
        if (button("Save", {.tone = Tone::Primary, .icon = icon::Check}) || (!clean.empty() && ImGui::IsKeyPressed(ImGuiKey_Enter, false))) {
            std::lock_guard<std::mutex> lock(t.m);
            if (t.renameFor >= 0 && size_t(t.renameFor) < t.slots.size()) {
                Slot& s = t.slots[size_t(t.renameFor)];
                std::vector<Slot> others = t.slots;
                others.erase(others.begin() + t.renameFor);
                s.name = movement::uniqueName(others, clean);
                s.note = movement::cleanName(t.renameNote);
                saveSlotsLocked(t);
            }
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine(0, px(8));
    if (button("Cancel", {.tone = Tone::Ghost})) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void drawSlots(Ui& ui, const Snapshot& s) {
    TpState& t = T();
    std::vector<Slot> slots;
    {
        std::lock_guard<std::mutex> lock(t.m);
        slots = t.slots;
    }
    char head[64];
    std::snprintf(head, sizeof head, "Saved Slots  (%zu)", slots.size());
    fieldLabel(head);
    gap(sp::XS + 2);
     
    {
        const float bw = buttonWidth("Save Here", icon::Add);
        inputText("##newslot", &t.newName, "Name for Where You Stand", (availWidth() - bw - px(8)) / scale());
        ImGui::SameLine(0, px(8));
        auto offline = requireConnection(ui);
        if (button("Save Here", {.tone = Tone::Primary, .icon = icon::Add, .tooltip = "Save your current position as a named slot",
                                 .busy = ui.busy("Save a slot")})) {
            const std::string name = t.newName;
            t.newName.clear();
            post("Save a slot", [name](Ctx& c) {
                c.require(world::kFingerprints, "World readers");
                world::RuntimeReader mem(c.rt());
                const world::LocalPlayer p = world::requireLocalPlayer(mem);
                const Vec3 pos = world::readPosition(mem, p);
                if (!pos.finite() || !movement::validTarget(pos)) throw Error("Your position (+0x2810) is not plausible: nothing saved.");
                ensureLoaded();
                std::string saved;
                {
                    TpState& tt = T();
                    std::lock_guard<std::mutex> lock(tt.m);
                    Slot slot;
                    slot.name = movement::uniqueName(tt.slots, name.empty() ? std::string("Spot") : name);
                    slot.pos = pos;
                    slot.created = int64_t(std::time(nullptr));
                    tt.slots.push_back(slot);
                    saveSlotsLocked(tt);
                    saved = slot.name;
                }
                publishPosition(p, pos, std::string());
                c.toast("Saved \"" + saved + "\" at " + movement::formatVec(pos));
            });
        }
    }
    gap(sp::S);
    searchField("##slotfilter", &t.filter, "Search Slots by Name or Note", 0, false);
    gap(sp::S);
    if (slots.empty()) {
        emptyState(icon::Pin, "No Saved Slots", "Name Where You Stand and Press Save Here", 170);
        return;
    }
    const std::string f = lower(t.filter);
    const auto order = movement::slotOrder(slots);
    int shown = 0;
    int toggleFav = -1, remove = -1;
    for (size_t k : order) {
        const Slot& sl = slots[k];
        if (!f.empty() && lower(sl.name + " " + sl.note).find(f) == std::string::npos) continue;
        ++shown;
        ImGui::PushID(int(k));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = availWidth(), h = px(kSlotRowH);
        ImDrawList* d = ImGui::GetWindowDrawList();
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##row", ImVec2(w, h));
         
         
         
        const bool hov = ImGui::IsItemHovered();
        const bool dbl = hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        if (hov) d->AddRectFilled(p, p + ImVec2(w, h), u32(pal::Raised), px(kRowRounding));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("Double-click to teleport");
         
        ImGui::SetCursorScreenPos(p + ImVec2(px(4), (h - px(26)) * 0.5f));
        if (iconButton("##fav", sl.favourite ? icon::StarFill : icon::Star, sl.favourite ? "Unpin from the top" : "Pin to the top", false, 26,
                       sl.favourite ? pal::Accent : pal::Decor))
            toggleFav = int(k);
         
        const float tx = p.x + px(38);
        const float buttonsW = px(kSmallControlH) * 2 + px(56) + px(18);
        const float tw = std::max(px(40), w - (tx - p.x) - buttonsW);
        const std::string name = ellipsize(sl.name.c_str(), tw, FontKind::Semibold, type::Label);
        drawText(d, FontKind::Semibold, type::Label, ImVec2(tx, p.y + px(6)), u32(pal::Text), name.c_str());
        const std::string coords = movement::formatVec(sl.pos, 1);
        const std::string sub = sl.note.empty() ? coords : sl.note + "   " + coords;
        const std::string subCut = ellipsize(sub.c_str(), tw, FontKind::Body, type::Tiny);
        drawText(d, FontKind::Body, type::Tiny, ImVec2(tx, p.y + px(26)), u32(pal::Faint), subCut.c_str());
         
        float bx = p.x + w - buttonsW + px(10);
        if (s.havePos) {
            char dist[24];
            const float dd = world::distance(s.pos, sl.pos);
            std::snprintf(dist, sizeof dist, dd < 1000 ? "%.0f m" : "%.1f km", double(dd < 1000 ? dd : dd / 1000));
            const ImVec2 ds = textSize(FontKind::Body, type::Tiny, dist);
            drawText(d, FontKind::Body, type::Tiny, ImVec2(bx - ds.x - px(8), p.y + (h - ds.y) * 0.5f), u32(pal::Faint), dist);
        }
        ImGui::SetCursorScreenPos(ImVec2(bx, p.y + (h - px(kSmallControlH)) * 0.5f));
        {
            auto offline = requireConnection(ui);
            if (button("Go", {.tone = Tone::Primary, .width = 50, .small = true, .tooltip = "Teleport here", .busy = ui.busy("Teleport to " + sl.name)})) gotoSlot(sl);
        }
        ImGui::SameLine(0, px(4));
        if (iconButton("##more", icon::More, "Rename, update, copy, delete", false, kSmallControlH)) ImGui::OpenPopup("##slotmenu");
        if (ImGui::BeginPopup("##slotmenu")) {
            if (ImGui::MenuItem("Rename / Note...")) {
                t.renameFor = int(k);
                t.renameText = sl.name;
                t.renameNote = sl.note;
                t.openRename = true;
            }
            if (ImGui::MenuItem("Copy Coordinates")) copyToClipboard(movement::formatVec(sl.pos, 3), "coordinates");
            if (ImGui::MenuItem("Load into Go to Coordinates")) {
                t.coords[0] = sl.pos.x;
                t.coords[1] = sl.pos.y;
                t.coords[2] = sl.pos.z;
            }
            if (ImGui::MenuItem("Move Here: Where I Stand", nullptr, false, ui.connected() && s.havePos)) {
                std::lock_guard<std::mutex> lock(t.m);
                for (Slot& x : t.slots)
                    if (x.name == sl.name) x.pos = s.pos;
                saveSlotsLocked(t);
                notify("\"" + sl.name + "\" now points at " + movement::formatVec(s.pos), Level::Good);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete...")) remove = int(k);
            ImGui::EndPopup();
        }
        if (dbl && ui.connected()) gotoSlot(sl);
        ImGui::SetCursorScreenPos(p + ImVec2(0, h + px(2)));
        ImGui::Dummy(ImVec2(w, 0));
        ImGui::PopID();
    }
    if (!shown) caption("No slots match.", pal::Muted);
    if (toggleFav >= 0) {
        std::lock_guard<std::mutex> lock(t.m);
        if (size_t(toggleFav) < t.slots.size()) t.slots[size_t(toggleFav)].favourite = !t.slots[size_t(toggleFav)].favourite;
        saveSlotsLocked(t);
    }
    if (remove >= 0) {
        const std::string name = slots[size_t(remove)].name;
        confirm("Delete \"" + name + "\"?", "The slot and its coordinates are removed. This can't be undone.", "Delete", [name] {
            TpState& tt = T();
            std::lock_guard<std::mutex> lock(tt.m);
            tt.slots.erase(std::remove_if(tt.slots.begin(), tt.slots.end(), [&](const Slot& x) { return x.name == name; }), tt.slots.end());
            saveSlotsLocked(tt);
        });
    }
}

void drawHistory(Ui& ui) {
    TpState& t = T();
    std::vector<Vec3> pts;
    {
        std::lock_guard<std::mutex> lock(t.m);
        pts = t.history.points;
    }
    if (pts.empty()) return;
    gap(sp::M);
    fieldLabel("Return History");
    gap(sp::XS + 2);
    auto offline = requireConnection(ui);
    for (size_t i = 0; i < pts.size() && i < 4; ++i) {
        ImGui::PushID(int(i));
        const Vec3 v = pts[i];
        if (button("Go##h", {.tone = Tone::Ghost, .small = true, .tooltip = "Teleport back here"}))
            post("Teleport back", [v](Ctx& c) { teleport(c, Mode::To, v, true, "History"); });
        ImGui::SameLine(0, px(8));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(5));
        mono(movement::formatVec(v, 1).c_str(), i == 0 ? pal::Text : pal::Muted, type::MonoSmall);
        ImGui::PopID();
    }
}

void drawTeleport(Ui& ui) {
    seedDemo();
    ensureLoaded();
    const Snapshot s = snapshot();
    const float avail = availWidth();
    const bool split = avail >= px(820);
    const float leftW = split ? std::clamp(avail * 0.46f, px(380), px(560)) : avail;
    ImGui::BeginGroup();
    ImGui::BeginChild("##tpleft", ImVec2(leftW, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_NavFlattened, ImGuiWindowFlags_NoBackground);
    drawPosition(ui, s);
    gap(sp::M);
    drawQuick(ui);
    divider(sp::L);
    drawCoordinates(ui, s);
    ImGui::EndChild();
    ImGui::EndGroup();
    if (split) ImGui::SameLine(0, px(28));
    else divider(sp::L);
    ImGui::BeginGroup();
    ImGui::BeginChild("##tpright", ImVec2(split ? avail - leftW - px(28) : avail, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_NavFlattened,
                      ImGuiWindowFlags_NoBackground);
    drawSlots(ui, s);
    drawHistory(ui);
    ImGui::EndChild();
    ImGui::EndGroup();
    drawRename();
}

void resetTp() {
    T().live = false;
}

void paletteEntries(const std::string& query, std::vector<PaletteEntry>& out) {
    ensureLoaded();
    std::vector<Slot> slots;
    {
        TpState& t = T();
        std::lock_guard<std::mutex> lock(t.m);
        slots = t.slots;
    }
    const std::string q = lower(query);
    for (size_t k : movement::slotOrder(slots)) {
        const Slot& s = slots[k];
        if (!q.empty() && !s.favourite && lower(s.name + " " + s.note + " teleport").find(q) == std::string::npos) continue;
        PaletteEntry e;
        e.label = "Teleport: " + s.name;
        e.where = "Console  \xE2\x80\xBA  Movement  \xE2\x80\xBA  Teleport  \xC2\xB7  " + movement::formatVec(s.pos, 1);
        e.icon = s.favourite ? icon::StarFill : icon::Send;
        e.boost = s.favourite ? 200 : 0;
        e.run = [s] { gotoSlot(s); };
        e.alt = [] { mvbridge::reveal(kPanelId); };
        e.altHint = "Show";
        out.push_back(std::move(e));
    }
}

}   

BR_FEATURES(movement_teleport) {
    const Page P = Page::Console;
    auto action = [&](const char* id, const char* label, const char* help, void (*run)(Ctx&), bool dangerous = false) {
        r.action({.id = id, .page = P, .tab = kTab, .section = kTeleport, .label = label, .help = help, .run = run, .dangerous = dangerous,
                  .badge = kBadgeNeedsCheck});
    };
    action("movement.tp.save", "Save Position", "Remember where you stand in the quick slot (the Xbox TP Save).", saveQuick);
    action("movement.tp.goto", "Teleport to Saved Position", "Go to the quick slot (the Xbox TP Goto). Your spot before goes to the return history.",
           gotoQuick);
    action("movement.tp.return", "Return to Previous Position", "Undo the last teleport: back to where you were before it.", goBack);
    action("movement.tp.blink", "Blink Forward", "Forward along the camera by the blink distance, at the same height.",
           [](Ctx& c) { teleport(c, Mode::Forward, {blinkDistance(), 0, 0}, true, "Blink"); });
    action("movement.tp.edge", "Teleport Up 500 m (Edge)", "Straight up 500 m: you die when you land (the Xbox TP Edge).",
           [](Ctx& c) { teleport(c, Mode::Edge, {}, true, "Edge"); }, true);
    r.section({.id = kPanelId, .page = P, .tab = kTab, .section = kTeleport, .order = 20, .draw = drawTeleport, .onDisconnect = resetTp,
               .badge = kBadgeNeedsCheck});
    ui::styleSection(kTeleport, {.fullWidth = true, .icon = ui::icon::Send, .ownsActions = true});
    ui::addPaletteProvider(paletteEntries);
}

}   
