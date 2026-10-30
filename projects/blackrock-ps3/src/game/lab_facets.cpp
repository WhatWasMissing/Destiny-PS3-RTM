#include "game/lab_facets.h"

#include <algorithm>
#include <set>

#include "core/error.h"
#include "game/world_addr.h"
#include "game/world_objects.h"

namespace brps3::lab {

using world::hex8;

const FacetFingerprints kFacetFingerprints = {
    {kFacetObjectMap, "7C0802A648BB3B49F821FF71F80100A0"},
    {kFacetObjectMapHelper, "3CC001A254A504FE30C6D9E03CE001A2"},
    {kFacetChainUnlink, "7C0802A648BB4991F821FF61F80100B0"},
    {0x1E3100, "7C0802A648BB5911F821FF41F80100D0"},    
    {0x1E5C98, "F821FF817C0802A6F80100903CA0018E"},    
    {0x276968, "F821FF817C0802A6F80100903CA0018E"},    
};

RegistrySnapshot readRegistry(world::Reader& mem) { return RegistrySnapshot{mem.bytes(world::addr::GLOBALS_REGISTRY, kRegistrySnapshotSize)}; }

BlockCheck checkBlock(world::Reader& mem, const RegistrySnapshot& reg, const BlockSpec& spec) {
    BlockCheck c;
    c.slot = spec.slot;
    c.size = spec.size;
    auto fail = [&](std::string s) { c.failures.push_back(std::move(s)); };
    const uint32_t body = mem.u32(spec.slot);
    if (!world::validHandle(body) || body < 16 || !world::heap(body - 16, spec.size + 16)) {
        fail("slot " + hex8(spec.slot) + " holds " + hex8(body) + ": not initialised (load into an activity)");
        return c;
    }
    c.body = body;
    c.header = world::parseBlockHeader(mem.bytes(body - 16, 16));
    const world::BlockHeader& h = c.header;
    const bool cat3 = spec.category == 3;
    if (cat3 ? (h.index < 256 || h.index >= 512) : h.index >= 256) fail("header index " + std::to_string(h.index) + " is outside the category's range");
    if (h.hash != spec.hash) fail("header hash " + hex8(h.hash) + ", expected " + hex8(spec.hash));
    if (h.region >= 8 || (spec.region < 8 && h.region != spec.region)) fail("header region " + std::to_string(h.region) + ", expected " + (spec.region < 8 ? std::to_string(spec.region) : std::string("< 8")));
    if (h.category != spec.category) fail("header category " + std::to_string(h.category) + ", expected " + std::to_string(spec.category));
    if (h.owner != spec.owner) fail("header owner " + std::to_string(h.owner) + ", expected " + std::to_string(spec.owner));
    if (cat3 ? h.flags != 1 : h.flags > spec.flagsMax) fail("header flags " + hex8(h.flags));
    if (!c.failures.empty()) return c;
    uint32_t handle = 0, offset = 0;
    if (cat3) {
        if (reg.count3() > 256 || h.index - 256 >= reg.count3()) fail("registry category-3 count " + std::to_string(reg.count3()) + " does not cover the index");
        handle = reg.raw.u16(kRegistryCat3Handle - world::addr::GLOBALS_REGISTRY);
    } else {
        if (reg.count0() > 256 || h.index >= reg.count0()) fail("registry count " + std::to_string(reg.count0()) + " does not cover the index");
        else {
            handle = reg.recordHandle(h.index);
            offset = reg.recordOffset(h.index);
            if (reg.recordSize(h.index) != spec.size) fail("registry record size " + hex8(reg.recordSize(h.index)) + ", expected " + hex8(spec.size));
            if (handle != reg.cat0Handle()) fail("registry record allocator " + std::to_string(handle) + " is not the category-0 allocator");
        }
    }
    if (!c.failures.empty()) return c;
    if (reg.backReference(h.index) != spec.slot) fail("registry back-reference " + hex8(reg.backReference(h.index)) + " is not the slot");
    const world::Bytes pool = mem.bytes(kAllocContextPool, 0x10);
    const uint32_t base = kAllocContextPool + pool.u32(0), stride = pool.u32(8), cap = pool.u16(0xC);
    if (!handle || handle == 0xFFFF || handle >= cap || stride < 0x44 || stride > 4096 || stride % 4 || !world::mapped(base, cap * stride)) {
        fail("allocation context " + std::to_string(handle) + " is outside the context pool (" + std::to_string(cap) + " x " + hex8(stride) + ")");
        return c;
    }
    c.allocation = base + handle * stride;
    const world::Bytes a = mem.bytes(c.allocation, 0x44);
    c.payload = a.u32(0x30);
    c.extent = a.u32(0x40);
    if (!world::mapped(c.payload, std::max<uint32_t>(c.extent, 1))) fail("allocation payload " + hex8(c.payload) + " is unmapped");
    else if (cat3) {
        if (body - 16 < c.payload || uint64_t(body) + spec.size > uint64_t(c.payload) + c.extent) fail("the block is outside its allocation");
    } else if (offset > c.extent || c.extent - offset < spec.size + 16 || uint64_t(c.payload) + offset != body - 16) {
        fail("payload " + hex8(c.payload) + " + offset " + hex8(offset) + " is not the block header " + hex8(body - 16));
    }
    if (mem.u32(spec.slot) != body) fail("the slot changed while it was read");
    return c;
}

Directory parseDirectory(const world::Bytes& m) {
    Directory d;
    if (!m.has(0x8188)) {
        d.why = "the manager is shorter than its directory";
        return d;
    }
    d.regions = m.u32(0x8000);
    d.total = m.u32(0x8184);
    if (d.regions > 32 || d.total > 750 || d.total > (uint32_t(m.size()) - 0x8188) / 4) {
        d.why = "directory counts implausible (" + std::to_string(d.regions) + " regions, " + std::to_string(d.total) + " heads)";
        return d;
    }
    uint32_t cursor = 0;
    for (uint32_t i = 0; i < d.regions; ++i) {
        DirectoryRegion r;
        r.index = i;
        r.count = m.u32(0x8004 + i * 12);
        r.start = m.u32(0x8008 + i * 12);
        r.handle = m.u32(0x800C + i * 12);
        if (r.start != cursor || r.count > d.total - cursor) {
            d.why = "region " + std::to_string(i) + " does not continue the head list";
            d.rows.clear();
            return d;
        }
        for (uint32_t k = 0; k < r.count; ++k) r.heads.push_back(m.u32(0x8188 + (r.start + k) * 4));
        cursor += r.count;
        d.rows.push_back(std::move(r));
    }
    if (cursor != d.total) {
        d.why = "the regions cover " + std::to_string(cursor) + " of " + std::to_string(d.total) + " heads";
        d.rows.clear();
    }
    return d;
}

FacetRecord parseFacetRecord(const world::Bytes& rec, uint32_t handle, const world::PoolHeader& pool) {
    FacetRecord r;
    r.handle = handle;
    r.index = handle & 0x1FFF;
    r.addr = rec.addr();
    if (pool.identityOffset + 4 <= rec.size()) r.identity = rec.u32(pool.identityOffset);
    r.canonical = canonicalHandle(r.identity, pool.mask, pool.nameSpace, r.index);
    r.identityOk = world::validHandle(handle) && (r.canonical == handle || r.identity == handle);
    r.dirtyPointer = rec.u32(0x90);
    r.dirtyDescriptor = rec.u32(0x94);
    for (uint32_t i = 0; i < 4; ++i) r.dirty[i] = rec.u32(0x98 + i * 4);
    r.self = rec.u32(0xA8);
    r.linked = rec.u32(0xAC);
    r.arena = rec.u32(0xB0);
    r.networkObject = rec.u32(0xF8);
    r.key = rec.u32(0xFC);
    r.marker = rec.u32(0x100);
    r.next = rec.u32(0x104);
    r.flags = rec.u16(0x10C);
    return r;
}

std::optional<FacetRecord> readFacetRecord(world::Reader& mem, const world::PoolHeader& pool, uint32_t handle) {
    if (!world::validHandle(handle) || !pool.plausible()) return std::nullopt;
    const uint32_t index = handle & 0x1FFF;
    if (index >= pool.capacity) return std::nullopt;
    const auto rec = mem.tryBytes(pool.entry(index), std::max<uint32_t>(pool.stride, 0x110));
    if (!rec) return std::nullopt;
    return parseFacetRecord(*rec, handle, pool);
}

std::string facetsPoolProblem(const world::PoolHeader& p) {
     
    if (!p.plausible()) return "the facets pool header (" + hex8(kFacetsPool) + ") is not plausible";
    if (p.stride != kFacetStride || p.capacity != kFacetCapacity || p.identityOffset != kFacetIdentity)
        return "the facets pool is " + std::to_string(p.capacity) + " x " + hex8(p.stride) + " (identity +" + hex8(p.identityOffset) +
               "), expected 512 x 0x120 (identity +0x110)";
    return {};
}

uint32_t FacetManagerView::facetFor(uint32_t mapIndex) const {
    if (mapIndex >= 8192 || !body.has(mapIndex * 4, 4)) return 0xFFFFFFFFu;
    return body.u32(mapIndex * 4);
}

FacetManagerView readFacetManager(world::Reader& mem, const RegistrySnapshot& reg) {
    FacetManagerView m;
    m.check = checkBlock(mem, reg, kManagerSpec);
    m.facets = world::readPool(mem, kFacetsPool);
    if (!m.check.ok()) return m;
    m.body = mem.bytes(m.check.body, kFacetManagerSize);
    m.directory = parseDirectory(m.body);
    return m;
}

const char* routeName(Route r) {
    switch (r) {
    case Route::Disabled: return "Disabled";
    case Route::NoComponent: return "No component";
    case Route::NoNetworkObject: return "No network object";
    case Route::Immediate: return "Immediate";
    case Route::Deferred: return "Deferred";
    case Route::Unknown: break;
    }
    return "Unknown";
}

const char* routeHelp(Route r) {
    switch (r) {
    case Route::Disabled: return "Facet flags bit 0 is clear: a change is not replicated.";
    case Route::NoComponent: return "The components lookup holds FFFF for this arena: nothing to notify.";
    case Route::NoNetworkObject: return "The facet has no network object (+0xF8 is -1).";
    case Route::Immediate: return "The deferred context is in mode 0: the native notifies at once (needs its own validation before any call).";
    case Route::Deferred: return "The change would set the facet's dirty bit and a deferred mark (the mark bitset is unmapped on PS3).";
    case Route::Unknown: break;
    }
    return "Not evaluated.";
}

FacetOfObject readFacetOfObject(world::Reader& mem, const FacetManagerView& m, const RegistrySnapshot& reg, uint32_t objectHandle) {
    FacetOfObject f;
    f.object = objectHandle;
    if (!m.check.ok()) {
        f.why = "the facet manager did not validate";
        return f;
    }
    const auto obj = world::readSObject(mem, objectHandle);
    if (!obj) {
        f.why = "handle " + hex8(objectHandle) + " is not a live sObject";
        return f;
    }
    f.entry = obj->addr;
    f.arena = obj->componentHead;
    if (!world::validHandle(f.arena)) {
        f.why = "the object has no component arena (+0x3C is " + hex8(f.arena) + ")";
        return f;
    }
     
     
    const world::AccessorPool acc = world::readAccessorPool(mem);
    f.domain = mem.u32(world::addr::COMPONENT_ACCESSOR_POOL + 0x28);
    if (!acc.plausible()) {
        f.why = "the component accessor pool is not plausible (not in a session?)";
        return f;
    }
    const auto arenaId = poolIdentity(mem, acc.header, f.arena & 0x1FFF);
    f.arenaCanonical = arenaId ? arenaId->canonical : 0;
    if (!arenaId || !arenaId->matches(f.arena)) {
        f.why = "arena " + hex8(f.arena) + " is stale: the accessor pool identity table gives " + hex8(f.arenaCanonical);
        return f;
    }
    f.mapIndex = ((f.domain & 0x7FFFFu) << 13) | (f.arena & 0x1FFF);
    if (f.mapIndex >= 8192) {
        f.why = "map index " + hex8(f.mapIndex) + " (domain " + hex8(f.domain) + ") is outside the 8192-entry map";
        return f;
    }
    f.facet = m.facetFor(f.mapIndex);
    if (f.facet == 0xFFFFFFFFu) {
        f.why = "no network facet for this object (map entry is -1)";
        return f;
    }
    if (const std::string bad = facetsPoolProblem(m.facets); !bad.empty()) {
        f.why = bad;
        return f;
    }
    f.record = readFacetRecord(mem, m.facets, f.facet);
    if (!f.record) {
        f.why = "facet " + hex8(f.facet) + " is outside the facets pool";
        return f;
    }
    const FacetRecord& r = *f.record;
    if (!r.identityOk) {
        f.why = "facet " + hex8(f.facet) + " is stale (identity " + hex8(r.identity) + ")";
        return f;
    }
     
     
    auto glueFail = [&](std::string why) {
        if (f.glueWhy.empty()) f.glueWhy = std::move(why);
    };
    if (!r.selfOk()) glueFail("+0xA8 self " + hex8(r.self) + " is not the facet");
    if (r.arena != f.arena) glueFail("+0xB0 canonical arena " + hex8(r.arena) + " is not the object's arena");
    if (world::validHandle(r.linked) && (r.linked & 0x1FFF) < acc.header.capacity) {
        f.linked = world::resolveComponent(mem, acc, r.linked, false);    
        if (const auto id = poolIdentity(mem, acc.header, r.linked & 0x1FFF)) {
            f.linkedCanonical = id->canonical;
            f.linkedIdentityOk = id->matches(r.linked);
        }
    }
    if (!f.linked) {
        glueFail("+0xAC linked accessor " + hex8(r.linked) + " does not resolve");
    } else {
        const world::Accessor& l = *f.linked;
        f.linkedOwner = l.identity;
        if (!f.linkedIdentityOk) glueFail("linked accessor " + hex8(r.linked) + " is stale (pool identity gives " + hex8(f.linkedCanonical) + ")");
        if (!l.allocated() || l.size() < 0x44 || l.size() > 0x400000 || !world::heap(l.data, l.size()))
            glueFail("linked accessor " + hex8(r.linked) + " is not an allocated 0x44+ byte block (" + hex8(l.flags) + ")");
        else if (const auto g = mem.tryBytes(l.data, 0x24)) {
            f.glueArena = g->u32(0x14);
            f.glueOwner = g->u32(0x1C);
            f.glueFacet = g->u32(0x20);
            if (f.linkedOwner != r.arena) glueFail("linked accessor owner (entry +0xC) " + hex8(f.linkedOwner) + " is not the canonical arena");
            if (f.glueArena != r.arena) glueFail("linked +0x14 " + hex8(f.glueArena) + " is not the canonical arena");
            if (f.glueOwner != objectHandle) glueFail("linked +0x1C " + hex8(f.glueOwner) + " is not the object");
            if (f.glueFacet != f.facet) glueFail("linked +0x20 " + hex8(f.glueFacet) + " is not the facet");
        } else {
            glueFail("linked accessor data " + hex8(l.data) + " is unreadable");
        }
    }
    f.glueOk = f.glueWhy.empty();
    if (!f.glueOk) {
        f.why = "glue: " + f.glueWhy;
        return f;
    }
    if (!(r.flags & 1)) {
        f.route = Route::Disabled;
        return f;
    }
    const BlockCheck lookup = checkBlock(mem, reg, kLookupSpec);
    if (!lookup.ok()) {
        f.why = "components lookup: " + lookup.failures.front();
        return f;
    }
    f.lookupValue = mem.bytes(lookup.body + (r.arena & 0x1FFF) * 2, 2).u16(0);
    if (f.lookupValue == 0xFFFF) {
        f.route = Route::NoComponent;
        return f;
    }
    if (f.lookupValue > 0x7FFF) {
        f.why = "components lookup value " + hex8(f.lookupValue) + " is negative";
        return f;
    }
    const DeferredContext ctx = readDeferredContext(mem, reg);
    if (!ctx.check.ok()) {
        f.why = "deferred context: " + ctx.check.failures.front();
        return f;
    }
    f.contextMode = ctx.mode;
    if (r.networkObject == 0xFFFFFFFFu) f.route = Route::NoNetworkObject;
    else if (!(ctx.mode & 7)) f.route = Route::Immediate;
    else {
         
         
        if (f.lookupValue >= 128) {
            f.why = "components lookup value " + std::to_string(f.lookupValue) + " is outside the 128-bit dirty storage";
            return f;
        }
        if (!r.dirtyPointerOk() || !r.descriptorOk()) {
            f.why = "the dirty storage did not validate (+0x90 " + hex8(r.dirtyPointer) + ", +0x94 " + hex8(r.dirtyDescriptor) + ")";
            return f;
        }
        f.route = Route::Deferred;
        f.dirtyWord = f.lookupValue / 32;
        f.dirtyBit = f.lookupValue & 31;
    }
    return f;
}

std::vector<ChainNode> walkChain(world::Reader& mem, const world::PoolHeader& facets, uint32_t head, std::string& why) {
    std::vector<ChainNode> out;
    why = facetsPoolProblem(facets);
    if (!why.empty()) return out;
     
    const world::Bytes all = mem.bytes(facets.base, facets.capacity * facets.stride);
    std::set<uint32_t> seen;
    for (uint32_t h = head; h != 0xFFFFFFFFu;) {
        const uint32_t index = h & 0x1FFF;
        if (index >= facets.capacity) {
            why = "handle " + hex8(h) + " is outside the pool";
            break;
        }
        if (!seen.insert(index).second) {
            why = "the chain loops at " + hex8(h);
            break;
        }
        if (out.size() >= 512) {
            why = "more than 512 nodes";
            break;
        }
        const FacetRecord r = parseFacetRecord(all.slice(index * facets.stride, facets.stride), h, facets);
        out.push_back({h, r.next, r.identityOk});
        if (!r.identityOk) {
            why = "node " + hex8(h) + " is stale";
            break;
        }
        h = r.next;
    }
    return out;
}

std::vector<ActivityBundle> readActivityBundles(world::Reader& mem, std::string& why) {
    std::vector<ActivityBundle> out;
    why.clear();
    const world::PoolHeader p = world::readPool(mem, kActivityBundlesPool);
    if (!p.plausible() || p.capacity > 64 || p.stride < 8) {
        why = "the activity bundles pool is not plausible (stride " + hex8(p.stride) + ", capacity " + std::to_string(p.capacity) + ")";
        return out;
    }
    const world::Bytes all = mem.bytes(p.base, p.capacity * p.stride);
    for (uint32_t i = 0; i < p.capacity; ++i) {
        ActivityBundle b;
        b.index = i;
        b.key = all.u32(i * p.stride);
        b.region = all.u32(i * p.stride + 4);
        if (p.identityOffset + 4 <= p.stride) b.identity = all.u32(i * p.stride + p.identityOffset);
        b.handle = canonicalHandle(b.identity, p.mask, p.nameSpace, i);
        out.push_back(b);
    }
    return out;
}

DeferredContext readDeferredContext(world::Reader& mem, const RegistrySnapshot& reg) {
    DeferredContext d;
    d.check = checkBlock(mem, reg, kDeferredSpec);
    if (!d.check.ok()) return d;
    const world::Bytes b = mem.bytes(d.check.body, 12);
    d.mode = b.u8(0);
    d.context = b.u32(4);
    d.state = b.u8(8);
    return d;
}

}   
