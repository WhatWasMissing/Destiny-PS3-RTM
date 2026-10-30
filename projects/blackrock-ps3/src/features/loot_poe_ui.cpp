 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "app/engine.h"
#include "app/registry.h"
#include "app/settings.h"
#include "core/error.h"
#include "core/profile.h"
#include "core/runtime.h"
#include "features/loot_common.h"
#include "game/loot_addr.h"
#include "game/loot_afk.h"
#include "game/loot_poe.h"
#include "ui/widgets.h"

namespace brps3::lootui {

using namespace ui;
using namespace loot;

namespace {

constexpr const char* kSection = "Prison of Elders chest loop";
constexpr const char* kAfkSection = "AFK keepalive";
constexpr const char* kStartJob = "PoE: start chest loop";
constexpr const char* kCheckJob = "PoE: check the treasure room";
constexpr const char* kIntervalJob = "PoE: change the delay";
constexpr const char* kLoop = "PoE loop";
constexpr const char* kAfkLoop = "AFK keepalive";
constexpr const char* kTaskName = "loot.poe";
constexpr const char* kStartId = "loot.poe.start";
constexpr const char* kStopId = "loot.poe.stop";
constexpr const char* kCheckId = "loot.poe.check";
constexpr const char* kAfkId = "loot.afk";

uint64_t nowMs() { return uint64_t(now() * 1000.0); }

enum class Phase { Off, Checking, Starting, Running, Holding, Stopping, Stopped, Faulted };
bool live(Phase p) { return p == Phase::Running || p == Phase::Holding; }    

struct Options {
    bool mainOnly = false;
    double interval = 0;
    int limit = 0;
    double timeout = 5;
    bool keepalive = true;
    bool afk = true;
    int engine = 0;                      
};

struct ChestView {
    poe::Family family = poe::Family::Main;
    uint32_t address = 0, owner = 0, sequence = 0;
    int32_t lifetime = 0;
    uint32_t cycles = 0, refills = 0;
    std::string label;
};

struct State {
    std::mutex m;
    Options opt;
    bool optLoaded = false;
    Phase phase = Phase::Off;
    std::string message;
    bool cleanupVerified = true;
     
    bool checked = false, checking = false;
    std::string checkError;
    double checkedAt = 0;
    uint32_t worldState = 0;
    bool codeOk = false;
    uint32_t map = 0, nodes = 0;
    std::vector<ChestView> found;
    std::vector<std::string> warnings;
     
