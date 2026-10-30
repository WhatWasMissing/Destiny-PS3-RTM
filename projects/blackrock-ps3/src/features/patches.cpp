 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app/engine.h"
#include "app/registry.h"
#include "app/settings.h"
#include "core/error.h"
#include "core/profile.h"
#include "core/runtime.h"
#include "game/patches_book.h"
#include "game/patches_control.h"
#include "game/patches_sites.h"
#include "game/patches_tick.h"
#include "game/world.h"
#include "game/world_player.h"
#include "ui/widgets.h"

namespace brps3 {
namespace {

using namespace ui;
using patches::Feature;

constexpr const char* kStateKey = "patches";
constexpr const char* kMonitorLoop = "patches.monitor";
constexpr const char* kArgKey = "patches.respawn_arg";
constexpr const char* kEntitleKey = "patches.entitle.";
constexpr const char* kChecks = "Patches";

 
constexpr const char* kRespawn = "Respawn";
constexpr const char* kRules = "Game rules";
constexpr const char* kProjectiles = "Projectiles";
constexpr const char* kCameraMode = "Camera mode";
constexpr const char* kFov = "Field of view";
constexpr const char* kViewmodel = "Viewmodel";
constexpr const char* kSpeed = "Simulation";
constexpr const char* kQol = "Quality of life";
constexpr const char* kGates = "Activity gates";
constexpr const char* kNetwork = "Network and loading";
constexpr const char* kPerformance = "Performance";

std::string fmt(const char* f, ...) {
    char buf[512];
    va_list a;
    va_start(a, f);
    std::vsnprintf(buf, sizeof buf, f, a);
    va_end(a);
    return buf;
}

std::string hex8(uint32_t v) { return fmt("%08X", v); }

bool demoMode() {
    char* v = nullptr;
    size_t n = 0;
    const bool on = _dupenv_s(&v, &n, "BRPS3_UI_DEMO") == 0 && v && v[0] == '1';
    free(v);
    return on;
}

 
 
 

struct Shared {
    std::mutex m;
    patches::Snapshot snap;
    bool known = false;                  
     
