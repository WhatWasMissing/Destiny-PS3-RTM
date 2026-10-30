#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <string>
#include <vector>

#include "game/world_mem.h"

namespace brps3::activity {

struct RscRequest {
    int index = 0;
    uint32_t addr = 0, state = 0;
    uint8_t dirty = 0, ran = 0;
};

struct OnDemandEntry {
    uint16_t index = 0;
    uint32_t tag = 0;
    uint8_t flag = 0;
};

struct RscState {
    bool ok = false;
    std::string why;
    uint16_t beginCat0 = 0, beginCat1 = 0;
    uint32_t mode = 0;
    uint16_t handleA = 0, handleB = 0;
    uint8_t dirtyAdd = 0, dirtyDel = 0;
    uint16_t cursor = 0xFFFF;             
    uint16_t odHead = 0xFFFF;
    size_t odCount = 0;                   
    bool odTruncated = false;
    std::vector<OnDemandEntry> onDemand;  
    std::vector<RscRequest> requests;     
    bool loaderBusy() const { return cursor != 0xFFFF; }
};
inline constexpr size_t kMaxShown = 64, kOdWalkGuard = 4096;
 
 
RscState readRscState(world::Reader& mem);

struct WtlState {
    bool ok = false;
    std::string why;
    uint32_t globals = 0;
    uint32_t inflightDet = 0, inflightNondet = 0;    
    uint32_t queueA = 0, queueB = 0, queueC = 0;      
    uint16_t cursor = 0xFFFF;
    bool idle() const { return queueA == 0xFFFFFFFFu && queueB == 0xFFFFFFFFu && queueC == 0xFFFFFFFFu && cursor == 0xFFFF; }
};
WtlState readWtlState(world::Reader& mem);            

}   
