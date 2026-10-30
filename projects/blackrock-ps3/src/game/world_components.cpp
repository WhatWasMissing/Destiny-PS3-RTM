#include "game/world_components.h"

#include <algorithm>

namespace brps3::world {

bool AccessorPool::plausible() const {
    return header.stride == kAccessorStride && header.capacity > 0 && header.capacity <= kAccessorMaxCapacity &&
           heap(header.base, header.capacity * kAccessorStride);
}

AccessorPool parseAccessorPool(const Bytes& b) {
    AccessorPool p;
    p.header = parsePool(b, b.addr());
    p.mask = b.u32(0x24);
    return p;
}

AccessorPool readAccessorPool(Reader& mem) { return parseAccessorPool(mem.bytes(addr::COMPONENT_ACCESSOR_POOL, pool::HEADER_SIZE)); }

Accessor parseAccessor(const AccessorPool& p, const Bytes& e, uint32_t index) {
    Accessor a;
    a.index = index;
    a.addr = p.entry(index);
    a.flags = e.u32(0);
    a.instance = e.u32(4);
    a.backOffset = e.u32(8);
    a.identity = e.u32(0xC);
    a.word14 = e.u32(0x14);
    a.data = a.addr - (a.backOffset & p.mask);
    return a;
}

std::optional<Accessor> resolveComponent(Reader& mem, const AccessorPool& p, uint32_t handle, bool identity) {
    if (!validHandle(handle) || !p.plausible()) return std::nullopt;
    const uint32_t index = handle & 0x1FFF;
    if (index >= p.header.capacity) return std::nullopt;
    const auto e = mem.tryBytes(p.entry(index), kAccessorStride);
    if (!e) return std::nullopt;
    Accessor a = parseAccessor(p, *e, index);
    if (identity && a.identity != handle) return std::nullopt;
    return a;
}

Accessor AccessorTable::at(uint32_t index) const {
    if (index >= count()) throw Error("accessor index " + std::to_string(index) + " is beyond the table");
    return parseAccessor(pool, entries.slice(index * kAccessorStride, kAccessorStride), index);
}

AccessorTable readAccessorTable(Reader& mem) {
    AccessorTable t;
    t.pool = readAccessorPool(mem);
    if (!t.pool.plausible())
        throw Error("the component accessor pool at " + hex8(addr::COMPONENT_ACCESSOR_POOL) + " is not plausible (base " + hex8(t.pool.header.base) +
                    ", stride " + hex8(t.pool.header.stride) + "): not in a game session?");
    t.entries = mem.bytes(t.pool.header.base, t.pool.header.capacity * kAccessorStride);
    return t;
}

namespace {

bool arenaOk(const Accessor& a) { return a.allocated() && a.size() >= 12 && a.size() <= 0x400000 && heap(a.data, a.size()); }

}   

std::vector<Arena> arenaChain(const AccessorTable& t, uint32_t head) {
    std::vector<Arena> out;
    std::vector<bool> seen(t.count(), false);
    for (uint32_t h = head; h != 0xFFFFFFFFu;) {
        const uint32_t index = h & 0x1FFF;
        if (index >= t.count() || seen[index]) return {};
        seen[index] = true;
        const Accessor a = t.at(index);
        if (!arenaOk(a)) return {};
        out.push_back({index, a.data, a.size()});
        h = a.nextArena();
    }
    return out;
}

std::vector<Arena> readArenaChain(Reader& mem, const AccessorPool& p, uint32_t head) {
    std::vector<Arena> out;
    if (!p.plausible()) return out;
    std::vector<bool> seen(p.header.capacity, false);
    for (uint32_t h = head; h != 0xFFFFFFFFu;) {
        const uint32_t index = h & 0x1FFF;
        if (index >= p.header.capacity || seen[index]) return {};
        seen[index] = true;
        const auto e = mem.tryBytes(p.entry(index), kAccessorStride);
        if (!e) return {};
        const Accessor a = parseAccessor(p, *e, index);
        if (!arenaOk(a)) return {};
        out.push_back({index, a.data, a.size()});
        h = a.nextArena();
    }
    return out;
}

std::vector<Accessor> componentsIn(const AccessorTable& t, const std::vector<Arena>& arenas) {
    std::vector<Accessor> out;
    if (arenas.empty()) return out;
    for (uint32_t i = 0; i < t.count(); ++i) {
        if (std::any_of(arenas.begin(), arenas.end(), [&](const Arena& a) { return a.index == i; })) continue;
        const uint32_t flags = t.entries.u32(i * kAccessorStride);
        if ((flags & 0xF0000000u) != kAccessorAllocFlag || (flags & 0x0FFFFFFFu) < 0x20) continue;
        const Accessor a = t.at(i);
        const bool inside = std::any_of(arenas.begin(), arenas.end(),
                                        [&](const Arena& ar) { return a.data >= ar.data && uint64_t(a.data) + a.size() <= uint64_t(ar.data) + ar.size; });
        if (inside) out.push_back(a);
    }
    return out;
}

}   
