#include "game/activity_monitor.h"

#include <cstdio>

#include "game/activity_addr.h"
#include "game/activity_catalog.h"
#include "game/names_catalog.h"
#include "game/world_tags.h"

namespace brps3::activity {

namespace {

std::string scenarioText(uint32_t tag) {
    if (!tag) return "none";
    char b[16];
    std::snprintf(b, sizeof b, "%08X", tag);
    if (const NamedActivity* a = catalog().byScenario(names::fromPs3Tag(tag))) return a->display + " (" + a->name + ", " + b + ")";
    return b;
}

}   

WorldSnapshot parseMonitorBlock(const world::Bytes& block, WorldSnapshot s) {
    s.clock = block.u32(addr::ENGINE_CLOCK_LO - kMonitorBlock);
    s.sobjectCount = block.u32(addr::SOBJECT_COUNT - kMonitorBlock);
    s.wtlGlobals = block.u32(addr::WTL_GLOBALS_PTR - kMonitorBlock);
    s.wtlCursor = block.u16(addr::WTL_CURSOR - kMonitorBlock);
    return s;
}

WorldSnapshot readWorldSnapshot(world::Reader& mem) {
    WorldSnapshot s;
    const world::Bytes holder = mem.bytes(addr::WORLD_HOLDER, 0x10);
    s.controller = holder.u32(addr::WORLD_CONTROLLER_PTR - addr::WORLD_HOLDER);
    s.record = holder.u32(addr::ACTIVITY_RECORD_PTR - addr::WORLD_HOLDER);
    if (world::mapped(s.controller, 0x130)) {
        if (auto c = mem.tryBytes(s.controller + 0x118, 0x18)) {
            s.controllerOk = true;
            s.state = c->u32(0);
            s.requested = c->u32(0x10);
            s.reason = c->u32(0x14);
        }
    }
    if (world::mapped(s.record, 0x274)) {
        if (auto r = mem.tryBytes(s.record + addr::RECORD_CHECKSUM, 4)) {
            s.scenarioOk = true;
            s.scenario = r->u32(0);
        }
    }
    if (auto h = mem.tryBytes(addr::TARGET_NAME, 0x38)) {
        s.target = h->cstr(0, 0x28);
        s.copy628 = h->u32(0x28);
        s.copy630 = h->u64(0x30);
    }
    s = parseMonitorBlock(mem.bytes(kMonitorBlock, kMonitorBlockSize), s);
    if (auto a = mem.tryBytes(addr::ACTIVE_ACTIVITY, 8)) {
        const uint32_t p = a->u32(4);
        if (world::mapped(p, 0x28)) {
            if (auto n = mem.tryBytes(p, 0x28)) {
                s.activeOk = true;
                s.activeName = n->cstr(0, 0x28);
            }
        }
    }
    return s;
}

uint32_t datumTag(uint32_t ns, uint32_t salt, uint32_t datum) {
    const uint32_t idx = datum & 0x1FFF;
    uint32_t tag;
    if (ns & 0x40000000u) tag = 0x80000000u | ((salt << 27) & 0x38000000u) | ((ns << 13) & 0x07FFE000u);
    else tag = ((salt << 23) & 0x7F800000u) | ((ns << 13) & 0x007FE000u);
    return (tag & 0xFFFFE000u) | idx;
}

DescriptorRead readCurrentActivity(world::Reader& mem) {
    DescriptorRead d;
    auto no = [&](std::string why) {
        d.ok = false;
        d.why = std::move(why);
        return d;
    };
    d.base = mem.u32(addr::DEST_OWNER_PTR);
    if (!world::mapped(d.base, 0x30)) return no("destination owner " + world::hex8(d.base) + " is not set");
    const world::Bytes head = mem.bytes(d.base, 0x30);
    d.index = head.u32(0);
    if (d.index > 3) return no("destination owner index " + std::to_string(d.index) + " is out of range");
    d.datum = head.u32(8 * d.index + 0x14);
    if (!world::validHandle(d.datum)) return no("no destination (datum -1: not in an activity yet)");
    const world::PoolHeader pool = world::readPool(mem, addr::DEST_POOL);
    if (!pool.base || !pool.identityStride) return no("destination pool " + world::hex8(addr::DEST_POOL) + " is not set up");
    const uint32_t idx = d.datum & 0x1FFF;
    if (idx >= pool.capacity) return no("datum index " + std::to_string(idx) + " is beyond the pool capacity");
    const uint32_t saltAt = pool.base + pool.identityOffset + idx * pool.identityStride;
    if (!world::mapped(saltAt, 4)) return no("datum salt at " + world::hex8(saltAt) + " is unmapped");
    d.tag = datumTag(pool.nameSpace, mem.u32(saltAt) & pool.mask, d.datum);
    const world::TagTable t = world::readTagTable(mem);
    const world::Resolved r = world::resolve(mem, t, d.tag, false);
    if (!r.ok) return no("destination tag " + world::hex8(d.tag) + ": " + r.why);
    d.object = r.body;
    d.desc = d.object + addr::DEST_BLOCK + addr::DEST_DESCRIPTOR;
    if (!world::mapped(d.desc - 4, 0x34)) return no("descriptor " + world::hex8(d.desc) + " is unmapped");
    const world::Bytes b = mem.bytes(d.desc - 4, 0x34);
    d.fnvBefore = b.u32(0);
    d.id = b.u16(4);
    d.overrideId = b.u16(6);
    d.name = b.cstr(12, 0x28);
    d.ok = true;
    return d;
}

std::vector<Transition> TransitionTracker::feed(const WorldSnapshot& s, double now) {
    std::vector<Transition> out;
    auto add = [&](Transition::Kind k, uint32_t from, uint32_t to, std::string text, bool warn = false) {
        Transition t;
        t.time = now;
        t.kind = k;
        t.from = from;
        t.to = to;
        t.text = std::move(text);
        t.warn = warn;
        out.push_back(std::move(t));
    };
    if (!last_) {
        if (s.controllerOk) add(Transition::Found, 0, s.state, "watching: " + stateLabel(s.state));
        else add(Transition::Lost, 0, 0, "world controller not set up yet", false);
        last_ = s;
        return out;
    }
    const WorldSnapshot& p = *last_;
    if (p.controllerOk != s.controllerOk) {
        if (s.controllerOk) add(Transition::Found, 0, s.state, "world controller back: " + stateLabel(s.state));
        else add(Transition::Lost, p.state, 0, "world controller gone (title restart or teardown)", true);
    }
    if (s.controllerOk) {
        if (p.controllerOk && p.state != s.state) add(Transition::State, p.state, s.state, stateLabel(p.state) + "  \xE2\x86\x92  " + stateLabel(s.state));
        if (p.controllerOk && p.requested != s.requested)
            add(Transition::Requested, p.requested, s.requested, "requested " + stateLabel(s.requested));
         
        if (s.aborting() && (!p.controllerOk || !p.aborting() || p.reason != s.reason))
            add(Transition::Abort, p.reason, s.reason, "abort to cleanup, reason " + abortReasonLabel(s.reason), true);
    }
    if (p.target != s.target && !s.target.empty()) add(Transition::Target, 0, 0, "transition target \"" + s.target + "\"");
    if (p.loaderBusy() != s.loaderBusy())
        add(Transition::Loader, p.wtlCursor, s.wtlCursor, s.loaderBusy() ? "tag loader busy (request " + std::to_string(s.wtlCursor) + ")" : "tag loader idle");
    if (s.scenarioOk && p.scenario != s.scenario) add(Transition::Scenario, p.scenario, s.scenario, "activity scenario " + scenarioText(s.scenario));
    last_ = s;
    return out;
}

}   
