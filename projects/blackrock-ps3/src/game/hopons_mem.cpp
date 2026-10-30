#include "game/hopons_mem.h"

#include <cstdio>
#include <cstring>

#include "core/error.h"
#include "game/hopons_addr.h"

namespace brps3::hopons {

std::string hex8(uint32_t v) { return world::hex8(v); }

std::string hexShort(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "%X", v);
    return b;
}

std::string hexBytes(const std::vector<uint8_t>& b, size_t max) {
    static const char* d = "0123456789ABCDEF";
    std::string s;
    const size_t n = max ? std::min(max, b.size()) : b.size();
    s.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        s += d[b[i] >> 4];
        s += d[b[i] & 15];
    }
    if (n < b.size()) s += "...";
    return s;
}

uint32_t be32At(const std::vector<uint8_t>& b, size_t off) {
    if (off + 4 > b.size()) throw Error("record too short (+" + hexShort(uint32_t(off)) + ")");
    return uint32_t(b[off]) << 24 | uint32_t(b[off + 1]) << 16 | uint32_t(b[off + 2]) << 8 | b[off + 3];
}

 

Bytes guardRead(Reader& mem, uint32_t addr, uint32_t size, std::vector<Guard>& guards) {
    Bytes b = mem.bytes(addr, size);
    guards.push_back({addr, b.data()});
    return b;
}

bool guardsUnchanged(Reader& mem, const std::vector<Guard>& guards, std::string* why) {
    for (const Guard& g : guards) {
        if (g.bytes.empty()) continue;
        const auto now = mem.tryBytes(g.addr, uint32_t(g.bytes.size()));
        if (!now || now->data() != g.bytes) {
            if (why) *why = (now ? "bytes at " : "unreadable: ") + hex8(g.addr) + (now ? " changed" : "");
            return false;
        }
    }
    return true;
}

 

TagReader::TagReader(Reader& mem) : mem_(mem), table_(world::readTagTable(mem)) {}

void TagReader::loadAllPages() {
    if (!all_.size()) all_ = mem_.bytes(table_.base, table_.pageCount * table_.pageStride);
}

const Bytes& TagReader::pageBytes(uint32_t page) {
    if (page >= table_.pageCount) throw Error("tag page " + hexShort(page) + " is beyond the table (" + std::to_string(table_.pageCount) + " pages)");
    auto it = pages_.find(page);
    if (it != pages_.end()) return it->second;
    Bytes b = all_.size() ? all_.slice(page * table_.pageStride, 0x40) : mem_.bytes(table_.base + page * table_.pageStride, 0x40);
    return pages_.emplace(page, std::move(b)).first->second;
}

std::optional<TagMapping> TagReader::map(uint32_t tag, uint32_t wantStride, std::string* why) {
    auto no = [&](std::string w) -> std::optional<TagMapping> {
        if (why) *why = std::move(w);
        return std::nullopt;
    };
    if (!world::validHandle(tag)) return no("not a handle");
    TagMapping m;
    m.tag = tag;
    m.page = world::tagPage(tag);
    m.index = world::tagIndex(tag);
    if (m.page >= table_.pageCount) return no("page " + hexShort(m.page) + " is beyond the tag table");
    m.manager = table_.manager;
    m.tableBase = table_.base;
    m.desc = table_.base + m.page * table_.pageStride;
    m.descBytes = pageBytes(m.page);
    m.array = m.descBytes.u32(0x10);
    m.registration = m.descBytes.u32(0x18);
    m.stop = m.descBytes.u32(0x20);
    m.stride = m.descBytes.u32(0x30);
    m.mask = m.descBytes.u32(0x34);
    if (!m.array || !m.registration) return no("page " + hexShort(m.page) + " is not registered (not loaded)");
    if (m.stride < 12 || m.stride > 0x10000 || m.stride % 4) return no("page " + hexShort(m.page) + " has item stride " + hexShort(m.stride));
    if (wantStride && m.stride != wantStride)
        return no("page " + hexShort(m.page) + " item stride " + hexShort(m.stride) + " (expected " + hexShort(wantStride) + ")");
    if (m.stop <= m.array || (m.stop - m.array) % m.stride || (m.stop - m.array) / m.stride > 8192 || !storage(m.array, m.stop - m.array))
        return no("page " + hexShort(m.page) + " item array " + hex8(m.array) + ".." + hex8(m.stop) + " is not plausible");
    if (m.index >= (m.stop - m.array) / m.stride) return no("index " + hexShort(m.index) + " is outside page " + hexShort(m.page));
    m.entry = m.array + m.index * m.stride;
    const auto e = mem_.tryBytes(m.entry, 12);
    if (!e) return no("entry " + hex8(m.entry) + " is unreadable");
    m.entryBytes = *e;
    m.word = e->u32(0);
    m.metadata = e->u32(4);
    m.backOffset = e->u32(8);
    if (!m.word || m.word == 0xC0000000u || (m.word & 0xFFFF) == 0xFEFE) return no("tag " + hex8(tag) + " is not resident (entry " + hex8(m.word) + ")");
    m.body = m.entry - (m.backOffset & m.mask);
    if (!storage(m.body, 16)) return no("body " + hex8(m.body) + " is not plausible");
    return m;
}

