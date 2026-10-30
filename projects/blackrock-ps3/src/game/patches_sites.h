#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core/runtime.h"

namespace brps3::patches {

using Fingerprints = std::vector<std::pair<uint32_t, const char*>>;

struct Word {
    uint32_t addr = 0, original = 0, patched = 0;
};

struct Unit {
    const char* name;                    
    const char* label;                   
    std::vector<Word> words;             
    const char* sharedWith = "";         
};

 
 
enum class Feature : int {
    StopBullets,
    InstantRespawn,
    FastRespawn,
    JoiningAllies,
    SparrowEnergy,
    ImmuneBypass,
    Fov,
    ThirdPerson,
    BulletWalls,
    InstantDismantle,
    ViewmodelFov,
    WorldTransition,
    EntitleAccess,                       
    EntitleUnlock,                       
    EntitleCounter,                      
    EntitleGate,                         
    Deafen,
    SparrowAnywhere,
    HotloadSuppress,
    Count
};

struct FeatureInfo {
    Feature feature;
    const char* key;                     
    const char* label;                   
    std::vector<const char*> units;      
    Fingerprints code;                   
};

const std::vector<Unit>& units();
const Unit* findUnit(const std::string& name);
const FeatureInfo& info(Feature f);
const std::vector<FeatureInfo>& features();

 
std::vector<Runtime::Word> runtimeWords(const Unit& u);
 
 
Fingerprints fingerprints(Feature f);

 
enum class SiteState { Original, Patched, Foreign };
SiteState classify(const Word& w, uint32_t current);
const char* siteStateName(SiteState s);    

 
 
 
 
inline constexpr uint32_t kDismantleWindow = 0x7CBAD8;
extern const char* const kDismantleWindowHex;
 
 
bool dismantleWindowOk(const std::vector<uint8_t>& bytes, std::string* why = nullptr);

 
 
 
 
inline constexpr uint32_t kCounterLeaf = 0xC5C014;
inline constexpr uint32_t kCounterLeafOriginal = 0x7CA50734;    
std::vector<uint8_t> counterLeafStub(uint32_t base);

 
namespace addr {
inline constexpr uint32_t FOV_OVERRIDE = 0x189A0C8;             
inline constexpr uint32_t GAME_TIME_PTR = 0x1AB6714;            
inline constexpr uint32_t GAME_SPEED_OFFSET = 0x4;
inline constexpr uint32_t GAME_SPEED_GETTER = 0x2B9EFC;         
inline constexpr uint32_t NO_INACTIVITY_FLAG = 0x1B6AF80;       
inline constexpr uint32_t SIGNIN_LISTENER = 0x1B6AF78;          
inline constexpr uint32_t SIGNIN_LISTENER_VTABLE = 0x13FB548;   
inline constexpr uint32_t VIEWMODEL_HANDLE = 0x1ACDB08;         
inline constexpr uint32_t VIEWMODEL_DEFAULT = 0x1ACD908;        
inline constexpr uint32_t VIEWMODEL_OFFSET = 0x10;              
inline constexpr uint32_t ENTITLE_ROOT_PTR = 0x1AB6458;         
inline constexpr uint32_t ENTITLE_VALID_OFFSET = 0x32C;
inline constexpr uint32_t ENTITLE_PROVIDER = 0x1AB6250;         
inline constexpr uint32_t ENTITLE_VTABLE = 0x1411170;
inline constexpr uint32_t ENTITLE_INNER_GET = 0x2AFA6C;         
inline constexpr uint32_t ENTITLE_OVERRIDE_OFFSET = 0x6B8C;     
inline constexpr uint32_t FLYIN_SUPPRESS_PTR = 0x1AD6050;       
inline constexpr uint32_t ENGINE_CLOCK_LO = 0x1A1D67C;          
inline constexpr uint32_t RESPAWN = 0x3A86C0;                   
inline constexpr uint32_t LOCAL_PLAYER_HANDLE = 0x733454;       
inline constexpr uint32_t LOCAL_PLAYER_HANDLE_SLOT = 0x1B294F0;    
inline constexpr uint32_t LOCAL_PLAYER_HANDLE_OFFSET = 0xCC;
inline constexpr uint32_t PLAYER_DESC_PTR = 0x1B29450;          
}   

 
 
inline const Fingerprints kRespawnCall = {{addr::RESPAWN, "7C0802A6489F0355F821FF21F80100F0"}};
inline const Fingerprints kLocalPlayerHandle = {{addr::LOCAL_PLAYER_HANDLE, "2C8300012C0300004C4023423880FFFF"}};
inline const Fingerprints kGameSpeed = {{addr::GAME_SPEED_GETTER, "3C6001AB80636714C02300044E800020"}};
inline const Fingerprints kEntitleData = {
    {addr::ENTITLE_INNER_GET, "7C0802A648AE8FB5F821FF81F8010090"},    
    {0xC35138, "80630004806302DC"},                                     
    {0x36EA08, "88636B8C"},                                             
};

}   
