#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "game/world_mem.h"
#include "game/world_objects.h"
#include "game/world_tags.h"

namespace brps3::hopons {

using world::Bytes;
using world::Reader;

inline bool storage(uint32_t addr, uint32_t size = 4) { return addr % 4 == 0 && world::mapped(addr, size); }
std::string hex8(uint32_t v);
std::string hexShort(uint32_t v);
std::string hexBytes(const std::vector<uint8_t>& b, size_t max = 0);
uint32_t be32At(const std::vector<uint8_t>& b, size_t off);

 
struct Guard {
    uint32_t addr = 0;
    std::vector<uint8_t> bytes;
    bool operator==(const Guard&) const = default;
};
 
Bytes guardRead(Reader& mem, uint32_t addr, uint32_t size, std::vector<Guard>& guards);
 
bool guardsUnchanged(Reader& mem, const std::vector<Guard>& guards, std::string* why = nullptr);

 
struct TagMapping {
    uint32_t tag = 0, page = 0, index = 0;
    uint32_t manager = 0, tableBase = 0;       
    uint32_t desc = 0;                         
    uint32_t registration = 0;                 
    uint32_t array = 0, stop = 0, stride = 0, mask = 0;    
    uint32_t entry = 0;                        
    uint32_t word = 0, metadata = 0, backOffset = 0;       
    uint32_t body = 0;                         
    Bytes descBytes, entryBytes;               
};

 
class TagReader {
public:
    explicit TagReader(Reader& mem);                        
    Reader& mem() { return mem_; }
    const world::TagTable& table() const { return table_; }
    void loadAllPages();                                    
    const Bytes& pageBytes(uint32_t page);                  
    world::TagPage page(uint32_t page) { return world::parsePage(pageBytes(page), page); }
     
     
     
    std::optional<TagMapping> map(uint32_t tag, uint32_t wantStride = 0, std::string* why = nullptr);
    uint32_t managerSnapshot() const { return table_.manager; }
private:
    Reader& mem_;
    world::TagTable table_;
    std::map<uint32_t, Bytes> pages_;
    Bytes all_;
};

 
 
 
uint32_t registrationOf(Reader& mem, uint32_t handle, std::string* why = nullptr);

 
 
 
uint32_t reconstructHandle(uint32_t identity, uint32_t mask, uint32_t nameSpace, uint32_t index);

struct PoolSlot {
    world::PoolHeader pool;
    uint32_t index = 0, handle = 0;            
    uint32_t identity = 0, identityAddr = 0;   
    uint32_t bitmapAddr = 0, bitmapWord = 0, bitmapBit = 0;    
    uint32_t headerCapacity = 0;               
};
 
std::optional<PoolSlot> poolSlot(Reader& mem, uint32_t poolAddr, uint32_t index, std::string* why = nullptr);

 
struct ContextRecord {
    uint32_t handle = 0, addr = 0;
    std::string name;                          
    uint32_t buffer = 0, extent = 0;           
    uint32_t w54 = 0, w58 = 0;                 
};
struct ContextPool {
    uint32_t base = 0, stride = 0, capacity = 0;
};
std::optional<ContextPool> readContextPool(Reader& mem, std::string* why = nullptr);
std::optional<ContextRecord> contextRecord(Reader& mem, uint32_t handle, std::string* why = nullptr);

 
 
inline unsigned nativeReferenceBank(unsigned kind) {
    return kind == 41 || kind == 42 || kind == 44 || kind == 45 || kind == 57 || kind == 58 ? 1u : 0u;
}
struct ScriptReference {
    bool present = false;                      
    uint32_t key = 0, manager = 0, allocation = 0, slot = 0, slotKind = 0, container = 0;
    uint16_t managerHandle = 0;
    uint8_t kind = 0, bank = 0;
    std::array<uint32_t, 3> target{};          
};
bool resolveReference(Reader& mem, const std::array<uint8_t, 8>& bytes, unsigned bank, ScriptReference& out, std::string& error);

}   
