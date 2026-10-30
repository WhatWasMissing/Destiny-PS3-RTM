#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "game/player_addr.h"
#include "game/player_gwl.h"
#include "game/player_records.h"
#include "game/world_components.h"
#include "game/world_player.h"
#include "game/world_tags.h"

namespace brps3 {
class Runtime;
}

namespace brps3::player {

enum class Feature : uint8_t {
    God, Interact, Pickup, Super, Grenade, Melee,                                                    
    Handling, ZoomMult, ZoomSpeed, Magazine, RateOfFire, Damage, Spread, Recoil, Ammo, FullAuto,     
    Count
};
inline constexpr size_t kFeatureCount = size_t(Feature::Count);
using FeatureSet = std::array<bool, kFeatureCount>;
const char* featureName(Feature f);      
const char* featureKey(Feature f);       
bool weaponFeature(Feature f);
inline bool anyOf(const FeatureSet& s) {
    for (bool b : s)
        if (b) return true;
    return false;
}

struct Magnitudes {
    float range = kRangeValue, ability = kAbilityValue, rpm = kRpmValue, magazine = kMagazineValue;
    uint32_t ammo = kAmmoValue;
    bool operator==(const Magnitudes&) const = default;
};

 
struct ComponentChain {
    bool weapon = false;
    uint32_t sobjPoolBase = 0;           
    uint32_t ownerHandle = 0, ownerEntry = 0;
    uint32_t parent = 0;                 
    uint32_t handle = 0;                 
    uint32_t accPoolBase = 0;            
    uint32_t accessor = 0, accFlags = 0, accBack = 0, accInstance = 0, accClass = 0;
    uint32_t data = 0, size = 0, tagBase = 0;
    uint32_t gate = 0;                   
    uint32_t pluginBase() const { return tagBase ? tagBase : data; }
    bool sameAs(const ComponentChain& o) const {
        return ownerHandle == o.ownerHandle && ownerEntry == o.ownerEntry && handle == o.handle && accessor == o.accessor &&
               accBack == o.accBack && accFlags == o.accFlags && data == o.data;
    }
    std::string label() const;           
};

struct Target {
    ComponentChain chain;
    ComponentView view;
};

struct Targets {
    bool havePlayer = false;
    bool weaponsScanned = false;
    std::string note;                    
    uint32_t playerHandle = 0, playerEntry = 0;
    std::optional<Target> player;        
    std::vector<Target> weapons;         
    int weaponsRefused = 0;              
    std::string weaponProblem;           
    std::vector<std::string> problems;   
};
 
Targets resolveTargets(world::Reader& mem, bool weapons, world::PlayerCache* cache = nullptr);
 
class Resolver {
public:
    explicit Resolver(world::Reader& mem);    
     
     
     
    std::optional<Target> component(const world::SObject& owner, bool weapon, std::string* problem, bool requireGate = true);
     
    uint32_t tagResolve(uint32_t handle);
    const world::AccessorPool& accessorPool() const { return acc_; }
    const world::PoolHeader& sobjectPool() const { return sobj_; }
    const std::string& tagError() const { return tagError_; }
    const std::string& tagNote() const { return tagNote_; }
private:
    struct PageInfo {
        world::TagPage page;
        uint32_t limit = 0;
    };
    world::Reader& mem_;
    world::AccessorPool acc_;
    world::PoolHeader sobj_;
    std::optional<world::TagTable> tags_;
    std::map<uint32_t, PageInfo> pages_;
    std::string tagError_, tagNote_;
};

 
std::vector<gwl::Guard> chainGuards(const ComponentChain& c);
 
bool chainCurrent(world::Reader& mem, const ComponentChain& c);

struct PlannedWrite {
    Feature feature = Feature::God;
    ComponentChain chain;
    uint32_t addr = 0, original = 0, applied = 0;
    std::string field;                   
    size_t group = 0, write = 0;         
};
struct FeatureReport {
    int targets = 0;                     
    int targetsWritten = 0;              
    int writes = 0;                      
    std::vector<std::string> refused;    
};
struct Plan {
    std::vector<gwl::Group> groups;
    std::vector<PlannedWrite> writes;
    std::array<FeatureReport, kFeatureCount> report{};
};
Plan plan(const Targets& t, const FeatureSet& enabled, const Magnitudes& m);

 
 
 
struct JournalEntry {
    Feature feature = Feature::God;
    ComponentChain chain;
    uint32_t addr = 0, original = 0, applied = 0;
    std::vector<uint32_t> earlier;
    std::string field;
    bool ours(uint32_t v) const { return v == applied || std::find(earlier.begin(), earlier.end(), v) != earlier.end(); }
};
class WriteJournal {
public:
    void note(const PlannedWrite& w);    
    std::vector<JournalEntry> take(Feature f);
    std::vector<JournalEntry> takeAll();
    size_t size() const { return entries_.size(); }
    const std::vector<JournalEntry>& entries() const { return entries_; }
private:
    std::vector<JournalEntry> entries_;
};
enum class RestoreResult { Restored, AlreadyOriginal, Changed, Gone };
const char* restoreResultName(RestoreResult r);
using WordWriter = std::function<void(uint32_t addr, uint32_t value)>;    
 
 
RestoreResult restore(world::Reader& mem, const WordWriter& write, const JournalEntry& e, bool chainChecked = false);
struct RestoreTally {
    int restored = 0, already = 0, changed = 0, gone = 0;
    int left() const { return changed + gone; }
};
 
RestoreTally restoreEntries(world::Reader& mem, const WordWriter& write, const std::vector<JournalEntry>& entries);

 
struct FeatureState {
    enum Level { Off, Waiting, Active, Partial, Refused } level = Off;
    std::string text;
    int writes = 0;
    bool landing = false;                
};
struct Board {
    std::array<FeatureState, kFeatureCount> features{};
    bool running = false;                
    uint32_t frames = 0;                 
    uint32_t playerHandle = 0;
    int weapons = 0;
    std::vector<std::string> problems;
    std::string lastError;
    double updated = 0;                  
};
Board boardSnapshot();
void resetBoard();
void publishBoard(Board b);          

 
class WriteManager {
public:
    using Log = std::function<void(const std::string&)>;
     
    void setFeature(Runtime& rt, world::Reader& mem, Feature f, bool on, const Log& log);
    void setMagnitudes(Runtime& rt, world::Reader& mem, const Magnitudes& m, const Log& log);
    Magnitudes magnitudes() const;
     
     
    void tick(Runtime& rt, world::Reader& mem, const Log& log);
     
    void shutdown(Runtime& rt, world::Reader& mem, const Log& log);
    bool anyOn() const;
    FeatureSet enabled() const;
    size_t journalSize() const;
     
    std::vector<std::string> describe(Runtime& rt);
private:
    void sync(Runtime& rt, world::Reader& mem, bool resolve, const Log& log);
    void restoreFeature(Runtime& rt, world::Reader& mem, Feature f, const Log& log);
    void publish(const gwl::TableStats* stats);
    mutable std::recursive_mutex m_;
    FeatureSet enabled_{};
    Magnitudes mags_;
    gwl::WriteList list_{"player.writes"};
    WriteJournal journal_;
    Targets targets_;
    bool haveTargets_ = false;
    Plan plan_;
    std::chrono::steady_clock::time_point resolvedAt_{}, failingSince_{};
    bool failing_ = false;
    int brokenStreak_ = 0;
    std::vector<uint32_t> lastHits_;
    std::array<bool, kFeatureCount> landing_{};
    world::PlayerCache cache_;
    std::string lastError_;
};

}   
