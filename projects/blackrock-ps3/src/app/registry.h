#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/error.h"

namespace brps3 {

class Runtime;
class Engine;
class Settings;

 
enum class Page : int { Home, Spawner, HopOns, Console, Loot, LootEvents, Vault, Testing, Count };
const char* pageName(Page p);            

 
inline constexpr const char* kPortChecksTab = "Port checks";
 
inline constexpr const char* kBadgeNeedsCheck = "Needs check";    
inline constexpr const char* kBadgeBlocked = "Blocked";           
inline constexpr const char* kBadgeDev = "Dev";                   
 
inline constexpr int kMaxChildDepth = 4;

class CtxProxy;

 
class Ctx {
public:
    virtual ~Ctx() = default;
    virtual Runtime& rt() = 0;
    virtual Settings& settings() = 0;
    virtual void log(const std::string& text) = 0;            
    virtual void toast(const std::string& text) = 0;          
    virtual bool cancelled() const = 0;                       
     
    virtual void require(const std::vector<std::pair<uint32_t, const char*>>& fingerprints, const char* what) = 0;
     
     
    virtual void startLoop(const std::string& id, int intervalMs, std::function<void(Ctx&)> fn) = 0;
    virtual void stopLoop(const std::string& id) = 0;
    virtual bool loopRunning(const std::string& id) const = 0;
     
    virtual void onDisconnect(std::function<void(Ctx&)> fn) = 0;
     
     
     
     
    virtual void onPanic(const std::string& id, std::function<void(Ctx&)> fn) = 0;
     
    template <class T> T& state(const std::string& key) {
        auto& slot = stateSlot(key);
        if (!slot) slot = std::shared_ptr<void>(new T(), [](void* p) { delete static_cast<T*>(p); });
        return *static_cast<T*>(slot.get());
    }
protected:
    friend class CtxProxy;
    virtual std::shared_ptr<void>& stateSlot(const std::string& key) = 0;
};

 
 
class CtxProxy : public Ctx {
public:
    explicit CtxProxy(Ctx& inner) : inner_(inner) {}
    Runtime& rt() override { return inner_.rt(); }
    Settings& settings() override { return inner_.settings(); }
    void log(const std::string& text) override { inner_.log(text); }
    void toast(const std::string& text) override { inner_.toast(text); }
    bool cancelled() const override { return inner_.cancelled(); }
    void require(const std::vector<std::pair<uint32_t, const char*>>& fingerprints, const char* what) override {
        inner_.require(fingerprints, what);
    }
    void startLoop(const std::string& id, int intervalMs, std::function<void(Ctx&)> fn) override {
        inner_.startLoop(id, intervalMs, std::move(fn));
    }
    void stopLoop(const std::string& id) override { inner_.stopLoop(id); }
    bool loopRunning(const std::string& id) const override { return inner_.loopRunning(id); }
    void onDisconnect(std::function<void(Ctx&)> fn) override { inner_.onDisconnect(std::move(fn)); }
    void onPanic(const std::string& id, std::function<void(Ctx&)> fn) override { inner_.onPanic(id, std::move(fn)); }
    Ctx& inner() { return inner_; }
protected:
    std::shared_ptr<void>& stateSlot(const std::string& key) override { return inner_.stateSlot(key); }
private:
    Ctx& inner_;
};

 
class Ui {
public:
    virtual ~Ui() = default;
    virtual bool connected() const = 0;
    virtual bool ticking() const = 0;                          
     
    virtual void post(const std::string& label, std::function<void(Ctx&)> job,
                      std::function<void(bool ok, const std::string& error)> done = {}) = 0;
    virtual void toast(const std::string& text) = 0;
    virtual Settings& settings() = 0;
    virtual bool busy(const std::string& label) const = 0;    
};

struct ToggleDef {                       
    std::string id;                      
    Page page = Page::Console;
    std::string tab;                     
    std::string section;                 
    std::string label;
    std::string help;                    
     
    std::function<void(Ctx&, bool on)> set;
     
    std::function<bool(Ctx&)> query;
    std::string badge;                   
    std::string hotkey;                  
    std::string parent;                  
};

struct ValueDef {                        
    std::string id;
    Page page = Page::Console;
    std::string tab, section, label, help;
    float min = 0, max = 1, def = 0, step = 0;    
    std::string format = "%.2f";
    bool applyWhileDragging = false;     
    std::function<void(Ctx&, float)> set;
    std::function<float(Ctx&)> query;    
    std::function<void(Ctx&)> reset;     
    std::string badge;
    std::string parent;                  
    std::string unit;                    
    bool logScale = false;               
    bool integer = false;                

     
    const char* numberFormat() const;
     
    float quantize(float v) const;
     
    std::string text(float v) const;
};

struct ActionDef {                       
    std::string id;
    Page page = Page::Console;
    std::string tab, section, label, help;
    std::function<void(Ctx&)> run;
    bool dangerous = false;              
    std::string badge;
    std::string hotkey;                  
     
     
    std::function<void()> immediate;
     
     
    const char* icon = nullptr;
    std::string parent;                  
};

struct SectionDef {                      
    std::string id;
    Page page = Page::Console;
    std::string tab, section;
    int order = 50;                      
    std::function<void(Ui&)> draw;
     
     
    std::function<void()> onDisconnect;
    std::string badge;                   
     
     
    std::function<void()> onPanic;
};

class Registry {
public:
    static Registry& get();
    void toggle(ToggleDef d);
    void value(ValueDef d);
    void action(ActionDef d);
    void section(SectionDef d);
    const std::vector<ToggleDef>& toggles() const { return toggles_; }
    const std::vector<ValueDef>& values() const { return values_; }
    const std::vector<ActionDef>& actions() const { return actions_; }
    const std::vector<SectionDef>& sections() const { return sections_; }
     
     
     
     
    std::vector<std::string> tabs(Page p) const;
     
    static std::vector<std::string> canonicalTabs(Page p);
     
    static std::string defaultTab(Page p);
    static std::string effectiveTab(Page p, const std::string& tab) { return tab.empty() ? defaultTab(p) : tab; }
     
    const ToggleDef* findToggle(const std::string& id) const;
    const ValueDef* findValue(const std::string& id) const;
    const ActionDef* findAction(const std::string& id) const;

     
    enum class Kind : uint8_t { Toggle, Value, Action, Section };
    struct Ref {
        Kind kind;
        size_t index;                    
        bool operator==(const Ref&) const = default;
    };
    const std::vector<Ref>& order() const { return order_; }    
    const std::string& idOf(Ref r) const;
    const std::string& declaredParent(Ref r) const;            
     
     
     
    const ToggleDef* parentOf(const std::string& id) const;
    const ToggleDef* parentOf(Ref r) const;
    const ToggleDef* rootOf(Ref r) const;                      
    int depth(Ref r) const;                                     
    std::vector<Ref> children(const std::string& parentId) const;    
     
    std::string parentProblem(Ref r) const;
     
    struct Place {
        Page page;
        std::string tab, section;
    };
    Place placeOf(Ref r) const;
private:
    std::vector<ToggleDef> toggles_;
    std::vector<ValueDef> values_;
    std::vector<ActionDef> actions_;
    std::vector<SectionDef> sections_;
    std::vector<Ref> order_;
};

struct AutoRegister {
    explicit AutoRegister(void (*fn)(Registry&)) { fn(Registry::get()); }
};

}   

#define BR_FEATURES(name)                                                    \
    static void br_features_##name(::brps3::Registry& r);                   \
    static ::brps3::AutoRegister br_autoreg_##name(&br_features_##name);    \
    static void br_features_##name(::brps3::Registry& r)