    std::vector<ChestView> chests;
    uint32_t cycles = 0, writes = 0, refills = 0, current = 0;
    bool pending = false;
    int engine = 0;
    double startedAt = 0, stoppedAt = 0;
    std::deque<std::pair<double, std::string>> events;
    std::atomic<bool> stop{false};
};

State& st() {
    static State s;
    return s;
}

 
struct Run {
    std::mutex m;
    bool active = false, hooked = false;
    poe::Context ctx;
    std::vector<poe::Chest> chests;
    poe::LoopConfig cfg;
    int engine = 0;
    uint32_t block = 0;
    std::unique_ptr<poe::PcLoop> pc;
    bool afkHeld = false;
    bool holding = false;                
    uint32_t refillBase = 0;             
    size_t deepCursor = 0;
    double nextDeep = 0;
};

void event(const std::string& line, bool ok = true) {
    feed("PoE", line, ok);
    State& S = st();
    std::lock_guard<std::mutex> lock(S.m);
    S.events.push_front({now(), line});
    while (S.events.size() > 40) S.events.pop_back();
}

const char* engineName(int e) { return e == 0 ? "every frame" : "PC-driven"; }

 

void loadOptions(Settings& s, Options& o) {
    o.mainOnly = s.flag("loot.poe.main_only", false);
    o.interval = std::clamp(double(s.number("loot.poe.interval", 0.0f)), 0.0, 3600.0);
    o.limit = std::clamp(s.integer("loot.poe.limit", 0), 0, 1000000);
    o.timeout = std::clamp(double(s.number("loot.poe.timeout", 5.0f)), 0.1, 10.0);
    o.keepalive = s.flag("loot.poe.keepalive", true);
    o.afk = s.flag("loot.poe.afk", true);
    o.engine = std::clamp(s.integer("loot.poe.engine", 0), 0, 1);
}

void saveOptions(Settings& s, const Options& o) {
    s.set(std::string("loot.poe.main_only"), o.mainOnly);
    s.set(std::string("loot.poe.interval"), float(o.interval));
    s.set(std::string("loot.poe.limit"), o.limit);
    s.set(std::string("loot.poe.timeout"), float(o.timeout));
    s.set(std::string("loot.poe.keepalive"), o.keepalive);
    s.set(std::string("loot.poe.afk"), o.afk);
    s.set(std::string("loot.poe.engine"), o.engine);
}

Options options(Settings& s) {
    State& S = st();
    std::lock_guard<std::mutex> lock(S.m);
    if (!S.optLoaded) {
        loadOptions(s, S.opt);
        S.optLoaded = true;
    }
    return S.opt;
}

struct Pins {
    uint32_t map = 0, mainSeq = 0, sideSeq = 0;
};
uint32_t hexSetting(Settings& s, const char* key) {
    try {
        const std::string v = s.str(key, "");
        return v.empty() ? 0 : world::parseHex(v);
    } catch (const std::exception&) {
        return 0;
    }
}
Pins pins(Settings& s) { return {hexSetting(s, "loot.poe.map"), hexSetting(s, "loot.poe.seq_main"), hexSetting(s, "loot.poe.seq_side")}; }

std::vector<ChestView> views(const std::vector<poe::Chest>& chests) {
    std::vector<ChestView> v;
    int side = 0;
    for (const poe::Chest& c : chests) {
        ChestView cv;
        cv.family = c.family;
        cv.address = c.address;
        cv.owner = c.owner;
        cv.sequence = c.sequence;
        cv.lifetime = c.timers.lifetime;
        cv.label = c.family == poe::Family::Main ? "Main Chest" : "Side Chest " + std::to_string(++side);
        v.push_back(cv);
    }
    return v;
}

void storeCheck(const poe::Discovery& d) {
    State& S = st();
    std::lock_guard<std::mutex> lock(S.m);
    S.checked = true;
    S.checkError.clear();
    S.checkedAt = now();
    S.worldState = d.ctx.worldState;
    S.map = d.ctx.map;
    S.nodes = uint32_t(d.nodes.size());
    S.found = views(d.chests);
    S.warnings = d.warnings;
}

void checkFailed(const std::string& why, bool codeOk) {
    State& S = st();
    std::lock_guard<std::mutex> lock(S.m);
    S.checked = true;
    S.checkError = why;
    S.checkedAt = now();
    S.codeOk = codeOk;
    S.found.clear();
}

 

poe::Discovery discoverRoom(Ctx& c, bool& codeOk) {
    codeOk = false;
    c.require(kPoeFingerprints, "PoE chest loop (native chest code)");
    codeOk = true;
    {
        std::lock_guard<std::mutex> lock(st().m);
        st().codeOk = true;
    }
    RuntimeConsole con(c.rt());
    Resolver res(con);
    return poe::discover(con, res, [&c] { return c.cancelled() || st().stop.load(); },
                         [](const std::string& p) {
                             std::lock_guard<std::mutex> lock(st().m);
                             st().message = p;
                         });
}

void checkJob(Ctx& c) {
    {
        std::lock_guard<std::mutex> lock(st().m);
        st().checking = true;
    }
    struct Done {
        ~Done() {
            std::lock_guard<std::mutex> lock(st().m);
            st().checking = false;
        }
    } done;
    bool codeOk = false;
    try {
        const poe::Discovery d = discoverRoom(c, codeOk);
        storeCheck(d);
        c.log(fmt("[poe] check: map %s, %zu list nodes, %u main + %u side chests opened", hex(d.ctx.map).c_str(), d.nodes.size(), d.mains(),
                  d.sides()));
        for (const std::string& w : d.warnings) c.log("[poe] " + w);
    } catch (const std::exception& e) {
        checkFailed(e.what(), codeOk);
        throw;
    }
}

 
 
void cleanupTask(Ctx& c, Run& R, RuntimeConsole& con, std::vector<std::string>& problems) {
    if (!R.block) return;
    try {
        con.w32(R.block + poe::ctl::ENABLED, 0, false);
        c.rt().waitFrames(2, 1500);
        const poe::TaskState t = poe::decodeControl(c.rt().read(R.block, poe::ctl::SIZE));
        if (t.pending && t.current < R.chests.size()) {
            const poe::Chest& ch = R.chests[t.current];
            Resolver res(con);
            const poe::Timers tm = poe::validate(con, res, R.ctx, ch, false);
            if (tm.looted == 0) con.w8(ch.address + chest::LOOTED, 1, false);
            if (con.u8(ch.address + chest::LOOTED) != 1) problems.push_back("the pending callback byte did not read back as 1");
        }
    } catch (const std::exception& e) {
        problems.push_back(std::string("callback byte cleanup not verified: ") + e.what() + ". No stale-address write attempted");
    }
    try {
        c.rt().removeTask(kTaskName);
        c.rt().freeLater(R.block);
    } catch (const std::exception& e) {
        problems.push_back(std::string("task removal: ") + e.what());
    }
    R.block = 0;
}

 
 
void finish(Ctx& c, Run& R, const std::string& why, Phase phase, std::vector<std::string> problems = {}) {
    RuntimeConsole con(c.rt());
    {
        std::lock_guard<std::mutex> lock(R.m);
        if (R.active) {
            if (R.engine == 0 && R.block) cleanupTask(c, R, con, problems);
            if (R.pc) {
                std::string w;
                if (!R.pc->restore(con, &w)) problems.push_back(w);
                R.pc.reset();
            }
            if (R.afkHeld) {
                std::string w;
                AfkShared& A = afkShared();
                std::lock_guard<std::mutex> alock(A.m);
                if (!A.keeper.release(con, "poe", &w)) problems.push_back(w);
                A.publish();
                R.afkHeld = false;
            }
            R.holding = false;
            R.active = false;
        }
    }
    try {
        c.stopLoop(kLoop);
    } catch (...) {
    }
    State& S = st();
    std::string msg = why;
    if (!problems.empty()) {
        msg += ". Cleanup NOT verified: ";
        for (size_t i = 0; i < problems.size(); ++i) msg += (i ? "; " : "") + problems[i];
    } else {
        msg += ". Cleanup verified.";
    }
    {
        std::lock_guard<std::mutex> lock(S.m);
        S.phase = phase;
        S.message = msg;
        S.cleanupVerified = problems.empty();
        S.stoppedAt = now();
        S.pending = false;
    }
    S.stop = false;
    event(msg, problems.empty() && phase != Phase::Faulted);
    c.log("[poe] " + msg);
}

void monitor(Ctx& c);

 
 
bool replayFault(uint32_t fault) {
    return fault == poe::kFaultByte || fault == poe::kFaultTimeout || fault == poe::kFaultNotSettled;
}

 
 
 
void hold(Ctx& c, Run& R, const std::string& reason) {
    State& S = st();
    std::vector<std::string> problems;
    RuntimeConsole con(c.rt());
    {
        std::lock_guard<std::mutex> lock(R.m);
        if (!R.active) return;
        if (R.engine == 0) {
            cleanupTask(c, R, con, problems);
            if (problems.empty()) {
                try {
                     
                    std::vector<poe::Chest> fresh = R.chests;
                    Resolver res(con);
                    for (poe::Chest& ch : fresh) ch.timers = poe::validate(con, res, R.ctx, ch, false);
                    R.pc = std::make_unique<poe::PcLoop>(R.ctx, fresh, R.cfg, nowMs());
                    std::lock_guard<std::mutex> slock(S.m);
                    R.refillBase = S.refills;
                } catch (const std::exception& e) {
                    problems.push_back(std::string("the chests no longer validate: ") + e.what());
                }
            }
        } else if (R.pc) {
            std::string w;
            if (!R.pc->restore(con, &w)) problems.push_back(w);
            R.refillBase = 0;
        }
        if (problems.empty()) R.holding = true;
    }
    if (!problems.empty()) {
        finish(c, R, "Stopped: " + reason, Phase::Faulted, problems);
        return;
    }
    const std::string msg = "Replay paused: " + reason + ". The pending callback byte is restored; the chest timers and the AFK keepalive stay on "
                                                         "until you press Stop.";
    {
        std::lock_guard<std::mutex> lock(S.m);
        S.phase = Phase::Holding;
        S.message = msg;
        S.pending = false;
        S.cleanupVerified = true;
    }
    event(msg, false);
    c.log("[poe] " + msg);
    c.startLoop(kLoop, 200, monitor);    
}

void monitor(Ctx& c) {
    Run& R = c.state<Run>("loot.poe.run");
    State& S = st();
    bool active, afkHeld, holding;
    int engine;
    {
        std::lock_guard<std::mutex> lock(R.m);
        active = R.active;
        engine = R.engine;
        afkHeld = R.afkHeld;
        holding = R.holding;
    }
    if (!active) {
        c.stopLoop(kLoop);
        return;
    }
    if (S.stop.load()) {
        finish(c, R, holding ? "Stopped (the replay was paused)" : "Stopped", Phase::Stopped);
        return;
    }
    RuntimeConsole con(c.rt());
    std::string end;                     
    std::string pause;                   
    bool stepping = false;               
    Phase endPhase = Phase::Stopped;
    try {
        if (afkHeld) {
            AfkShared& A = afkShared();
            std::lock_guard<std::mutex> lock(A.m);
            const std::string line = A.keeper.poll(con, nowMs());
            A.publish(line);
            if (!line.empty()) event(line);
        }
        std::lock_guard<std::mutex> lock(R.m);
        if (holding) {
            const std::string line = R.pc->keepalive(con, nowMs());
            const poe::LoopStats& s = R.pc->stats();
            {
                std::lock_guard<std::mutex> slock(S.m);
                S.refills = R.refillBase + s.refills;
                for (size_t i = 0; i < S.chests.size() && i < s.lifetimes.size(); ++i) S.chests[i].lifetime = s.lifetimes[i];
            }
            if (!line.empty()) event(line);
        } else if (engine == 0) {
            const poe::TaskState t = poe::decodeControl(c.rt().read(R.block, poe::ctl::SIZE));
            if (!t.valid) throw Error("the control block was overwritten");
            {
                std::lock_guard<std::mutex> slock(S.m);
                const uint32_t before = S.refills;
                S.cycles = t.cycles;
                S.writes = t.writes;
                S.refills = t.refills;
                S.current = t.current;
                S.pending = t.pending;
                for (size_t i = 0; i < S.chests.size() && i < t.chests.size(); ++i) {
                    S.chests[i].lifetime = t.chests[i].lifetime;
                    S.chests[i].cycles = t.chests[i].cycles;
                    S.chests[i].refills = t.chests[i].refills;
                }
                if (t.refills != before) S.events.push_front({now(), fmt("Chest timer refilled (%u so far)", t.refills)});
            }
            if (t.status == poe::TaskStatus::Faulted) {
                if (replayFault(t.fault) && R.cfg.keepalive && !S.stop.load()) {
                    pause = poe::faultText(t.fault);
                } else {
                    end = std::string("Stopped: ") + poe::faultText(t.fault);
                    endPhase = Phase::Faulted;
                }
            } else if (t.status == poe::TaskStatus::Done) {
                end = "Callback limit reached";
            } else if (now() >= R.nextDeep && !R.chests.empty()) {
                Resolver res(con);
                poe::validate(con, res, R.ctx, R.chests[R.deepCursor], true);
                R.deepCursor = (R.deepCursor + 1) % R.chests.size();
                R.nextDeep = now() + 5.0 / double(R.chests.size());
            }
        } else {
            stepping = true;
            const std::string line = R.pc->step(con, nowMs());
            stepping = false;
            const poe::LoopStats& s = R.pc->stats();
            {
                std::lock_guard<std::mutex> slock(S.m);
                S.cycles = s.cycles;
                S.writes = s.writes;
                S.refills = s.refills;
                S.current = s.current;
                S.pending = s.pending;
                for (size_t i = 0; i < S.chests.size() && i < s.lifetimes.size(); ++i) {
                    S.chests[i].lifetime = s.lifetimes[i];
                    S.chests[i].cycles = s.chestCycles[i];
                    S.chests[i].refills = s.chestRefills[i];
                }
            }
            if (line.find("refilled") != std::string::npos) event(line);
            if (s.done) end = "Callback limit reached";
        }
    } catch (const std::exception& e) {
        bool replay = false;
        {
            std::lock_guard<std::mutex> lock(R.m);
            replay = stepping && !R.holding && R.engine == 1 && R.pc && R.pc->failedInReplay() && R.cfg.keepalive;
        }
        if (replay && !S.stop.load()) {
            pause = e.what();
        } else {
            end = std::string(holding ? "Stopped: keepalive failed during the pause: " : "Stopped: ") + e.what();
            endPhase = Phase::Faulted;
        }
    }
    if (!end.empty()) finish(c, R, end, endPhase);
    else if (!pause.empty()) hold(c, R, pause);
}

void stopAll(Ctx& c) {
    Run& R = c.state<Run>("loot.poe.run");
    bool active;
    {
        std::lock_guard<std::mutex> lock(R.m);
        active = R.active;
    }
    if (active) finish(c, R, "Stopped", Phase::Stopped);
}

void startJob(Ctx& c) {
    State& S = st();
    Run& R = c.state<Run>("loot.poe.run");
    {
        std::lock_guard<std::mutex> lock(R.m);
        if (R.active) throw Error("The PoE loop is already running");
    }
    const Options o = options(c.settings());
    poe::LoopConfig cfg;
    cfg.mainOnly = o.mainOnly;
    cfg.intervalS = o.interval;
    cfg.cycleLimit = uint32_t(o.limit);
    cfg.timeoutS = o.timeout;
    cfg.keepalive = o.keepalive;
    std::string why;
    if (!poe::validConfig(cfg, &why)) throw Error(why);
    {
         
        std::lock_guard<std::mutex> lock(S.m);
        if (!S.cleanupVerified)
            throw Error("The last run's cleanup was not verified: check the log, then press \"Checked\" on Loot > PoE before starting again");
    }
    S.stop = false;
    {
        std::lock_guard<std::mutex> lock(S.m);
        S.phase = Phase::Starting;
        S.message = "Checking the native chest code...";
        S.cycles = S.writes = S.refills = S.current = 0;
        S.pending = false;
        S.engine = o.engine;
    }
    auto fail = [&](const std::string& w) {
        std::lock_guard<std::mutex> lock(S.m);
        S.phase = Phase::Off;
        S.message = w;
        S.cleanupVerified = true;
    };
    bool codeOk = false, stored = false;
    poe::Discovery d;
    std::vector<poe::Chest> chests;
    try {
        d = discoverRoom(c, codeOk);
        storeCheck(d);
        stored = true;
        const Pins p = pins(c.settings());
        if (p.map && d.ctx.map != p.map)
            throw Error("This is not the pinned PoE treasure room (map " + hex(d.ctx.map) + ", pinned " + hex(p.map) + ")");
        chests = poe::select(d, o.mainOnly);
        for (const poe::Chest& ch : chests) {
            const uint32_t want = ch.family == poe::Family::Main ? p.mainSeq : p.sideSeq;
            if (want && ch.sequence != want)
                throw Error(ch.label() + " definition tag " + hex(ch.sequence) + " differs from the pinned " + hex(want));
        }
    } catch (const std::exception& e) {
        if (!stored) checkFailed(e.what(), codeOk);
        fail(e.what());
        throw;
    }
    if (S.stop.load()) {
        fail("Stopped before starting");
        return;
    }
    RuntimeConsole con(c.rt());
    std::unique_lock<std::mutex> lock(R.m);
    if (!R.hooked) {
        R.hooked = true;
        c.onDisconnect([](Ctx& dc) {
            try {
                stopAll(dc);
            } catch (...) {
            }
        });
    }
    c.onPanic("loot.poe", [](Ctx& pc) { stopAll(pc); });
    R.ctx = d.ctx;
    R.chests = chests;
    R.cfg = cfg;
    R.engine = o.engine;
    R.deepCursor = 0;
    R.nextDeep = now() + 2.0;
    try {
        if (o.afk) {
            c.require(kAfkFingerprints, "AFK keepalive");
            AfkShared& A = afkShared();
            std::lock_guard<std::mutex> alock(A.m);
            const std::string line = A.keeper.acquire(con, afkMethod(c.settings()), "poe", nowMs());
            A.publish();
            R.afkHeld = true;
            event("AFK: " + line);
        }
        if (o.engine == 0) {
            c.rt().removeTask(kTaskName);    
            const std::vector<uint8_t> blk = poe::encodeControl(d.ctx, chests, cfg, false);
            R.block = c.rt().alloc(poe::ctl::SIZE, blk.data(), blk.size());
             
            const uint32_t taskSize = uint32_t((poe::taskProgram(profile::CAVE + 0x400).size() + 15) & ~size_t(15));
            c.rt().addTask(kTaskName, [](uint32_t base) { return poe::taskProgram(base); }, R.block, std::min(taskSize, poe::kTaskSize));
            con.w32(R.block + poe::ctl::STATUS, uint32_t(poe::TaskStatus::Running));
            con.w32(R.block + poe::ctl::ENABLED, 1);
        } else {
            R.pc = std::make_unique<poe::PcLoop>(d.ctx, chests, cfg, nowMs());
        }
        R.active = true;
    } catch (const std::exception& e) {
         
        if (R.block) {
            try {
                con.w32(R.block + poe::ctl::ENABLED, 0, false);
                c.rt().removeTask(kTaskName);
                c.rt().freeLater(R.block);
            } catch (...) {
            }
            R.block = 0;
        }
        if (R.afkHeld) {
            AfkShared& A = afkShared();
            std::lock_guard<std::mutex> alock(A.m);
            A.keeper.release(con, "poe");
            A.publish();
            R.afkHeld = false;
        }
        fail(e.what());
        throw;
    }
    lock.unlock();    
    const std::string started = fmt("Running %s on %zu chest%s", engineName(o.engine), chests.size(), chests.size() == 1 ? "" : "s");
    {
        std::lock_guard<std::mutex> slock(S.m);
        S.phase = Phase::Running;
        S.startedAt = now();
        S.chests = views(chests);
        S.message = started;
        S.cleanupVerified = true;
    }
    event(started);
    c.log("[poe] " + started);
    c.startLoop(kLoop, o.engine == 0 ? 200 : 1, monitor);
}

void intervalJob(Ctx& c, double seconds) {
    Run& R = c.state<Run>("loot.poe.run");
    std::lock_guard<std::mutex> lock(R.m);
    if (!R.active) return;
    R.cfg.intervalS = seconds;
    if (R.engine == 0 && R.block) RuntimeConsole(c.rt()).w32(R.block + poe::ctl::INTERVAL, poe::framesFor(seconds), false);
    if (R.pc) R.pc->setInterval(seconds);
}

 

void afkSet(Ctx& c, bool on) {
    RuntimeConsole con(c.rt());
    AfkShared& A = afkShared();
    if (on) {
        c.require(kAfkFingerprints, "AFK keepalive");
        std::string line;
        {
            std::lock_guard<std::mutex> lock(A.m);
            line = A.keeper.acquire(con, afkMethod(c.settings()), "switch", nowMs());
            A.publish(line);
        }
        c.log("[afk] " + line);
        feed("AFK", "On: " + line);
        c.startLoop(kAfkLoop, 1000, [](Ctx& lc) {
            RuntimeConsole lcon(lc.rt());
            AfkShared& LA = afkShared();
            std::lock_guard<std::mutex> lock(LA.m);
            if (!LA.keeper.heldBy("switch")) {
                lc.stopLoop(kAfkLoop);
                return;
            }
            const std::string l = LA.keeper.poll(lcon, nowMs());
            LA.publish(l);
            if (!l.empty()) {
                lc.log("[afk] " + l);
                feed("AFK", l);
            }
        });
        struct Hooked {
            bool done = false;
        };
        Hooked& h = c.state<Hooked>("loot.afk.hooked");
        if (!h.done) {
            h.done = true;
            c.onDisconnect([](Ctx& dc) {
                AfkShared& DA = afkShared();
                std::lock_guard<std::mutex> lock(DA.m);
                RuntimeConsole dcon(dc.rt());
                try {
                    DA.keeper.release(dcon, "switch");
                    DA.keeper.release(dcon, "poe");
                } catch (...) {
                }
                DA.keeper.forget();
                DA.publish();
            });
        }
    } else {
        c.stopLoop(kAfkLoop);
        std::string why;
        std::lock_guard<std::mutex> lock(A.m);
        const bool ok = !A.keeper.heldBy("switch") || A.keeper.release(con, "switch", &why);
        A.publish();
        if (!ok) {
            feed("AFK", why, false);
            throw Error(why);
        }
        A.clearEvent();
        feed("AFK", A.keeper.held() ? "Off for the switch (the PoE loop still holds it)" : "Off: the game's limit is back");
    }
}

 

void seedDemo() {
    static bool seeded = false;
    if (seeded || !demo()) return;
    seeded = true;
    State& S = st();
    std::lock_guard<std::mutex> lock(S.m);
    S.phase = Phase::Running;
    S.engine = 0;
    S.startedAt = now() - 754;
    S.cycles = 1287;
    S.writes = 1288;
    S.refills = 9;
    S.current = 1;
    S.pending = true;
    S.checked = true;
    S.checkedAt = now() - 5;
    S.codeOk = true;
    S.worldState = 36;
    S.map = 0x811BC003;
    S.nodes = 14;
    S.chests = {{poe::Family::Main, 0x61A04000, 0x5A5A0010, 0x811944DF, 7020, 429, 2, "Main Chest"},
                {poe::Family::Side, 0x61A05000, 0x5B5B0011, 0x811944DA, 2310, 429, 4, "Side Chest 1"},
                {poe::Family::Side, 0x61A06000, 0x5C5C0012, 0x811944DA, 1966, 429, 3, "Side Chest 2"}};
    S.found = S.chests;
    S.message = "Running every frame on 3 chests";
    S.events = {{now() - 3, "Chest timer refilled (9 so far)"}, {now() - 120, "AFK: Idle timer reset 0 times (the 15-minute limit is kept)"},
                {now() - 754, "Running every frame on 3 chests"}};
}

 

ImVec4 phaseColor(Phase p, bool verified) {
    switch (p) {
    case Phase::Running: return pal::Success;
    case Phase::Holding: return pal::Warning;
    case Phase::Checking:
    case Phase::Starting:
    case Phase::Stopping: return pal::Accent;
    case Phase::Faulted: return verified ? pal::Warning : pal::Danger;
    case Phase::Stopped: return verified ? pal::Muted : pal::Danger;
    default: return pal::Muted;
    }
}

const char* phaseLabel(Phase p) {
    switch (p) {
    case Phase::Checking: return "Checking";
    case Phase::Starting: return "Starting";
    case Phase::Running: return "Running";
    case Phase::Holding: return "Replay paused (timers kept alive)";
    case Phase::Stopping: return "Stopping";
    case Phase::Stopped: return "Stopped";
    case Phase::Faulted: return "Stopped with a fault";
    default: return "Off";
    }
}

void drawHero(Ui& ui, State& S, Phase phase, const Options& o) {
    const bool running = phase == Phase::Running;
    const bool owned = live(phase);      
    const bool busy = ui.busy(kStartJob) || phase == Phase::Starting;
    double started, stopped;
    uint32_t cycles, refills;
    std::string message;
    bool verified;
    int engine;
    {
        std::lock_guard<std::mutex> lock(S.m);
        engine = S.engine;
        started = S.startedAt;
        stopped = S.stoppedAt;
        cycles = S.cycles;
        refills = S.refills;
        message = S.message;
        verified = S.cleanupVerified;
    }
    const ImVec4 col = phaseColor(phase, verified);
    const float avail = availWidth();
    const float btnW = px(150);
    const ImVec2 top = ImGui::GetCursorScreenPos();
     
    ImGui::BeginGroup();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(4));
    statusDot(col, running);
    ImGui::SameLine(0, px(8));
    {
        FontScope f(FontKind::Semibold, type::Label);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(3));
        ImGui::TextColored(col, "%s", phaseLabel(phase));
    }
    if (phase == Phase::Off) tooltip("Open the chests in the treasure room normally, stay there, and press Start with more than 30 seconds left.");
    if (owned || phase == Phase::Stopped || phase == Phase::Faulted) {
        const double el = (owned ? now() : stopped) - started;
        ImGui::SameLine(0, px(10));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(1));
        caption(fmt("%s  \xC2\xB7  %s", engineName(engine), duration(std::max(0.0, el)).c_str()).c_str(), pal::Faint);
    }
    gap(sp::XS);
    {
        FontScope f(FontKind::Semibold, 30);
        const std::string big = grouped(cycles);
        ImGui::TextColored(running ? pal::Text : pal::Muted, "%s", big.c_str());
    }
    ImGui::SameLine(0, px(10));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(16));
    {
        const double el = std::max(1.0, (owned ? now() : stopped) - started);
        const std::string rate = cycles && started > 0 ? fmt("callbacks  \xC2\xB7  %.0f / min  \xC2\xB7  %u timer refill%s", double(cycles) * 60.0 / el,
                                                             refills, refills == 1 ? "" : "s")
                                                       : std::string("callbacks");
        caption(rate.c_str(), pal::Muted);
    }
    if (!message.empty())
        wrapText(message.c_str(), std::max(px(200), avail - btnW - px(40)),
                 phase == Phase::Faulted || phase == Phase::Holding || !verified ? (verified ? pal::Warning : pal::Danger) : pal::Muted);
    ImGui::EndGroup();
    const ImVec2 after = ImGui::GetCursorScreenPos();
     
    ImGui::SetCursorScreenPos(ImVec2(top.x + avail - btnW, top.y));
    {
        auto offline = requireConnection(ui);
        if (owned) {
            if (button("Stop", {.tone = Tone::Danger, .icon = icon::Stop, .width = 150,
                                .tooltip = running ? "Stop and clean up (restores a pending callback byte, gives the AFK limit back)"
                                                   : "End the pause: stop the chest timers and give the AFK limit back",
                                .spinner = S.stop.load()}))
                S.stop = true;
        } else if (phase == Phase::Starting) {
             
            if (button(S.stop.load() ? "Cancelling" : "Cancel", {.tone = Tone::Secondary, .icon = icon::Stop, .width = 150,
                                                                  .tooltip = "Call the start off (it stops before the loop runs, or cleans up "
                                                                             "at once if it already started)",
                                                                  .spinner = true}))
                S.stop = true;
        } else if (!verified) {
             
            if (button("Checked", {.tone = Tone::Secondary, .icon = icon::Check, .width = 150,
                                   .tooltip = "The last run could not verify its cleanup (see the message and the log: a callback byte or "
                                              "the AFK limit may not be back). Check the game, then press this to allow Start again."})) {
                std::lock_guard<std::mutex> lock(S.m);
                S.cleanupVerified = true;
                S.message += " Acknowledged.";
            }
        } else {
            std::string why;
            poe::LoopConfig cfg{o.mainOnly, o.interval, uint32_t(o.limit), o.timeout, o.keepalive};
            const bool ok = poe::validConfig(cfg, &why);
            DisabledScope bad(!ok, ok ? kConnectFirst : "Check the delay and timeout values");
            if (button("Start", {.tone = Tone::Primary, .icon = icon::Play, .width = 150,
                                 .tooltip = "Check the room, select the chests and start the loop", .busy = busy})) {
                noteQueued(kStartJob);
                ui.post(kStartJob, startJob);
            }
        }
        ImGui::SetCursorScreenPos(ImVec2(top.x + avail - btnW, top.y + px(kControlH + 8)));
        const bool checking = ui.busy(kCheckJob);
        DisabledScope locked(owned, running ? "Running: the loop validates the chests itself" : "Paused: Stop first");
        if (checking) {
            if (button("Stop Check", {.tone = Tone::Ghost, .icon = icon::Stop, .width = 150, .tooltip = "Stop walking the interactable list",
                                      .spinner = true}))
                Engine::get().cancel();
        } else if (button("Check Room", {.tone = Tone::Ghost, .icon = icon::Refresh, .width = 150,
                                         .tooltip = "Read-only: walk the interactable list and show which chests are ready"})) {
            noteQueued(kCheckJob);
            ui.post(kCheckJob, checkJob);
        }
    }
    ImGui::SetCursorScreenPos(after);
    ImGui::Dummy(ImVec2(1, std::max(0.0f, top.y + px(kControlH * 2 + 8) - after.y)));
}

