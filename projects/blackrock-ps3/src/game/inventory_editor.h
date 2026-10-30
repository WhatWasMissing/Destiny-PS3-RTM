#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "game/inventory_data.h"
#include "game/inventory_snapshot.h"

namespace brps3::inventory {

 
struct GridStep {
    std::vector<uint16_t> perks;         
    std::vector<int> abilities;          
    std::vector<uint32_t> modifiers;     
};
struct GridGroup {
    std::vector<GridStep> steps;
};
struct GridNode {
    int index = 0;
    int roll = 255;                      
    bool exclusive = false;
    std::vector<GridGroup> groups;
};
struct Grid {
    uint16_t index = 0;
    std::string label;
    std::vector<GridNode> nodes;
};
struct ArtCategory {
    int category = 0, statSelector = 255;
    std::vector<std::pair<int, int>> thresholds;    
};
struct Pattern {
    uint16_t index = 0;
    std::string label, search;
    std::vector<ArtCategory> categories;
};
struct ArtPart {
    int category = 0;
    std::vector<uint32_t> tags;          
};
struct Art {
    uint16_t index = 0;
    uint32_t baseTag = 0, altTag = 0;    
    std::string label, search;
    std::vector<ArtPart> parts;
};
struct Choice {
    uint32_t key = 0;                    
    std::string label;
};

class EditorCatalog {
public:
    static EditorCatalog parse(const std::string& grids, const std::string& patterns, const std::string& art, const std::string& abilities,
                               const std::string& modifiers, const std::string& meta);
    std::vector<Grid> grids;
    std::vector<Pattern> patterns;
    std::vector<Art> art;
    std::vector<Choice> abilities, modifiers;
    std::vector<int> artConstants;
    std::string fingerprint;
    std::string error;
};
const EditorCatalog& editorCatalog();    

 
struct Talents {
    uint16_t grid = 0xFFFF;
    std::array<uint8_t, 36> nodes{};
    std::array<uint8_t, 8> rolls{};
    std::array<std::array<uint8_t, 3>, 5> abilities{};
    bool operator==(const Talents&) const = default;
};
Talents talentsOf(const std::array<uint8_t, 168>& detail);
void applyTalents(std::array<uint8_t, 168>& detail, const Talents& t);
struct TalentPerks {
    std::vector<uint16_t> perks;         
    std::vector<uint32_t> modifiers;
    std::vector<int> abilities;          
};
 
TalentPerks decodeTalents(const Talents& t, const EditorCatalog& ec, const Catalog& cat);
 
int nodeVariant(const GridNode& n, const Talents& t);

 
inline const char* const kSlotNames[20] = {"Subclass", "Helmet", "Gauntlets", "Chest armor", "Leg armor", "Class item", "Primary weapon",
                                           "Special weapon", "Heavy weapon", "Ship", "Sparrow", "Ghost", "Emblem", "Shader", "Emote",
                                           "Horn", "Artifact", "Slot 17", "Slot 18", "Slot 19"};
inline const char* const kAbilityNames[5] = {"Grenade", "Super", "Melee", "Jump", "Sprint"};
inline bool aggregateSlot(int slot) { return slot >= 0 && (slot <= 5 || slot == 11 || slot == 16); }
inline bool weaponSlot(int slot) { return slot >= 6 && slot <= 8; }

struct Profile {
    enum Kind { Cache, Gameplay } kind = Cache;
    uint32_t addr = 0;
    std::vector<uint8_t> raw;            
};
struct EditorItem {
    uint64_t soid = 0;
    uint16_t def = 0;
    uint32_t addr = 0;
    std::array<uint8_t, 168> raw{};
    int slot = -1;                       
    Talents talents;
    std::vector<uint16_t> perks;         
    std::string perkError;
};
struct SlotState {
    uint64_t soid = 0;
    uint16_t def = 0xFFFF, pattern = 0xFFFF, art = 0xFFFF;
    std::array<uint8_t, 32> parts{};
    std::vector<uint16_t> perks;         
};
struct EditorSnapshot {
    uint32_t manager = 0, characterAddr = 0, descriptorAddr = 0;
    std::array<uint8_t, 64> descriptor{};
    uint64_t accountSoid = 0, characterSoid = 0;
    std::array<uint8_t, 160> equipped{};
    std::vector<EditorItem> items;
    std::vector<Profile> profiles;       
    std::array<SlotState, 20> slots;
    std::vector<uint16_t> characterPerks;    
    std::array<std::array<uint8_t, 68>, 5> abilities{};
    std::map<uint64_t, std::vector<uint16_t>> contributions;    
    uint64_t stamp = 0;                  
    uint64_t reads = 0;
    const EditorItem* item(uint64_t soid) const;
    std::array<uint8_t, 64> aggregateKey() const;    
};
EditorSnapshot editorRead(world::Reader& mem, DefinitionCache& cache, const EditorCatalog& ec, const Catalog& cat);

 
enum class EditMode { Equipment, Ability, Character, Talents };
const char* editModeName(EditMode m);

struct EditRequest {
    EditMode mode = EditMode::Equipment;
    int slot = 0;                        
    uint64_t soid = 0;                   
    uint16_t pattern = 0xFFFF, art = 0xFFFF;
    std::vector<uint16_t> perks;         
    uint8_t selector = 255;
    bool haveModifiers = false;
    std::array<uint32_t, 16> modifiers{};
    Talents talents;
    bool operator==(const EditRequest&) const = default;
};
struct Change {
    std::string name;
    uint32_t addr = 0;
    std::vector<uint8_t> before, after;
    bool operator==(const Change&) const = default;
};
struct EditPlan {
    EditRequest request;
    std::vector<Change> changes;
    uint64_t character = 0;
    std::array<uint8_t, 160> equipped{};
    std::map<uint64_t, std::vector<uint16_t>> previousContributions;
};
EditPlan editorPlan(const EditorSnapshot& s, const EditRequest& r, const EditorCatalog& ec, const Catalog& cat);
 
std::array<uint8_t, 32> resolveArt(int pattern, int art, int slot, const std::vector<uint8_t>& profile, const EditorCatalog& ec);
 
EditRequest draftFor(const EditorSnapshot& s, EditMode m, int slot, uint64_t itemSoid);
std::vector<uint16_t> nonEmpty(const std::vector<uint16_t>& v);

 
struct Applied {
    EditPlan plan;
    uint32_t manager = 0, descriptorAddr = 0, characterAddr = 0;
    std::array<uint8_t, 160> equipped{};
    uint64_t character = 0;
    size_t written = 0;                  
    std::string when;
};
using WriteFn = std::function<void(uint32_t addr, const std::vector<uint8_t>& bytes)>;
 
 
Applied editorCommit(world::Reader& mem, const WriteFn& write, DefinitionCache& cache, const EditorSnapshot& snapshot, const EditPlan& plan,
                     const EditorCatalog& ec, const Catalog& cat, Applied* partial = nullptr);
 
 
 
size_t editorRevert(world::Reader& mem, const WriteFn& write, const Applied& a, std::vector<std::string>* skipped = nullptr);

 
std::string serialiseRequest(const EditRequest& r);
EditRequest parseRequest(const std::string& text);    

}   
