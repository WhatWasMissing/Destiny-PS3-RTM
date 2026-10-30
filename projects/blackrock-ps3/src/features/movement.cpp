 
 
 
 
 
 
 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <set>

#include "app/engine.h"
#include "app/registry.h"
#include "app/settings.h"
#include "core/error.h"
#include "core/runtime.h"
#include "features/movement_bridge.h"
#include "features/movement_shared.h"
#include "features/movement_widgets.h"
#include "game/movement_addr.h"
#include "game/movement_body.h"
#include "game/movement_stub.h"
#include "game/world_addr.h"
#include "game/world_objects.h"
#include "game/world_player.h"
#include "ui/widgets.h"

namespace brps3::mvui {

using namespace ui;
using movement::Btn;

namespace {

struct UiState {
    std::mutex m;
    Snapshot snap;
    std::vector<movement::Bind> binds;
    bool bindsLoaded = false;
    std::vector<std::string> queue;
    Capture cap;
    movement::ChordCapture machine;
    std::set<std::string> live;
};
UiState& U() {
    static UiState u;
    return u;
}
std::atomic<bool> g_bindsArmed{false};
std::atomic<bool> g_capture{false};
std::atomic<bool> g_live{false};

 
constexpr const char* kFlyId = "movement.fly";
constexpr const char* kBrakeId = "movement.fly.brake";
constexpr const char* kPrecisionId = "movement.fly.precision";
constexpr const char* kSpeedId = "movement.fly.speed";
constexpr const char* kFactorId = "movement.fly.precision_factor";
constexpr const char* kGatherId = "movement.opk.gather";
constexpr const char* kEdgeId = "movement.opk.edge";
constexpr const char* kDistId = "movement.opk.distance";
constexpr const char* kPlayerScaleId = "movement.scale.player";

float fpsNow() {
    const float f = Engine::get().status().fps;
    return f >= 10.0f && f <= 120.0f ? f : 30.0f;
}

void needFingerprints(Ctx& c) {
    c.require(world::kFingerprints, "World readers");
    c.require(movement::kPadFingerprints, "Controller input (move.input)");
    c.require(movement::kPhysicsFingerprints, "Fly / OPK physics (move.fly)");
}

}   

 

double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

bool demo() {
    static const bool on = [] {
        char* v = nullptr;
        size_t n = 0;
        const bool d = _dupenv_s(&v, &n, "BRPS3_UI_DEMO") == 0 && v && v[0] == '1';
        free(v);
        return d;
    }();
    return on;
}

Session& session(Ctx& c) { return c.state<Session>("movement.session"); }

movement::WriteFn writer(Ctx& c, bool verify) {
    Runtime* rt = &c.rt();
    return [rt, verify](uint32_t a, const std::vector<uint8_t>& b) { rt->write(a, b.data(), b.size(), verify); };
}

movement::Config paramsFromSettings(Settings& s) {
    movement::Config p;
    p.speed = std::clamp(s.number(key::FlySpeed, movement::d1::FLY_SPEED), 0.05f, 5.0f);
    p.precision = s.flag(key::FlyPrecision, true);
    p.precisionFactor = std::clamp(s.number(key::FlyFactor, movement::d1::PRECISION), 0.01f, 1.0f);
    p.dist = std::clamp(s.number(key::OpkDist, movement::d1::OPK_DIST), 1.0f, 20.0f);
    p.fps = fpsNow();
    return p;
}

void configure(Ctx& c, const std::function<void(movement::Config&)>& edit) {
    needFingerprints(c);
    Session& S = session(c);
    world::RuntimeReader mem(c.rt());
    const movement::Config cur = S.tick.config();
    movement::Config p = paramsFromSettings(c.settings());
    p.want = cur.want;
    p.brake = cur.brake;
    edit(p);
    p.want.pad = g_bindsArmed.load() || g_capture.load() || g_live.load();
    S.tick.apply(c.rt(), mem, p);
    c.onPanic("movement.tick", [](Ctx& pc) {
        session(pc).tick.shutdown(pc.rt());
        pc.stopLoop(kLoop);
    });
    if (p.want.any()) {
        if (!c.loopRunning(kLoop)) c.startLoop(kLoop, 100, [](Ctx& lc) { statusTick(lc, false); });
    } else {
        c.stopLoop(kLoop);
        publish([](Snapshot& s) { s.installed = false; });
    }
    statusTick(c, true);
}

void statusTick(Ctx& c, bool force) {
    Session& S = session(c);
    const bool fast = g_bindsArmed.load() || g_capture.load() || g_live.load();
    {
        std::lock_guard<std::mutex> lock(S.m);
        const auto t = std::chrono::steady_clock::now();
        if (!force && !fast && t - S.lastRead < std::chrono::milliseconds(400)) return;
        S.lastRead = t;
    }
    std::optional<movement::Control> ctl;
    try {
        ctl = S.tick.read(c.rt());
    } catch (const std::exception&) {
        std::lock_guard<std::mutex> lock(S.m);
        if (++S.failures > 25) throw;                
        return;
    }
    {
        std::lock_guard<std::mutex> lock(S.m);
        S.failures = 0;
    }
    if (!ctl) {
        publish([](Snapshot& s) { s.installed = false; });
        if (!S.tick.installed()) c.stopLoop(kLoop);    
        return;
    }
     
    if (g_bindsArmed.load() || g_capture.load()) {
         
        const bool capturing = g_capture.load();
        const auto list = capturing ? std::vector<movement::Bind>{} : binds();
        std::vector<uint32_t> masks;
        std::vector<std::string> fired;
        uint32_t level;                                
        {
            std::lock_guard<std::mutex> lock(S.m);     
            const bool synced = S.ring.synced();
            level = synced ? S.matcher.previous() : ctl->held;
            masks = S.ring.take(ctl->seq, ctl->ring);
            if (!synced) S.matcher.reset(ctl->held);
            if (capturing) S.matcher.reset(masks.empty() ? level : masks.back());
            else fired = S.matcher.feed(masks, list);
        }
        if (capturing) {
            UiState& u = U();
            std::lock_guard<std::mutex> lock(u.m);
             
             
            if (u.machine.state() == movement::ChordCapture::State::WaitRelease) u.machine.feed(level);
            u.machine.feed(masks);
            u.cap.state = u.machine.state();
            u.cap.partial = u.machine.partial();
            if (!u.machine.active()) u.cap.chord = u.machine.chord();
            if (!masks.empty()) ui::wake();              
        } else {
            for (const std::string& id : fired) {
                queueActivation(id);
                std::string chord;
                for (const auto& b : list)
                    if (b.item == id) chord = movement::chordText(b.chord);
                c.log("[controller] " + chord + " -> " + id);
            }
            if (!fired.empty()) ui::wake();              
        }
    } else {
        std::lock_guard<std::mutex> lock(S.m);
        S.ring.reset();
    }
     
    try {
        world::RuntimeReader mem(c.rt());
        if (S.tick.maintain(c.rt(), mem, *ctl)) c.log("[movement] player changed: physics body re-linked");
    } catch (const std::exception& e) {
        c.log(std::string("[movement] re-link failed: ") + e.what());
    }
    const std::string note = S.tick.note();
    const auto chain = S.tick.chain();
    const double t = now();
    publish([&](Snapshot& s) {
        s.installed = true;
        s.c = *ctl;
        s.at = t;
        s.note = note;
        s.chain = chain;
    });
}

Snapshot snapshot() {
    UiState& u = U();
    std::lock_guard<std::mutex> lock(u.m);
    return u.snap;
}

void publish(const std::function<void(Snapshot&)>& fn) {
    UiState& u = U();
    std::lock_guard<std::mutex> lock(u.m);
    fn(u.snap);
}

void resetUi() {
    g_bindsArmed = false;
    g_capture = false;
    g_live = false;
    UiState& u = U();
    std::lock_guard<std::mutex> lock(u.m);
    const Snapshot old = u.snap;
    u.snap = Snapshot{};
    u.snap.lastTeleport = old.lastTeleport;
    u.snap.lastTeleportOk = old.lastTeleportOk;
    u.cap = Capture{};
    u.machine.stop();
    u.queue.clear();
    u.live.clear();
}

 

void loadBinds() {
    UiState& u = U();
    {
        std::lock_guard<std::mutex> lock(u.m);
        if (u.bindsLoaded) return;
        u.bindsLoaded = true;
    }
    std::vector<movement::Bind> list;
    for (const std::string& e : Engine::get().settings().list(key::Binds))
        if (auto b = movement::decodeBind(e)) list.push_back(*b);
    std::lock_guard<std::mutex> lock(u.m);
    u.binds = std::move(list);
}

std::vector<movement::Bind> binds() {
    loadBinds();
    UiState& u = U();
    std::lock_guard<std::mutex> lock(u.m);
    return u.binds;
}

void setBinds(std::vector<movement::Bind> b) {
    std::vector<std::string> enc;
    for (const auto& x : b) enc.push_back(movement::encodeBind(x));
    if (!demo()) Engine::get().settings().setList(key::Binds, enc);
    UiState& u = U();
    std::lock_guard<std::mutex> lock(u.m);
    u.binds = std::move(b);
    u.bindsLoaded = true;
}

bool bindsArmed() { return g_bindsArmed.load(); }

void queueActivation(const std::string& id) {
    UiState& u = U();
    std::lock_guard<std::mutex> lock(u.m);
    if (u.queue.size() < 32) u.queue.push_back(id);
}

std::vector<std::string> takeActivations() {
    UiState& u = U();
    std::lock_guard<std::mutex> lock(u.m);
    std::vector<std::string> out;
    out.swap(u.queue);
    return out;
}

Capture capture() {
    UiState& u = U();
    std::lock_guard<std::mutex> lock(u.m);
    return u.cap;
}

bool captureWanted() { return g_capture.load(); }
bool liveWanted() { return g_live.load(); }

namespace {
void postConfigure(const char* label) {
    if (!Engine::get().connected()) return;
    Engine::get().post(label, [](Ctx& c) { configure(c, [](movement::Config&) {}); });
}
}   

void startCapture(const std::string& item) {
    {
        UiState& u = U();
        std::lock_guard<std::mutex> lock(u.m);
        u.cap = Capture{};
        u.cap.active = true;
        u.cap.item = item;
        u.cap.started = now();
        u.cap.state = movement::ChordCapture::State::WaitRelease;
        u.machine.start();
    }
    const bool was = g_capture.exchange(true);
    if (!was) postConfigure("Controller: listen for a chord");
}

void stopCapture() {
    {
        UiState& u = U();
        std::lock_guard<std::mutex> lock(u.m);
        u.cap.active = false;
        u.machine.stop();
    }
    if (g_capture.exchange(false)) postConfigure("Controller: stop listening");
}

void setLiveWanted(const std::string& who, bool on) {
    bool changed;
    {
        UiState& u = U();
        std::lock_guard<std::mutex> lock(u.m);
        const bool before = !u.live.empty();
        if (on) u.live.insert(who);
        else u.live.erase(who);
        changed = before != !u.live.empty();
        g_live = !u.live.empty();
    }
    if (changed) postConfigure(on ? "Controller: live view on" : "Controller: live view off");
}

namespace {
constexpr const char* kPadViews[] = {"binds-card", "testing-pad"};
}   

bool padViewOn() {
    UiState& u = U();
    std::lock_guard<std::mutex> lock(u.m);
    for (const char* k : kPadViews)
        if (u.live.count(k)) return true;
    return false;
}

void togglePadView(const std::string& who) {
    if (!padViewOn()) {
        setLiveWanted(who, true);
        return;
    }
    for (const char* k : kPadViews) setLiveWanted(k, false);
}

bool pump() {
    for (const std::string& id : takeActivations()) {
        if (auto it = mvbridge::item(id)) {
            mvbridge::activate(id);
            notify(std::string("Controller: ") + it->label, Level::Info);
        }
    }
    return g_bindsArmed.load() || g_capture.load();
}

 

void seedDemo() {
    static bool done = false;
    if (done || !demo()) return;
    done = true;
    publish([](Snapshot& s) {
        s.installed = true;
        s.at = now() - 0.2;
        s.c.flyOn = true;
        s.c.flyStatus = 0;
        s.c.flyCount = 1842;
        s.c.flyMoves = 611;
        s.c.playerStatus = 0;
        s.c.body = 0x3A6C1F40;
        s.c.handle = 0x5A5A0007;
        s.c.step = movement::stepPerFrame(0.5f, 30);
        s.c.gather = false;
        s.c.opkStatus = 1;
        s.c.connected = 1;
        s.c.padOn = true;
        s.c.held = movement::maskOf(Btn::L1) | movement::maskOf(Btn::Up);
        s.c.sticks[0] = 9200;
        s.c.sticks[1] = 26800;
        s.c.sticks[2] = -4000;
        s.c.sticks[3] = 1200;
        s.havePos = true;
        s.pos = {-412.38f, 1186.02f, 54.71f};
        s.forward = {0.62f, 0.78f, -0.08f};
        s.handle = 0x5A5A0007;
        s.posAt = now() - 3;
        s.lastTeleport = "Moved to -412.38, 1186.02, 54.71.";
        s.scaled = {{0x2A4C0113, 0x601066C0, 10, 1.0f, 2.5f, false}, {0x2A4D0151, 0x6010F3C0, 9, 1.0f, 0.5f, false}};
        movement::IslandScan scan;
        scan.worldOk = true;
        scan.islands[0] = 3;
        scan.islands[1] = 1;
        scan.islandsOk = 4;
        scan.entities = 61;
        scan.bodies = 44;
        scan.containsSelf = true;
        s.islands = scan;
        s.islandsAt = now() - 12;
    });
    setBinds({{"movement.fly", movement::maskOf(Btn::L1) | movement::maskOf(Btn::Up)},
              {"movement.fly.brake", movement::maskOf(Btn::L1) | movement::maskOf(Btn::Down)},
              {"movement.tp.save", movement::maskOf(Btn::L1) | movement::maskOf(Btn::Left)},
              {"movement.tp.goto", movement::maskOf(Btn::L1) | movement::maskOf(Btn::Right)},
              {"app.panic", movement::maskOf(Btn::L2) | movement::maskOf(Btn::R2) | movement::maskOf(Btn::Select)}});
    g_bindsArmed = true;
    for (const char* id : {"movement.fly", "movement.fly.precision", "movement.binds"}) mvbridge::demoToggle(id, true);
}

namespace {

 

void setFly(Ctx& c, bool on) {
    configure(c, [on](movement::Config& p) { p.want.fly = on; });
    if (on) {
        const std::string note = session(c).tick.note();
        c.log(note.empty() ? "[fly] on: left stick moves along the camera, Cross up, Circle down" : "[fly] on: " + note);
    }
}

void setBrake(Ctx& c, bool on) {
    configure(c, [on](movement::Config& p) { p.brake = on; });
}

void setPrecision(Ctx& c, bool on) {
    c.settings().set(std::string(key::FlyPrecision), on);
    if (session(c).tick.installed()) configure(c, [](movement::Config&) {});
}

void setSpeed(Ctx& c, float v) {
    c.settings().set(std::string(key::FlySpeed), std::clamp(v, 0.05f, 5.0f));
    if (session(c).tick.installed()) configure(c, [](movement::Config&) {});
}

void setFactor(Ctx& c, float v) {
    c.settings().set(std::string(key::FlyFactor), std::clamp(v, 0.01f, 1.0f));
    if (session(c).tick.installed()) configure(c, [](movement::Config&) {});
}

constexpr const char* kRelinkLabel = "Find My Physics Body Again";

void relinkBody(Ctx& c) {
    needFingerprints(c);
    world::RuntimeReader mem(c.rt());
    session(c).tick.relink(c.rt(), mem);
    const std::string note = session(c).tick.note();
    c.toast(note.empty() ? "Physics body found" : note);
    if (session(c).tick.installed()) statusTick(c, true);
}

struct SpeedPreset {
    const char* name;
    float v;
};
constexpr SpeedPreset kSpeeds[] = {{"Slow", 0.2f}, {"Normal", 0.5f}, {"Fast", 1.5f}, {"Rocket", 4.0f}};

void nudgeSpeed(float factor) {
    const float cur = mvbridge::value(kSpeedId, movement::d1::FLY_SPEED);
    const float v = std::clamp(std::round(cur * factor * 20.0f) / 20.0f, 0.05f, 5.0f);
    if (mvbridge::applyValue(kSpeedId, v)) {
        char b[48];
        std::snprintf(b, sizeof b, "Fly speed %.2f", double(v));
        notify(b, Level::Info);
    }
}

std::string flyDetail(const Snapshot& s) {
    char b[160];
    std::snprintf(b, sizeof b, "body %08X  \xC2\xB7  %.2f / frame  \xC2\xB7  %u frames hovering, %u moving", s.c.body, double(s.c.step), s.c.flyCount,
                  s.c.flyMoves);
    return b;
}

void drawFly(Ui& ui) {
    seedDemo();
    const Snapshot s = snapshot();
    const bool on = mvbridge::toggleOn(kFlyId);
     
    if (!ui.connected()) statusLine(pal::Decor, "Offline", "Connect to the PS3, then load into an activity.");
    else if (!on || !s.installed || !s.c.flyOn) statusLine(pal::Decor, "Off", "Switch Fly on, then fly with the left stick.");
    else if (s.c.flyStatus == uint32_t(movement::FlyStatus::Flying)) statusLine(pal::Success, "Flying", flyDetail(s), true);
    else if (s.c.flyStatus == uint32_t(movement::FlyStatus::NoController))
        statusLine(pal::Warning, "Controller not connected", "Fly hovers only while controller 1 is connected (as on Xbox).");
    else if (s.c.flyStatus == uint32_t(movement::FlyStatus::Identity))
        statusLine(pal::Warning, "Waiting for your player", s.note.empty() ? "Load into an activity; Fly starts by itself." : s.note);
    else if (s.c.flyStatus == uint32_t(movement::FlyStatus::Body))
        statusLine(pal::Danger, "Physics body check failed", s.note.empty() ? "Run TESTING > Port checks > Movement > Fly body chain." : s.note);
    else if (s.c.flyStatus == uint32_t(movement::FlyStatus::BadForward))
        statusLine(pal::Warning, "Hovering only", "Player row +0x110 is not a camera direction, so the stick is ignored: run TESTING > Port checks > Movement > Player row.");
    else statusLine(pal::Decor, movement::flyStatusText(s.c.flyStatus), "");
    gap(sp::S);
     
    {
        fieldLabel("On the Controller");
        gap(sp::XS + 2);
        auto legend = [](const char* what, bool last) {
            ImGui::SameLine(0, px(7));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (px(22) - textSize(FontKind::Body, type::Small, "A").y) * 0.5f);
            caption(what, pal::Muted);
            if (!last) ImGui::SameLine(0, px(16));
        };
        stickCap(22);
        legend("Move", false);
        padCap(Btn::Cross, false, 22);
        legend("Up", false);
        padCap(Btn::Circle, false, 22);
        legend("Down", false);
        padCap(Btn::L3, false, 22);
        legend("Held: Precision", true);
    }
    gap(sp::M);
     
