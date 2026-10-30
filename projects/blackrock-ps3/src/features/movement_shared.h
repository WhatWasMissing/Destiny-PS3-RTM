#pragma once
 
 
 
 
 
 
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "app/registry.h"
#include "app/settings.h"
#include "game/movement_pad.h"
#include "game/movement_scale.h"
#include "game/movement_teleport.h"
#include "game/movement_tick.h"

namespace brps3::mvui {

inline constexpr const char* kTab = "Movement";
inline constexpr const char* kFly = "Fly";
inline constexpr const char* kOpk = "Physics (OPK)";
inline constexpr const char* kTeleport = "Teleport";
inline constexpr const char* kScale = "Scale";
inline constexpr const char* kBinds = "Controller binds";
inline constexpr const char* kChecks = "Movement";                
inline constexpr const char* kLoop = "movement.status";           

namespace key {
inline constexpr const char* FlySpeed = "movement.fly.speed";
inline constexpr const char* FlyPrecision = "movement.fly.precision";
inline constexpr const char* FlyFactor = "movement.fly.precision_factor";
inline constexpr const char* OpkDist = "movement.opk.distance";
inline constexpr const char* Slots = "movement.tp.slots";
inline constexpr const char* History = "movement.tp.history";
inline constexpr const char* Quick = "movement.tp.quick";
inline constexpr const char* Route = "movement.tp.route";          
inline constexpr const char* Blink = "movement.tp.blink";
inline constexpr const char* Binds = "movement.binds";
inline constexpr const char* BindsSeeded = "movement.binds.seeded";
}   

 
struct Session {
    movement::TickSession tick;
    movement::ScaleJournal scale;
    std::mutex m;                        
    bool disconnectHook = false;
    movement::RingReader ring;
    movement::BindMatcher matcher;
    std::chrono::steady_clock::time_point lastRead{};
    int failures = 0;
};
Session& session(Ctx& c);

 
movement::Config paramsFromSettings(Settings& s);
 
 
void configure(Ctx& c, const std::function<void(movement::Config&)>& edit);
 
void statusTick(Ctx& c, bool force);
 
void resetUi();
 
 
movement::WriteFn writer(Ctx& c, bool verify = true);

 
struct Snapshot {
    bool installed = false;
    movement::Control c;                 
    double at = 0;                       
    std::string note;                    
    std::optional<movement::BodyChain> chain;
     
    bool havePos = false;
    world::Vec3 pos, forward;
    uint32_t handle = 0;
    std::string posNote;
    double posAt = 0;
    std::string lastTeleport;            
    bool lastTeleportOk = true;
     
    std::vector<movement::ScaleEdit> scaled;
     
    std::optional<movement::IslandScan> islands;
    double islandsAt = 0;
};
Snapshot snapshot();
void publish(const std::function<void(Snapshot&)>& fn);
double now();                            

 
std::vector<movement::Bind> binds();
void setBinds(std::vector<movement::Bind> b);           
void loadBinds();
bool bindsArmed();
void armBinds(bool on);                  
 
void queueActivation(const std::string& id);
std::vector<std::string> takeActivations();

struct Capture {
    bool active = false;                 
    movement::ChordCapture::State state = movement::ChordCapture::State::Idle;
    uint32_t partial = 0, chord = 0;
    std::string item;                    
    double started = 0;
};
Capture capture();
void startCapture(const std::string& item);            
void stopCapture();                                    
bool captureWanted();                    
bool liveWanted();                       
void setLiveWanted(const std::string& who, bool on);   
 
 
 
bool padViewOn();
void togglePadView(const std::string& who);
 
 
bool pump();

 
bool demo();                             
void seedDemo();                         

}   
