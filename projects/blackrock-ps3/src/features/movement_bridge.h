#pragma once
 
 
 
 
 
 
 
#include <optional>
#include <string>
#include <vector>

#include "app/registry.h"

namespace brps3::mvbridge {

struct ItemInfo {
    std::string id, label, help;
    std::string where;                   
    Page page = Page::Home;
    bool toggle = false;                 
    bool dangerous = false;
    const char* icon = nullptr;
};

 
 
std::vector<ItemInfo> bindableItems();
std::optional<ItemInfo> item(const std::string& id);

bool activate(const std::string& id);    
bool toggleOn(const std::string& id);    
float value(const std::string& id, float fallback);    
bool applyValue(const std::string& id, float v);       
void demoToggle(const std::string& id, bool on);       
void reveal(const std::string& id);      

}   