    std::deque<float> fps;
    double lastSample = 0;
    bool demoSeeded = false;
};

Shared& shared() {
    static Shared s;
    return s;
}

patches::Snapshot snapshotCopy(bool* known = nullptr) {
    Shared& s = shared();
    std::lock_guard<std::mutex> lock(s.m);
    if (known) *known = s.known;
    return s.snap;
}

 
 
 

struct SessionState {
    patches::Controller ctl;
    std::atomic<bool> hooked{false};
};

void teardown(Ctx& c);

SessionState& sess(Ctx& c) {
    SessionState& s = c.state<SessionState>(kStateKey);
    if (!s.hooked.exchange(true)) {
        c.onDisconnect([](Ctx& cc) { teardown(cc); });
        c.onPanic(kStateKey, [](Ctx& cc) { teardown(cc); });
    }
    return s;
}

void publish(Ctx& c, SessionState& s) {
    for (const std::string& line : s.ctl.takeLog()) c.log(line);
    patches::Snapshot snap = s.ctl.snapshot();
    Shared& sh = shared();
    std::lock_guard<std::mutex> lock(sh.m);
    sh.snap = std::move(snap);
    sh.known = true;
}

void monitorTick(Ctx& c) {
    SessionState& s = sess(c);
    s.ctl.monitor(c.rt(), c.state<world::PlayerCache>("world.player"));
    publish(c, s);
    if (!s.ctl.monitorNeeded()) c.stopLoop(kMonitorLoop);    
}

 
void syncMonitor(Ctx& c, SessionState& s) {
    const bool need = s.ctl.monitorNeeded();
    const bool running = c.loopRunning(kMonitorLoop);
    if (need && !running) c.startLoop(kMonitorLoop, 1000, monitorTick);
    else if (!need && running) c.stopLoop(kMonitorLoop);
}

void teardown(Ctx& c) {
    c.stopLoop(kMonitorLoop);
    SessionState& s = c.state<SessionState>(kStateKey);
    for (const std::string& e : s.ctl.allOff(c.rt())) c.log("patches: could not undo " + e);
    publish(c, s);
}

 
template <class Fn>
void run(Ctx& c, Fn&& fn) {
    SessionState& s = sess(c);
    try {
        fn(s.ctl);
    } catch (...) {
        publish(c, s);
        syncMonitor(c, s);
        throw;
    }
    publish(c, s);
    syncMonitor(c, s);
}

 
 
void requireFeature(Ctx& c, Feature f) {
    const patches::FeatureInfo& fi = patches::info(f);
    const auto fp = patches::fingerprints(f);
    if (fp.empty()) return;
    try {
        c.require(fp, fi.label);
    } catch (const Error&) {
        const std::string bad = patches::checkFeatureBytes(c.rt(), f);
        if (!bad.empty())
            throw Error(std::string(fi.label) + ": game bytes differ from build " + profile::BUILD + " or are patched by another tool: " + bad);
    }
}

 

void setPlain(Ctx& c, Feature f, bool on) {
    if (on) {
        requireFeature(c, f);
        if (f == Feature::InstantDismantle) {
            std::string why;
            if (!patches::dismantleWindowOk(c.rt().read(patches::kDismantleWindow, 0xB8), &why))
                throw Error("Instant dismantle: the inventory hold handler at 007CBAD8 differs from this build (" + why + "): nothing written");
        }
    }
    run(c, [&](patches::Controller& ctl) { ctl.setPatch(c.rt(), f, on); });
}

 

patches::RespawnArg respawnArgSetting(Settings& s) { return patches::RespawnArg(std::clamp(s.integer(kArgKey, 0), 0, 2)); }

void setFastRespawn(Ctx& c, bool on) {
    const patches::RespawnArg arg = respawnArgSetting(c.settings());
    if (on) {
        requireFeature(c, Feature::FastRespawn);
        if (arg == patches::RespawnArg::Native) c.require(patches::kLocalPlayerHandle, "Auto respawn (native handle)");
    }
    run(c, [&](patches::Controller& ctl) {
        if (on) ctl.setRespawnArg(c.rt(), arg);
        ctl.setFastRespawn(c.rt(), c.state<world::PlayerCache>("world.player"), on);
    });
}

void applyRespawnArg(Ctx& c, int a) {
    const auto arg = patches::RespawnArg(std::clamp(a, 0, 2));
    if (arg == patches::RespawnArg::Native) c.require(patches::kLocalPlayerHandle, "Auto respawn (native handle)");
    run(c, [&](patches::Controller& ctl) { ctl.setRespawnArg(c.rt(), arg); });
    c.log(std::string("Auto respawn argument: ") + patches::respawnArgName(arg));
}

 

struct Lever {
    const char* key;
    const char* label;
    const char* tag;                     
    bool warn;                           
    const char* help;
    bool patches::EntitleOptions::*field;
};

const Lever kLevers[] = {
    {"access", "Access Predicate", "recommended", false,
     "Patches the entitlement 'valid' predicate (0xC35140 clrlwi -> li r3,1) so all twelve readers of the access state see it as valid. "
     "The master gate; no side effects were seen on Xbox. Shared with Walkable Lighthouse.",
     &patches::EntitleOptions::access},
    {"override", "Override Byte", "recommended", false,
     "Sets the sticky master override byte (inner + 0x6B8C = 1) that skips the DLC and permission aborts. The inner object comes from "
     "the game's own getter (0x2AFA6C) on the game thread; re-checked every few seconds.",
     &patches::EntitleOptions::overrideByte},
    {"suppress", "Fly-In Suppress", "held", false,
     "Holds byte 0 of *(0x1AD6050) at 1 every frame: the fly-in filler is skipped (entitle.py holds it during Lighthouse loads; it decays).",
     &patches::EntitleOptions::suppress},
    {"valid", "Valid Bit", "superseded", false,
     "Holds root + 0x32C bit 0 every frame (internal_mode.py). The game recomputes it within 2 s; the access predicate makes it unnecessary.",
     &patches::EntitleOptions::validBit},
    {"unlock", "Unlock Leaf", "breaks rules", true,
     "Every unlock flag reads true (0xC5BE34 li r3,1; blr): vendors and nodes show everything, but negative rules break too.",
     &patches::EntitleOptions::unlock},
    {"counter", "Counter Leaf", "no known use", true,
     "Every unlock counter reads 9999 (a cave stub for 0xC5C014). Never showed an effect on Xbox.",
     &patches::EntitleOptions::counter},
    {"gate", "Keep Permission Gate", "old bypass", true,
     "Also patches the state-33 permission branch (0x36EA5C), the old bypass; World-transition bypass uses the same site.",
     &patches::EntitleOptions::gate},
};

patches::EntitleOptions entitleSetting(Settings& s) {
    patches::EntitleOptions o;
    for (const Lever& l : kLevers) o.*(l.field) = s.flag(std::string(kEntitleKey) + l.key, o.*(l.field));
    return o;
}

void requireEntitle(Ctx& c, const patches::EntitleOptions& o) {
    if (o.access) requireFeature(c, Feature::EntitleAccess);
    if (o.unlock) requireFeature(c, Feature::EntitleUnlock);
    if (o.counter) requireFeature(c, Feature::EntitleCounter);
    if (o.gate) requireFeature(c, Feature::EntitleGate);
    if (o.overrideByte || o.validBit) c.require(patches::kEntitleData, "Internal mode (entitlement data)");
}

void setEntitle(Ctx& c, bool on) {
    const patches::EntitleOptions o = entitleSetting(c.settings());
    if (on) requireEntitle(c, o);
    run(c, [&](patches::Controller& ctl) { ctl.setEntitle(c.rt(), on, o); });
}

 
 
 

void seedDemo() {
    Shared& sh = shared();
    if (sh.demoSeeded || !demoMode()) return;
    sh.demoSeeded = true;
    std::lock_guard<std::mutex> lock(sh.m);
    patches::Snapshot& s = sh.snap;
    s.taskRunning = true;
    s.fastRespawn = true;
    s.respawnArmed = true;
    s.playerAlive = true;
    s.latchedHandle = 0xF3A10041;
    s.latchedDatum = 0x00020000;
    s.argument = 0x00020000;
    s.respawnCalls = 0;
    s.respawnNote = "Armed with players datum (+0x4C) = 00020000: respawns you the frame you die.";
    s.entitleOn = true;
    s.entitleNote = "Override byte held at 30A41B9C.";
    sh.known = true;
    for (int i = 0; i < 120; ++i)
        sh.fps.push_back(29.6f + 0.35f * std::sin(float(i) * 0.4f) - (i > 70 && i < 78 ? 6.5f * std::sin(float(i - 70) * 0.45f) : 0.0f));
     
    for (const char* id : {"patch.fast_respawn", "patch.internal_entitle"}) ui::demoShowSwitch(id, true);
}

const char* const kArgTabs[] = {"+0x4C Datum", "+0x54 sObject", "Native Call"};
const char* const kArgHelp[] = {
    "What every PS3 caller of respawn() passes: the player-array row +0x4C (the port map's choice). Confirm it once: "
    "die with Auto respawn on.",
    "The player's sObject handle (row +0x54), what the Xbox plugin passed. Try it if +0x4C does not bring you back.",
    "Asks the game for the local player handle in the same frame (local_player_handle(0)), like the game's own respawn path.",
};

bool anyLever(const patches::EntitleOptions& o) {
    for (const Lever& l : kLevers)
        if (o.*(l.field)) return true;
    return false;
}

 
 
 
void selectRespawnArg(int arg) {
    arg = std::clamp(arg, 0, 2);
    Settings& settings = Engine::get().settings();
    const int before = std::clamp(settings.integer(kArgKey, 0), 0, 2);
    if (arg == before) return;
    settings.set(std::string(kArgKey), arg);
    if (Engine::get().connected() && snapshotCopy().fastRespawn) {
        noteQueued("Respawn argument");
        Engine::get().post("Respawn argument", [arg](Ctx& c) { applyRespawnArg(c, arg); },
                           [before](bool ok, const std::string&) {
                               if (!ok) Engine::get().settings().set(std::string(kArgKey), before);
                           });
    }
}

 
 
 
void toggleLever(const Lever& l) {
    Settings& settings = Engine::get().settings();
    patches::EntitleOptions o = entitleSetting(settings);
    const bool want = !(o.*(l.field));
    o.*(l.field) = want;
    const bool live = Engine::get().connected() && snapshotCopy().entitleOn;
    if (live && !anyLever(o)) {
        notify("Internal mode needs at least one lever: turn Internal mode off instead", Level::Warn);
        return;
    }
    const std::string key = std::string(kEntitleKey) + l.key;
    settings.set(key, want);
    if (live) {
        noteQueued("Internal mode levers");
        Engine::get().post(
            "Internal mode levers",
            [o](Ctx& c) {
                requireEntitle(c, o);
                run(c, [&](patches::Controller& ctl) { ctl.setEntitle(c.rt(), true, o); });
            },
            [key, want](bool ok, const std::string&) {
                if (!ok) Engine::get().settings().set(key, !want);
            });
    }
}

void drawRespawnOptions(Ui& ui) {
    seedDemo();
    bool known = false;
    const patches::Snapshot snap = snapshotCopy(&known);
    fieldLabel("Respawn Argument");
    gap(sp::XS + 2);
    int arg = std::clamp(ui.settings().integer(kArgKey, 0), 0, 2);
    const std::vector<std::string> tabs(std::begin(kArgTabs), std::end(kArgTabs));
    if (pillTabs("##respawnarg", tabs, &arg)) selectRespawnArg(arg);
    tooltip((std::string("Which handle Auto respawn passes to respawn(handle, 0, -1). Takes effect at once while it is on. ") + kArgHelp[arg]).c_str());
    gap(sp::M);

     
    ImVec4 tone = pal::Faint;
    bool pulse = false;
    std::string text, note;
    if (!ui.connected() && !(demoMode() && known)) {
        text = "Not connected";
        note = "Connect, then turn on Auto respawn: it brings you back the frame you die.";
    } else if (!snap.fastRespawn) {
        text = snap.latchedHandle ? "Off  \xC2\xB7  your handle is latched" : "Off";
        note = snap.latchedHandle ? std::string("Latched while you were alive: turning it on now also brings you back if you are already dead.")
                                  : std::string("No player handle seen in this activity yet: load in, then turn it on.");
    } else if (snap.respawnArmed) {
        tone = pal::Success;
        const bool dead = snap.lastSeen == 0 || snap.lastSeen == 0xFFFFFFFFu;
        pulse = dead && snap.respawnCalls > 0;
        text = snap.respawnCalls ? fmt("Armed  \xC2\xB7  %u respawn call%s so far", snap.respawnCalls, snap.respawnCalls == 1 ? "" : "s")
                                 : std::string("Armed  \xC2\xB7  waiting for you to die");
        note = snap.respawnNote;
    } else {
        tone = pal::Warning;
        text = "Not armed yet";
        note = snap.respawnNote.empty() ? std::string("Waiting for your player.") : snap.respawnNote;
    }
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float lineH = textSize(FontKind::Semibold, type::Label, "Ag").y;
        ImGui::Dummy(ImVec2(availWidth(), lineH));
        ImDrawList* d = ImGui::GetWindowDrawList();
        const ImVec2 c(p.x + px(5), p.y + lineH * 0.5f);
        if (pulse && !reduceMotion()) {
            const float t = float(std::fmod(ImGui::GetTime(), 1.6) / 1.6);
            d->AddCircleFilled(c, px(4) + px(6) * t, u32(tone, 0.35f * (1.0f - t)), 20);
            requestRedraw();
        }
        d->AddCircleFilled(c, px(4), u32(tone), 16);
        drawText(d, FontKind::Semibold, type::Label, ImVec2(p.x + px(18), p.y), u32(snap.respawnArmed ? pal::Text : pal::Muted), text.c_str());
    }
    if (snap.fastRespawn && snap.respawnArmed && snap.respawnArg != patches::RespawnArg::Native) {
         
        gap(sp::XS);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + px(18));
        hashLabel(uint32_t(snap.argument), "respawn argument", pal::Accent);
        ImGui::SameLine(0, px(8));
        caption(patches::respawnArgName(snap.respawnArg), pal::Muted);
    } else if (!snap.fastRespawn && snap.latchedHandle && arg != int(patches::RespawnArg::Native)) {
         
        gap(sp::XS);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + px(18));
        hashLabel(arg == int(patches::RespawnArg::ObjectHandle) ? snap.latchedHandle : snap.latchedDatum, "latched handle", pal::Muted);
        ImGui::SameLine(0, px(8));
        caption(patches::respawnArgName(patches::RespawnArg(arg)), pal::Faint);
        gap(sp::XS);
        wrapped(note.c_str(), pal::Faint, type::Small);
    } else if (!note.empty()) {
        gap(sp::XS);
        wrapped(note.c_str(), pal::Faint, type::Small);
    }
    gap(sp::M);
    if (button("Check It in Game", {.tone = Tone::Ghost, .icon = icon::Probe, .small = true,
                                    .tooltip = "Opens TESTING > Port checks: run 'Auto Respawn: Respawn Argument' (section Patches) while dead"}))
        openPage(Page::Testing, kPortChecksTab);
}

