#pragma once
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <utility>
#include <vector>

namespace brps3::loot {

using Fingerprints = std::vector<std::pair<uint32_t, const char*>>;

 
inline constexpr uint32_t SLICE_MANAGER_PTR = 0x1A1DC80;    
inline constexpr uint32_t WORLD_PTR = 0x1ABC89C;            
inline constexpr uint32_t WORLD_STATE = 0x118;
inline constexpr uint32_t STATE_NAMES = 0x12F7F00;          
inline constexpr uint32_t WORLD_IN_WORLD = 36, WORLD_ORBIT = 27, WORLD_CHARACTER_SELECT = 23;
inline constexpr uint32_t SLICE_CURRENT = 0x110C, SLICE_PENDING = 0x1110, SLICE_LOADED = 0x1114, SLICE_TABLE = 0x8;
inline constexpr uint32_t ENGINE_CLOCK = 0x1A1D678;         

 
inline constexpr uint32_t INTERACTABLE_LIST = 0x1AD704C;    
inline constexpr uint32_t SOBJECT_POOL = 0x1A1D9E0;         
inline constexpr uint32_t CLOCK_OBJECT_PTR = 0x1AB6710;     
inline constexpr uint32_t AUTHORITY_BITSET = 0x1A21138;     
 
namespace chest {
inline constexpr uint32_t SEQUENCE = 0x00;      
inline constexpr uint32_t DEF_OFFSET = 0x04;    
inline constexpr uint32_t CLASS = 0x08;         
inline constexpr uint32_t HANDLE = 0x14;        
inline constexpr uint32_t OWNER = 0x1C;         
inline constexpr uint32_t OPEN = 0x1C6;         
inline constexpr uint32_t LOOTED = 0x1C7;       
inline constexpr uint32_t COUNTDOWN = 0x1C8;    
inline constexpr uint32_t AUTHORITY = 0x1CC;    
inline constexpr uint32_t LIFETIME = 0x1D0;     
inline constexpr uint32_t EXPIRY = 0x1F0;       
inline constexpr uint32_t TIMER_SPAN = 0x0E;    
inline constexpr uint32_t READ_SIZE = 0x1F8;    
}   
 
namespace chestdef {
inline constexpr uint32_t OPEN_DELAY = 0x188, THRESHOLD = 0x18C, ROOT = 0x190, KEY = 0x194, LIFETIME_S = 0x1B4;
inline constexpr uint32_t SIZE = 0x210;
}   
inline constexpr uint32_t POE_ROOT = 0x7A0FD954;            
inline constexpr uint32_t POE_MAIN_KEY = 0xDDA58EF3, POE_SIDE_KEY = 0xDDA58EF2;
inline constexpr uint32_t POE_COMPONENT_CLASS = 0x808034A1;  
inline constexpr float POE_MAIN_LIFETIME_S = 300.0f, POE_SIDE_LIFETIME_S = 90.0f;
 
 
inline constexpr uint32_t XBOX_POE_MAP = 0x811BC003, XBOX_POE_MAIN_SEQUENCE = 0x811944DF, XBOX_POE_SIDE_SEQUENCE = 0x811944DA;
 
inline constexpr int32_t TICKS_PER_SECOND = 30, REFILL_AT = 1800, REFILL_TO = 2700, MIN_TICKS = 900;
inline constexpr uint32_t MAX_LIST_NODES = 512;            

 
inline constexpr uint32_t AFK_CONFIG_ROOT = 0x1A1D2E4;      
inline constexpr uint32_t AFK_BLOCK = 0x30E0;
inline constexpr uint32_t AFK_LIMIT_MODE0 = 0x94, AFK_LIMIT_MODE1 = 0x98, AFK_LIMIT_MODE2 = 0x9C;    
inline constexpr uint32_t AFK_ORIGINAL_MODE0 = 900000, AFK_ORIGINAL_MODE1 = 300000, AFK_ORIGINAL_MODE2 = 3600000;
inline constexpr uint32_t AFK_MODE_BLOCK = 0x1ABD4B8;       
inline constexpr uint32_t AFK_IN_WORLD = 0x1F2C, AFK_MODE = 0x1F34;    
inline constexpr uint32_t AFK_SELECTED_PLAYER = 0x1897C48;  
inline constexpr uint32_t AFK_SLOT = 0x33C;
inline constexpr uint32_t AFK_PLAYER_BASE = 0x1B6B060, AFK_PLAYER_STRIDE = 0x140;    
inline constexpr uint32_t AFK_ACTIVITY = 0x118, AFK_ELAPSED = 0x120;                
inline constexpr uint64_t AFK_RESET_AT_MS = 30000;           
inline constexpr int AFK_POLL_MS = 5000;                     

 
inline constexpr uint32_t PICKUP_SITE = 0xC4371C;           
inline constexpr uint32_t PICKUP_SITE_ORIGINAL = 0x4BFFA849;
inline constexpr uint32_t PICKUP_ELIGIBILITY = 0xC3DF64;    
inline constexpr uint32_t PICKUP_PROVIDER = 0x1897CEC;      
inline constexpr uint32_t PICKUP_PROVIDER_VTABLE = 0x13E6478;
inline constexpr uint32_t INVESTMENT_ROOT_TAG = 0x80A58001;
inline constexpr uint32_t ITEM_TABLE_SLOT = 0x144;          
inline constexpr uint32_t PS3_ITEM_TABLE_TAG = 0x80A5F241;  
inline constexpr uint32_t ITEM_SCHEMA_A = 0x80800184, ITEM_SCHEMA_B = 0x808016BD;
inline constexpr uint32_t ITEM_DEFINITIONS = 6557, IMAGO_DEFINITION = 5258;
inline constexpr const char* ITEM_TABLE_SHA256 = "a6d0c8d7518514dda7a21349de06162a11ebe0798447d67ee618b5e0c0d516e1";

 
inline constexpr uint32_t GRIM_SETTER = 0xC5BEC0;           
inline constexpr uint32_t GRIM_CONTEXT_GET = 0x2A8080;      
inline constexpr uint32_t GRIM_CONTEXT = 0x1AB6250;         
inline constexpr uint32_t GRIM_CONTEXT_VTABLE = 0x1411170;
inline constexpr uint32_t GRIM_ACCOUNT_PTR = 0x1AB6258;     
inline constexpr uint32_t GRIM_ACCOUNT_FLAGS = 0x383C;      
inline constexpr uint32_t GRIM_FLAGS_SPAN = 2352;            
inline constexpr uint32_t GRIM_PROFILE_MGR = 0x1AD5FA4;     
inline constexpr uint32_t GRIM_PROFILE_META = 0x49518;       
inline constexpr uint32_t GRIM_PROFILE_ACCOUNT = 0x40A0;     
inline constexpr uint32_t GRIM_PROFILE_DESCRIPTORS = 0x60;   
inline constexpr uint32_t GRIM_PROFILE_LIMIT = 283384;       
inline constexpr uint32_t GRIM_SITE_ITEM_ACQUIRED = 0xC3EF10, GRIM_SITE_EXPLICIT_ROWS = 0xC48CE4;    
inline constexpr uint32_t PS3_GRIM_CORE_TAG = 0x80A5E1E4, PS3_GRIM_FLAGS_TAG = 0x80A5F24A;           
inline constexpr uint8_t GRIM_UNLOCKED = 2;

 
inline constexpr uint32_t LH_ROOT_PTR = 0x1AB6458;          
inline constexpr uint32_t LH_CONTEXT = 0x1AB6250;           
inline constexpr uint32_t LH_PERMISSION = 0x6B8C;           
inline constexpr uint32_t LH_FLYIN_PTR = 0x1AD6050;         
inline constexpr uint32_t LH_TRANSITION_MGR_PTR = 0x1ABC8AC;    
inline constexpr uint32_t LH_GLOBALS_PTR = 0x1ABC8C4;       
namespace mgr {
inline constexpr uint32_t PAUSE = 0x80, ENABLED = 0x88, STATE89 = 0x89, INSTANTIATE = 0x8A, TARGET_SLICE = 0x130, WORKER = 0x1D2;
inline constexpr uint32_t REQUEST_TARGET = 0x3A4, REQUEST_HASH = 0x3A8, REQUEST_FLAG = 0x3AC;
}   
inline constexpr uint32_t LH_GLOBALS_TARGET = 0xA8;
inline constexpr uint32_t LH_REQUEST_PREDICATE = 0x71ABA4;   
inline constexpr uint32_t LH_PREDICATE_GLOBALS = 0x1B28860;  
inline constexpr uint32_t LH_TELEPORT_REQUEST = 0x35097C;    
inline constexpr uint32_t LH_INSTANTIATE_EVENT = 0x354144;   
inline constexpr uint32_t LH_REGISTRY_INDEX = 0x1A1DEC0;     
inline constexpr uint32_t LH_ALLOC_DIRECTORY = 0x1917100;    
inline constexpr uint32_t LH_REGISTRY_META = 0x1550C;        
inline constexpr uint32_t LH_REGISTRY_RECORDS = 0x50C, LH_REGISTRY_RECORD = 28;    
inline constexpr uint32_t LH_PLAYER_ENTRY_PTR = 0x1A1D728;   
namespace small {
inline constexpr uint32_t IDENTITY = 0x18, HANDLE = 0x48, ENTITY = 0x50, ENTITY2 = 0x54;
}
namespace entity {
inline constexpr uint32_t BOUND_PLAYER = 0x130, INNER_REF = 0x134, IDENTITY_REF = 0x13C, LIFETIME_PHASE = 0x108;
inline constexpr uint32_t READ_SIZE = 0x1B8;
}
inline constexpr uint32_t LH_BINDING_REFRESH = 0x1584C0;     
inline constexpr uint32_t LH_SPAWN_READINESS = 0x3A890C;     
inline constexpr uint32_t LH_LIFETIME_GETTER = 0xC07EF0;     
inline constexpr uint32_t LH_FADE_PTR = 0x1ACBD44;           
inline constexpr uint32_t LH_PERMISSION_GATE = 0x36EA5C;     
inline constexpr uint32_t FNV_EMPTY = 0x811C9DC5;
inline constexpr uint32_t LH_TARGET_SLICE = 8, LH_START_SLICE = 9;
inline constexpr uint8_t LH_PHASE_NONE = 0xFF, LH_PHASE_ACCEPTED = 3;
 
inline constexpr uint32_t XBOX_LH_MAP = 0x8155C003, XBOX_LH_SLOT8 = 0x8154E003, XBOX_LH_SLOT8_VALUE = 0xFC;
inline constexpr uint32_t LH_TRIALS_CACHE = 0x1C;            

 
 
 
 
extern const Fingerprints kPoeFingerprints;
extern const Fingerprints kAfkFingerprints;        
extern const Fingerprints kPickupFingerprints;     
extern const Fingerprints kGrimoireFingerprints;   
extern const Fingerprints kLighthouseFingerprints; 

}   