uint32_t registrationOf(Reader& mem, uint32_t handle, std::string* why) {
    auto no = [&](const std::string& w) {
        if (why) *why = w;
        return 0u;
    };
    if (!world::validHandle(handle)) return no("not a handle");
    const uint32_t mgr = mem.u32(addr::IDENTITY_MGR_PTR);
    if (!storage(mgr, 0x14)) return no("identity manager " + hex8(mgr) + " is not set up");
    const Bytes m = mem.bytes(mgr, 0x14);
    const uint32_t base = m.u32(0), end = m.u32(8), stride = m.u32(0x10), page = world::tagPage(handle);
    if (stride != 0x40 || end <= base || (end - base) % stride || page >= (end - base) / stride) return no("page " + hexShort(page) + " is outside the identity manager");
    const uint32_t reg = mem.u32(base + page * 0x40 + 0x18);
    if (!storage(reg, 0x38)) return no("page " + hexShort(page) + " has no registration");
    return reg;
}

 

uint32_t reconstructHandle(uint32_t identity, uint32_t mask, uint32_t ns, uint32_t index) {
    const uint32_t gen = identity & mask;
    if (ns & 0x40000000u) return ((((gen << 14) & 0x1C000u) | 0xFFFC0000u | (ns & 0x3FFFu)) << 13) | index;
    return ((((gen << 10) & 0x3FC00u) | (ns & 0x3FFu)) << 13) | index;
}

std::optional<PoolSlot> poolSlot(Reader& mem, uint32_t poolAddr, uint32_t index, std::string* why) {
    auto no = [&](std::string w) -> std::optional<PoolSlot> {
        if (why) *why = std::move(w);
        return std::nullopt;
    };
    PoolSlot s;
    s.index = index;
    s.pool = world::readPool(mem, poolAddr);
    const auto& p = s.pool;
    if (!p.capacity || p.capacity > 8192 || index >= p.capacity) return no("index " + hexShort(index) + " is outside pool " + hex8(poolAddr));
    if (!storage(p.header, 0x20)) return no("pool " + hex8(poolAddr) + " header is not set up");
    const Bytes h = mem.bytes(p.header, 0x20);
    s.headerCapacity = h.u16(0x1C);
    if (!s.headerCapacity || s.headerCapacity > 8192 || index >= s.headerCapacity) return no("index beyond the pool header capacity");
    const uint32_t bitmap = h.u32(8);
    if (!storage(bitmap, 12)) return no("allocation bitmap descriptor " + hex8(bitmap) + " is not plausible");
    const Bytes bd = mem.bytes(bitmap, 12);
    const uint32_t bytes = bd.u32(0), bits = bd.u32(4), words = bd.u32(8);
    if (bits > 8192 || index >= bits || bytes < ((bits + 31) / 32) * 4 || bytes > 4096 || !storage(words, bytes)) return no("allocation bitmap is not plausible");
    s.bitmapAddr = words + (index >> 5) * 4;
    s.bitmapWord = mem.u32(s.bitmapAddr);
    s.bitmapBit = 1u << (index & 31);
    if (!(s.bitmapWord & s.bitmapBit)) return no("slot " + hexShort(index) + " is not allocated");
    if (!p.identityStride || p.identityStride > 0x10000 || p.identityStride % 4 || p.identityOffset % 4 || p.identityOffset > 0x100000)
        return no("pool identity layout is not plausible");
    const uint64_t a = uint64_t(p.base) + p.identityOffset + uint64_t(index) * p.identityStride;
    if (a > 0xFFFFFFFCull || !storage(uint32_t(a))) return no("identity word is outside memory");
    s.identityAddr = uint32_t(a);
    s.identity = mem.u32(s.identityAddr);
    s.handle = reconstructHandle(s.identity, p.mask, p.nameSpace, index);
    if (!world::validHandle(s.handle)) return no("reconstructed handle is not valid");
    return s;
}

 

std::optional<ContextPool> readContextPool(Reader& mem, std::string* why) {
    const Bytes b = mem.bytes(addr::CONTEXT_POOL, 0x10);
    ContextPool c;
    c.base = addr::CONTEXT_POOL + b.u32(0);
    c.stride = b.u32(8);
    c.capacity = b.u16(0xC);
    if (c.stride < 0x44 || c.stride > 4096 || c.stride % 4 || !c.capacity || !storage(c.base, c.capacity * c.stride)) {
        if (why) *why = "allocation context pool " + hex8(addr::CONTEXT_POOL) + " is not plausible";
        return std::nullopt;
    }
    return c;
}