void drawEntitleLevers(Ui& ui) {
    const patches::Snapshot snap = snapshotCopy();
    const patches::EntitleOptions o = entitleSetting(ui.settings());
    fieldLabel("Levers");
    gap(sp::XS + 2);
    const float rowW = availWidth();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, px(9)));
    for (const Lever& l : kLevers) {
        ImGui::PushID(l.key);
        bool v = o.*(l.field);
        const ImVec2 rowPos = ImGui::GetCursorScreenPos();
        if (checkbox(l.label, &v)) toggleLever(l);
        tooltip(l.help);
         
        const float rowH = ImGui::GetItemRectSize().y;
        const ImVec2 ts = textSize(FontKind::Body, type::Small, l.tag);
        const ImVec4 tagColor = l.warn ? mix(pal::Warning, pal::Muted, 0.35f) : pal::Faint;
        drawText(ImGui::GetWindowDrawList(), FontKind::Body, type::Small,
                 ImVec2(rowPos.x + rowW - ts.x, textTop(FontKind::Body, type::Small, rowPos.y + rowH * 0.5f)), u32(tagColor), l.tag);
        ImGui::PopID();
    }
    ImGui::PopStyleVar();
    const bool busy = ui.busy("Internal mode levers");
    if (snap.entitleOn) {
        gap(sp::S);
        const std::string status = snap.entitleNote.empty() ? std::string("Levers on.") : snap.entitleNote;
        wrapped(status.c_str(), pal::Muted, type::Small);
    }
    if (snap.entitleOn && !busy && !(o == snap.entitle)) {
         
        std::string held;
        for (const Lever& l : kLevers)
            if (snap.entitle.*(l.field)) held += (held.empty() ? "" : ", ") + std::string(l.label);
        gap(sp::XS);
        wrapped(("Applied now: " + (held.empty() ? std::string("none") : held) + ". Tick a lever again to retry, or see the log.").c_str(),
                pal::Warning, type::Small);
    }
}

