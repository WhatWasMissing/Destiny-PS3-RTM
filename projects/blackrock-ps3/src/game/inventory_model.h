#pragma once
 
 
 
#include <cstdint>
#include <string>
#include <vector>

#include "game/inventory_data.h"
#include "game/inventory_snapshot.h"

namespace brps3::inventory {

struct RollPerk {
    int node = 0;
    std::string name;
    bool active = false;                 
};
struct RollView {
    bool readable = false;
    std::string why;                     
    uint16_t grid = 0xFFFF;
    std::vector<RollPerk> perks;         
    std::string summary;                 
    bool has(const std::string& perk) const;
};
 
 
RollView describeRoll(const Item& it, const Catalog& cat);

 
 
 
 
 
class Query {
public:
    explicit Query(const std::string& text);
    bool empty() const { return terms_.empty(); }
    bool matches(const Item& it, const Catalog& cat, const RollView* roll = nullptr) const;
    bool wantsPerks() const { return perks_; }
private:
    struct Term {
        std::string key, value;
        bool negated = false;
    };
    std::vector<Term> terms_;
    bool perks_ = false;
};

 
std::string itemName(const Item& it, const Catalog& cat);
std::string locationName(const Item& it);    
int rarityOf(const Item& it, const Catalog& cat);    

}   