void drawChecklist(Ui& ui, State& S, const Options& o) {
    bool checked, codeOk, checking;
    std::string err;
    uint32_t ws, map, nodes;
    std::vector<ChestView> found;
    std::vector<std::string> warnings;
    double at;
    {
        std::lock_guard<std::mutex> lock(S.m);
        checked = S.checked;
        codeOk = S.codeOk;
        checking = S.checking;
        err = S.checkError;
        ws = S.worldState;
        map = S.map;
        nodes = S.nodes;
        found = S.found;
        warnings = S.warnings;
        at = S.checkedAt;
    }
    const Pins p = pins(ui.settings());
    kicker("BEFORE YOU START");
    gap(sp::XS + 2);
    const bool conn = ui.connected();
    checkRow("Connected to the PS3", conn ? Mark::Ok : Mark::Bad, conn ? nullptr : "Connect from the top bar");
    const Mark pending = checking ? Mark::Waiting : Mark::Unknown;
    checkRow("Native chest code matches this build", codeOk ? Mark::Ok : checked && !err.empty() && !codeOk ? Mark::Bad : pending,
             codeOk ? "5 bodies fingerprinted" : nullptr);
    const bool inWorld = checked && err.empty() && ws == WORLD_IN_WORLD;
    checkRow("In the PoE treasure room", inWorld ? Mark::Ok : checked ? Mark::Bad : pending,
             inWorld ? (p.map ? (map == p.map ? "pinned map" : "NOT the pinned map") : ("map " + hex(map)).c_str()) : (!err.empty() ? err.c_str() : nullptr));
    int sides = 0;
    const ChestView* main = nullptr;
    std::vector<const ChestView*> sideViews;
    for (const ChestView& c : found) {
        if (c.family == poe::Family::Main) main = &c;
        else {
            ++sides;
            sideViews.push_back(&c);
        }
    }
    auto chestMark = [&](const ChestView* c) { return c ? (c->lifetime > MIN_TICKS ? Mark::Ok : Mark::Bad) : (checked && err.empty() ? Mark::Bad : pending); };
    auto chestDetail = [&](const ChestView* c) -> std::string {
        if (!c) return checked && err.empty() ? "open it normally first" : "";
        return fmt("%.0f s left", double(c->lifetime) / TICKS_PER_SECOND);
    };
    std::string d = chestDetail(main);
    checkRow("Main chest opened", chestMark(main), d.c_str());
    for (int i = 0; i < 2; ++i) {
        const ChestView* c = i < int(sideViews.size()) ? sideViews[size_t(i)] : nullptr;
        d = chestDetail(c);
        const std::string label = "Side chest " + std::to_string(i + 1) + " opened";
        checkRow(label.c_str(), o.mainOnly ? Mark::Skip : chestMark(c), o.mainOnly ? "not needed (main only)" : d.c_str());
    }
    {
        int32_t least = main ? main->lifetime : 0;
        if (!o.mainOnly)
            for (const ChestView* c : sideViews) least = std::min(least, c->lifetime);
        const bool all = main && (o.mainOnly || sideViews.size() >= 2);
        const std::string leastText = all ? fmt("least %.0f s", double(least) / TICKS_PER_SECOND) : std::string();
        checkRow("More than 30 s left on every chest", checked && all ? (least > MIN_TICKS ? Mark::Ok : Mark::Bad) : checked && err.empty() ? Mark::Bad : pending,
                 leastText.empty() ? nullptr : leastText.c_str());
    }
    if (checked) {
        gap(sp::XS);
        captionf("Checked %s ago  \xC2\xB7  %u list nodes", duration(now() - at).c_str(), nodes);
    }
    for (const std::string& w : warnings) {
        gap(sp::XS);
        caption(w.c_str(), pal::Warning);
    }
}

