#pragma once
 
 
 
 
 
 
 
 
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "game/world_mem.h"
#include "game/world_tags.h"

namespace brps3 {
class Runtime;
}

namespace brps3::loot {

class Console : public world::Reader {
public:
     
     
    virtual void write(uint32_t addr, const std::vector<uint8_t>& data, bool verify = true) = 0;
    void w8(uint32_t addr, uint8_t v, bool verify = true) { write(addr, {v}, verify); }
    void w32(uint32_t addr, uint32_t v, bool verify = true) { write(addr, world::be32(v), verify); }
    void w64(uint32_t addr, uint64_t v, bool verify = true);
    uint8_t u8(uint32_t addr) { return bytes(addr, 1).u8(0); }
    uint64_t u64(uint32_t addr) { return bytes(addr, 8).u64(0); }
};

class RuntimeConsole final : public Console {
public:
    explicit RuntimeConsole(Runtime& rt) : rt_(rt) {}
    std::vector<uint8_t> read(uint32_t addr, uint32_t size) override;
    void write(uint32_t addr, const std::vector<uint8_t>& data, bool verify = true) override;
    Runtime& rt() { return rt_; }
private:
    Runtime& rt_;
};

 
 
 
class Resolver {
public:
    explicit Resolver(world::Reader& mem) : mem_(mem) {}
     
    std::optional<uint32_t> body(uint32_t handle, bool tagChecks = false, std::string* why = nullptr);
    const world::TagTable& table();                   
    void clear();                                     
    size_t pagesRead() const { return pages_.size(); }
private:
    struct Page {
        world::TagPage desc;
        uint32_t limit = 0;
        bool ok = false;
        std::string why;
    };
    const Page& page(uint32_t index);
    world::Reader& mem_;
    std::optional<world::TagTable> table_;
    std::map<uint32_t, Page> pages_;
    std::map<uint32_t, uint32_t> bodies_;
};

 
std::string fmt(const char* f, ...);
std::string hex(uint32_t v);                          
bool heapPtr(uint32_t p, uint32_t size = 4);          
bool dataPtr(uint32_t p, uint32_t size = 4);          

}   
