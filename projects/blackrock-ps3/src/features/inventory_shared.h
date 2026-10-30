#pragma once
 
 
 
 
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "app/registry.h"
#include "game/inventory_data.h"
#include "game/inventory_model.h"
#include "game/inventory_policy.h"
#include "game/inventory_request.h"
#include "game/inventory_snapshot.h"

namespace brps3::invui {

 
inline constexpr const char* kFavKey = "inventory.favorites";         
inline constexpr const char* kRecentKey = "inventory.recent";         
inline constexpr const char* kViewKey = "inventory.view";             
inline constexpr const char* kGroupKey = "inventory.group";           
inline constexpr const char* kAutoKey = "inventory.autorefresh";
inline constexpr const char* kRevertKey = "inventory.editor.revert";  

 
struct Selection {
    bool valid = false;
    inventory::Owner owner = inventory::Owner::Character;
    uint16_t slot = 0, def = 0;
    uint64_t soid = 0;
    uint32_t hash = 0;
    bool operator==(const Selection&) const = default;
};

struct LastResult {
    bool ok = false;
    std::string text;
    double at = 0;                       
};

struct Shared {
    std::mutex m;
    std::shared_ptr<const inventory::Snapshot> snap;      
    uint64_t generation = 0;                              
    double readAt = 0;                                    
    std::string error;                                    
    bool notSignedIn = false;
    Selection sel;
     
    std::atomic<bool> acting{false};
    std::atomic<bool> stopWatch{false};
    std::string actionLabel, progress;
    LastResult last;
    std::vector<std::string> lastConditions;              
    bool demo = false;
};
Shared& shared();

 
struct Io {
    std::mutex m;
    inventory::DefinitionCache cache;
};

 
std::shared_ptr<const inventory::Snapshot> readAndPublish(Ctx& c, bool talents = true);
void publish(std::shared_ptr<const inventory::Snapshot> s, const std::string& error = {}, bool notSignedIn = false);

 
void postRefresh(Ui& ui, bool quiet = false);
std::shared_ptr<const inventory::Snapshot> current();
Selection selection();
void select(const Selection& s);
Selection selectionOf(const inventory::Item& it);
const inventory::Item* findSelected(const inventory::Snapshot& s, const Selection& sel);
 
 
void startAction(inventory::Action a, uint32_t quantity = 1);
bool actionBusy();
 
void runSelectedAction(Ctx& c, inventory::Action a);

 
bool favourite(uint32_t hash);
void toggleFavourite(uint32_t hash);
void noteRecent(uint32_t hash);
std::vector<uint32_t> recents();

 
void revealItem(const Selection& s);
 
void openVaultTab(const char* tab);

 
bool demoMode();
void seedDemo();

std::string rarityText(int rarity);
std::string shortHex(uint64_t soid);    

}   
