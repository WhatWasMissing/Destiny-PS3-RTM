#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "game/world_mem.h"

namespace brps3 {
class Runtime;
}

namespace brps3::lab {

inline constexpr uint32_t kSendNow = 0x5ADB4C, kSendNowWord = 0x7C0802A6;
inline constexpr uint32_t kItemProducer = 0xC3C370, kItemSubmitSite = 0xC3C408, kItemSubmitWord = 0x4B62F0FD;
inline constexpr uint32_t kEmitInvestment = 0x26B504;
inline constexpr uint32_t kRootItemAction = 0x1A93478, kRootItemAcquired = 0x1A9347C;
inline constexpr uint32_t kRootAltGather = 0x1A207A0, kRootGather = 0x1A207A4;
inline constexpr uint32_t kLootEventHashBlock = 0x1A2074C, kLootEventHashBlockSize = 0xB4;
inline constexpr uint32_t kSchemaGlobalItem = 0x18DF3D8, kSchemaItem = 0x80800E33;
inline constexpr uint32_t kXboxSchemaTableBegin = 0x8344EBC8, kXboxSchemaTableEnd = 0x8344EC14;    
inline uint32_t ps3SchemaGlobal(uint32_t xbox) { return xbox - 0x8344EBD4u + 0x18DF3BCu; }

using ObserveFingerprints = std::vector<std::pair<uint32_t, const char*>>;
extern const ObserveFingerprints kDispatchFingerprints;
extern const ObserveFingerprints kItemRouteFingerprints;

 
inline constexpr uint32_t kRingSlots = 32, kRingSlotSize = 0x80, kRingHeader = 0x100;
inline constexpr uint32_t kRingSize = kRingHeader + kRingSlots * kRingSlotSize;
struct RingHeader {
    uint32_t enabled = 0, lock = 0, head = 0, tail = 0, seen = 0, full = 0, busy = 0;
};
RingHeader parseRingHeader(const world::Bytes& block);    
std::vector<uint8_t> ringInit();                           
 
std::vector<world::Bytes> ringSlots(const world::Bytes& block, uint32_t tail, uint32_t* newTail = nullptr);

 
struct DispatchEvent {
    uint32_t seq = 0, count = 0, list = 0, caller = 0;
    uint8_t guard = 0;
    std::vector<std::array<uint32_t, 3>> rows;    
    uint32_t root() const { return rows.empty() ? 0 : rows.front()[0]; }
};
DispatchEvent parseDispatchSlot(const world::Bytes& slot);
std::vector<uint8_t> dispatchStub(uint32_t base, uint32_t ctrl, uint32_t original);

 
struct ItemEvent {
    uint32_t seq = 0, root = 0, schema = 0, a = 0, b = 0, c = 0, d = 0;
    std::array<uint8_t, 0x20> payload{};
    std::array<uint32_t, 4> wrapper{};
    uint32_t rowPtr = 0, payloadPtr = 0;
    uint16_t item() const { return uint16_t(payload[0x18] << 8 | payload[0x19]); }
};
ItemEvent parseItemSlot(const world::Bytes& slot);
std::vector<uint8_t> itemStub(uint32_t base, uint32_t ctrl, uint32_t site);

 
struct Observer {
    uint32_t site = 0, ctrl = 0, tail = 0;
    bool active() const { return ctrl != 0; }
};
 
Observer startDispatchObserver(Runtime& rt);
Observer startItemObserver(Runtime& rt);
 
 
void stopObserver(Runtime& rt, Observer& o, std::string* error = nullptr);
 
std::vector<world::Bytes> pollObserver(Runtime& rt, Observer& o, RingHeader& header);

 
struct RootCheck {
    std::string name;
    uint32_t addr = 0, expected = 0, actual = 0;
    bool ok() const { return actual == expected; }
};
std::vector<RootCheck> checkItemRoots(world::Reader& mem);    

struct HashBlockRow {
    uint32_t offset = 0, addr = 0, hash = 0;
};
std::vector<HashBlockRow> readLootEventHashBlock(world::Reader& mem);

struct SchemaRow {
    uint32_t xbox = 0, ps3 = 0, schema = 0;    
    uint32_t object = 0, actual = 0;           
    std::vector<uint32_t> loot_events;           
    bool inTable = false;                      
    bool read = false;
    bool ok() const { return read && actual == schema; }
};
 
std::vector<SchemaRow> schemaRows();
 
void readSchemaRows(world::Reader& mem, std::vector<SchemaRow>& rows, const std::function<bool()>& cancelled = {});

}   
