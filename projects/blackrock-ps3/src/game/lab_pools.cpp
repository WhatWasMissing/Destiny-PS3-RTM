#include "game/lab_pools.h"

#include <algorithm>
#include <cctype>

#include "core/error.h"
#include "game/world_addr.h"

namespace brps3::lab {

using world::hex8;

namespace {

bool sameName(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

}   

std::string PoolView::verdict() const {
    if (!read) return "not read";
    if (!plausible) return "header not plausible (not registered yet?)";
    if (!strideOk) return "stride " + hex8(header.stride) + " does not fit elem " + hex8(info.elemSize);
    if (!capacityOk) return "capacity " + std::to_string(header.capacity) + ", registered " + std::to_string(info.capacity);
    if (!runtimeName.empty() && !nameOk) return "runtime name \"" + runtimeName + "\"";
    return "ok";
}

PoolView parsePoolView(const world::PoolInfo& info, const world::Bytes& b) {
    PoolView v;
    v.info = info;
    v.read = true;
    v.header = world::parsePool(b, info.ps3);
    v.plausible = v.header.plausible();
    v.strideOk = world::strideMatches(v.header.stride, info.elemSize, info.align, v.header.identityOffset);
    v.capacityOk = info.capacity == 0 || v.header.capacity == info.capacity;
    const uint32_t o = info.ps3 - b.addr();
    if (b.has(o + 0x3D, 1)) v.runtimeName = b.cstr(o + 0x3D, std::min<uint32_t>(0x40, uint32_t(b.size()) - (o + 0x3D)));
    v.nameOk = !v.runtimeName.empty() && sameName(v.runtimeName, info.name);
    return v;
}

PoolView readPoolView(world::Reader& mem, const world::PoolInfo& info) { return parsePoolView(info, mem.bytes(info.ps3, kPoolReadSize)); }

std::vector<PoolView> readPoolViews(world::Reader& mem, const std::vector<world::PoolInfo>& infos, const std::function<void(size_t, size_t)>& progress,
                                    const std::function<bool()>& cancelled) {
    std::vector<size_t> order(infos.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return infos[a].ps3 < infos[b].ps3; });
     
    std::vector<std::pair<size_t, size_t>> clusters;    
    for (size_t i = 0; i < order.size();) {
        size_t j = i + 1;
        while (j < order.size() && infos[order[j]].ps3 + kPoolReadSize - infos[order[i]].ps3 <= 0x4000) ++j;
        clusters.push_back({i, j});
        i = j;
    }
    std::vector<PoolView> out(infos.size());
    for (size_t c = 0; c < clusters.size(); ++c) {
        if (cancelled && cancelled()) break;
        const auto [a, b] = clusters[c];
        const uint32_t start = infos[order[a]].ps3, end = infos[order[b - 1]].ps3 + kPoolReadSize;
        try {
            const world::Bytes chunk = mem.bytes(start, end - start);
            for (size_t k = a; k < b; ++k) out[order[k]] = parsePoolView(infos[order[k]], chunk.slice(infos[order[k]].ps3 - start, kPoolReadSize));
        } catch (const std::exception&) {
            for (size_t k = a; k < b; ++k) {
                out[order[k]].info = infos[order[k]];
                if (const auto one = mem.tryBytes(infos[order[k]].ps3, kPoolReadSize)) out[order[k]] = parsePoolView(infos[order[k]], *one);
            }
        }
        if (progress) progress(c + 1, clusters.size());
    }
    for (size_t i = 0; i < out.size(); ++i)
        if (!out[i].read) out[i].info = infos[i];
    return out;
}

EntryDump readEntries(world::Reader& mem, const world::PoolHeader& h, uint32_t first, uint32_t count, uint32_t maxBytes) {
    if (!h.plausible()) throw Error("the pool header at " + hex8(h.addr) + " is not plausible");
    if (first >= h.capacity) throw Error("entry " + std::to_string(first) + " is beyond the capacity " + std::to_string(h.capacity));
    EntryDump d;
    d.first = first;
    d.stride = h.stride;
    d.base = h.base;
    const uint32_t fit = std::max<uint32_t>(1, maxBytes / h.stride);
    d.count = std::min({std::max<uint32_t>(count, 1), fit, h.capacity - first});
    d.bytes = mem.bytes(h.entry(first), d.count * h.stride);
    for (uint32_t i = 0; i < d.count; ++i) {
         
        if (h.identityStride == h.stride && h.identityOffset + 4 <= h.stride) d.identity.push_back(d.bytes.u32(i * h.stride + h.identityOffset));
        else d.identity.push_back(std::nullopt);
    }
    return d;
}

std::string GlobalView::verdict() const {
    if (!read) return "not read";
    if (!block.initialised) return block.block ? "slot holds " + hex8(block.block) + " (not a block)" : "not initialised";
    if (!registered) return "block at " + hex8(block.block) + ", registry back-reference missing";
    return "ok";
}

std::vector<uint32_t> readRegistryRefs(world::Reader& mem) {
    const world::Bytes b = mem.bytes(world::addr::GLOBALS_REGISTRY + 0x1034, 512 * 8);
    std::vector<uint32_t> out(512);
    for (uint32_t i = 0; i < 512; ++i) out[i] = b.u32(i * 8);
    return out;
}

GlobalView readGlobalView(world::Reader& mem, const world::GlobalInfo& info, const std::vector<uint32_t>& backRefs, uint32_t dumpBytes) {
    GlobalView v;
    v.info = info;
    v.block = world::readGlobal(mem, info.slot);
    v.read = true;
    if (!v.block.initialised) return v;
    v.registered = v.block.header.index < backRefs.size() && backRefs[v.block.header.index] == info.slot;
    const uint32_t n = std::min<uint32_t>(dumpBytes, info.size ? info.size : 0x40);
    if (n && world::mapped(v.block.block, n))
        if (const auto b = mem.tryBytes(v.block.block, n)) v.data = b->data();
    return v;
}

}   
