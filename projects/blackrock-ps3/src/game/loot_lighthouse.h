#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "game/loot_addr.h"
#include "game/loot_console.h"

namespace brps3::loot::lh {

 
struct Patch {
    const char* id;                      
    uint32_t addr;
    std::vector<uint8_t> original, replacement;
    const char* what;
    bool flagLeaf = false;               
};
const std::vector<Patch>& patches();
std::string patchName(const Patch& p);   
enum class SiteState { Original, Patched, Foreign };
SiteState siteState(const std::vector<uint8_t>& current, const Patch& p);

 
struct Slot {
    uint32_t tag = 0, value = 0;
};
struct Snapshot {
    std::string error;                   
    uint32_t world = 0, worldState = 0xFFFFFFFF;
    std::string stateName;
    uint32_t slices = 0, map = 0, current = 0xFFFFFFFF, pending = 0xFFFFFFFF, loaded = 0;
    std::array<Slot, 16> slots{};
    uint32_t mgr = 0;
    uint16_t pause = 0xFFFF;             
    uint8_t enabled = 0, s89 = 0, s8a = 0;    
    uint32_t target130 = 0;              
    uint8_t worker = 0;                  
    uint32_t reqTarget = 0, reqHash = 0;
    uint8_t reqFlag = 0;
    uint32_t globals = 0, globalsTarget = 0;
    uint32_t root = 0, ctx4 = 0, inner = 0, cache = 0;
    bool coherent = false;               
    uint8_t permission = 0;
     
     
    bool trialsKnown = false;
    uint8_t trials[2] = {0, 0};
    uint32_t flyin = 0;
    uint8_t flyinByte = 0;
    uint32_t small = 0;
    std::array<uint8_t, 8> identity{};
    uint32_t smallHandle = 0, smallEntity = 0, smallEntity2 = 0;
    uint32_t fade = 0, fadeCount = 0;
    bool predicateBlocks = false;
    uint16_t registryIndex = 0;
    std::vector<SiteState> sites;        
    bool gateOriginal = false;
    bool loadedSlice(uint32_t s) const { return (loaded >> s) & 1; }
    bool bound() const { return smallEntity != 0xFFFFFFFF && smallEntity != 0; }
};
Snapshot readSnapshot(world::Reader& mem);
std::string worldStateLabel(uint32_t state, const std::string& name);    

 
struct Pins {
    uint32_t map = 0;                    
    Slot slot8;                          
    bool hasMap() const { return map != 0; }
    bool hasSlot() const { return slot8.tag != 0; }
};

 
struct Check {
    std::string what;
    bool ok = false;
};
struct Readiness {
    std::vector<Check> checks;
    bool ready() const;
    std::string firstProblem() const;    
};
Readiness armReadiness(const Snapshot& s);
Readiness requestReadiness(const Snapshot& s, const Pins& pins);
Readiness instantiateReadiness(const Snapshot& s, const Pins& pins);
Readiness bindReadiness(const Snapshot& s, const Pins& pins);
Readiness lifetimeReadiness(const Snapshot& s, const Pins& pins);
bool isLighthouse(const Snapshot& s, const Pins& pins);    

 
struct Entity {
    uint32_t slot = 0, handle = 0, offset = 0, address = 0;
    uint32_t boundPlayer = 0xFFFFFFFF;   
    uint8_t phase = 0;                   
    uint32_t identityAddr = 0, innerAddr = 0;
    std::array<uint8_t, 8> identity{};
    bool identityKnown = false;
    bool matches = false;                
    bool empty() const { return identityKnown && identity == std::array<uint8_t, 8>{}; }
};
struct Inventory {
    uint32_t registry = 0, small = 0;
    uint32_t count = 0;                  
    std::array<uint8_t, 8> identity{};
    std::vector<Entity> entities;
};
 
Inventory readInventory(world::Reader& mem, Resolver& res, uint32_t type);

 
struct Write {
    uint32_t addr = 0;
    std::vector<uint8_t> data;
};
 
std::vector<Write> fadeWrites(const world::Bytes& block);

}   
