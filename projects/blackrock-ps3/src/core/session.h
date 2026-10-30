#pragma once
 
 
 
 
 
 
 
 
 
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/ps3mapi.h"

namespace brps3 {

 
 
 
struct Journal {
    struct Patch {
        uint32_t address = 0;
        std::vector<uint8_t> original, written;
        bool operator==(const Patch&) const = default;
    };
    struct Scratch {
        uint32_t address = 0, size = 0;
        bool operator==(const Scratch&) const = default;
    };
    uint32_t pid = 0;
    std::string build;
    std::vector<Patch> patches;
    std::vector<Scratch> scratch;
    bool operator==(const Journal&) const = default;

    std::string text() const;                        
    static Journal parse(const std::string& text);   
    static std::filesystem::path path();             
};

class Session {
public:
    explicit Session(const std::string& host);    
    ~Session();                                    
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    uint32_t pid() const { return pid_; }
    const std::string& process() const { return process_; }
    Ps3Mapi& mapi() { return *mapi_; }

    std::vector<uint8_t> read(uint32_t addr, uint32_t size);
    uint32_t u32(uint32_t addr);
    void write(uint32_t addr, const void* data, size_t size, bool verify = true);
    void writeU32(uint32_t addr, uint32_t value, bool verify = true);

    void verifyBuild();                                
    void requireFree(uint32_t addr, uint32_t size);    

     
    void patch(uint32_t addr, const std::vector<uint8_t>& bytes);
    void restore(uint32_t addr);                       
    void claimScratch(uint32_t addr, uint32_t size);   
    void restoreAll(int settleMs = 500);               
    size_t patchCount() const { return patchCount_.load(); }

    static bool journalPending();
     
    static std::vector<std::string> recover(const std::string& host);

private:
    Session(const std::string& host, bool allowJournal);
    struct Patch { uint32_t addr; std::vector<uint8_t> original, written; };
    struct Scratch { uint32_t addr, size; };
    void saveJournal();
    void zero(uint32_t addr, uint32_t size);
    void releaseLock();
    std::unique_ptr<Ps3Mapi> mapi_;
    uint32_t pid_ = 0;
    std::string process_;
    void* lock_ = nullptr;                 
    std::vector<Patch> patches_;
    std::vector<Scratch> scratch_;
    std::atomic<size_t> patchCount_{0};
    mutable std::recursive_mutex mutex_;   
};

}   
