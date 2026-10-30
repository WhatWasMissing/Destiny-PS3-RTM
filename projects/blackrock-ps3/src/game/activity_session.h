#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <string>
#include <vector>

#include "game/world_mem.h"

namespace brps3::activity {

 
struct LobbyField {
    int index;                           
    const char* name;                    
    uint32_t offset;                     
};
extern const std::vector<LobbyField> kLobbyFields;    
const LobbyField* lobbyField(int index);

struct FieldValue {
    int index = 0;
    uint32_t addr = 0;                   
    bool has = false;                    
    std::vector<uint8_t> value;          
};
inline constexpr uint32_t kFieldValueBytes = 0x50;

struct SessionSlot {
    int slot = 0;
    bool ok = false;                     
    std::string why;
    uint32_t slotBase = 0, index = 0, sub = 0, gate = 0, goal = 0;
    std::vector<FieldValue> fields;      
    const FieldValue* field(int fieldIndex) const;
     
     
    bool active() const { return ok && gate >= 4 && gate <= 10; }
};
struct SessionRead {
    bool ok = false;
    std::string why;
    uint32_t root = 0;
    SessionSlot slots[2];
};
SessionRead readSessionFields(world::Reader& mem);           
 
SessionSlot parseSessionSlot(int slot, uint32_t slotBase, uint32_t subIndex, const world::Bytes& goalToGate);
inline constexpr uint32_t kGoalReadSize = 0x8468 - 0x6708 + 4;
 
std::string describeFieldValue(int index, const std::vector<uint8_t>& value);

 
enum class CaptureKind : int { JoinRefuse = 0, ActivityHost = 1, SessionDisband = 2, ActivityStatus = 3 };
inline constexpr int kCaptureKinds = 4;
struct CaptureSpec {
    CaptureKind kind;
    const char* id;                      
    const char* label;                   
    uint32_t site;                       
    int destReg;                         
    int words;                           
};
extern const CaptureSpec kCaptureSpecs[kCaptureKinds];
const CaptureSpec& captureSpec(CaptureKind k);

 
 
inline constexpr uint32_t kCaptureBlockSize = 0x40, kCaptureAreaSize = kCaptureBlockSize * kCaptureKinds;
inline constexpr uint32_t kCapLr = 0x0, kCapDest = 0x8, kCapCounter = 0xC, kCapR3 = 0x10, kCapWords = 0x14;
inline constexpr uint32_t kCaptureWrapSize = 0x20;       
inline constexpr uint32_t kCaptureAfterSize = 0x70;      
 
std::vector<uint8_t> captureWrap(uint32_t base, uint32_t site, uint32_t after, uint32_t captureBlock, int destReg);
 
std::vector<uint8_t> captureAfter(uint32_t base, uint32_t captureBlock, int words);
 
std::string captureAfterName(CaptureKind k);

struct Capture {
    uint64_t lr = 0;
    uint32_t dest = 0, counter = 0, r3 = 0;
    std::vector<uint32_t> words;
};
Capture parseCapture(const world::Bytes& area, CaptureKind k);    
std::string describeCapture(CaptureKind k, const Capture& c);     
 
struct JoinRefuse {
    uint64_t session = 0, peer = 0;
    uint32_t reason = 0;
    bool decoded = false;                
};
JoinRefuse joinRefuse(const Capture& c);

}   
