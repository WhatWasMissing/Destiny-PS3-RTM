#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <string>
#include <vector>

#include "game/world_registry.h"

namespace brps3::lab {

inline constexpr uint32_t kAudioStateSlot = 0x1ADBB4C, kAudioSpatialSlot = 0x1ADBB50, kAudioRegistrar = 0x5E9718;
inline constexpr uint32_t kAudioStateHash = 0xEBF701A3, kAudioStateCategory = 3, kAudioStateOwner = 26, kAudioStateSize = 16144;
inline constexpr uint32_t kProfileErrorLatch = 0x1AB638C, kProfileLatchClear = 0x2A84DC;
inline constexpr uint32_t kSessionPool = 0x1ABCA14;
inline constexpr uint32_t kInvestmentIface = 0x1897CEC, kProviderGet = 0x1CB820, kInvestmentVtable = 0x13E6478;
inline constexpr uint32_t kInvestmentVt1FC = 0x184ECD0, kInvestVt1FCCode = 0x1CCA50;

using ResearchFingerprints = std::vector<std::pair<uint32_t, const char*>>;
extern const ResearchFingerprints kAudioFingerprints;
extern const ResearchFingerprints kProbeFingerprints;

struct Check {
    std::string what, value, expected;
    bool ok = false;
};

struct AudioState {
    world::GlobalBlock state;
    uint32_t spatial = 0;
    std::vector<Check> checks;
    bool ok() const;
};
AudioState readAudioState(world::Reader& mem);

struct ProbeAnchors {
    uint32_t latch = 0;                  
    uint8_t latchBytes[2] = {};
    bool latchRead = false;
    uint32_t sessionPool = 0;            
    uint32_t provider = 0, vtable = 0, slot1FC = 0, code1FC = 0;
    std::vector<Check> checks;
    bool ok() const;
};
ProbeAnchors readProbeAnchors(world::Reader& mem);

}   
