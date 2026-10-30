#pragma once
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "game/world_objects.h"

namespace brps3::world {

struct PoolInfo {
    std::string name, category, confidence;    
    uint32_t ps3 = 0;                          
    uint32_t elemSize = 0, capacity = 0, align = 0;    
};
struct GlobalInfo {
    std::string name, confidence;
    uint32_t slot = 0;                         
    uint32_t size = 0, align = 0;
};
 
 
const std::vector<PoolInfo>& poolCatalog();
const std::vector<GlobalInfo>& globalCatalog();
const PoolInfo* findPool(const std::string& name);        
const GlobalInfo* findGlobal(const std::string& name);
std::vector<PoolInfo> parsePoolCsv(const std::string& text);       
std::vector<GlobalInfo> parseGlobalCsv(const std::string& text);
std::vector<std::string> splitCsvLine(const std::string& line);   

 
struct PoolStatus {
    PoolInfo info;
    PoolHeader header;
    bool plausible = false;                    
    bool strideOk = false;                     
    bool capacityOk = false;                   
};
PoolStatus readPoolStatus(Reader& mem, const PoolInfo& info);    

struct BlockHeader {
    uint32_t index = 0, hash = 0, flags = 0;
    uint8_t region = 0, category = 0, owner = 0;
};
BlockHeader parseBlockHeader(const Bytes& b16);            
struct GlobalBlock {
    uint32_t slot = 0, block = 0;
    bool initialised = false;                  
    BlockHeader header;                        
};
GlobalBlock readGlobal(Reader& mem, uint32_t slot);        

struct RegistryRow {
    uint32_t index = 0;
    uint16_t handle = 0;
    uint32_t offset = 0, size = 0, owner = 0;
    uint32_t slot = 0;                         
};
 
std::vector<RegistryRow> readGlobalsRegistry(Reader& mem);
std::vector<RegistryRow> parseGlobalsRegistry(const Bytes& reg);    
inline constexpr uint32_t kRegistryReadSize = 0x1834;

}   
