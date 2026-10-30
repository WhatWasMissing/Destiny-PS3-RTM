#include "game/movement_body.h"

#include <algorithm>

#include "core/error.h"
#include "game/movement_addr.h"
#include "game/world_addr.h"

namespace brps3::movement {

namespace {

using world::Bytes;
using world::hex8;
using world::Reader;

 
 
uint32_t d1Resolve(Reader& mem, const world::TagTable& t, uint32_t h) {
    if (!world::validHandle(h)) return 0;
    const uint32_t page = world::tagPage(h);
    if (page >= t.pageCount) return 0;
    const auto rec = mem.tryBytes(t.base + page * t.pageStride, 0x38);
    if (!rec) return 0;
    const uint32_t items = rec->u32(0x10), stride = rec->u32(0x30), mask = rec->u32(0x34);
    if (!items || !stride || stride > 0x10000 || !world::mapped(items)) return 0;
    const uint32_t elem = items + world::tagIndex(h) * stride;
    const auto e = mem.tryBytes(elem, 0xC);
    if (!e) return 0;
    const uint32_t back = e->u32(8) & mask;
    return back <= elem ? elem - back : 0;
}

 
uint32_t resolvePointer(Reader& mem, const world::TagTable& t, uint32_t h, bool tagChecks, std::string& note, const char* what) {
    const world::Resolved r = world::resolve(mem, t, h, tagChecks);
    if (r.ok) return r.body;
    const uint32_t d1 = d1Resolve(mem, t, h);
    if (d1) note += std::string(what) + ": strict resolve refused (" + r.why + "), the Xbox resolve gave " + hex8(d1) + ". ";
    else note += std::string(what) + ": " + r.why + ". ";
    return d1;
}

}   

BodyChain resolveBody(Reader& mem, const world::LocalPlayer& p) { return resolveBody(mem, world::readTagTable(mem), p); }

BodyChain resolveBody(Reader& mem, const world::TagTable& t, const world::LocalPlayer& p) {
    BodyChain c;
    std::string note;
    auto fail = [&](std::string why) {
        c.ok = false;
        c.why = note.empty() ? why : why + " (" + note + ")";
        return c;
    };
    c.arenaHandle = p.object.componentHead;
    if (!world::validHandle(c.arenaHandle)) return fail("the player sObject +0x3C holds no arena handle (" + hex8(c.arenaHandle) + ")");
    c.arena = resolvePointer(mem, t, c.arenaHandle, false, note, "arena");
    if (!c.arena || (c.arena & 3) || !world::heap(c.arena, 8)) return fail("arena handle " + hex8(c.arenaHandle) + " does not resolve to a heap block");
    const auto arenaHead = mem.tryBytes(c.arena, 8);
    if (!arenaHead) return fail("arena " + hex8(c.arena) + " is unreadable");
    c.defTag = arenaHead->u32(chain::ARENA_DEF_TAG);
    c.def = resolvePointer(mem, t, c.defTag, true, note, "definition");
    if (!c.def || (c.def & 3) || !world::mapped(c.def, chain::DEF_OFFSET + 4))
        return fail("definition tag " + hex8(c.defTag) + " (arena +4) does not resolve");
    const auto defBytes = mem.tryBytes(c.def + chain::DEF_COUNT, chain::DEF_OFFSET + 4 - chain::DEF_COUNT);
    if (!defBytes) return fail("definition " + hex8(c.def) + " +0x60..+0x94 is unreadable");
    c.count = defBytes->u32(0);
    c.offset = defBytes->u32(chain::DEF_OFFSET - chain::DEF_COUNT);
    if (c.count <= chain::PHYSICS_INDEX || c.count > chain::MAX_COUNT)
        return fail("definition +0x60 component count " + std::to_string(c.count) + " is not 3..64");
    if (int32_t(c.offset) < 0 || (c.offset & 3) || c.offset > chain::MAX_OFFSET)
        return fail("definition +0x90 component offset " + hex8(c.offset) + " is not a positive 4-aligned offset");
    c.component = c.arena + c.offset;
    c.physicsSlot = c.component + chain::COMP_PHYSICS;
    if (!world::heap(c.component, chain::COMP_FLAG + 1)) return fail("component " + hex8(c.component) + " is outside the heap");
    const auto comp = mem.tryBytes(c.physicsSlot, chain::COMP_FLAG + 1 - chain::COMP_PHYSICS);
    if (!comp) return fail("component " + hex8(c.component) + " +0xF8..+0x174 is unreadable");
    c.physics = comp->u32(0);
    c.entryCount = comp->u32(chain::COMP_COUNT - chain::COMP_PHYSICS);
    c.componentFlag = comp->u8(chain::COMP_FLAG - chain::COMP_PHYSICS);
    if (!taskHeap(c.physics)) return fail("physics entries pointer " + hex8(c.physics) + " (component +0xF8) is not a heap pointer");
    const auto entry = mem.tryBytes(c.physics + chain::PHYSICS_BODY, 4);
    if (!entry) return fail("physics entry " + hex8(c.physics) + " +0x20 is unreadable");
    c.body = entry->u32(0);
    if (!taskHeap(c.body) || (c.body & 0xF)) return fail("body pointer " + hex8(c.body) + " is not a 16-aligned heap pointer");
    const auto body = mem.tryBytes(c.body, hk::BODY_READ);
    if (!body) return fail("body " + hex8(c.body) + " is unreadable");
    c.bodyVptr = body->u32(0);
    c.bodyWorld = body->u32(hk::BODY_WORLD);
    c.motionVptr = body->u32(hk::BODY_MOTION);
    c.motionType = body->u8(hk::BODY_MOTION_TYPE);
    c.xform = body->vec3(hk::BODY_XFORM_POS);
    c.com1 = body->vec3(hk::BODY_COM1);
    c.linvel = body->vec3(hk::BODY_LINVEL);
    if (c.bodyVptr != addr::VT_RIGID_BODY) return fail("body " + hex8(c.body) + " vptr " + hex8(c.bodyVptr) + " is not hkpRigidBody (013E1F38)");
    if (!c.com1.finite()) return fail("body " + hex8(c.body) + " +0x140 (com1) is not finite");
    c.ok = true;
    c.why = note;
    return c;
}

LinkResult resolveLink(Reader& mem) {
    LinkResult r;
    const world::PlayerLookup look = world::findLocalPlayer(mem);
    if (!look.player) {
        r.noPlayer = look.status == world::PlayerStatus::NoDescriptor || look.status == world::PlayerStatus::NotSpawned;
        r.message = look.message;
        return r;
    }
    const world::LocalPlayer& p = *look.player;
    if (!p.playerEntry) {
        r.message = "The player array has no row for handle " + hex8(p.handle) + " (forward / position unavailable).";
        return r;
    }
    r.chain = resolveBody(mem, p);
    if (!r.chain.ok) {
        r.message = "Physics body not found: " + r.chain.why;
        return r;
    }
     
    if (!world::stillValid(mem, p) || mem.u32(p.playerEntry + world::player::ENTRY_HANDLE) != p.handle) {
        r.noPlayer = true;
        r.message = "The player changed while the body was resolved (respawn or load): retrying.";
        return r;
    }
    const auto again = mem.tryBytes(r.chain.physicsSlot, 4);
    if (!again || again->u32(0) != r.chain.physics) {
        r.message = "The physics component changed while it was resolved: retrying.";
        return r;
    }
    r.link.handle = p.handle;
    r.link.element = p.element;
    r.link.entry = p.entry;
    r.link.row = p.playerEntry;
    r.link.body = r.chain.body;
    r.link.physicsSlot = r.chain.physicsSlot;
    r.ok = true;
    return r;
}

IslandScan scanIslands(Reader& mem, uint32_t selfBody, uint32_t maxBodies, uint32_t maxEntities) {
    IslandScan s;
    s.world = mem.u32(addr::HKP_WORLD_PTR);
    if (!taskHeap(s.world)) {
        s.why = "no physics world (*(01897CC8) = " + hex8(s.world) + "): load into an activity";
        return s;
    }
    const Bytes w = mem.bytes(s.world, hk::WORLD_ISLANDS_INACTIVE + 0xC);
    s.worldVptr = w.u32(0);
    if (s.worldVptr != addr::VT_HKP_WORLD) {
        s.why = "world " + hex8(s.world) + " vptr " + hex8(s.worldVptr) + " is not hkpWorld (013E1640)";
        return s;
    }
    s.worldOk = true;
    uint32_t bodyReads = 0;
    const uint32_t arrays[2] = {hk::WORLD_ISLANDS_ACTIVE, hk::WORLD_ISLANDS_INACTIVE};
    for (int k = 0; k < 2; ++k) {
        const uint32_t data = w.u32(arrays[k]), n = w.u32(arrays[k] + 4);
        s.islands[k] = n;
        if (!n || n > hk::MAX_ISLANDS || !taskHeap(data)) continue;
        const auto isl = mem.tryBytes(data, n * 4);
        if (!isl) continue;
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t island = isl->u32(i * 4);
            if (!taskHeap(island)) continue;
            const auto head = mem.tryBytes(island, hk::ISLAND_ENTITY_COUNT + 4);
            if (!head || head->u32(0) != addr::VT_ISLAND) continue;
            ++s.islandsOk;
            const uint32_t ents = head->u32(hk::ISLAND_ENTITIES), m = head->u32(hk::ISLAND_ENTITY_COUNT);
            if (!m || m > hk::MAX_ENTITIES || !taskHeap(ents)) continue;
            const uint32_t take = std::min(m, maxEntities > s.entities ? maxEntities - s.entities : 0u);
            if (!take) continue;
            const auto list = mem.tryBytes(ents, take * 4);
            if (!list) continue;
            s.entities += take;
            for (uint32_t j = 0; j < take; ++j) {
                const uint32_t body = list->u32(j * 4);
                if (!body) continue;
                if (body == selfBody) {
                    s.containsSelf = true;
                    continue;
                }
                if (!taskHeap(body) || (body & 0xF) || bodyReads >= maxBodies) continue;
                ++bodyReads;
                const auto b = mem.tryBytes(body, hk::BODY_COM1 + 12);
                if (!b) continue;
                if (b->u32(0) != addr::VT_RIGID_BODY) {
                    ++s.otherVptr;
                    continue;
                }
                const uint32_t x = b->u32(hk::BODY_COM1), y = b->u32(hk::BODY_COM1 + 4), z = b->u32(hk::BODY_COM1 + 8);
                if (!finiteBits(x) || !finiteBits(y) || !finiteBits(z) || !((x | y | z) & 0x7FFFFFFFu)) continue;
                ++s.bodies;
                if (s.sample.size() < 8) s.sample.push_back(body);
            }
        }
    }
    return s;
}

}   
