#pragma once
 
 
 
 
 
 
 
 
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "game/world_mem.h"
#include "game/world_player.h"

namespace brps3::movement {

using WriteFn = std::function<void(uint32_t addr, const std::vector<uint8_t>& bytes)>;

 
bool validTarget(const world::Vec3& v, std::string* why = nullptr);

struct TeleportResult {
    world::Vec3 before;                  
    world::Vec3 target;
    bool verified = false;               
    std::string message;
};

 
 
 
TeleportResult teleportTo(world::Reader& mem, const WriteFn& write, const world::Vec3& target);
 
TeleportResult teleportBy(world::Reader& mem, const WriteFn& write, const world::Vec3& offset);
TeleportResult teleportEdge(world::Reader& mem, const WriteFn& write, float up = 500.0f);
 
TeleportResult teleportForward(world::Reader& mem, const WriteFn& write, float distance);
 
std::optional<world::Vec3> horizontal(const world::Vec3& forward);

 
struct Slot {
    std::string name;
    world::Vec3 pos;
    std::string note;                    
    bool favourite = false;
    int64_t used = 0;                    
    int64_t created = 0;
    bool operator==(const Slot&) const = default;
};
 
std::string encodeSlots(const std::vector<Slot>& slots);
std::vector<Slot> decodeSlots(const std::string& text);     
std::string cleanName(const std::string& name);             
std::string uniqueName(const std::vector<Slot>& slots, const std::string& base);    
 
std::vector<size_t> slotOrder(const std::vector<Slot>& slots);
std::string formatVec(const world::Vec3& v, int decimals = 2);    
std::optional<world::Vec3> parseVec(const std::string& text);     

 
struct History {
    std::vector<world::Vec3> points;
    static constexpr size_t kMax = 12;
    void push(const world::Vec3& p);
    std::optional<world::Vec3> pop();
    std::string encode() const;
    static History decode(const std::string& text);
};

}   
