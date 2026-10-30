#include "game/inventory_snapshot.h"

#include <algorithm>
#include <chrono>
#include <set>
#include <thread>

#include "game/inventory_addr.h"
#include "game/inventory_data.h"
#include "game/world_tags.h"

namespace brps3::inventory {

namespace {

using world::Bytes;
using world::hex8;

[[noreturn]] void fail(const std::string& m, FailKind k = FailKind::Invalid) { throw SnapshotError(m, k); }
void need(bool ok, const std::string& m, FailKind k = FailKind::Invalid) {
    if (!ok) fail(m, k);
}

uint32_t add(uint32_t base, uint64_t off, uint64_t size = 1) {
    need(base != 0 && uint64_t(base) + off + size <= 0x100000000ULL, "inventory address overflow at " + hex8(base));
    return uint32_t(base + off);
}

 
uint32_t pointer(uint32_t v, const char* what, uint32_t size = 4) {
    need(world::validHandle(v) && world::mapped(v, size) && v % 4 == 0, std::string(what) + " pointer " + hex8(v) + " is not plausible",
         FailKind::Transient);
    return v;
}

Bytes get(world::Reader& mem, uint32_t addr, uint32_t size) {
    try {
        return mem.bytes(addr, size);
    } catch (const SnapshotError&) {
        throw;
    } catch (const std::exception& e) {
        fail(std::string("inventory memory is unavailable (") + e.what() + "); retry after loading", FailKind::Transient);
    }
}

uint32_t word(world::Reader& mem, uint32_t addr) { return get(mem, addr, 4).u32(0); }

 
void requireReady(world::Reader& mem, uint32_t tag) {
    const uint32_t page = world::tagPage(tag);
    const uint16_t count = get(mem, addr::READY_COUNT, 2).u16(0);
    const uint32_t head = pointer(word(mem, addr::READY_HEAD), "readiness head", 12);
    const Bytes h = get(mem, head, 12);
    const uint32_t headStride = h.u32(8);
    need(headStride >= 2 && headStride <= 0x100, "invalid tag-readiness head stride " + hex8(headStride));
    uint16_t node = get(mem, add(head, uint64_t(h.u32(0)) + uint64_t(count) * headStride, 2), 2).u16(0);
    const uint32_t pool = pointer(word(mem, addr::READY_POOL), "readiness pool", 12);
    const Bytes p = get(mem, pool, 12);
    const uint32_t data = add(pool, p.u32(0));
    const uint32_t stride = p.u32(8);
    need(stride >= 0x402 && stride <= 0x10000, "invalid tag-readiness record stride " + hex8(stride));
    const uint32_t links = add(pool, 4ULL + p.u32(4));
    std::set<uint16_t> seen;
    const int64_t key = int64_t(page) - 0x400;
    for (int i = 0; i < 128; ++i) {
        if (node == 0xFFFF) fail("inventory definitions are not resident yet (load your character)", FailKind::Transient);
        need(seen.insert(node).second, "tag-readiness list contains a cycle");
        const uint32_t record = add(data, uint64_t(node) * stride, stride);
        if (int16_t(get(mem, record + 0x400, 2).u16(0)) == key) {
            const uint32_t bits = word(mem, add(record, 4ULL * ((tag & 0x1FFF) >> 5), 4));
            if (!(bits & (1u << (tag & 31)))) fail("inventory definition tag " + hex8(tag) + " is not resident", FailKind::Transient);
            return;
        }
        node = get(mem, add(links, uint64_t(node) * 4, 2), 2).u16(0);
    }
    fail("tag-readiness traversal exceeded its bound");
}

uint32_t resolveTag(world::Reader& mem, const world::TagTable& t, uint32_t tag, uint32_t mode) {
    need((tag & 0x80000000u) && world::isPackagePage(world::tagPage(tag)), "reference " + hex8(tag) + " is not a package tag");
    if (mode == 2) requireReady(mem, tag);
    const world::Resolved r = world::resolve(mem, t, tag, true);
    if (!r.ok) fail("tag " + hex8(tag) + " does not resolve: " + r.why, FailKind::Transient);
    return pointer(r.body, "tag body", 0x10);
}

struct Interface {
    uint32_t iface = 0, vtable = 0, mode = 0, tag = 0;
};

Interface readInterface(world::Reader& mem) {
    Interface i;
    i.iface = word(mem, addr::INVESTMENT_IFACE);
    if (!world::validHandle(i.iface)) fail("the investment provider is not set up (sign in and load a character)", FailKind::Transient);
    pointer(i.iface, "investment provider", 12);
    const Bytes s = get(mem, i.iface, 12);
    i.vtable = s.u32(0);
    i.mode = s.u32(4);
    i.tag = s.u32(8);
    need(i.vtable == addr::INVESTMENT_VTABLE && i.tag == off::GLOBALS_TAG,
         "unrecognised investment provider (vtable " + hex8(i.vtable) + ", tag " + hex8(i.tag) + ")");
    if (i.mode < 2 || i.mode > 4) fail("the investment provider is not ready (mode " + std::to_string(i.mode) + ")", FailKind::Transient);
    return i;
}

void loadDefinitions(world::Reader& mem, DefinitionCache& cache, uint64_t account, uint32_t version, bool force) {
    const Interface i = readInterface(mem);
    const bool sameSession = cache.valid() && cache.account == account && cache.iface == i.iface && cache.vtable == i.vtable &&
                             cache.mode == i.mode;
    if (sameSession && !force) {
         
        const Bytes meta = get(mem, cache.header - 4, 20);
        if (meta.u32(0) == off::DEF_MARKER && meta.u32(4) == cache.count && meta.u32(8) == off::DEF_CLASS) {
            if (cache.version != version) {
                const Bytes rows = get(mem, add(cache.header, 16, uint64_t(cache.count) * 12), cache.count * 12);
                std::vector<uint32_t> hashes(cache.count);
                for (uint32_t k = 0; k < cache.count; ++k) hashes[k] = rows.u32(k * 12);
                cache.hashes = std::move(hashes);
                cache.version = version;
            }
            return;
        }
    }
     
    DefinitionCache c;
    c.account = account;
    c.iface = i.iface;
    c.vtable = i.vtable;
    c.mode = i.mode;
    world::TagTable t;
    try {
        t = world::readTagTable(mem);
    } catch (const SnapshotError&) {
        throw;
    } catch (const std::exception& e) {
        fail(e.what(), FailKind::Transient);
    }
    c.globals = resolveTag(mem, t, off::GLOBALS_TAG, i.mode);
    const Bytes refs = get(mem, add(c.globals, off::GLOBALS_BUCKETS, 0x220), 0x29C - 0x84 + 4);
    c.bucketsTag = refs.u32(0);
    c.defsTag = refs.u32(off::GLOBALS_DEFS - off::GLOBALS_BUCKETS);
    c.indexTag = refs.u32(off::GLOBALS_INDEX - off::GLOBALS_BUCKETS);
    c.defsBase = resolveTag(mem, t, c.defsTag, i.mode);
    const Bytes span = get(mem, add(c.defsBase, 4, 8), 8);
    c.count = span.u32(0);
    const uint32_t rel = span.u32(4);
    need(c.count && c.count <= 0x7FFF && rel >= 8 && rel <= 8192, "invalid live item definition table (count " + std::to_string(c.count) + ")");
    c.header = add(c.defsBase, 8ULL + rel, 16);
    const Bytes meta = get(mem, c.header - 4, 20);
    need(meta.u32(0) == off::DEF_MARKER && meta.u32(4) == c.count && meta.u32(8) == off::DEF_CLASS, "item definition table schema changed");
    const Bytes rows = get(mem, add(c.header, 16, uint64_t(c.count) * 12), c.count * 12);
    c.hashes.resize(c.count);
    for (uint32_t k = 0; k < c.count; ++k) c.hashes[k] = rows.u32(k * 12);
    c.version = version;
    cache = std::move(c);
}

}   

 

std::vector<uint8_t> Observation::read(uint32_t addr, uint32_t size) {
    for (const auto& [key, data] : evidence_)
        if (key.first == addr && key.second == size) return data;
    ++reads_;
    auto data = inner_.read(addr, size);
    if (data.size() != size) fail("short inventory read at " + hex8(addr), FailKind::Transient);
    evidence_.push_back({{addr, size}, data});
    return data;
}

void Observation::verify() {
    for (const auto& [key, data] : evidence_) {
        std::vector<uint8_t> now;
        try {
            now = inner_.read(key.first, key.second);
        } catch (const std::exception&) {
            fail("inventory memory became unreadable during the snapshot; retry", FailKind::Transient);
        }
        ++reads_;
        if (now != data) fail("inventory changed during the snapshot; retrying is required", FailKind::Transient);
    }
}

 

const Item* Snapshot::find(uint64_t soid) const {
    if (!soid) return nullptr;
    for (const Item& i : items)
        if (i.soid == soid) return &i;
    return nullptr;
}

const Item* Snapshot::at(Owner owner, uint16_t slot) const {
    for (const Item& i : items)
        if (i.owner == owner && i.slot == slot) return &i;
    return nullptr;
}

bool Snapshot::isEquipped(uint64_t soid) const { return equippedIndex(soid) >= 0; }

int Snapshot::equippedIndex(uint64_t soid) const {
    if (!soid) return -1;
    for (int i = 0; i < 20; ++i)
        if (equipped[size_t(i)] == soid) return i;
    return -1;
}

std::vector<Row> parseRows(const Bytes& profile, uint32_t holderOffset, uint32_t count) {
    std::vector<Row> rows(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t at = holderOffset + i * off::ROW_SIZE;
        Row& r = rows[i];
        r.def = profile.u16(at);
        r.soid = profile.u64(at + 8);
        r.quantity = profile.u32(at + 0x10);
        for (uint32_t k = 0; k < 6; ++k) r.raw[k] = profile.u8(at + 2 + k);
        if (r.empty()) need(!r.soid && !r.quantity, "malformed empty inventory row (slot " + std::to_string(i) + ")");
    }
    return rows;
}

Snapshot readSnapshot(world::Reader& mem, DefinitionCache& cache, const ReadOptions& o) {
    const auto t0 = std::chrono::steady_clock::now();
    const uint64_t reads0 = mem.reads();
    Observation obs(mem);
    Snapshot s;
    s.sessionOnly = o.sessionOnly;
    s.manager = word(obs, addr::PROFILE_MANAGER);
    if (!s.manager) fail("no profile manager yet (still loading?)", FailKind::Transient);
    pointer(s.manager, "profile manager", 0x50000);
    const Bytes meta = get(obs, add(s.manager, off::META, 96), 96);
    auto object = [&](unsigned kind, uint32_t size, uint32_t schema) {
        const uint32_t offset = meta.u32(kind * 16);
        need(meta.u32(kind * 16 + 4) == 1 && meta.u32(kind * 16 + 8) == size && meta.u32(kind * 16 + 12) == schema &&
                 uint64_t(offset) + size <= off::STORAGE_LIMIT,
             "family-4 inventory layout changed (kind " + std::to_string(kind) + ")");
        return add(s.manager, uint64_t(off::STORAGE) + offset, size);
    };
    s.accountAddr = object(0, off::ACCOUNT_SIZE, off::ACCOUNT_SCHEMA);
    s.characterAddr = object(1, off::CHARACTER_SIZE, off::CHARACTER_SCHEMA);
    const Bytes account = get(obs, s.accountAddr, off::ACCOUNT_SIZE);
    const Bytes character = get(obs, s.characterAddr, off::CHARACTER_SIZE);
    s.accountSoid = account.u64(0);
    s.characterSoid = character.u64(0);
    if (!s.accountSoid || !s.characterSoid) fail("not signed in: no account or character loaded (zero family-4 identity)", FailKind::NotSignedIn);
    need(!o.expectedCharacter || o.expectedCharacter == s.characterSoid, "the current character changed");
    const Bytes table = get(obs, add(s.manager, off::DESCRIPTORS, off::DESCRIPTOR_COUNT * off::DESCRIPTOR_SIZE),
                            off::DESCRIPTOR_COUNT * off::DESCRIPTOR_SIZE);
    unsigned matches = 0;
    for (uint32_t i = 0; i < off::DESCRIPTOR_COUNT; ++i) {
        const uint32_t at = i * off::DESCRIPTOR_SIZE;
        if (table.u8(at) != 4 || table.u64(at + off::DESC_SOID) != s.accountSoid) continue;
        ++matches;
        s.descriptorIndex = i;
        s.descriptorAddr = s.manager + off::DESCRIPTORS + at;
        for (uint32_t k = 0; k < 64; ++k) s.descriptor[k] = table.u8(at + k);
        s.familyVersion = table.u32(at + off::DESC_VERSION);
    }
    need(matches == 1, "expected one account inventory descriptor, found " + std::to_string(matches));
    const uint8_t state = s.descriptor[off::DESC_STATE];
    if (state != 3) {
        if (s.familyVersion == 0xFFFFFFFFu) fail("not signed in (inventory descriptor state " + std::to_string(state) + ")", FailKind::NotSignedIn);
        fail("inventory update is pending (descriptor state " + std::to_string(state) + ")", FailKind::Transient);
    }
    for (uint32_t i = 0; i < 20; ++i) s.equipped[i] = character.u64(off::EQUIPPED + i * 8);
    if (o.sessionOnly) {
        obs.verify();
        s.reads = mem.reads() - reads0;
        s.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        return s;
    }
    loadDefinitions(obs, cache, s.accountSoid, s.familyVersion, false);
    s.mode = cache.mode;
    s.globals = cache.globals;
    s.defsTag = cache.defsTag;
    s.defsBase = cache.defsBase;
    s.defCount = cache.count;
    s.bucketsTag = cache.bucketsTag;
    s.indexTag = cache.indexTag;
    s.character = parseRows(character, off::CHARACTER_HOLDER, off::CHARACTER_ROWS);
    s.account = parseRows(account, off::ACCOUNT_HOLDER, off::ACCOUNT_ROWS);
    const Catalog& cat = catalog();
    std::set<uint64_t> instances;
    auto collect = [&](const std::vector<Row>& rows, Owner owner) {
        for (uint16_t i = 0; i < rows.size(); ++i) {
            const Row& r = rows[i];
            if (r.empty()) continue;
            need(r.def < cache.hashes.size(), "inventory definition " + std::to_string(r.def) + " is outside the live table");
            need(!r.soid || instances.insert(r.soid).second, "duplicate inventory instance " + hex16(r.soid) + "; snapshot refused");
            Item it;
            it.owner = owner;
            it.slot = i;
            it.def = r.def;
            it.hash = cache.hashes[r.def];
            it.soid = r.soid;
            it.quantity = r.quantity;
            it.raw = r.raw;
            it.equipped = owner == Owner::Character && s.isEquipped(r.soid);
            if (const Bucket* b = cat.bucketAt(owner == Owner::Account, i)) it.bucket = b->key;
            s.items.push_back(it);
        }
    };
    collect(s.character, Owner::Character);
    collect(s.account, Owner::Account);
    s.postmasterCount = character.u32(off::POSTMASTER_COUNT);
    for (uint32_t i = 0; i < off::POSTMASTER_ENTRIES; ++i) {
        const uint32_t at = off::POSTMASTER + i * off::POSTMASTER_SIZE;
        PostmasterEntry e{character.u16(at), character.u32(at + 4), character.u8(at + 8)};
        if (e.def != 0xFFFF && (e.def || e.amount)) s.postmaster.push_back(e);
    }
    s.exoticFlag = character.u8(off::EXOTIC_FLAG);
    if (o.talents) {
        const uint32_t offset = meta.u32(48), count = meta.u32(52), stride = meta.u32(56);
        if (stride == off::DETAIL_SIZE && meta.u32(60) == off::DETAIL_SCHEMA && count <= off::DETAIL_MAX &&
            uint64_t(offset) + uint64_t(count) * off::DETAIL_SIZE <= off::STORAGE_LIMIT) {
            s.talents = true;
            s.detailAddr = add(s.manager, uint64_t(off::STORAGE) + offset);
            s.detailCount = count;
            if (count) {
                const Bytes d = get(obs, s.detailAddr, count * off::DETAIL_SIZE);
                for (Item& it : s.items) {
                    if (!it.soid) continue;
                    unsigned found = 0;
                    for (uint32_t n = 0; n < count; ++n) {
                        if (d.u64(n * off::DETAIL_SIZE) != it.soid) continue;
                        ++found;
                        if (d.u16(n * off::DETAIL_SIZE + off::D_DEF) == it.def) {
                            for (uint32_t k = 0; k < off::DETAIL_SIZE; ++k) it.detail[k] = d.u8(n * off::DETAIL_SIZE + k);
                            it.detailAddr = s.detailAddr + n * off::DETAIL_SIZE;
                            it.hasDetail = true;
                        }
                    }
                    if (found != 1) {
                        it.hasDetail = false;
                        it.detailAddr = 0;
                    }
                    s.detailMatched += it.hasDetail;
                }
            }
        }
    }
    obs.verify();
    s.reads = mem.reads() - reads0;
    s.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return s;
}

Snapshot readSnapshotRetry(world::Reader& mem, DefinitionCache& cache, const ReadOptions& o, int attempts,
                           const std::function<bool()>& cancelled, int delayMs) {
    for (int i = 1;; ++i) {
        try {
            return readSnapshot(mem, cache, o);
        } catch (const SnapshotError& e) {
            if (e.kind != FailKind::Transient || i >= attempts || (cancelled && cancelled())) throw;
            if (i == 2) cache.clear();    
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }
}

DescriptorState readDescriptor(world::Reader& mem, const Snapshot& s) {
    const Bytes d = get(mem, s.descriptorAddr, 64);
    DescriptorState st;
    st.family = d.u8(0);
    st.account = d.u64(off::DESC_SOID);
    st.state = d.u8(off::DESC_STATE);
    st.version = d.u32(off::DESC_VERSION);
    return st;
}

InvestmentInterface readInvestmentInterface(world::Reader& mem) {
    const Interface i = readInterface(mem);
    return {i.iface, i.vtable, i.mode, i.tag};
}

uint32_t resolveInvestmentTag(world::Reader& mem, uint32_t tag, uint32_t mode) {
    world::TagTable t;
    try {
        t = world::readTagTable(mem);
    } catch (const SnapshotError&) {
        throw;
    } catch (const std::exception& e) {
        fail(e.what(), FailKind::Transient);
    }
    return resolveTag(mem, t, tag, mode);
}

std::vector<uint32_t> readDefinitionHashes(world::Reader& mem, DefinitionCache& cache, uint32_t* mode) {
    Observation obs(mem);
    loadDefinitions(obs, cache, cache.account, cache.version, true);
    obs.verify();
    if (mode) *mode = cache.mode;
    return cache.hashes;
}

}   
