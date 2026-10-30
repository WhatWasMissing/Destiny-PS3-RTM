#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "game/world_registry.h"

namespace brps3::world {

inline constexpr int kFactionCount = 32;
inline constexpr uint32_t kAllegianceHash = 0x2963398E;
inline constexpr uint32_t kFactionTableEntries = 1500;

int allegianceBit(int a, int b);                     
unsigned allegianceByte(unsigned bit);
bool allegiancePair(const std::array<uint8_t, 64>& bits, int a, int b);
void setAllegiancePair(std::array<uint8_t, 64>& bits, int a, int b, bool allied);
unsigned allegianceCount(const std::array<uint8_t, 64>& bits);    

struct Allegiance {
    uint32_t body = 0;                   
    BlockHeader header;                  
    uint32_t registryIndex = 0;          
    uint16_t ledgerHandle = 0;
    uint32_t ledgerOffset = 0, ledgerSize = 0;
    uint32_t directoryBase = 0, directoryStride = 0, directoryCapacity = 0;
    uint32_t payload = 0, extent = 0;    
    std::array<uint8_t, 64> bits{};
    std::string proof;                   
    bool owned() const { return proof.empty(); }
    bool sameOwner(const Allegiance& o) const {
        return body == o.body && registryIndex == o.registryIndex && ledgerHandle == o.ledgerHandle && ledgerOffset == o.ledgerOffset &&
               payload == o.payload;
    }
};
Allegiance readAllegiance(Reader& mem);              
 
Allegiance readAllegianceStable(Reader& mem);        

struct FactionTable {
    uint32_t base = 0, bank = 0;
    std::vector<int8_t> entries;         
    int8_t of(uint32_t handle) const;    
};
FactionTable readFactionTable(Reader& mem);          
int8_t factionOf(Reader& mem, uint32_t handle);      

}   
