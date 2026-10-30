#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "core/runtime.h"
#include "game/patches_book.h"
#include "game/patches_sites.h"
#include "game/patches_tick.h"
#include "game/world_player.h"

namespace brps3::patches {

 
 
enum class RespawnArg : int {
    PlayersDatum = 0,                    
    ObjectHandle = 1,                    
    Native = 2,                          
};
const char* respawnArgName(RespawnArg a);

 
struct EntitleOptions {
    bool access = true;                  
    bool overrideByte = true;            
    bool suppress = true;                
    bool validBit = false;               
    bool unlock = false;                 
    bool counter = false;                
    bool gate = false;                   
    bool operator==(const EntitleOptions&) const = default;
};

struct Snapshot {
     
    bool taskRunning = false;
    uint32_t block = 0, flags = 0, frames = 0;
     
    bool fastRespawn = false, respawnArmed = false;
    RespawnArg respawnArg = RespawnArg::PlayersDatum;
    uint32_t latchedHandle = 0, latchedDatum = 0, latchedElement = 0;
    uint64_t argument = 0;
    uint32_t respawnCalls = 0, lastSeen = 0;
    bool playerAlive = false;
    std::string respawnNote;
     
    bool fovOn = false, speedOn = false, viewmodelOn = false, afkOn = false;
    float fovDeg = kFovDefault, speedMult = kSpeedDefault, viewmodelDeg = kViewmodelDefault;
    uint32_t speedSkips = 0;
    float speedSeen = 0;
    std::string viewmodelNote;
     
    bool entitleOn = false;
    EntitleOptions entitle;
    uint32_t entitleInner = 0;
    std::string entitleNote;
     
    std::array<bool, size_t(Feature::Count)> patchOn{};
    std::vector<SiteBook::UnitStatus> units;
    std::string lastError;               
    size_t dataEdits = 0;
};

class Controller {
public:
    Controller();
    ~Controller();

     
     
     
    void setPatch(Runtime& rt, Feature f, bool on);
    bool patchOn(Feature f) const;

     
     
     
    void setFastRespawn(Runtime& rt, world::PlayerCache& cache, bool on);
    void setRespawnArg(Runtime& rt, RespawnArg a);

     
     
    void setFov(Runtime& rt, bool on);
    void setFovDegrees(Runtime& rt, float deg);
    void setViewmodel(Runtime& rt, bool on);
    void setViewmodelDegrees(Runtime& rt, float deg);
    void setSpeed(Runtime& rt, bool on);
    void setSpeedValue(Runtime& rt, float mult);
    void setNoInactivity(Runtime& rt, bool on);

     
    void setEntitle(Runtime& rt, bool on, const EntitleOptions& o);

     
     
     
     
    void setTracking(bool on);
    bool monitorNeeded() const;
    void monitor(Runtime& rt, world::PlayerCache& cache);
    void refresh(Runtime& rt);           

     
     
    std::vector<std::string> allOff(Runtime& rt);
     
     
    std::vector<std::string> recoverLeftovers(Runtime& rt);

    Snapshot snapshot() const;
    SiteBook& book() { return book_; }
     
    std::vector<std::string> takeLog();

     
     
     
     
     
    struct DataEdit {
        uint32_t addr = 0;
        uint8_t size = 4;                
        uint32_t original = 0, applied = 0;
        std::string what;
        uint32_t anchor = 0, anchorValue = 0;    
    };
    static std::string journalText(uint32_t pid, const std::vector<DataEdit>& edits);
    static bool parseJournal(const std::string& text, uint32_t& pid, std::vector<DataEdit>& edits);

private:
    void begin(Runtime& rt);             
    void noteBorrowed(const SiteBook::Result& r);
     
    void ensureTask(Runtime& rt);
    void pushBlock(Runtime& rt, uint32_t changing);
    void dropTaskIfIdle(Runtime& rt);
    void settle(Runtime& rt);
    tick::Counters readCounters(Runtime& rt);
     
    void recordEdit(const DataEdit& e);
    void forgetEdit(uint32_t addr);
    std::optional<DataEdit> edit(uint32_t addr) const;
    void saveJournal();
    void restoreEdit(Runtime& rt, uint32_t addr, std::vector<std::string>* errors);
    static bool anchorHolds(Runtime& rt, const DataEdit& e);
     
    bool latch(Runtime& rt, world::PlayerCache& cache, bool force);
    void armRespawn(Runtime& rt);
    void applyViewmodelTarget(Runtime& rt, const ViewmodelTarget& t);
    void entitleOverrideOn(Runtime& rt);
    void entitleOverrideOff(Runtime& rt, std::vector<std::string>* errors);
    void entitlePartsOff(Runtime& rt, const EntitleOptions& parts, std::vector<std::string>* errors);

    mutable std::recursive_mutex m_;
    SiteBook book_;
    std::array<bool, size_t(Feature::Count)> on_{};
     
    uint32_t block_ = 0;
    tick::Block blk_;                    
    uint32_t liveFlags_ = 0;             
    tick::Counters counters_;
     
    bool fovOn_ = false, vmOn_ = false, speedOn_ = false, afkOn_ = false;
    float fovDeg_ = kFovDefault, vmDeg_ = kViewmodelDefault, speed_ = kSpeedDefault;
    uint32_t fovOriginal_ = 0;
    ViewmodelTarget vm_;
    std::string vmNote_;
     
    bool fast_ = false, track_ = false;
    RespawnArg arg_ = RespawnArg::PlayersDatum;
    struct Latch {
        bool ok = false;
        uint32_t desc = 0, elements = 0, element = 0, handle = 0, datum = 0;
    } latch_;
    bool alive_ = false;
    int latchAge_ = 0;
    std::string respawnNote_;
     
    bool entitleOn_ = false;
    EntitleOptions entitle_;
    uint32_t inner_ = 0, overrideRoot_ = 0;
    int innerAge_ = 0;
    uint32_t suppressObj_ = 0, root_ = 0;
    uint8_t suppressOriginal_ = 0;
    bool validOriginal_ = false;
    std::string entitleNote_;
     
    std::vector<DataEdit> edits_;
    uint32_t pid_ = 0;
    bool recovered_ = false;
    std::string lastError_;
    std::vector<std::string> log_;
};

 
inline uint64_t signExtend(uint32_t v) { return uint64_t(int64_t(int32_t(v))); }

}   