    fieldLabel("Speed Presets");
    gap(sp::XS + 2);
    {
        auto offline = requireConnection(ui);
        const float cur = mvbridge::value(kSpeedId, movement::d1::FLY_SPEED);
        bool first = true;
        for (const SpeedPreset& p : kSpeeds) {
            if (!first) ImGui::SameLine(0, px(6));
            first = false;
            char label[48];
            std::snprintf(label, sizeof label, "%s  %.2g##spd", p.name, double(p.v));
            if (button(label, {.tone = Tone::Ghost, .small = true, .tooltip = "Set the fly speed (Xbox step per 16 ms)",
                               .selected = std::fabs(cur - p.v) < 0.001f}))
                mvbridge::applyValue(kSpeedId, p.v);
        }
        ImGui::SameLine(0, px(12));
        if (iconButton("##slower", icon::ChevronDown, "Fly slower (x0.67). Bindable: \"Fly Slower\"", false, kSmallControlH)) nudgeSpeed(1.0f / 1.5f);
        ImGui::SameLine(0, px(2));
        if (iconButton("##faster", icon::ChevronUp, "Fly faster (x1.5). Bindable: \"Fly Faster\"", false, kSmallControlH)) nudgeSpeed(1.5f);
    }
    gap(sp::M);
    {
        auto offline = requireConnection(ui);
        const bool busy = ui.busy(kRelinkLabel);
        if (button("Find Body Again", {.tone = Tone::Ghost, .icon = icon::Refresh, .small = true,
                                       .tooltip = "Resolve the player's physics body now (after a respawn this happens by itself within 1.5 s)",
                                       .busy = busy}))
            ui.post(kRelinkLabel, relinkBody);
        if (s.chain && s.chain->ok && !s.chain->why.empty()) {
            ImGui::SameLine(0, px(10));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(5));
            pill("resolved with the Xbox rule", pal::Warning, icon::Info);
            tooltip(s.chain->why.c_str());
        }
    }
}

 

