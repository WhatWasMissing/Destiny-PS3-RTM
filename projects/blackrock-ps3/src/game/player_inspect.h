#pragma once
 
 
 
 
 
 
 
 
#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "game/world_factions.h"
#include "game/world_objects.h"
#include "game/world_player.h"
#include "game/world_tags.h"

namespace brps3::player {

 
 
 
std::map<uint32_t, world::Bytes> readRanges(world::Reader& mem, const std::vector<std::pair<uint32_t, uint32_t>>& ranges,
                                            uint32_t gap = 0x1000, uint32_t maxSpan = 0x10000);

 
struct LoadoutRef {
    uint32_t owner = 0;                  
    bool weapon = false;
    uint32_t cls = 0, component = 0;     
    uint32_t offset = 0, tag = 0, slot = 0, arrayOffset = 0, associated = 0, flags = 0;
    std::string kind;                    
    std::string slotName;                
};
 
std::vector<LoadoutRef> decodeLoadout(uint32_t cls, uint32_t owner, bool weapon, uint32_t component, const world::Bytes& bytes);
struct ClassSeen {
    uint32_t owner = 0, cls = 0, size = 0;
    bool weapon = false;
};
struct Loadout {
    uint32_t player = 0;
    std::vector<LoadoutRef> refs;
    std::vector<ClassSeen> classes;      
    size_t unreadable = 0;
};
Loadout readLoadout(world::Reader& mem);    

 
struct Attached {
    uint32_t tag = 0;                    
    uint32_t arenaHandle = 0, address = 0, size = 0, flags = 0;    
};
struct Attachments {
    uint32_t owner = 0;
    uint8_t type = 0;
    std::vector<Attached> defs;
};
 
 
Attachments readAttachments(world::Reader& mem, uint32_t owner);

 
struct StatRef {
    uint32_t handle = 0, id = 0, address = 0;
    uint16_t index = 0;
    uint8_t category = 0;
};
struct StatRegistry {
    uint32_t base = 0, stride = 0, capacity = 0;
    std::array<uint16_t, 15> counts{};
    std::vector<StatRef> refs;
};
std::string statLabel(uint32_t id);      
 
StatRegistry parseStatRegistry(const world::Bytes& hdr, const world::Bytes& data);
StatRegistry readStatRegistry(world::Reader& mem);    

struct StatusRecord {
    uint32_t id = 0, raw = 0;
    float value = 0;
    int32_t priority = 0, contributions = 0;
    uint8_t flags = 0;
};
struct StatusAggregate {
    uint32_t handle = 0, accessor = 0, data = 0, size = 0, cls = 0, owner = 0;
    std::vector<StatusRecord> records;
};
StatusAggregate parseStatus(const world::Bytes& data, uint32_t handle);    
StatusAggregate readStatus(world::Reader& mem);                            

 
enum class Hostility { Friendly, Hostile, Unknown };
const char* hostilityName(Hostility h);
 
Hostility hostilityOf(int mine, int theirs, const std::array<uint8_t, 64>* allegiance);

struct EspEntity {
    uint32_t handle = 0, entry = 0;
    uint8_t type = 0;
    world::Vec3 position;                
    float distance = 0;
    int8_t faction = -1;
    Hostility hostility = Hostility::Unknown;
    uint32_t tag = 0;                    
};
struct EspOptions {
    float radius = 150.0f;               
    bool bipeds = true, vehicles = false, others = false;
    bool hideFriendly = false, hideHostile = false, hideUnknown = false;
    size_t maxEntities = 64;
};
struct EspSnapshot {
    uint32_t player = 0;
    world::Vec3 origin;
    int8_t playerFaction = -1;
    bool allegianceKnown = false;
    std::vector<EspEntity> entities;     
    size_t scanned = 0, inRange = 0;
};
 
std::vector<EspEntity> nearby(const world::ObjectScan& scan, uint32_t playerHandle, const world::Vec3& origin, const EspOptions& o,
                              const world::FactionTable* factions, int mine, const std::array<uint8_t, 64>* allegiance, size_t* inRange = nullptr);

 
class EspScanner {
public:
    EspSnapshot scan(world::Reader& mem, const EspOptions& o);
    void reset();
private:
    world::PlayerCache cache_;
    std::map<uint32_t, uint32_t> tags_;
    std::optional<world::TagTable> tagTable_;
    std::optional<std::array<uint8_t, 64>> allegiance_;
    std::chrono::steady_clock::time_point allegianceAt_{};
};

}   
