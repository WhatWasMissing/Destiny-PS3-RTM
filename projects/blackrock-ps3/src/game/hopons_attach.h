#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "game/guard_call.h"
#include "game/hopons_mem.h"

namespace brps3 {
class Runtime;
}

namespace brps3::hopons {

enum class HopResult {
    Ok,
    BuildMismatch,       
    InvalidTarget,       
    NotResident,         
    UnsupportedType,     
    BuildFailed,         
    AttachFailed,        
    TrackingFailed,      
    NotOwned,            
    Expired,             
    Duplicate,           
    Refused,             
};
const char* hopResultText(HopResult r);

 
struct HopDefinition {
    HopResult status = HopResult::NotResident;
    std::string why;
    uint32_t tag = 0;
    TagMapping map;                      
    uint32_t limit = 0;                  
    uint32_t nativeClass = 0;            
    uint32_t classRecord = 0;            
    uint32_t definition = 0;             
    uint8_t type = 0;                    
};
HopDefinition checkDefinition(Reader& mem, TagReader& tags, uint32_t tag);
 
HopResult checkCompatible(uint8_t defType, uint8_t targetType, std::string* why = nullptr);

 
 
struct AccessorInfo {
    uint32_t accessor = 0, entry = 0;
    uint32_t word = 0, instance = 0, backOffset = 0, canonical = 0;
    uint32_t body = 0, target = 0, identity = 0;
    bool operator==(const AccessorInfo&) const = default;
};
struct AccessorPoolInfo {
    uint32_t base = 0, stride = 0, capacity = 0, mask = 0;
};
std::optional<AccessorPoolInfo> readAccessorPoolInfo(Reader& mem, std::string* why = nullptr);
std::optional<AccessorInfo> readAccessor(Reader& mem, uint32_t accessor, std::string* why = nullptr);
 
std::optional<uint32_t> handleIdentity(Reader& mem, uint32_t handle, std::string* why = nullptr);

enum class Life { Unverified, Active, RemovalRequested, Gone, Removed };
const char* lifeText(Life l);            

struct Receipt {
    uint32_t id = 0;
    uint32_t tag = 0, target = 0;
    bool self = false;                   
    uint8_t type = 0, targetType = 0;
    AccessorInfo acc;
    Life life = Life::Unverified;
    int64_t appliedAt = 0;               
    std::string note;                    
};

 
bool stillOwned(Reader& mem, const Receipt& r, std::string* why = nullptr);
 
 
size_t refreshReceipts(Reader& mem, std::vector<Receipt>& receipts);

 
inline constexpr const char* kApplyStubName = "hopons.apply.v1";
std::vector<uint8_t> applyStubProgram(uint32_t base);    
uint32_t applyStubSize();
uint32_t installApplyStub(Runtime& rt);
std::vector<uint8_t> arenaBytes();                        
guard::Request applyRequest(uint32_t stub, const HopDefinition& d, uint32_t target, uint8_t targetType, uint32_t arena);
guard::Request removeRequest(const Receipt& r, uint32_t accessorPoolBase);

struct ApplyOutcome {
    HopResult result = HopResult::Refused;
    std::string message;
    std::optional<Receipt> receipt;      
};
 
 
ApplyOutcome applyHop(Runtime& rt, Reader& mem, uint32_t tag, uint32_t target, bool self, const std::vector<Receipt>& existing, uint32_t id,
                      const std::function<void(const std::string&)>& log = {});
 
HopResult removeHop(Runtime& rt, Reader& mem, Receipt& r, const std::function<void(const std::string&)>& log = {});

}   