void setGather(Ctx& c, bool on) {
    configure(c, [on](movement::Config& p) { p.want.gather = on; });
}
void setEdge(Ctx& c, bool on) {
    configure(c, [on](movement::Config& p) { p.want.edge = on; });
}
void setDist(Ctx& c, float v) {
    c.settings().set(std::string(key::OpkDist), std::clamp(std::round(v), 1.0f, 20.0f));
    if (session(c).tick.installed()) configure(c, [](movement::Config&) {});
}

constexpr const char* kScanLabel = "Scan the Physics World";

void scanWorld(Ctx& c) {
    needFingerprints(c);
    world::RuntimeReader mem(c.rt());
    const auto link = session(c).tick.link();
    const auto scan = movement::scanIslands(mem, link ? link->body : 0);
    const double t = now();
    publish([&](Snapshot& sn) {
        sn.islands = scan;
        sn.islandsAt = t;
    });
    if (!scan.worldOk) c.toast(scan.why);
    else c.toast(std::to_string(scan.bodies) + " movable bodies in " + std::to_string(scan.islandsOk) + " islands");
}

void drawOpk(Ui& ui) {
    seedDemo();
    const Snapshot s = snapshot();
    const bool gather = mvbridge::toggleOn(kGatherId), edge = mvbridge::toggleOn(kEdgeId);
    char b[160];
    if (!ui.connected()) statusLine(pal::Decor, "Offline", "Connect to the PS3, then load into an activity.");
    else if (!(gather || edge) || !s.installed) statusLine(pal::Decor, "Off", "Gather pulls loose physics objects in front of you; edge flings them up.");
    else if (s.c.opkStatus == uint32_t(movement::OpkStatus::Ran)) {
        std::snprintf(b, sizeof b, "%u bodies this frame, %u moved", s.c.opkSeen, s.c.opkMoved);
        statusLine(pal::Success, gather && edge ? "Gathering + flinging" : gather ? "Gathering" : "Flinging", b, true);
    } else if (s.c.opkStatus == uint32_t(movement::OpkStatus::Identity))
        statusLine(pal::Warning, "Waiting for your player", s.note.empty() ? "Load into an activity." : s.note);
    else statusLine(s.c.opkStatus == uint32_t(movement::OpkStatus::Body) ? pal::Danger : pal::Warning, movement::opkStatusText(s.c.opkStatus),
                    s.c.opkStatus == uint32_t(movement::OpkStatus::NoTarget) ? "Look less steeply up or down." : s.note);
    gap(sp::M);
    {
        auto offline = requireConnection(ui);
        const bool busy = ui.busy(kScanLabel);
        if (button("Scan Physics World", {.tone = Tone::Ghost, .icon = icon::Search, .small = true,
                                          .tooltip = "Read-only: walk the hkpWorld islands as the OPK task would and count what it would move",
                                          .busy = busy}))
            ui.post(kScanLabel, scanWorld);
    }
    if (s.islands) {
        const auto& i = *s.islands;
        ImGui::SameLine(0, px(10));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(5));
        if (!i.worldOk) caption(i.why.c_str(), pal::Warning);
        else {
            std::snprintf(b, sizeof b, "%u + %u islands  \xC2\xB7  %u bodies%s  \xC2\xB7  %s", i.islands[0], i.islands[1], i.bodies,
                          i.otherVptr ? " (+ other entities)" : "", ago(s.islandsAt).c_str());
            caption(b, pal::Muted);
        }
    }
}

 