void drawOptions(Ui& ui, State& S, Options& o, bool running) {
    Settings& set = ui.settings();
    bool changed = false;
    kicker("OPTIONS");
    gap(sp::XS + 2);
    {
        DisabledScope locked(running, "Stop the loop to change this");
        int rot = o.mainOnly ? 1 : 0;
        if (pillTabs("##rotation", {"Main + 2 Sides", "Main Only"}, &rot)) {
            o.mainOnly = rot == 1;
            changed = true;
        }
        tooltip("Rotation: main, side, side (Xbox default), or the main chest alone");
        ImGui::SameLine(0, px(10));
        int eng = o.engine;
        if (pillTabs("##engine", {"Every Frame", "PC-Driven"}, &eng)) {
            o.engine = eng;
            changed = true;
        }
        tooltip("Every frame: a game-thread task replays the callback (poe60, fastest). PC-driven: the PC validates and writes each "
                "cycle over PS3MAPI (about one per second; the Python backend's behaviour).");
    }
    gap(sp::M);
    fieldLabel("DELAY BETWEEN CALLBACKS");
    gap(sp::XS + 2);
    const double presets[] = {0, 0.1, 0.25, 0.5, 1};
    const char* labels[] = {"Max", "0.1 s", "0.25 s", "0.5 s", "1 s"};
    bool intervalChanged = false;
    for (int i = 0; i < 5; ++i) {
        if (i) ImGui::SameLine(0, px(6));
        const std::string id = std::string(labels[i]) + "##d";
        if (button(id.c_str(), {.tone = Tone::Ghost, .small = true, .tooltip = i == 0 ? "No artificial delay; every safety check still runs" : nullptr,
                                .selected = std::abs(o.interval - presets[i]) < 1e-6})) {
            o.interval = presets[i];
            intervalChanged = changed = true;
        }
    }
    ImGui::SameLine(0, px(10));
    float iv = float(o.interval);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(3));
    if (inputFloat("##interval", &iv, "%.2f s", 96)) {
        o.interval = std::clamp(double(iv), 0.0, 3600.0);
        intervalChanged = changed = true;
    }
    tooltip("Seconds between callbacks (0..3600). Changes apply to a running loop too.");
    gap(sp::M);
    {
        DisabledScope locked(running, "Stop the loop to change this");
        bool keep = o.keepalive;
        if (toggleRow("poe.keepalive", "Keep the Chests Alive", &keep,
                      {.help = "Refill a chest's lifetime to 90 s whenever it drops to 60 s (never below 30 s). Off: the loop stops before a chest expires."})) {
            o.keepalive = keep;
            changed = true;
        }
        bool afk = o.afk;
        if (toggleRow("poe.afk", "Stay in the Activity (AFK)", &afk,
                      {.help = "Hold the AFK keepalive while the loop runs (method chosen in the AFK keepalive card); given back on Stop."})) {
            o.afk = afk;
            changed = true;
        }
        gap(sp::S);
        ImGui::BeginGroup();
        fieldLabel("CALLBACK LIMIT");
        gap(sp::XS + 2);
        int lim = o.limit;
        if (inputInt("##limit", &lim, 0, 1000000, 140, 10)) {
            o.limit = lim;
            changed = true;
        }
        tooltip("Stop after this many callbacks; 0 runs until Stop");
        ImGui::EndGroup();
        ImGui::SameLine(0, px(16));
        ImGui::BeginGroup();
        fieldLabel("CALLBACK TIMEOUT");
        gap(sp::XS + 2);
        float to = float(o.timeout);
        if (inputFloat("##timeout", &to, "%.1f s", 110)) {
            o.timeout = std::clamp(double(to), 0.1, 10.0);
            changed = true;
        }
        tooltip("How long the native update may take to restore the callback byte (0.1 to 10 s; default 5)");
        ImGui::EndGroup();
    }
    if (changed) {
        {
            std::lock_guard<std::mutex> lock(S.m);
            S.opt = o;
        }
        saveOptions(set, o);
    }
    if (intervalChanged && running) {
        const double v = o.interval;
        ui.post(kIntervalJob, [v](Ctx& c) { intervalJob(c, v); });
    }
}

