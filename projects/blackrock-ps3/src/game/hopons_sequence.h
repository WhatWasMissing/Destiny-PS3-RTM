#pragma once
 
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "game/hopons_scripts.h"

namespace brps3 {
class Runtime;
}

namespace brps3::hopons {

 
 
 
bool sequenceDelay(uint32_t mode, uint32_t durationBits, uint32_t rateBits, uint32_t& delay);

struct ClockOwner {
    uint32_t managerHandle = 0, allocation = 0, buffer = 0, directory = 0;
    uint32_t worldHandle = 0, worldIdentity = 0, globalHandle = 0, globalIdentity = 0, globalBody = 0;
    uint32_t rateBits = 0, timeSource = 0, frequency = 0;
    float rate = 0;
    std::vector<std::string> lines;      
};
 
 
std::optional<ClockOwner> readClockOwner(Reader& mem, uint32_t slotHash, std::string& why);

struct BundleInfo {
    uint32_t handle = 0, identity = 0;   
    uint32_t allocHandle = 0, allocation = 0, payload = 0, extent = 0, size = 0;
    bool wrapperLinked = false;          
    std::vector<std::string> lines;
};
std::optional<BundleInfo> readBundle(Reader& mem, const ScriptRef& ref, std::string& why);

 
inline constexpr const char* kRestartBlocked =
    "Restart needs the effect definition, placement class / source / header / plan and reciprocal-effect validators, which are not ported yet "
    "(port map enc.sequence_dev). The Xbox tool keeps Restart unwired for the same reason; the preflight shows what is checked so far.";

struct RestartPreflight {
    bool ok = false;                     
    bool complete = false;               
    std::vector<std::string> lines;      
    SequenceState state;
    uint32_t delay = 0;
    std::optional<AccessorInfo> tracked;
};
RestartPreflight preflightRestart(Reader& mem, const ScriptInstance& s);

struct RestartOutcome {
    int step = 0;                        
    bool ok = false;
    std::string message;
    std::vector<std::string> lines;
};
 
 
RestartOutcome restartSequence(Runtime& rt, Reader& mem, const ScriptInstance& s, const std::function<void(const std::string&)>& log = {});

}   
