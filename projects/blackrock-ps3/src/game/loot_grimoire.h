#pragma once
 
 
 
 
 
 
 
 
 
 
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "game/loot_addr.h"
#include "game/loot_console.h"

namespace brps3::loot::grim {

struct Flag {
    uint16_t ordinal = 0, dense = 0;
    std::string name;
    std::string group() const;           
};

struct Catalog {
    std::vector<Flag> flags;
    uint32_t coreFirst = 0, coreSize = 0, flagsSize = 0;
    std::string coreSha256, flagsSha256;
    std::string error;                   
    static Catalog parse(const std::string& tsv);    
    const Flag* byOrdinal(uint16_t ordinal) const;
     
    std::vector<size_t> search(const std::string& query) const;
};
const Catalog& catalog();                

 
struct Tables {
    uint32_t core = 0, flags = 0;        
};
Tables verifyTables(world::Reader& mem, Resolver& res, const Catalog& cat);
void verifyContextClass(world::Reader& mem);       

struct Profile {
    uint32_t manager = 0, offset = 0, account = 0;
    std::array<uint8_t, 8> identity{};
    bool descriptorOk = false;           
    uint32_t descriptor = 0, version = 0;
    std::string note;                    
    std::vector<uint8_t> values;         
    uint32_t unlocked = 0;
    bool isUnlocked(size_t i) const { return i < values.size() && values[i] == GRIM_UNLOCKED; }
    std::string identityHex() const;
};
 
Profile readProfile(world::Reader& mem, const Catalog& cat);
 
 
void recheck(world::Reader& mem, const Profile& before);
 
std::vector<uint8_t> readValues(world::Reader& mem, const Profile& p, const Catalog& cat);

 
 
 
 
 
inline constexpr uint32_t kStubSize = 0x100, kBatch = 16, kOutDone = 3, kOutNoContext = 4, kOutAccount = 5;
std::vector<uint8_t> stubProgram(uint32_t base);

}   
