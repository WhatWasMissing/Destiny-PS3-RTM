 
 
 
 
 
 
 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <thread>

#include "app/engine.h"
#include "app/registry.h"
#include "core/error.h"
#include "core/runtime.h"
#include "features/movement_shared.h"
#include "features/movement_widgets.h"
#include "game/movement_addr.h"
#include "game/movement_body.h"
#include "game/movement_pad.h"
#include "game/movement_stub.h"
#include "game/movement_w2s.h"
#include "game/world_addr.h"
#include "game/world_objects.h"
#include "game/world_player.h"
#include "ui/widgets.h"

namespace brps3::mvui {

using namespace ui;
using movement::Btn;
using world::Vec3;
using world::hex8;

namespace {

std::string fmt(const char* f, ...) {
    char buf[512];
    va_list a;
    va_start(a, f);
    std::vsnprintf(buf, sizeof buf, f, a);
    va_end(a);
    return buf;
}

std::string v3(const Vec3& v) { return fmt("(%.3f, %.3f, %.3f)", double(v.x), double(v.y), double(v.z)); }

world::LocalPlayer player(Ctx& c, world::Reader& mem) {
    c.require(world::kFingerprints, "World readers");
    return world::requireLocalPlayer(mem);
}

 

void probeBody(Ctx& c) {
    c.require(movement::kPhysicsFingerprints, "Fly / OPK physics (move.fly)");
    world::RuntimeReader mem(c.rt());
    const world::LocalPlayer p = player(c, mem);
    c.log(fmt("player handle %s, sObject %s +0x3C arena handle %s", hex8(p.handle).c_str(), hex8(p.entry).c_str(), hex8(p.object.componentHead).c_str()));
    const movement::BodyChain b = movement::resolveBody(mem, p);
    c.log(fmt("arena %s; definition tag (arena +4) %s -> definition %s", hex8(b.arena).c_str(), hex8(b.defTag).c_str(), hex8(b.def).c_str()));
    c.log(fmt("definition +0x60 count %u (expect 3..64), +0x90 offset %s (expect positive, 4-aligned)", b.count, hex8(b.offset).c_str()));
    c.log(fmt("component %s: +0xF8 physics %s, +0xFC entries %u (expect >= 1), +0x174 flag %u", hex8(b.component).c_str(), hex8(b.physics).c_str(),
              b.entryCount, b.componentFlag));
    c.log(fmt("physics +0x20 body %s: vptr %s (expect 013E1F38 hkpRigidBody)", hex8(b.body).c_str(), hex8(b.bodyVptr).c_str()));
    if (b.body) {
        const uint32_t world = mem.u32(movement::addr::HKP_WORLD_PTR);
        c.log(fmt("body +0x8 world %s (hkpWorld *(01897CC8) = %s), motion vptr %s (expect 013E1FA0 hkpCharacterMotion), motion type %u "
                  "(Xbox player: 7)",
                  hex8(b.bodyWorld).c_str(), hex8(world).c_str(), hex8(b.motionVptr).c_str(), b.motionType));
        c.log("body +0x120 transform " + v3(b.xform) + ", +0x140 com1 " + v3(b.com1) + ", +0x1B0 linear velocity " + v3(b.linvel));
        c.log(fmt("distance transform +0x120 to the engine position sObject +0xB0: %.3f (expect < 1.5)", double(world::distance(b.xform, p.object.position))));
        const movement::IslandScan s = movement::scanIslands(mem, b.body, 16);
        c.log(std::string("player body listed in an island: ") + (s.containsSelf ? "yes" : "NO") + " (OPK must skip it; fly does not need it)");
    }
    if (!b.why.empty()) c.log("note: " + b.why);
    if (b.ok && world::distance(b.xform, p.object.position) < 1.5f)
        c.log("verdict: body chain confirmed: Fly and OPK can find your body (switch Fly on to test the movement)");
    else if (b.ok) c.log("verdict: chain resolves to an hkpRigidBody, but its transform is not where you stand: send this report before flying");
    else c.log("verdict: chain BROKEN at: " + b.why);
}

 

void probeRow(Ctx& c) {
    world::RuntimeReader mem(c.rt());
    const world::LocalPlayer p = player(c, mem);
    if (!p.playerEntry) {
        c.log("no player array row has +0x54 == the handle");
        c.log("verdict: row NOT found (fly forward / OPK gather unavailable)");
        return;
    }
    const world::Bytes row = mem.bytes(p.playerEntry, 0x120);
    const Vec3 pos = row.vec3(world::player::ENTRY_POSITION), fwd = row.vec3(world::player::ENTRY_FORWARD);
    const Vec3 eng = world::readEnginePosition(mem, p);
    c.log(fmt("row %s: +0x54 %s, +0x4C %s, +0x90 faction %d", hex8(p.playerEntry).c_str(), hex8(row.u32(0x54)).c_str(), hex8(row.u32(0x4C)).c_str(),
              int(row.s8(0x90))));
    c.log("+0xE0 position (OPK gather origin) " + v3(pos) + fmt(", %.3f from the engine position +0xB0 (expect < 1)", double(world::distance(pos, eng))));
    c.log("+0x110 camera forward " + v3(fwd) + fmt(", length %.4f (expect 1.0; turn the camera and run again: it follows)", double(fwd.length())));
    const bool ok = pos.finite() && fwd.finite() && std::fabs(fwd.length() - 1.0f) < 0.02f && world::distance(pos, eng) < 1.0f;
    c.log(ok ? "verdict: +0xE0 / +0x110 confirmed (fly direction and OPK target are right)" : "verdict: row fields differ from the Xbox layout (send this report)");
}

 

void probeIslands(Ctx& c) {
    c.require(movement::kPhysicsFingerprints, "Fly / OPK physics (move.opk)");
    world::RuntimeReader mem(c.rt());
    const auto link = session(c).tick.link();
    const movement::IslandScan s = movement::scanIslands(mem, link ? link->body : 0, 48);
    if (!s.worldOk) {
        c.log(s.why);
        c.log("verdict: no physics world here (load into an activity with loose objects)");
        return;
    }
    c.log(fmt("hkpWorld %s vptr %s (expect 013E1640)", hex8(s.world).c_str(), hex8(s.worldVptr).c_str()));
    c.log(fmt("islands: +0x28 array %u, +0x34 array %u (D1 walks both, at most 64 each); %u have vptr 013E2060", s.islands[0], s.islands[1], s.islandsOk));
    c.log(fmt("entities listed %u; %u pass the task's checks (hkpRigidBody, 16-aligned, finite non-zero com1), %u other entity types", s.entities,
              s.bodies, s.otherVptr));
    for (size_t i = 0; i < s.sample.size() && i < 4; ++i) {
        const Vec3 com = mem.bytes(s.sample[i] + movement::hk::BODY_COM1, 12).vec3(0);
        c.log(fmt("  body %s com1 ", hex8(s.sample[i]).c_str()) + v3(com));
    }
    c.log(s.bodies ? "verdict: islands and bodies look as on Xbox: OPK gather / edge have something to move"
                   : "verdict: no movable bodies found (empty area, or the island layout differs: send this report)");
}

 

void probePad(Ctx& c) {
    c.require(movement::kPadFingerprints, "Controller input (move.input)");
    world::RuntimeReader mem(c.rt());
    const world::Bytes block = mem.bytes(movement::addr::PAD_BLOCK, 0x10 + 0x10 + 7 * movement::pad::STATE_SIZE);
    const uint32_t frameIndex = mem.u32(movement::addr::PAD_FRAME_INDEX);
    c.log(fmt("input block 01915A60: enabled %u, neutral flag %u; current frame [019166F0] = %u (expect 0)", block.u8(0), block.u8(1), frameIndex));
    std::string conn;
    for (int i = 0; i < 7; ++i) conn += fmt("%u", block.u8(0x10 + uint32_t(i)));
    c.log("connected[0..6] at 01915A70: " + conn + " (expect 1 for controller 1)");
    const world::Bytes st = block.slice(0x20, movement::pad::STATE_SIZE);
    const movement::PadState pad = movement::parsePortState(st.data().data(), st.size(), block.u8(0x10) != 0);
    c.log("held now: " + (pad.held ? movement::chordText(pad.held) : std::string("nothing")) +
          fmt("; triggers L2 %u R2 %u; sticks L (%d, %d) R (%d, %d)", pad.l2, pad.r2, pad.lx, pad.ly, pad.rx, pad.ry));
    c.log("expected: hold L1 + Cross and push the left stick fully up while this runs: L1+Cross held, LY above 20000, LX near 0");
    const bool pressed = (pad.held & movement::maskOf(Btn::L1)) && (pad.held & movement::maskOf(Btn::Cross)) && pad.ly > 20000;
    if (!pad.connected) c.log("verdict: controller 1 is not connected (connect it and run again)");
    else if (pressed) c.log("verdict: pad layout confirmed (buttons, mask order and stick axis): binds and fly input are right");
    else c.log("verdict: connected; hold L1 + Cross + stick up during the check to confirm the layout");
}

 

void probeTask(Ctx& c) {
    Session& S = session(c);
    const bool wasInstalled = S.tick.installed();
    configure(c, [](movement::Config& p) { p.want.probe = true; });
    struct Undo {
        Ctx& c;
        ~Undo() {
            try {
                configure(c, [](movement::Config& p) { p.want.probe = false; });
            } catch (...) {
            }
        }
    } undo{c};
    c.log(fmt("task %s installed: tick program %u bytes at the block %s (Blackrock's cave and arena only)", movement::kTaskName,
              movement::tickProgramSize(), hex8(S.tick.block()).c_str()));
    auto first = S.tick.read(c.rt());
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    auto c2 = S.tick.read(c.rt());
    if (!first || !c2) throw Error("the task block is gone (Panic during the check?)");
    const uint32_t frames = c2->frames - first->frames;
    c.log(fmt("frames in 0.5 s: %u (expect ~15 at 30 fps)", frames));
    c.log(fmt("player checks: %s (status %u); link handle %s body %s", movement::playerCheckText(c2->playerStatus), c2->playerStatus,
              hex8(c2->handle).c_str(), hex8(c2->body).c_str()));
    if (!S.tick.note().empty()) c.log("note: " + S.tick.note());
    c.log(fmt("controller 1 connected %u, held %s", c2->connected, c2->held ? movement::chordText(c2->held).c_str() : "nothing"));
    (void)wasInstalled;
    if (!frames) c.log("verdict: the task does not run (is the executor ticking? load into an activity)");
    else if (c2->playerStatus == uint32_t(movement::PlayerCheck::Ok))
        c.log("verdict: the task runs and every per-frame player check passes: Fly, OPK and the body teleport route are ready to try");
    else c.log("verdict: the task runs, but the player checks fail (see above): run the body chain check");
}

 

void probeTeleport(Ctx& c) {
    world::RuntimeReader mem(c.rt());
    const world::LocalPlayer p = player(c, mem);
    const Vec3 elem = world::readPosition(mem, p), eng = world::readEnginePosition(mem, p);
    c.log(fmt("descriptor %s element %u at %s", hex8(p.descriptor).c_str(), p.elementIndex, hex8(p.element).c_str()));
    c.log("element +0x2810 (route 1 writes here) " + v3(elem));
    c.log("sObject +0xB0 (engine position)       " + v3(eng));
    if (p.playerEntry) c.log("player row +0xE0                      " + v3(p.entryPosition));
    const float d = world::distance(elem, eng);
    c.log(fmt("distance +0x2810 to +0xB0: %.3f (expect below 0.5 standing still)", double(d)));
    c.log(fmt("an edge teleport would write z = %.2f", double(elem.z + movement::d1::TP_EDGE)));
    if (!elem.finite()) c.log("verdict: +0x2810 is not a position: use the Physics Body route");
    else if (d < 0.5f) c.log("verdict: +0x2810 is your position: route 1 (Position) should work; try Save, walk, Go Back to It");
    else c.log("verdict: +0x2810 is NOT where you stand: switch the Teleport route to Physics Body");
}

 

void probeScale(Ctx& c) {
    world::RuntimeReader mem(c.rt());
    const world::LocalPlayer p = player(c, mem);
    c.log(fmt("you (%s): +0x9C %.4f, +0xBC %.4f (expect 1.0 / 1.0 unscaled)", hex8(p.handle).c_str(), double(p.object.scale), double(p.object.scale2)));
    const world::ObjectScan scan = world::readObjects(mem);
    std::vector<world::SObject> nearby;
    for (const auto& o : scan.objects)
        if (o.live() && o.handle != p.handle && o.position.finite()) nearby.push_back(o);
    std::sort(nearby.begin(), nearby.end(), [&](const auto& a, const auto& b) {
        return world::distance(a.position, p.object.position) < world::distance(b.position, p.object.position);
    });
    int ones = 0, shown = 0;
    for (const auto& o : nearby) {
        if (shown >= 8) break;
        ++shown;
        ones += std::fabs(o.scale - 1.0f) < 0.01f;
        c.log(fmt("  %s %-10s +0x9C %.4f  %.1f m", hex8(o.handle).c_str(), o.typeLabel(), double(o.scale), double(world::distance(o.position, p.object.position))));
    }
    const bool ok = std::fabs(p.object.scale - 1.0f) < 0.01f && ones * 2 >= shown;
    c.log(ok ? "verdict: +0x9C reads as a scale (1.0): try Your Scale 2.0 and look at your character"
             : "verdict: +0x9C is not 1.0 here: send this report before scaling");
}

 

void probeView(Ctx& c) {
    c.require(movement::kViewFingerprints, "World-to-screen (move.w2s)");
    world::RuntimeReader mem(c.rt());
    const movement::View v = movement::readView(mem);
    c.log(fmt("view 01AE6F10 rows (%.3f %.3f %.3f %.3f) (%.3f %.3f %.3f %.3f) (%.3f %.3f %.3f %.3f) (%.3f %.3f %.3f %.3f)", double(v.V[0]),
              double(v.V[1]), double(v.V[2]), double(v.V[3]), double(v.V[4]), double(v.V[5]), double(v.V[6]), double(v.V[7]), double(v.V[8]),
              double(v.V[9]), double(v.V[10]), double(v.V[11]), double(v.V[12]), double(v.V[13]), double(v.V[14]), double(v.V[15])));
    c.log(fmt("projection P0 %.3f P5 %.3f P10 %.3f P11 %.3f P14 %.3f (orbit read 1.93 / 3.43 / -1 / -1 / -0.15)", double(v.P[0]), double(v.P[5]),
              double(v.P[10]), double(v.P[11]), double(v.P[14])));
    c.log(fmt("render size %u x %u (expect 1024 x 624 or 1280 x 720)", v.width, v.height));
    const Vec3 cam = v.cameraPosition();
    c.log("camera position (from the view matrix) " + v3(cam) + ", forward " + v3(v.cameraForward()));
    const auto look = world::findLocalPlayer(mem);
    if (look.player) {
        const Vec3 me = look.player->object.position;
        c.log(fmt("you are %.2f m from the camera (expect < 1 first person, 2..8 third person)", double(world::distance(cam, me))));
        if (auto s = v.project(me + Vec3{0, 0, 1.0f})) c.log(fmt("your chest projects to (%.0f, %.0f)%s", double(s->x), double(s->y), s->onScreen ? "" : " (off screen)"));
        else c.log("your chest is behind the camera (expected only in first person)");
    }
    c.log(v.valid() ? "verdict: matrices are live and finite: world-to-screen works" : "verdict: matrices are not set (menus?): run in an activity");
}

 

struct ViewUi {
    std::mutex m;
    std::optional<movement::View> view;
    std::optional<Vec3> me;
    double at = 0;
    struct Dot {
        Vec3 pos;
        uint8_t type;
        uint32_t handle;
    };
    std::vector<Dot> objects;
    double objectsAt = 0;
    bool live = false;
};
ViewUi& VU() {
    static ViewUi v;
    return v;
}

void readViewJob(Ctx& c) {
    c.require(movement::kViewFingerprints, "World-to-screen (move.w2s)");
    c.require(world::kFingerprints, "World readers");
    world::RuntimeReader mem(c.rt());
    const movement::View v = movement::readView(mem);
    std::optional<Vec3> me;
    if (auto look = world::findLocalPlayer(mem); look.player) me = look.player->object.position;
    ViewUi& u = VU();
    std::lock_guard<std::mutex> lock(u.m);
    u.view = v;
    u.me = me;
    u.at = now();
}

void projectObjectsJob(Ctx& c) {
    world::RuntimeReader mem(c.rt());
    readViewJob(c);
    const world::ObjectScan scan = world::readObjects(mem);
    std::vector<ViewUi::Dot> dots;
    for (const auto& o : scan.objects)
        if (o.live() && o.position.finite() && (o.type == world::TYPE_BIPED || o.type == world::TYPE_VEHICLE || o.type == world::TYPE_CREATURE ||
                                                 o.type == world::TYPE_TURRET || o.type == 0))
            dots.push_back({o.position, o.type, o.handle});
    ViewUi& u = VU();
    std::lock_guard<std::mutex> lock(u.m);
    u.objects = std::move(dots);
    u.objectsAt = now();
}

void setViewLive(bool on) {
    ViewUi& u = VU();
    if (u.live == on) return;
    u.live = on;                                       
    if (!Engine::get().connected()) return;
    Engine::get().post(on ? "View: live on" : "View: live off", [on](Ctx& c) {
        if (on) c.startLoop("movement.view", 250, readViewJob);
        else c.stopLoop("movement.view");
    });
}

ImVec4 typeColor(uint8_t t) {
    switch (t) {
    case world::TYPE_BIPED: return pal::Danger;
    case world::TYPE_VEHICLE: return pal::Info;
    case world::TYPE_CREATURE: return pal::Warning;
    default: return pal::Muted;
    }
}

void drawViewCard(Ui& ui) {
    seedDemo();
    ViewUi& u = VU();
    std::optional<movement::View> view;
    std::optional<Vec3> me;
    std::vector<ViewUi::Dot> dots;
    double at, objAt;
    {
        std::lock_guard<std::mutex> lock(u.m);
        view = u.view;
        me = u.me;
        dots = u.objects;
        at = u.at;
        objAt = u.objectsAt;
    }
    if (demo() && !view) {
         
        movement::View v;
        v.V[0] = 1;                                    
        v.V[5] = 1;                                    
        v.V[10] = -1;                                  
        v.V[12] = 150;
        v.V[13] = -56;
        v.V[14] = 222;
        v.V[15] = 1;
        v.P[0] = 1.93f;
        v.P[5] = 3.43f;
        v.P[10] = -1;
        v.P[11] = -1;
        v.P[14] = -0.15f;
        view = v;
        me = Vec3{-150.0f, 230.0f, 54.0f};
        at = now() - 1;
        for (int i = 0; i < 9; ++i)
            dots.push_back({Vec3{-150.0f + 25.0f * float(i % 4) - 30.0f, 230.0f + 10.0f * float(i), 54.0f + float(i % 3)}, uint8_t(i % 3 == 0 ? 12 : 9),
                            0x2A000000u + uint32_t(i)});
        objAt = now() - 4;
    }
     
    {
        auto offline = requireConnection(ui);
        if (button("Read View", {.tone = Tone::Ghost, .icon = icon::Refresh, .small = true, .busy = ui.busy("Read the view")})) ui.post("Read the view", readViewJob);
        ImGui::SameLine(0, px(6));
        if (button("Live", {.tone = Tone::Ghost, .icon = u.live ? icon::Pause : icon::Play, .small = true, .tooltip = "Read the view 4 times a second",
                            .selected = u.live}))
            setViewLive(!u.live);
        ImGui::SameLine(0, px(6));
        if (button("Project Nearby Objects", {.tone = Tone::Ghost, .icon = icon::View, .small = true,
                                              .tooltip = "One pool read: every biped, vehicle and creature, projected with the current view",
                                              .busy = ui.busy("Project objects")}))
            ui.post("Project objects", projectObjectsJob);
    }
    gap(sp::S);
    const float avail = availWidth();
    const float cw = std::min(avail, px(560));
    const float aspect = view ? float(view->height) / float(std::max<uint16_t>(1, view->width)) : 624.0f / 1024.0f;
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const ImVec2 size(cw, cw * aspect);
    ImGui::Dummy(size);
    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(o, o + size, u32(ImVec4(0, 0, 0, 0.25f)), px(8));
    d->AddRect(o, o + size, u32(pal::Border), px(8), 0, 1.0f);
    const ImVec2 mid = o + size * 0.5f;
    d->AddLine(mid - ImVec2(px(8), 0), mid + ImVec2(px(8), 0), u32(alpha(pal::Muted, 0.6f)), 1.0f);
    d->AddLine(mid - ImVec2(0, px(8)), mid + ImVec2(0, px(8)), u32(alpha(pal::Muted, 0.6f)), 1.0f);
    if (!view) {
        const char* t = ui.connected() ? "Read the view to see what the game camera sees" : "Connect to read the game camera";
        const ImVec2 ts = textSize(FontKind::Body, type::Small, t);
        drawText(d, FontKind::Body, type::Small, mid - ts * 0.5f, u32(pal::Faint), t);
        return;
    }
    auto toCanvas = [&](const movement::ScreenPoint& s) {
        return o + ImVec2(s.x / float(view->width) * size.x, s.y / float(view->height) * size.y);
    };
    int onScreen = 0;
    for (const auto& dot : dots) {
        const auto s = view->project(dot.pos);
        if (!s || !s->onScreen) continue;
        ++onScreen;
        const float r = std::clamp(px(6) * 20.0f / std::max(5.0f, s->depth), px(2.5f), px(6));
        d->AddCircleFilled(toCanvas(*s), r, u32(alpha(typeColor(dot.type), 0.85f)), 16);
    }
    if (me) {
        if (auto s = view->project(*me + Vec3{0, 0, 1.0f}); s && s->onScreen) {
            const ImVec2 p = toCanvas(*s);
            d->AddCircle(p, px(7), u32(pal::Accent), 20, 2.0f);
            drawText(d, FontKind::Semibold, type::Tiny, p + ImVec2(px(10), -px(7)), u32(pal::Accent), "you");
        }
    }
    gap(sp::S);
    const Vec3 cam = view->cameraPosition();
    char line[200];
    std::snprintf(line, sizeof line, "%u x %u  \xC2\xB7  camera %s  \xC2\xB7  %s", view->width, view->height, movement::formatVec(cam, 1).c_str(), ago(at).c_str());
    caption(line, pal::Muted);
    if (!dots.empty()) {
        std::snprintf(line, sizeof line, "%d of %zu objects on screen (red bipeds, blue vehicles, amber creatures)  \xC2\xB7  scanned %s", onScreen, dots.size(),
                      ago(objAt).c_str());
        caption(line, pal::Faint);
    }
}

void drawPadCard(Ui& ui) {
    seedDemo();
    const Snapshot s = snapshot();
    const bool reading = ui.connected() && s.installed && s.c.padOn && now() - s.at < 2.0;
    movement::PadState pad;
    pad.connected = !reading || s.c.connected;
    pad.held = reading ? s.c.held : 0;
    pad.lx = reading ? s.c.sticks[0] : 0;
    pad.ly = reading ? s.c.sticks[1] : 0;
    pad.rx = reading ? s.c.sticks[2] : 0;
    pad.ry = reading ? s.c.sticks[3] : 0;
    const float w = std::min(availWidth(), px(360));
    padDiagram(pad, w, w * 0.6f);
    gap(sp::S);
    char b[160];
    std::snprintf(b, sizeof b, "held %s  \xC2\xB7  L (%d, %d)  R (%d, %d)", pad.held ? movement::chordText(pad.held).c_str() : "nothing", pad.lx, pad.ly, pad.rx,
                  pad.ry);
    if (reading) statusLine(s.c.connected ? pal::Success : pal::Warning, s.c.connected ? "Live" : "No controller 1", b, true);
    else statusLine(pal::Decor, "Not reading", "Live view reads port 1 from the movement task.");
    gap(sp::XS);
    const bool live = padViewOn();
    auto offline = requireConnection(ui);
    if (button(live ? "Stop Live View" : "Live View", {.tone = Tone::Ghost, .icon = live ? icon::Stop : icon::View, .small = true, .selected = live}))
        togglePadView("testing-pad");
}

void resetViewUi() {
    ViewUi& u = VU();
    u.live = false;
}

}   

BR_FEATURES(movement_checks) {
    auto probe = [&](const char* id, const char* label, const char* help, void (*run)(Ctx&), const char* badge) {
        r.action({.id = id, .page = Page::Testing, .tab = kPortChecksTab, .section = kChecks, .label = label, .help = help, .run = run, .badge = badge});
    };
    probe("movement.check.task", "Movement Task on the Console (Dry Run)",
          "Installs the movement task with every feature off and runs its per-frame player checks for half a second (writes only Blackrock's "
          "cave and arena). Expected in an activity: ~15 frames, player checks ok.",
          probeTask, kBadgeNeedsCheck);
    probe("movement.check.body", "Fly / OPK: Player Physics Body Chain",
          "Walks sObject +0x3C -> arena -> definition +0x60 / +0x90 -> component +0xF8 -> +0x20 body and reads the body. Expected: vptr "
          "013E1F38, transform +0x120 within 1.5 m of the engine position.",
          probeBody, kBadgeNeedsCheck);
    probe("movement.check.row", "Player Row: Position +0xE0, Camera Forward +0x110",
          "Reads the player array row fly and OPK use. Expected: +0xE0 within 1 m of the engine position, |+0x110| = 1 and it follows the camera.",
          probeRow, kBadgeNeedsCheck);
    probe("movement.check.islands", "OPK: Physics Islands and Bodies",
          "Walks hkpWorld +0x28 / +0x34 islands and their +0x4C entities as the OPK task does (read-only). Expected: islands with vptr 013E2060 "
          "and some movable hkpRigidBody entries.",
          probeIslands, kBadgeNeedsCheck);
    probe("movement.check.pad", "Controller 1: Pad State Layout",
          "Hold L1 + Cross and push the left stick up while it runs. Reads the input block 01915A60. Expected: connected, L1+Cross held, LY "
          "above 20000.",
          probePad, kBadgeNeedsCheck);
    probe("movement.check.teleport", "Teleport Target +0x2810",
          "Stand still. Compares the descriptor element +0x2810 (teleport route 1) with the engine position sObject +0xB0. Expected: below 0.5.",
          probeTeleport, kBadgeNeedsCheck);
    probe("movement.check.scale", "Scale +0x9C (You and Nearby Objects)",
          "Reads +0x9C for you and the 8 nearest objects. Expected: 1.0 for unscaled objects (the Xbox scale field).", probeScale, kBadgeNeedsCheck);
    probe("movement.check.view", "World-To-Screen: View and Projection",
          "Reads the view / projection matrices and render size at 01AE6F10 and projects your position. Expected: finite matrices, you on "
          "screen in third person.",
          probeView, "");
    r.section({.id = "movement.testing.view", .page = Page::Testing, .tab = kTab, .section = "World to screen", .order = 10, .draw = drawViewCard,
               .onDisconnect = resetViewUi});
    r.section({.id = "movement.testing.pad", .page = Page::Testing, .tab = kTab, .section = "Controller 1", .order = 20, .draw = drawPadCard});
    ui::styleSection("World to screen", {.icon = ui::icon::View});
    ui::styleSection("Controller 1", {.icon = ui::icon::Console});
}

}   
