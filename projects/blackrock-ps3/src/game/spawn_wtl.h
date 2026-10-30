#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <string>
#include <vector>

#include "game/world_addr.h"

namespace brps3 {
class Runtime;
}

namespace brps3::spawn::wtl {

using world::Fingerprints;

 
inline constexpr uint32_t GET_GLOBALS = 0x14E858, CREATE = 0x15216C, ADD_TAG = 0x15135C, RELEASE = 0x1523C4, SUBMIT = 0x1521E4,
                          WAIT = 0x150C70;
inline constexpr uint32_t GLOBALS = 0x1A1DCE0;           
inline constexpr uint32_t DRAIN_CURSOR = 0x1A1DCE8;      
inline constexpr uint32_t DET_POOL = 0x1A1DD20;          
inline constexpr uint32_t NONDET_POOL = 0x1A1DDA0;
inline constexpr uint32_t SOBJECT_COUNT = 0x1A1D9F4;     
inline constexpr uint32_t WORLD_CONTROLLER = 0x1ABC89C;  
inline constexpr uint32_t WORLD_STATE_OFFSET = 0x118;
inline constexpr uint32_t STATE_SLICE_LOADING = 34, STATE_IN_WORLD = 36, STATE_WORLD_TRANSITION = 33;
extern const Fingerprints kFingerprints;                 

 
const char* worldStateName(uint32_t state);              

enum class Kind : uint32_t { OnDemand = 1, Manifest = 2 };
enum class Trigger : uint32_t { Now = 0, LowObjects = 1, LoaderBusy = 2, WorldState = 3, Never = 4 };
const char* triggerName(Trigger t);

 
enum class Status : uint32_t {
    Pending = 0, Fired = 1, Waiting = 2, BadTag = 3, NoGlobals = 4, CreateFailed = 5, BadRequest = 6, BadBlock = 7, Disarmed = 8
};
const char* statusName(Status s);

struct Params {
    uint32_t tag = 0;
    Kind kind = Kind::OnDemand;
    bool wait = false;                   
    Trigger trigger = Trigger::Now;
    uint32_t value = 8;                  
    bool once = true;                    
};

 
inline constexpr uint32_t kMagic = 0x42525731;           
inline constexpr uint32_t kBlockSize = 0x60;
inline constexpr uint32_t kOffFlags = 0x04, kOffStatus = 0x08, kOffFrames = 0x0C, kOffTag = 0x10, kOffKind = 0x14, kOffTrigger = 0x18,
                          kOffValue = 0x1C, kOffLastState = 0x20, kOffLastCursor = 0x24, kOffLastCount = 0x28, kOffMinCount = 0x2C,
                          kOffFired = 0x30, kOffHandle = 0x34, kOffGlobals = 0x38, kOffRequest = 0x3C, kOffToc = 0x40,
                          kOffTransitionFrames = 0x44, kOffPair = 0x48, kOffOut = 0x50;
inline constexpr uint32_t kFlagArmed = 1, kFlagWait = 2, kFlagOnce = 4;

std::vector<uint8_t> stubProgram(uint32_t base);        
uint32_t stubAllocSize();
uint32_t installStub(Runtime& rt);                       
std::vector<uint8_t> encodeBlock(const Params& p, bool armed = true);
void validate(const Params& p);                          

struct State {
    Status status = Status::Pending;
    bool armed = false;
    uint32_t frames = 0;                 
    uint32_t transitionFrames = 0;       
    uint32_t fired = 0, handle = 0, globals = 0, request = 0;
    uint32_t lastState = 0xFFFFFFFF, lastCursor = 0xFFFFFFFF, lastCount = 0xFFFFFFFF, minCount = 0xFFFFFFFF;
    uint32_t tag = 0;
    Trigger trigger = Trigger::Now;
    uint32_t value = 0;
};
State decodeBlock(const std::vector<uint8_t>& block);    

 
 
State loadNow(Runtime& rt, const Params& p, int timeoutMs);

 
class AutoLoad {
public:
    static constexpr const char* kTask = "spawn.wtl.auto";
    void start(Runtime& rt, const Params& p);            
    void stop(Runtime& rt);                              
    bool running(Runtime& rt) const;
    State poll(Runtime& rt) const;                       
    uint32_t block() const { return block_; }
    const Params& params() const { return params_; }
private:
    uint32_t block_ = 0;
    Params params_;
};

}   

namespace brps3::spawn::rsc {

using world::Fingerprints;

inline constexpr uint32_t BEGIN = 0xE5898, END = 0xE5A18, RELEASE = 0xE120C;
inline constexpr uint32_t REQUESTS = 0x19A3F80;          
inline constexpr uint32_t PUMP = 0xE2E10, DISPATCHER = 0xE442C;    

struct Poster {
    uint32_t event;                      
    const char* name;
    uint32_t addr;
    const char* fingerprint;
    bool adds;                           
};
const std::vector<Poster>& posters();    
const Poster* findPoster(uint32_t event);
extern const Fingerprints kFingerprints;    

inline constexpr uint32_t kMagic = 0x42525231;           
inline constexpr uint32_t kBlockSize = 0x30;
inline constexpr uint32_t kOffFlags = 0x04, kOffStatus = 0x08, kOffCalls = 0x0C, kOffPoster = 0x10, kOffTag = 0x14, kOffCategory = 0x18,
                          kOffRequest = 0x1C, kOffEndR3 = 0x20, kOffToc = 0x28;
inline constexpr uint32_t kFlagArmed = 1, kFlagRaw = 2;
enum class Status : uint32_t { Pending = 0, Done = 1, BeginFailed = 2, BadBlock = 3, Disarmed = 4, BadTag = 5 };
const char* statusName(Status s);

std::vector<uint8_t> stubProgram(uint32_t base);
uint32_t stubAllocSize();
uint32_t installStub(Runtime& rt);                       
 
std::vector<uint8_t> encodeBlock(uint32_t poster, uint32_t tag, uint32_t category, bool raw = false);

struct Result {
    Status status = Status::Pending;
    uint32_t request = 0xFFFF;           
    uint32_t endR3 = 0;                  
};
Result decodeBlock(const std::vector<uint8_t>& block);
 
 
Result run(Runtime& rt, uint32_t event, uint32_t tag, uint32_t category = 1, int timeoutMs = 3000, bool raw = false);

}   
