#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/ppc.h"
#include "game/world_mem.h"

namespace brps3::movement {

inline constexpr const char* kTaskName = "movement.tick";
inline constexpr const char* kOpkTaskName = "movement.opk";
inline constexpr uint32_t kMagic = 0x4D4F5631;    

 
namespace ctl {
inline constexpr uint32_t MAGIC = 0x00, FRAMES = 0x04;
 
inline constexpr uint32_t CONFIG = 0x08;
inline constexpr uint32_t PAD_ON = 0x08;
inline constexpr uint32_t P_PROBE = 0x0C;        
inline constexpr uint32_t P_HANDLE = 0x10, P_ELEM = 0x14, P_ENTRY = 0x18, P_ROW = 0x1C, P_BODY = 0x20, P_PSLOT = 0x24;
inline constexpr uint32_t FLY_ON = 0x28, FLY_BRAKE = 0x2C, FLY_PREC = 0x30;
inline constexpr uint32_t FLY_STEP = 0x34;       
inline constexpr uint32_t FLY_PFAC = 0x38;       
inline constexpr uint32_t OPK_GATHER = 0x3C, OPK_EDGE = 0x40;
inline constexpr uint32_t OPK_DIST = 0x44;       
inline constexpr uint32_t CONFIG_END = 0x48;
 
inline constexpr uint32_t FLY_HOVER = 0x48;      
inline constexpr uint32_t FLY_SCALE = 0x4C;      
inline constexpr uint32_t EPS = 0x50;            
inline constexpr uint32_t OPK_EDGEZ = 0x54;      
inline constexpr uint32_t OPK_LIFT = 0x58;       
inline constexpr uint32_t OPK_MIN = 0x5C;        
inline constexpr uint32_t PAD_MASKS = 0x60;      
 
inline constexpr uint32_t PAD_HELD = 0xA0, PAD_SEQ = 0xA4, PAD_CONN = 0xA8;
inline constexpr uint32_t PAD_STICKS = 0xAC;     
inline constexpr uint32_t P_STATUS = 0xB4;       
inline constexpr uint32_t FLY_STATUS = 0xB8;     
inline constexpr uint32_t FLY_COUNT = 0xBC;      
inline constexpr uint32_t FLY_MOVES = 0xC0;      
inline constexpr uint32_t OPK_STATUS = 0xC4;     
inline constexpr uint32_t OPK_MOVED = 0xC8;      
inline constexpr uint32_t OPK_SEEN = 0xCC;       
inline constexpr uint32_t OPK_TX = 0xD0, OPK_TY = 0xD4, OPK_TZ = 0xD8;    
inline constexpr uint32_t PAD_RING = 0xE0;       
inline constexpr uint32_t TMP64 = 0x120;         
 
inline constexpr uint32_t WARP_N = 0x128;
inline constexpr uint32_t WARP_X = 0x12C, WARP_Y = 0x130, WARP_Z = 0x134;
 
 
inline constexpr uint32_t FWD_MIN = 0x138, FWD_MAX = 0x13C;    
inline constexpr uint32_t SIZE = 0x140;
}   

enum class PlayerCheck : uint32_t { Ok = 0, Off = 1, Body = 2, Identity = 3 };
 
enum class FlyStatus : uint32_t { Flying = 0, Off = 1, Body = 2, Identity = 3, NoController = 4, BadForward = 5 };
enum class OpkStatus : uint32_t { Ran = 0, Off = 1, Body = 2, Identity = 3, NoWorld = 5, NoTarget = 6 };
const char* flyStatusText(uint32_t s);
const char* opkStatusText(uint32_t s);
const char* playerCheckText(uint32_t s);

 
struct Control {
     
    bool padOn = false;
    uint32_t held = 0, seq = 0, connected = 0;
    int16_t sticks[4] = {};
    std::array<uint32_t, 16> ring{};
     
    bool probe = false;
    uint32_t handle = 0, element = 0, entry = 0, row = 0, body = 0, physicsSlot = 0;
    uint32_t playerStatus = 1;
     
    bool flyOn = false, brake = false, precision = true;
    float step = 1.0f, precisionFactor = 0.2f;
    uint32_t flyStatus = 1, flyCount = 0, flyMoves = 0;
     
    bool gather = false, edge = false;
    float dist = 5.0f;
    world::Vec3 target;
    uint32_t opkMoved = 0, opkSeen = 0, opkStatus = 1;
     
    uint32_t warpFrames = 0;
    world::Vec3 warp;
    uint32_t frames = 0;
    uint32_t magic = kMagic;
};
 
std::vector<uint8_t> encodeControl(const Control& c);
 
std::vector<uint8_t> encodeConfig(const Control& c);
Control decodeControl(const world::Bytes& b);    

 
std::vector<uint8_t> tickProgram(uint32_t base);    
std::vector<uint8_t> opkProgram(uint32_t base);     
uint32_t tickProgramSize();              
uint32_t opkProgramSize();

 
float stepPerFrame(float speed, float fps);

 
 
namespace enc {
using namespace ::brps3::ppc::enc;
}   

}   
