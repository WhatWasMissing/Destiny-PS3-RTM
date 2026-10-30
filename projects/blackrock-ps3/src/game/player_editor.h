#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "game/world_mem.h"

namespace brps3::player::editor {

struct ComponentRef {
    uint32_t owner = 0, ownerEntry = 0, ownerHead = 0;    
    uint32_t accessor = 0, flags = 0, instance = 0, back = 0, cls = 0;
    uint32_t data = 0, size = 0;
    uint32_t definition = 0;             
    bool sameAs(const ComponentRef& o) const {
        return owner == o.owner && accessor == o.accessor && data == o.data && instance == o.instance && back == o.back;
    }
};

struct Field {
    uint32_t offset = 0;                 
    uint32_t id = 0, tag = 0;
    uint32_t raw = 0, copy = 0;
    bool integer = false;                
    bool mirrored = true;                
    std::string name, group, unit, note;
    bool known() const { return !name.empty(); }
    double value() const;
    std::string label() const;           
};

struct Weapon {
    uint32_t handle = 0, entry = 0;
    uint32_t archetype = 0;              
    std::string name;                    
    ComponentRef fire;                   
    std::vector<Field> fields;
    std::vector<ComponentRef> components;    
    std::string note;                    
};

struct Scan {
    uint32_t player = 0;
    std::vector<Weapon> weapons;
    std::vector<std::string> notes;
};

 
std::vector<Field> decodeFields(const world::Bytes& data);
Scan scanWeapons(world::Reader& mem);
bool validate(world::Reader& mem, const ComponentRef& ref, std::string& error);
std::optional<world::Bytes> readComponent(world::Reader& mem, const ComponentRef& ref, std::string& error);

struct Edit {
    ComponentRef component;
    uint32_t weapon = 0;
    uint32_t offset = 0;
    std::vector<uint8_t> original, applied;    
    std::string label;
};
using ByteWriter = std::function<void(uint32_t addr, const std::vector<uint8_t>& bytes)>;    
using Checkpoint = std::function<bool(const std::vector<Edit>& journal)>;                  

struct Change {
    Field field;                         
    double value = 0;
};
 
 
bool apply(world::Reader& mem, const ByteWriter& write, const ComponentRef& ref, uint32_t weapon, const std::vector<Change>& changes,
           std::vector<Edit>& journal, const Checkpoint& checkpoint, std::string& error, size_t& done);
bool restoreEdit(world::Reader& mem, const ByteWriter& write, const Edit& e, std::string& error);
 
bool restoreAll(world::Reader& mem, const ByteWriter& write, std::vector<Edit>& journal, const Checkpoint& checkpoint, std::string& error,
                size_t& restored);

 
std::string serializeJournal(const std::vector<Edit>& edits);
std::vector<Edit> parseJournal(const std::string& text);    
std::filesystem::path journalPath();

 
struct PresetField {
    uint32_t id = 0, tag = 0, offset = 0;
    bool integer = false;
    double value = 0;
};
struct Preset {
    std::string name;
    uint32_t archetype = 0, componentClass = 0;
    std::vector<PresetField> fields;
};
std::string serializePresets(const std::vector<Preset>& presets);
std::vector<Preset> parsePresets(const std::string& text);    
std::filesystem::path presetsPath();                           
std::vector<Preset> loadPresets(std::string& error);           
bool savePresets(const std::vector<Preset>& presets, std::string& error);

struct PresetMatch {
    bool sameWeapon = false;             
    std::vector<std::pair<size_t, double>> values;    
    size_t missing = 0;                  
};
PresetMatch matchPreset(const Preset& p, const Weapon& w);
Preset presetFrom(const std::string& name, const Weapon& w, const std::vector<double>& values);    

 
struct QuickPreset {
    const char* name;
    const char* help;
};
const std::vector<QuickPreset>& quickPresets();
 
std::vector<std::pair<size_t, double>> quickValues(size_t index, const Weapon& w, int burst = 3);

}   
