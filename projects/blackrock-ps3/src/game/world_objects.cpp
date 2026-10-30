#include "game/world_objects.h"

#include <algorithm>

namespace brps3::world {

namespace {

const char* const kTypeNames[TYPE_COUNT] = {
    "inherited", "static_mesh", "prop_simple", "prop_cinematic", "prop_simulated", "prop_expensive", "prop_garbage",
    "speedtree", "interactive", "biped", "creature", "weapon", "vehicle", "turret", "emitter", "projectile",
    "item", "item_ammo", "item_loot", "gear", "hop_on", "hop_on_gear_biped", "hop_on_gear_weapon", "hop_on_gear_ship", "system"};

}   

 

const char* typeName(uint8_t type) { return type < TYPE_COUNT ? kTypeNames[type] : "?"; }

bool typeIsRisky(uint8_t type) {
     
     
    return type == 0 || type == TYPE_PROP_GARBAGE || type == 7 || type == 24 || type >= 20;
}

const Fingerprints kFingerprints = {
    {0x112550, "38E000002C04FFFF418200485486043E"},    
    {0x13E7FC, "F821FF717C0802A6F80100A03C60018E"},    
    {0x1475BC, "80C3002C2C06FFFF418200303C6001A2"},    
    {0x2FF4B8, "3880FFFF2C03FFFF4182002C3C8001A2"},    
    {addr::ACTIVE_PLAYER_HANDLE_RESOLVE, "7C0802A648C61EB9F821FF81F8010090"},
    {addr::PLAYER_ARRAY_INDEX_HANDLE, "7C0802A648C61FD5F821FF61F80100B0"},
    {addr::TAG_REF_RESOLVE, "3C80018980A300007CA66E7054A504FE"},
    {addr::TAG_GET_CLASS, "3CC0018980A400007CA7FE7054E704BE"},
    {addr::POSITION_OF, "3C8001A21041038C546304FE3084D9E0"},
    {addr::FACTIONS_ARE_ALLIED, "7C0802A648BFDBE1F821FF71F80100A0"},
    {addr::FACTION_PAIR_INDEX, "F821FF917C0802A6F801008060650000"},
    {addr::FACTION_LOOKUP, "3CA001A23CC001A230A5D9E0548404FE"},
};

const Fingerprints kSobjectNewFingerprints = {
    {addr::SOBJECT_NEW, "7C0802A648C59CC9F821FE21F80101F0"},
};

const std::array<uint32_t, 32>& spawnDescTemplate() {
     
    static const std::array<uint32_t, 32> t = {
        0x80AAA7F4, 0x0000000C, 0x80800906, 0x00000000,    
        0xBCCC2D40, 0x3B964F10, 0x3F661BE6, 0x3EDFFF75,    
        0x423D8A6A, 0xC43800AD, 0x411EB261, 0x3F7FFFFE,    
        0xFFFFFFFF, 0x61FB8004, 0x00000000, 0x00000000,    
        0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,    
        0x00000000, 0xFFFFFFFF, 0x00000000, 0x3F800000,    
        0xFFFFFFFF, 0x811C9DC5, 0x10030000, 0x00000000,    
        0xFFFFFFFF, 0xFFFFFFFF, 0x0000000C, 0x00000000};   
    return t;
}

const Fingerprints kFactionFingerprints = {
    {addr::FACTION_CONSUMER, "83C7F6584BFFFE817C642E70546306FE7884176438A000017C8407B47CA318307C9E202E7C8318382C030000"},
    {addr::FACTION_ALLEGIANCE_REGISTER, "F821FF817C0802A6F80100903CA0018E"},
};

 

bool PoolHeader::plausible() const {
    return base != 0 && mapped(base) && stride > 0 && stride <= 0x40000 && capacity > 0 && capacity <= 0x10000 &&
           mapped(base, std::max<uint32_t>(1, uint32_t(std::min<uint64_t>(uint64_t(capacity) * stride, 0xFFFFFFFFull))));
}

std::optional<uint32_t> PoolHeader::entryOf(uint32_t handle, uint32_t indexMask) const {
    if (!validHandle(handle) || !plausible()) return std::nullopt;
    const uint32_t index = handle & indexMask;
    if (index >= capacity) return std::nullopt;
    return entry(index);
}

PoolHeader parsePool(const Bytes& b, uint32_t poolAddr) {
    const uint32_t o = poolAddr - b.addr();
    PoolHeader p;
    p.addr = poolAddr;
    p.header = b.u32(o + pool::HEADER);
    p.base = b.u32(o + pool::BASE);
    p.stride = b.u32(o + pool::STRIDE);
    p.capacity = b.u32(o + pool::CAPACITY);
    p.count = b.u32(o + pool::COUNT);
    p.identityOffset = b.u32(o + pool::IDENTITY_OFFSET);
    p.identityStride = b.u32(o + pool::IDENTITY_STRIDE);
    p.mask = b.u32(o + pool::MASK);
    p.mask2 = b.u32(o + pool::MASK2);
    p.classWord = b.u32(o + pool::CLASS);
    p.nameSpace = b.u32(o + pool::NAMESPACE);
    return p;
}

PoolHeader readPool(Reader& mem, uint32_t poolAddr) { return parsePool(mem.bytes(poolAddr, pool::HEADER_SIZE), poolAddr); }

bool strideMatches(uint32_t stride, uint32_t elemSize, uint32_t align, uint32_t identityOffset) {
    if (stride == elemSize) return true;
    if (align == 0 || (align & (align - 1))) return false;
    const uint32_t padded = (elemSize + 4 + align - 1) & ~(align - 1);
    return stride == padded && identityOffset == elemSize;
}

WorldGlobals readGlobals(Reader& mem) {
    const Bytes b = mem.bytes(kGlobalsBlock, kGlobalsBlockSize);
    WorldGlobals g;
    g.engineClock = b.u64(addr::ENGINE_CLOCK - kGlobalsBlock);
    g.players = parsePool(b, addr::PLAYER_POOL);
    g.attachments = parsePool(b, addr::ATTACHMENT_ROOT_POOL);
    g.sobjectGlobals = b.u32(addr::SOBJECT_GLOBALS_PTR - kGlobalsBlock);
    g.sobjects = parsePool(b, addr::SOBJECT_POOL);
    g.bank = b.u32(addr::SOBJECT_POOL_BANK - kGlobalsBlock);
    return g;
}

 

SObject parseSObject(const Bytes& e, uint32_t index) {
    SObject o;
    o.addr = e.addr();
    o.index = index;
    o.flags = e.u32(sobj::FLAGS);
    o.type = e.u8(sobj::TYPE);
    o.handle = e.u32(sobj::HANDLE);
    o.parent = e.u32(sobj::PARENT);
    o.componentHead = e.u32(sobj::COMPONENT_HEAD);
    o.vector40 = e.vec3(sobj::VECTOR40);
    o.attachment = e.u32(sobj::ATTACHMENT);
    o.component = e.u32(sobj::COMPONENT);
    o.rot = e.quat(sobj::ROT);
    o.pos90 = e.vec3(sobj::POS90);
    o.scale = e.f32(sobj::SCALE);
    o.rot2 = e.quat(sobj::ROT2);
    o.position = e.vec3(sobj::POSITION);
    o.scale2 = e.f32(sobj::SCALE2);
    return o;
}

std::optional<SObject> readSObject(Reader& mem, const PoolHeader& sobjects, uint32_t handle) {
    if (sobjects.stride != sobj::STRIDE) return std::nullopt;
    const auto at = sobjects.entryOf(handle);
    if (!at) return std::nullopt;
    const auto e = mem.tryBytes(*at, sobj::STRIDE);
    if (!e) return std::nullopt;
    SObject o = parseSObject(*e, handle & 0x1FFF);
    if (o.handle != handle || o.deleting()) return std::nullopt;
    return o;
}

std::optional<SObject> readSObject(Reader& mem, uint32_t handle) { return readSObject(mem, readPool(mem, addr::SOBJECT_POOL), handle); }

ObjectScan parseObjects(const PoolHeader& pool, const Bytes& entries) {
    ObjectScan s;
    s.pool = pool;
    if (pool.stride == 0) return s;
    s.scanned = uint32_t(entries.size() / pool.stride);
    for (uint32_t i = 0; i < s.scanned; ++i) {
        const uint32_t off = i * pool.stride;
        const uint32_t h = entries.u32(off + sobj::HANDLE);
        if (!validHandle(h) || (h & 0x1FFF) != i) continue;
        s.objects.push_back(parseSObject(entries.slice(off, sobj::STRIDE), i));
    }
    return s;
}

ObjectScan readObjects(Reader& mem, uint32_t maxEntries) {
    const PoolHeader pool = readPool(mem, addr::SOBJECT_POOL);
    if (!pool.plausible() || pool.stride != sobj::STRIDE)
        throw Error("the sObject pool header at " + hex8(addr::SOBJECT_POOL) + " is not plausible (base " + hex8(pool.base) + ", stride " +
                    hex8(pool.stride) + "): not in a game session?");
    const uint32_t n = maxEntries ? std::min(maxEntries, pool.capacity) : pool.capacity;
    return parseObjects(pool, mem.bytes(pool.base, n * pool.stride));
}

std::vector<SObject> childrenOf(const ObjectScan& scan, uint32_t parentHandle) {
    std::vector<SObject> out;
    for (const SObject& o : scan.objects)
        if (o.parent == parentHandle && !o.deleting()) out.push_back(o);
    return out;
}

}   
