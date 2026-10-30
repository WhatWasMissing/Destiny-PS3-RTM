#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "app/registry.h"
#include "game/world_factions.h"

namespace brps3::activity {

struct AllegianceEdit {
    enum class Kind { Pair, Fill } kind = Kind::Pair;
    int a = 0, b = 0;                    
    bool before = false, after = false;  
    uint32_t body = 0;                   
    unsigned byteIndex = 0;              
    uint8_t originalByte = 0;            
    std::array<uint8_t, 64> original{};  
    std::string text() const;            
};

 
struct AllegianceJournal {
    std::vector<AllegianceEdit> edits;
    bool hooked = false;                 
};

 
world::Allegiance readAllegiance(Ctx& c);
 
 
bool setPair(Ctx& c, int a, int b, bool allied);
 
void fillAll(Ctx& c, bool allied);
 
std::array<uint8_t, 64> filledBits(const std::array<uint8_t, 64>& original, bool allied);
 
 
std::array<uint8_t, 64> unfilledBits(const std::array<uint8_t, 64>& live, const AllegianceEdit& fill);
 
void restoreEdit(Ctx& c, size_t index);
 
void restoreAll(Ctx& c);
 
std::vector<AllegianceEdit> journalEdits(Ctx& c);

}   
