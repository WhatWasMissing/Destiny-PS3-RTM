#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "game/world_addr.h"
#include "game/world_mem.h"

namespace brps3::world {

 
struct PoolHeader {
    uint32_t addr = 0;                   
    uint32_t header = 0, base = 0, stride = 0, capacity = 0, count = 0;
    uint32_t identityOffset = 0, identityStride = 0, mask = 0, mask2 = 0, classWord = 0, nameSpace = 0;
     
    bool plausible() const;
    uint32_t entry(uint32_t index) const { return base + index * stride; }
    uint32_t span() const { return capacity * stride; }
     
    std::optional<uint32_t> entryOf(uint32_t handle, uint32_t indexMask = 0x1FFF) const;
};
PoolHeader parsePool(const Bytes& b, uint32_t poolAddr);    
PoolHeader readPool(Reader& mem, uint32_t poolAddr);        
 
 
bool strideMatches(uint32_t stride, uint32_t elemSize, uint32_t align, uint32_t identityOffset);

 
struct WorldGlobals {
    uint64_t engineClock = 0;            
    PoolHeader players;                  
    PoolHeader attachments;              
    uint32_t sobjectGlobals = 0;         
    PoolHeader sobjects;                 
    uint32_t bank = 0;                   
    bool sobjectsOk() const { return sobjects.plausible() && sobjects.stride == sobj::STRIDE; }
};
WorldGlobals readGlobals(Reader& mem);
inline constexpr uint32_t kGlobalsBlock = 0x1A1D670, kGlobalsBlockSize = 0x3B0;

struct SObject {
    uint32_t addr = 0, index = 0;        
    uint32_t handle = 0, flags = 0;      
    uint8_t type = 0;                    
    uint32_t parent = 0;                 
    uint32_t componentHead = 0;          
    uint32_t attachment = 0;             
    uint32_t component = 0;              
    Vec3 vector40;                       
    Quat rot;                            
    Vec3 pos90;                          
    float scale = 1;                     
    Quat rot2;                           
    Vec3 position;                       
    float scale2 = 1;                    
    bool identityOk() const { return validHandle(handle) && (handle & 0x1FFF) == index; }
    bool deleting() const { return (flags & sobj::FLAG_DELETING) != 0; }
    bool live() const { return identityOk() && !deleting(); }
    bool hasAttachment() const { return validHandle(attachment); }
    const char* typeLabel() const { return typeName(type); }
};
inline constexpr bool kScaleNeedsCheck = true;   

SObject parseSObject(const Bytes& entry, uint32_t index);    
 
 
std::optional<SObject> readSObject(Reader& mem, const PoolHeader& sobjects, uint32_t handle);
std::optional<SObject> readSObject(Reader& mem, uint32_t handle);    

 
 
struct ObjectScan {
    PoolHeader pool;
    std::vector<SObject> objects;        
    uint32_t scanned = 0;
};
ObjectScan readObjects(Reader& mem, uint32_t maxEntries = 0);
ObjectScan parseObjects(const PoolHeader& pool, const Bytes& entries);    

 
std::vector<SObject> childrenOf(const ObjectScan& scan, uint32_t parentHandle);

 
 
extern const Fingerprints kSobjectNewFingerprints;
 
 
const std::array<uint32_t, 32>& spawnDescTemplate();

}   
