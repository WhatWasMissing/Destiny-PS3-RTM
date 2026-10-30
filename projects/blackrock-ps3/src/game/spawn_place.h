#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "game/world_mem.h"

namespace brps3::spawn {

using world::Quat;
using world::Vec3;

 
inline constexpr uint32_t kViewMatrix = 0x1AE6F10;       
inline constexpr uint32_t kProjectionMatrix = 0x1AE6F50;   
inline constexpr uint32_t kCameraBytes = 0x80;           

struct CameraMatrices {
    std::array<float, 16> view{}, projection{};
};
struct CameraPose {
    Vec3 position, forward;              
};
CameraMatrices parseMatrices(const world::Bytes& b);    
 
bool cameraFromView(const float* view, CameraPose& out);
 
bool cameraFromMatrices(const CameraMatrices& m, float screenX, float screenY, CameraPose& out);
 
bool aimRotation(Vec3 forward, Quat& out);
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

 
enum class Mode : int { Crosshair = 0, Player = 1, Coordinates = 2 };
enum class Facing : int { TowardMe = 0, AlongAim = 1, World = 2 };    
enum class PlayerSource : int { Engine = 0, Element = 1 };             
const char* modeName(Mode m);            
const char* facingName(Facing f);
const char* playerSourceName(PlayerSource s);

struct Placement {
    Mode mode = Mode::Crosshair;
    float distance = 6.0f;               
    float screenX = 0.5f, screenY = 0.5f;    
    Vec3 offset;                         
    Vec3 coords;                         
    int count = 1;                       
    float spacing = 2.0f;                
    float scale = 1.0f;                  
    Facing facing = Facing::TowardMe;
    PlayerSource source = PlayerSource::Engine;
};
inline constexpr int kMaxCount = 32;

 
struct PlacementInputs {
    std::optional<CameraMatrices> camera;
    std::optional<Vec3> player;          
};

struct SpawnPoint {
    Vec3 position;
    Quat rotation;                       
};
 
 
std::vector<SpawnPoint> resolvePoints(const Placement& p, const PlacementInputs& in);
 
std::vector<std::pair<float, float>> gridOffsets(int count, float spacing);
 
std::string summary(const Placement& p);
 
Placement clamped(Placement p);

 
inline constexpr uint32_t kDescSize = 0x80;
inline constexpr uint32_t kDescTag = 0x00, kDescTagOffset = 0x04, kDescClass = 0x08, kDescRot = 0x10, kDescPos = 0x20,
                          kDescScale = 0x2C, kDescOwner = 0x34;
inline constexpr uint32_t kPlacementClass = 0x80800906, kTagRefOffset = 0x0C;
const std::array<uint32_t, kDescSize / 4>& descTemplate();
std::vector<uint8_t> buildDescriptor(uint32_t tag, const SpawnPoint& at, float scale);

}   
