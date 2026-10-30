#include "game/activity_changeworld.h"

#include "core/error.h"
#include "core/ppc.h"
#include "core/runtime.h"
#include "game/activity_addr.h"
#include "game/activity_catalog.h"
#include "game/world_mem.h"

namespace brps3::activity {

namespace {

constexpr const char* kPhasePatch = "activity.change_world.phase";
constexpr const char* kPhaseStub = "activity.phase_stub";

 
struct PhaseControl {
    uint32_t block = 0;
};

}   

 
 
 
 
std::vector<uint8_t> phaseStub(uint32_t base, uint32_t ctrl) {
    using namespace ppc;
    Asm a(base);
    a.addr(11, ctrl);
    a.lwz(12, 0, 11);
    a.cmpwi(12, 1);
    a.bne("skip");
    a.lwz(12, 4, 11);
    a.stw(12, int64_t(addr::CW_PHASE_OFFSET), 5);
    a.li(12, 2);
    a.stw(12, 0, 11);
    a.label("skip");
    a.b(addr::WE_POST);
    return a.link();
}

std::string ChangeWorldReady::blocker() const {
    if (!controllerOk) return "world controller not set up";
    if (state != 36) return "not in a world (state " + stateLabel(state) + ", need 36 activity:in_world)";
    if (!inOrbit) return "activity scenario is not the orbit tag (read " + world::hex8(scenario) + ")";
    if (!registry) return "world-event registry not available";
    if (!payloadOk) return "world-event registry " + world::hex8(registry) + " looks implausible";
    if (slotState != 0) return "change-world slot busy (state " + std::to_string(slotState) + ")";
    if (dependencyPending) return "a world-event dependency (event 11) is pending";
    return "";
}

ChangeWorldReady readChangeWorldReady(Ctx& c, uint32_t orbitScenario) {
    c.require(kChangeWorldFingerprints, "Change world");
    world::RuntimeReader mem(c.rt());
    ChangeWorldReady r;
    const uint32_t controller = mem.u32(addr::WORLD_CONTROLLER_PTR);
    if (world::mapped(controller, 0x130)) {
        r.controllerOk = true;
        r.state = mem.u32(controller + 0x118);
    }
    const uint32_t record = mem.u32(addr::ACTIVITY_RECORD_PTR);
    if (world::mapped(record, 0x274)) {
        r.scenario = mem.u32(record + addr::RECORD_CHECKSUM);
        r.inOrbit = r.scenario == orbitScenario;    
    }
    r.registry = mem.u32(addr::WE_REGISTRY_PTR);
    if (world::mapped(r.registry, addr::WE_PAYLOADS)) {
        const world::Bytes reg = mem.bytes(r.registry + addr::WE_RUN, addr::WE_PAYLOADS - addr::WE_RUN);
        r.slotState = reg.u32(addr::WE_SLOT_STATE_CW - addr::WE_RUN);
        r.dependencyPending = reg.u32(addr::WE_PENDING_RUN_11 - addr::WE_RUN) != 0 && reg.u32(addr::WE_PENDING_STATE_11 - addr::WE_RUN) == 2;
        r.payloadOk = world::mapped(r.registry + addr::WE_PAYLOAD_CW, addr::CW_EVENT_SIZE);
    } else {
        r.registry = 0;
    }
    if (auto obj = mem.pointer(addr::SCENARIO_CLIENT_PTR, 0x14)) {
        if (auto v = mem.tryBytes(*obj + 0x10, 4)) {
            r.scenarioClientOk = true;
            r.scenarioClient = v->u32(0);
        }
    }
    return r;
}

std::vector<guard::Cond> changeWorldGuards(uint32_t orbit) {
    using guard::Cmp;
    using guard::Cond;
    return {
        Cond::ptr(addr::WORLD_CONTROLLER_PTR, {0x118}, Cmp::Eq, addr::STATE_IN_WORLD, "world state 36 activity:in_world"),
        Cond::ptr(addr::ACTIVITY_RECORD_PTR, {addr::RECORD_CHECKSUM}, Cmp::Eq, orbit, "activity scenario is the orbit tag " + world::hex8(orbit)),
        Cond::ptr(addr::WE_REGISTRY_PTR, {addr::WE_SLOT_STATE_CW}, Cmp::Eq, 0, "change-world slot free (registry +0x128 == 0)"),
    };
}

guard::Request changeWorldRequest(const std::string& activityName, uint32_t orbit) {
    if (!validActivityName(activityName)) throw Error("activity name must be [a-z0-9_], 1..39 chars");
    guard::Request q(addr::CW_NAMED);
     
    q.scratch.assign(activityName.begin(), activityName.end());
    q.scratch.resize((activityName.size() + 1 + 15) & ~size_t(15), 0);
    q.scratchArg(0);
    for (guard::Cond& c : changeWorldGuards(orbit)) q.require(std::move(c));
    return q;
}

std::string ChangeWorldResult::summary() const {
    if (!called) return "refused on the game thread: " + refused;
    std::string s = r3 == 1 ? "queued" : "not queued: a world-event dependency (event 11) was pending";
    if (phaseArmed) s += phaseFired ? ", phase written" : ", phase stub did NOT fire (launched without the override)";
    if (slotAfter != 0xFFFFFFFFu) {
        s += ", slot state " + std::to_string(slotAfter);
        if (slotAfter == 0 && r3 == 1) s += " (already consumed)";
    }
    if (queuedPhaseKnown) s += ", queued phase " + world::hex8(queuedPhase);
    return s;
}

ChangeWorldResult changeWorld(Ctx& c, const std::string& activityName, uint32_t phaseHash, uint32_t orbitScenario) {
    const bool withPhase = phaseHash != kNoPhase;
    if (withPhase && !isCrotaPhase(phaseHash)) throw Error("a phase override must be one of the six Crota's End phases");
    if (withPhase && activityName != kCrotaActivity)
        throw Error(std::string("a phase override only applies to ") + kCrotaActivity + " (Crota's End)");
    const guard::Request request = changeWorldRequest(activityName, orbitScenario);    
    c.require(kChangeWorldFingerprints, "Change world");

     
    const ChangeWorldReady ready = readChangeWorldReady(c, orbitScenario);
    if (!ready.ready()) throw Error("change world refused: " + ready.blocker());

    Runtime& rt = c.rt();
    ChangeWorldResult res;
    uint32_t ctrl = 0;
    if (withPhase) {
        PhaseControl& pc = c.state<PhaseControl>("activity.phase_control");
        if (!pc.block) pc.block = rt.alloc(16);
        ctrl = pc.block;
        rt.writeU32(ctrl + 4, phaseHash);
        rt.writeU32(ctrl + 0, 1);    
        const uint32_t stub = rt.sharedCode(kPhaseStub, [ctrl](uint32_t base) { return phaseStub(base, ctrl); }, 0x30);
        const uint32_t bl = ppc::branchWord(addr::CW_POST_CALLSITE, stub, true);
        rt.apply(kPhasePatch, {{addr::CW_POST_CALLSITE, {0x4B, 0xDF, 0x3F, 0xE1}, world::be32(bl)}});
        res.phaseArmed = true;
    }
    auto disarm = [&] {
        if (!withPhase) return;
        rt.revert(kPhasePatch);             
        res.phaseFired = rt.u32(ctrl + 0) == 2;
        rt.writeU32(ctrl + 0, 0);
    };

    guard::Result g;
    try {
        g = guard::call(rt, request, 3000, true);
    } catch (...) {
        try {
            disarm();
        } catch (const std::exception& e) {
            c.log(std::string("change world: phase hook cleanup failed (the runtime reverts it on disconnect): ") + e.what());
        }
        throw;
    }
    disarm();
    res.called = g.called;
    if (!g.called) {
        res.refused = g.reason;
        return res;
    }
    res.r3 = int(int32_t(uint32_t(g.r3)));
     
    world::RuntimeReader mem(rt);
    if (auto st = mem.tryBytes(ready.registry + addr::WE_SLOT_STATE_CW, 4)) res.slotAfter = st->u32(0);
    if (res.slotAfter == 2) {
        if (auto ph = mem.tryBytes(ready.registry + addr::WE_PAYLOAD_CW + addr::CW_PHASE_OFFSET, 4)) {
            res.queuedPhaseKnown = true;
            res.queuedPhase = ph->u32(0);
        }
    }
    return res;
}

}   