void drawPerformance(Ui& ui) {
    seedDemo();
    Shared& sh = shared();
    const EngineStatus st = Engine::get().status();
    const double now = ImGui::GetTime();
    if (ui.connected() && st.ticking && now - sh.lastSample >= 0.25) {
        sh.lastSample = now;
        sh.fps.push_back(st.fps);
        while (sh.fps.size() > 120) sh.fps.pop_front();
    }
    const bool show = (ui.connected() && st.ticking) || (demoMode() && !sh.fps.empty());
    if (!show) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = availWidth(), h = px(58);
        ImGui::Dummy(ImVec2(w, h));
        ImDrawList* d = ImGui::GetWindowDrawList();
        d->AddRectFilled(p, p + ImVec2(w, h), u32(pal::Raised), px(kRowRounding));
        d->AddRect(p, p + ImVec2(w, h), u32(pal::Border), px(kRowRounding), 0, 1.0f);
        const ImVec2 is = textSize(FontKind::Icons, 16, icon::Power);
        drawText(d, FontKind::Icons, 16, ImVec2(p.x + px(16), p.y + (h - is.y) * 0.5f), u32(pal::Muted), icon::Power);
        drawText(d, FontKind::Semibold, type::Label, ImVec2(p.x + px(44), textTop(FontKind::Semibold, type::Label, p.y + h * 0.5f)), u32(pal::Text),
                 ui.connected() ? "Waiting for the Game Thread" : "Connect to See the Game's Frame Rate");
        return;
    }
    float cur = sh.fps.empty() ? st.fps : sh.fps.back();
    float sum = 0, lo = 1e9f, hi = 0;
    for (float f : sh.fps) {
        sum += f;
        lo = std::min(lo, f);
        hi = std::max(hi, f);
    }
    const float avg = sh.fps.empty() ? 0.0f : sum / float(sh.fps.size());
    if (sh.fps.empty()) lo = hi = cur;
    const float avail = availWidth();
    const float w = std::floor((avail - px(8) * 2) / 3);
    auto tone = [](float f) { return f >= 27.0f ? pal::Success : f >= 20.0f ? pal::Warning : pal::Danger; };
    statTile("fps.now", "Now", fmt("%.1f fps", double(cur)), {}, w, tone(cur));
    ImGui::SameLine(0, px(8));
    statTile("fps.avg", "Average  30 s", fmt("%.1f", double(avg)), {}, w);
    ImGui::SameLine(0, px(8));
    statTile("fps.low", "Low  30 s", fmt("%.1f", double(lo)), {}, w, tone(lo));
    gap(sp::S);
     
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = px(56);
    ImGui::Dummy(ImVec2(avail, h));
    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(p, p + ImVec2(avail, h), u32(pal::Input), px(6));
    const float top = std::max(32.0f, hi + 1.0f), bottom = std::max(0.0f, std::min(lo - 2.0f, 20.0f));
    auto yOf = [&](float f) { return p.y + h - px(6) - (f - bottom) / (top - bottom) * (h - px(12)); };
     
    const float g30 = yOf(30.0f);
    d->AddLine(ImVec2(p.x + px(6), g30), ImVec2(p.x + avail - px(6), g30), u32(pal::Border), 1.0f);
    drawText(d, FontKind::Body, type::Tiny, ImVec2(p.x + px(10), g30 - px(15)), u32(pal::Faint), "30");
    if (sh.fps.size() >= 2) {
        std::vector<ImVec2> pts;
        const size_t n = sh.fps.size();
        for (size_t i = 0; i < n; ++i) {
            const float x = p.x + px(6) + (avail - px(12)) * float(i) / float(std::max<size_t>(119, n - 1));
            pts.push_back(ImVec2(x, yOf(sh.fps[i])));
        }
        std::vector<ImVec2> fill = pts;
        fill.push_back(ImVec2(pts.back().x, p.y + h - px(4)));
        fill.push_back(ImVec2(pts.front().x, p.y + h - px(4)));
        d->AddConcavePolyFilled(fill.data(), int(fill.size()), u32(alpha(pal::Accent, 0.10f)));
        d->AddPolyline(pts.data(), int(pts.size()), u32(pal::Accent), 0, px(1.6f));
        d->AddCircleFilled(pts.back(), px(3), u32(pal::Accent));
    }
    tooltip("Game frames per second from the executor's per-frame heartbeat (about 30 in an activity), over the last 30 seconds. There is no "
            "in-game overlay on PS3.");
}

 
 
void paletteEntries(const std::string& query, std::vector<PaletteEntry>& out) {
    if (query.size() < 2) return;
    Settings& settings = Engine::get().settings();
    const int cur = std::clamp(settings.integer(kArgKey, 0), 0, 2);
    static const char* const names[] = {"players datum (+0x4C)", "sObject handle (+0x54)", "native call"};
    for (int i = 0; i < 3; ++i) {
        PaletteEntry e;
        e.label = std::string("Auto Respawn Argument: ") + names[i];
        e.where = "Console  \xE2\x80\xBA  Player  \xE2\x80\xBA  Respawn";
        e.icon = icon::Restore;
        e.badge = i == cur ? "selected" : "";
        e.run = [i] { selectRespawnArg(i); };
        out.push_back(std::move(e));
    }
    {
         
        PaletteEntry e;
        e.label = "Game FPS (Frame-Rate Readout)";
        e.where = "Console  \xE2\x80\xBA  World  \xE2\x80\xBA  Performance";
        e.icon = icon::Testing;
        e.run = [] { openPage(Page::Console, "World"); };
        out.push_back(std::move(e));
    }
    const patches::EntitleOptions o = entitleSetting(settings);
    for (const Lever& l : kLevers) {
        PaletteEntry e;
        e.label = std::string("Internal Mode Lever: ") + l.label;
        e.where = std::string("Console  \xE2\x80\xBA  World  \xE2\x80\xBA  Activity Gates  \xC2\xB7") + l.tag;
        e.icon = icon::Link;
        e.badge = o.*(l.field) ? "on" : "off";
        const Lever* lever = &l;
        e.run = [lever] { toggleLever(*lever); };
        out.push_back(std::move(e));
    }
}

 
 
 
 
bool recoverOnConnect(Ctx& c) {
    SessionState& s = sess(c);
    s.ctl.recoverLeftovers(c.rt());
    s.ctl.setTracking(true);
    publish(c, s);
    syncMonitor(c, s);
    return false;
}

void resetOnDisconnect() {
    Shared& s = shared();
    std::lock_guard<std::mutex> lock(s.m);
    s.snap = {};
    s.known = false;
    s.fps.clear();
    s.lastSample = 0;
    s.demoSeeded = false;
}

 
 
 

