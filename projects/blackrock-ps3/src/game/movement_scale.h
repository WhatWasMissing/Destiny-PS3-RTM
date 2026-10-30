#pragma once
 
 
 
 
 
 
 
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "game/movement_teleport.h"    
#include "game/world_mem.h"

namespace brps3::movement {

inline constexpr float kScaleMin = 0.05f, kScaleMax = 20.0f;
bool validScale(float s);

struct ScaleEdit {
    uint32_t handle = 0, entry = 0;
    uint8_t type = 0;
    float original = 1.0f;
    float applied = 1.0f;
    bool player = false;
};

class ScaleJournal {
public:
     
     
    float set(world::Reader& mem, const WriteFn& write, uint32_t handle, float scale, bool player = false);
     
     
     
    bool restore(world::Reader& mem, const WriteFn& write, uint32_t handle, std::string* why = nullptr);
     
    size_t restoreAll(world::Reader& mem, const WriteFn& write, std::vector<std::string>* notes = nullptr);
    std::vector<ScaleEdit> edits() const;
    std::optional<ScaleEdit> find(uint32_t handle) const;
    std::optional<ScaleEdit> playerEdit() const;
    void forget();                       
private:
    mutable std::mutex m_;
    std::vector<ScaleEdit> edits_;
};

 
std::optional<float> readScale(world::Reader& mem, uint32_t handle);

}   
