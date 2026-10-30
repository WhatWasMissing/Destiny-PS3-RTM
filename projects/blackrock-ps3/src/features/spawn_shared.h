#pragma once
 
 
 
 
 
 
 
 
 
 
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "app/registry.h"
#include "game/spawn_catalog.h"
#include "game/spawn_ops.h"
#include "game/spawn_place.h"
#include "game/spawn_town.h"
#include "game/spawn_wtl.h"
#include "game/world_objects.h"

namespace brps3::spawnui {

using world::Vec3;

 
inline constexpr const char* kTabSpawn = "Spawn";
inline constexpr const char* kTabObjects = "Objects";
inline constexpr const char* kTabResident = "Resident tags";
inline constexpr const char* kTabWorld = "World load";
inline constexpr const char* kSecCatalog = "Spawn catalog";
inline constexpr const char* kSecPlacement = "Spawn placement";
inline constexpr const char* kSecSpawned = "Spawned this session";
inline constexpr const char* kSecOpeners = "Hidden vendors";
inline constexpr const char* kSecManual = "Spawn by tag";
inline constexpr const char* kSecObjects = "Live objects";
inline constexpr const char* kSecResidency = "Residency";
inline constexpr const char* kSecPackages = "Loaded packages";
inline constexpr const char* kSecTagCheck = "Check a tag";
inline constexpr const char* kSecWtl = "World tag load";
inline constexpr const char* kSecAuto = "Auto-load on transition";
inline constexpr const char* kSecRsc = "Resourcerer transaction";
inline constexpr const char* kSecTown = "Town presence";
inline constexpr const char* kProbeSection = "Spawner (spawn group)";

 
inline constexpr const char* kActSpawnSelected = "spawn.spawn_selected";
inline constexpr const char* kActSpawnCrosshair = "spawn.spawn_crosshair";
inline constexpr const char* kActSpawnPlayer = "spawn.spawn_player";
inline constexpr const char* kActDespawnLast = "spawn.despawn_last";
inline constexpr const char* kActDespawnMine = "spawn.despawn_mine";
inline constexpr const char* kActObjectsRefresh = "spawn.objects_refresh";
inline constexpr const char* kActPages = "spawn.residency_pages";
inline constexpr const char* kActScan = "spawn.residency_scan";
inline constexpr const char* kActWtlLoad = "spawn.wtl_load_selected";
inline constexpr const char* kActWtlArm = "spawn.wtl_arm";
inline constexpr const char* kActWtlDisarm = "spawn.wtl_disarm";
inline constexpr const char* kActRsc = "spawn.rsc_post";
inline constexpr const char* kActTownRefresh = "world.town.refresh";
inline constexpr const char* kToggleTown = "world.town.normal_equipment";
inline constexpr const char* kToggleObjectsAuto = "spawn.objects_auto";

 
inline constexpr const char* kJobSpawn = "Spawn";
inline constexpr const char* kJobDespawn = "Despawn";
inline constexpr const char* kJobObjects = "Read live objects";
inline constexpr const char* kJobPages = "Read loaded packages";
inline constexpr const char* kJobScan = "Scan Loaded Entities";
inline constexpr const char* kJobCheck = "Check tag";
inline constexpr const char* kJobMove = "Move object";
inline constexpr const char* kJobWtl = "World tag load";
inline constexpr const char* kJobArm = "Arm Auto-Load";
inline constexpr const char* kJobRsc = "Resourcerer transaction";
inline constexpr const char* kJobTown = "Town presence";
inline constexpr const char* kLoopObjects = "spawn.objects.auto";
inline constexpr const char* kLoopAuto = "spawn.wtl.poll";

 
struct SpawnedRow {
    uint32_t handle = 0, tag = 0;
    uint8_t type = 0;
    Vec3 position;
    std::string label;
    double at = 0;                       
    bool gone = false;                   
};

struct TagCheck {
    bool resident = false;
    uint8_t type = 0;
    std::string why;
    uint32_t element = 0, body = 0, definition = 0;
    double at = 0;
};

struct LiveRow {
    world::SObject o;
    uint32_t tag = 0;                    
    float dist = -1;                     
    bool mine = false;
};

struct AutoStatus {
    bool running = false;
    spawn::wtl::Params params;
    spawn::wtl::State state;
    bool havePoll = false;
    std::set<uint32_t> statesSeen;       
    uint32_t transitionFramesSeen = 0;   
    std::string error;
};

struct State {
    std::mutex m;
     
