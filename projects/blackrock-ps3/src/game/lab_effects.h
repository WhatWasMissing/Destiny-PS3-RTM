#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <string>
#include <vector>

#include "game/lab_components.h"

namespace brps3 {
class Runtime;
}

namespace brps3::lab {

inline constexpr uint32_t kEffectTypeGetter = 0x1EE060;
inline constexpr uint32_t kEffectDefinitionLookup = 0x2049E0;    
extern const Fingerprints kEffectFingerprints;

struct EffectType {
    TypeKey type;                         
    bool descriptorExpected = false;      
};
EffectType readEffectType(world::Reader& mem, const world::TagTable& t);

 
struct ParamComponent {
    uint32_t index = 0;                   
    uint32_t offset = 0;                  
    uint32_t kind = 0;                    
    std::vector<uint32_t> keys;
     
    uint32_t runtime = 0;                 
    uint32_t notification = 0;            
    uint32_t valuesAt = 0;                
    std::vector<uint32_t> values;         
    std::string why;                      
};
struct EffectParameters {
    Definition definition;
    uint32_t record = 0, recordSize = 0;  
    uint32_t root = 0, count = 0;
    std::vector<ParamComponent> components;
    std::string why;                      
    bool ok() const { return why.empty(); }
    size_t keyCount() const;
};
 
EffectParameters readEffectParameters(world::Reader& mem, const world::TagTable& t, uint32_t record, uint32_t recordSize);
 
EffectParameters decodeEffectParameters(WordSource& def, uint32_t defExtent, WordSource& rec, uint32_t recSize, uint32_t defTag);

 
bool channelOffset(uint32_t field, uint32_t relative, uint32_t count, uint32_t stride, uint32_t extent, uint32_t& out);

 
struct NotificationPlan {
    uint32_t slot = 0xFFFFFFFFu;
    bool stored = false, inserted = false, forward = true;
};
 
NotificationPlan notificationPlan(const uint32_t keys[3], const uint32_t values[3], uint32_t key, uint32_t value);

 
struct NativeEffect {
    bool called = false;
    std::string refused;                  
    uint32_t handle = 0xFFFFFFFFu;        
};
 
 
 
 
 
NativeEffect nativeEffectOf(Runtime& rt, uint32_t owner, uint32_t typeHandle);

}   