void probeSites(Ctx& c) {
    Runtime& rt = c.rt();
    SessionState& s = sess(c);
    size_t original = 0, ours = 0, other = 0, foreign = 0;
    for (const patches::Unit& u : patches::units()) {
        std::vector<uint32_t> cur;
        const auto states = patches::readUnit(rt, u, &cur);
        const auto holders = s.ctl.book().holders(u.name);
        std::string line = fmt("%-34s", u.label);
        for (size_t i = 0; i < u.words.size(); ++i) {
            line += fmt("  %s=%s", hex8(u.words[i].addr).c_str(), hex8(cur[i]).c_str());
            if (states[i] == patches::SiteState::Original) ++original;
            else if (states[i] == patches::SiteState::Foreign) ++foreign;
            else if (s.ctl.book().applied(u.name)) ++ours;
            else ++other;
        }
        const bool allOrig = std::all_of(states.begin(), states.end(), [](auto st) { return st == patches::SiteState::Original; });
        const bool anyForeign = std::any_of(states.begin(), states.end(), [](auto st) { return st == patches::SiteState::Foreign; });
        std::string who;
        for (const auto& h : holders) who += (who.empty() ? "" : ", ") + h;
        line += anyForeign ? "  FOREIGN (neither original nor our patch)"
                : allOrig  ? "  original"
                : s.ctl.book().applied(u.name) ? "  patched by us (" + who + ")"
                                               : std::string("  patched by another owner") + (*u.sharedWith ? std::string(" (") + u.sharedWith + ")" : "");
        c.log(line);
    }
    const uint32_t leaf = rt.u32(patches::kCounterLeaf);
    c.log(fmt("%-34s  %s=%s  %s", "Counter leaf (cave stub)", hex8(patches::kCounterLeaf).c_str(), hex8(leaf).c_str(),
              leaf == patches::kCounterLeafOriginal ? "original" : (leaf >> 26) == 18u ? (rt.hooked(patches::kCounterLeaf) ? "branch to our stub" : "branch (another tool)") : "FOREIGN"));
    std::string why;
    const bool window = patches::dismantleWindowOk(rt.read(patches::kDismantleWindow, 0xB8), &why);
    c.log(std::string("instant-dismantle window 007CBAD8 (0xB8 bytes): ") + (window ? "matches" : "DIFFERS: " + why));
    c.log(fmt("expected: every site 'original' (or patched by us while a switch is on); %zu original, %zu ours, %zu other owner, %zu foreign", original,
              ours, other, foreign));
    c.log(foreign == 0 && window ? "verdict: every patch site matches the port map (safe to switch on)"
                                 : "verdict: some site differs from the port map: do not use the features listed as FOREIGN; send this report");
}

void probeRespawn(Ctx& c) {
    c.require(patches::kRespawnCall, "Auto respawn probe");
    world::RuntimeReader mem(c.rt());
    const world::PlayerLookup r = world::findLocalPlayer(mem);
    const uint32_t slot = mem.u32(patches::addr::LOCAL_PLAYER_HANDLE_SLOT);
    uint32_t native = 0xFFFFFFFFu;
    if (world::mapped(slot, patches::addr::LOCAL_PLAYER_HANDLE_OFFSET + 4)) native = mem.u32(slot + patches::addr::LOCAL_PLAYER_HANDLE_OFFSET);
    c.log(fmt("local_player_handle(0) reads *(*(01B294F0)+0xCC): slot %s -> %s", hex8(slot).c_str(), hex8(native).c_str()));
    if (r.player) {
        const world::LocalPlayer& p = *r.player;
        c.log(fmt("alive: element %s +0x84 = %s; player row %s: +0x54 sObject %s, +0x4C datum %s", hex8(p.element).c_str(), hex8(p.handle).c_str(),
                  hex8(p.playerEntry).c_str(), hex8(p.handle).c_str(), hex8(p.playerDatum).c_str()));
        c.log(native == p.playerDatum ? "local_player_handle(0) equals +0x4C: the game's own respawn path and the port map agree"
                                      : fmt("local_player_handle(0) %s differs from +0x4C %s: note which one respawns you", hex8(native).c_str(),
                                            hex8(p.playerDatum).c_str()));
    } else {
        c.log(std::string("no live player: ") + world::playerStatusName(r.status) + " (" + r.message + ")");
        const uint32_t desc = mem.u32(world::addr::PLAYER_DESC_PTR);
        if (world::heap(desc, 0x10)) {
            const uint32_t elements = mem.u32(desc + 4);
            if (world::heap(elements, 0x88)) c.log(fmt("descriptor %s, element 0 +0x84 = %s (0 or FFFFFFFF = dead)", hex8(desc).c_str(), hex8(mem.u32(elements + 0x84)).c_str()));
        }
    }
    SessionState& s = sess(c);
    try {
        s.ctl.refresh(c.rt());
    } catch (const std::exception&) {
    }
    patches::Snapshot a = s.ctl.snapshot();
    if (!a.fastRespawn) {
        c.log("Auto respawn is off. To confirm the argument: turn it on, die, then run this check while dead.");
        c.log("expected: the respawn-call counter rises every frame while you are dead and you respawn within a second.");
        c.log(r.player ? "verdict: inputs read; switch Auto respawn on and die to finish the check" : "verdict: load into an activity and run again");
        return;
    }
    c.log(fmt("Auto respawn: %s, argument %s = %016llX, %u calls, last element +0x84 %s", a.respawnArmed ? "armed" : "NOT armed",
              patches::respawnArgName(a.respawnArg), static_cast<unsigned long long>(a.argument), a.respawnCalls, hex8(a.lastSeen).c_str()));
    if (!a.respawnArmed) {
        c.log("note: " + a.respawnNote);
        c.log("verdict: not armed yet (see the note above)");
        return;
    }
    const uint32_t before = a.respawnCalls;
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    try {
        s.ctl.refresh(c.rt());
    } catch (const std::exception&) {
    }
    a = s.ctl.snapshot();
    const uint32_t delta = a.respawnCalls - before;
    c.log(fmt("calls in 0.6 s: %u (about 18 expected while dead, 0 while alive)", delta));
    if (delta > 0)
        c.log(std::string("verdict: respawn() is called every frame with ") + patches::respawnArgName(a.respawnArg) +
              ". If you came back, this argument works (keep it); if not, pick the next argument on the Respawn card and die again");
    else if (r.player)
        c.log("verdict: armed and you are alive: die to see the calls (run this again while dead)");
    else
        c.log("verdict: dead but no calls: the descriptor changed since it was armed (activity change?); respawn manually once, then test again");
}