void drawChests(Ui& ui, State& S, Phase phase) {
    std::vector<ChestView> chests;
    uint32_t current;
    bool pending;
    {
        std::lock_guard<std::mutex> lock(S.m);
        chests = live(phase) || phase == Phase::Stopped || phase == Phase::Faulted ? S.chests : S.found;
        current = S.current;
        pending = S.pending;
    }
    kicker(phase == Phase::Running ? "CHESTS" : phase == Phase::Holding ? "CHESTS (TIMERS KEPT ALIVE)" : live(phase) ? "CHESTS" : "CHESTS (LAST CHECK)");
    gap(sp::XS + 2);
    if (chests.empty()) {
        emptyState(icon::Loot, "No Chests Yet", "Open the chests in the treasure room, then press Check Room (read only) or Start.", 150);
        return;
    }
    for (size_t i = 0; i < chests.size(); ++i) {
        const ChestView& c = chests[i];
        ImGui::PushID(int(i));
        const bool cur = phase == Phase::Running && i == current;
        const float max = c.family == poe::Family::Main ? POE_MAIN_LIFETIME_S : POE_SIDE_LIFETIME_S;
        const float secs = float(c.lifetime) / float(TICKS_PER_SECOND);
        const ImVec4 col = secs > 60 ? pal::Success : secs > 30 ? pal::Warning : pal::Danger;
        label(c.label.c_str());
        ImGui::SameLine(0, px(8));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(2));
        if (cur) pill(pending ? "waiting for the reward" : "next", pending ? pal::Accent : pal::Info, pending ? icon::History : icon::ChevronRight);
        else if (phase == Phase::Running) pill(fmt("%u", c.cycles).c_str(), pal::Muted);
        ImGui::SameLine();
        const std::string owner = "owner " + hex(c.owner);
        alignRight(textSize(FontKind::Mono, type::MonoSmall, owner.c_str()).x);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(3));
        mono(owner.c_str(), pal::Faint, type::MonoSmall);
        const std::string value = fmt("%.0f s of %.0f", double(secs), double(max)) +
                                  (phase == Phase::Running ? fmt("  \xC2\xB7  %u cycles  \xC2\xB7  %u refills", c.cycles, c.refills) : std::string());
        meter("Lifetime", secs / max, value.c_str(), col);
        if (i + 1 < chests.size()) gap(sp::S);
        ImGui::PopID();
    }
    (void)ui;
}

