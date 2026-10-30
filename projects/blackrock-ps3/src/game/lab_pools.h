#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "game/world_registry.h"

namespace brps3::lab {

inline constexpr uint32_t kPoolReadSize = 0x80;    

struct PoolView {
    world::PoolInfo info;
    world::PoolHeader header;
    std::string runtimeName;             
    bool read = false;
    bool plausible = false, strideOk = false, capacityOk = false, nameOk = false;
    float usage() const { return header.capacity ? float(header.count) / float(header.capacity) : 0.0f; }
     
    std::string verdict() const;
    bool healthy() const { return read && plausible && strideOk && capacityOk; }
};
 
PoolView parsePoolView(const world::PoolInfo& info, const world::Bytes& b);
PoolView readPoolView(world::Reader& mem, const world::PoolInfo& info);
 
 
std::vector<PoolView> readPoolViews(world::Reader& mem, const std::vector<world::PoolInfo>& infos,
                                    const std::function<void(size_t, size_t)>& progress = {}, const std::function<bool()>& cancelled = {});

struct EntryDump {
    uint32_t first = 0, count = 0, stride = 0, base = 0;
    world::Bytes bytes;                  
    std::vector<std::optional<uint32_t>> identity;    
};
 
EntryDump readEntries(world::Reader& mem, const world::PoolHeader& h, uint32_t first, uint32_t count, uint32_t maxBytes = 0x8000);

struct GlobalView {
    world::GlobalInfo info;
    world::GlobalBlock block;
    bool read = false;
    bool registered = false;             
    std::vector<uint8_t> data;           
    std::string verdict() const;
};
 
GlobalView readGlobalView(world::Reader& mem, const world::GlobalInfo& info, const std::vector<uint32_t>& backRefs, uint32_t dumpBytes = 0x400);
 
std::vector<uint32_t> readRegistryRefs(world::Reader& mem);

}   
