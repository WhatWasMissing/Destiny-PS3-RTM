#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <array>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "game/loot_addr.h"
#include "game/loot_console.h"

namespace brps3::loot::pickup {

 
enum class ItemClass : uint8_t { Weapon = 0, Armor, Ghost, Engram, Other, Count };
const char* className(ItemClass c);                    
const char* classLabel(ItemClass c);                   
std::optional<ItemClass> parseClass(const std::string& s);
const char* tierName(uint8_t tier);                    
std::string bucketName(uint8_t bucket);                
const std::vector<uint8_t>& knownBuckets();            

struct Definition {
    uint16_t index = 0;
    uint32_t hash = 0;
    uint8_t tier = 0, bucket = 0;
    ItemClass cls = ItemClass::Other;
    std::string slot, type, name;
    std::string label() const;                         
};

class DefinitionCatalog {
public:
    static DefinitionCatalog parse(const std::string& tsv);    
    const std::vector<Definition>& all() const { return rows_; }
    const Definition* at(uint32_t index) const { return index < rows_.size() ? &rows_[index] : nullptr; }
    const Definition* byHash(uint32_t hash) const;
    size_t size() const { return rows_.size(); }
     
    std::vector<const Definition*> search(const std::string& query, size_t limit = 0) const;
    std::string error;
private:
    std::vector<Definition> rows_;
    std::vector<std::string> hay_;
};
const DefinitionCatalog& definitions();                

 
inline constexpr uint32_t kBitmapBytes = (ITEM_DEFINITIONS + 7) / 8;    

struct Rule {
    bool keep = true;
    uint32_t classes = 0;                
    uint32_t tiers = 0;                  
    std::vector<uint8_t> buckets;        
    std::string name;                    
    std::vector<uint32_t> items;         
    bool matches(const Definition& d) const;
    std::string describe() const;        
    static uint32_t tierBit(uint8_t tier) { return tier == 255 ? 1u << 8 : tier < 8 ? 1u << tier : 0u; }
    bool operator==(const Rule&) const = default;
};

struct Preset {
    std::string name, note;
    bool builtIn = false;
    bool keepOthers = false;             
    std::vector<Rule> rules;
    bool operator==(const Preset&) const = default;
};

using Bitmap = std::array<uint8_t, kBitmapBytes>;
Bitmap compile(const Preset& p, const DefinitionCatalog& cat);    
inline bool allowed(const Bitmap& b, uint32_t index) { return index < ITEM_DEFINITIONS && (b[index / 8] >> (index % 8)) & 1; }
uint32_t countAllowed(const Bitmap& b);
 
int decidingRule(const Preset& p, const Definition& d);

 
 
 
 
 
 
std::vector<Preset> parsePresets(const std::string& text, bool builtIn = false);    
std::string serialisePresets(const std::vector<Preset>& presets);

 
class PresetStore {
public:
    static PresetStore& get();
    std::vector<Preset> builtIns() const;
    std::vector<Preset> user() const;
    std::vector<Preset> all() const;                  
    std::optional<Preset> find(const std::string& name) const;
     
    void setUser(std::vector<Preset> presets);
    std::string error() const;
    std::string uniqueName(const std::string& base) const;
    uint64_t revision() const;                        
    void reload(const std::filesystem::path& builtInFile, const std::filesystem::path& userFile);    
private:
    PresetStore() = default;
    const Preset* findLocked(const std::string& name) const;
    void ensureLoaded() const;
    mutable std::mutex m_;
    mutable bool loaded_ = false;
    mutable std::vector<Preset> builtIns_, user_;
    mutable std::filesystem::path userPath_;
    mutable std::string error_;
    mutable uint64_t revision_ = 0;
    void reloadLocked(const std::filesystem::path& builtInFile, const std::filesystem::path& userFile) const;
};

 
enum class Mode : uint32_t { Allow = 0, Bitmap = 1, RejectAll = 2 };
 
inline constexpr uint32_t kDataMode = 0x0, kDataMasks = 0x4, kDataBitmap = 0x10, kDataSize = 0x350;
inline constexpr uint32_t kStubSize = 0x60;
std::vector<uint8_t> stubProgram(uint32_t base, uint32_t data);    
std::vector<uint8_t> encodeData(Mode mode, const Bitmap& bitmap);
uint32_t hookWord(uint32_t stub);                                    

struct TableCheck {
    uint32_t provider = 0, root = 0, tableTag = 0, table = 0, count = 0;
    std::string sha256;
    std::vector<uint32_t> hashes;        
};
 
 
TableCheck verifyItemTable(world::Reader& mem, Resolver& res);

}   
