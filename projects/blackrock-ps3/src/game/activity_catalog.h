#pragma once
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <string>
#include <vector>

namespace brps3::activity {

struct DeployEntry {
    uint32_t id = 0;
    std::string category, name;
};

struct NamedActivity {
    std::string name;                    
    std::string category, display;       
    int phases = 0;
    std::vector<uint32_t> scenarios;     
};

struct Phase {
    std::string activity;
    int index = 0;
    std::string name;                    
    uint32_t hash = 0;                   
    int kind = 0;                        
};

struct Catalog {
    std::vector<DeployEntry> deploy;
    std::vector<std::string> deployCategories;    
    std::vector<NamedActivity> named;
    std::vector<std::string> namedCategories;
    std::vector<Phase> phases;
    std::string error;                   

    const DeployEntry* byId(uint32_t id) const;              
    const NamedActivity* byName(const std::string& name) const;
    const NamedActivity* byScenario(uint32_t catalogTag) const;
    std::vector<const Phase*> phasesOf(const std::string& activity) const;
};

 
const Catalog& catalog();
 
std::vector<DeployEntry> parseDeployTsv(const std::string& text);
std::vector<NamedActivity> parseNamedTsv(const std::string& text);
std::vector<Phase> parsePhasesTsv(const std::string& text);

 
std::string lower(std::string s);
std::vector<std::string> queryWords(const std::string& query);
bool matchesWords(const std::string& lowerHaystack, const std::vector<std::string>& words);

 
inline constexpr const char* kCrotaActivity = "moon_raid_1";
inline constexpr uint32_t kNoPhase = 0x811C9DC5;    
inline constexpr uint32_t kCrotaPhases[6] = {0x904F3E56, 0x4515D38F, 0xE97CA5AF, 0xD322D2E9, 0x128739E1, 0x85F2C86D};
inline constexpr const char* kCrotaPhaseLabels[6] = {"Hellmouth descent", "Abyss maze (normal start)", "Lens bridge",
                                                     "Lens hall (Deathsingers)", "Lens council", "Lens finale (Crota)"};
bool isCrotaPhase(uint32_t hash);
 
 
bool validActivityName(const std::string& name);

}   