struct ScaleUi {
    uint32_t handle = 0;
    float scale = 2.0f;
    int kind = 0;                        
    struct Near {
        uint32_t handle;
        uint8_t type;
        float dist;
    };
    std::mutex m;
    std::vector<Near> nearby;
    std::string nearbyNote;
};
ScaleUi& SU() {
    static ScaleUi s;
    return s;
}

void scaleHooks(Ctx& c) {
    Session& S = session(c);
    {
        std::lock_guard<std::mutex> lock(S.m);
        if (!S.disconnectHook) {
            S.disconnectHook = true;
            c.onDisconnect([](Ctx& dc) {
                world::RuntimeReader mem(dc.rt());
                session(dc).scale.restoreAll(mem, writer(dc));
            });
        }
    }
    c.onPanic("movement.scale", [](Ctx& pc) {
        world::RuntimeReader mem(pc.rt());
        std::vector<std::string> notes;
        const size_t n = session(pc).scale.restoreAll(mem, writer(pc), &notes);
        if (n) pc.log("[scale] restored " + std::to_string(n) + " object(s)");
        for (const std::string& note : notes) pc.log("[scale] " + note);
        const auto left = session(pc).scale.edits();    
        publish([&](Snapshot& s) { s.scaled = left; });
    });
}

void publishScaled(Ctx& c) {
    const auto edits = session(c).scale.edits();
    publish([&](Snapshot& s) { s.scaled = edits; });
}

