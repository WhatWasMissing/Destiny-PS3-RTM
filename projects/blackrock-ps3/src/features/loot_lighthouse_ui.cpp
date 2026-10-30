 
 
 
 
 
 
 
 
 
 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app/engine.h"
#include "app/registry.h"
#include "app/settings.h"
#include "core/error.h"
#include "core/runtime.h"
#include "features/loot_common.h"
#include "game/guard_call.h"
#include "game/loot_addr.h"
#include "game/loot_lighthouse.h"
#include "ui/widgets.h"

namespace brps3::lootui {

using namespace ui;
using namespace loot;

namespace {

constexpr const char* kSection = "Walkable Lighthouse";
constexpr const char* kHoldsLoop = "Lighthouse holds";
constexpr const char* kArmJob = "Lighthouse: arm";
constexpr const char* kDisarmJob = "Lighthouse: Disarm";
constexpr const char* kRefreshJob = "Lighthouse: Read State";
constexpr const char* kRunJob = "Lighthouse: Run the Remaining Steps";
constexpr const char* kStepJobs[] = {"", "", "", "Lighthouse: Request Slice 8", "Lighthouse: instantiate", "Lighthouse: Bind the Guardian",
                                      "Lighthouse: Clear the Fade", "Lighthouse: Spawn"};

enum Step { kArm = 1, kLaunch, kRequest, kInstantiate, kBind, kFade, kSpawn, kSteps = 7 };

struct Markers {
    bool armed = false, requested = false, instantiated = false, bound = false, faded = false, spawned = false;
    bool flagLeafRestored = false;
    bool flagLeafForeign = false;        
    bool wrongMap = false;               
};

struct State {
    std::mutex m;
    bool known = false;
    lh::Snapshot snap;
    double snapAt = 0;
    Markers mk;
    uint32_t ownedPatches = 0, sharedPatches = 0, permWrites = 0, flyWrites = 0;
    std::string message;
    bool messageOk = true;
    bool autoRunning = false;
    std::string autoStep;
};
State& st() {
    static State s;
    return s;
}

struct Held {
    uint32_t addr = 0;
    uint8_t before = 0;
    bool flyin = false;
    uint32_t owner = 0;                  
};

struct Session {
     
     
    std::mutex m;
    bool hooked = false;
    Markers mk;
    std::vector<std::string> owned;      
    std::vector<std::string> shared;     
    std::map<uint32_t, Held> held;
    uint32_t permWrites = 0, flyWrites = 0;
    double nextSnapshot = 0;
    std::string tickError;               
};

lh::Pins pins(Settings& s) {
    lh::Pins p;
    if (demo() && s.str("loot.lh.map", "").empty()) {    
        p.map = XBOX_LH_MAP;
        p.slot8 = {XBOX_LH_SLOT8, XBOX_LH_SLOT8_VALUE};
        return p;
    }
    try {
        const std::string m = s.str("loot.lh.map", "");
        if (!m.empty()) p.map = world::parseHex(m);
        const std::string slot = s.str("loot.lh.slot8", "");
        const size_t colon = slot.find(':');
        if (colon != std::string::npos) p.slot8 = {world::parseHex(slot.substr(0, colon)), world::parseHex(slot.substr(colon + 1))};
    } catch (const std::exception&) {
    }
    return p;
}

bool optFlagLeaf(Settings& s) { return s.flag("loot.lh.restore_leaf", true); }
bool optFlyin(Settings& s) { return s.flag("loot.lh.flyin", true); }

void publish(Session& S, const lh::Snapshot* snap) {
    State& U = st();
    std::lock_guard<std::mutex> lock(U.m);
    if (snap) {
        U.snap = *snap;
        U.known = true;
        U.snapAt = now();
    }
    U.mk = S.mk;
    U.ownedPatches = uint32_t(S.owned.size());
    U.sharedPatches = uint32_t(S.shared.size());
    U.permWrites = S.permWrites;
    U.flyWrites = S.flyWrites;
}

void say(const std::string& text, bool ok = true) {
    feed("Lighthouse", text, ok);
    std::lock_guard<std::mutex> lock(st().m);
    st().message = text;
    st().messageOk = ok;
}

lh::Snapshot snapshot(Ctx& c) {
    RuntimeConsole con(c.rt());
    return lh::readSnapshot(con);
}

 

void restoreHeld(Ctx& c, Session& S) {
    RuntimeConsole con(c.rt());
    for (auto& [addr, h] : S.held) {
        try {
            bool same = false;
            if (h.flyin) {
                same = con.u32(LH_FLYIN_PTR) == addr;
            } else {
                const world::Bytes ctx = con.bytes(LH_CONTEXT, 0x10);
                const uint32_t root = con.u32(LH_ROOT_PTR);
                same = root == h.owner && ctx.u32(4) == root + 0x50 && ctx.u32(0xC) + LH_PERMISSION == addr;
            }
            if (same && con.u8(addr) == 1) con.w8(addr, h.before);
            else c.log("[lighthouse] held byte at " + hex(addr) + " changed owner: left alone");
        } catch (const std::exception& e) {
            c.log("[lighthouse] restoring " + hex(addr) + ": " + e.what());
        }
    }
    S.held.clear();
}

 
void disarm(Ctx& c, Session& S, const char* why) {
    for (auto it = S.owned.rbegin(); it != S.owned.rend(); ++it) {
        try {
            c.rt().revert(*it);
        } catch (const std::exception& e) {
            c.log("[lighthouse] revert " + *it + ": " + e.what());
        }
    }
    S.owned.clear();
    S.shared.clear();
    restoreHeld(c, S);
    S.mk = Markers{};
    S.permWrites = S.flyWrites = 0;
    S.tickError.clear();
    publish(S, nullptr);
    say(why);
    c.log(std::string("[lighthouse] ") + why);
}

 
 
 
 
std::vector<Runtime::Word> patchWords(const lh::Patch& p) {
    std::vector<Runtime::Word> words;
    for (size_t k = 0; k < p.original.size(); k += 4) {
        Runtime::Word w;
        w.addr = p.addr + uint32_t(k);
        w.original.assign(p.original.begin() + std::ptrdiff_t(k), p.original.begin() + std::ptrdiff_t(k + 4));
        w.replacement.assign(p.replacement.begin() + std::ptrdiff_t(k), p.replacement.begin() + std::ptrdiff_t(k + 4));
        words.push_back(std::move(w));
    }
    std::reverse(words.begin(), words.end());
    return words;
}

 
 
bool heldByRuntime(Runtime& rt, const lh::Patch& p) {
    for (size_t k = 0; k < p.original.size(); k += 4)
        if (rt.siteHolders(p.addr + uint32_t(k)).empty()) return false;
    return true;
}

 
void holdsStep(Ctx& c, Session& S) {
    RuntimeConsole con(c.rt());
    if (!S.mk.wrongMap) {
         
        const world::Bytes ctx = con.bytes(LH_CONTEXT, LH_ROOT_PTR + 4 - LH_CONTEXT);
        const uint32_t root = ctx.u32(LH_ROOT_PTR - LH_CONTEXT), inner = ctx.u32(0xC);
        if (heapPtr(root) && ctx.u32(4) == root + 0x50 && heapPtr(inner, LH_PERMISSION + 4)) {
            const uint32_t a = inner + LH_PERMISSION;
            const uint8_t v = con.u8(a);
            if (v != 1) {
                if (!S.held.count(a)) S.held[a] = {a, v, false, root};
                con.w8(a, 1, false);    
                ++S.permWrites;
            }
        }
        if (optFlyin(c.settings())) {
            const uint32_t f = con.u32(LH_FLYIN_PTR);
            if (heapPtr(f)) {
                const uint8_t v = con.u8(f);
                if (v != 1) {
                    if (!S.held.count(f)) S.held[f] = {f, v, true, f};
                    con.w8(f, 1, false);
                    ++S.flyWrites;
                }
            }
        }
    }
    if (now() < S.nextSnapshot) return;
    S.nextSnapshot = now() + 1.0;
    const lh::Snapshot snap = lh::readSnapshot(con);
    const lh::Pins p = pins(c.settings());
     
     
    const bool wrong = snap.worldState == WORLD_IN_WORLD && p.hasMap() && snap.map != p.map;
    if (wrong != S.mk.wrongMap) {
        S.mk.wrongMap = wrong;
        const std::string msg = wrong ? "Loaded map " + hex(snap.map) + ", not the pinned Lighthouse " + hex(p.map) +
                                            ": holds paused, patches kept. Disarm to restore them, or return to orbit to try again."
                                      : std::string("Left that activity: the holds are on again. Launch the Lighthouse.");
        say(msg, !wrong);
        c.log("[lighthouse] " + msg);
    }
     
    if (!S.mk.flagLeafRestored && optFlagLeaf(c.settings()) && lh::isLighthouse(snap, p)) {
        const std::string name = std::string("loot.lighthouse.flag_leaf");
        uint32_t leafAddr = 0;
        for (const lh::Patch& lp : lh::patches())
            if (lp.flagLeaf) leafAddr = lp.addr;
        if (c.rt().applied(name)) {
            c.rt().revert(name);
            S.owned.erase(std::remove(S.owned.begin(), S.owned.end(), name), S.owned.end());
            if (leafAddr && !c.rt().siteHolders(leafAddr).empty()) {
                 
                S.mk.flagLeafForeign = true;
                const std::string msg = "Lighthouse map verified, but the blanket flag leaf is also held by Internal mode and stays "
                                        "patched: turn that lever off so chest rewards evaluate normally.";
                say(msg, false);
                c.log("[lighthouse] " + msg);
            } else {
                c.log("[lighthouse] Lighthouse map verified: blanket flag leaf restored; permission and transition holds remain");
            }
        } else if (std::find(S.shared.begin(), S.shared.end(), std::string("flag_leaf")) != S.shared.end()) {
             
             
            S.mk.flagLeafForeign = true;
            const std::string msg = "Lighthouse map verified, but the blanket flag leaf belongs to Internal mode and stays patched: turn "
                                    "that lever off so chest rewards evaluate normally.";
            say(msg, false);
            c.log("[lighthouse] " + msg);
        }
        S.mk.flagLeafRestored = true;
    }
     
     
     
    const auto& list = lh::patches();
    for (size_t i = 0; i < list.size() && i < snap.sites.size(); ++i) {
        const lh::Patch& patch = list[i];
        const auto sh = std::find(S.shared.begin(), S.shared.end(), std::string(patch.id));
        if (sh == S.shared.end() || snap.sites[i] != lh::SiteState::Original) continue;
        S.shared.erase(sh);
        if (patch.flagLeaf && S.mk.flagLeafRestored) continue;    
        c.rt().apply(lh::patchName(patch), patchWords(patch));
        S.owned.push_back(lh::patchName(patch));
        c.log(std::string("[lighthouse] ") + patch.id + ": the other feature reverted its shared patch; the Lighthouse owns it now");
    }
    publish(S, &snap);
}

void holdsTick(Ctx& c) {
    Session& S = c.state<Session>("loot.lighthouse");
    std::lock_guard<std::mutex> lock(S.m);
    if (!S.mk.armed) {
        c.stopLoop(kHoldsLoop);    
        return;
    }
    try {
        holdsStep(c, S);
    } catch (const std::exception& e) {
         
        if (S.tickError != e.what()) {
            S.tickError = e.what();
            c.log(std::string("[lighthouse] holds: ") + e.what());
            say(std::string("Holds: ") + e.what() + " (still armed, retrying)", false);
        }
    }
}

void hookOnce(Ctx& c, Session& S) {
    if (S.hooked) return;
    S.hooked = true;
    c.onDisconnect([](Ctx& dc) {
        dc.stopLoop(kHoldsLoop);    
        Session& s = dc.state<Session>("loot.lighthouse");
        std::lock_guard<std::mutex> lock(s.m);
        if (s.mk.armed || !s.owned.empty() || !s.held.empty()) disarm(dc, s, "Disarmed (disconnect)");
    });
}

void armJob(Ctx& c) {
     
     
     
    if (!c.rt().siteHolders(LH_PERMISSION_GATE).empty())
        throw Error("The permission gate " + hex(LH_PERMISSION_GATE) +
                    " is patched by Console > World > Activity gates (World-transition bypass, or Internal mode with \"Keep permission "
                    "gate\"). Turn those off first: the Lighthouse procedure needs the gate original.");
    c.require(kLighthouseFingerprints, "Walkable Lighthouse (native transition code)");
    Session& S = c.state<Session>("loot.lighthouse");
    {
        std::lock_guard<std::mutex> lock(S.m);
        if (S.mk.armed) throw Error("Already armed");
        RuntimeConsole con(c.rt());
        const lh::Snapshot snap = lh::readSnapshot(con);
        publish(S, &snap);
        const lh::Readiness r = lh::armReadiness(snap);
        if (!r.ready()) throw Error("Cannot arm: " + r.firstProblem());
        hookOnce(c, S);
        c.onPanic("loot.lighthouse", [](Ctx& pc) {
            pc.stopLoop(kHoldsLoop);    
            Session& s = pc.state<Session>("loot.lighthouse");
            std::lock_guard<std::mutex> l(s.m);
            if (s.mk.armed || !s.owned.empty() || !s.held.empty()) disarm(pc, s, "Disarmed by Panic");
        });
        const auto& list = lh::patches();
        try {
            for (size_t i = 0; i < list.size(); ++i) {
                const lh::Patch& p = list[i];
                const lh::SiteState site = lh::siteState(con.read(p.addr, uint32_t(p.original.size())), p);
                if (site == lh::SiteState::Patched && heldByRuntime(c.rt(), p)) {
                     
                     
                    c.rt().apply(lh::patchName(p), patchWords(p));
                    S.owned.push_back(lh::patchName(p));
                    c.log(std::string("[lighthouse] ") + p.id + " already patched by another feature: shared through the runtime");
                    continue;
                }
                if (site == lh::SiteState::Patched) {
                    S.shared.push_back(p.id);
                    c.log(std::string("[lighthouse] ") + p.id + " already patched by another feature: shared, not owned");
                    continue;
                }
                if (site == lh::SiteState::Foreign) throw Error(std::string("Unexpected bytes at ") + hex(p.addr) + " (" + p.id + ")");
                c.rt().apply(lh::patchName(p), patchWords(p));
                S.owned.push_back(lh::patchName(p));
            }
        } catch (...) {
            for (auto it = S.owned.rbegin(); it != S.owned.rend(); ++it) {
                try {
                    c.rt().revert(*it);
                } catch (...) {
                }
            }
            S.owned.clear();
            S.shared.clear();
            throw;
        }
        S.mk = Markers{};
        S.mk.armed = true;
        S.nextSnapshot = 0;
        S.tickError.clear();
        publish(S, nullptr);
        say(fmt("Armed: %zu patches applied%s. Launch the Lighthouse from orbit now.", S.owned.size(),
                S.shared.empty() ? "" : fmt(", %zu shared", S.shared.size()).c_str()));
        c.log("[lighthouse] armed");
    }
    c.startLoop(kHoldsLoop, 100, holdsTick);    
}

void disarmJob(Ctx& c) {
    c.stopLoop(kHoldsLoop);    
    Session& S = c.state<Session>("loot.lighthouse");
    std::lock_guard<std::mutex> lock(S.m);
    if (!S.mk.armed && S.owned.empty() && S.held.empty()) {
        say("Not armed");
        return;
    }
    disarm(c, S, "Disarmed: patches reverted, held bytes given back");
}

void refreshJob(Ctx& c) {
    Session& S = c.state<Session>("loot.lighthouse");
    const lh::Snapshot snap = snapshot(c);
    std::lock_guard<std::mutex> lock(S.m);
    publish(S, &snap);
    if (!snap.error.empty()) c.log("[lighthouse] state read: " + snap.error);
}

 

lh::Snapshot checked(Ctx& c, Session& S, lh::Readiness (*fn)(const lh::Snapshot&, const lh::Pins&), const char* what) {
    const lh::Snapshot snap = snapshot(c);
    publish(S, &snap);
    const lh::Readiness r = fn(snap, pins(c.settings()));
    if (!r.ready()) throw Error(std::string(what) + ": " + r.firstProblem());
    return snap;
}

void requestStep(Ctx& c) {
    c.require(kLighthouseFingerprints, "Walkable Lighthouse (native transition code)");
    Session& S = c.state<Session>("loot.lighthouse");
    std::lock_guard<std::mutex> lock(S.m);
    const lh::Snapshot s = checked(c, S, lh::requestReadiness, "Request slice 8");
    RuntimeConsole con(c.rt());
     
    con.w32(s.mgr + mgr::REQUEST_HASH, FNV_EMPTY);
    con.w8(s.mgr + mgr::REQUEST_FLAG, 0);
    if (con.u32(s.mgr + mgr::REQUEST_TARGET) != 0xFFFFFFFF || con.u8(s.mgr + mgr::STATE89) != 0)
        throw Error("The transition state changed before the request: nothing committed");
    con.w32(s.mgr + mgr::REQUEST_TARGET, LH_TARGET_SLICE, false);
    S.mk.requested = true;
    publish(S, nullptr);
    say("Slice 8 requested: wait until it is loaded (current slice -1, slice 8 loaded), then instantiate.");
    c.log("[lighthouse] slice 8 requested through the teleport mailbox");
}

void instantiateStep(Ctx& c) {
    c.require(kLighthouseFingerprints, "Walkable Lighthouse (native transition code)");
    Session& S = c.state<Session>("loot.lighthouse");
    std::lock_guard<std::mutex> lock(S.m);
    const lh::Snapshot s = checked(c, S, lh::instantiateReadiness, "Instantiate");
    RuntimeConsole(c.rt()).w8(s.mgr + mgr::INSTANTIATE, 0x80, false);    
    S.mk.instantiated = true;
    publish(S, nullptr);
    say("Instantiation requested: wait for current slice 8.");
    c.log("[lighthouse] instantiate requested (manager +0x8A = 0x80)");
}

void bindStep(Ctx& c) {
    c.require(kLighthouseFingerprints, "Walkable Lighthouse (native transition code)");
    Session& S = c.state<Session>("loot.lighthouse");
    std::lock_guard<std::mutex> lock(S.m);
    const lh::Snapshot s = checked(c, S, lh::bindReadiness, "Bind");
    RuntimeConsole con(c.rt());
    Resolver res(con);
    const lh::Inventory inv = lh::readInventory(con, res, 12);
    if (!heapPtr(inv.small, small::ENTITY2 + 4)) throw Error("No local player entry");
    if (std::any_of(inv.entities.begin(), inv.entities.end(), [](const lh::Entity& e) { return e.matches; }))
        throw Error("An entity already carries your identity");
    const lh::Entity* pick = nullptr;
    for (const lh::Entity& e : inv.entities)
        if (e.empty() && e.boundPlayer == 0xFFFFFFFF) {
            pick = &e;
            break;
        }
    if (!pick) throw Error(fmt("No empty player entity among %zu", inv.entities.size()));
    if (pick->offset != 0 || !pick->identityAddr || !pick->innerAddr) throw Error("The empty entity is not the verified shape");
    if (inv.identity == std::array<uint8_t, 8>{}) throw Error("The local player has no identity");
     
    if (c.rt().call(LH_REQUEST_PREDICATE).u32() != 0) throw Error("The request predicate blocks: try again in a moment");
    Resolver fresh(con);    
    if (fresh.body(pick->handle) != std::optional<uint32_t>(pick->address - pick->offset)) throw Error("The entity moved");
    if (con.read(pick->identityAddr, 8) != std::vector<uint8_t>(8, 0)) throw Error("The entity identity is no longer empty");
    con.write(pick->identityAddr, std::vector<uint8_t>(inv.identity.begin(), inv.identity.end()));
     
     
    const uint32_t hi = uint32_t(inv.identity[0]) << 24 | uint32_t(inv.identity[1]) << 16 | uint32_t(inv.identity[2]) << 8 | inv.identity[3];
    const uint32_t lo = uint32_t(inv.identity[4]) << 24 | uint32_t(inv.identity[5]) << 16 | uint32_t(inv.identity[6]) << 8 | inv.identity[7];
    guard::Request q(LH_BINDING_REFRESH);
    q.arg(inv.small + small::IDENTITY);
    q.require(guard::Cond::abs(pick->identityAddr, guard::Cmp::Eq, hi, guard::Width::U32, 0xFFFFFFFFu, "entity identity (high)"));
    q.require(guard::Cond::abs(pick->identityAddr + 4, guard::Cmp::Eq, lo, guard::Width::U32, 0xFFFFFFFFu, "entity identity (low)"));
    q.require(guard::Cond::abs(inv.small + small::ENTITY, guard::Cmp::Eq, 0xFFFFFFFFu, guard::Width::U32, 0xFFFFFFFFu, "player still unbound"));
    const guard::Result r = guard::call(c.rt(), q, 3000, true);
    if (!r.called) throw Error("Binding refresh refused: " + r.reason);
    const uint32_t bound = con.u32(inv.small + small::ENTITY), back = con.u32(pick->address + entity::BOUND_PLAYER);
    c.log(fmt("[lighthouse] binding refresh: player +0x50 = %s, entity +0x130 = %s", hex(bound).c_str(), hex(back).c_str()));
    if (bound == 0xFFFFFFFF) throw Error("The binding refresh ran but the player is still unbound");
    S.mk.bound = true;
    publish(S, nullptr);
    say(fmt("Guardian bound (entity %s). Clear the fade, then spawn.", hex(pick->handle).c_str()));
}

void fadeStep(Ctx& c) {
    c.require(kLighthouseFingerprints, "Walkable Lighthouse (native transition code)");
    Session& S = c.state<Session>("loot.lighthouse");
    std::lock_guard<std::mutex> lock(S.m);
    const lh::Snapshot s = snapshot(c);
    publish(S, &s);
    if (!lh::isLighthouse(s, pins(c.settings()))) throw Error("Clear the fade: the pinned Lighthouse map is not loaded");
    RuntimeConsole con(c.rt());
    const uint32_t p = con.u32(LH_FADE_PTR);
    if (!heapPtr(p, 512)) throw Error("No fade block");
    for (const lh::Write& w : lh::fadeWrites(con.bytes(p, 512))) con.write(w.addr, w.data);
    S.mk.faded = true;
    publish(S, nullptr);
    say("Fade cleared.");
    c.log("[lighthouse] fade cleared");
}

void spawnStep(Ctx& c) {
    c.require(kLighthouseFingerprints, "Walkable Lighthouse (native transition code)");
    Session& S = c.state<Session>("loot.lighthouse");
    std::lock_guard<std::mutex> lock(S.m);
    const lh::Snapshot s = checked(c, S, lh::lifetimeReadiness, "Spawn");
    RuntimeConsole con(c.rt());
    Resolver res(con);
    const lh::Inventory players = lh::readInventory(con, res, 12);
    if (!std::any_of(players.entities.begin(), players.entities.end(), [](const lh::Entity& e) { return e.matches; }))
        throw Error("Bind your Guardian first");
    const lh::Inventory life = lh::readInventory(con, res, 16);
     
    if (life.count != 1 || life.entities.size() != 1)
        throw Error(fmt("Expected one activity-lifetime entity, found %u (%zu resolve)", life.count, life.entities.size()));
    const lh::Entity& e = life.entities[0];
    if (e.phase != LH_PHASE_NONE) throw Error(fmt("The lifetime phase is already %u", e.phase));
    auto readiness = [&]() -> std::string {
        guard::Request q(LH_SPAWN_READINESS);
        q.arg(s.smallHandle).arg(0).arg(0);
        q.require(guard::Cond::abs(s.small + small::HANDLE, guard::Cmp::Eq, s.smallHandle, guard::Width::U32, 0xFFFFFFFFu, "player handle unchanged"));
        const guard::Result r = guard::call(c.rt(), q, 3000, true);
        return r.called ? std::to_string(uint32_t(r.r3)) : "refused (" + r.reason + ")";
    };
    const std::string before = readiness();
     
    Resolver fresh(con);    
    if (fresh.body(e.handle) != std::optional<uint32_t>(e.address - e.offset) || con.u8(e.address + entity::LIFETIME_PHASE) != LH_PHASE_NONE)
        throw Error("The activity-lifetime entity changed before the write: nothing written");
    con.w8(e.address + entity::LIFETIME_PHASE, LH_PHASE_ACCEPTED);
    const std::string after = readiness();
    S.mk.spawned = true;
    publish(S, nullptr);
    say(fmt("Lifetime phase 3 supplied: spawn readiness %s -> %s. The engine creates your Guardian.", before.c_str(), after.c_str()));
    c.log("[lighthouse] lifetime phase FF -> 03; readiness " + before + " -> " + after);
}

 
lh::Snapshot waitFor(Ctx& c, const std::function<bool(const lh::Snapshot&)>& pred, const char* what, double seconds) {
    const double end = now() + seconds;
    for (;;) {
        if (c.cancelled()) throw Error("Stopped");
        const lh::Snapshot s = snapshot(c);
        {
            Session& S = c.state<Session>("loot.lighthouse");
            std::lock_guard<std::mutex> lock(S.m);
            publish(S, &s);
        }
        if (pred(s)) return s;
        if (now() > end) throw Error(std::string("Timed out waiting for ") + what);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
}

void runJob(Ctx& c) {
    {
        std::lock_guard<std::mutex> lock(st().m);
        st().autoRunning = true;
    }
    struct Done {
        ~Done() {
            std::lock_guard<std::mutex> lock(st().m);
            st().autoRunning = false;
            st().autoStep.clear();
        }
    } done;
    auto step = [](const char* t) {
        std::lock_guard<std::mutex> lock(st().m);
        st().autoStep = t;
    };
    const lh::Pins p = pins(c.settings());
    Markers mk;
    {
        Session& S = c.state<Session>("loot.lighthouse");
        std::lock_guard<std::mutex> lock(S.m);
        mk = S.mk;
    }
    lh::Snapshot s = snapshot(c);
    if (!lh::isLighthouse(s, p)) throw Error("Load the pinned Lighthouse map first (step 2)");
    if (s.current == LH_START_SLICE && !mk.requested) {
        step("Requesting slice 8");
        requestStep(c);
    }
    if (s.current != LH_TARGET_SLICE) {
        step("Waiting for slice 8 to load");
        waitFor(c, [&](const lh::Snapshot& x) { return lh::instantiateReadiness(x, p).ready() || x.current == LH_TARGET_SLICE; },
                "slice 8 to load", 30);
        s = snapshot(c);
        if (s.current != LH_TARGET_SLICE) {
            step("Instantiating");
            instantiateStep(c);
            step("Waiting for current slice 8");
            waitFor(c, [](const lh::Snapshot& x) { return x.current == LH_TARGET_SLICE; }, "slice 8 instantiation", 20);
        }
    }
    s = snapshot(c);
    if (!s.bound()) {
        step("Binding your Guardian");
        bindStep(c);
    }
    step("Clearing the fade");
    fadeStep(c);
    step("Spawning");
    spawnStep(c);
    c.toast("Lighthouse: done. Your Guardian should appear; walk around.");
}

 

void seedDemo() {
    static bool seeded = false;
    if (seeded || !demo()) return;
    seeded = true;
    State& U = st();
    std::lock_guard<std::mutex> lock(U.m);
    U.known = true;
    U.snapAt = now() - 0.4;
    lh::Snapshot& s = U.snap;
    s.worldState = 36;
    s.stateName = "activity:in_world";
    s.map = 0x8155C003;
    s.current = 0xFFFFFFFF;
    s.pending = 0xFFFFFFFF;
    s.loaded = 0x100;
    s.slots[8] = {0x8154E003, 0xFC};
    s.mgr = 0x3073E780;
    s.pause = 0;
    s.enabled = 4;
    s.s89 = 6;
    s.s8a = 0;
    s.target130 = 8;
    s.worker = 4;
    s.reqTarget = 0xFFFFFFFF;
    s.reqHash = FNV_EMPTY;
    s.globalsTarget = 8;
    s.coherent = true;
    s.permission = 1;
    s.flyinByte = 1;
    s.identity = {0x11, 0x00, 0x00, 0x00, 0x2A, 0x5B, 0x9C, 0x07};
    s.smallEntity = s.smallEntity2 = 0xFFFFFFFF;
    s.fadeCount = 1;
    s.sites.assign(lh::patches().size(), lh::SiteState::Patched);
    s.sites[1] = lh::SiteState::Original;
    s.gateOriginal = true;
    s.trialsKnown = true;
    s.trials[0] = s.trials[1] = 2;
    U.mk.armed = U.mk.requested = U.mk.flagLeafRestored = true;
    U.ownedPatches = 6;
    U.permWrites = 3;
    U.flyWrites = 41;
    U.message = "Slice 8 requested: wait until it is loaded (current slice -1, slice 8 loaded), then instantiate.";
}

 
bool stepDone(int step, const lh::Snapshot& s, const Markers& mk, const lh::Pins& p, bool known) {
    switch (step) {
    case kArm: return mk.armed;
    case kLaunch: return known && lh::isLighthouse(s, p);
    case kRequest: return mk.requested || (known && (s.current == LH_TARGET_SLICE || (s.current == 0xFFFFFFFF && s.loadedSlice(LH_TARGET_SLICE))));
    case kInstantiate: return known && s.current == LH_TARGET_SLICE;
    case kBind: return mk.bound || (known && s.bound());
    case kFade: return mk.faded;
    case kSpawn: return mk.spawned;
    default: return false;
    }
}

struct StepInfo {
    const char* title;
    const char* text;
    const char* button;
};
const StepInfo kInfo[] = {
    {"", "", ""},
    {"Arm in Orbit", "Applies the 7 instruction patches and holds the permission and fly-in bytes while the Lighthouse loads.", "Arm"},
    {"Launch the Lighthouse", "Launch it from orbit as usual. Blackrock keeps the load open and verifies the map (pin it the first time).", ""},
    {"Request Slice 8", "Asks the transition manager for Mercury slice 8 through its own teleport mailbox.", "Request"},
    {"Instantiate Slice 8", "Once slice 8 is loaded, requests the normal instantiation past the missing session acknowledgement.", "Instantiate"},
    {"Bind Your Guardian", "Gives your identity to an empty player entity and runs the engine's binding refresh on the game thread.", "Bind"},
    {"Clear the Fade", "Clears the parked screen fade so the scene shows.", "Clear Fade"},
    {"Spawn", "Supplies activity-lifetime phase 3: spawn readiness turns true and the engine creates your Guardian.", "Spawn"},
};

lh::Readiness readinessFor(int step, const lh::Snapshot& s, const lh::Pins& p) {
    switch (step) {
    case kArm: return lh::armReadiness(s);
    case kRequest: return lh::requestReadiness(s, p);
    case kInstantiate: return lh::instantiateReadiness(s, p);
    case kBind: return lh::bindReadiness(s, p);
    case kSpawn: return lh::lifetimeReadiness(s, p);
    case kFade: {
        lh::Readiness r;
        r.checks.push_back({"The pinned Lighthouse map is loaded", lh::isLighthouse(s, p)});
        r.checks.push_back({"Your Guardian is bound", s.bound()});
        return r;
    }
    default: {
        lh::Readiness r;
        r.checks.push_back({"In an activity (world state 36)", s.worldState == WORLD_IN_WORLD});
        r.checks.push_back({p.hasMap() ? "The pinned Lighthouse map is loaded" : "The Lighthouse map is pinned", lh::isLighthouse(s, p)});
        return r;
    }
    }
}

void postStep(Ui& ui, int step) {
    void (*fn)(Ctx&) = nullptr;
    switch (step) {
    case kArm: fn = armJob; break;
    case kRequest: fn = requestStep; break;
    case kInstantiate: fn = instantiateStep; break;
    case kBind: fn = bindStep; break;
    case kFade: fn = fadeStep; break;
    case kSpawn: fn = spawnStep; break;
    default: return;
    }
    const char* job = step == kArm ? kArmJob : kStepJobs[step];
    noteQueued(job);
    ui.post(job, fn, [](bool ok, const std::string& err) {
        if (!ok) say(err, false);
    });
}

void drawStepper(Ui& ui, const lh::Snapshot& s, const Markers& mk, const lh::Pins& p, bool known) {
    int active = 0;
    for (int i = 1; i <= kSteps; ++i)
        if (!stepDone(i, s, mk, p, known)) {
            active = i;
            break;
        }
    const bool conn = ui.connected();
    for (int i = 1; i <= kSteps; ++i) {
        ImGui::PushID(i);
        const bool done = stepDone(i, s, mk, p, known);
        const bool cur = i == active;
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float w = availWidth();
        ImDrawList* d = ImGui::GetWindowDrawList();
         
        const float r = px(12);
        const ImVec2 c(pos.x + r, pos.y + r + px(2));
        if (done) {
            d->AddCircleFilled(c, r, u32(alpha(pal::Success, 0.18f)));
            d->AddCircle(c, r, u32(alpha(pal::Success, 0.6f)), 0, px(1.2f));
            const ImVec2 is = textSize(FontKind::Icons, 11, icon::Check);
            drawText(d, FontKind::Icons, 11, c - is * 0.5f, u32(pal::Success), icon::Check);
        } else {
            d->AddCircleFilled(c, r, u32(cur ? alpha(pal::Accent, 0.16f) : pal::Raised));
            d->AddCircle(c, r, u32(cur ? pal::Accent : pal::Border), 0, px(1.2f));
            const std::string n = std::to_string(i);
            const ImVec2 ns = textSize(FontKind::Semibold, type::Small, n.c_str());
            drawText(d, FontKind::Semibold, type::Small, c - ns * 0.5f, u32(cur ? pal::Accent : pal::Muted), n.c_str());
        }
         
        const float tx = pos.x + r * 2 + px(14);
        ImGui::SetCursorScreenPos(ImVec2(tx, pos.y));
        ImGui::BeginGroup();
        {
            FontScope f(FontKind::Semibold, type::Label);
            ImGui::TextColored(done ? pal::Muted : cur ? pal::Text : pal::Soft, "%s", kInfo[i].title);
        }
        tooltip(kInfo[i].text);
        if (cur && known) {
            gap(sp::XS);
            const lh::Readiness rd = readinessFor(i, s, p);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0));
            for (const lh::Check& ch : rd.checks) checkRow(ch.what.c_str(), ch.ok ? Mark::Ok : Mark::Waiting);
            ImGui::PopStyleVar();
        }
        ImGui::EndGroup();
        const float bottom = ImGui::GetItemRectMax().y;
         
        if (kInfo[i].button[0]) {
            ImGui::SetCursorScreenPos(ImVec2(pos.x + w - px(120), pos.y));
            const char* job = i == kArm ? kArmJob : kStepJobs[i];
            bool ready = conn && known && readinessFor(i, s, p).ready() && (i == kArm ? !mk.armed : mk.armed || i >= kBind);
            if (i == kFade || i == kSpawn) ready = conn && known && readinessFor(i, s, p).ready();
            DisabledScope dis(!ready, !conn ? kConnectFirst : done ? "Done" : "Not ready yet: see the checks");
            if (button(kInfo[i].button, {.tone = cur ? Tone::Primary : Tone::Secondary, .width = 120, .busy = ui.busy(job)})) postStep(ui, i);
        } else if (i == kLaunch && known && !p.hasMap() && s.worldState == WORLD_IN_WORLD && s.current == LH_START_SLICE) {
            ImGui::SetCursorScreenPos(ImVec2(pos.x + w - px(120), pos.y));
            if (button("Pin This Map", {.tone = Tone::Primary, .icon = icon::Pin, .width = 120,
                                        .tooltip = "You are in the Lighthouse (state 36, slice 9): pin its map and slot-8 tags. Unknown on PS3 until now."})) {
                ui.settings().set(std::string("loot.lh.map"), hex(s.map));
                ui.settings().set(std::string("loot.lh.slot8"), hex(s.slots[8].tag) + ":" + hex(s.slots[8].value));
                notify("Lighthouse map pinned", Level::Info);
            }
        }
        const float next = std::max(bottom, pos.y + r * 2 + px(4)) + px(14);
        if (i < kSteps)    
            d->AddLine(ImVec2(c.x, c.y + r + px(3)), ImVec2(c.x, next + px(1)), u32(done ? alpha(pal::Success, 0.5f) : pal::Border), px(1.5f));
        ImGui::SetCursorScreenPos(ImVec2(pos.x, next));
        ImGui::PopID();
    }
}

void kv(const char* k, const std::string& v, const ImVec4& col = pal::Text, bool monoValue = true) {
    caption(k, pal::Faint);
    ImGui::SameLine(px(118));
    if (monoValue) mono(v.c_str(), col, type::MonoSmall);
    else caption(v.c_str(), col);
}

void drawStatus(Ui& ui, const lh::Snapshot& s, const Markers& mk, const lh::Pins& p, bool known, double at, uint32_t owned, uint32_t shared,
                uint32_t perm, uint32_t fly) {
    kicker("LIVE STATE");
    ImGui::SameLine();
    alignRight(px(28));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(8));
    {
        auto offline = requireConnection(ui);
        if (iconButton("##lhrefresh", icon::Refresh, "Read the state again (read only)", false, 28)) ui.post(kRefreshJob, refreshJob);
    }
    gap(sp::XS);
    if (!known) {
        caption("Not read yet: connect and press refresh (read only).", pal::Faint);
        return;
    }
    captionf("Read %s ago%s", duration(now() - at).c_str(), s.error.empty() ? "" : "  \xC2\xB7  partial");
    gap(sp::S);
    kv("World", lh::worldStateLabel(s.worldState, s.stateName), s.worldState == WORLD_IN_WORLD ? pal::Success : pal::Text, false);
    kv("Map", hex(s.map) + (p.hasMap() ? (s.map == p.map ? "  pinned" : "  (pinned " + hex(p.map) + ")") : "  not pinned"),
       p.hasMap() && s.map == p.map ? pal::Success : pal::Text);
    kv("Slice", fmt("current %d  pending %d", int32_t(s.current), int32_t(s.pending)));
    std::string bits;
    for (int b = 0; b < 16; ++b)
        if (s.loadedSlice(uint32_t(b))) bits += (bits.empty() ? "" : ",") + std::to_string(b);
    kv("Loaded", bits.empty() ? "-" : bits);
    kv("Slot 8", hex(s.slots[8].tag) + " " + hex(s.slots[8].value));
    gap(sp::S);
    kv("Transition", fmt("+80 %04X  +88 %02X %02X %02X", s.pause, s.enabled, s.s89, s.s8a));
    kv("Target", fmt("mgr %d  globals %d  worker %u", int32_t(s.target130), int32_t(s.globalsTarget), s.worker));
    kv("Mailbox", fmt("%d  %s  %u", int32_t(s.reqTarget), hex(s.reqHash).c_str(), s.reqFlag));
    gap(sp::S);
    kv("Patches", fmt("%u owned  %u shared  of %zu", owned, shared, lh::patches().size()), owned + shared == lh::patches().size() ? pal::Success : pal::Text, false);
    {
         
        bool leafPatched = false;
        for (size_t i = 0; i < lh::patches().size() && i < s.sites.size(); ++i)
            if (lh::patches()[i].flagLeaf) leafPatched = s.sites[i] == lh::SiteState::Patched;
        const char* leaf = !mk.armed                           ? (leafPatched ? "patched by another feature" : "original")
                           : !mk.flagLeafRestored             ? "held (until the map is verified)"
                           : leafPatched && mk.flagLeafForeign ? "kept by Internal mode (not ours)"
                                                               : "restored after load";
        kv("Flag Leaf", leaf, leafPatched && mk.flagLeafRestored ? pal::Warning : pal::Text, false);
    }
    kv("Holds", mk.wrongMap ? std::string("paused: not the pinned Lighthouse map") : fmt("permission %u  fly-in %u writes", perm, fly),
       mk.wrongMap ? pal::Warning : pal::Text, false);
    kv("Permission", fmt("%u (context %s)", s.permission, s.coherent ? "coherent" : "not coherent"), pal::Text, false);
    gap(sp::S);
    kv("Identity", hexBytes(s.identity.data(), 8));
    kv("Bound", s.bound() ? hex(s.smallEntity) : "no", s.bound() ? pal::Success : pal::Muted);
    kv("Fade", fmt("%u entr%s", s.fadeCount, s.fadeCount == 1 ? "y" : "ies"), pal::Text, false);
    kv("Trials", s.trialsKnown ? fmt("%02X %02X%s", s.trials[0], s.trials[1], s.trials[0] == 2 && s.trials[1] == 2 ? "  held" : "") : std::string("-"),
       pal::Muted);
    if (s.trialsKnown) {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Optional prerequisite of the later Xbox script (Trials 8/9-win flags, 02 02). Likely on PS3; not required here.");
    }
    if (!s.error.empty()) {
        gap(sp::S);
        caption(s.error.c_str(), pal::Warning);
    }
}