    spawn::Placement place;
    bool placeLoaded = false;
     
    std::vector<SpawnedRow> spawned;
    std::string lastSpawn;               
    bool lastSpawnOk = true;
    std::string progress;                
     
    std::set<uint32_t> livePages;        
    double pagesAt = 0;
    uint32_t registeredPages = 0;
    std::set<uint32_t> resident;         
    std::set<uint32_t> scannedPages;     
    std::set<uint32_t> checkedOut;       
    double entriesAt = 0;
    size_t scanDone = 0, scanTotal = 0;
    std::atomic<bool> scanning{false};
    std::map<uint32_t, TagCheck> checks;    
    uint64_t residencyRev = 0;           
     
    std::vector<LiveRow> objects;
    world::PoolHeader pool;
    double objectsAt = 0;
    std::string objectsError;
    std::optional<Vec3> player;          
    uint32_t playerHandle = 0;
    std::map<uint32_t, uint32_t> tagCache;    
    std::set<uint32_t> tagTried;         
     
    std::optional<spawn::town::State> town;
    std::string townError;
    std::vector<spawn::town::Edit> townEdits;    
     
    std::string wtlResult;
    bool wtlOk = true;
    AutoStatus autoLoad;
    std::string rscResult;
    bool rscOk = true;
     
    spawn::wtl::Params wtlUi;
    uint32_t rscEvent = 4, rscCategory = 1;
    uint32_t rscTag = 0;                 
    bool rscRaw = false;
     
    uint32_t selected = 0;
};
State& st();

double now();                            
bool demoMode();                         

 
spawn::Placement placement();            
void setPlacement(const spawn::Placement& p);    

 
uint32_t selectedTag();
void select(uint32_t tag);
bool favourite(uint32_t tag);
void toggleFavourite(uint32_t tag);
std::vector<uint32_t> favourites();
std::vector<uint32_t> recents();

 
std::string knownName(uint32_t tag);     
std::string unnamedLabel(uint32_t tag);  
std::string displayName(uint32_t tag);   
std::string shortName(uint32_t tag);     
std::string location(uint32_t tag);      
int catalogType(uint32_t tag);           
std::string vec3Text(const Vec3& v, int decimals = 1);

 
void requireReaders(Ctx& c);             
 
void runSpawn(Ctx& c, uint32_t tag, const spawn::Placement& place, const std::string& label);
void spawnSelected(Ctx& c, std::optional<spawn::Mode> mode = std::nullopt);
void runDespawn(Ctx& c, const std::vector<uint32_t>& handles);
void despawnLast(Ctx& c);
void despawnMine(Ctx& c);
void checkSpawnedAlive(Ctx& c);
void refreshPages(Ctx& c);               
void scanEntries(Ctx& c);                
TagCheck checkTag(Ctx& c, uint32_t tag);    
void refreshObjects(Ctx& c, bool resolveTags = true);
spawn::MoveResult moveObject(Ctx& c, uint32_t handle, const Vec3& to);
Vec3 readPlayerPosition(Ctx& c, spawn::PlayerSource source);    
std::optional<spawn::CameraPose> readCamera(Ctx& c, float screenX = 0.5f, float screenY = 0.5f);
 
void refreshTown(Ctx& c);
void requestTown(Ctx& c, bool town);
void restoreTown(Ctx& c, size_t index);   
void restoreAllTown(Ctx& c);             
 
void loadTagNow(Ctx& c, const spawn::wtl::Params& p);
void armAutoLoad(Ctx& c, const spawn::wtl::Params& p);
void disarmAutoLoad(Ctx& c);
void runRsc(Ctx& c, uint32_t event, uint32_t tag, bool raw, uint32_t category);

 
void seedDemo();
 
void resetSession();

}   