void setPlayerScale(Ctx& c, float v) {
    c.require(world::kFingerprints, "World readers");
    world::RuntimeReader mem(c.rt());
    const world::LocalPlayer p = world::requireLocalPlayer(mem);
    Session& S = session(c);
    scaleHooks(c);
    const auto edit = S.scale.playerEdit();
    if (edit && edit->handle != p.handle) S.scale.restore(mem, writer(c), edit->handle);    
    if (std::fabs(v - 1.0f) < 1e-4f && S.scale.find(p.handle)) S.scale.restore(mem, writer(c), p.handle);
    else S.scale.set(mem, writer(c), p.handle, v, true);
    publishScaled(c);
}

void resetPlayerScale(Ctx& c) {
    world::RuntimeReader mem(c.rt());
    Session& S = session(c);
    if (const auto e = S.scale.playerEdit()) S.scale.restore(mem, writer(c), e->handle);
    publishScaled(c);
}

constexpr const char* kRestoreAllLabel = "Restore Every Scale";

void restoreAllScales(Ctx& c) {
    world::RuntimeReader mem(c.rt());
    std::vector<std::string> notes;
    const size_t n = session(c).scale.restoreAll(mem, writer(c), &notes);
    c.toast("Restored " + std::to_string(n) + (n == 1 ? " object" : " objects") + (notes.empty() ? "" : " (" + notes.front() + ")"));
    publishScaled(c);
}