void drawLighthouse(Ui& ui) {
    seedDemo();
    State& U = st();
    lh::Snapshot s;
    Markers mk;
    bool known, autoRunning, ok;
    double at;
    uint32_t owned, shared, perm, fly;
    std::string message, autoStep;
    {
        std::lock_guard<std::mutex> lock(U.m);
        s = U.snap;
        mk = U.mk;
        known = U.known;
        at = U.snapAt;
        owned = U.ownedPatches;
        shared = U.sharedPatches;
        perm = U.permWrites;
        fly = U.flyWrites;
        message = U.message;
        ok = U.messageOk;
        autoRunning = U.autoRunning;
        autoStep = U.autoStep;
    }
    Settings& set = ui.settings();
    const lh::Pins p = pins(set);
    if (mk.armed || autoRunning) requestRedraw();
     
    if (ui.connected() && !mk.armed && !autoRunning && !ui.busy(kRefreshJob) && now() - at > 4.0 && !demo()) {
        {
            std::lock_guard<std::mutex> lock(U.m);
            U.snapAt = now();    
        }
        ui.post(kRefreshJob, refreshJob);
    }
     
    {
        int active = 0;
        for (int i = 1; i <= kSteps; ++i)
            if (!stepDone(i, s, mk, p, known)) {
                active = i;
                break;
            }
        const float avail = availWidth();
        ImGui::BeginGroup();
        statusDot(mk.armed ? pal::Success : pal::Muted, mk.armed);
        ImGui::SameLine(0, px(8));
        {
            FontScope f(FontKind::Semibold, type::Title);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(4));
            if (active) ImGui::Text("Step %d of %d: %s", active, int(kSteps), kInfo[active].title);
            else ImGui::TextColored(pal::Success, "Done: Walk Around");
        }
        tooltip(known ? "Return to orbit, press Arm, then launch the Lighthouse. Every step re-checks the game state before it writes."
                      : "Connect, return to orbit and press Arm. Every step re-checks the game state before it writes anything.");
        if (!message.empty()) wrapText(message.c_str(), std::max(px(240), avail - px(360)), ok ? pal::Muted : pal::Danger);
        ImGui::EndGroup();
        ImGui::SameLine();
        const float bw = px(200) + px(8) + px(110);
        alignRight(bw);
        auto offline = requireConnection(ui);
        if (autoRunning) {
            if (button("Stop", {.tone = Tone::Danger, .icon = icon::Stop, .width = 200, .tooltip = autoStep.c_str(), .spinner = true})) Engine::get().cancel();
        } else {
            const bool can = known && lh::isLighthouse(s, p) && mk.armed && active >= kRequest;
            DisabledScope dis(!can, !known ? "Read the state first" : !mk.armed ? "Arm first (step 1)" : "Load the pinned Lighthouse first (step 2)");
            if (button("Run the Rest", {.tone = Tone::Primary, .icon = icon::Play, .width = 200,
                                        .tooltip = "Steps 3 to 7 in order, waiting (bounded) for each state; Stop at any time"})) {
                noteQueued(kRunJob);
                ui.post(kRunJob, runJob, [](bool good, const std::string& err) {
                    if (!good) say(err, false);
                });
            }
        }
        ImGui::SameLine(0, px(8));
        DisabledScope notArmed(!mk.armed && owned == 0, "Not armed");
        if (button("Disarm", {.tone = Tone::Ghost, .icon = icon::Undo, .width = 110, .tooltip = "Revert the patches and give the held bytes back (asks first)"}))
            confirm("Disarm the Lighthouse", "Reverting the patches can tear down the held activity. Disarm now?", "Disarm",
                    [] { Engine::get().post(kDisarmJob, disarmJob); });
    }
    if (autoRunning) {
        gap(sp::S);
        progressBar(-1, 0, 4);
        caption(autoStep.c_str(), pal::Muted);
    }
    gap(sp::L);
    const float avail = availWidth();
    const bool split = avail >= px(880);
    const float rightW = split ? std::clamp(avail * 0.36f, px(320), px(420)) : avail;
    const float leftW = split ? avail - rightW - px(16) : avail;
    ImGui::BeginChild("##steps", ImVec2(leftW, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_NavFlattened);
    drawStepper(ui, s, mk, p, known);
    gap(sp::S);
    bool leaf = optFlagLeaf(set), fly2 = optFlyin(set);
    if (toggleRow("lh.leaf", "Restore the Blanket Flag Leaf After Load", &leaf,
                  {.help = "Recommended (verifier): once the Lighthouse map is verified the flag leaf goes back to original, so chest rewards "
                           "evaluate negative requirements normally."}))
        set.set(std::string("loot.lh.restore_leaf"), leaf);
    if (toggleRow("lh.flyin", "Hold the Fly-In Curtain Byte", &fly2, {.help = "lhrun.py's default; off = its 'noflyin' option."}))
        set.set(std::string("loot.lh.flyin"), fly2);
    if (p.hasMap()) {
        gap(sp::S);
        mono(fmt("pinned: map %s  slot 8 %s %s", hex(p.map).c_str(), hex(p.slot8.tag).c_str(), hex(p.slot8.value).c_str()).c_str(), pal::Faint, type::MonoSmall);
        ImGui::SameLine(0, px(10));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(4));
        if (button("Unpin", {.tone = Tone::Ghost, .icon = icon::Unpin, .small = true, .tooltip = "Forget the pinned Lighthouse tags"})) {
            set.set(std::string("loot.lh.map"), std::string());
            set.set(std::string("loot.lh.slot8"), std::string());
        }
    }
    ImGui::EndChild();
    if (split) ImGui::SameLine(0, px(16));
    else gap(sp::M);
    beginPanel("##lhstatus", rightW);
    drawStatus(ui, s, mk, p, known, at, owned, shared, perm, fly);
    endPanel();
}

