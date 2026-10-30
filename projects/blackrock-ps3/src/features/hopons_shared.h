#pragma once
 
 
 
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "app/registry.h"
#include "game/hopons_scripts.h"
#include "game/hopons_swap.h"

namespace brps3::hoponsui {

inline std::string hx(uint32_t v) {
    char b[12];
    std::snprintf(b, sizeof b, "%08X", v);
    return b;
}

 
std::string ago(double seconds);
double nowSeconds();                     
int64_t unixNow();
std::string clockTime(int64_t unixSeconds);    

 
 
struct Flow {
    float width = 0, used = 0;
    bool first = true;
    Flow();
    void next(float w, float gap);
};

 
struct ScanState {
    std::mutex m;
    hopons::ScriptScan scan;             
    bool have = false;
    double at = 0;                       
    uint64_t revision = 0;               
    std::string status;                  
    bool statusOk = true;
    std::string progress;
    float fraction = -1;
    std::atomic<bool> running{false};
};
ScanState& scanState();
inline constexpr const char* kDiscoverJob = "Discover Encounter Instances";
 
void postDiscover(Ui& ui);
void discover(Ctx& c);                   
 
void replaceInstance(uint32_t address, const hopons::ScriptInstance* fresh);

 
std::string journalPath(const char* name);    
std::string readJournal(const char* name);    
void writeJournal(const char* name, const std::string& text);    
 
 
 
void armTimerRecovery(Ctx& c);
void armSwapRecovery(Ctx& c);

 
void ensureEncounterCatalog();
 
void selectEncounterRoot(uint32_t root);

 
void openSwapFor(uint32_t squadTag);

}   