bool kindMatches(int kind, uint8_t type) {
    switch (kind) {
    case 1: return type == world::TYPE_BIPED;
    case 2: return type == world::TYPE_VEHICLE;
    case 3: return type != world::TYPE_BIPED && type != world::TYPE_VEHICLE;
    default: return true;
    }
}

void findNearby(Ctx& c, int kind) {
    c.require(world::kFingerprints, "World readers");
    world::RuntimeReader mem(c.rt());
    const auto look = world::findLocalPlayer(mem);
    const world::ObjectScan scan = world::readObjects(mem);
    const world::Vec3 me = look.player ? look.player->object.position : world::Vec3{};
    std::vector<ScaleUi::Near> list;
    for (const world::SObject& o : scan.objects) {
        if (!o.live() || (look.player && o.handle == look.player->handle) || !o.position.finite()) continue;
        if (o.type == world::TYPE_WEAPON && look.player && o.parent == look.player->handle) continue;    
        if (!kindMatches(kind, o.type)) continue;
        list.push_back({o.handle, o.type, look.player ? world::distance(o.position, me) : 0.0f});
    }
    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.dist < b.dist; });
    if (list.size() > 12) list.resize(12);
    ScaleUi& s = SU();
    std::lock_guard<std::mutex> lock(s.m);
    s.nearby = std::move(list);
    s.nearbyNote = look.player ? std::string() : "No player: listed in pool order, distances unknown.";
}

