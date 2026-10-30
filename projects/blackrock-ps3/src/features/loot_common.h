#pragma once
 
 
 
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <imgui.h>

#include "app/registry.h"
#include "game/loot_afk.h"
#include "ui/widgets.h"

namespace brps3::lootui {

inline constexpr const char* kTabLoot = "Loot";
inline constexpr const char* kTabPoe = "PoE";
inline constexpr const char* kTabPickup = "Pickup filter";
inline constexpr const char* kTabGrimoire = "Grimoire";
inline constexpr const char* kTabLighthouse = "Lighthouse";
inline constexpr const char* kChecksSection = "Loot";    

 
 
 
 
struct AfkView {
    bool held = false, armed = false, noLimit = false;
    loot::afk::Method method = loot::afk::Method::ResetIdle;
    uint32_t resets = 0;
    std::string status = "Off";
    std::string lastEvent;                
};
struct AfkShared {
    std::mutex m;
    loot::afk::Keeper keeper;
    void publish(const std::string& lastEvent = {});    
    void clearEvent();
    AfkView view() const;                 
private:
    mutable std::mutex viewM_;
    AfkView view_;
};
AfkShared& afkShared();
loot::afk::Method afkMethod(Settings& s);    

 
struct Summary {
    std::string value, sub;
    ImVec4 color = ui::pal::Muted;
    bool active = false;
};
Summary poeSummary();
Summary afkSummary();
Summary pickupSummary();
Summary grimoireSummary();
Summary lighthouseSummary();

 
enum class Mark { Unknown, Ok, Bad, Waiting, Skip };
 
void checkRow(const char* text, Mark mark, const char* detail = nullptr);
 
void meter(const char* labelText, float fraction, const char* value, const ImVec4& color, float width = 0);
 
void kicker(const char* text);
 
void wrapText(const char* text, float widthPx, const ImVec4& color = ui::pal::Muted, float size = ui::type::Small);
 
void beginPanel(const char* id, float width, float minHeight = 0);
void endPanel();
 
std::string duration(double seconds);
std::string grouped(uint64_t n);          
double now();                             

bool demo();                              

 
struct FeedItem {
    double time = 0;
    std::string source, text;
    bool ok = true;
};
void feed(const char* source, const std::string& text, bool ok = true);
std::vector<FeedItem> feedItems();       

}   
