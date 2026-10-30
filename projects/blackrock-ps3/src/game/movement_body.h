#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <string>
#include <vector>

#include "game/world_mem.h"
#include "game/world_player.h"
#include "game/world_tags.h"

namespace brps3::movement {

struct BodyChain {
    bool ok = false;
    std::string why;                     
    uint32_t arenaHandle = 0, arena = 0;
    uint32_t defTag = 0, def = 0;
    uint32_t count = 0;                  
    uint32_t offset = 0;                 
    uint32_t component = 0;              
    uint32_t physicsSlot = 0;            
    uint32_t physics = 0;                
    uint32_t entryCount = 0;             
    uint8_t componentFlag = 0;           
    uint32_t body = 0;                   
    uint32_t bodyVptr = 0;
    uint32_t bodyWorld = 0;              
    uint32_t motionVptr = 0;             
    uint8_t motionType = 0;              
    world::Vec3 xform, com1, linvel;     
};

 
 
BodyChain resolveBody(world::Reader& mem, const world::LocalPlayer& p);
BodyChain resolveBody(world::Reader& mem, const world::TagTable& t, const world::LocalPlayer& p);

 
struct PlayerLink {
    uint32_t handle = 0;                 
    uint32_t element = 0;                
    uint32_t entry = 0;                  
    uint32_t row = 0;                    
    uint32_t body = 0;
    uint32_t physicsSlot = 0;
    bool complete() const { return handle && element && entry && row && body && physicsSlot; }
    bool operator==(const PlayerLink&) const = default;
};

struct LinkResult {
    bool ok = false;
    bool noPlayer = false;               
    std::string message;
    PlayerLink link;
    BodyChain chain;
};
 
LinkResult resolveLink(world::Reader& mem);

 
struct IslandScan {
    bool worldOk = false;
    std::string why;
    uint32_t world = 0, worldVptr = 0;
    uint32_t islands[2] = {0, 0};        
    uint32_t islandsOk = 0;              
    uint32_t entities = 0;               
    uint32_t bodies = 0;                 
    uint32_t otherVptr = 0;              
    bool containsSelf = false;           
    std::vector<uint32_t> sample;        
};
 
IslandScan scanIslands(world::Reader& mem, uint32_t selfBody = 0, uint32_t maxBodies = 48, uint32_t maxEntities = 0x1000);

 
inline bool taskHeap(uint32_t p) { return p >= 0x30000000u && p < 0x80000000u && (p & 3) == 0; }
 
inline bool finiteBits(uint32_t bits) { return ((bits >> 23) & 0xFF) != 0xFF; }

}   
