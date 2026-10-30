#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "game/loot_addr.h"
#include "game/loot_console.h"
#include "game/world_objects.h"

namespace brps3::loot::poe {

 
struct Context {
    uint32_t slices = 0, map = 0, sliceIndex = 0;       
    uint32_t world = 0, worldState = 0;                  
    uint32_t listRoot = 0;                               
    uint32_t clock = 0;                                  
    int32_t scale = 0;                                   
    world::PoolHeader pool;                              
    std::string stateName;                               
     
    bool sameAs(const Context& o) const {    
        return slices == o.slices && map == o.map && sliceIndex == o.sliceIndex && world == o.world && listRoot == o.listRoot && clock == o.clock &&
               pool.base == o.pool.base && pool.stride == o.pool.stride && pool.capacity == o.pool.capacity;
    }
};
 
 
Context readContext(world::Reader& mem, bool requireInWorld = true);
std::string worldStateName(world::Reader& mem, uint32_t state);    

 
enum class Family { Main, Side };
const char* familyName(Family f);                      

struct Timers {
    uint8_t open = 0, looted = 0, authority = 0;
    int32_t countdown = 0, lifetime = 0;
    float seconds() const { return float(lifetime) / float(TICKS_PER_SECOND); }
};
Timers parseTimers(const world::Bytes& rec, uint32_t offsetOfOpen);    

struct Chest {
    Family family = Family::Main;
    uint32_t address = 0;                
    uint32_t sequence = 0, defOffset = 0, classTag = 0, handle = 0, owner = 0;
    uint32_t definitionBase = 0, definition = 0;
    uint32_t linkAddress = 0, link0 = 0, link1 = 0;     
    uint32_t ownerGenAddr = 0;           
    uint32_t expiry0 = 0, expiry1 = 0;
    std::vector<uint8_t> definitionBytes;               
    Timers timers;                        
    uint32_t key() const { return family == Family::Main ? POE_MAIN_KEY : POE_SIDE_KEY; }
    std::string label() const;           
};

 
struct Node {
    uint32_t handle = 0, address = 0, sequence = 0, classTag = 0;
    bool opened = false;                  
    std::string note;                     
};
struct Discovery {
    Context ctx;
    std::vector<Chest> chests;            
    std::vector<Node> nodes;              
    std::vector<std::string> warnings;    
    uint32_t mains() const;
    uint32_t sides() const;
};
using Cancel = std::function<bool()>;
using Progress = std::function<void(const std::string&)>;
 
 
Discovery discover(world::Reader& mem, Resolver& res, const Cancel& cancel = {}, const Progress& progress = {});
 
 
 
std::vector<Chest> select(const Discovery& d, bool mainOnly);

 
 
 
Timers validate(world::Reader& mem, Resolver& res, const Context& admitted, const Chest& c, bool deep);

 
 
 
std::optional<int32_t> refillTarget(int32_t ticks);
 
void naturalWindow(int32_t previousTicks, int32_t currentTicks, double elapsedSeconds);

 
struct LoopConfig {
    bool mainOnly = false;
    double intervalS = 0;                
    uint32_t cycleLimit = 0;             
    double timeoutS = 5;                 
    bool keepalive = true;               
};
bool validConfig(const LoopConfig& c, std::string* why = nullptr);    

struct LoopStats {
    uint32_t cycles = 0, writes = 0, refills = 0, current = 0;
    bool pending = false;
    std::vector<uint32_t> chestCycles, chestRefills;
    std::vector<int32_t> lifetimes;      
    bool done = false;                   
};

class PcLoop {
public:
    PcLoop(Context ctx, std::vector<Chest> chests, LoopConfig cfg, uint64_t nowMs);
     
     
    std::string step(Console& con, uint64_t nowMs);
     
     
     
     
    std::string keepalive(Console& con, uint64_t nowMs);
     
     
    bool failedInReplay() const { return replayStage_; }
     
     
    bool restore(Console& con, std::string* why = nullptr);
    const LoopStats& stats() const { return stats_; }
    const std::vector<Chest>& chests() const { return chests_; }
    void setInterval(double s) { cfg_.intervalS = s; }
private:
    std::string pollTimer(Console& con, size_t index, uint64_t nowMs);
    Context ctx_;
    std::vector<Chest> chests_;
    LoopConfig cfg_;
    LoopStats stats_;
    uint64_t nextWriteAt_ = 0, pendingSince_ = 0;
    bool replayStage_ = false;
    std::vector<uint64_t> nextPoll_, lastPollAt_;
    std::vector<int32_t> lastTicks_;
};

 
 
 
namespace ctl {
inline constexpr uint32_t MAGIC = 0x00, ENABLED = 0x04, STATUS = 0x08, FAULT = 0x0C, CYCLES = 0x10, WRITES = 0x14,
                          REFILLS = 0x18, FRAMES = 0x1C, CURRENT = 0x20, PENDING = 0x24, WAIT = 0x28, INTERVAL = 0x2C,
                          TIMEOUT = 0x30, COUNT = 0x34, LIMIT = 0x38, DELAY = 0x3C, CTX_SLICES = 0x40, CTX_MAP = 0x44,
                          CTX_WORLD = 0x48, KEEPALIVE = 0x4C, CHESTS = 0x50;
 
inline constexpr uint32_t C_ADDR = 0x00, C_SEQ = 0x04, C_DEFOFF = 0x08, C_HANDLE = 0x0C, C_OWNER = 0x10, C_GEN = 0x14,
                          C_LINK = 0x18, C_LINK0 = 0x1C, C_LINK1 = 0x20, C_LIFETIME = 0x24, C_REFILLS = 0x28,
                          C_CYCLES = 0x2C, C_CLASS = 0x30, CHEST = 0x40;
inline constexpr uint32_t SIZE = CHESTS + 3 * CHEST;      
inline constexpr uint32_t MAGIC_VALUE = 0x42525045;      
}   
enum class TaskStatus : uint32_t { Idle = 0, Running = 1, Faulted = 2, Done = 3 };
 
enum : uint32_t {
    kFaultNone = 0, kFaultSlices = 1, kFaultMap = 2, kFaultWorld = 3, kFaultState = 4, kFaultIdentity = 5, kFaultOwner = 6,
    kFaultLink = 7, kFaultNotOpen = 8, kFaultCountdown = 9, kFaultExpiry = 10, kFaultByte = 11, kFaultTimeout = 12,
    kFaultNotSettled = 13,
};
const char* faultText(uint32_t code);
inline constexpr uint32_t kTaskSize = 0x300;             

std::vector<uint8_t> taskProgram(uint32_t base);         
 
uint32_t framesFor(double seconds);
std::vector<uint8_t> encodeControl(const Context& ctx, const std::vector<Chest>& chests, const LoopConfig& cfg, bool enabled);

struct TaskState {
    bool valid = false;                  
    bool enabled = false;
    TaskStatus status = TaskStatus::Idle;
    uint32_t fault = 0, cycles = 0, writes = 0, refills = 0, frames = 0, current = 0, wait = 0, count = 0;
    bool pending = false;
    struct PerChest {
        int32_t lifetime = 0;
        uint32_t refills = 0, cycles = 0;
    };
    std::vector<PerChest> chests;
};
TaskState decodeControl(const std::vector<uint8_t>& block);

}   