void drawPins(Ui& ui, State& S) {
    uint32_t map;
    std::vector<ChestView> found;
    {
        std::lock_guard<std::mutex> lock(S.m);
        map = S.map;
        found = S.found;
    }
    Settings& set = ui.settings();
    const Pins p = pins(set);
    uint32_t mainSeq = 0, sideSeq = 0;
    for (const ChestView& c : found) (c.family == poe::Family::Main ? mainSeq : sideSeq) = c.sequence;
    kicker("PS3 TAGS");
    gap(sp::XS + 2);
    if (p.map) {
        mono(fmt("map %s  main %s  side %s", hex(p.map).c_str(), hex(p.mainSeq).c_str(), hex(p.sideSeq).c_str()).c_str(), pal::Muted, type::MonoSmall);
        ImGui::SameLine(0, px(10));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(4));
        pill("pinned", pal::Success, icon::Pin);
        tooltip("Start refuses another map or chest definition.");
        ImGui::SameLine(0, px(8));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(4));
        if (button("Unpin", {.tone = Tone::Ghost, .icon = icon::Unpin, .small = true, .tooltip = "Forget the pinned tags (chests are then found by definition only)"})) {
            for (const char* k : {"loot.poe.map", "loot.poe.seq_main", "loot.poe.seq_side"}) set.set(std::string(k), std::string());
        }
    } else if (map && mainSeq) {
        mono(fmt("map %s  main %s  side %s", hex(map).c_str(), hex(mainSeq).c_str(), sideSeq ? hex(sideSeq).c_str() : "-").c_str(), pal::Text,
             type::MonoSmall);
        ImGui::SameLine(0, px(10));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(4));
        if (button("Pin These", {.tone = Tone::Secondary, .icon = icon::Pin, .small = true,
                                 .tooltip = "The PoE map and chest tags are unknown on PS3 (port map). Pin the ones read in this room so later runs "
                                            "refuse anything else."})) {
            set.set(std::string("loot.poe.map"), hex(map));
            set.set(std::string("loot.poe.seq_main"), hex(mainSeq));
            if (sideSeq) set.set(std::string("loot.poe.seq_side"), hex(sideSeq));
            notify("PoE tags pinned", Level::Info);
        }
    } else {
        wrapped("Unknown on PS3 until read in the treasure room: chests are found by their definition (root 7A0FD954, keys DDA58EF3 / "
                "DDA58EF2). Check the room once and pin them.",
                pal::Faint);
    }
}

