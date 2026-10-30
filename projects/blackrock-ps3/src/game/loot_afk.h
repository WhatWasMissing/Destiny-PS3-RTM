#pragma once
 
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <optional>
#include <string>

#include "game/loot_console.h"

namespace brps3::loot::afk {

enum class Method { ResetIdle = 0, ZeroLimit = 1 };
const char* methodName(Method m);

struct Binding {
    uint32_t root = 0;                   
    uint32_t inWorld = 0, mode = 0xFFFFFFFF;
    bool noLimit = false;                
    uint32_t limitAddr = 0, original = 0, limit = 0;
    uint32_t playerRoot = 0, slot = 0xFFFFFFFF;
    uint32_t activityAddr = 0;
    uint64_t activity = 0, elapsed = 0;
    std::string modeName() const;        
    bool sameBinding(const Binding& o) const {
        return root == o.root && mode == o.mode && limitAddr == o.limitAddr && playerRoot == o.playerRoot && slot == o.slot;
    }
};
 
 
Binding inspect(world::Reader& mem, bool needSlot);

 
 
class Keeper {
public:
     
     
     
    std::string acquire(Console& con, Method method, const std::string& owner, uint64_t nowMs);
     
    std::string poll(Console& con, uint64_t nowMs);
     
     
    bool release(Console& con, const std::string& owner, std::string* why = nullptr);
    bool held() const { return !owners_.empty(); }
    bool armed() const { return held() && binding_.root != 0; }    
    bool heldBy(const std::string& owner) const;
    Method method() const { return method_; }
    uint32_t resets() const { return resets_; }
    const Binding& binding() const { return binding_; }
    std::string status() const;          
    void forget();                       
private:
    std::string once(const std::string& line);    
    std::vector<std::string> owners_;
    Method method_ = Method::ResetIdle;
    Binding binding_;
    bool limitZeroed_ = false;
    uint32_t resets_ = 0;
    uint64_t nextPoll_ = 0;
    std::string lastNote_;
};

}   