void resetOnDisconnect() {
    State& U = st();
    std::lock_guard<std::mutex> lock(U.m);
    U.mk = Markers{};
    U.ownedPatches = U.sharedPatches = 0;
    U.autoRunning = false;
}

}   

Summary lighthouseSummary() {
    seedDemo();
    Summary s;
    State& U = st();
    std::lock_guard<std::mutex> lock(U.m);
    const lh::Pins p = pins(Engine::get().settings());
    int active = 0;
    for (int i = 1; i <= kSteps; ++i)
        if (!stepDone(i, U.snap, U.mk, p, U.known)) {
            active = i;
            break;
        }
    if (U.mk.armed) {
        s.active = true;
        s.color = pal::Accent;
        s.value = active ? fmt("Step %d of 7", active) : std::string("Done");
        s.sub = U.mk.wrongMap ? "Holds paused: not the pinned Lighthouse map" : active ? kInfo[active].title : "Walk around";
        if (U.mk.wrongMap) s.color = pal::Warning;
    } else {
        s.value = "Disarmed";
        s.sub = p.hasMap() ? "Arm in orbit to start" : "Arm in orbit, then pin the map";
    }
    return s;
}

}   

namespace brps3 {

BR_FEATURES(loot_lighthouse) {
    using namespace lootui;
    r.section({.id = "loot.lighthouse", .page = Page::Loot, .tab = kTabLighthouse, .section = kSection, .order = 10, .draw = drawLighthouse,
               .onDisconnect = resetOnDisconnect, .badge = kBadgeNeedsCheck});
    auto act = [&](const char* id, const char* label, const char* help, void (*fn)(Ctx&), bool dangerous = false) {
        r.action({.id = id, .page = Page::Loot, .tab = kTabLighthouse, .section = kSection, .label = label, .help = help, .run = fn,
                  .dangerous = dangerous, .badge = kBadgeNeedsCheck});
    };
    act("loot.lh.arm", "Lighthouse: Arm (In Orbit)", "Apply the 7 Lighthouse patches and start the holds; launch the Lighthouse afterwards.", armJob);
    act("loot.lh.request", "Lighthouse: Request Slice 8", "Teleport mailbox request for slice 8 (in the Lighthouse, slice 9).", requestStep);
    act("loot.lh.instantiate", "Lighthouse: Instantiate Slice 8", "Request the instantiation once slice 8 is loaded.", instantiateStep);
    act("loot.lh.bind", "Lighthouse: Bind the Guardian", "Give your identity to an empty player entity and run the binding refresh.", bindStep);
    act("loot.lh.fade", "Lighthouse: Clear the Fade", "Clear the parked screen fade.", fadeStep);
    act("loot.lh.spawn", "Lighthouse: Spawn", "Supply lifetime phase 3: the engine creates your Guardian.", spawnStep);
    act("loot.lh.run", "Lighthouse: Run the Remaining Steps", "Steps 3 to 7 in order with bounded waits.", runJob);
    act("loot.lh.disarm", "Lighthouse: Disarm", "Revert the patches and give the held bytes back (may tear down the activity).", disarmJob, true);
    act("loot.lh.read", "Lighthouse: Read State", "Read the Lighthouse / transition state (read only).", refreshJob);
    ui::styleSection(kSection, {.fullWidth = true,
                                .icon = ui::icon::Lightbulb,
                                .ownsActions = true});
}

}   
