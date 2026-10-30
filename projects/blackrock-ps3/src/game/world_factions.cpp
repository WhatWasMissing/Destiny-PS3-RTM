#include "game/world_factions.h"

#include <utility>

namespace brps3::world {

int allegianceBit(int a, int b) {
    if (a < 0 || a >= kFactionCount || b < 0 || b >= kFactionCount || a == b) return -1;
    if (a > b) std::swap(a, b);
    return 32 * a - (a + 2) * (a + 1) / 2 + b;
}

unsigned allegianceByte(unsigned bit) { return (bit / 32) * 4 + 3 - (bit % 32) / 8; }

bool allegiancePair(const std::array<uint8_t, 64>& bits, int a, int b) {
    if (a < 0 || a >= kFactionCount || b < 0 || b >= kFactionCount) return false;
    if (a == b) return true;
    const unsigned bit = unsigned(allegianceBit(a, b));
    return (bits[allegianceByte(bit)] & (1u << (bit % 8))) != 0;
}

void setAllegiancePair(std::array<uint8_t, 64>& bits, int a, int b, bool allied) {
    const int bit = allegianceBit(a, b);
    if (bit < 0) return;
    const uint8_t mask = uint8_t(1u << (unsigned(bit) % 8));
    uint8_t& byte = bits[allegianceByte(unsigned(bit))];
    byte = allied ? uint8_t(byte | mask) : uint8_t(byte & ~mask);
}

unsigned allegianceCount(const std::array<uint8_t, 64>& bits) {
    unsigned n = 0;
    for (int a = 0; a < kFactionCount; ++a)
        for (int b = a + 1; b < kFactionCount; ++b) n += allegiancePair(bits, a, b);
    return n;
}

Allegiance readAllegiance(Reader& mem) {
    Allegiance s;
    auto fail = [&](std::string why) {
        if (s.proof.empty()) s.proof = std::move(why);
        return s;
    };
    s.body = mem.u32(addr::FACTION_ALLEGIANCE_SLOT);
    if (!validHandle(s.body) || s.body < 16 || (s.body & 3) || !mapped(s.body - 16, 80)) return fail("allegiance slot is not set (" + hex8(s.body) + ")");
    const Bytes blk = mem.bytes(s.body - 16, 80);
    s.header = parseBlockHeader(blk.slice(0, 16));
    for (int i = 0; i < 64; ++i) s.bits[size_t(i)] = blk.u8(16 + uint32_t(i));
    s.registryIndex = s.header.index;
    if (s.header.index >= 256 || s.header.hash != kAllegianceHash || s.header.region != 1 || s.header.category != 0 || s.header.owner != 0x17 ||
        s.header.flags > 1)
        return fail("block header is not the allegiance block (index " + std::to_string(s.header.index) + ", hash " + hex8(s.header.hash) + ")");
    const Bytes reg = mem.bytes(addr::GLOBALS_REGISTRY, kRegistryReadSize);
    const uint32_t count = reg.u32(0x30);
    if (count > 256 || s.header.index >= count) return fail("registry count " + std::to_string(count) + " does not cover index " + std::to_string(s.header.index));
    if (reg.u32(0x1034 + s.header.index * 8) != addr::FACTION_ALLEGIANCE_SLOT) return fail("registry back-reference does not point at the slot");
    s.ledgerHandle = reg.u16(0x34 + s.header.index * 16);
    s.ledgerOffset = reg.u32(0x34 + s.header.index * 16 + 4);
    s.ledgerSize = reg.u32(0x34 + s.header.index * 16 + 8);
    if (s.ledgerSize != 64) return fail("registry ledger size " + std::to_string(s.ledgerSize) + " is not 64");
    const Bytes dir = mem.bytes(addr::ALLOC_CONTEXT_POOL, 14);
    const int64_t base = int64_t(addr::ALLOC_CONTEXT_POOL) + int32_t(dir.u32(0));
    s.directoryStride = dir.u32(8);
    s.directoryCapacity = dir.u16(12);
    if (base < 0 || base > 0xFFFFFFFFll) return fail("allocation directory base out of range");
    s.directoryBase = uint32_t(base);
    if (!s.ledgerHandle || s.ledgerHandle == 0xFFFF || s.ledgerHandle >= s.directoryCapacity || s.directoryStride < 0x44 ||
        s.directoryStride > 4096 || s.directoryStride % 4 || !mapped(s.directoryBase, s.directoryCapacity * s.directoryStride))
        return fail("allocation directory does not hold handle " + std::to_string(s.ledgerHandle));
    const Bytes rec = mem.bytes(s.directoryBase + s.ledgerHandle * s.directoryStride + 0x30, 20);
    s.payload = rec.u32(0);
    s.extent = rec.u32(16);
    if (!mapped(s.payload, 1) || uint64_t(s.ledgerOffset) + 80 > s.extent || uint64_t(s.payload) + s.ledgerOffset != uint64_t(s.body) - 16)
        return fail("allocation record does not own the block (payload " + hex8(s.payload) + " + " + hex8(s.ledgerOffset) + ")");
    return s;
}

Allegiance readAllegianceStable(Reader& mem) {
    const Allegiance a = readAllegiance(mem);
    if (!a.owned()) throw Error("Faction allegiance not verified: " + a.proof);
    const Allegiance b = readAllegiance(mem);
    if (!b.owned() || !a.sameOwner(b) || a.bits != b.bits) throw Error("Faction allegiance changed while reading; try again");
    return b;
}

int8_t FactionTable::of(uint32_t handle) const {
    if (!validHandle(handle)) return -1;
    const uint32_t i = (bank << 13) | (handle & 0x1FFF);
    return i < entries.size() ? entries[i] : int8_t(-1);
}

FactionTable readFactionTable(Reader& mem) {
    FactionTable t;
    t.base = mem.u32(addr::FACTION_TABLE_CACHE);
    t.bank = mem.u32(addr::SOBJECT_POOL_BANK);
    if (!validHandle(t.base) || !mapped(t.base, kFactionTableEntries)) throw Error("the faction table is not set (" + hex8(t.base) + ")");
    const Bytes b = mem.bytes(t.base, kFactionTableEntries);
    t.entries.resize(kFactionTableEntries);
    for (uint32_t i = 0; i < kFactionTableEntries; ++i) t.entries[i] = b.s8(i);
    return t;
}

int8_t factionOf(Reader& mem, uint32_t handle) {
    if (!validHandle(handle)) return -1;
    const uint32_t base = mem.u32(addr::FACTION_TABLE_CACHE);
    const uint32_t bank = mem.u32(addr::SOBJECT_POOL_BANK);
    const uint32_t i = (bank << 13) | (handle & 0x1FFF);
    if (!validHandle(base) || !mapped(base + i, 1)) return -1;
    return mem.bytes(base + i, 1).s8(0);
}

}   
