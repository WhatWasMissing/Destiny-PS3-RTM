#pragma once
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <optional>

#include "game/world_mem.h"

namespace brps3::movement {

struct ScreenPoint {
    float x = 0, y = 0;
    float depth = 0;                     
    bool onScreen = false;               
};

struct View {
    float V[16] = {};                    
    float P[16] = {};                    
    uint16_t width = 1024, height = 624;    
    bool valid() const;                  
     
    std::optional<ScreenPoint> project(const world::Vec3& p) const;
     
    world::Vec3 cameraPosition() const;
     
    world::Vec3 cameraForward() const;
};

View parseView(const world::Bytes& b);    
View readView(world::Reader& mem);        

}   