void drawScale(Ui& ui) {
    seedDemo();
    ScaleUi& su = SU();
    const Snapshot s = snapshot();
    fieldLabel("Any Object");
    gap(sp::XS + 2);
    inputHex("##scalehandle", &su.handle, 110);
    tooltip("sObject handle (full, with salt): from Nearby, the Spawner's object list or a port check");
    ImGui::SameLine(0, px(6));
    {
        auto offline = requireConnection(ui);
        if (button("Nearby", {.icon = icon::Search, .tooltip = "List the 12 objects closest to you (one pool read)", .busy = ui.busy("Scale: nearby objects")})) {
            const int kind = su.kind;
            ui.post("Scale: nearby objects", [kind](Ctx& c) { findNearby(c, kind); });
        }
    }
    ImGui::SameLine(0, px(10));
    if (pillTabs("##nearkind", {"All", "Bipeds", "Vehicles", "Other"}, &su.kind) && ui.connected() && !ui.busy("Scale: nearby objects")) {
        const int kind = su.kind;
        ui.post("Scale: nearby objects", [kind](Ctx& c) { findNearby(c, kind); });
    }
    {
        std::vector<ScaleUi::Near> nearby;
        std::string note;
        {
            std::lock_guard<std::mutex> lock(su.m);
            nearby = su.nearby;
            note = su.nearbyNote;
        }
        if (!nearby.empty()) {
            gap(sp::S);
            if (beginTable("##nearby", 3, 0, ImGuiTableFlags_BordersOuter)) {
                ImGui::TableSetupColumn("Handle", ImGuiTableColumnFlags_WidthFixed, px(92));
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Distance", ImGuiTableColumnFlags_WidthFixed, px(74));
                tableHeaders();
                for (const auto& n : nearby) {
                    ImGui::TableNextRow(0, px(28));
                    ImGui::TableSetColumnIndex(0);
                    ImGui::PushID(int(n.handle));
                    const float y = ImGui::GetCursorPosY();
                    if (tableRowSelectable("##near", su.handle == n.handle, nullptr, 28 - 2 * ImGui::GetStyle().CellPadding.y / scale())) su.handle = n.handle;
                    ImGui::PopID();
                    ImGui::SetCursorPosY(y);
                    char hb[16];
                    std::snprintf(hb, sizeof hb, "%08X", n.handle);
                    cellText(hb, su.handle == n.handle ? pal::Accent : pal::Text, FontKind::Mono, type::MonoSmall, 28);
                    ImGui::TableSetColumnIndex(1);
                    cellText(world::typeName(n.type), pal::Muted, FontKind::Body, type::Label, 28);
                    ImGui::TableSetColumnIndex(2);
                    char db[24];
                    std::snprintf(db, sizeof db, "%.1f m", double(n.dist));
                    cellText(db, pal::Muted, FontKind::Body, type::Label, 28);
                }
                endTable();
            }
            if (!note.empty()) caption(note.c_str(), pal::Faint);
        }
    }
    gap(sp::S);
    fieldLabel("Scale");
    gap(sp::XS + 2);
    slider("##objscale", &su.scale, movement::kScaleMin, 10.0f, 0.05f);
    gap(sp::XS);
    inputFloat("##objscalev", &su.scale, "%.2f", 80);
    su.scale = std::clamp(su.scale, movement::kScaleMin, movement::kScaleMax);
    for (float q : {0.5f, 1.0f, 2.0f, 5.0f}) {
        ImGui::SameLine(0, px(6));
        char l[16];
        std::snprintf(l, sizeof l, "%gx##q", double(q));
        if (button(l, {.tone = Tone::Ghost, .small = true, .selected = std::fabs(su.scale - q) < 1e-4f})) su.scale = q;
    }
    gap(sp::S);
    {
        auto offline = requireConnection(ui);
        DisabledScope noHandle(su.handle == 0 || su.handle == 0xFFFFFFFFu, "Enter or pick an object handle first");
        const uint32_t h = su.handle;
        const float v = su.scale;
        if (button("Apply", {.tone = Tone::Primary, .icon = icon::Check, .tooltip = "Write sObject +0x9C (identity checked; the original is kept for Restore)",
                             .busy = ui.busy("Scale object")}))
            ui.post("Scale object", [h, v](Ctx& c) {
                c.require(world::kFingerprints, "World readers");
                world::RuntimeReader mem(c.rt());
                scaleHooks(c);
                const float orig = session(c).scale.set(mem, writer(c), h, v);
                char b[96];
                std::snprintf(b, sizeof b, "Scaled %08X to %.2f (was %.2f)", h, double(v), double(orig));
                c.toast(b);
                publishScaled(c);
            });
        ImGui::SameLine(0, px(6));
        if (button("Restore", {.icon = icon::Undo, .tooltip = "Put this object's original scale back", .busy = ui.busy("Restore object scale")}))
            ui.post("Restore object scale", [h](Ctx& c) {
                world::RuntimeReader mem(c.rt());
                std::string why;
                if (!session(c).scale.restore(mem, writer(c), h, &why)) c.toast("Nothing restored: " + why);
                publishScaled(c);
            });
    }
    if (!s.scaled.empty()) {
        gap(sp::M);
        char b[64];
        std::snprintf(b, sizeof b, "Scaled This Session  (%zu)", s.scaled.size());
        fieldLabel(b);
        ImGui::SameLine();
        alignRight(buttonWidth("Restore All", icon::Undo, true));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(8));
        {
            auto offline = requireConnection(ui);
            if (button("Restore All", {.tone = Tone::Ghost, .icon = icon::Undo, .small = true, .busy = ui.busy(kRestoreAllLabel)}))
                ui.post(kRestoreAllLabel, restoreAllScales);
        }
        gap(sp::XS);
        for (const auto& e : s.scaled) {
            ImGui::PushID(int(e.handle));
            const float rowY = ImGui::GetCursorPosY();
            {
                auto offline = requireConnection(ui);
                const uint32_t h = e.handle;
                if (iconButton("##undo1", icon::Undo, "Put this object's original scale back", false, 24))
                    ui.post("Restore object scale", [h](Ctx& c) {
                        world::RuntimeReader mem(c.rt());
                        std::string why;
                        if (!session(c).scale.restore(mem, writer(c), h, &why)) c.toast("Nothing restored: " + why);
                        publishScaled(c);
                    });
            }
            ImGui::SameLine(0, px(8));
            ImGui::SetCursorPosY(rowY + (px(24) - textSize(FontKind::Mono, type::Mono, "0").y) * 0.5f);
            char line[128];
            std::snprintf(line, sizeof line, "%08X", e.handle);
            if (copyable(line, "handle", e.player ? pal::Accent : pal::Text)) su.handle = e.handle;
            ImGui::SameLine(0, px(10));
            ImGui::SetCursorPosY(rowY + (px(24) - textSize(FontKind::Body, type::Small, "A").y) * 0.5f);
            std::snprintf(line, sizeof line, "%s  \xC2\xB7  %.2f \xE2\x86\x92 %.2f", e.player ? "you" : world::typeName(e.type), double(e.original),
                          double(e.applied));
            caption(line, pal::Muted);
            ImGui::PopID();
        }
    }
}

 

void onDisconnectUi() { resetUi(); }

}   

