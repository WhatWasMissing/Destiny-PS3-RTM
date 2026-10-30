#pragma once
 
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "core/error.h"

namespace brps3 {

class Ps3Mapi {
public:
    static constexpr int PORT = 7887;
    static constexpr uint32_t CHUNK = 0x10000;

     
    explicit Ps3Mapi(const std::string& host, int timeoutMs = 10000);
    ~Ps3Mapi();
    Ps3Mapi(const Ps3Mapi&) = delete;
    Ps3Mapi& operator=(const Ps3Mapi&) = delete;

    std::vector<uint32_t> pids();
    std::string processName(uint32_t pid);
     
    std::pair<uint32_t, std::string> findProcess(const std::string& needle);

     
    std::vector<uint8_t> read(uint32_t pid, uint32_t addr, uint32_t size);
     
    void write(uint32_t pid, uint32_t addr, const void* data, size_t size);

    const std::string& host() const { return host_; }
    int port() const { return port_; }

private:
    std::string command(const std::string& text);       
    std::string value(const std::string& text);         
    std::string line();
    uintptr_t pasv();
     
     
    void ready();
    bool desync_ = false;
    std::string host_;
    int port_;
    uintptr_t ctrl_ = ~uintptr_t(0);                    
    int timeoutMs_;
    std::string rbuf_;
    void* oplock_ = nullptr;                            
    std::mutex mutex_;
};

}   