std::optional<ContextRecord> contextRecord(Reader& mem, uint32_t handle, std::string* why) {
    const auto pool = readContextPool(mem, why);
    if (!pool) return std::nullopt;
    if (!handle || handle == 0xFFFF || handle >= pool->capacity) {
        if (why) *why = "context handle " + hexShort(handle) + " is outside the pool";
        return std::nullopt;
    }
    ContextRecord r;
    r.handle = handle;
    r.addr = pool->base + handle * pool->stride;
    const Bytes b = mem.bytes(r.addr, 0x5C);
    r.name = b.cstr(0, 0x30);
    r.buffer = b.u32(0x30);
    r.extent = b.u32(0x40);
    r.w54 = b.u32(0x54);
    r.w58 = b.u32(0x58);
    return r;
}

 

bool resolveReference(Reader& mem, const std::array<uint8_t, 8>& bytes, unsigned bank, ScriptReference& out, std::string& error) {
    out = {};
    error.clear();
    if (bank > 1) {
        error = "reference bank must be 0 or 1";
        return false;
    }
    ScriptReference r;
    r.bank = uint8_t(bank);
    r.kind = bytes[4];
    r.key = uint32_t(bytes[0]) << 24 | uint32_t(bytes[1]) << 16 | uint32_t(bytes[2]) << 8 | bytes[3];
    const uint16_t index = uint16_t(uint16_t(bytes[6]) << 8 | bytes[7]);
    if (r.kind == 0xFF || index == 0xFFFF) {
        out = r;    
        return true;
    }
    if (index >= 0x8000) {
        error = "reference has a negative signed slot";
        return false;
    }
    std::vector<Guard> guards;
    const Bytes handle = guardRead(mem, addr::REFMGR_HANDLE, 2, guards);
    r.managerHandle = handle.u16(0);
    std::string why;
    const auto rec = contextRecord(mem, r.managerHandle, &why);
    if (!rec) {
        error = "client reference manager unavailable: " + why;
        return false;
    }
    guards.push_back({rec->addr, mem.bytes(rec->addr, 0x5C).data()});
    r.allocation = rec->addr;
    r.manager = rec->buffer;
    if (rec->name != "client reference manager" || rec->extent < 0x16E10 || !storage(r.manager, rec->extent) || rec->w54 != 25 || rec->w58 != 1) {
        error = "context " + hexShort(r.managerHandle) + " (" + rec->name + ") is not the initialised client reference manager";
        return false;
    }
    const Bytes header = guardRead(mem, r.manager, 8, guards);
    const Bytes count = guardRead(mem, r.manager + 0x508, 4, guards);
    if (header.u32(0) != 2 || header.u32(4) > 64 || count.u32(0) > 0xC00) {
        error = "client reference manager header / counts are not plausible";
        return false;
    }
    const uint32_t directories = header.u32(4), total = count.u32(0);
    if (!directories) {
        error = "reference directory is empty";
        return false;
    }
    const Bytes rows = guardRead(mem, r.manager + 8, directories * 20, guards);
    int match = -1;
    for (uint32_t i = 0; i < directories; ++i) {
        const uint32_t o = i * 20, n = rows.u32(o), start = rows.u32(o + 4);
        if (start > total || n > total - start) {
            error = "reference directory range exceeds the registered slots";
            return false;
        }
        if (rows.u32(o + 8) == r.key) {
            if (match >= 0) {
                error = "reference directory key is ambiguous";
                return false;
            }
            match = int(o);
        }
    }
    if (match < 0) {
        error = "reference directory key " + hex8(r.key) + " was not found";
        return false;
    }
    if (index >= rows.u32(uint32_t(match))) {
        error = "reference slot exceeds its directory";
        return false;
    }
    r.slot = rows.u32(uint32_t(match) + 4) + index;
    r.container = rows.u32(uint32_t(match) + 12);
    const Bytes slot = guardRead(mem, r.manager + 0x50C + r.slot * 28, 28, guards);
    r.slotKind = slot.u32(0);
    for (uint32_t i = 0; i < 3; ++i) r.target[i] = slot.u32(4 + bank * 12 + i * 4);
    if (r.target[0] == 0xFFFFFFFFu) {
        error = "reference bank " + std::to_string(bank) + " has no registered target";
        return false;
    }
    if (!guardsUnchanged(mem, guards, &why)) {
        error = "reference manager changed during the lookup (" + why + "); refresh";
        return false;
    }
    r.present = true;
    out = r;
    return true;
}

 

std::vector<std::pair<const char*, const Fingerprints*>> allFingerprintSets() {
    return {{"Hop-ons", &kHopFingerprints},
            {"Timer edits", &kTimerFingerprints},
            {"Sequence cleanup", &kCleanupFingerprints},
            {"Spawner removal", &kSpawnerRemoveFingerprints},
            {"Sequence restart", &kRestartFingerprints},
            {"AI preflight", &kAiFingerprints}};
}

}   
