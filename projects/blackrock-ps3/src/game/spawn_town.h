#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "game/world_addr.h"
#include "game/world_mem.h"

namespace brps3::spawn::town {

inline constexpr uint32_t STATE_PTR = 0x1AB6894;
inline constexpr uint32_t GET = 0x2CB988, SET_LATCH = 0x2CB7C4, RECOMPUTE = 0x2CBA58;
extern const world::Fingerprints kFingerprints;

struct State {
    uint32_t block = 0;                  
    std::array<uint8_t, 12> bytes{};
    uint32_t player = 0;                 
    bool current() const { return bytes[0] != 0; }       
    bool desired() const { return bytes[1] != 0; }
    uint8_t dirty() const { return bytes[2]; }
    uint8_t latch() const { return bytes[3]; }
    uint32_t refs() const { return uint32_t(bytes[4]) << 24 | uint32_t(bytes[5]) << 16 | uint32_t(bytes[6]) << 8 | bytes[7]; }
    std::array<uint8_t, 8> controls() const;            
};
 
std::optional<State> read(world::Reader& mem, std::string* why = nullptr);
std::string describe(const State& s);    

 
 
std::array<uint8_t, 8> requested(const std::array<uint8_t, 8>& original, bool town);

struct Edit {
    uint32_t block = 0, player = 0;      
    std::array<uint8_t, 8> original{}, applied{};
    bool town = false;                   
};

using Writer = std::function<void(uint32_t addr, const std::vector<uint8_t>& bytes)>;
 
using PlayerFn = std::function<uint32_t()>;

 
 
Edit request(world::Reader& mem, const Writer& write, const PlayerFn& player, const State& expected, bool town);
 
 
void restore(world::Reader& mem, const Writer& write, const PlayerFn& player, const Edit& e);

}   
