#pragma once
 
 
 
 
 
 
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace brps3::inventory {

 
struct Bucket {
    int key = 0;
    bool account = false;                
    int first = 0, count = 0;
    bool eviction = false;
    bool contains(int slot) const { return slot >= first && slot < first + count; }
};
 
std::string bucketName(int key);
 
int bucketGroup(int key);
inline bool isVaultBucket(int key) { return key == 22 || key == 23 || key == 24; }
inline bool isWeaponBucket(int key) { return key >= 2 && key <= 4; }
inline bool isArmorBucket(int key) { return key >= 7 && key <= 11; }

 
struct ItemDef {
    uint16_t def = 0;
    uint32_t hash = 0, tag = 0;
    uint8_t rarity = 0;                  
    uint8_t home = 255, fallback = 255;  
    bool perInstance = false;            
    bool compat = false;                 
    uint32_t maxStack = 0;               
    uint32_t uniq = 0;                   
    uint32_t related = 0;                
    std::string category;                
    uint16_t grid = 0xFFFF, pattern = 0xFFFF;
    uint8_t eslot = 255;                 
    std::vector<uint16_t> intrinsic;     
    std::string name, type;              
    std::string alias;                   
    std::string label() const;           
};

struct Perk {
    uint16_t ordinal = 0;
    bool enabled = false;                
    uint32_t displayHash = 0;
    std::string name, description;
    std::string label() const;           
};

 
 
struct RollNode {
    unsigned groups = 0, count = 0;
    int selector = -1;                   
    std::map<unsigned, std::string> variants;    
};

inline constexpr uint32_t kDefaultUniq = 0x811C9DC5;

class Catalog {
public:
     
    static Catalog parse(const std::string& items, const std::string& buckets, const std::string& routes, const std::string& perks,
                         const std::string& rolls, const std::string& tables = {});
    const ItemDef* item(uint32_t def) const { return def < items_.size() ? &items_[def] : nullptr; }
    const ItemDef* byHash(uint32_t hash) const;
    const std::vector<ItemDef>& items() const { return items_; }
    size_t size() const { return items_.size(); }
    const std::vector<Bucket>& buckets() const { return buckets_; }
    const Bucket* bucket(int key) const;
    const Bucket* bucketAt(bool account, int slot) const;
     
    std::optional<int> route(int sourceKey) const;
    const std::map<int, int>& routes() const { return routes_; }
    const Perk* perk(uint32_t ordinal) const { return ordinal < perks_.size() ? &perks_[ordinal] : nullptr; }
    const std::vector<Perk>& perks() const { return perks_; }
    const std::map<unsigned, RollNode>* rollGrid(uint32_t grid) const;
    unsigned rollNodes(uint32_t grid) const;   
     
    const std::vector<uint8_t>* table(uint32_t xboxTag) const;
     
    const std::string& hay(uint32_t def) const { return hay_[def]; }
    std::string error;                   
private:
    std::vector<ItemDef> items_;
    std::vector<std::string> hay_;
    std::map<uint32_t, uint32_t> byHash_;
    std::vector<Bucket> buckets_;
    std::map<int, int> routes_;
    std::vector<Perk> perks_;
    std::map<unsigned, std::map<unsigned, RollNode>> rolls_;
    std::map<unsigned, unsigned> rollNodes_;
    std::map<uint32_t, std::vector<uint8_t>> tables_;
};
 
const Catalog& catalog();

 
std::vector<std::string> splitTabs(const std::string& line);
std::vector<std::string> splitChar(const std::string& text, char sep);
std::string lowerAscii(std::string s);
std::string hex16(uint64_t v);           

}   
