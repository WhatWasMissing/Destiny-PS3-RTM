#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace brps3 {
class Runtime;
}

namespace brps3::cmd {

using Fingerprints = std::vector<std::pair<uint32_t, const char*>>;

inline constexpr const char* kCodeName = "cmd.v1";       
inline constexpr const char* kTaskName = "cmd.v1";       
inline constexpr uint32_t kMagic = 0x42524331;            
inline constexpr uint32_t kProgramMax = 0xC00;            
inline constexpr uint32_t kSlots = 8;
inline constexpr uint32_t kSpawnBatch = 8;                
inline constexpr uint32_t kMaxSpawns = 32, kMaxLootEventCount = 10000, kMaxFields = 16;

 
inline constexpr uint32_t kOffEnabled = 0x04, kOffBeat = 0x08;
inline constexpr uint32_t kCHalf = 0x10, kCOne = 0x14, kCEps3 = 0x18, kCEps2 = 0x1C, kCEps6 = 0x20, kCMax = 0x24, kCZero = 0x28,
                          kCBig = 0x2C;
inline constexpr uint32_t kOffTemplate = 0x40, kOffDesc = 0xC0, kOffOut = 0x140;
inline constexpr uint32_t kSlotsOffset = 0x150, kSlotSize = 0x370;
inline constexpr uint32_t kBlockSize = kSlotsOffset + kSlots * kSlotSize;

 
inline constexpr uint32_t kState = 0x00, kOp = 0x04, kSeq = 0x08, kResult = 0x0C, kCancel = 0x10, kDone = 0x14, kOk = 0x18,
                          kFrames = 0x1C, kStarted = 0x20, kType = 0x24, kValue = 0x28;
inline constexpr uint32_t kCentre = 0x30, kFwd = 0x40, kRight = 0x50, kEye = 0x60, kEyeOk = 0x6C, kAim = 0x70, kAimOk = 0x7C,
                          kPlayer = 0x80, kPlayerOk = 0x8C;
inline constexpr uint32_t kArgs = 0x90, kArgsSize = 0xA0;
inline constexpr uint32_t kStatus = 0x130, kResults = 0x170, kResultSize = 0x10;
inline constexpr uint32_t kHeaderRead = 0x30;             
 
inline constexpr uint32_t kATag = 0x90, kAMode = 0x94, kAFacing = 0x98, kACount = 0x9C, kASource = 0xA0, kADistance = 0xA4,
                          kASpacing = 0xA8, kAScale = 0xAC, kAOffset = 0xB0, kACoords = 0xBC;
 
inline constexpr uint32_t kAHash = 0x90, kASchema = 0x94, kAIType = 0x98, kASize = 0x9C, kAICount = 0xA0, kANFields = 0xA4,
                          kAFields = 0xA8, kFieldSize = 8;
inline constexpr uint32_t kPayloadOffset = 0x100;         
inline constexpr uint32_t kMaxPayload = 0x340;

enum State : uint32_t { kFree = 0, kQueued = 1, kRunning = 2, kStateDone = 3, kStateFailed = 4 };
enum Op : uint32_t { kOpSpawn = 1, kOpLootEvent = 2 };

 
enum Result : uint32_t {
    kOkResult = 0,
    kCancelled = 1,
    kBadArgs = 2,
    kNotEntityTag = 3,       
    kNoPool = 4,             
    kNoGlobals = 5,          
    kBusy = 6,               
    kNoTagTable = 7,         
    kNotLoaded = 8,          
    kNotEntityClass = 9,     
    kBodyUnmapped = 10,      
    kEmptyDefinition = 11,
    kGarbageProp = 12,
    kTypeMismatch = 13,      
    kNoCamera = 14,
    kNoPlayer = 15,
    kBadPoint = 16,
};
enum NotLoaded : uint32_t { kNlBeyond = 1, kNlUnregistered = 2, kNlUnmapped = 3, kNlStride = 4, kNlIndex = 5, kNlElement = 6 };
 
enum SpawnStatus : uint32_t { kSpawnNotRun = 0, kSpawnOk = 1, kSpawnNone = 2, kSpawnNotLive = 3, kSpawnWrongType = 4 };

 
 
const Fingerprints& fingerprints();

 
std::vector<uint8_t> handlerProgram(uint32_t base);       
uint32_t programAllocSize();                              
 
std::vector<uint8_t> encodeBlock(const std::array<uint32_t, 32>& descTemplate);

struct SpawnArgs {
    uint32_t tag = 0;
    int expectedType = -1;               
    uint32_t mode = 0;                   
    uint32_t facing = 0;                 
    uint32_t count = 1;                  
    uint32_t source = 0;                 
    float distance = 6, spacing = 2, scale = 1;
    float offset[3] = {0, 0, 0}, coords[3] = {0, 0, 0};
};
struct Field {
    uint32_t offset = 0;
    uint32_t width = 4;                  
    uint32_t value = 0;
};
struct LootEventArgs {
    uint32_t hash = 0, schema = 0, type = 0, size = 0;
    uint32_t count = 1;                  
    std::vector<Field> fields;           
};
 
 
std::vector<uint8_t> encodeSpawn(const SpawnArgs& a, uint32_t seq);
std::vector<uint8_t> encodeLootEvent(const LootEventArgs& a, uint32_t seq);

struct SlotHeader {
    uint32_t state = 0, op = 0, seq = 0, result = 0, cancel = 0, done = 0, ok = 0, frames = 0, started = 0, type = 0, value = 0;
    uint32_t code() const { return result & 0xFF; }
    uint32_t sub() const { return (result >> 8) & 0xFF; }
    bool finished() const { return state == kStateDone || state == kStateFailed; }
};
SlotHeader parseHeader(const std::vector<uint8_t>& b);    

struct SpawnOut {
    uint32_t status = 0;                 
    uint8_t liveType = 0;                
    uint32_t handle = 0;
    float x = 0, y = 0, z = 0;
};
 
std::vector<SpawnOut> parseSpawnResults(const std::vector<uint8_t>& b, uint32_t n);
const char* resultName(uint32_t code);   

 
struct Outcome {
    SlotHeader header;                   
    std::vector<uint8_t> tail;           
    bool panicked = false;               
};

class Handler {
public:
     
     
     
    void ensure(Runtime& rt);
     
     
     
     
     
    Outcome run(Runtime& rt, const std::vector<uint8_t>& body, const std::function<bool()>& cancelled,
                const std::function<void(uint32_t)>& progress, bool readResults = false, int stallMs = 5000, int pollMs = 30);
     
    void shutdown(Runtime& rt);
    uint32_t block() const { return block_; }
    uint32_t code() const { return code_; }
    uint32_t slotAddr(uint32_t i) const { return block_ + kSlotsOffset + i * kSlotSize; }
    uint32_t nextSeq();

private:
    int takeSlot(Runtime& rt);
    std::mutex m_;
    bool verified_ = false;
    uint32_t block_ = 0, code_ = 0, seq_ = 0;
    uint32_t generation_ = 0;                 
    std::array<bool, kSlots> busy_{};         
    std::array<bool, kSlots> abandoned_{};    
};

}   
