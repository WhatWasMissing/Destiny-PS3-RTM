#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "app/registry.h"
#include "game/guard_call.h"

namespace brps3::activity {

struct ChangeWorldReady {
    bool controllerOk = false;
    uint32_t state = 0xFFFFFFFFu;
    bool inOrbit = false;                
    uint32_t scenario = 0;
    uint32_t registry = 0;
    bool payloadOk = false;              
    uint32_t slotState = 0xFFFFFFFFu;    
    bool dependencyPending = false;      
     
     
    bool scenarioClientOk = false;
    uint32_t scenarioClient = 0;
    bool ready() const {
        return controllerOk && state == 36 && inOrbit && registry && payloadOk && slotState == 0 && !dependencyPending;
    }
    std::string blocker() const;         
};
 
 
ChangeWorldReady readChangeWorldReady(Ctx& c, uint32_t orbitScenario);

 
std::vector<guard::Cond> changeWorldGuards(uint32_t orbitScenario);
guard::Request changeWorldRequest(const std::string& activityName, uint32_t orbitScenario);
 
std::vector<uint8_t> phaseStub(uint32_t base, uint32_t ctrl);

struct ChangeWorldResult {
    bool called = false;                 
    std::string refused;                 
    int r3 = 0;                          
    bool phaseArmed = false, phaseFired = false;
    uint32_t slotAfter = 0xFFFFFFFFu;    
    bool queuedPhaseKnown = false;
    uint32_t queuedPhase = 0;            
    bool queued() const { return called && r3 == 1; }
    std::string summary() const;         
};

 
 
 
ChangeWorldResult changeWorld(Ctx& c, const std::string& activityName, uint32_t phaseHash, uint32_t orbitScenario);

}   
