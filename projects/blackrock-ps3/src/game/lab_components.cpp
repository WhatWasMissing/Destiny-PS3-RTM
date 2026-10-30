#include "game/lab_components.h"

#include <algorithm>
#include <set>

#include "core/error.h"
#include "game/world_addr.h"
#include "game/world_objects.h"

namespace brps3::lab {

using world::hex8;

const Fingerprints kComponentFingerprints = {
    {kDispatchConsumer38, "F821FF917C0802A6F801008080830000"},
    {kDispatchConsumer58, "F821FF917C0802A6F801008080A30000"},
    {kSpawnerPrimaryComponent, "7C0802A648B42BC9F821FF31F80100E0"},
    {kSecondaryComponent, "7C0802A648B91F95F821FF51F80100C0"},
    {kComponentLookupByType, "7C0802A648C516D9F821F951F80106C0"},
    {kComponentLookupHead, "7C0802A648C51505F821FF61F80100B0"},
};

 

uint32_t canonicalHandle(uint32_t identity, uint32_t mask, uint32_t nameSpace, uint32_t index) {
    const uint32_t gen = identity & mask;
    if (nameSpace & 0x40000000u) return ((((gen << 14) & 0x1C000u) | 0xFFFC0000u | (nameSpace & 0x3FFFu)) << 13) | index;
    return ((((gen << 10) & 0x3FC00u) | (nameSpace & 0x3FFu)) << 13) | index;
}

std::optional<PoolIdentity> poolIdentity(world::Reader& mem, const world::PoolHeader& p, uint32_t index, const world::Bytes* entries) {
     
    if (!p.identityStride || p.identityStride > 0x10000 || p.identityStride % 4 || p.identityOffset % 4 || p.identityOffset > 0x100000 ||
        !p.capacity || index >= p.capacity)
        return std::nullopt;
    const uint64_t a = uint64_t(p.base) + p.identityOffset + uint64_t(index) * p.identityStride;
    if (a > 0xFFFFFFFCull) return std::nullopt;
    PoolIdentity r;
    r.at = uint32_t(a);
    if (entries && r.at >= entries->addr() && entries->has(r.at - entries->addr(), 4)) {
        r.identity = entries->u32(r.at - entries->addr());
    } else {
        if (!world::mapped(r.at, 4)) return std::nullopt;
        const auto b = mem.tryBytes(r.at, 4);
        if (!b) return std::nullopt;
        r.identity = b->u32(0);
    }
    r.canonical = canonicalHandle(r.identity, p.mask, p.nameSpace, index);
    return r;
}

 

uint8_t WordSource::byte(uint32_t at) {
    const uint32_t w = word(at & ~3u);
    return uint8_t(w >> (24 - 8 * (at & 3)));
}

uint32_t BytesSource::word(uint32_t at) {
    if (at % 4 || uint64_t(at) + 4 > b_.size()) throw Error("word +" + hex8(at) + " is outside the " + std::to_string(b_.size()) + "-byte source");
    return uint32_t(b_[at]) << 24 | uint32_t(b_[at + 1]) << 16 | uint32_t(b_[at + 2]) << 8 | b_[at + 3];
}

uint32_t MemorySource::word(uint32_t at) {
    if (at % 4 || uint64_t(at) + 4 > extent_) throw Error("word +" + hex8(at) + " is outside the definition window " + hex8(extent_));
    const uint32_t chunk = at & ~0xFFFu;
    auto it = chunks_.find(chunk);
    if (it == chunks_.end()) {
        const uint32_t n = std::min<uint32_t>(0x1000, extent_ - chunk);
        it = chunks_.emplace(chunk, mem_.read(base_ + chunk, n)).first;
    }
    const std::vector<uint8_t>& b = it->second;
    const uint32_t o = at - chunk;
    if (o + 4 > b.size()) throw Error("short read in the definition window");
    return uint32_t(b[o]) << 24 | uint32_t(b[o + 1]) << 16 | uint32_t(b[o + 2]) << 8 | b[o + 3];
}

 

const char* lookupName(Lookup r) {
    switch (r) {
    case Lookup::Found: return "found";
    case Lookup::Missing: return "missing";
    case Lookup::Malformed: break;
    }
    return "malformed";
}

bool span(uint32_t size, uint32_t at, uint32_t count, uint32_t stride) { return at <= size && stride && count <= (size - at) / stride; }

bool relArray(WordSource& r, uint32_t size, uint32_t field, uint32_t count, uint32_t stride, uint32_t& at) {
    if (!span(size, field, 1, 4)) return false;
    const uint32_t raw = r.word(field);
    const int64_t rel = (raw & 0x80000000u) ? int64_t(raw) - 0x100000000LL : int64_t(raw);
    const int64_t start = int64_t(field) + 16 + rel;
    if (!raw || start < 0 || start > int64_t(size)) return false;
    at = uint32_t(start);
    return !(at % 4) && span(size, at, count, stride);
}

uint32_t lookupSlot(uint32_t seed, uint32_t key, uint32_t buckets) {
    const uint32_t x = (seed ^ key) + 16u * key;
    const uint32_t y = 129u * ((x >> 10) ^ x);
    return ((y >> 13) ^ y) & (buckets - 1);
}

Lookup lookupOffset(WordSource& r, uint32_t size, uint32_t definitionOffset, uint32_t key, uint32_t arenaSize, uint32_t& componentOffset,
                    Descriptor* selected) {
    componentOffset = 0;
    try {
        if (size > 0x4000000u || definitionOffset % 4 || !span(size, definitionOffset, 1, 0x38)) return Lookup::Malformed;
        const uint32_t h = definitionOffset + 0x1C;
        const uint32_t buckets = r.word(h + 4), rows = r.word(h + 0xC);
        if (!buckets || buckets > 8192 || (buckets & (buckets - 1)) || rows > 0x7FFF) return Lookup::Malformed;
        uint32_t bucketBase = 0, descriptors = 0, records = 0;
        if (!relArray(r, size, h + 8, buckets, 8, bucketBase)) return Lookup::Malformed;
        if (rows && (!relArray(r, size, h + 0x10, rows, 28, descriptors) || !relArray(r, size, h + 0x18, rows, 16, records)))
            return Lookup::Malformed;
        uint32_t slot = lookupSlot(r.word(h), key, buckets);
        for (uint32_t probes = 0; probes < buckets; ++probes) {
            const uint32_t bucket = bucketBase + slot * 8;
            const uint32_t range = r.word(bucket + 4), flags = range & 0xFFFF;
            if (r.word(bucket) == key) {
                const uint32_t first = range >> 16, count = flags & 0x7FFF;
                if (!count || first >= rows || count > rows - first) return Lookup::Malformed;
                const uint32_t offset = r.word(records + first * 16 + 12);
                if (offset % 4 || !span(arenaSize, offset, 1, 0x18)) return Lookup::Malformed;
                if (selected) {
                    Descriptor d;
                    d.index = first;
                    for (uint32_t i = 0; i < 7; ++i) d.words[i] = r.word(descriptors + first * 28 + i * 4);
                    *selected = d;
                }
                componentOffset = offset;
                return Lookup::Found;
            }
            if (!(flags & 0x8000)) return Lookup::Missing;
            slot = (slot + 1) & (buckets - 1);
        }
        return Lookup::Malformed;    
    } catch (const std::exception&) {
        return Lookup::Malformed;
    }
}

 

uint32_t windowExtent(uint32_t body, uint32_t cap) {
    if (world::heap(body)) return uint32_t(std::min<uint64_t>(cap, uint64_t(world::kHeapEnd) - body));
    if (world::mapped(body)) return uint32_t(std::min<uint64_t>(cap, uint64_t(world::kEbootDataEnd) - body));
    return 0;
}

namespace {

 
 
std::optional<uint32_t> identityBody(world::Reader& mem, uint32_t tag, std::string& why) {
    const uint32_t mgr = mem.u32(kIdentityManager);
    if (!world::mapped(mgr, 0x14)) {
        why = "identity manager not set";
        return std::nullopt;
    }
    const world::Bytes m = mem.bytes(mgr, 0x14);
    const uint32_t base = m.u32(0), end = m.u32(8), stride = m.u32(0x10);
    const uint32_t page = world::tagPage(tag);
    if (stride != 0x40 || end <= base || (end - base) % 0x40 || page >= (end - base) / 0x40) {
        why = "identity manager table implausible";
        return std::nullopt;
    }
    const uint32_t reg = mem.u32(base + page * 0x40 + 0x18);
    if (!world::mapped(reg, 0x38)) {
        why = "page not registered with the identity manager";
        return std::nullopt;
    }
    const world::Bytes r = mem.bytes(reg, 0x38);
    const uint32_t array = r.u32(8), es = r.u32(0xC), cap = r.u32(0x10), mask = r.u32(0x24);
    const uint32_t index = tag & 0x1FFF;
    if (!cap || cap > 8192 || es < 12 || es > 0x10000 || es % 4 || index >= cap || !world::mapped(array, cap * es)) {
        why = "identity registration implausible";
        return std::nullopt;
    }
    const auto e = mem.tryBytes(array + index * es, 12);
    if (!e) {
        why = "identity entry unreadable";
        return std::nullopt;
    }
     
    if (e->u32(4) & (0x00080000u | 0x20000000u)) {
        why = "identity entry is not a dereferenceable definition (metadata " + hex8(e->u32(4)) + ")";
        return std::nullopt;
    }
    return array + index * es - (e->u32(8) & mask);
}

}   

Definition readDefinition(world::Reader& mem, const world::TagTable& t, uint32_t tag, uint32_t windowCap) {
    Definition d;
    d.tag = tag;
    d.resource = world::resolve(mem, t, tag, false);
    if (!d.resource.ok) {
        d.why = "definition " + hex8(tag) + ": " + d.resource.why;
        return d;
    }
    if (d.resource.item0 == 0 || d.resource.item0 == 0xC0000000u) {
        d.why = "definition " + hex8(tag) + ": the tag entry is empty";
        return d;
    }
    d.body = d.resource.body;
    if (!world::mapped(d.body, 16) || d.body % 4) {
        d.why = "definition " + hex8(tag) + ": body " + hex8(d.body) + " is unmapped";
        d.body = 0;
        return d;
    }
    std::string idWhy;
    try {
        if (auto ib = identityBody(mem, tag, idWhy)) {
            d.identityChecked = true;
            d.identityBody = *ib;
        }
    } catch (const std::exception&) {
         
    }
    d.extent = windowExtent(d.body, windowCap);
    return d;
}

 

const char* anchorName(uint32_t anchor) {
    switch (anchor) {
    case kTypeAnchorPrimary: return "primary (spawner)";
    case kTypeAnchorSecondary: return "secondary";
    case kTypeAnchorEffect: return "effect";
    default: return "custom";
    }
}

TypeKey readTypeKey(world::Reader& mem, const world::TagTable& t, uint32_t anchor) {
    TypeKey k;
    k.anchor = anchor;
    k.descriptor = mem.u32(anchor);
    if (k.descriptor % 4 || !world::ebootData(k.descriptor, 4)) {
        k.why = "the anchor holds " + hex8(k.descriptor) + ", not a descriptor in the EBOOT data segment";
        return k;
    }
    k.handle = mem.u32(k.descriptor);
    if (!world::validHandle(k.handle)) {
        k.why = "no type handle published yet (" + hex8(k.handle) + "): load into an activity";
        return k;
    }
    const world::Resolved r = world::resolve(mem, t, k.handle, false);
    if (!r.ok) {
        k.why = "type handle " + hex8(k.handle) + ": " + r.why;
        return k;
    }
    k.body = r.body;
    if (!world::mapped(k.body + 4, 4)) {
        k.why = "type body " + hex8(k.body) + " is unmapped";
        return k;
    }
    k.key = mem.u32(k.body + 4);
     
    if (mem.u32(anchor) != k.descriptor || mem.u32(k.descriptor) != k.handle || mem.u32(k.body + 4) != k.key)
        k.why = "the type key changed while it was read: try again";
    return k;
}

 

namespace {

OwnerLookup malformed(OwnerLookup o, std::string why) {
    o.result = Lookup::Malformed;
    o.why = std::move(why);
    return o;
}

 
 
 
void checkChild(world::Reader& mem, const world::AccessorTable& table, const ArenaStep& a, OwnerLookup& o) {
    auto fail = [&](std::string why) {
        o.childOk = false;
        if (o.childWhy.empty()) o.childWhy = std::move(why);
    };
    if (a.componentOffset < 20 || a.componentOffset > a.size || 0x20 > a.size - a.componentOffset) return fail("record +" + hex8(a.componentOffset) + " leaves no room for a component header");
    const auto c = mem.tryBytes(o.component + 0x14, 4);
    if (!c) return fail("record " + hex8(o.component) + " is unreadable");
    o.child = c->u32(0);
    const uint32_t index = o.child & 0x1FFF;
    if (!world::validHandle(o.child) || index >= table.count()) return fail("record +0x14 holds " + hex8(o.child) + ", not an accessor handle");
    o.childAccessor = table.at(index);
    const world::Accessor& acc = *o.childAccessor;
    o.childOwner = acc.identity;    
    if (!acc.allocated() || acc.size() < 0x20 || acc.size() > 0x100000) return fail("accessor " + hex8(o.child) + " is not allocated (" + hex8(acc.flags) + ")");
    if (acc.data != o.component) return fail("accessor " + hex8(o.child) + " points at " + hex8(acc.data) + ", not at the record");
    if (acc.size() > a.size - a.componentOffset) return fail("accessor " + hex8(o.child) + " is larger than the rest of its arena");
    const auto target = mem.tryBytes(acc.data + 0x1C, 4);
    if (!target) return fail("the component's target (+0x1C) is unreadable");
    o.childTarget = target->u32(0);
    if (o.childTarget != o.owner) return fail("the component belongs to " + hex8(o.childTarget) + " (+0x1C), not to the owner");
    const auto id = poolIdentity(mem, table.pool.header, index, &table.entries);
    if (!id) return fail("the accessor pool identity table is unreadable");
    o.childCanonical = id->canonical;
    if (!id->matches(o.child)) return fail("the pool identity gives " + hex8(id->canonical) + " for index " + std::to_string(index) + ": stale");
    o.childOk = true;
}

OwnerLookup ownerPass(world::Reader& mem, const world::TagTable& t, uint32_t ownerHandle, uint32_t key, bool headOnly, bool walkAll, OwnerMode mode) {
    OwnerLookup o;
    o.owner = ownerHandle;
    o.key = key;
    o.mode = mode;
    const world::PoolHeader sobjects = world::readPool(mem, world::addr::SOBJECT_POOL);
    const auto obj = world::readSObject(mem, sobjects, ownerHandle);
    if (!obj) return malformed(o, "handle " + hex8(ownerHandle) + " is not a live sObject (stale, out of range or being deleted)");
    o.object = obj->addr;
    o.head = obj->componentHead;
    if (headOnly && o.head == 0xFFFFFFFFu) return malformed(o, "the object has no component head (+0x3C is -1)");
    const world::AccessorTable table = world::readAccessorTable(mem);
    std::set<uint32_t> seen;
    std::map<uint32_t, Definition> defs;
    Lookup result = Lookup::Missing;
     
     
    for (uint32_t current = o.head; current != 0xFFFFFFFFu;) {
        const bool listing = o.found >= 0;
        const uint32_t index = current & 0x1FFF;
        if (index >= table.count() || !seen.insert(index).second) {
            if (listing) break;
            return malformed(o, "the arena chain loops or leaves the accessor table at " + hex8(current));
        }
        const world::Accessor acc = table.at(index);
        ArenaStep s;
        s.handle = current;
        s.index = index;
        s.entry = acc.addr;
        for (uint32_t i = 0; i < 6; ++i) s.words[i] = table.entries.u32(index * world::kAccessorStride + i * 4);
        s.size = acc.size();
        s.body = acc.data;
        std::string bad;    
        if (const auto id = poolIdentity(mem, table.pool.header, index, &table.entries)) {
            s.identity = id->identity;
            s.canonical = id->canonical;
            s.identityOk = id->matches(current);
        }
        if (!s.identityOk) {
            bad = "arena accessor " + hex8(current) + ": the pool identity table gives " + hex8(s.canonical) + " (identity word " + hex8(s.identity) +
                  "), not this handle";
        } else if (!acc.allocated() || s.size < 20 || s.size > 0x400000 || !world::heap(s.body, s.size)) {
            bad = "arena accessor " + hex8(current) + " is not an allocated arena (flags " + hex8(acc.flags) + ")";
        } else if (const auto head = mem.tryBytes(s.body, 12); !head) {
            bad = "arena body " + hex8(s.body) + " is unreadable";
        } else {
            s.flags = head->u32(0);
            s.definition = head->u32(4);
            s.offset = head->u32(8);
            if (headOnly || !(s.flags & 2)) {
                auto it = defs.find(s.definition);
                if (it == defs.end()) it = defs.emplace(s.definition, readDefinition(mem, t, s.definition)).first;
                const Definition& def = it->second;
                if (!def.ok()) {
                    s.result = Lookup::Malformed;
                    s.why = def.why;
                } else {
                    MemorySource src(mem, def.body, def.extent);
                    s.result = lookupOffset(src, def.extent, s.offset, key, s.size, s.componentOffset, &s.descriptor);
                    if (s.result == Lookup::Malformed) s.why = "definition " + hex8(s.definition) + " +" + hex8(s.offset) + ": hash table malformed";
                }
                if (s.result == Lookup::Malformed && !listing) bad = s.why;
            } else {
                s.skipped = true;
            }
            if (bad.empty()) {
                 
                const auto entryAgain = mem.tryBytes(acc.addr, world::kAccessorStride);
                const auto headAgain = mem.tryBytes(s.body, 12);
                bool same = entryAgain && headAgain && headAgain->u32(0) == s.flags && headAgain->u32(4) == s.definition && headAgain->u32(8) == s.offset;
                for (uint32_t i = 0; same && i < 6; ++i) same = entryAgain->u32(i * 4) == s.words[i];
                if (!same) bad = "arena accessor " + hex8(current) + " changed during the walk: try again";
            }
        }
        if (!bad.empty()) {
            s.result = Lookup::Malformed;
            s.why = bad;
            o.arenas.push_back(s);
            if (listing) break;
            return malformed(o, bad);
        }
        if (s.result == Lookup::Found && !listing) {
            o.found = int(o.arenas.size());
            result = Lookup::Found;
        }
        o.arenas.push_back(s);
        if (headOnly || (o.found >= 0 && !walkAll)) break;
        current = acc.nextArena();
    }
    o.result = result;
    if (o.found >= 0) {
        const ArenaStep& a = o.arenas[size_t(o.found)];
        o.component = a.body + a.componentOffset;
        const uint32_t rt = a.descriptor.words[1];
        const bool runtimeOk = rt >= 20 && rt % 4 == 0 && rt <= a.size && 24 <= a.size - rt;
        if (runtimeOk) o.runtime = a.body + rt;
        checkChild(mem, table, a, o);
        if (mode == OwnerMode::Component && !runtimeOk)
            return malformed(o, "descriptor #" + std::to_string(a.descriptor.index) + " runtime offset +" + hex8(rt) + " is outside its arena");
        if (mode == OwnerMode::Effect && !o.childOk) return malformed(o, "the effect's own accessor: " + o.childWhy);
    }
     
    const world::AccessorPool poolAgain = world::readAccessorPool(mem);
    if (!poolAgain.sameLayout(table.pool)) return malformed(o, "the accessor pool moved during the walk: try again");
    const auto again = world::readSObject(mem, sobjects, ownerHandle);
    if (!again || again->addr != o.object || again->componentHead != o.head) return malformed(o, "the object changed during the walk: try again");
    return o;
}

bool sameWalk(const OwnerLookup& a, const OwnerLookup& b) {
    return a.result == b.result && a.object == b.object && a.head == b.head && a.found == b.found && a.arenas == b.arenas &&
           a.component == b.component && a.runtime == b.runtime && a.child == b.child && a.childOk == b.childOk && a.childOwner == b.childOwner &&
           a.childTarget == b.childTarget;
}

}   

OwnerLookup findOwnerComponent(world::Reader& mem, const world::TagTable& t, uint32_t ownerHandle, uint32_t key, bool headOnly, bool walkAll,
                               OwnerMode mode) {
    OwnerLookup first = ownerPass(mem, t, ownerHandle, key, headOnly, walkAll, mode);
    first.passes = 1;
    if (first.result == Lookup::Malformed) return first;
    const OwnerLookup second = ownerPass(mem, t, ownerHandle, key, headOnly, walkAll, mode);
    first.passes = 2;
    if (!sameWalk(first, second)) return malformed(first, "the two walks differ (the chain changed meanwhile): try again");
    return first;
}

 

Dispatch readDispatch(world::Reader& mem, const world::TagTable& t, uint32_t typeTag, uint32_t slot) {
    Dispatch d;
    d.typeTag = typeTag;
    d.slot = slot;
    if (slot != 0x38 && slot != 0x58) {
        d.why = "slot must be 0x38 or 0x58";
        return d;
    }
    d.definition = readDefinition(mem, t, typeTag);
    if (!d.definition.ok()) {
        d.why = d.definition.why;
        return d;
    }
    const uint32_t extent = d.definition.extent;
    if (extent < 0x14) {
        d.why = "the type definition window is too small";
        return d;
    }
    try {
        MemorySource src(mem, d.definition.body, extent);
        d.relative = src.word(0x10);
        const int64_t rel = (d.relative & 0x80000000u) ? int64_t(d.relative) - 0x100000000LL : int64_t(d.relative);
        const int64_t offset = 0x10 + rel;
        if (offset < 0 || offset % 4 || uint64_t(offset) > extent || slot + 8 > extent - uint32_t(offset)) {
            d.why = "the dispatch table (+" + hex8(uint32_t(offset)) + ") is outside the type definition";
            return d;
        }
        d.table = d.definition.body + uint32_t(offset);
        d.fn = src.word(uint32_t(offset) + slot);
        d.adjust = src.word(uint32_t(offset) + slot + 4);
    } catch (const std::exception& e) {
        d.why = e.what();
        return d;
    }
    if (d.isVirtual()) return d;    
    d.opd = d.fn;
    d.opdAligned = d.opd % 8 == 0;
    d.opdInRange = d.opd >= kOpdBegin && uint64_t(d.opd) + 8 <= kOpdEnd;
    if (!d.opdAligned || !d.opdInRange) {
        d.why = "member pointer " + hex8(d.fn) + " is not an OPD (8-aligned in .opd " + hex8(kOpdBegin) + ".." + hex8(kOpdEnd) + ")";
        return d;
    }
    const world::Bytes opd = mem.bytes(d.opd, 8);
    d.code = opd.u32(0);
    d.toc = opd.u32(4);
    d.tocOk = d.toc == kToc;
    d.codeInText = d.code % 4 == 0 && d.code >= world::kTextBegin && uint64_t(d.code) + 16 <= world::kTextEnd;
    if (!d.tocOk || !d.codeInText) {
        d.why = "OPD " + hex8(d.opd) + " -> code " + hex8(d.code) + ", TOC " + hex8(d.toc) + ": not a function of this EBOOT";
        return d;
    }
    const world::Bytes code = mem.bytes(d.code, 16);
    for (uint32_t i = 0; i < 4; ++i) d.instructions[i] = code.u32(i * 4);
    return d;
}

}   
