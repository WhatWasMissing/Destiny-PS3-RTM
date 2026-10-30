#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "game/cmd_handler.h"
#include "game/guard_call.h"
#include "game/spawn_place.h"
#include "game/world_addr.h"
#include "game/world_objects.h"
#include "game/world_tags.h"

namespace brps3 {
class Runtime;
}

namespace brps3::spawn {

using world::Fingerprints;

 
extern const Fingerprints& kSpawnFingerprints;      
extern const Fingerprints kDespawnFingerprints;     

inline constexpr uint32_t kCreationDepthOffset = 0x3C4;    
inline constexpr uint32_t kMaxCreationDepth = 5;
inline constexpr size_t kSpawnBatch = 8;                   

 
struct Preflight {
    uint32_t tag = 0;                    
    world::TagTable table;
    world::Resolved resolved;            
    world::Residency residency;          
    world::PoolHeader pool;              
    uint32_t sobjectGlobals = 0, creationDepth = 0;
    uint8_t type = 0;                    
    std::vector<std::string> warnings;   
};
 
 
 
Preflight preflight(world::Reader& mem, uint32_t tag, int expectedType = -1);

 
guard::Request spawnRequest(const Preflight& p, const std::vector<uint8_t>& desc);
inline constexpr uint32_t kScratchOut = 0x00, kScratchDesc = 0x10;

struct SpawnResult {
    SpawnPoint point;
    bool called = false;                 
    uint32_t handle = 0;                 
    bool verified = false;               
    std::string reason;                  
    bool ok() const { return called && verified; }
};
 
 
std::vector<SpawnResult> spawnAt(Runtime& rt, world::Reader& mem, const Preflight& p, const std::vector<SpawnPoint>& points, float scale,
                                 const std::function<bool()>& cancelled = {},
                                 const std::function<void(size_t, size_t)>& progress = {});

 
 
 
 
 
 
struct InGameReport {
    uint8_t type = 0;                    
    size_t requested = 0;                
    std::string stopped;                 
    cmd::SlotHeader header;              
};
cmd::SpawnArgs spawnArgs(uint32_t tag, int expectedType, const Placement& p);
std::string refusalText(const cmd::SlotHeader& h, uint32_t tag, int expectedType);    
std::vector<SpawnResult> spawnResults(const cmd::Outcome& o);                        
std::vector<SpawnResult> spawnInGame(Runtime& rt, cmd::Handler& handler, uint32_t tag, int expectedType, const Placement& p,
                                     const std::function<bool()>& cancelled = {},
                                     const std::function<void(size_t, size_t)>& progress = {}, InGameReport* report = nullptr);

 
guard::Request despawnRequest(uint32_t handle);
struct DespawnResult {
    uint32_t handle = 0;
    bool called = false;                 
    bool gone = false;                   
    std::string reason;
};
 
std::vector<DespawnResult> despawn(Runtime& rt, world::Reader& mem, const std::vector<uint32_t>& handles);

 
struct MoveResult {
    world::SObject before, after;
    bool wrote40 = false;                
    bool held = false;                   
};
 
 
MoveResult moveObject(world::Reader& mem, const std::function<void(uint32_t, const std::vector<uint8_t>&)>& write, uint32_t handle,
                      const Vec3& to);

 
 
 
 
 
std::map<uint32_t, uint32_t> tagsOfObjects(world::Reader& mem, const world::TagTable& t, const std::vector<world::SObject>& objects,
                                           uint32_t budget = 0x200000);

}   
