#pragma once
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "game/world_mem.h"

namespace brps3::activity {

struct HeapRange {
    uint32_t lo, hi;
};
 
inline const std::vector<HeapRange> kDefaultScanRanges = {{0x30000000, 0x31000000}, {0x60000000, 0x60800000}};
inline constexpr uint32_t kDeployRecordTag = 0x01000000;    
inline constexpr uint32_t kFnvBasis = 0x811C9DC5;
inline constexpr uint32_t kScanChunk = 0x10000;
inline constexpr uint64_t kMaxScanBytes = 0x10000000;    

 
 
std::vector<HeapRange> parseRanges(const std::string& text);
std::string rangesText(const std::vector<HeapRange>& ranges);

struct MirrorHit {
    uint32_t addr = 0;
    bool fnvBefore = false;              
    bool tailZero = false;               
    bool inSession = false;              
    std::string context;                
};

struct MirrorScan {
    uint32_t id = 0;                     
    uint32_t state = 0xFFFFFFFFu;        
    std::string activity;                
    uint32_t root = 0;                   
    bool cancelled = false;
    std::vector<MirrorHit> hits;
};

 
 
MirrorScan scanMirrors(world::Reader& mem, uint32_t id, const std::vector<HeapRange>& ranges,
                       const std::function<void(uint64_t done, uint64_t total)>& progress = {},
                       const std::function<bool()>& cancelled = {});

 
std::vector<uint32_t> intersect(const MirrorScan& a, const MirrorScan& b);

 
std::string toJson(const MirrorScan& s);
MirrorScan fromJson(const std::string& text);         

}   
