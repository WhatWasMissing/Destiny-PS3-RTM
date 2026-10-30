#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace brps3::names {

 
inline constexpr uint32_t kFnvBasis = 2166136261u, kFnvPrime = 16777619u;
uint32_t fnv1(std::string_view s);       
uint32_t fnv1a(std::string_view s);      

 
struct Entity {
    uint32_t tag = 0;
    uint8_t type = 0;                    
    uint32_t size = 0;                   
    std::string package;                 
    std::string name;                    
    std::string confidence;              
    std::string aliases;                 
    bool hasName() const { return !name.empty(); }
    std::string location() const;        
    std::string activity() const;        
};

 
void packageParts(const std::string& package, std::string& location, std::string& activity);
 
std::string locationName(const std::string& token);

class EntityCatalog {
public:
    static EntityCatalog parse(const std::string& tsv);       
    const Entity* find(uint32_t tag) const;                   
    const std::vector<Entity>& all() const { return rows_; }  
    size_t size() const { return rows_.size(); }
    size_t namedCount() const { return named_; }
     
     
     
    std::vector<const Entity*> search(const std::string& query, int type = -1, size_t limit = 0) const;
    std::string error;                   
private:
    std::vector<Entity> rows_;
    std::vector<std::string> hay_;       
    size_t named_ = 0;
};
 
 
const EntityCatalog& entities();

 
struct Item {
    uint32_t hash = 0;                   
    uint32_t itemTag = 0, displayTag = 0;    
    uint8_t rarity = 0;                  
    std::string type, name;              
};
const char* rarityName(uint8_t rarity);  

class ItemCatalog {
public:
    static ItemCatalog parse(const std::string& tsv);
    const Item* find(uint32_t hash) const;
    const Item* findByTag(uint32_t itemTag) const;            
    const std::vector<Item>& all() const { return rows_; }    
    size_t size() const { return rows_.size(); }
     
    std::vector<const Item*> search(const std::string& query, size_t limit = 0) const;
    std::string error;
private:
    std::vector<Item> rows_;
    std::vector<std::string> hay_;
};
const ItemCatalog& items();              

 
struct Label {
    std::string name, notes;
    bool verified = false;               
};
class Labels {
public:
    static Labels& get();                
    std::optional<Label> find(uint32_t tag) const;
    void set(uint32_t tag, const Label& l);                   
    void remove(uint32_t tag);
    std::vector<std::pair<uint32_t, Label>> all() const;      
    size_t size() const;
    std::string error() const;           
     
    static std::vector<std::pair<uint32_t, Label>> parse(const std::string& tsv);
    static std::string serialise(const std::vector<std::pair<uint32_t, Label>>& rows);
    void reloadFrom(const std::filesystem::path& userPath, const std::filesystem::path& seedPath);
private:
    Labels() = default;
    void loadLocked();
    void saveLocked();
    mutable std::mutex m_;
    bool loaded_ = false;
    std::filesystem::path userPath_, seedPath_;
    std::string error_;
    std::vector<std::pair<uint32_t, Label>> rows_;
};

 
 
 
std::string tagLabel(uint32_t tag);
 
std::string tagName(uint32_t tag);
 
std::string itemLabel(uint32_t hash);

 
 
 
 
 
 
uint32_t toPs3Tag(uint32_t catalogTag);
uint32_t fromPs3Tag(uint32_t ps3Tag);
bool hasPs3Tag(uint32_t catalogTag);     
struct TranslationStatus {
    const char* assumption;              
    const char* portMap;                 
    const char* evidence;                
    std::vector<std::pair<uint32_t, uint32_t>> reportedShifts;    
};
const TranslationStatus& translationStatus();

 
 
struct TranslationReport {
    size_t live = 0;                     
    size_t inCatalog = 0;                
    size_t typeMatch = 0, typeMismatch = 0;
    std::vector<std::pair<uint32_t, std::pair<uint8_t, uint8_t>>> mismatches;    
    std::string verdict;                 
};
TranslationReport compareLiveTypes(const EntityCatalog& catalog, const std::vector<std::pair<uint32_t, uint8_t>>& live);

}   
