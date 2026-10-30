#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/error.h"
#include "game/world_mem.h"

namespace brps3::inventory {

enum class Owner : uint8_t { Character, Account };

struct Row {
    uint16_t def = 0xFFFF;
    uint64_t soid = 0;
    uint32_t quantity = 0;
    std::array<uint8_t, 6> raw{};        
    bool empty() const { return def == 0xFFFF; }
    bool operator==(const Row&) const = default;
};

struct Item {
    Owner owner = Owner::Character;
    uint16_t slot = 0;
    uint16_t def = 0;
    uint32_t hash = 0;
    uint64_t soid = 0;                   
    uint32_t quantity = 0;
    bool equipped = false;
    std::array<uint8_t, 6> raw{};
    int bucket = -1;                     
    bool hasDetail = false;              
    uint32_t detailAddr = 0;
    std::array<uint8_t, 168> detail{};
    bool vault() const { return owner == Owner::Account && (bucket == 22 || bucket == 23 || bucket == 24); }
    bool instance() const { return soid != 0; }
};

struct PostmasterEntry {
    uint16_t def = 0;
    uint32_t amount = 0;
    uint8_t category = 0;
};

struct Snapshot {
    uint32_t manager = 0;
    uint32_t accountAddr = 0, characterAddr = 0;         
    uint32_t descriptorIndex = 0, descriptorAddr = 0;
    std::array<uint8_t, 64> descriptor{};
    uint64_t accountSoid = 0, characterSoid = 0;
    uint32_t familyVersion = 0;
    uint32_t mode = 0;                                   
    uint32_t globals = 0, defsTag = 0, defsBase = 0, defCount = 0;
    uint32_t bucketsTag = 0, indexTag = 0;               
    std::vector<Row> character, account;                 
    std::array<uint64_t, 20> equipped{};
    std::vector<Item> items;                             
    uint32_t postmasterCount = 0;
    std::vector<PostmasterEntry> postmaster;             
    uint8_t exoticFlag = 0;                              
    bool talents = false;                                
    uint32_t detailAddr = 0, detailCount = 0;
    size_t detailMatched = 0;
    uint64_t reads = 0;                                  
    double ms = 0;                                       
    bool sessionOnly = false;

    const Item* find(uint64_t soid) const;               
    const Item* at(Owner owner, uint16_t slot) const;
    bool isEquipped(uint64_t soid) const;
    int equippedIndex(uint64_t soid) const;              
    uint32_t profileOffset(Owner o) const { return (o == Owner::Account ? accountAddr : characterAddr) - manager; }
};

enum class FailKind { Invalid, Transient, NotSignedIn };
struct SnapshotError : Error {
    FailKind kind;
    SnapshotError(const std::string& m, FailKind k) : Error(m), kind(k) {}
};

 
struct DefinitionCache {
    uint64_t account = 0;
    uint32_t iface = 0, vtable = 0, mode = 0, globals = 0, defsTag = 0, defsBase = 0, header = 0, count = 0, version = 0;
    uint32_t bucketsTag = 0, indexTag = 0;
    std::vector<uint32_t> hashes;
    bool valid() const { return !hashes.empty(); }
    void clear() { *this = DefinitionCache(); }
};

struct ReadOptions {
    uint64_t expectedCharacter = 0;      
    bool talents = false;                
    bool sessionOnly = false;            
};

 
Snapshot readSnapshot(world::Reader& mem, DefinitionCache& cache, const ReadOptions& o = {});
 
Snapshot readSnapshotRetry(world::Reader& mem, DefinitionCache& cache, const ReadOptions& o, int attempts,
                           const std::function<bool()>& cancelled = {}, int delayMs = 250);

 
struct DescriptorState {
    uint8_t family = 0, state = 0;
    uint64_t account = 0;
    uint32_t version = 0;
    bool sameOwner(const Snapshot& s) const { return family == 4 && account == s.accountSoid; }
};
DescriptorState readDescriptor(world::Reader& mem, const Snapshot& s);

 
std::vector<uint32_t> readDefinitionHashes(world::Reader& mem, DefinitionCache& cache, uint32_t* mode = nullptr);

 
 
struct InvestmentInterface {
    uint32_t iface = 0, vtable = 0, mode = 0, tag = 0;
};
InvestmentInterface readInvestmentInterface(world::Reader& mem);
uint32_t resolveInvestmentTag(world::Reader& mem, uint32_t tag, uint32_t mode);

 
std::vector<Row> parseRows(const world::Bytes& profile, uint32_t holderOffset, uint32_t count);
 
 
class Observation final : public world::Reader {
public:
    explicit Observation(world::Reader& inner) : inner_(inner) {}
    std::vector<uint8_t> read(uint32_t addr, uint32_t size) override;
    void verify();
    uint64_t innerReads() const { return inner_.reads(); }
private:
    world::Reader& inner_;
    std::vector<std::pair<std::pair<uint32_t, uint32_t>, std::vector<uint8_t>>> evidence_;
};

}   
