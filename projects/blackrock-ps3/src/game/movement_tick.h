#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

#include "game/movement_addr.h"
#include "game/movement_body.h"
#include "game/movement_stub.h"

namespace brps3 {
class Runtime;
}

namespace brps3::movement {

struct Want {
    bool fly = false, gather = false, edge = false;
    bool pad = false;                    
    bool probe = false;                  
    bool any() const { return fly || gather || edge || pad || probe; }
    bool needsPlayer() const { return fly || gather || edge || probe; }
    bool operator==(const Want&) const = default;
};

struct Config {
    Want want;
    float speed = d1::FLY_SPEED;         
    float precisionFactor = d1::PRECISION;
    bool precision = true;               
    bool brake = false;                  
    float dist = d1::OPK_DIST;           
    float fps = 30.0f;                   
    bool operator==(const Config&) const = default;
};

 
 
inline constexpr float kMaxComOffset = 5.0f;

 
Control controlFor(const Config& cfg, const Want& effective, const std::optional<PlayerLink>& link);

class TickSession {
public:
    void apply(Runtime& rt, world::Reader& mem, const Config& cfg);
    template <class F>
    void edit(Runtime& rt, world::Reader& mem, F&& f) {
        Config c = config();
        f(c);
        apply(rt, mem, c);
    }
    Config config() const;

     
    std::optional<Control> read(Runtime& rt);
     
     
    bool maintain(Runtime& rt, world::Reader& mem, const Control& c, int minIntervalMs = 1500);
     
    void relink(Runtime& rt, world::Reader& mem);
     
     
     
     
     
     
    world::Vec3 warp(Runtime& rt, world::Reader& mem, const world::Vec3& target, uint32_t frames = 3);
     
    void shutdown(Runtime& rt);

    bool installed() const;
    uint32_t block() const;
    std::string note() const;            
    std::optional<PlayerLink> link() const;
    std::optional<BodyChain> chain() const;

private:
    void syncLocked(Runtime& rt, world::Reader& mem);
    void removeLocked(Runtime& rt);
    void resolveLocked(world::Reader& mem);
    bool quickValidLocked(world::Reader& mem) const;
    void writeConfigLocked(Runtime& rt);
    Want effectiveLocked() const;

    mutable std::mutex m_;
    Config cfg_;
    uint32_t ctrl_ = 0;
    std::optional<PlayerLink> link_;
    std::optional<PlayerLink> written_;   
    std::optional<BodyChain> chain_;
    std::string note_;
    bool warpHold_ = false;
    std::chrono::steady_clock::time_point lastResolve_{};
};

}   
