#pragma once
 
 
 
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "app/registry.h"
#include "app/settings.h"

namespace brps3 {

struct LogLine {
    double time;                         
    enum Level { Info, Good, Warn, Error } level;
    std::string text;
};

struct Toast {
    double time;
    LogLine::Level level;
    std::string text;
};

struct EngineStatus {
    enum State { Disconnected, Connecting, Connected, Disconnecting } state = Disconnected;
    std::string host, process, build, error;
    bool ticking = false;                
    float fps = 0;                       
    uint32_t heartbeat = 0;
    size_t patches = 0, hooks = 0, tasks = 0, loops = 0, queued = 0;
    std::string job;                     
    double latencyMs = 0;                
    bool journalPending = false;         
};

class Engine {
public:
    static Engine& get();

     
    void connect(const std::string& host);
    void disconnect();                   
    void recover(const std::string& host);    
    void shutdown();                     

     
    void post(const std::string& label, std::function<void(Ctx&)> job,
              std::function<void(bool ok, const std::string& error)> done = {});
    void cancel();                       
    bool busy(const std::string& label) const;
    void pump();                         

    EngineStatus status() const;         
    bool connected() const;

     
    void log(LogLine::Level level, const std::string& text);
    void toast(LogLine::Level level, const std::string& text);
    std::vector<LogLine> logSince(size_t& cursor) const;    
    std::vector<Toast> takeToasts();

     
     
     
     
     
     
    bool panic(const std::vector<std::string>& toggles = {}, const std::vector<std::string>& values = {});
    static constexpr const char* kPanicJob = "Panic: all off";

    Settings& settings();
     
    std::function<void()> onConnected;
    std::function<void()> onDisconnected;    
    std::function<void()> onPanicked;        

private:
    Engine();
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}   
