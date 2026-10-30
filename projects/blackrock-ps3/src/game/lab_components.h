#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "game/world_components.h"
#include "game/world_mem.h"
#include "game/world_tags.h"

namespace brps3::lab {

 
inline constexpr uint32_t kTypeAnchorPrimary = 0x18E92D0;
inline constexpr uint32_t kTypeAnchorSecondary = 0x18DF4F0;
inline constexpr uint32_t kTypeAnchorEffect = 0x18DF174;
inline constexpr uint32_t kEffectDescriptor = 0x1AAF520;    
inline constexpr uint32_t kIdentityManager = 0x1896D4C;
inline constexpr uint32_t kDispatchConsumer38 = 0x291AA0;
inline constexpr uint32_t kDispatchConsumer58 = 0x291714;
inline constexpr uint32_t kSpawnerPrimaryComponent = 0x255E4C;
inline constexpr uint32_t kSecondaryComponent = 0x206A8C;
inline constexpr uint32_t kComponentLookupByType = 0x147340;
inline constexpr uint32_t kComponentLookupHead = 0x147518;
inline constexpr uint32_t kOpdBegin = 0x1849508, kOpdEnd = 0x1889798, kToc = 0x1891798;

using Fingerprints = std::vector<std::pair<uint32_t, const char*>>;
 
extern const Fingerprints kComponentFingerprints;

 
 
 
uint32_t canonicalHandle(uint32_t identity, uint32_t mask, uint32_t nameSpace, uint32_t index);
struct PoolIdentity {
    uint32_t at = 0, identity = 0, canonical = 0;    
     
     
    bool matches(uint32_t handle) const { return world::validHandle(handle) && (canonical == handle || identity == handle); }
};
 
 
 
 
std::optional<PoolIdentity> poolIdentity(world::Reader& mem, const world::PoolHeader& p, uint32_t index, const world::Bytes* entries = nullptr);

 
 
 

 
 
 
class WordSource {
public:
    virtual ~WordSource() = default;
    virtual uint32_t word(uint32_t at) = 0;
    uint8_t byte(uint32_t at);
};
class BytesSource final : public WordSource {
public:
    explicit BytesSource(const std::vector<uint8_t>& b) : b_(b) {}
    uint32_t word(uint32_t at) override;
private:
    const std::vector<uint8_t>& b_;
};
 
class MemorySource final : public WordSource {
public:
    MemorySource(world::Reader& mem, uint32_t base, uint32_t extent) : mem_(mem), base_(base), extent_(extent) {}
    uint32_t word(uint32_t at) override;
    uint32_t base() const { return base_; }
    uint32_t extent() const { return extent_; }
    size_t chunks() const { return chunks_.size(); }
private:
    world::Reader& mem_;
    uint32_t base_, extent_;
    std::map<uint32_t, std::vector<uint8_t>> chunks_;
};

 
enum class Lookup { Malformed, Missing, Found };
const char* lookupName(Lookup r);
struct Descriptor {
    uint32_t index = 0;
    uint32_t words[7] = {};
    bool operator==(const Descriptor&) const = default;
};
bool span(uint32_t size, uint32_t at, uint32_t count, uint32_t stride);
 
bool relArray(WordSource& r, uint32_t size, uint32_t field, uint32_t count, uint32_t stride, uint32_t& at);
 
uint32_t lookupSlot(uint32_t seed, uint32_t key, uint32_t buckets);
Lookup lookupOffset(WordSource& r, uint32_t size, uint32_t definitionOffset, uint32_t key, uint32_t arenaSize, uint32_t& componentOffset,
                    Descriptor* selected = nullptr);

 
struct Definition {
    uint32_t tag = 0, body = 0, extent = 0;
    world::Resolved resource;             
    bool identityChecked = false;         
    uint32_t identityBody = 0;            
    std::string why;                      
    bool ok() const { return why.empty() && body != 0; }
};
 
 
Definition readDefinition(world::Reader& mem, const world::TagTable& t, uint32_t tag, uint32_t windowCap = 0x40000);
uint32_t windowExtent(uint32_t body, uint32_t cap);    

 
struct TypeKey {
    uint32_t anchor = 0, descriptor = 0, handle = 0, key = 0, body = 0;
    std::string why;                      
    bool ok() const { return why.empty(); }
};
 
TypeKey readTypeKey(world::Reader& mem, const world::TagTable& t, uint32_t anchor);
const char* anchorName(uint32_t anchor);    

 
struct ArenaStep {
    uint32_t handle = 0, index = 0, entry = 0, body = 0, size = 0;
    uint32_t words[6] = {};               
    uint32_t flags = 0, definition = 0, offset = 0;    
    bool identityOk = false;              
    uint32_t identity = 0, canonical = 0;    
    bool skipped = false;                 
    Lookup result = Lookup::Missing;
    uint32_t componentOffset = 0;         
    Descriptor descriptor;
    std::string why;                      
    bool operator==(const ArenaStep&) const = default;
};
 
 
 
enum class OwnerMode { Component, Effect };
struct OwnerLookup {
    Lookup result = Lookup::Malformed;
    std::string why;
    uint32_t owner = 0, object = 0, head = 0, key = 0;
    OwnerMode mode = OwnerMode::Component;
    std::vector<ArenaStep> arenas;        
    int found = -1;                       
     
    uint32_t component = 0;               
    uint32_t runtime = 0;                 
    uint32_t child = 0;                   
    std::optional<world::Accessor> childAccessor;    
    uint32_t childOwner = 0;              
    uint32_t childTarget = 0;             
    uint32_t childCanonical = 0;          
    bool childOk = false;                 
    std::string childWhy;                 
    int passes = 0;                       
};
 
 
 
 
OwnerLookup findOwnerComponent(world::Reader& mem, const world::TagTable& t, uint32_t ownerHandle, uint32_t key, bool headOnly = false,
                               bool walkAll = false, OwnerMode mode = OwnerMode::Component);

 
struct Dispatch {
    uint32_t typeTag = 0, slot = 0;       
    Definition definition;
    uint32_t relative = 0, table = 0;     
    uint32_t fn = 0, adjust = 0;          
    bool isVirtual() const { return (fn & 1) != 0; }
    uint32_t vtableOffset() const { return fn - 1; }
     
    bool opdAligned = false, opdInRange = false, tocOk = false, codeInText = false;
    uint32_t opd = 0, code = 0, toc = 0;
    uint32_t instructions[4] = {};
    std::string why;                      
    bool ok() const { return why.empty(); }
    uint32_t consumer() const { return slot == 0x58 ? kDispatchConsumer58 : kDispatchConsumer38; }
};
Dispatch readDispatch(world::Reader& mem, const world::TagTable& t, uint32_t typeTag, uint32_t slot);

}   
