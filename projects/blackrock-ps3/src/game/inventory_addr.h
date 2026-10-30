#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <utility>
#include <vector>

namespace brps3::inventory {

using Fingerprints = std::vector<std::pair<uint32_t, const char*>>;

namespace addr {
 
inline constexpr uint32_t PROFILE_MANAGER = 0x1AD5FA4;       
inline constexpr uint32_t INVESTMENT_IFACE = 0x1897CEC;      
inline constexpr uint32_t INVESTMENT_OBJ = 0x1897CF0;
inline constexpr uint32_t INVESTMENT_VTABLE = 0x13E6478;     
inline constexpr uint32_t TAG_TABLE_PTR = 0x1896D50;         
inline constexpr uint32_t READY_COUNT = 0x1A1DCF4;           
inline constexpr uint32_t READY_HEAD = 0x1A1DCFC;            
inline constexpr uint32_t READY_POOL = 0x1A1DD08;            
inline constexpr uint32_t ORBIT_PROFILE_CACHE = 0x1AB66A8;   
inline constexpr uint32_t PLAYER_DESCRIPTOR = 0x1B29450;     
inline constexpr uint32_t ENGINE_CLOCK = 0x1A1D678;          
inline constexpr uint32_t TAG_CACHE_GLOBALS = 0x1A2081C;     
inline constexpr uint32_t TAG_CACHE_BUCKETS = 0x1A2085C;     
inline constexpr uint32_t TAG_CACHE_DEFS = 0x1A208B8;        
inline constexpr uint32_t TAG_CACHE_INDEX = 0x1A20954;       

 
inline constexpr uint32_t ITEM_ACTION_402 = 0x471D9C;        
inline constexpr uint32_t BUILDER_402 = 0xC4E584;
inline constexpr uint32_t EQUIP_403 = 0x471C6C;              
inline constexpr uint32_t BUILDER_403 = 0xC4E490;
inline constexpr uint32_t UNEQUIP_404 = 0x471ED4;            
inline constexpr uint32_t BUILDER_404 = 0xC4E690;
inline constexpr uint32_t TRANSFER_405 = 0x472004;           
inline constexpr uint32_t BUILDER_405 = 0xC4E784;
inline constexpr uint32_t VENDOR_CLAIM_904 = 0x473088;       
inline constexpr uint32_t BUILDER_904 = 0xC4F4EC;
inline constexpr uint32_t ACTION_DISPATCH = 0x7E12FC;        
inline constexpr uint32_t PROVIDER_CHECK = 0x1CB82C;         
inline constexpr uint32_t REQUEST_STATE = 0x475D50;          
inline constexpr uint32_t MANAGER_GET = 0x47C1A0;
inline constexpr uint32_t FAMILY_LOOKUP = 0x47E3CC;
inline constexpr uint32_t ROW_READER = 0x8208B8;
inline constexpr uint32_t READY_CHECK = 0x152030;
inline constexpr uint32_t PROFILE_APPLY = 0x71F86C;          
inline constexpr uint32_t PROFILE_COPY = 0x720198;           
inline constexpr uint32_t PROFILE_CACHE_LOOKUP = 0x2B684C;   
inline constexpr uint32_t EDITOR_CODECHECK_TAG = 0x2A6870;   
inline constexpr uint32_t EDITOR_CODECHECK_2 = 0x2A6954;     
 
inline constexpr uint32_t INVEST_VT_4 = 0x1CEDCC, INVEST_VT_C = 0x17F10, INVEST_VT_4C = 0x1CF92C, INVEST_VT_B4 = 0x1D0B68,
                          INVEST_VT_160 = 0x1D2C00, INVEST_VT_1FC = 0x1CCA50, INVEST_VT_290 = 0x1CDC6C;

 
inline constexpr uint32_t CAPTURE_402 = 0x471DBC;            
inline constexpr uint32_t CAPTURE_403 = 0x471C84;            
 
inline constexpr uint32_t CAPTURE_404 = 0x471EEC;
inline constexpr uint32_t CAPTURE_405 = 0x472028;            
inline constexpr uint32_t CAPTURE_904 = 0x4730B8;            
}   

 
namespace off {
inline constexpr uint32_t META = 0x49518;                    
inline constexpr uint32_t STORAGE = 0x40A0;                  
inline constexpr uint32_t STORAGE_LIMIT = 300000 - STORAGE;  
inline constexpr uint32_t DESCRIPTORS = 0x60;                
inline constexpr uint32_t DESCRIPTOR_COUNT = 257, DESCRIPTOR_SIZE = 0x40;
inline constexpr uint32_t DESC_SOID = 0x20, DESC_STATE = 0x28, DESC_VERSION = 0x3C;
inline constexpr uint32_t ACCOUNT_SIZE = 28752, ACCOUNT_SCHEMA = 0x80801803;
inline constexpr uint32_t CHARACTER_SIZE = 28088, CHARACTER_SCHEMA = 0x80801743;
inline constexpr uint32_t DETAIL_SIZE = 168, DETAIL_SCHEMA = 0x80801732, DETAIL_MAX = 1024;
inline constexpr uint32_t ACCOUNT_HOLDER = 0x4C8, ACCOUNT_ROWS = 326;      
inline constexpr uint32_t CHARACTER_HOLDER = 0x38, CHARACTER_ROWS = 320;   
inline constexpr uint32_t ROW_SIZE = 32;
inline constexpr uint32_t EQUIPPED = 0x2900, EQUIPPED_COUNT = 20;           
inline constexpr uint32_t POSTMASTER_COUNT = 0x3BD0, POSTMASTER = 0x3BD8;   
inline constexpr uint32_t POSTMASTER_ENTRIES = 64, POSTMASTER_SIZE = 0x38;
inline constexpr uint32_t EXOTIC_FLAG = 0x549A;              
 
inline constexpr uint32_t D_SOID = 0x00, D_DEF = 0x24, D_ABILITIES = 0x38, D_GRID = 0x48, D_NODES = 0x4A, D_ROLLS = 0x6E;
inline constexpr uint32_t NODE_COUNT = 36, ROLL_COUNT = 8;
 
inline constexpr uint32_t GLOBALS_BUCKETS = 0x84, GLOBALS_DEFS = 0x144, GLOBALS_INDEX = 0x29C;
inline constexpr uint32_t GLOBALS_TAG = 0x80A58001;
inline constexpr uint32_t DEF_MARKER = 0x80800184, DEF_CLASS = 0x808016BD, INDEX_CLASS = 0x8080172B, CONTEXT_CLASS = 0x80801877;
inline constexpr uint32_t TRANSFER_CONTEXT = 26;             
 
inline constexpr uint32_t ORBIT_RECORDS = 20, ORBIT_RECORD = 0xD08, ORBIT_PROFILE = 0x28;    
inline constexpr uint32_t PROFILE_SIZE = 0xCD0;
inline constexpr uint32_t GAMEPLAY_PROFILE = 0x840, GAMEPLAY_STRIDE = 0x2930, GAMEPLAY_MAX = 16;
inline constexpr uint32_t P_PARTS = 0x178;                   
inline constexpr uint32_t P_ABILITY = 0x418;                 
inline constexpr uint32_t P_EQUIP = 0x5F0;                   
inline constexpr uint32_t P_CHAR_PERKS = 0xA30;              
inline constexpr uint32_t P_WEAPON_PERKS = 0xA70;            
inline constexpr uint32_t P_WEAPON_STATS = 0xB50;            
}   

 
 
extern const Fingerprints kReadFingerprints;
 
extern const Fingerprints kCatalogFingerprints;
extern const Fingerprints kGateFingerprints;              
 
extern const Fingerprints kTransferFingerprints;          
extern const Fingerprints kItemActionFingerprints;        
extern const Fingerprints kEquipFingerprints;             
extern const Fingerprints kUnequipFingerprints;           
 
extern const Fingerprints kEditorFingerprints;
 
extern const Fingerprints kCaptureFingerprints;

}   
