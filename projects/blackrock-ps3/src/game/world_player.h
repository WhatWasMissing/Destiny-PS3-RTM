#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

#include "game/world_objects.h"

namespace brps3::world {

struct LocalPlayer {
    uint32_t descriptor = 0;             
    uint32_t elements = 0, elementSize = 0, elementCount = 0;
    uint32_t elementIndex = 0;
    uint32_t element = 0;                
    uint32_t handle = 0;                 
    uint32_t entry = 0;                  
    uint32_t playerEntry = 0;            
    uint32_t playerDatum = 0;            
    int8_t faction = -1;                 
    SObject object;                      
    Vec3 position;                       
    Vec3 entryPosition;                  
    Vec3 forward;                        
    static constexpr bool positionNeedsCheck = true;    
     
    bool sameAs(const LocalPlayer& o) const {
        return descriptor == o.descriptor && element == o.element && handle == o.handle && entry == o.entry;
    }
};

enum class PlayerStatus {
    Ok,
    NoDescriptor,                        
    NotSpawned,                          
    BadPool,                             
    IdentityMismatch,                    
};
struct PlayerLookup {
    PlayerStatus status = PlayerStatus::NoDescriptor;
    std::string message;                 
    std::optional<LocalPlayer> player;
};
const char* playerStatusName(PlayerStatus s);

 
PlayerLookup findLocalPlayer(Reader& mem);
 
LocalPlayer requireLocalPlayer(Reader& mem);
 
bool stillValid(Reader& mem, const LocalPlayer& p);
 
Vec3 readPosition(Reader& mem, const LocalPlayer& p);          
Vec3 readEnginePosition(Reader& mem, const LocalPlayer& p);    

 
 
class PlayerCache {
public:
    std::optional<LocalPlayer> get(Reader& mem, int maxAgeMs = 1000, bool validate = false);
    LocalPlayer require(Reader& mem, int maxAgeMs = 1000, bool validate = false);    
    void invalidate();
    PlayerLookup last() const;
private:
    mutable std::mutex m_;
    PlayerLookup last_;
    std::chrono::steady_clock::time_point at_{};
    bool have_ = false;
};

}   
