#include "game/loot_lighthouse.h"

#include <algorithm>

#include "core/error.h"
#include "game/loot_poe.h"

namespace brps3::loot::lh {

using world::Bytes;

 

const std::vector<Patch>& patches() {
     
    static const std::vector<Patch> p = {
        {"valid_predicate", 0xC35140, {0x54, 0x63, 0x07, 0xFE}, {0x38, 0x60, 0x00, 0x01},
         "context vt+0x2C \"valid\" returns 1 (clrlwi r3 -> li r3,1)"},
        {"flag_leaf", 0xC5BE34, {0x7C, 0x84, 0x07, 0x34, 0x38, 0xA0, 0x00, 0x00}, {0x38, 0x60, 0x00, 0x01, 0x4E, 0x80, 0x00, 0x20},
         "flag_is_unlocked returns 1 (blanket; restored once the map is verified)", true},
        {"bap_dead_1", 0x8C5F98, {0x90, 0x7E, 0x10, 0xF8}, {0x60, 0x00, 0x00, 0x00}, "BAP pump: no \"state := 9 dead\" store (1)"},
        {"bap_dead_2", 0x8C6018, {0x90, 0x7E, 0x10, 0xF8}, {0x60, 0x00, 0x00, 0x00}, "BAP pump: no \"state := 9 dead\" store (2)"},
        {"state21", 0x33AA9C, {0x90, 0x7C, 0x04, 0xE0}, {0x60, 0x00, 0x00, 0x00}, "activity transition keeps its state (no 0x21)"},
        {"state6", 0x33AAA8, {0x40, 0x82, 0x00, 0xF4}, {0x48, 0x00, 0x00, 0xF4}, "skip the state-6 block (bne -> b)"},
        {"result13", 0x3599DC, {0x3B, 0xC0, 0x00, 0x13}, {0x3B, 0xC0, 0x00, 0x1E}, "result 0x13 becomes 0x1E"},
    };
    return p;
}

std::string patchName(const Patch& p) { return std::string("loot.lighthouse.") + p.id; }

SiteState siteState(const std::vector<uint8_t>& current, const Patch& p) {
    if (current == p.original) return SiteState::Original;
    if (current == p.replacement) return SiteState::Patched;
    return SiteState::Foreign;
}

 

std::string worldStateLabel(uint32_t state, const std::string& name) {
    if (state == 0xFFFFFFFF) return "unknown";
    return name.empty() ? std::to_string(state) : std::to_string(state) + " " + name;
}

Snapshot readSnapshot(world::Reader& mem) {
    Snapshot s;
    auto attempt = [&](const char* what, auto fn) {
        try {
            fn();
        } catch (const std::exception& e) {
            if (s.error.empty()) s.error = std::string(what) + ": " + e.what();
        }
    };
    attempt("pointers", [&] {
        const Bytes p = mem.bytes(WORLD_PTR, LH_GLOBALS_PTR + 4 - WORLD_PTR);
        s.world = p.u32(0);
        s.mgr = p.u32(LH_TRANSITION_MGR_PTR - WORLD_PTR);
        s.globals = p.u32(LH_GLOBALS_PTR - WORLD_PTR);
    });
    attempt("world state", [&] {
        if (!heapPtr(s.world, WORLD_STATE + 4)) return;
        s.worldState = mem.u32(s.world + WORLD_STATE);
        s.stateName = poe::worldStateName(mem, s.worldState);
    });
    attempt("slices", [&] {
        const Bytes b = mem.bytes(LH_PLAYER_ENTRY_PTR, LH_REGISTRY_INDEX + 2 - LH_PLAYER_ENTRY_PTR);
        s.small = b.u32(0);
        s.slices = b.u32(SLICE_MANAGER_PTR - LH_PLAYER_ENTRY_PTR);
        s.registryIndex = b.u16(LH_REGISTRY_INDEX - LH_PLAYER_ENTRY_PTR);
        if (!heapPtr(s.slices, SLICE_LOADED + 4)) return;
        const Bytes t = mem.bytes(s.slices, SLICE_TABLE + 8 * 16);
        s.map = t.u32(0);
        for (uint32_t i = 0; i < 16; ++i) s.slots[i] = {t.u32(SLICE_TABLE + 8 * i), t.u32(SLICE_TABLE + 8 * i + 4)};
        const Bytes c = mem.bytes(s.slices + SLICE_CURRENT, 12);
        s.current = c.u32(0);
        s.pending = c.u32(4);
        s.loaded = c.u32(8);
    });
    attempt("transition manager", [&] {
        if (!heapPtr(s.mgr, mgr::REQUEST_FLAG + 4)) return;
        const Bytes m = mem.bytes(s.mgr + mgr::PAUSE, mgr::REQUEST_FLAG + 4 - mgr::PAUSE);
        auto o = [](uint32_t off) { return off - mgr::PAUSE; };
        s.pause = m.u16(o(mgr::PAUSE));
        s.enabled = m.u8(o(mgr::ENABLED));
        s.s89 = m.u8(o(mgr::STATE89));
        s.s8a = m.u8(o(mgr::INSTANTIATE));
        s.target130 = m.u32(o(mgr::TARGET_SLICE));
        s.worker = m.u8(o(mgr::WORKER));
        s.reqTarget = m.u32(o(mgr::REQUEST_TARGET));
        s.reqHash = m.u32(o(mgr::REQUEST_HASH));
        s.reqFlag = m.u8(o(mgr::REQUEST_FLAG));
    });
    attempt("globals", [&] {
        if (heapPtr(s.globals, LH_GLOBALS_TARGET + 4)) s.globalsTarget = mem.u32(s.globals + LH_GLOBALS_TARGET);
    });
    attempt("context", [&] {
        const Bytes c = mem.bytes(LH_CONTEXT, LH_ROOT_PTR + 4 - LH_CONTEXT);
        s.ctx4 = c.u32(4);
        s.inner = c.u32(0xC);
        s.root = c.u32(LH_ROOT_PTR - LH_CONTEXT);
        s.coherent = heapPtr(s.root) && s.ctx4 == s.root + 0x50;
        s.cache = c.u32(LH_TRIALS_CACHE);
        if (s.coherent && heapPtr(s.inner, LH_PERMISSION + 4)) s.permission = mem.bytes(s.inner + LH_PERMISSION, 1).u8(0);
        if (s.coherent && heapPtr(s.inner) && heapPtr(s.cache, 12 + 1996)) {
            const Bytes t = mem.bytes(s.cache + 12 + 1994, 2);
            s.trials[0] = t.u8(0);
            s.trials[1] = t.u8(1);
            s.trialsKnown = true;
        }
    });
    attempt("fly-in block", [&] {
        s.flyin = mem.u32(LH_FLYIN_PTR);
        if (heapPtr(s.flyin)) s.flyinByte = mem.bytes(s.flyin, 1).u8(0);
    });
    attempt("local player", [&] {
        if (!heapPtr(s.small, small::ENTITY2 + 4)) return;
        const Bytes p = mem.bytes(s.small, small::ENTITY2 + 4);
        for (uint32_t i = 0; i < 8; ++i) s.identity[i] = p.u8(small::IDENTITY + i);
        s.smallHandle = p.u32(small::HANDLE);
        s.smallEntity = p.u32(small::ENTITY);
        s.smallEntity2 = p.u32(small::ENTITY2);
    });
    attempt("fade block", [&] {
        s.fade = mem.u32(LH_FADE_PTR);
        if (heapPtr(s.fade)) s.fadeCount = mem.u32(s.fade);
    });
    attempt("request predicate", [&] {
         
        const Bytes g = mem.bytes(LH_PREDICATE_GLOBALS, 8);
        const uint32_t p = g.u32(0), q = g.u32(4);
        if (!p) {
            s.predicateBlocks = false;
            return;
        }
        if (!dataPtr(p & ~3u, 4) || !dataPtr(q)) {
            s.predicateBlocks = true;    
            return;
        }
        const Bytes pb = mem.bytes(p, 2);
        s.predicateBlocks = (pb.u8(0) || pb.u8(1)) && mem.u32(q) != 0;
    });
    attempt("patch sites", [&] {
        for (const Patch& p : patches()) s.sites.push_back(siteState(mem.read(p.addr, uint32_t(p.original.size())), p));
        s.gateOriginal = mem.u32(LH_PERMISSION_GATE) == 0x4082001C;
    });
    return s;
}

 

bool Readiness::ready() const {
    return std::all_of(checks.begin(), checks.end(), [](const Check& c) { return c.ok; });
}

std::string Readiness::firstProblem() const {
    for (const Check& c : checks)
        if (!c.ok) return c.what;
    return {};
}

bool isLighthouse(const Snapshot& s, const Pins& pins) { return s.worldState == WORLD_IN_WORLD && pins.hasMap() && s.map == pins.map; }

namespace {
void lighthouseChecks(Readiness& r, const Snapshot& s, const Pins& pins) {
    r.checks.push_back({"In an activity (world state 36)", s.worldState == WORLD_IN_WORLD});
    r.checks.push_back({pins.hasMap() ? "The pinned Lighthouse map (" + hex(pins.map) + ") is loaded" : "The Lighthouse map is pinned",
                        pins.hasMap() && s.map == pins.map});
}
}   

Readiness armReadiness(const Snapshot& s) {
    Readiness r;
    r.checks.push_back({"In orbit (world state 27)", s.worldState == WORLD_ORBIT});
    bool sites = !s.sites.empty();
    for (SiteState st : s.sites) sites = sites && st != SiteState::Foreign;
    r.checks.push_back({"Every patch site holds its original (or the same patch)", sites});
    r.checks.push_back({"Permission gate 0x36EA5C is original", s.gateOriginal});
    return r;
}

Readiness requestReadiness(const Snapshot& s, const Pins& pins) {
    Readiness r;
    lighthouseChecks(r, s, pins);
    r.checks.push_back({"Current slice is 9 (initial Lighthouse slice)", s.current == LH_START_SLICE});
    r.checks.push_back({"No slice load pending", s.pending == 0xFFFFFFFF});
    r.checks.push_back({pins.hasSlot() ? "Slot 8 holds the pinned entry" : "Slot 8 entry is pinned",
                        pins.hasSlot() && s.slots[LH_TARGET_SLICE].tag == pins.slot8.tag && s.slots[LH_TARGET_SLICE].value == pins.slot8.value});
    r.checks.push_back({"Transition manager not paused (+0x80 == 0000)", s.pause == 0});
    r.checks.push_back({"Transition manager enabled and idle (+0x88 == 04 00)", s.enabled == 4 && s.s89 == 0});
    r.checks.push_back({"No teleport pending (+0x3A4 == -1)", s.reqTarget == 0xFFFFFFFF});
    r.checks.push_back({"Request predicate does not block", !s.predicateBlocks});
    return r;
}

Readiness instantiateReadiness(const Snapshot& s, const Pins& pins) {
    Readiness r;
    lighthouseChecks(r, s, pins);
    r.checks.push_back({"Current and pending slice cleared (-1)", s.current == 0xFFFFFFFF && s.pending == 0xFFFFFFFF});
    r.checks.push_back({"Slice 8 exclusively loaded (+0x1114 == 0x100)", s.loaded == 0x100});
    r.checks.push_back({"Transition manager not paused", s.pause == 0});
    r.checks.push_back({"Manager +0x88..8A == 04 06 00", s.enabled == 4 && s.s89 == 6 && s.s8a == 0});
    r.checks.push_back({"Transition worker 4 (+0x1D2)", s.worker == 4});
    r.checks.push_back({"Target slice 8 (manager +0x130, globals +0xA8)", s.target130 == LH_TARGET_SLICE && s.globalsTarget == LH_TARGET_SLICE});
    return r;
}

Readiness bindReadiness(const Snapshot& s, const Pins& pins) {
    Readiness r;
    lighthouseChecks(r, s, pins);
    r.checks.push_back({"Slice 8 instantiated (current slice 8)", s.current == LH_TARGET_SLICE});
    r.checks.push_back({"Local player has an identity", s.identity != std::array<uint8_t, 8>{}});
    r.checks.push_back({"Local player not bound yet (+0x50 / +0x54 == -1)", s.smallEntity == 0xFFFFFFFF && s.smallEntity2 == 0xFFFFFFFF});
    return r;
}

Readiness lifetimeReadiness(const Snapshot& s, const Pins& pins) {
    Readiness r;
    lighthouseChecks(r, s, pins);
    r.checks.push_back({"Slice 8 instantiated", s.current == LH_TARGET_SLICE});
    r.checks.push_back({"Local player bound to an entity (+0x50)", s.bound()});
    return r;
}

 

Inventory readInventory(world::Reader& mem, Resolver& res, uint32_t type) {
    Inventory inv;
    const uint16_t index = mem.bytes(LH_REGISTRY_INDEX, 2).u16(0);
    if (!index) throw Error("No active entity registry");
    const Bytes dir = mem.bytes(LH_ALLOC_DIRECTORY, 0x10);
    const uint32_t table = LH_ALLOC_DIRECTORY + dir.u32(0), stride = dir.u32(8);
    if (!stride || stride > 0x1000) throw Error("The allocator directory is not the verified shape");
    inv.registry = mem.u32(table + uint32_t(index) * stride + 0x30);
    if (!heapPtr(inv.registry, LH_REGISTRY_META + 4 * (1537 + 32))) throw Error("No entity registry (" + hex(inv.registry) + ")");
    const uint32_t meta = mem.u32(inv.registry + LH_REGISTRY_META + 4 * (type + 1537));
    const uint32_t start = meta >> 16, count = meta & 0xFFFF;
    if (count > 256) throw Error(fmt("Unexpected type-%u entity count %u", type, count));
    inv.count = count;
    inv.small = mem.u32(LH_PLAYER_ENTRY_PTR);
    if (heapPtr(inv.small, small::IDENTITY + 8)) {
        const Bytes id = mem.bytes(inv.small + small::IDENTITY, 8);
        for (uint32_t i = 0; i < 8; ++i) inv.identity[i] = id.u8(i);
    }
    if (!count) return inv;
    const Bytes slots = mem.bytes(inv.registry + LH_REGISTRY_META + 2 * (start + 2), 2 * count);
    for (uint32_t k = 0; k < count; ++k) {
        Entity e;
        e.slot = slots.u16(2 * k);
        const Bytes rec = mem.bytes(inv.registry + LH_REGISTRY_RECORDS + LH_REGISTRY_RECORD * e.slot, LH_REGISTRY_RECORD);
        e.handle = rec.u32(4);
        e.offset = rec.u32(8);
        const auto base = res.body(e.handle);
        if (!base) continue;
        e.address = *base + e.offset;
        const Bytes raw = mem.bytes(e.address, entity::READ_SIZE);
        e.boundPlayer = raw.u32(entity::BOUND_PLAYER);
        e.phase = raw.u8(entity::LIFETIME_PHASE);
        if (const auto ref = res.body(raw.u32(entity::IDENTITY_REF))) {
            e.identityAddr = *ref + raw.u32(entity::IDENTITY_REF + 4);
            if (const auto id = mem.tryBytes(e.identityAddr, 8)) {
                for (uint32_t i = 0; i < 8; ++i) e.identity[i] = id->u8(i);
                e.identityKnown = true;
                e.matches = e.identity == inv.identity && inv.identity != std::array<uint8_t, 8>{};
            }
        }
        if (const auto inner = res.body(raw.u32(entity::INNER_REF))) e.innerAddr = *inner + raw.u32(entity::INNER_REF + 4);
        inv.entities.push_back(e);
    }
    return inv;
}

std::vector<Write> fadeWrites(const Bytes& block) {
    std::vector<Write> out;
    for (uint32_t k = 0; k < 10; ++k) {
        const uint32_t o = 16 + k * 48;
        if (!block.has(o + 32)) break;
        if (block.u32(o) != FNV_EMPTY && block.u32(o + 28) != 0) out.push_back({block.addr() + o + 28, {0, 0, 0, 0}});
    }
    out.push_back({block.addr(), {0, 0, 0, 0}});
    return out;
}

}   