void drawEvents(State& S) {
    std::deque<std::pair<double, std::string>> events;
    {
        std::lock_guard<std::mutex> lock(S.m);
        events = S.events;
    }
    if (events.empty()) return;
    kicker("RECENT");
    gap(sp::XS + 2);
    const double t = now();
    int n = 0;
    for (const auto& [when, text] : events) {
        if (++n > 5) break;
        const std::string ago = duration(t - when);
        caption(ago.c_str(), pal::Faint);
        ImGui::SameLine(px(70));
        caption(text.c_str(), pal::Muted);
    }
}

void drawLoop(Ui& ui) {
    seedDemo();
    State& S = st();
    Options o = options(ui.settings());
    Phase phase;
    {
        std::lock_guard<std::mutex> lock(S.m);
        phase = S.phase;
    }
    const bool running = phase == Phase::Running;
    const bool owned = live(phase);
    if (owned) requestRedraw();
    drawHero(ui, S, phase, o);
    gap(sp::L);
    const float avail = availWidth();
    const bool split = avail >= px(820);
    const float leftW = split ? std::floor((avail - px(16)) * 0.48f) : avail;
    const float rightW = split ? avail - leftW - px(16) : avail;
    auto left = [&] {
        beginPanel("##left", leftW, split ? px(430) : 0);
        drawChecklist(ui, S, o);
        divider(sp::L);
        drawOptions(ui, S, o, owned);
        endPanel();
    };
    auto right = [&] {
        beginPanel("##right", rightW, split ? px(430) : 0);
        drawChests(ui, S, phase);
        divider(sp::L);
        {
            const AfkView k = afkShared().view();
            kicker("AFK");
            gap(sp::XS + 2);
            const bool held = k.held || (demo() && running);
            checkRow(held ? (demo() ? "Idle timer reset 3 times (the 15-minute limit is kept)" : k.status.c_str())
                          : (o.afk ? "Taken when the loop starts" : "Not held (option off)"),
                     held ? (k.armed || demo() ? Mark::Ok : Mark::Waiting) : Mark::Skip);
        }
        divider(sp::L);
        drawPins(ui, S);
        endPanel();
    };
    if (split) {
        left();
        ImGui::SameLine(0, px(16));
        right();
    } else if (owned) {    
        right();
        gap(sp::M);
        left();
    } else {
        left();
        gap(sp::M);
        right();
    }
    gap(sp::M);
    drawEvents(S);
}

