#pragma once
 
 
 
 
 
 
 
#include <cstdint>
#include <string>

#include "game/inventory_data.h"
#include "game/inventory_snapshot.h"

namespace brps3::inventory {

enum class Action { Deposit, Withdraw, Equip, Unequip, Dismantle, Use };
const char* actionName(Action a);        
const char* actionVerb(Action a);        

 
 
struct Route {
    int context = 26;
    Bucket source, destination;
    int home = 255;
    bool perInstance = false, compat = false;
};
Route resolveRoute(const Catalog& cat, const ItemDef& def, bool fromCharacter, int slot);    

struct Plan {
    Action action = Action::Deposit;
    Owner owner = Owner::Character;      
    uint16_t slot = 0;
    uint16_t def = 0;
    uint32_t hash = 0;
    uint64_t soid = 0;                   
    uint32_t sourceQuantity = 0;         
    uint32_t quantity = 1;               
    bool stack = false;
    int equippedIndex = -1;              
    Route route;                         
     
    uint32_t manager = 0, descriptorIndex = 0, familyVersion = 0;
    uint64_t account = 0, character = 0;
    std::string name;                    
    std::string summary() const;         
};

struct Eligibility {
    bool ok = false;
    std::string reason;                  
    uint32_t maxQuantity = 1;            
};

 
Eligibility eligibility(const Snapshot& s, const Item& item, Action a, const Catalog& cat);
 
Plan plan(const Snapshot& s, const Item& item, Action a, uint32_t quantity, const Catalog& cat);
 
 
void validate(const Snapshot& fresh, const Plan& p, const Catalog& cat);

struct Outcome {
    enum State { Confirmed, Partial, Pending, Failed } state = Pending;
    std::string text;
    uint32_t moved = 0;                  
};
 
 
Outcome reconcile(const Snapshot& before, const Snapshot& after, const Plan& p, const Catalog& cat);

 
int freeRows(const Snapshot& s, const Bucket& b);
int usedRows(const Snapshot& s, const Bucket& b);

inline constexpr uint16_t kThreeOfCoins = 4392;    
inline constexpr uint32_t kMaxUseQuantity = 10000;

}   