void probeValues(Ctx& c) {
    world::RuntimeReader mem(c.rt());
    std::vector<std::string> odd;
     
    const uint32_t clk1 = mem.u32(patches::addr::ENGINE_CLOCK_LO);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const uint32_t clk2 = mem.u32(patches::addr::ENGINE_CLOCK_LO);
    c.log(fmt("engine clock 01A1D67C: %s -> %s (%s)", hex8(clk1).c_str(), hex8(clk2).c_str(), clk1 != clk2 ? "advancing" : "STALLED"));
     
    const uint32_t fov = mem.u32(patches::addr::FOV_OVERRIDE);
    c.log(fmt("FOV override 0189A0C8 = %s (%.2f); expected BF800000 (-1.0) while FOV override is off", hex8(fov).c_str(), double(world::bitsFloat(fov))));
    if (!patches::fovGlobalPlausible(world::bitsFloat(fov))) odd.push_back("FOV global");
     
    const uint32_t gp = mem.u32(patches::addr::GAME_TIME_PTR);
    if (world::heap(gp, 8)) {
        const uint32_t v = mem.u32(gp + 4);
        c.log(fmt("game time *(01AB6714) = %s, +4 speed = %s (%.3f); expected 3F800000 (1.0), 0.0 while paused", hex8(gp).c_str(), hex8(v).c_str(),
                  double(world::bitsFloat(v))));
        if (!(world::bitsFloat(v) == 0.0f || patches::speedValid(world::bitsFloat(v)))) odd.push_back("game speed");
    } else {
        c.log(fmt("game time *(01AB6714) = %s: not set up (menus?)", hex8(gp).c_str()));
    }
     
    const world::Bytes l = mem.bytes(patches::addr::SIGNIN_LISTENER, 0x10);
    c.log(fmt("sign-in listener 01B6AF78: vtable %s (expected 013FB548), user-0 flag word 01B6AF80 = %s (flag byte %02X)", hex8(l.u32(0)).c_str(),
              hex8(l.u32(8)).c_str(), l.u8(8)));
    if (l.u32(0) != patches::addr::SIGNIN_LISTENER_VTABLE) odd.push_back("listener vtable");
     
    try {
        const patches::ViewmodelTarget t = patches::resolveViewmodel(mem);
        if (t.ok)
            c.log(fmt("viewmodel settings handle %s -> block %s, +0x10 = %s (%.3f rad = %.1f deg); expected about 1.344 rad (77 deg)", hex8(t.handle).c_str(),
                      hex8(t.base).c_str(), hex8(t.current).c_str(), double(world::bitsFloat(t.current)),
                      double(world::bitsFloat(t.current)) / patches::kDegToRad));
        else
            c.log("viewmodel settings: " + t.why);
        if (t.ok && !patches::viewmodelRadiansValid(world::bitsFloat(t.current))) odd.push_back("viewmodel angle");
    } catch (const std::exception& e) {
        c.log(std::string("viewmodel settings: ") + e.what());
    }
    const patches::Snapshot a = sess(c).ctl.snapshot();
    if (a.taskRunning)
        c.log(fmt("per-frame task: block %s, flags %02X, frames %u, speed pause skips %u", hex8(a.block).c_str(), a.flags, a.frames, a.speedSkips));
    else
        c.log("per-frame task: not running (no FOV / speed / viewmodel / inactivity / respawn switch on)");
    if (odd.empty()) c.log("verdict: every per-frame target holds a plausible value");
    else {
        std::string list;
        for (const auto& o : odd) list += (list.empty() ? "" : ", ") + o;
        c.log("verdict: unexpected value at " + list + " (send this report)");
    }
}

void probeEntitle(Ctx& c) {
    world::RuntimeReader mem(c.rt());
    const patches::EntitleChain ch = patches::readEntitleChain(mem);
    c.log(fmt("root *(01AB6458) = %s%s", hex8(ch.root).c_str(), ch.rootOk ? "" : " (not a heap pointer: menus?)"));
    if (ch.rootOk) c.log(fmt("valid dword root+0x32C = %s, bit 0 = %u (0 = denied, as on Xbox 0x0E)", hex8(ch.valid).c_str(), ch.valid & 1));
    c.log(fmt("provider 01AB6250: vtable %s (expected 01411170), +4 %s, +0xC inner %s (both 0 between evaluations)", hex8(ch.providerVtable).c_str(),
              hex8(ch.providerRoot).c_str(), hex8(ch.providerInner).c_str()));
    if (world::heap(ch.providerInner, patches::addr::ENTITLE_OVERRIDE_OFFSET + 1))
        c.log(fmt("override byte inner+0x6B8C = %02X", mem.bytes(ch.providerInner + patches::addr::ENTITLE_OVERRIDE_OFFSET, 1).u8(0)));
    c.log(fmt("fly-in suppress *(01AD6050) = %s%s", hex8(ch.suppressObj).c_str(),
              ch.suppressOk ? fmt(", byte 0 = %u (1 = suppressed)", ch.suppress).c_str() : " (no object)"));
    for (const char* name : {"site.00C35140", "site.00C5BE34", "site.0036EA5C", "patches.wt_dlc"}) {
        const patches::Unit* u = patches::findUnit(name);
        std::vector<uint32_t> cur;
        const auto st = patches::readUnit(c.rt(), *u, &cur);
        std::string words;
        for (size_t i = 0; i < cur.size(); ++i) words += fmt(" %s=%s(%s)", hex8(u->words[i].addr).c_str(), hex8(cur[i]).c_str(), patches::siteStateName(st[i]));
        c.log(std::string(u->label) + ":" + words);
    }
    const uint32_t leaf = c.rt().u32(patches::kCounterLeaf);
    c.log(fmt("counter leaf %s = %s", hex8(patches::kCounterLeaf).c_str(), hex8(leaf).c_str()));
    const bool ok = ch.providerVtable == patches::addr::ENTITLE_VTABLE && ch.rootOk;
    c.log(ok ? "verdict: entitlement chain resolves (root and provider vtable as in the port map)"
             : "verdict: entitlement chain not resolvable here (load into orbit and run again)");
}

 
 
 

}   

