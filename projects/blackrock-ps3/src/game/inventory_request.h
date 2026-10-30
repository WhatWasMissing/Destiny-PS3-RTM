#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "game/guard_call.h"
#include "game/inventory_addr.h"
#include "game/inventory_policy.h"

namespace brps3 {
class Runtime;
}

namespace brps3::inventory {

 
guard::Request buildRequest(const Snapshot& s, const Plan& p);
const Fingerprints& fingerprintsFor(Action a);    
uint32_t nativeFor(Action a);                     
int opFor(Action a);                              

 
inline constexpr uint32_t kGateMagic = 0x42524947;           
inline constexpr const char* kGateStubName = "inventory.gate.v1";
inline constexpr uint32_t kGateBlock = 0x20;                 
                                                             
inline constexpr uint32_t kGateProviderNull = 0xA1, kGateNotReady = 0xA2, kGateBadBlock = 0xA3;
std::vector<uint8_t> gateProgram(uint32_t base);
uint32_t installGate(Runtime& rt);
std::vector<uint8_t> encodeGateBlock(uint32_t block, uint32_t guardStub, const guard::Request& req);

struct SendResult {
    bool called = false;                 
    bool uncertain = false;              
    std::string reason;                  
    uint32_t provider = 0, state = 0;    
    uint32_t outcome = 0;                
    guard::Result guard;
};
 
SendResult decodeGate(uint32_t outcome, uint32_t provider, uint32_t state, const guard::Request& req);
 
 
SendResult send(Runtime& rt, const guard::Request& req, int timeoutMs = 3000);

 
struct ActionRequest {
    Action action = Action::Deposit;
    Owner owner = Owner::Character;
    uint16_t slot = 0;
    uint16_t def = 0;
    uint64_t soid = 0;
    uint32_t quantity = 1;
    uint64_t expectedCharacter = 0;      
    bool dryRun = false;                 
};

struct ActionResult {
    Plan plan;
    SendResult sent;
    Outcome outcome;
    bool confirmed = false;
    std::string message;                 
    std::vector<std::string> conditions;    
    Snapshot before, after;
    bool haveAfter = false;
};

struct ActionHooks {
    std::function<void(const std::string&)> progress;
    std::function<bool()> cancelled;     
    int confirmMs = 20000;
    int pollMs = 300;
};

 
ActionResult runAction(Runtime& rt, world::Reader& mem, DefinitionCache& cache, const ActionRequest& r, const Catalog& cat,
                       const ActionHooks& hooks = {});
 
 
ActionResult sendAction(Runtime& rt, world::Reader& mem, DefinitionCache& cache, const ActionRequest& r, const Catalog& cat,
                        const ActionHooks& hooks = {});
 
 
void confirmAction(world::Reader& mem, DefinitionCache& cache, ActionResult& a, const Catalog& cat, const ActionHooks& hooks = {});
 
 
 
 
struct ConfirmWatch {
    std::chrono::steady_clock::time_point deadline{};
    uint32_t seen = 0;                   
    bool started = false;
};
bool confirmStep(world::Reader& mem, DefinitionCache& cache, ActionResult& a, ConfirmWatch& w, const Catalog& cat, const ActionHooks& hooks = {});
bool needsConfirmation(const ActionResult& a);    

 
 
 
 
 
 
void verifyTransferDefinition(world::Reader& mem, const DefinitionCache& cache, uint16_t def, const Catalog& cat);

}   
