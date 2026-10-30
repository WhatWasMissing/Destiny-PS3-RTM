#pragma once
 
 
 
 
 
 
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace brps3::hopons {

 
struct HopEntry {
    uint32_t tag = 0;
    uint8_t type = 0;                    
    std::string package;                 
    std::string name;                    
    std::string evidence;                
    std::string identifier;              
    std::string archetype;               
    std::string hints;                   
    std::string location() const;        
    std::string activity() const;
};
const char* hopTypeName(uint8_t type);   
const char* hopTypeShort(uint8_t type);  
 
uint8_t requiredTargetType(uint8_t defType);

class HopCatalog {
public:
    static HopCatalog parse(const std::string& tsv);           
    const HopEntry* find(uint32_t tag) const;
    const std::vector<HopEntry>& all() const { return rows_; }    
    size_t size() const { return rows_.size(); }
    size_t namedCount() const { return named_; }
    size_t countOfType(uint8_t type) const;
     
    const std::string& hay(size_t row) const { return hay_[row]; }
    std::string error;
private:
    std::vector<HopEntry> rows_;
    std::vector<std::string> hay_;
    std::unordered_map<uint32_t, size_t> byTag_;
    size_t named_ = 0;
};
const HopCatalog& hopCatalog();          

 
struct EncounterDef {
    uint32_t root = 0, blob = 0, cls = 0, binding = 0, bindingClass = 0, size = 0;
    uint32_t slotHash = 0, slotId = 0, outputSchema = 0, inputSchema = 0, effect = 0;
    std::string name;                    
    char evidence = 'g';                 
};
class EncounterCatalog {
public:
    static EncounterCatalog parse(const std::string& rowsTsv, const std::string& classesTsv);
    const std::vector<EncounterDef>& rows() const { return rows_; }
    const EncounterDef* match(uint32_t blob, uint32_t binding, uint32_t bindingClass) const;
    const EncounterDef* byRoot(uint32_t root) const;
    std::string kind(uint32_t cls) const;                
    std::vector<std::pair<uint32_t, std::string>> classes() const;    
    const std::string& hay(size_t row) const { return hay_[row]; }
    std::string error;
private:
    std::vector<EncounterDef> rows_;
    std::vector<std::string> hay_;
    std::unordered_map<uint64_t, std::vector<size_t>> byKey_;    
    std::unordered_map<uint32_t, size_t> byRoot_;
    std::vector<std::pair<uint32_t, std::string>> classes_;
    std::unordered_map<uint32_t, std::string> kinds_;
};
const EncounterCatalog& encounterCatalog();    

 
struct SquadSite {
    uint8_t member = 0, variant = 0, combatant = 0;
    uint32_t offset = 0;                 
    uint32_t entity = 0;
};
struct SquadDefRow {
    uint32_t tag = 0;
    std::string name;                    
    uint32_t size = 0;                   
    std::vector<std::string> members;    
    std::vector<SquadSite> sites;
    std::vector<uint32_t> entities() const;    
};
class SquadCatalog {
public:
    static SquadCatalog parse(const std::string& tsv);
    const SquadDefRow* find(uint32_t tag) const;
    const std::vector<SquadDefRow>& rows() const { return rows_; }
    const std::string& hay(size_t row) const { return hay_[row]; }
    std::string error;
private:
    std::vector<SquadDefRow> rows_;
    std::vector<std::string> hay_;
    std::unordered_map<uint32_t, size_t> byTag_;
};
const SquadCatalog& squadCatalog();      

 
std::string lowerCopy(std::string s);
std::vector<std::string> searchWords(const std::string& query);    
bool matchesAll(const std::string& hay, const std::vector<std::string>& words);

}   
