#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "core/runtime.h"
#include "game/patches_sites.h"

namespace brps3::patches {

class SiteBook {
public:
    struct Result {
        std::vector<std::string> applied;     
        std::vector<std::string> borrowed;    
    };
     
    Result acquire(Runtime& rt, const std::string& holder, const std::vector<std::string>& unitNames);
     
    void release(Runtime& rt, const std::string& holder);
     
    std::vector<std::string> releaseAll(Runtime& rt);
     
    void forget();

    bool holds(const std::string& holder) const;
    std::vector<std::string> holders(const std::string& unit) const;
    bool borrowed(const std::string& unit) const;
    bool applied(const std::string& unit) const;    

    struct UnitStatus {
        std::string unit;
        std::vector<std::string> holders;
        bool ours = false, borrowed = false;
    };
    std::vector<UnitStatus> snapshot() const;

private:
    void releaseLocked(Runtime& rt, const std::string& holder, std::vector<std::string>& errors);
    mutable std::mutex m_;
    std::map<std::string, std::set<std::string>> holders_;    
    std::set<std::string> ours_, borrowed_;
};

 
std::vector<SiteState> readUnit(Runtime& rt, const Unit& u, std::vector<uint32_t>* current = nullptr);

 
 
 
std::string checkFeatureBytes(Runtime& rt, Feature f);

}   
