#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <string>
#include <vector>

namespace brps3::spawn {

struct CatalogRow {
    uint32_t tag = 0;                    
    uint8_t type = 0;                    
    uint16_t squads = 0;                 
    bool refused = false;                
    std::string squadNames;              
    std::string family;                  
    std::string candidates;              
};

class Catalog {
public:
    static Catalog parse(const std::string& tsv);             
    const std::vector<CatalogRow>& rows() const { return rows_; }    
    const CatalogRow* find(uint32_t tag) const;               
    size_t size() const { return rows_.size(); }
    std::string error;                   
private:
    std::vector<CatalogRow> rows_;
};
 
const Catalog& catalog();

 
struct Opener {
    std::string group;                   
    std::string id;                      
    std::string name, where, opens, verdict, note;
    std::vector<uint32_t> tags;          
};
std::vector<Opener> parseOpeners(const std::string& tsv);     
const std::vector<Opener>& openers();                         
std::string openersError();
 
const char* openerGroupLabel(const std::string& group);

 
 
const char* categoryName(uint8_t type);
 
const char* usageNote(const CatalogRow& r);
 
bool noiseType(uint8_t type);
 
bool riskyType(uint8_t type);

 
 
std::string searchText(const CatalogRow& r);
 
std::vector<std::string> queryWords(const std::string& query);
bool matches(const std::string& hay, const std::vector<std::string>& words);

 
bool parseTag(const std::string& text, uint32_t& tag);
std::string tagText(uint32_t tag);       

}   
