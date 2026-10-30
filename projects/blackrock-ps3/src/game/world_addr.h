#pragma once
 
 
 
 
 
 
 
 
#include <cstdint>
#include <utility>
#include <vector>

namespace brps3::world {

using Fingerprints = std::vector<std::pair<uint32_t, const char*>>;

namespace addr {
 
inline constexpr uint32_t PLAYER_DESC_PTR = 0x1B29450;      
inline constexpr uint32_t PLAYER_POOL = 0x1A1D720;          
inline constexpr uint32_t ATTACHMENT_ROOT_POOL = 0x1A1D7C0; 
inline constexpr uint32_t SOBJECT_GLOBALS_PTR = 0x1A1D9D4;  
inline constexpr uint32_t SOBJECT_POOL = 0x1A1D9E0;         
inline constexpr uint32_t SOBJECT_POOL_BANK = 0x1A1DA08;    
inline constexpr uint32_t ENGINE_CLOCK = 0x1A1D678;         
inline constexpr uint32_t TAG_TABLE_PTR = 0x1896D50;        
inline constexpr uint32_t DATUM_REGISTRATION = 0x1896D4C;   
inline constexpr uint32_t COMPONENT_ACCESSOR_POOL = 0x1A21CB0;    
inline constexpr uint32_t GLOBALS_REGISTRY = 0x19F2E38;     
inline constexpr uint32_t GLOBALS_REGISTRY_CAT3 = 0x19F2E5C;
inline constexpr uint32_t ALLOC_CONTEXT_POOL = 0x1917100;   
inline constexpr uint32_t FACTION_ALLEGIANCE_SLOT = 0x1A1F658;    
inline constexpr uint32_t FACTION_TABLE_SLOT = 0x1A1F65C;   
inline constexpr uint32_t FACTION_TABLE_CACHE = 0x1A1F660;  
inline constexpr uint32_t OBJECT_TYPE_NAMES = 0x189A0CC;    
inline constexpr uint32_t TYPE_KEY_ANCHOR_PRIMARY = 0x18E92D0;
inline constexpr uint32_t TYPE_KEY_ANCHOR_SECONDARY = 0x18DF4F0;

 
inline constexpr uint32_t ACTIVE_PLAYER_HANDLE_RESOLVE = 0x136B68;    
inline constexpr uint32_t PLAYER_ARRAY_INDEX_HANDLE = 0x136A3C;       
inline constexpr uint32_t TAG_REF_RESOLVE = 0x20B4D8;       
                                                            
inline constexpr uint32_t TAG_GET_CLASS = 0x107B34;         
inline constexpr uint32_t POSITION_OF = 0x284EEC;           
inline constexpr uint32_t GLOBALS_REGISTER = 0x115E34;      
inline constexpr uint32_t DATUM_POOL_REGISTER = 0xBE148;    
inline constexpr uint32_t FACTIONS_ARE_ALLIED = 0x19AE3C;   
inline constexpr uint32_t FACTION_PAIR_INDEX = 0x19AD34;
inline constexpr uint32_t FACTION_LOOKUP = 0x19B040;        
inline constexpr uint32_t FACTION_ALLEGIANCE_REGISTER = 0x19AC80;
inline constexpr uint32_t FACTION_CONSUMER = 0x19AEB0;      
inline constexpr uint32_t SOBJECT_NEW = 0x13ED38;           
inline constexpr uint32_t SOBJECT_DELETE = 0x13F4B8;        
}   

 
namespace sobj {
inline constexpr uint32_t STRIDE = 0xC0;
inline constexpr uint32_t FLAGS = 0x04;         
inline constexpr uint32_t TYPE = 0x08;          
inline constexpr uint32_t HANDLE = 0x0C;        
inline constexpr uint32_t PARENT = 0x2C;        
inline constexpr uint32_t COMPONENT_HEAD = 0x3C;    
inline constexpr uint32_t VECTOR40 = 0x40;      
inline constexpr uint32_t ATTACHMENT = 0x64;    
inline constexpr uint32_t COMPONENT = 0x78;     
inline constexpr uint32_t ROT = 0x80;           
inline constexpr uint32_t POS90 = 0x90;         
inline constexpr uint32_t SCALE = 0x9C;         
inline constexpr uint32_t ROT2 = 0xA0;          
inline constexpr uint32_t POSITION = 0xB0;      
inline constexpr uint32_t SCALE2 = 0xBC;
inline constexpr uint32_t FLAG_DELETING = 0x4;
}   

 
namespace pool {
inline constexpr uint32_t HEADER = 0x00, BASE = 0x08, STRIDE = 0x0C, CAPACITY = 0x10, COUNT = 0x14, IDENTITY_OFFSET = 0x18,
                          IDENTITY_STRIDE = 0x1C, MASK = 0x20, MASK2 = 0x24, CLASS = 0x2C, NAMESPACE = 0x30;
inline constexpr uint32_t HEADER_SIZE = 0x34;
}   

 
namespace player {
inline constexpr uint32_t ENTRY_DATUM = 0x4C;       
inline constexpr uint32_t ENTRY_HANDLE = 0x54;      
inline constexpr uint32_t ENTRY_FACTION = 0x90;     
inline constexpr uint32_t ENTRY_POSITION = 0xE0;    
inline constexpr uint32_t ENTRY_FORWARD = 0x110;    
inline constexpr uint32_t DESC_ELEMENT_SIZE = 0x2930;
inline constexpr uint32_t ELEM_HANDLE = 0x84;       
inline constexpr uint32_t ELEM_POSITION = 0x2810;   
inline constexpr uint32_t ELEM_OWNED_LIST = 0x265C;
}   

 
const char* typeName(uint8_t type);                 
inline constexpr uint8_t TYPE_BIPED = 9, TYPE_CREATURE = 10, TYPE_WEAPON = 11, TYPE_VEHICLE = 12, TYPE_TURRET = 13,
                         TYPE_PROP_GARBAGE = 6, TYPE_COUNT = 25;
bool typeIsRisky(uint8_t type);                     

 
extern const Fingerprints kFingerprints;
 
extern const Fingerprints kFactionFingerprints;

}   
