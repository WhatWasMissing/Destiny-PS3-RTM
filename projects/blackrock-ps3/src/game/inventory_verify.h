#pragma once
 
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "game/inventory_data.h"
#include "game/world_mem.h"

namespace brps3::inventory {

struct VerifyLine {
    enum Level { Ok, Warn, Fail, Info } level = Info;
    std::string what, detail;
};

struct VerifyReport {
    std::vector<VerifyLine> lines;
    bool ok = false;                     
    std::string verdict;
    uint32_t mode = 0;
    std::string contextSource;           
};

VerifyReport verifyCatalog(world::Reader& mem, const Catalog& cat, const std::function<void(const std::string&)>& progress = {});

 
std::vector<std::pair<int, int>> parseContextPairs(const std::vector<uint8_t>& table);

}   
