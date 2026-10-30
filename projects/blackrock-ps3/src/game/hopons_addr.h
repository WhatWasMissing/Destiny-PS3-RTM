#pragma once
 
 
 
 
 
#include <cstdint>
#include <utility>
#include <vector>

namespace brps3::hopons {

using Fingerprints = std::vector<std::pair<uint32_t, const char*>>;

namespace addr {
 
inline constexpr uint32_t TAG_TABLE_PTR = 0x1896D50;         
inline constexpr uint32_t IDENTITY_MGR_PTR = 0x1896D4C;      
inline constexpr uint32_t ACCESSOR_POOL = 0x1A21CB0;         
inline constexpr uint32_t SOBJECT_POOL = 0x1A1D9E0;
inline constexpr uint32_t SOBJECT_ALLOC_BITMAP = 0x1A21138;  
inline constexpr uint32_t ACTOR_POOL = 0x1A1E160;            
inline constexpr uint32_t CONTEXT_POOL = 0x1917100;          
inline constexpr uint32_t REFMGR_HANDLE = 0x1A1DEC0;         
inline constexpr uint32_t BUNDLE_INSTANCES = 0x1A1D460;      
inline constexpr uint32_t BUNDLE_REGISTRATION = 0x1A1D470;
inline constexpr uint32_t BUNDLE_ARRAY = 0x1A1D478;          
inline constexpr uint32_t BUNDLE_IDENTITY_OFFSET = 0x1A1D488;    
inline constexpr uint32_t BUNDLE_IDENTITY_STRIDE = 0x1A1D48C;    
inline constexpr uint32_t BUNDLE_ALLOC_HANDLE = 0x1A1D4F0;   
inline constexpr uint32_t BUNDLE_ACCESSORS = 0x1A1D580;      
inline constexpr uint32_t CLIENT_WORLDS_POOL = 0x1A20BF0;    
inline constexpr uint32_t CLIENT_GLOBALS_POOL = 0x1A20C70;   
inline constexpr uint32_t GAME_TIME_A = 0x1AB6710;           
inline constexpr uint32_t GAME_TIME_B = 0x1AB6714;
inline constexpr uint32_t CLOCK_SOURCE_PTR = 0x1897C44;      
inline constexpr uint32_t CLOCK_SOURCE_OBJECT = 0x1AB9D7C;
inline constexpr uint32_t CLOCK_SOURCE_VTABLE = 0x13E7CF0;   
inline constexpr uint32_t CLOCK_SOURCE_OPD = 0x1853DF0;
inline constexpr uint32_t CLOCK_SOURCE_CODE = 0x2FB484;
inline constexpr uint32_t NETWORK_FACET_MANAGER = 0x1A20FC0;
inline constexpr uint32_t NETWORK_FACETS_POOL = 0x1A20FD0;   
inline constexpr uint32_t ACTIVITY_BUNDLES_POOL = 0x1A21050;
 
inline constexpr uint32_t CLASS_PLACEMENT_PTR = 0x18DED08;   
inline constexpr uint32_t CLASS_DEFINITION_PTR = 0x18DED0C;  
inline constexpr uint32_t CLASS_HEADER_PTR = 0x18DED10;      
inline constexpr uint32_t CLASS_WRAPPER_PTR = 0x18DF1BC;     
 
inline constexpr uint32_t TAG_HEAP_PTR = 0x1892538;          
inline constexpr uint32_t TAG_HEAP_HANDLE = 0x19A4040;       
inline constexpr uint32_t FIXED_TAG_POOLS = 0x189253C;       
inline constexpr uint32_t FIXED_TAG_POOL_ROWS = 6;
inline constexpr uint32_t AI_SCHEMA_PAYLOAD = 0x18E8E54;     
inline constexpr uint32_t AI_SCHEMA_ALLOCATION = 0x18E8E58;  
inline constexpr uint32_t AI_SCHEMA_WANTED = 0x18E8E5C;      

 
inline constexpr uint32_t PLACEMENT_DATA_NEW = 0x1F7F74;     
inline constexpr uint32_t HOP_ATTACH = 0x1F7D7C;             
inline constexpr uint32_t HOP_REMOVE = 0xC72170;             
inline constexpr uint32_t ACCESSOR_OWNER = 0x1F8C1C;         
inline constexpr uint32_t IDENTITY_LOOKUP = 0xBEE098;        
inline constexpr uint32_t REFERENCE_VALIDATE = 0xBEE188;     
inline constexpr uint32_t PLACEMENT_ARENA_ALLOC = 0x1F7DAC;  
inline constexpr uint32_t PLACEMENT_DESC_ALLOC = 0x1F7E68;   
inline constexpr uint32_t SOBJECT_DELETE = 0x13F4B8;         
inline constexpr uint32_t SEQUENCE_CLEANUP = 0xC8D750;       
inline constexpr uint32_t SEQUENCE_SET_INPUT = 0xC8E164;     
inline constexpr uint32_t SEQUENCE_TICKS = 0x20FB58;         
inline constexpr uint32_t SEQUENCE_TIMING = 0xC8DFF0;        
inline constexpr uint32_t SEQUENCE_ACTIVATE = 0xC8D988;      
inline constexpr uint32_t TYPED_INPUT_RESOLVE = 0x1290C;     
inline constexpr uint32_t TIMING_MIRROR = 0x20FC00;          
inline constexpr uint32_t TICKS_FROM_RATE = 0x1DA668;
inline constexpr uint32_t GAME_TIME_FREQ = 0x2BA4E0;
inline constexpr uint32_t GAME_TIME_C = 0x2B9DE8;
inline constexpr uint32_t CLOCK_THUNK_BODY = 0x12E340;       
inline constexpr uint32_t CLOCK_SOURCE_BL = 0x2FB490;        
inline constexpr uint32_t TIMER_SET_INPUT = 0xC100B0;        
inline constexpr uint32_t TIMER_TICK = 0xC10100;
inline constexpr uint32_t SPAWNER_REMOVE_ACTOR = 0x255FA8;   
inline constexpr uint32_t SPAWNER_REMOVE_METHOD = 0x256118;  
inline constexpr uint32_t SPAWNER_REFRESH = 0x256E30;        
inline constexpr uint32_t SQUAD_ROSTER_REMOVE = 0x2481A8;    
inline constexpr uint32_t FACET_UNLINK = 0x1E4ED4;           
inline constexpr uint32_t SQUAD_ROSTER_INIT = 0x245210;      
inline constexpr uint32_t AI_CONTEXT_PRODUCER = 0x245B40;    
inline constexpr uint32_t PLACEMENT_ADD_COMPONENT = 0x1F7724;
inline constexpr uint32_t PLACEMENT_BUDGET = 0x201780;       
}   

 
struct Anchor {
    const char* what;
    uint32_t slot;           
    uint32_t expect;         
};
inline constexpr Anchor kPlacementAnchors[] = {
    {"activation allocation (0xC8D988)", 0x18DF4E0, 0x1AB4E70},
    {"activation wanted", 0x18DF4DC, 0x1AB4E00},
    {"budget allocation 1 (0x201780)", 0x18DF114, 0x1AAE410},
    {"budget wanted 1", 0x18DF110, 0x1AAE400},
    {"budget allocation 2", 0x18DF330, 0x1AB2EE0},
    {"budget wanted 2", 0x18DF32C, 0x1AB2ED0},
};
inline constexpr Anchor kClassAnchors[] = {
    {"placement class (80800906)", addr::CLASS_PLACEMENT_PTR, 0x1AAA090},
    {"definition class (80800734)", addr::CLASS_DEFINITION_PTR, 0x1AAA0B0},
    {"placement header type (80800406)", addr::CLASS_HEADER_PTR, 0x1AAA420},
    {"wrapper class (80800477)", addr::CLASS_WRAPPER_PTR, 0x1AAFA40},
};
inline constexpr Anchor kAiSchemaAnchors[] = {
    {"AI payload schema", addr::AI_SCHEMA_PAYLOAD, 0x1BB9760},
    {"AI allocation schema", addr::AI_SCHEMA_ALLOCATION, 0x1BB9780},
    {"AI wanted schema", addr::AI_SCHEMA_WANTED, 0x1BB9790},
};
 
inline constexpr uint32_t kAiSchemaKeys[] = {0x0086D95F, 0xC93A789E, 0x5515FBA6};

 
namespace cls {
inline constexpr uint32_t ENTITY = 0x80800734;
inline constexpr uint32_t PLACEMENT = 0x80800906;
inline constexpr uint32_t ENTITY_MARKER = 0x80800406;      
inline constexpr uint32_t SEQUENCE = 0x808026B9;
inline constexpr uint32_t SEQUENCE_INPUT = 0x808029A1;
inline constexpr uint32_t TIMER = 0x80800497;
inline constexpr uint32_t TIMER_INPUT = 0x80800725;
inline constexpr uint32_t SPAWNER = 0x80800810;
inline constexpr uint32_t SQUAD = 0x808007BC;
inline constexpr uint32_t SQUAD_DEFINITION = 0x808012D9;
inline constexpr uint32_t ARRAY = 0x80800184;
inline constexpr uint32_t SQUAD_MEMBER = 0x808014D6;
inline constexpr uint32_t COMBATANT_ROW = 0x80801348;
inline constexpr uint32_t BLOB = 0x80800000;
inline constexpr uint32_t SPATIAL_OBJECT = 0x8080092E;
}   

 
inline constexpr uint8_t TYPE_HOP_ON = 20, TYPE_BIPED_ATTACHMENT = 21, TYPE_WEAPON_ATTACHMENT = 22, TYPE_SHIP_ATTACHMENT = 23;
inline bool isHopType(uint8_t t) { return t >= TYPE_HOP_ON && t <= TYPE_SHIP_ATTACHMENT; }

 
inline constexpr uint32_t kArenaSize = 0x840;

 
 
inline const Fingerprints kHopFingerprints = {
    {addr::PLACEMENT_DATA_NEW, "7C0802A648BA0AA5F821FF61F80100B0"},
    {addr::HOP_ATTACH, "7C0802A648BA0CA5F821FF81F8010090"},
    {addr::HOP_REMOVE, "7C0802A6481268B1F821FF81F8010090"},
    {addr::ACCESSOR_OWNER, "3CA001A2548404FE30A51CB080C5000C"},
    {addr::IDENTITY_LOOKUP, "38C0FFFF2C04FFFF60C5000041820060"},
    {addr::REFERENCE_VALIDATE, "7C0802A6481AA899F821FFC1F8010050"},
    {addr::PLACEMENT_ARENA_ALLOC, "7C0802A648BA0C71F821FF81F8010090"},
    {addr::PLACEMENT_DESC_ALLOC, "7C0802A648BA0BB5F821FF81F8010090"},
};
 
inline const Fingerprints kTimerFingerprints = {
    {addr::TIMER_SET_INPUT, "7C0802A648188971F821FF81F80100903CA0018E80C40000607F000080840004"},
    {addr::TIMER_TICK, "7C0802A64818891DF821FF71F80100A0607F00003BC000004B5FF4D12C030000"},
};
 
inline const Fingerprints kCleanupFingerprints = {
    {addr::SEQUENCE_CLEANUP, "7C0802A64810B2D1F821FF81F8010090607F000038A0000030610070309F0128"},
    {addr::SOBJECT_DELETE, "7C0802A648C59559F821FF41F80100D0"},
    {addr::REFERENCE_VALIDATE, "7C0802A6481AA899F821FFC1F8010050"},
};
 
inline const Fingerprints kSpawnerRemoveFingerprints = {
    {addr::SPAWNER_REMOVE_ACTOR, "7C0802A648B42A5DF821FF21F80100F0"},
    {addr::SPAWNER_REMOVE_METHOD, "F821FF917C0802A6F801008080830174"},
    {addr::SPAWNER_REFRESH, "7C0802A648B41BD9F821FF51F80100C0"},
    {addr::SQUAD_ROSTER_REMOVE, "7C0802A648B50865F821FF51F80100C0"},
    {addr::FACET_UNLINK, "7C0802A648BB3B49F821FF71F80100A0"},
    {addr::SOBJECT_DELETE, "7C0802A648C59559F821FF41F80100D0"},
};
 
inline const Fingerprints kRestartFingerprints = {
    {addr::SEQUENCE_SET_INPUT, "7C0802A64810A8A9F821FF51F80100C03CA0018F80C40000607E000080840004"},
    {addr::SEQUENCE_TICKS, "7C0802A648B88EC9F821FF81F8010090"},
    {addr::SEQUENCE_TIMING, "7C0802A63181FFF84810AAD94810AB2D"},
    {addr::SEQUENCE_ACTIVATE, "7C0802A63181FFF84810B1394810B195"},
    {addr::SEQUENCE_CLEANUP, "7C0802A64810B2D1F821FF81F8010090607F000038A0000030610070309F0128"},
    {addr::TYPED_INPUT_RESOLVE, "806300044E800020"},
    {addr::TIMING_MIRROR, "7C0802A648B88E21F821FF71F80100A0"},
    {addr::TICKS_FROM_RATE, "7C0802A63181FFF848BBE46948BBE4B5"},
    {addr::GAME_TIME_FREQ, "3C6001AB80636710806300047C6307B44E800020"},
    {addr::GAME_TIME_C, "3C6001AB806367108063000C7C6307B44E800020"},
    {addr::CLOCK_THUNK_BODY, "3C60018980637C44808300008084002C80840000"},
    {addr::CLOCK_SOURCE_BL, "4BFBF051"},
    {addr::SOBJECT_DELETE, "7C0802A648C59559F821FF41F80100D0"},
};
 
inline const Fingerprints kAiFingerprints = {
    {addr::AI_CONTEXT_PRODUCER, "7C0802A648B52EC1F821F5D1F8010A40"},
    {addr::PLACEMENT_ADD_COMPONENT, "7C0802A648BA12E5F821FEC1F8010150"},
    {addr::PLACEMENT_BUDGET, "7C0802A648B9729DF821FF81F8010090"},
    {addr::SEQUENCE_ACTIVATE, "7C0802A63181FFF84810B1394810B195"},
    {addr::PLACEMENT_DATA_NEW, "7C0802A648BA0AA5F821FF61F80100B0"},
};
 
std::vector<std::pair<const char*, const Fingerprints*>> allFingerprintSets();

}   