BR_FEATURES(patches_group) {
    const Page C = Page::Console;
    auto plain = [&](const char* id, const char* tab, const char* section, const char* label, const char* help, Feature f,
                     const char* badge = "") {
        r.toggle({.id = id, .page = C, .tab = tab, .section = section, .label = label, .help = help,
                  .set = [f](Ctx& c, bool on) { setPlain(c, f, on); }, .badge = badge});
    };

     
    plain("patch.instant_respawn", "Player", kRespawn, "Instant Respawn Timer (RTM Offset Test)",
          "Uses the RTM's BLES 01.24 site at 0x0071C348. Blackrock's separate multi-site Auto Respawn control remains unchanged.",
          Feature::InstantRespawn);
    r.toggle({.id = "patch.fast_respawn", .page = C, .tab = "Player", .section = kRespawn, .label = "Auto Respawn",
              .help = "Brings you back the frame you die (fast respawn): removes the respawn timers and checks, then calls respawn() every "
                      "frame while you are dead. The player handle is latched while you are alive, so it also works when switched on "
                      "after you died. The respawn argument still needs one live check.",
              .set = setFastRespawn, .badge = kBadgeNeedsCheck});
    r.section({.id = "patches.respawn_options", .page = C, .tab = "Player", .section = kRespawn, .draw = drawRespawnOptions,
               .onDisconnect = resetOnDisconnect});
    plain("patch.immune_bypass", "Player", kRules, "Immune Bypass",
          "Damage reaches targets the game makes immune: the static-actor damage factor falls back to 0 and the Warpriest damage "
          "multiplier is pinned to 1.0.",
          Feature::ImmuneBypass);
    plain("patch.joining_allies", "Player", kRules, "Joining Allies",
          "Skips the activity-quarantine game state (its tick returns at once), so fireteam members can join you where the game would "
          "hold them back.",
          Feature::JoiningAllies);

     
    plain("patch.stop_bullets", "Weapons", kProjectiles, "Stop Bullets (RTM Offset Test)",
          "Uses the RTM's BLES 01.24 address and 1.0/0.0 values at 0x0051E7E0. Byte-level verification does not establish the in-game effect.",
          Feature::StopBullets);
    plain("patch.bullet_walls", "Weapons", kProjectiles, "Bullets Through Walls",
          "Projectile traces use collision layer 0x11 instead of 0xA, so shots pass through world geometry.", Feature::BulletWalls);

     
    plain("patch.third_person", "Camera", kCameraMode, "Third Person",
          "Requests the chase camera (mode 0) instead of first person (mode 4) for the local player.", Feature::ThirdPerson);
    r.toggle({.id = "patch.fov", .page = C, .tab = "Camera", .section = kFov, .label = "FOV Override",
              .help = "Points the plain FOV getter at the override-aware one and writes the angle below every frame. Off puts the "
                      "game's own value back.",
              .set = [](Ctx& c, bool on) {
                  if (on) requireFeature(c, Feature::Fov);
                  run(c, [&](patches::Controller& ctl) { ctl.setFov(c.rt(), on); });
              }});
    r.value({.id = "patch.fov.degrees", .page = C, .tab = "Camera", .section = kFov, .label = "Field of View",
             .help = "World camera FOV in degrees while FOV override is on. The game ignores angles above 150.",
             .min = patches::kFovMin, .max = patches::kFovMax, .def = patches::kFovDefault, .step = 1, .format = "%.0f\xC2\xB0",
             .set = [](Ctx& c, float v) { run(c, [&](patches::Controller& ctl) { ctl.setFovDegrees(c.rt(), v); }); },
             .parent = "patch.fov"});
    r.toggle({.id = "patch.viewmodel_fov", .page = C, .tab = "Camera", .section = kViewmodel, .label = "Viewmodel FOV",
              .help = "Weapons take their hip-fire FOV from the settings block, and the angle below is written there every frame. "
                      "Swap weapons to see a change.",
              .set = [](Ctx& c, bool on) {
                  if (on) requireFeature(c, Feature::ViewmodelFov);
                  run(c, [&](patches::Controller& ctl) { ctl.setViewmodel(c.rt(), on); });
              }});
    r.value({.id = "patch.viewmodel_fov.degrees", .page = C, .tab = "Camera", .section = kViewmodel, .label = "Angle",
             .help = "Hip-fire viewmodel FOV in degrees while Viewmodel FOV is on (the game's default is 77).",
             .min = patches::kViewmodelMin, .max = patches::kViewmodelMax, .def = patches::kViewmodelDefault, .step = 1,
             .format = "%.0f\xC2\xB0",
             .set = [](Ctx& c, float v) { run(c, [&](patches::Controller& ctl) { ctl.setViewmodelDegrees(c.rt(), v); }); },
             .parent = "patch.viewmodel_fov"});

     
    r.toggle({.id = "patch.game_speed", .page = C, .tab = "World", .section = kSpeed, .label = "Game Speed",
              .help = "Runs the simulation at the multiplier below, re-asserted every frame. Pauses (0.0) are left alone; a load that "
                      "resets it to 1.0 is overridden again. Off returns to 1.0.",
              .set = [](Ctx& c, bool on) {
                  if (on) c.require(patches::kGameSpeed, "Game speed");
                  run(c, [&](patches::Controller& ctl) { ctl.setSpeed(c.rt(), on); });
              },
              .query = recoverOnConnect});
    r.value({.id = "patch.game_speed.multiplier", .page = C, .tab = "World", .section = kSpeed, .label = "Speed Value",
             .help = "1.0 is normal; higher is faster (0.1 to 7.0). Used while Game speed is on.",
             .min = patches::kSpeedMin, .max = patches::kSpeedMax, .def = patches::kSpeedDefault, .step = 0.05f, .format = "%.2fx",
             .set = [](Ctx& c, float v) { run(c, [&](patches::Controller& ctl) { ctl.setSpeedValue(c.rt(), v); }); },
             .parent = "patch.game_speed"});
    r.toggle({.id = "patch.no_inactivity_kick", .page = C, .tab = "World", .section = kQol, .label = "No Inactivity Kick",
              .help = "Clears the sign-in listener's activity flag every frame, so the inactivity timer always reads 0 and you are never "
                      "kicked for idling. Off just stops clearing it.",
              .set = [](Ctx& c, bool on) { run(c, [&](patches::Controller& ctl) { ctl.setNoInactivity(c.rt(), on); }); }});
    plain("patch.instant_dismantle", "World", kQol, "Instant Dismantle",
          "Every hold-to-confirm inventory action (dismantle and the other hold prompts) completes the moment you press it.",
          Feature::InstantDismantle);
    plain("patch.sparrow_energy", "World", kQol, "Sparrow Energy Never Drains",
          "Uses the same 0x0050A700 site and values as the RTM's Unlimited Sparrow preset.", Feature::SparrowEnergy);
    plain("patch.sparrow_anywhere", "World", kQol, "Summon Sparrow Anywhere",
          "The 'cannot summon in this location' verdict becomes 'allowed'. Other refusals (no free slot, invalid index) still apply.",
          Feature::SparrowAnywhere);
    plain("patch.world_transition", "World", kGates, "World-Transition Bypass",
          "Skips the 'World-transition requires DLC' and '...requires permissions' aborts of the activity-launch state (state 33).",
          Feature::WorldTransition);
    r.toggle({.id = "patch.internal_entitle", .page = C, .tab = "World", .section = kGates, .label = "Internal Mode and Entitlement",
              .help = "The developer / entitlement levers from internal_mode.py and entitle.py, with the levers picked below. Untested in "
                      "game on both consoles: a research tool.",
              .set = setEntitle, .badge = kBadgeDev});
    r.section({.id = "patches.entitle_levers", .page = C, .tab = "World", .section = kGates, .draw = drawEntitleLevers});
    plain("patch.deafen", "World", kNetwork, "Deafen",
          "The client stops noticing a dead activity host: BAP failures no longer drop the connection state, the ASTAT demotions are "
          "skipped and bubble authority reads true. Arm it in orbit, before the world loads. Also hides a real backend drop: "
          "experiment only.",
          Feature::Deafen, kBadgeDev);
    plain("patch.hotload_suppress", "World", kNetwork, "Hot-Load Suppression",
          "Removes the 'tag loaded' notification call (0x148D00) so a world-tag-loader load into a live world does not run the "
          "reclaimer on a half-loaded definition. Turn it on just before a World load, off right after.",
          Feature::HotloadSuppress, kBadgeDev);
    plain("rtm.offset.no_recoil", "RTM Test", "RTM 01.24 Main Cheat Offsets", "No Recoil",
          "Applies the RTM's three fixed sites with exact original-value checks.", Feature::RtmNoRecoil, "RTM 01.24");
    plain("rtm.offset.unlimited_abilities", "RTM Test", "RTM 01.24 Main Cheat Offsets", "Unlimited Abilities",
          "Applies the RTM's three fixed instruction sites with exact original-value checks.", Feature::RtmUnlimitedAbilities, "RTM 01.24");
    plain("rtm.offset.unlimited_ammo", "RTM Test", "RTM 01.24 Main Cheat Offsets", "Unlimited Ammo",
          "Applies the RTM's two instruction words and five-word ammo sequence with exact original-value checks.", Feature::RtmUnlimitedAmmo,
          "RTM 01.24");
    plain("rtm.offset.no_collision", "RTM Test", "RTM 01.24 Main Cheat Offsets", "No Collision",
          "Applies the RTM's fixed value at 0x014FC690 after an exact original-value check.", Feature::RtmNoCollision, "RTM 01.24");
    plain("rtm.offset.one_hit_kill", "RTM Test", "RTM 01.24 Main Cheat Offsets", "One-Hit Kill (Main RTM Variant)",
          "Uses the main RTM 0x447A halfword variant, guarded and restored as its enclosing aligned word.", Feature::RtmOneHitKill,
          "RTM 01.24");
    plain("rtm.offset.rapid_fire", "RTM Test", "RTM 01.24 Main Cheat Offsets", "Rapid Fire",
          "Applies the RTM's fixed site with an exact original-value check.", Feature::RtmRapidFire, "RTM 01.24");
    plain("rtm.offset.no_target", "RTM Test", "RTM 01.24 Main Cheat Offsets", "No Target",
          "Applies both RTM sites with exact original-value checks.", Feature::RtmNoTarget, "RTM 01.24");
    plain("rtm.offset.god_mode", "RTM Test", "RTM 01.24 Main Cheat Offsets", "God Mode",
          "Applies the RTM's fixed site with an exact original-value check.", Feature::RtmGodMode, "RTM 01.24");
    plain("rtm.offset.no_gravity", "RTM Test", "RTM 01.24 Main Cheat Offsets", "No Gravity",
          "Applies the RTM's fixed site with an exact original-value check.", Feature::RtmNoGravity, "RTM 01.24");
    plain("rtm.offset.no_pushbacks", "RTM Test", "RTM 01.24 Main Cheat Offsets", "No Pushbacks",
          "Applies all three RTM vector words with exact original-value checks.", Feature::RtmNoPushbacks, "RTM 01.24");
    plain("rtm.offset.no_floors", "RTM Test", "RTM 01.24 Main Cheat Offsets", "No Floors",
          "Applies the RTM's fixed site with an exact original-value check.", Feature::RtmNoFloors, "RTM 01.24");
    plain("rtm.offset.static_actors", "RTM Test", "RTM 01.24 Main Cheat Offsets", "Static Actors",
          "Applies the RTM's fixed site with an exact original-value check.", Feature::RtmStaticActors, "RTM 01.24");
    plain("rtm.offset.variable_buff", "RTM Test", "RTM 01.24 Main Cheat Offsets", "Variable Buff",
          "Uses the source-labelled 0x0005E614 preset. Its gameplay effect remains unverified.", Feature::RtmVariableBuff,
          "RTM 01.24");
    r.section({.id = "patches.performance", .page = C, .tab = "World", .section = kPerformance, .order = 55, .draw = drawPerformance,
               .onDisconnect = resetOnDisconnect});

     
    auto probe = [&](const char* id, const char* label, const char* help, void (*fn)(Ctx&), const char* badge = "") {
        r.action({.id = id, .page = Page::Testing, .tab = kPortChecksTab, .section = kChecks, .label = label, .help = help, .run = fn,
                  .badge = badge});
    };
    probe("patches.check.respawn", "Auto Respawn: Respawn Argument",
          "Logs the player-array row +0x4C (port map) and +0x54, local_player_handle(0) and, with Auto respawn on, the respawn calls "
          "per 0.6 s. Run it while dead with Auto respawn on: expected about 18 calls and you respawn; then the argument is confirmed.",
          probeRespawn, kBadgeNeedsCheck);
    probe("patches.check.sites", "Patch Sites Against the Port Map",
          "Reads every instruction the patch switches write (33 words) and the instant-dismantle window. Expected: every site "
          "'original' (or patched by us while its switch is on).",
          probeSites);
    probe("patches.check.values", "Per-Frame Targets: FOV, Speed, Inactivity, Viewmodel",
          "Reads the FOV global (expected -1.0), the game-speed multiplier (1.0, 0.0 while paused), the sign-in listener (vtable "
          "013FB548) and the viewmodel settings (about 1.344 rad).",
          probeValues);
    probe("patches.check.entitle", "Internal Mode / Entitlement Chain",
          "Reads the access root and its valid bit, the provider object (vtable 01411170), the override byte, the fly-in suppress "
          "byte and the state of every entitlement patch site (like entitle.py show).",
          probeEntitle);

     
    styleSection(kRespawn, {.icon = icon::Restore});
    styleSection(kRules, {.icon = icon::Shield});
    styleSection(kProjectiles, {.icon = icon::Console});
    styleSection(kCameraMode, {.icon = icon::View});
    styleSection(kFov, {.icon = icon::View});
    styleSection(kViewmodel, {.icon = icon::Console});
    styleSection(kSpeed, {.icon = icon::History});
    styleSection(kQol, {.icon = icon::Lightbulb});
    styleSection(kGates, {.icon = icon::Link});
    styleSection(kNetwork, {.icon = icon::Plug});
    styleSection(kPerformance, {.icon = icon::Testing});
    addPaletteProvider(paletteEntries);
}

}   