BR_FEATURES(movement_core) {
    const Page P = Page::Console;
     
    r.toggle({.id = kFlyId,
              .page = P,
              .tab = kTab,
              .section = kFly,
              .label = "Fly",
              .help = "Hover, then fly with the left stick along the camera. Cross rises, Circle sinks (the Xbox D1 fly).",
              .set = setFly,
              .badge = kBadgeNeedsCheck});
    r.toggle({.id = kBrakeId,
              .page = P,
              .tab = kTab,
              .section = kFly,
              .label = "Hover in Place (Brake)",
              .help = "While flying, ignore the stick and just hover. Handy as a controller bind.",
              .set = setBrake,
              .parent = kFlyId});
    r.toggle({.id = kPrecisionId,
              .page = P,
              .tab = kTab,
              .section = kFly,
              .label = "Precision While L3 Is Held",
              .help = "Holding L3 (left stick click) slows you to the precision factor.",
              .set = setPrecision,
              .query = [](Ctx& c) { return c.settings().flag(key::FlyPrecision, true); },
              .parent = kFlyId});
    r.value({.id = kSpeedId,
             .page = P,
             .tab = kTab,
             .section = kFly,
             .label = "Speed",
             .help = "Step per tick as on Xbox (D1 default 0.5), scaled to the PS3 frame rate.",
             .min = 0.05f,
             .max = 5.0f,
             .def = movement::d1::FLY_SPEED,
             .step = 0.05f,
             .format = "%.2f",
             .applyWhileDragging = false,
             .set = setSpeed,
             .query = [](Ctx& c) { return c.settings().number(key::FlySpeed, movement::d1::FLY_SPEED); },
             .parent = kFlyId});
    r.value({.id = kFactorId,
             .page = P,
             .tab = kTab,
             .section = kFly,
             .label = "Precision Factor",
             .help = "Speed multiplier while L3 is held (D1 default 0.2).",
             .min = 0.01f,
             .max = 1.0f,
             .def = movement::d1::PRECISION,
             .step = 0.01f,
             .format = "%.2fx",
             .set = setFactor,
             .query = [](Ctx& c) { return c.settings().number(key::FlyFactor, movement::d1::PRECISION); },
             .parent = kPrecisionId});
    r.action({.id = "movement.fly.faster", .page = P, .tab = kTab, .section = kFly, .label = "Fly Faster",
              .help = "Fly speed x1.5 (bind it to the controller).", .run = [](Ctx&) {}, .immediate = [] { nudgeSpeed(1.5f); }, .parent = kFlyId});
    r.action({.id = "movement.fly.slower", .page = P, .tab = kTab, .section = kFly, .label = "Fly Slower",
              .help = "Fly speed x0.67 (bind it to the controller).", .run = [](Ctx&) {}, .immediate = [] { nudgeSpeed(1.0f / 1.5f); }, .parent = kFlyId});
    r.action({.id = "movement.fly.relink", .page = P, .tab = kTab, .section = kFly, .label = kRelinkLabel,
              .help = "Resolve your physics body now (Fly and OPK re-find it by themselves within 1.5 s after a respawn).", .run = relinkBody,
              .badge = kBadgeNeedsCheck});
    r.section({.id = "movement.fly.panel", .page = P, .tab = kTab, .section = kFly, .order = 10, .draw = drawFly, .onDisconnect = onDisconnectUi});
    ui::styleSection(kFly, {.icon = ui::icon::ArrowUp, .ownsActions = true});

     
    r.toggle({.id = kGatherId,
              .page = P,
              .tab = kTab,
              .section = kOpk,
              .label = "Gather Objects (OPK)",
              .help = "Every loose physics object is pulled to a point in front of you (Xbox OPK).",
              .set = setGather,
              .badge = kBadgeNeedsCheck});
    r.toggle({.id = kEdgeId,
              .page = P,
              .tab = kTab,
              .section = kOpk,
              .label = "Edge Fling",
              .help = "Every loose physics object is sent 10 km up, every frame (Xbox edge OPK).",
              .set = setEdge,
              .badge = kBadgeNeedsCheck});
    r.value({.id = kDistId,
             .page = P,
             .tab = kTab,
             .section = kOpk,
             .label = "Gather Distance",
             .help = "How far in front of you objects are gathered (1 to 20 m, D1 default 5).",
             .min = 1,
             .max = 20,
             .def = movement::d1::OPK_DIST,
             .step = 1,
             .format = "%.0f m",
             .set = setDist,
             .query = [](Ctx& c) { return c.settings().number(key::OpkDist, movement::d1::OPK_DIST); },
             .parent = kGatherId});
    r.action({.id = "movement.opk.scan", .page = P, .tab = kTab, .section = kOpk, .label = kScanLabel,
              .help = "Read-only: walk the hkpWorld islands as the OPK task would and count the bodies it would move.", .run = scanWorld,
              .badge = kBadgeNeedsCheck});
    r.section({.id = "movement.opk.panel", .page = P, .tab = kTab, .section = kOpk, .order = 12, .draw = drawOpk});
    ui::styleSection(kOpk, {.icon = ui::icon::Lightbulb, .ownsActions = true});

     
    r.value({.id = kPlayerScaleId,
             .page = P,
             .tab = kTab,
             .section = kScale,
             .label = "Your Scale",
             .help = "Your sObject +0x9C (the Xbox scale). Reset, Panic and disconnect put the original back.",
             .min = 0.1f,
             .max = 10.0f,
             .def = 1.0f,
             .step = 0.05f,
             .format = "%.2fx",
             .set = setPlayerScale,
             .reset = resetPlayerScale,
             .badge = kBadgeNeedsCheck});
    r.section({.id = "movement.scale.panel", .page = P, .tab = kTab, .section = kScale, .order = 14, .draw = drawScale, .badge = kBadgeNeedsCheck});
    r.action({.id = "movement.scale.restore_all", .page = P, .tab = kTab, .section = kScale, .label = kRestoreAllLabel,
              .help = "Put the original scale back on every object scaled this session (you included).", .run = restoreAllScales,
              .badge = kBadgeNeedsCheck});
    ui::styleSection(kScale, {.icon = ui::icon::Sliders, .ownsActions = true});
}

 
void armBinds(bool on) { g_bindsArmed = on; }

}   
