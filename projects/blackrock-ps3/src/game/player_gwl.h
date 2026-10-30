#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <string>
#include <vector>

namespace brps3 {
class Runtime;
}

namespace brps3::player::gwl {

inline constexpr uint32_t kMagic = 0x42525731;           
inline constexpr const char* kProgramName = "player.gwl.v1";    
inline constexpr uint32_t kHeaderSize = 0x10, kGroupHeaderSize = 0x10, kGuardSize = 0xC, kWriteSize = 0x24;
inline constexpr uint32_t kMaxGroups = 64, kMaxGuards = 12, kMaxWrites = 96, kMaxWriteGuards = 2;
inline constexpr uint32_t kOffCount = 4, kOffFrames = 8, kOffEnd = 0xC;

struct Guard {
    uint32_t addr = 0, mask = 0xFFFFFFFFu, value = 0;
    std::string what;                    
    bool operator==(const Guard& o) const { return addr == o.addr && mask == o.mask && value == o.value; }
};
struct Write {
    uint32_t addr = 0, value = 0;
    std::vector<Guard> guards;           
    std::string what;
    bool operator==(const Write& o) const { return addr == o.addr && value == o.value && guards == o.guards; }
};
struct Group {
    std::string name;                    
    std::vector<Guard> guards;
    std::vector<Write> writes;
    bool operator==(const Group& o) const { return guards == o.guards && writes == o.writes; }
};

 
std::vector<uint8_t> program(uint32_t base);
uint32_t programSize();                  

void validate(const std::vector<Group>& groups);    
uint32_t tableSize(const std::vector<Group>& groups);
std::vector<uint8_t> encodeTable(const std::vector<Group>& groups, uint32_t tableAddr, bool armed = true);

struct GroupStats {
    uint32_t passes = 0, failed = 0;     
    std::vector<uint32_t> hits;          
};
struct TableStats {
    bool valid = false;                  
    uint32_t armedGroups = 0, frames = 0;
    std::vector<GroupStats> groups;
};
TableStats parseStats(const std::vector<uint8_t>& table, const std::vector<Group>& layout);

 
class WriteList {
public:
    explicit WriteList(std::string taskName = "player.writes") : task_(std::move(taskName)) {}
     
     
    void upload(Runtime& rt, const std::vector<Group>& groups);
     
     
    void stop(Runtime& rt);
    bool running(Runtime& rt) const;
    TableStats stats(Runtime& rt) const;    
    const std::vector<Group>& groups() const { return groups_; }
    uint32_t block() const { return block_; }
    const std::string& taskName() const { return task_; }
    bool dirty() const { return dirty_; }       
private:
    void fresh(Runtime& rt, uint32_t code, const std::vector<Group>& groups);    
    std::string task_;
    uint32_t block_ = 0, capacity_ = 0;
    std::vector<Group> groups_;
    bool dirty_ = false;
};

}   
