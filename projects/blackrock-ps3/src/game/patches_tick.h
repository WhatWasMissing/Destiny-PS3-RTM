#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "game/world_mem.h"

namespace brps3::patches::tick {

inline constexpr const char* kTaskName = "patches.tick";
inline constexpr uint32_t kBlockSize = 0x60;

enum Flag : uint32_t {
    Fov = 0x01,
    Speed = 0x02,
    Afk = 0x04,
    Viewmodel = 0x08,
    Respawn = 0x10,
    Suppress = 0x20,
    ValidBit = 0x40,
    AllFlags = 0x7F,
};

namespace off {
inline constexpr uint32_t FLAGS = 0x00, FOV = 0x04, SPEED = 0x08, SPEED_LAST = 0x0C, VM_ADDR = 0x10, VM_BITS = 0x14, VM_HANDLE = 0x18,
                          FRAMES = 0x1C, DESC = 0x20, ELEMENTS = 0x24, ELEMENT = 0x28, MODE = 0x2C, CALLS = 0x30, SEEN = 0x34,
                          SPEED_SKIPS = 0x38, SPEED_SEEN = 0x3C, ARG = 0x40, LAST_ARG = 0x48, VALID_FRAMES = 0x50,
                          SUPPRESS_FRAMES = 0x54;
}

 
inline constexpr uint32_t kSpeedMinBits = 0x3DCCCCCD;    
inline constexpr uint32_t kSpeedMaxBits = 0x40E00000;    

 
std::vector<uint8_t> program(uint32_t base);
uint32_t programSize();                 

 
struct Block {
    uint32_t flags = 0, fov = 0, speed = 0, speedLast = 0, vmAddr = 0, vmBits = 0, vmHandle = 0;
    uint32_t desc = 0, elements = 0, element = 0, mode = 0;
    uint64_t arg = 0;
    std::vector<uint8_t> encode() const;    
};

 
struct Counters {
    uint32_t flags = 0, frames = 0, calls = 0, seen = 0, speedSkips = 0, speedSeen = 0, validFrames = 0, suppressFrames = 0;
    uint64_t lastArg = 0;
};
Counters decode(const std::vector<uint8_t>& block);

}   

namespace brps3::patches {

 
inline constexpr float kFovMin = 30, kFovMax = 150, kFovDefault = 105;    
inline constexpr float kFovOff = -1.0f;                                    
inline constexpr float kSpeedMin = 0.1f, kSpeedMax = 7.0f, kSpeedDefault = 1.0f;
inline constexpr float kViewmodelMin = 30, kViewmodelMax = 170, kViewmodelDefault = 77;
inline constexpr double kDegToRad = 0.017453292519943295;

bool fovValid(float deg);                  
bool fovGlobalPlausible(float v);          
bool speedValid(float mult);               
bool viewmodelDegValid(float deg);         
bool viewmodelRadiansValid(float rad);     
float clampFov(float deg);
float clampSpeed(float mult);
float clampViewmodel(float deg);
uint32_t viewmodelBits(float deg);         

 
 
 
 
struct ViewmodelTarget {
    bool ok = false;
    std::string why;
    uint32_t handle = 0, base = 0, target = 0;
    uint32_t current = 0;                  
};
ViewmodelTarget resolveViewmodel(world::Reader& mem);

 
struct EntitleChain {
    uint32_t root = 0, valid = 0;          
    bool rootOk = false;
    uint32_t providerVtable = 0, providerRoot = 0, providerInner = 0;    
    uint32_t suppressObj = 0;              
    bool suppressOk = false;
    uint8_t suppress = 0;                  
};
EntitleChain readEntitleChain(world::Reader& mem);

}   
