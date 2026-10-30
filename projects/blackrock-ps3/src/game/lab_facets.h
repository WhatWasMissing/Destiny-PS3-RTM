#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "game/lab_components.h"    
#include "game/world_components.h"
#include "game/world_registry.h"

namespace brps3::lab {

inline constexpr uint32_t kFacetManagerSlot = 0x1A20FC0, kFacetManagerSize = 0x8D40, kFacetManagerHash = 0x3A851ADD;
inline constexpr uint32_t kFacetsPool = 0x1A20FD0, kFacetStride = 0x120, kFacetCapacity = 512, kFacetIdentity = 0x110;
inline constexpr uint32_t kActivityBundlesPool = 0x1A21050, kActivityStride = 0xC, kActivityCapacity = 32;
inline constexpr uint32_t kComponentsLookupSlot = 0x1A21124, kComponentsLookupSize = 0x4000, kComponentsLookupHash = 0x263D40D1;
inline constexpr uint32_t kDeferredContextSlot = 0x1A93690, kDeferredContextHash = 0x521B83CD;
inline constexpr uint32_t kAllocContextPool = 0x1917100;
inline constexpr uint32_t kRegistryCat3Handle = 0x19F2E5C;
inline constexpr uint32_t kFacetObjectMap = 0x1E4ED4, kFacetObjectMapHelper = 0x1E4B14, kFacetChainUnlink = 0x1E4080;
inline constexpr uint32_t kFacetOwner = 25;
inline constexpr uint32_t kRegistrySnapshotSize = 0x2040;    

using FacetFingerprints = std::vector<std::pair<uint32_t, const char*>>;
extern const FacetFingerprints kFacetFingerprints;

 
struct RegistrySnapshot {
    world::Bytes raw;                     
    uint32_t count0() const { return raw.u32(0x30); }
    uint32_t count3() const { return raw.u32(0x2034); }
    uint16_t cat0Handle() const { return raw.u16(0); }
    uint32_t backReference(uint32_t index) const { return raw.u32(0x1034 + index * 8); }
     
    uint16_t recordHandle(uint32_t i) const { return raw.u16(0x34 + i * 16); }
    uint32_t recordOffset(uint32_t i) const { return raw.u32(0x34 + i * 16 + 4); }
    uint32_t recordSize(uint32_t i) const { return raw.u32(0x34 + i * 16 + 8); }
};
RegistrySnapshot readRegistry(world::Reader& mem);

struct BlockCheck {
    uint32_t slot = 0, body = 0, size = 0;
    world::BlockHeader header;
    uint32_t allocation = 0, payload = 0, extent = 0;
    std::vector<std::string> failures;    
    bool ok() const { return failures.empty() && body != 0; }
};
struct BlockSpec {
    uint32_t slot, size, hash;
    uint32_t region;                      
    uint32_t category;                    
    uint32_t owner;
    uint32_t flagsMax;
};
BlockCheck checkBlock(world::Reader& mem, const RegistrySnapshot& reg, const BlockSpec& spec);
inline const BlockSpec kManagerSpec{kFacetManagerSlot, kFacetManagerSize, kFacetManagerHash, 8, 0, kFacetOwner, 1};
inline const BlockSpec kLookupSpec{kComponentsLookupSlot, kComponentsLookupSize, kComponentsLookupHash, 1, 0, kFacetOwner, 1};
inline const BlockSpec kDeferredSpec{kDeferredContextSlot, 12, kDeferredContextHash, 1, 3, kFacetOwner, 1};

 
struct DirectoryRegion {
    uint32_t index = 0, count = 0, start = 0, handle = 0;    
    std::vector<uint32_t> heads;          
};
struct Directory {
    uint32_t regions = 0, total = 0;
    std::vector<DirectoryRegion> rows;
    std::string why;                      
};
 
Directory parseDirectory(const world::Bytes& manager);

 
struct FacetRecord {
    uint32_t handle = 0, index = 0, addr = 0;
    uint32_t identity = 0, canonical = 0;
    bool identityOk = false;              
    uint32_t dirtyPointer = 0, dirtyDescriptor = 0, dirty[4] = {};
    uint32_t self = 0, linked = 0, arena = 0;
    uint32_t networkObject = 0, key = 0, marker = 0, next = 0;
    uint16_t flags = 0;
     
    bool dirtyPointerOk() const { return dirtyPointer == addr + 0x98; }
    bool descriptorOk() const { return dirtyDescriptor == 0xFFF8080Fu; }
    bool selfOk() const { return self == handle; }
};
FacetRecord parseFacetRecord(const world::Bytes& rec, uint32_t handle, const world::PoolHeader& pool);    
std::optional<FacetRecord> readFacetRecord(world::Reader& mem, const world::PoolHeader& pool, uint32_t handle);

 
std::string facetsPoolProblem(const world::PoolHeader& p);

struct FacetManagerView {
    BlockCheck check;
    world::Bytes body;                    
    Directory directory;
    world::PoolHeader facets;             
    uint32_t facetFor(uint32_t mapIndex) const;    
};
FacetManagerView readFacetManager(world::Reader& mem, const RegistrySnapshot& reg);

enum class Route { Unknown, Disabled, NoComponent, NoNetworkObject, Immediate, Deferred };
const char* routeName(Route r);
const char* routeHelp(Route r);

struct FacetOfObject {
    uint32_t object = 0, entry = 0, arena = 0, domain = 0, mapIndex = 0, facet = 0xFFFFFFFFu;
    uint32_t arenaCanonical = 0;          
    std::optional<FacetRecord> record;
     
     
     
    std::optional<world::Accessor> linked;
    uint32_t linkedOwner = 0, linkedCanonical = 0;    
    bool linkedIdentityOk = false;
    uint32_t glueArena = 0, glueOwner = 0, glueFacet = 0;
    bool glueOk = false;
    std::string glueWhy;                  
     
    uint16_t lookupValue = 0xFFFF;        
    uint8_t contextMode = 0;
    Route route = Route::Unknown;
    uint32_t dirtyWord = 0, dirtyBit = 0;    
    std::string why;                      
    bool ok() const { return why.empty(); }
};
FacetOfObject readFacetOfObject(world::Reader& mem, const FacetManagerView& m, const RegistrySnapshot& reg, uint32_t objectHandle);

struct ChainNode {
    uint32_t handle = 0, next = 0;
    bool identityOk = false;
};
 
std::vector<ChainNode> walkChain(world::Reader& mem, const world::PoolHeader& facets, uint32_t head, std::string& why);

struct ActivityBundle {
    uint32_t index = 0, key = 0, region = 0, identity = 0, handle = 0;
    bool used() const { return key != 0xFFFFFFFFu && identity != 0; }
};
std::vector<ActivityBundle> readActivityBundles(world::Reader& mem, std::string& why);

struct DeferredContext {
    BlockCheck check;
    uint8_t mode = 0, state = 0;
    uint32_t context = 0;
};
DeferredContext readDeferredContext(world::Reader& mem, const RegistrySnapshot& reg);

}   
