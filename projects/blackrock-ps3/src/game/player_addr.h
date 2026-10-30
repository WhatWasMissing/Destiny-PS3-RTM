#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <cfloat>
#include <cstdint>
#include <utility>
#include <vector>

namespace brps3::player {

using Fingerprints = std::vector<std::pair<uint32_t, const char*>>;

namespace addr {
 
 
inline constexpr uint32_t KILL_AURA_SITE = 0x21F258;
inline constexpr uint32_t KILL_AURA_ORIGINAL = 0xF821FFC1;    
 
inline constexpr uint32_t KNOWN_STATS_POOL = 0x1AB9820;       
inline constexpr uint32_t STAT_CATEGORY_COUNTS = 0x1AB98A0;   
inline constexpr uint32_t STAT_TABLE_REGISTER = 0x2EA400;     
 
 
inline constexpr uint32_t COMPONENT_SLOTS_INIT = 0x72A584;    
}   

 
inline const Fingerprints kKillAuraFingerprints = {
    {addr::KILL_AURA_SITE, "F821FFC138A00030C023001038600000D02100307C450C0E1040128C7C4021CE382100404E800020"},
};
 
inline const Fingerprints kStatsFingerprints = {
    {addr::STAT_TABLE_REGISTER, "F821FF717C0802A6F80100A03CA0018A"},
    {addr::COMPONENT_SLOTS_INIT, "7C0802A64866E499F821FF41F80100D0"},
};

 
 
 
inline constexpr uint32_t kRecordSignature = 0x9951DAEF;
inline constexpr uint32_t kRecordStride = 0x28;

 
namespace pc {
inline constexpr uint32_t GOD = 0x25A4;
inline constexpr uint32_t INTERACT = 0x2D14;
inline constexpr uint32_t PICKUP0 = 0x2D3C;       
inline constexpr uint32_t PICKUP_COUNT = 7;
inline constexpr uint32_t SUPER = 0x31A4;
inline constexpr uint32_t GRENADE = 0x31CC;
inline constexpr uint32_t MELEE = 0x31F4;
inline constexpr uint32_t READ_SIZE = 0x3300;     
}   

 
namespace wfc {
inline constexpr uint32_t GATE = 0x64;            
inline constexpr uint32_t HANDLING[3] = {0x94, 0xBC, 0xE4};
inline constexpr uint32_t ZOOM_MULT = 0x10C;
inline constexpr uint32_t ZOOM_SPEED = 0x134;
inline constexpr uint32_t MAGAZINE[2] = {0x24C, 0x29C};
inline constexpr uint32_t RPM[4] = {0xA34, 0xA5C, 0xA84, 0xAAC};
inline constexpr uint32_t DAMAGE = 0xF34;
inline constexpr uint32_t SPREAD = 0xF84;
inline constexpr uint32_t RECOIL0 = 0x10C4;       
inline constexpr uint32_t RECOIL_COUNT = 8;
inline constexpr uint32_t AMMO = 0x1998;          
inline constexpr uint32_t FULL_AUTO = 0x980;      
inline constexpr uint32_t SEMI_AUTO = 0x01010000;
 
 
inline constexpr uint32_t AMMO_TAG_OFFSET = 0x1920;
inline constexpr uint32_t AMMO_TAG = 0x80802449;
inline constexpr uint32_t AMMO_RESERVE = 0x1924;
inline constexpr uint32_t AMMO_CLIP = 0x1928;
inline constexpr uint32_t ROF_RECORD = 0x9C0;     
inline constexpr uint32_t ROF_TAG = 0x8080221A;
inline constexpr uint32_t READ_SIZE = 0x1A40;
}   

 
inline constexpr float kGodValue = 2147483648.0f;    
inline constexpr float kRangeValue = 100000.0f;      
inline constexpr float kAbilityValue = 10000.0f;     
inline constexpr float kRpmValue = 1000.0f;          
inline constexpr float kMagazineValue = 1000000.0f;  
inline constexpr float kDamageValue = FLT_MAX;       
inline constexpr uint32_t kAmmoValue = 1000;
inline constexpr float kKillAuraValue = 1000.0f;     

 
inline constexpr uint32_t kAbilityLoadoutClass = 0x80AA28D4;    
inline constexpr uint32_t kWeaponArchetypeClass = 0x8080271E;   
inline constexpr uint32_t kTalentGridClass = 0x8080206C;        
inline constexpr uint32_t kTalentHeader = 0x80800184;
inline constexpr uint32_t kTalentRecordTag = 0x80805CA7;
inline constexpr uint32_t kAbilityTags = 0x240, kAbilityWords = 0x228;

 
inline constexpr uint32_t kStatsStride = 0x14;
inline constexpr uint32_t kStatCategories = 15;
inline constexpr uint32_t kStatusSlot = 0x25BC;    
inline constexpr uint32_t kStatusMaxRecords = 4;

}   
