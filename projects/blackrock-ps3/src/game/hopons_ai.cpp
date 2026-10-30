#include "game/hopons_ai.h"

#include <algorithm>
#include <cstring>
#include <iterator>

#include "core/error.h"
#include "game/hopons_addr.h"

namespace brps3::hopons {

const char* checkStatusText(CheckLine::Status s) {
    switch (s) {
    case CheckLine::Ok: return "ok";
    case CheckLine::Warn: return "check";
    case CheckLine::Fail: return "FAIL";
    case CheckLine::Info: return "info";
    }
    return "?";
}

 

bool placementAlignment(uint32_t a) { return a && a <= 128 && !(a & (a - 1)); }

bool placementAllocate(uint32_t base, uint32_t size, uint32_t alignment, bool prefix, uint32_t& used, uint32_t& offset) {
    if (!placementAlignment(alignment) || size > 2048 || used > 2048) return false;
    const uint32_t start = base + used + (prefix ? 4 : 0);
    const uint32_t aligned = (start + alignment - 1) & ~(alignment - 1);
    offset = aligned - base;
    if (offset > 2048 || size > 2048 - offset) return false;
    used = offset + size;
    return true;
}

bool aiContextLayout(uint32_t arenaBase, uint32_t alreadyUsed, uint32_t componentSize, uint32_t componentAlignment, uint32_t payloadSize,
                     uint32_t payloadAlignment, bool reuseComponent, uint32_t existingComponentOffset, AiContextLayout& out) {
    if (arenaBase > 0xFFFFFFFFu - 2048u - 4u - 127u || alreadyUsed > 2048 || componentSize < 12 || componentSize > 2048 || payloadSize < 0x30 ||
        payloadSize > 2048 || !placementAlignment(payloadAlignment))
        return false;
    AiContextLayout r;
    r.used = alreadyUsed;
    r.copyComponentDefaults = !reuseComponent;
    if (reuseComponent) {
        if (existingComponentOffset > alreadyUsed || componentSize > alreadyUsed - existingComponentOffset) return false;
        r.componentOffset = existingComponentOffset;
    } else if (!placementAllocate(arenaBase, componentSize, componentAlignment, true, r.used, r.componentOffset)) {
        return false;
    }
    if (!placementAllocate(arenaBase, payloadSize, payloadAlignment, true, r.used, r.payloadOffset)) return false;
    out = r;
    return true;
}

bool payloadBudget(uint32_t headerSize, uint32_t headerAlignment, uint32_t payloadSize, uint32_t payloadAlignment, uint32_t& maximumUsed) {
    maximumUsed = 0;
    if (headerSize < 128 || !placementAlignment(headerAlignment) || payloadSize < 0x30 || !placementAlignment(payloadAlignment)) return false;
    for (uint32_t residue = 0; residue < 8; ++residue) {
        const uint32_t base = 8 + 16 * residue;    
        uint32_t used = 0, ignored = 0, offset = 0;
        if (!placementAllocate(base, headerSize, headerAlignment, false, used, ignored)) return false;
        if (!placementAllocate(base, payloadSize, payloadAlignment, true, used, offset)) return false;
        maximumUsed = std::max(maximumUsed, used);
    }
    return true;
}

 

std::optional<TypeDefinition> readTypeDefinition(Reader& mem, TagReader& tags, uint32_t handle, std::string& why) {
    const auto m = tags.map(handle, 0, &why);
    if (!m) return std::nullopt;
    TypeDefinition d;
    d.handle = handle;
    d.body = m->body;
    const auto b = mem.tryBytes(m->body, 0x18);
    if (!b) {
        why = "definition body " + hex8(m->body) + " is unreadable";
        return std::nullopt;
    }
    d.key = b->u32(4);
    d.size = b->u32(0x10);
    d.alignment = b->u8(0x14);
    return d;
}

std::optional<TagHeapInfo> readTagHeap(Reader& mem, std::string& why) {
    TagHeapInfo h;
    h.heap = mem.u32(addr::TAG_HEAP_PTR);
    h.handle = mem.bytes(addr::TAG_HEAP_HANDLE, 2).u16(0);
    if (!storage(h.heap, 0x90)) {
        why = "tag heap pointer *(" + hex8(addr::TAG_HEAP_PTR) + ") = " + hex8(h.heap) + " is not set up";
        return std::nullopt;
    }
    const Bytes b = mem.bytes(h.heap, 0x90);
    h.base = b.u32(0x30);
    h.size = b.u32(0x40);
    h.head = b.u32(0x60);
    h.tail = b.u32(0x64);
    h.headerSize = b.u8(0x84) ? 32 : 16;
    if (h.base % 16 || !h.size || !storage(h.base, h.size)) {
        why = "tag heap +0x30 base " + hex8(h.base) + " / +0x40 size " + hexShort(h.size) + " are not plausible (offsets unverified on PS3)";
        return std::nullopt;
    }
    return h;
}

std::vector<FixedPoolInfo> readFixedPools(Reader& mem) {
    std::vector<FixedPoolInfo> out;
    const Bytes rows = mem.bytes(addr::FIXED_TAG_POOLS, addr::FIXED_TAG_POOL_ROWS * 16);
    for (uint32_t i = 0; i < addr::FIXED_TAG_POOL_ROWS; ++i) {
        FixedPoolInfo f;
        f.row = addr::FIXED_TAG_POOLS + i * 16;
        f.pool = rows.u32(i * 16);
        f.handle = rows.u16(i * 16 + 4);
        f.count = rows.u32(i * 16 + 8);
        f.fraction = rows.f32(i * 16 + 0xC);
        if (f.pool && storage(f.pool, 0x80)) {
            if (const auto p = mem.tryBytes(f.pool, 0x80)) {
                f.allocation = p->u32(0x30);
                f.size = p->u32(0x40);
                f.base = p->u32(0x60);
                f.bitmap = p->u32(0x68);
                f.stride = p->u32(0x70);
                f.capacity = p->u16(0x7A);
                f.plausible = f.allocation % 4 == 0 && f.size && storage(f.allocation, f.size) && f.capacity && f.stride >= 4 && f.stride <= 0x100000 &&
                              f.stride % 4 == 0 && f.base >= f.allocation && uint64_t(f.base) + uint64_t(f.capacity) * f.stride <= uint64_t(f.allocation) + f.size;
            }
        }
        out.push_back(f);
    }
    return out;
}

Extent definitionExtent(Reader& mem, uint32_t body, uint32_t tag) {
    Extent e;
    for (const FixedPoolInfo& f : readFixedPools(mem)) {
        if (!f.plausible || body < f.allocation || uint64_t(body) >= uint64_t(f.allocation) + f.size) continue;
        if (body < f.base || (body - f.base) % f.stride || (body - f.base) / f.stride >= f.capacity) {
            e.why = "the body is inside fixed pool " + hex8(f.pool) + " but not on a slot boundary";
            return e;
        }
        const uint32_t index = (body - f.base) / f.stride;
        const auto bm = mem.tryBytes(f.bitmap, 12);
        if (bm && storage(bm->u32(8), bm->u32(0))) {
            const uint32_t word = mem.u32(bm->u32(8) + (index >> 5) * 4);
            if (!(word & (1u << (index & 31)))) {
                e.why = "fixed pool slot " + std::to_string(index) + " is not allocated";
                return e;
            }
        }
        e.ok = true;
        e.extent = f.stride;
        e.source = "fixed tag pool " + hex8(f.pool) + " slot " + std::to_string(index);
        return e;
    }
    std::string why;
    const auto heap = readTagHeap(mem, why);
    if (!heap) {
        e.why = why;
        return e;
    }
    if (body % 16 || body < heap->base || uint64_t(body) >= uint64_t(heap->base) + heap->size) {
        e.why = "the body is in neither a fixed tag pool nor the variable tag heap";
        return e;
    }
    const uint32_t block = body - heap->headerSize;
    const Bytes h = mem.bytes(block, 16);
    const uint32_t word = h.u32(0), length = word >> 4, prev = h.u32(12);
    if (length < heap->headerSize || length % 16 || block - heap->base > heap->size || length > heap->size - (block - heap->base) || (word & 8) ||
        h.u32(4) != tag) {
        e.why = "the heap block header below the body does not describe this tag";
        return e;
    }
    if (prev) {
        if (prev % 16 || prev >= heap->size) {
            e.why = "the block's previous link is not plausible";
            return e;
        }
        if (mem.u32(heap->base + prev + 8) != block - heap->base) {
            e.why = "the previous heap block does not link to this one";
            return e;
        }
    } else if (heap->head != block - heap->base) {
        e.why = "the block has no previous link but is not the heap head";
        return e;
    }
    e.ok = true;
    e.extent = length - heap->headerSize;
    e.source = "variable tag heap block " + hex8(block) + " (light check: previous link only)";
    return e;
}

 

AiPreflight runAiPreflight(Reader& mem) {
    AiPreflight p;
    auto add = [&](CheckLine::Status s, std::string label, std::string detail) {
        p.lines.push_back({s, std::move(label), std::move(detail)});
        if (s == CheckLine::Ok) ++p.ok;
        if (s == CheckLine::Warn) ++p.warnings;
        if (s == CheckLine::Fail) ++p.failures;
    };
    TagReader tags(mem);
    std::string why;
     
    static const uint32_t classHandles[] = {cls::PLACEMENT, cls::ENTITY, cls::ENTITY_MARKER, 0x80800477};
    for (size_t i = 0; i < std::size(kClassAnchors); ++i) {
        const Anchor& a = kClassAnchors[i];
        const uint32_t v = mem.u32(a.slot);
        const uint32_t h = v == a.expect ? mem.u32(v) : 0;
        add(v == a.expect && h == classHandles[i] ? CheckLine::Ok : CheckLine::Fail, a.what,
            "*(" + hex8(a.slot) + ") = " + hex8(v) + " (expect " + hex8(a.expect) + "), record " + hex8(h));
    }
     
    for (const Anchor& a : kPlacementAnchors) {
        const uint32_t v = mem.u32(a.slot);
        if (v != a.expect) {
            add(CheckLine::Fail, a.what, "*(" + hex8(a.slot) + ") = " + hex8(v) + ", expected " + hex8(a.expect));
            continue;
        }
        const uint32_t handle = mem.u32(v);
        const auto def = readTypeDefinition(mem, tags, handle, why);
        add(def ? CheckLine::Ok : CheckLine::Warn, a.what,
            "record " + hex8(v) + " type " + hex8(handle) +
                (def ? ": definition " + hex8(def->body) + ", size " + std::to_string(def->size) + ", alignment " + std::to_string(def->alignment) : ": " + why));
    }
     
    std::optional<TypeDefinition> header;
    {
        const uint32_t rec = mem.u32(addr::CLASS_HEADER_PTR);
        header = rec == 0x1AAA420 ? readTypeDefinition(mem, tags, mem.u32(rec), why) : std::nullopt;
        const bool ok = header && header->size >= 128 && header->size <= 2048 && placementAlignment(header->alignment);
        add(ok ? CheckLine::Ok : CheckLine::Fail, "placement header type",
            header ? "size " + std::to_string(header->size) + ", alignment " + std::to_string(header->alignment) : (rec == 0x1AAA420 ? why : "class record differs"));
        if (!ok) header.reset();
    }
     
    std::optional<TypeDefinition> payload;
    for (size_t i = 0; i < std::size(kAiSchemaAnchors); ++i) {
        const Anchor& a = kAiSchemaAnchors[i];
        const uint32_t rec = mem.u32(a.slot);
        if (rec != a.expect) {
            add(CheckLine::Fail, a.what, "*(" + hex8(a.slot) + ") = " + hex8(rec) + ", expected " + hex8(a.expect));
            continue;
        }
        const Bytes r = mem.bytes(rec, 16);
        const uint32_t handle = r.u32(0), defAddr = r.u32(4), backlink = r.u32(12);
        const auto def = readTypeDefinition(mem, tags, handle, why);
        if (!def) {
            add(CheckLine::Fail, a.what, "type " + hex8(handle) + ": " + why);
            continue;
        }
        const Bytes back = storage(backlink, 8) ? mem.bytes(backlink, 8) : Bytes();
        const bool linked = back.size() == 8 && back.u32(0) == rec && back.u32(4) == handle;
        const bool bodyOk = def->body == defAddr;
        const bool keyOk = def->key == kAiSchemaKeys[i];
        std::string detail = "type " + hex8(handle) + ", definition " + hex8(def->body) + (bodyOk ? "" : " (record says " + hex8(defAddr) + ")") + ", key " +
                             hex8(def->key) + (keyOk ? "" : " (Xbox " + hex8(kAiSchemaKeys[i]) + ")") + ", backlink " + (linked ? "ok" : "MISSING");
        if (i == 0) detail += ", payload size " + std::to_string(def->size) + " align " + std::to_string(def->alignment);
        add(!bodyOk || !linked ? CheckLine::Fail : keyOk ? CheckLine::Ok : CheckLine::Warn, a.what, detail);
        if (i == 0 && bodyOk && linked) payload = def;
    }
    if (header && payload) {
        uint32_t maxUsed = 0;
        const bool fits = payloadBudget(header->size, header->alignment, payload->size, payload->alignment, maxUsed);
        add(fits ? CheckLine::Ok : CheckLine::Fail, "AI payload budget",
            fits ? "header + payload need at most " + std::to_string(maxUsed) + " of 2048 arena bytes (all 8 stack residues)" : "can overflow the 2048-byte arena");
    } else {
        add(CheckLine::Warn, "AI payload budget", "not computed (header or payload schema unavailable)");
    }
     
    if (const auto cp = readContextPool(mem, &why))
        add(CheckLine::Ok, "allocation context pool", "base " + hex8(cp->base) + ", stride " + hexShort(cp->stride) + ", capacity " + std::to_string(cp->capacity));
    else
        add(CheckLine::Fail, "allocation context pool", why);
    {
        const Bytes b = mem.bytes(addr::BUNDLE_ARRAY, 0x18);
        const bool ok = b.u32(0x10) == 4 && b.u32(0x14) == 12 && storage(b.u32(0));
        const uint16_t h = mem.bytes(addr::BUNDLE_ALLOC_HANDLE, 2).u16(0);
        const auto rec = contextRecord(mem, h, &why);
        const bool named = rec && rec->name == "client bundle instances";
        add(ok ? (named ? CheckLine::Ok : CheckLine::Warn) : CheckLine::Fail, "client bundle instances header",
            "array " + hex8(b.u32(0)) + ", identity +" + hexShort(b.u32(0x10)) + " / stride " + hexShort(b.u32(0x14)) + "; allocation handle " + hexShort(h) +
                (named ? " -> \"client bundle instances\" (0x1A1D4F0 confirmed)" : " -> " + (rec ? "\"" + rec->name + "\"" : why) + " (0x1A1D4F0 is likely only)"));
    }
     
    if (const auto heap = readTagHeap(mem, why)) {
        const bool inPool = heap->heap >= addr::CONTEXT_POOL && heap->heap < 0x1A00000;
        add(CheckLine::Warn, "variable tag heap",
            "*(0x1892538) = " + hex8(heap->heap) + (inPool ? " (inside the context pool)" : "") + ": base " + hex8(heap->base) + ", size " + hexShort(heap->size) +
                ", head +" + hexShort(heap->head) + ", tail +" + hexShort(heap->tail) + ", " + std::to_string(heap->headerSize) +
                "-byte block headers (field offsets need a PS3 check)");
    } else {
        add(CheckLine::Warn, "variable tag heap", why);
    }
    size_t rows = 0, plausible = 0;
    for (const FixedPoolInfo& f : readFixedPools(mem)) {
        if (!f.pool) continue;
        ++rows;
        plausible += f.plausible;
        add(f.plausible ? CheckLine::Warn : CheckLine::Fail, "fixed tag pool " + hex8(f.row),
            "pool " + hex8(f.pool) + " count " + std::to_string(f.count) + " fraction " + std::to_string(f.fraction) + ": allocation " + hex8(f.allocation) +
                " size " + hexShort(f.size) + ", slots " + std::to_string(f.capacity) + " x " + hexShort(f.stride) + " (field offsets need a PS3 check)");
    }
    add(rows ? CheckLine::Info : CheckLine::Warn, "fixed tag pools", std::to_string(rows) + " of 6 rows set, " + std::to_string(plausible) + " plausible");
    return p;
}

}   
