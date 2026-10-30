#pragma once
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "game/hopons_mem.h"

namespace brps3 {
class Runtime;
}

namespace brps3::hopons {

struct StructuredSite {
    uint32_t member = 0, variant = 0, combatant = 0;
    uint32_t offset = 0;                 
    uint32_t weight = 0;                 
};
 
 
bool decodeSquadStructure(const std::vector<uint8_t>& body, std::vector<StructuredSite>& out, uint32_t& members, std::string& why);
 
std::vector<uint32_t> markerSites(const std::vector<uint8_t>& body);

struct LiveSite {
    uint32_t offset = 0, address = 0, entity = 0;    
    int member = -1, variant = -1, combatant = -1;   
    uint32_t weight = 0;
    uint32_t catalogEntity = 0;                       
};
struct SquadDefinition {
    uint32_t tag = 0, body = 0, size = 0;
    uint32_t members = 0;
    bool structured = false;
    std::string structureNote;
    std::vector<LiveSite> sites;
    std::vector<uint8_t> bytes;
    std::vector<uint32_t> entities() const;           
};
std::optional<SquadDefinition> readSquadDefinition(Reader& mem, uint32_t tag, std::string& why);

struct SwapJournal {
    uint32_t tag = 0, body = 0, entity = 0;           
    std::vector<std::pair<uint32_t, uint32_t>> words;    
    std::string label;
    int64_t at = 0;
};
 
 
std::optional<SwapJournal> planSwap(Reader& mem, const SquadDefinition& def, const std::vector<uint32_t>& offsets, uint32_t entity, std::string& why);
 
void applySwap(Runtime& rt, Reader& mem, const SwapJournal& j);
struct RestoreResult {
    size_t restored = 0, already = 0, skipped = 0;
    std::vector<std::string> lines;
};
 
RestoreResult restoreSwap(Runtime& rt, Reader& mem, const SwapJournal& j);
std::string serialiseSwaps(const std::vector<SwapJournal>& swaps);
std::vector<SwapJournal> parseSwaps(const std::string& text);    

}   
