#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <optional>
#include <vector>

#include "game/world_objects.h"

namespace brps3::world {

inline constexpr uint32_t kAccessorStride = 0x18;
inline constexpr uint32_t kAccessorMaxCapacity = 8192;
inline constexpr uint32_t kAccessorAllocFlag = 0xC0000000u;

struct AccessorPool {
    PoolHeader header;                   
    uint32_t mask = 0;                   
    bool plausible() const;              
    uint32_t entry(uint32_t index) const { return header.base + index * kAccessorStride; }
    bool sameLayout(const AccessorPool& o) const {
        return header.base == o.header.base && header.stride == o.header.stride && header.capacity == o.header.capacity && mask == o.mask;
    }
};
AccessorPool readAccessorPool(Reader& mem);               
AccessorPool parseAccessorPool(const Bytes& header);      

struct Accessor {
    uint32_t index = 0, addr = 0;        
    uint32_t flags = 0, instance = 0, backOffset = 0, identity = 0, word14 = 0;
    uint32_t data = 0;                   
    bool allocated() const { return (flags & 0xF0000000u) == kAccessorAllocFlag; }
    uint32_t size() const { return flags & 0x0FFFFFFFu; }
    uint32_t cls() const { return word14; }           
    uint32_t nextArena() const { return word14; }     
};
Accessor parseAccessor(const AccessorPool& p, const Bytes& entry, uint32_t index);    

 
std::optional<Accessor> resolveComponent(Reader& mem, const AccessorPool& p, uint32_t handle, bool identity = true);

 
struct AccessorTable {
    AccessorPool pool;
    Bytes entries;
    uint32_t count() const { return uint32_t(entries.size() / kAccessorStride); }
    Accessor at(uint32_t index) const;   
};
AccessorTable readAccessorTable(Reader& mem);             

struct Arena {
    uint32_t index = 0, data = 0, size = 0;
};
 
 
std::vector<Arena> arenaChain(const AccessorTable& t, uint32_t head);
std::vector<Arena> readArenaChain(Reader& mem, const AccessorPool& p, uint32_t head);    
 
 
std::vector<Accessor> componentsIn(const AccessorTable& t, const std::vector<Arena>& arenas);

}   
