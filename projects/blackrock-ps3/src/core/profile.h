#pragma once
 
 
 
 
 
#include <cstdint>
#include <utility>
#include <vector>

namespace brps3::profile {

inline constexpr const char* BUILD = "51539.16.07.26.1808.v230";
inline constexpr const char* PROCESS = "EBOOT.BIN";
inline constexpr uint32_t TOC = 0x1891798;

 
inline constexpr uint32_t TICK_SITE = 0x354074;         
inline constexpr uint32_t TICK_TARGET = 0x3510F0;       
inline constexpr uint32_t EMIT = 0x26B398;              
inline constexpr uint32_t EMIT_BASIC = 0x26B448;        
inline constexpr uint32_t EMIT_INVESTMENT = 0x26B504;   
inline constexpr uint32_t PAYLOAD_INIT = 0x26B5E4;      
inline constexpr uint32_t EVALUATE = 0x1CAF20;          
inline constexpr uint32_t DISPATCH = 0x1CAE64;          

 
inline constexpr uint32_t GATHER_EMIT = 0x5B0A48;       
inline constexpr uint32_t POSITION_OF = 0x284EEC;       
inline constexpr uint32_t PLAYER_POOL = 0x1A1D720;      

 
inline constexpr uint32_t WORLD_PTR = 0x1ABC89C;        
inline constexpr uint32_t LOOT_EVENT_HASHES = 0x1A2074C;  
inline constexpr uint32_t TAG_TABLE_PTR = 0x1896D50;    

 
inline constexpr uint32_t CAVE = 0x183E000;             
inline constexpr uint32_t CAVE_SIZE = 0x2000;
 
 
 
 
 
inline constexpr uint32_t CAVE_LOW = 0x183D200;
inline constexpr uint32_t CAVE_LOW_SIZE = CAVE - CAVE_LOW;    
inline constexpr uint32_t CONTROL = 0x1C74000;         
inline constexpr uint32_t CONTROL_SIZE = 0xC000;

 
inline constexpr uint32_t TEXT_END = 0x1840000;

 
 
using Fingerprints = std::vector<std::pair<uint32_t, const char*>>;

inline const Fingerprints FINGERPRINTS = {
    {TICK_SITE, "4BFFD07D"},                               
    {TICK_TARGET, "7C0802A648A4792DF821FF71F80100A0"},
    {EMIT, "7C0802A648B2D681F821FF61F80100B0"},
    {EMIT_BASIC, "7C0802A648B2D5D5F821FF51F80100C0"},
    {EMIT_INVESTMENT, "7C0802A648B2D519F821F261F8010DB0"},
    {PAYLOAD_INIT, "7C0802A648B2D431F821FF61F80100B0"},
    {EVALUATE, "7C0802A648BCDAEDF821FE31F80101E0"},
    {DISPATCH, "7C0802A648BCDBBDF821F261F8010DB0"},
};

inline const Fingerprints GATHER_FINGERPRINTS = {
    {GATHER_EMIT, "7C0802A6487E7FC5F821FEA1F8010170"},
    {POSITION_OF, "3C8001A21041038C546304FE3084D9E0"},
};

}   
