#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "game/guard_call.h"
#include "game/hopons_attach.h"
#include "game/hopons_catalog.h"
#include "game/hopons_mem.h"

namespace brps3 {
class Runtime;
}

namespace brps3::hopons {

 
 
 
 
 
 
inline constexpr size_t kBaseGuards = 10;
struct ScriptRef {
    EncounterDef def;
    uint32_t address = 0;                
    uint32_t page = 0, index = 0;
    uint32_t desc = 0, registration = 0;
    uint32_t entry = 0, entryWord = 0, entryMeta = 0, entryBack = 0;
    uint32_t blobBody = 0, start = 0;    
    std::vector<Guard> guards;           
};
 
 
 
void instanceConds(guard::Request& q, const ScriptRef& r);
struct ScriptInstance {
    ScriptRef ref;
    std::vector<uint8_t> bytes;          
    std::vector<uint8_t> input;          
    uint32_t upstream = 0, record = 0, context = 0, payload = 0;
    bool joined = false, inputStable = false;
    bool headerVerified = false;         
    std::vector<uint8_t> recordBytes;    
    std::string inputStatus;
};
struct ScriptScan {
    std::vector<ScriptInstance> instances;
    uint32_t pages = 0, candidates = 0, unmatched = 0, sourceMismatch = 0;
    uint64_t reads = 0;
    bool cancelled = false;
};
using ScanProgress = std::function<void(uint32_t done, uint32_t total, size_t found)>;
ScriptScan scanScripts(Reader& mem, const EncounterCatalog& cat, const std::function<bool()>& cancelled = {}, const ScanProgress& progress = {});
 
bool validateScript(Reader& mem, const ScriptRef& ref, std::string* why = nullptr);
 
bool refreshScript(Reader& mem, const ScriptRef& ref, ScriptInstance& out, std::string* why = nullptr);
bool joinInput(Reader& mem, ScriptInstance& s, TagReader* tags = nullptr);
 
 
bool joinsDuringScan(uint32_t cls);
 
uint32_t expectedInputSize(const EncounterDef& d);

 
struct ScriptField {
    uint32_t offset = 0;
    std::string name, unit, help;
    double value = 0;
    enum Kind { Boolean, Integer, Float } kind = Integer;
};
 
 
std::vector<ScriptField> timerFields(const ScriptInstance& s);
std::string timerFieldsUnavailable(const ScriptInstance& s);    
struct ScriptEdit {
    ScriptRef ref;
    uint32_t offset = 0;
    std::vector<uint8_t> original, applied;
    std::string label;
    int64_t at = 0;                      
    std::vector<uint8_t> before;         
};
 
 
std::optional<ScriptEdit> prepareTimerEdit(Reader& mem, const ScriptInstance& s, const ScriptField& f, double value, std::string& why);
 
void writeTimerEdit(Runtime& rt, Reader& mem, const ScriptEdit& e);
 
bool restoreTimerEdit(Runtime& rt, Reader& mem, const ScriptEdit& e, std::string& why);
 
std::string serialiseEdits(const std::vector<ScriptEdit>& edits);
std::vector<ScriptEdit> parseEdits(const std::string& text);    

 
struct SequenceState {
    std::array<uint32_t, 8> input{};     
    uint8_t selector = 0;                
    uint32_t pairIdentity = 0xFFFFFFFFu, pairAccessor = 0xFFFFFFFFu;    
    uint8_t state = 0, completed = 0;    
    bool hasPair() const { return pairIdentity != 0xFFFFFFFFu || pairAccessor != 0xFFFFFFFFu; }
};
std::optional<SequenceState> decodeSequence(const ScriptInstance& s);
const char* sequenceStateText(uint8_t state);
struct CleanupPlan {
    ScriptRef ref;
    SequenceState state;
    std::optional<AccessorInfo> tracked;    
};
std::optional<CleanupPlan> prepareCleanup(Reader& mem, const ScriptInstance& s, std::string& why);
guard::Request cleanupRequest(const CleanupPlan& p);
struct CleanupOutcome {
    bool called = false, cleared = false;
    std::string message;
};
CleanupOutcome cleanupSequence(Runtime& rt, Reader& mem, const ScriptInstance& s, const std::function<void(const std::string&)>& log = {});

 
struct SpawnerState {
    uint32_t actor = 0xFFFFFFFFu;        
    uint8_t gate = 0, initialized = 0;   
    uint32_t inputKey = 0;               
    uint32_t linkedEffect = 0xFFFFFFFFu;    
};
std::optional<SpawnerState> decodeSpawner(uint32_t cls, const std::vector<uint8_t>& bytes);
enum class RefreshAction { ReconcileInputState, RemoveTrackedActor, KeepTrackedActor, SpawnEligible, SpawnBlocked };
struct SpawnerDecision {
    RefreshAction action = RefreshAction::SpawnBlocked;
    uint8_t mode = 0, enabled = 0;
    bool hasActor = false, spawnGate = false;
};
std::optional<SpawnerDecision> inspectRefresh(uint32_t cls, const std::vector<uint8_t>& state, const std::vector<uint8_t>& input, bool stable);
const char* refreshActionText(RefreshAction a);
struct RosterEntry {
    uint32_t generation = 0, actor = 0xFFFFFFFFu;
};
struct SquadRoster {
    uint32_t count = 0;
    std::vector<RosterEntry> entries;
};
std::optional<SquadRoster> decodeRoster(uint32_t cls, const std::vector<uint8_t>& bytes);
struct SquadSpawnInput {
    uint8_t variant = 0;
    std::array<uint8_t, 3> contextFlags{};
};
std::optional<SquadSpawnInput> decodeSquadSpawnInput(uint32_t cls, const std::vector<uint8_t>& input, bool stable);

struct ActorObject {
    uint32_t actor = 0, actorAddress = 0, generation = 0;
    uint32_t object = 0, objectAddress = 0;
    uint32_t squadTag = 0xFFFFFFFFu, squadOffset = 0;       
    uint32_t ownerTag = 0xFFFFFFFFu, ownerOffset = 0;       
    PoolSlot slot;
    uint8_t objectType = 0;
};
std::optional<ActorObject> readActorObject(Reader& mem, uint32_t actor, std::string* why = nullptr);

struct SpawnerBinding {
    std::array<uint8_t, 8> squadReference{};
    uint32_t member = 0, address = 0;
};
std::optional<SpawnerBinding> readSpawnerBinding(Reader& mem, const ScriptRef& ref, std::string* why = nullptr);
struct SquadBinding {
    uint32_t tag = 0, offset = 0, address = 0, members = 0;
};
std::optional<SquadBinding> readSquadBinding(Reader& mem, const ScriptRef& ref, std::string* why = nullptr);
 
uint32_t instanceHandle(Reader& mem, const ScriptRef& ref, std::string* why = nullptr);

 
 
 
 
struct SquadRosterTarget {
    uint32_t tag = 0, root = 0, size = 0;
    uint32_t entry = 0, entryWord = 0, entryMeta = 0, entryBack = 0;
    std::array<uint32_t, 3> header{};
    uint32_t count = 0, selected = 0xFFFFFFFFu;
    std::vector<uint32_t> words;         
    bool operator==(const SquadRosterTarget&) const = default;
};
inline constexpr uint32_t kSquadRosterEnd = 0x1DC + 40 * 8;    
std::optional<SquadRosterTarget> captureSquadRoster(Reader& mem, uint32_t tag, uint32_t offset, uint32_t actor, uint32_t generation, std::string& why);
 
 
bool squadRosterRemoved(Reader& mem, const SquadRosterTarget& before, std::string* why = nullptr);

 
inline constexpr uint32_t kSpawnerRootSize = 0xA24;    
struct SpawnerTarget {
    uint32_t root = 0, actor = 0, linkedEffect = 0xFFFFFFFFu, selfHandle = 0;
    ActorObject actorObj;
    uint32_t bitmapAddr = 0, bitmapBit = 0;
    std::optional<SquadRosterTarget> squad;    
    std::vector<std::string> lines;      
};
std::optional<SpawnerTarget> captureSpawnerTarget(Reader& mem, const ScriptInstance& s, std::string& why);
guard::Request spawnerRemoveRequest(const ScriptRef& ref, const SpawnerTarget& t);
struct RemovalOutcome {
    bool called = false, cleared = false;
    std::string message;
    std::vector<std::string> lines;
};
RemovalOutcome removeTrackedActor(Runtime& rt, Reader& mem, const ScriptInstance& s, const std::function<void(const std::string&)>& log = {});

 
std::vector<std::string> inspectTrackedActor(Reader& mem, const ScriptInstance& s);
std::vector<std::string> inspectSquadReference(Reader& mem, const ScriptInstance& s, const ScriptScan& scan, uint32_t* squadRoot = nullptr);
std::vector<std::string> inspectSpatialReference(Reader& mem, const ScriptInstance& s);

}   
