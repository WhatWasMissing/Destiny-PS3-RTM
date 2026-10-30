#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <string>
#include <vector>

#include "game/world_mem.h"

namespace brps3::player {

struct Record {
    uint32_t offset = 0;                 
    uint32_t tag = 0, id = 0, value = 0, copy = 0;
    float f() const { return world::bitsFloat(value); }
    bool mirrored() const { return copy == value; }
};

 
bool isRecordAt(const world::Bytes& data, uint32_t v);
Record recordAt(const world::Bytes& data, uint32_t v);          
std::vector<Record> decodeRecords(const world::Bytes& data);    

 
 
struct FieldInfo {
    const char* name;
    const char* group;                   
    const char* unit;                    
    const char* note = "";               
};
const FieldInfo* fieldInfo(uint32_t id, uint32_t tag);
 
int groupOrder(const std::string& group);

 
struct ComponentView {
    uint32_t data = 0;                   
    uint32_t tagBase = 0;                
    uint32_t size = 0;                   
    world::Bytes bytes;                  
    uint32_t pluginBase() const { return tagBase ? tagBase : data; }
    bool has(uint32_t addr, uint32_t n) const {
        return addr >= data && uint64_t(addr - data) + n <= bytes.size() && uint64_t(addr - data) + n <= size;
    }
    uint32_t u32(uint32_t addr) const { return bytes.u32(addr - data); }    
};

enum class Via { PluginBase, AccessorPlus4, Accessor, AmmoPair, None };
const char* viaName(Via v);              

struct Located {
    bool ok = false;
    uint32_t addr = 0;                   
    uint32_t recordOffset = 0;           
    Via via = Via::None;
    Record record;                       
    std::string why;                     
};
 
Located locateRecord(const ComponentView& c, uint32_t pluginOffset);
 
 
Located locateAmmo(const ComponentView& c);
 
 
Located locateFullAuto(const ComponentView& c);

}   