void drawAfk(Ui& ui) {
    Settings& set = ui.settings();
    int method = set.integer("loot.afk.method", 0);
    gap(sp::XS);
    fieldLabel("METHOD");
    gap(sp::XS + 2);
     
    const AfkView k = afkShared().view();
    const bool held = k.held;
    const std::string& status = k.status;
    const std::string& last = k.lastEvent;
    {
        DisabledScope locked(held, "Turn the keepalive off (and stop the PoE loop) to change the method");
        if (pillTabs("##afkmethod", {"Reset the Idle Timer", "Turn the Kick Limit Off"}, &method)) set.set(std::string("loot.afk.method"), method);
        tooltip(method == 0 ? "Keeps the game's limit. Every 5 s, once you have been idle 30 s, your last-activity time is moved to now "
                              "(the Xbox PoE backend's method)."
                            : "Writes 0 to the active kick limit (= never kick) and puts the original back when it stops "
                              "(the poe60 plugin's method).");
    }
    gap(sp::S);
    if (held || (demo() && method == 0)) {
        checkRow(held ? status.c_str() : "Idle timer reset 3 times (the 15-minute limit is kept)", Mark::Ok, last.empty() ? nullptr : last.c_str());
    } else {
        checkRow("Off", Mark::Skip, "PS3 modes above 2 have no limit at all");
    }
}

void resetOnDisconnect() {
    State& S = st();
    std::lock_guard<std::mutex> lock(S.m);
    if (S.phase == Phase::Running || S.phase == Phase::Holding || S.phase == Phase::Starting || S.phase == Phase::Checking) {
        S.phase = Phase::Stopped;
        if (S.message.find("Cleanup") == std::string::npos) S.message = "Stopped (disconnected or Panic)";
        S.stoppedAt = now();
    }
    S.checking = false;
    S.pending = false;
    S.stop = false;
}

}   

Summary poeSummary() {
    seedDemo();
    State& S = st();
    std::lock_guard<std::mutex> lock(S.m);
    Summary s;
    switch (S.phase) {
    case Phase::Running: {
        s.active = true;
        s.color = pal::Success;
        s.value = grouped(S.cycles) + " cycles";
        const double el = std::max(1.0, now() - S.startedAt);
        s.sub = fmt("%.0f / min, %zu chest%s", double(S.cycles) * 60.0 / el, S.chests.size(), S.chests.size() == 1 ? "" : "s");
        break;
    }
    case Phase::Holding:    
        s.active = true;
        s.color = pal::Warning;
        s.value = "Paused";
        s.sub = fmt("%u cycles; timers kept alive until Stop", S.cycles);
        break;
    case Phase::Starting:
    case Phase::Checking:
        s.value = "Starting";
        s.color = pal::Accent;
        s.sub = S.message;
        break;
    case Phase::Stopped:
    case Phase::Faulted:
        s.value = "Stopped";
        s.color = S.cleanupVerified ? pal::Muted : pal::Danger;
        s.sub = fmt("%u cycles. ", S.cycles) + S.message;
        break;
    default:
        s.value = "Off";
        s.sub = "Open the chests, then Start";
        break;
    }
    return s;
}

Summary afkSummary() {
    Summary s;
    const AfkView k = afkShared().view();    
    if (k.held) {
        s.active = true;
        s.color = pal::Success;
        s.value = "On";
        s.sub = !k.armed                                     ? "Waiting for an activity"
                : k.noLimit                                  ? "No limit in this activity"
                : k.method == loot::afk::Method::ZeroLimit ? "Kick limit off"
                                                             : fmt("Idle timer reset %u times", k.resets);
    } else if (demo()) {
        s.active = true;
        s.color = pal::Success;
        s.value = "On";
        s.sub = "Idle timer reset 3 times";
    } else {
        s.value = "Off";
        s.sub = "Stops the inactivity kick";
    }
    return s;
}

}   

namespace brps3 {

BR_FEATURES(loot_poe) {
    using namespace lootui;
    r.section({.id = "loot.poe.loop", .page = Page::Loot, .tab = kTabPoe, .section = kSection, .order = 10, .draw = drawLoop,
               .onDisconnect = resetOnDisconnect, .badge = kBadgeNeedsCheck});
    r.action({.id = kStartId, .page = Page::Loot, .tab = kTabPoe, .section = kSection, .label = "Start PoE Chest Loop",
              .help = "Check the treasure room, select the opened chests and start replaying their rewards (Loot > PoE).", .run = startJob,
              .badge = kBadgeNeedsCheck});
    r.action({.id = kStopId, .page = Page::Loot, .tab = kTabPoe, .section = kSection, .label = "Stop PoE Chest Loop",
              .help = "Stop the PoE loop and clean up (restores a pending callback byte, gives the AFK limit back).",
              .run = [](Ctx&) { st().stop = true; }, .immediate = [] { st().stop = true; }, .icon = ui::icon::Stop});
    r.action({.id = kCheckId, .page = Page::Loot, .tab = kTabPoe, .section = kSection, .label = "Check the PoE Treasure Room",
              .help = "Read-only: walk the interactable list and show which PoE chests are opened and ready.", .run = checkJob});
    r.toggle({.id = kAfkId, .page = Page::Loot, .tab = kTabPoe, .section = kAfkSection, .label = "AFK Keepalive",
              .help = "Stop the inactivity kick while you farm (PoE or anywhere in an activity). Restored when switched off.", .set = afkSet});
    r.section({.id = "loot.afk.method", .page = Page::Loot, .tab = kTabPoe, .section = kAfkSection, .order = 20, .draw = drawAfk});
    ui::styleSection(kSection, {.fullWidth = true,
                                .icon = ui::icon::Repeat,
                                .ownsActions = true});
    ui::styleSection(kAfkSection, {.icon = ui::icon::Shield});
}

}   
