#pragma once
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "game/hopons_mem.h"

namespace brps3::hopons {

struct CheckLine {
    enum Status { Ok, Warn, Fail, Info } status = Info;
    std::string label, detail;
};
const char* checkStatusText(CheckLine::Status s);    

 
bool placementAlignment(uint32_t a);                   
bool placementAllocate(uint32_t base, uint32_t size, uint32_t alignment, bool prefix, uint32_t& used, uint32_t& offset);
struct AiContextLayout {
    uint32_t componentOffset = 0, payloadOffset = 0, used = 0;
    bool copyComponentDefaults = true;
};
bool aiContextLayout(uint32_t arenaBase, uint32_t alreadyUsed, uint32_t componentSize, uint32_t componentAlignment, uint32_t payloadSize,
                     uint32_t payloadAlignment, bool reuseComponent, uint32_t existingComponentOffset, AiContextLayout& out);
 
 
bool payloadBudget(uint32_t headerSize, uint32_t headerAlignment, uint32_t payloadSize, uint32_t payloadAlignment, uint32_t& maximumUsed);

 
struct TypeDefinition {
    uint32_t handle = 0, body = 0;
    uint32_t key = 0;                    
    uint32_t size = 0;                   
    uint8_t alignment = 0;               
};
std::optional<TypeDefinition> readTypeDefinition(Reader& mem, TagReader& tags, uint32_t handle, std::string& why);

struct TagHeapInfo {
    uint32_t heap = 0, base = 0, size = 0, head = 0, tail = 0, headerSize = 16;
    uint16_t handle = 0;
};
std::optional<TagHeapInfo> readTagHeap(Reader& mem, std::string& why);
struct FixedPoolInfo {
    uint32_t row = 0, pool = 0, count = 0;
    uint16_t handle = 0;
    float fraction = 0;
    uint32_t allocation = 0, size = 0, base = 0, stride = 0, capacity = 0, bitmap = 0;
    bool plausible = false;
};
std::vector<FixedPoolInfo> readFixedPools(Reader& mem);

 
 
struct Extent {
    bool ok = false;
    uint32_t extent = 0;
    std::string source, why;
};
Extent definitionExtent(Reader& mem, uint32_t body, uint32_t tag);

struct AiPreflight {
    std::vector<CheckLine> lines;
    size_t ok = 0, warnings = 0, failures = 0;
};
AiPreflight runAiPreflight(Reader& mem);

}   
