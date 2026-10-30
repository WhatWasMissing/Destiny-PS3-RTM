#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "core/profile.h"
#include "core/session.h"

namespace brps3 {

struct CallResult {
    uint64_t r3 = 0;
    double f1 = 0;
    uint32_t u32() const { return uint32_t(r3); }
    int32_t i32() const { return int32_t(uint32_t(r3)); }
};

struct NativeCall {
    uint32_t code = 0;                   
    std::vector<uint64_t> args;          
    std::vector<double> floats;          
    uint32_t toc = profile::TOC;
};

 
class Heap {
public:
    Heap(uint32_t base, uint32_t end);
    uint32_t alloc(uint32_t size, uint32_t align = 16);    
    void release(uint32_t addr);
    uint32_t sizeOf(uint32_t addr) const;
private:
    std::vector<std::pair<uint32_t, uint32_t>> free_;
    std::map<uint32_t, uint32_t> used_;
};

std::vector<uint8_t> executorProgram(uint32_t cave = profile::CAVE, uint32_t ctrl = profile::CONTROL,
                                     uint32_t tail = profile::TICK_TARGET);

class Runtime {
public:
    explicit Runtime(Session& s);
    ~Runtime();                          

    void install();
    void close();                        
    bool installed() const { return installed_.load(); }
    Session& session() { return s_; }

    uint32_t heartbeat();
    bool waitFrames(uint32_t n = 1, int timeoutMs = 3000);
    bool alive();                        

     
    std::vector<uint8_t> read(uint32_t addr, uint32_t size) { return s_.read(addr, size); }
    uint32_t u32(uint32_t addr) { return s_.u32(addr); }
    float f32(uint32_t addr);
    void write(uint32_t addr, const void* data, size_t size, bool verify = true);
    void writeU32(uint32_t addr, uint32_t v, bool verify = true);
    void writeF32(uint32_t addr, float v, bool verify = true);
    uint32_t alloc(uint32_t size, const void* data = nullptr, size_t n = 0, uint32_t align = 16);
    void free(uint32_t addr);            
     
     
     
     
    void freeLater(uint32_t addr);
    size_t pendingFrees() const;         

     
    CallResult call(uint32_t code, std::vector<uint64_t> args = {}, std::vector<double> floats = {},
                    int timeoutMs = 3000);
    std::vector<CallResult> callMany(const std::vector<NativeCall>& calls, int timeoutMs = 3000);

     
     
     
    using Build = std::function<std::vector<uint8_t>(uint32_t base)>;
    int addTask(const Build& build, uint32_t arg = 0, uint32_t size = 0x400);
    void removeTask(int index);
     
     
    int addTask(const std::string& name, const Build& build, uint32_t arg = 0, uint32_t size = 0x400);
     
    int addTaskAt(const std::string& name, uint32_t code, uint32_t arg);
    void removeTask(const std::string& name);          
    bool hasTask(const std::string& name) const;
    std::vector<std::string> taskNames() const;        
    size_t taskCount() const { return taskCount_.load(); }        

     
     
     
    uint32_t sharedCode(const std::string& name, const Build& build, uint32_t size = 0x400);
    std::optional<uint32_t> sharedCodeAt(const std::string& name) const;

     
     
    void hook(uint32_t site, const std::function<std::vector<uint8_t>(uint32_t base, uint32_t original)>& build,
              std::optional<uint32_t> expect = std::nullopt, uint32_t size = 0x400);
     
     
    void unhook(uint32_t site);
    bool hooked(uint32_t site) const;
    size_t hookCount() const { return hookCount_.load(); }        
    size_t retiredCount() const;                        
    bool caveExtended() const { return caveLowOk_.load(); }    
    void setRetireDelay(int ms);                        

     
     
     
     
     
     
     
     
     
     
     
     
    struct Word { uint32_t addr; std::vector<uint8_t> original, replacement; };
    bool apply(const std::string& name, const std::vector<Word>& words);    
    void revert(const std::string& name);               
    bool applied(const std::string& name) const;
    std::vector<std::string> appliedNames() const;      
     
     
    std::vector<std::string> siteHolders(uint32_t addr) const;

     
     
    struct RevertReport {
        size_t tasks = 0, hooks = 0, patches = 0;
        std::vector<std::string> errors;
    };
    RevertReport revertAll();

private:
    struct Task {
        uint32_t code = 0;
        bool owned = false;                             
        std::string name;
    };
    struct Retired {
        uint32_t base;
        uint64_t at;                                    
        uint32_t beat;                                  
    };
    int addTaskEntry(const std::string& name, uint32_t code, bool owned, uint32_t arg);
    uint32_t caveAlloc(uint32_t size);                  
    Heap& caveHeap(uint32_t addr);                      
    bool tryCaveAlloc(uint32_t size, uint32_t* out);
    void reclaimRetired(uint64_t now);
    void reclaimArena();                                
    void updateTaskCount();

    Session& s_;
    Heap cave_, arena_;
    Heap caveLow_;                                      
    std::atomic<bool> caveLowOk_{false};
    std::vector<std::optional<Task>> tasks_;
    std::map<uint32_t, uint32_t> hooks_;                
    std::map<std::string, std::vector<uint32_t>> named_;    
    std::vector<std::string> namedOrder_;               
    struct Layer {
        uint32_t size = 0;
        std::vector<uint8_t> original, replacement;
        std::vector<std::string> holders;               
    };
     
     
    std::map<uint32_t, std::vector<Layer>> sites_;
    std::map<std::string, uint32_t> shared_;
    std::vector<Retired> retired_;
    std::vector<std::pair<uint32_t, uint32_t>> arenaLater_;    
    int retireMs_ = 5000;
    std::atomic<bool> installed_{false};
    std::atomic<uint32_t> lastBeat_{0};
    std::atomic<uint64_t> lastBeatMs_{0};
    std::atomic<size_t> hookCount_{0}, taskCount_{0};
    mutable std::recursive_mutex mutex_;
};

}   
