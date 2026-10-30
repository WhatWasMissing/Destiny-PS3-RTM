#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "game/world_mem.h"
#include "game/world_objects.h"

namespace brps3::activity {

struct WorldSnapshot {
    bool controllerOk = false;           
    uint32_t controller = 0, record = 0;
    uint32_t state = 0xFFFFFFFFu, requested = 0xFFFFFFFFu, reason = 0;    
    bool scenarioOk = false;
    uint32_t scenario = 0;               
    std::string target;                  
    uint32_t copy628 = 0;                
    uint64_t copy630 = 0;                
    uint32_t wtlGlobals = 0;
    uint16_t wtlCursor = 0xFFFF;
    uint32_t sobjectCount = 0;           
    uint32_t clock = 0;
    bool activeOk = false;
    std::string activeName;              
    bool loaderBusy() const { return wtlCursor != 0xFFFF; }
    bool inWorld() const { return controllerOk && state == 36; }
     
     
    bool aborting() const { return controllerOk && requested == 25 && reason != 0; }
};
WorldSnapshot readWorldSnapshot(world::Reader& mem);
inline constexpr uint32_t kMonitorBlock = 0x1A1D670, kMonitorBlockSize = 0x680;    
WorldSnapshot parseMonitorBlock(const world::Bytes& block, WorldSnapshot s);        

 
struct DescriptorRead {
    bool ok = false;
    std::string why;                     
    uint32_t base = 0, index = 0, datum = 0xFFFFFFFFu, tag = 0, object = 0, desc = 0;
    uint32_t fnvBefore = 0;              
    uint16_t id = 0, overrideId = 0;
    std::string name;                    
};
DescriptorRead readCurrentActivity(world::Reader& mem);
 
uint32_t datumTag(uint32_t nameSpace, uint32_t maskedSalt, uint32_t datum);

 
 
struct Transition {
    double time = 0;                     
    enum Kind { State, Requested, Abort, Target, Loader, Scenario, Lost, Found } kind = State;
    uint32_t from = 0, to = 0;
    std::string text;                    
    bool warn = false;                   
};
class TransitionTracker {
public:
    std::vector<Transition> feed(const WorldSnapshot& s, double now);
    void reset() { last_.reset(); }
private:
    std::optional<WorldSnapshot> last_;
};

}   
