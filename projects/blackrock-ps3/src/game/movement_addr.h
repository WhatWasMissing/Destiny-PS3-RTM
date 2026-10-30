#pragma once
 
 
 
 
 
 
 
#include <cstdint>
#include <utility>
#include <vector>

namespace brps3::movement {

using Fingerprints = std::vector<std::pair<uint32_t, const char*>>;

namespace addr {
 
inline constexpr uint32_t HKP_WORLD_PTR = 0x1897CC8;        
inline constexpr uint32_t PHYSICS_MAPPING_PTR = 0x1A203E4;  
inline constexpr uint32_t VT_HKP_WORLD = 0x13E1640;         
inline constexpr uint32_t VT_RIGID_BODY = 0x13E1F38;        
inline constexpr uint32_t VT_ISLAND = 0x13E2060;            
inline constexpr uint32_t VT_CHAR_MOTION = 0x13E1FA0;       

 
inline constexpr uint32_t PAD_BLOCK = 0x1915A60;            
inline constexpr uint32_t PAD_NEUTRAL_FLAG = 0x1915A61;     
inline constexpr uint32_t PAD_FRAME0 = 0x1915A70;           
inline constexpr uint32_t PAD_PORT0 = 0x1915A80;            
inline constexpr uint32_t PAD_FRAME_INDEX = 0x19166F0;      
inline constexpr uint32_t PAD_FRAME_STRIDE = 0x640;

 
inline constexpr uint32_t VIEW_STRUCT = 0x1AE6EE0;          
inline constexpr uint32_t VIEW_MATRIX = 0x1AE6F10;          
inline constexpr uint32_t PROJ_MATRIX = 0x1AE6F50;          
inline constexpr uint32_t SCREEN_SIZE = 0x1AE7344;          
inline constexpr uint32_t VIEW_READ = 0x438;                

 
inline constexpr uint32_t PAD_POLL_PORT = 0xCF52C;          
inline constexpr uint32_t PAD_POLL_PORTS = 0xCFC14;         
inline constexpr uint32_t INPUT_UPDATE = 0xCEBB0;           
inline constexpr uint32_t PHYSICS_COMPONENT = 0x1B92A4;     
inline constexpr uint32_t SET_LINEAR_VELOCITY = 0x5E77C;    
inline constexpr uint32_t HKP_WORLD_GETTER = 0x1B23C8;      
inline constexpr uint32_t WARP_TO_POSITION = 0x9BB310;      
inline constexpr uint32_t ISLAND_DTOR = 0x6D400;            
inline constexpr uint32_t VIEW_SETUP = 0x619A08;            
inline constexpr uint32_t VIEW_SET_SIZE = 0x6156EC;         
inline constexpr uint32_t VIEW_SET_CAMERA = 0x61541C;       
}   

 
namespace hk {
inline constexpr uint32_t WORLD_ISLANDS_ACTIVE = 0x28;      
inline constexpr uint32_t WORLD_ISLANDS_INACTIVE = 0x34;
inline constexpr uint32_t ISLAND_ENTITIES = 0x4C;           
inline constexpr uint32_t ISLAND_ENTITY_COUNT = 0x50;
inline constexpr uint32_t MAX_ISLANDS = 0x40;               
inline constexpr uint32_t MAX_ENTITIES = 0x1000;
inline constexpr uint32_t BODY_WORLD = 0x08;
inline constexpr uint32_t BODY_FILTER = 0x2C;
inline constexpr uint32_t BODY_MOTION = 0xE0;               
inline constexpr uint32_t BODY_MOTION_TYPE = 0xE8;          
inline constexpr uint32_t BODY_XFORM_POS = 0x120;           
inline constexpr uint32_t BODY_COM1 = 0x140;                
inline constexpr uint32_t BODY_LINVEL = 0x1B0;              
inline constexpr uint32_t BODY_ANGVEL = 0x1C0;
inline constexpr uint32_t BODY_READ = 0x1D0;                
}   

 
 
 
 
namespace chain {
inline constexpr uint32_t SOBJ_ARENA = 0x3C;
inline constexpr uint32_t ARENA_DEF_TAG = 0x04;
inline constexpr uint32_t DEF_COUNT = 0x60;
inline constexpr uint32_t DEF_TABLE = 0x78;
inline constexpr uint32_t DEF_STRIDE = 0x0C;
inline constexpr uint32_t PHYSICS_INDEX = 2;
inline constexpr uint32_t DEF_OFFSET = DEF_TABLE + PHYSICS_INDEX * DEF_STRIDE;    
inline constexpr uint32_t COMP_PHYSICS = 0xF8;              
inline constexpr uint32_t COMP_COUNT = 0xFC;
inline constexpr uint32_t COMP_FLAG = 0x174;
inline constexpr uint32_t PHYSICS_BODY = 0x20;              
inline constexpr uint32_t MAX_COUNT = 64;
inline constexpr uint32_t MAX_OFFSET = 0x100000;
}   

 
namespace pad {
inline constexpr uint32_t STATE_SIZE = 0xC0;
inline constexpr uint32_t TRIGGER_L2 = 0x00, TRIGGER_R2 = 0x01;       
inline constexpr uint32_t TRIGGER_COUNTER = 0x04;                     
inline constexpr uint32_t BUTTON_COUNTER = 0x06;                      
inline constexpr uint32_t BUTTON_HOLD = 0x1E;                         
inline constexpr uint32_t STICK_LX = 0x44, STICK_LY = 0x46, STICK_RX = 0x48, STICK_RY = 0x4A;    
inline constexpr uint32_t READ = 0x4C;                                
inline constexpr int BUTTONS = 14;                                    
}   

 
namespace d1 {
inline constexpr float HOVER_Z = 0.4f;                      
inline constexpr float FLY_SPEED = 0.5f;                    
inline constexpr float FLY_SPEED_MAX = 100.0f;
inline constexpr float PRECISION = 0.2f;                    
inline constexpr float STICK_SCALE = 0.00003051850947599858f;    
inline constexpr int DEADZONE = 8000;
inline constexpr float RIGHT_EPS = 0.0001f;
inline constexpr float XBOX_TICK_MS = 16.0f;
inline constexpr float OPK_DIST = 5.0f;                     
inline constexpr float OPK_LIFT = 1.2f;
inline constexpr float OPK_MIN_LEN = 0.001f;
inline constexpr float EDGE_Z = 10000.0f;                   
inline constexpr float TP_EDGE = 500.0f;                    
inline constexpr float TP_LIMIT = 10000000.0f;              
}   

 
extern const Fingerprints kPadFingerprints;        
extern const Fingerprints kPhysicsFingerprints;    
extern const Fingerprints kViewFingerprints;       
 
Fingerprints allFingerprints();

}   
