#pragma once
 
 
 
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>

#include "app/engine.h"
#include "app/registry.h"
#include "app/settings.h"
#include "ui/widgets.h"

namespace brps3::ui {

 
inline constexpr int kPageCount = int(Page::Count) + 1;
inline constexpr int kSettingsPage = int(Page::Count);
const char* pageLabel(int page);
const char* pageIcon(int page);
const char* pageSubtitle(int page);
int pageFromName(const std::string& name);     
bool pageReady(int page);                      
const char* pagePlans(int page);               

 
namespace key {
inline constexpr const char* Host = "host";
inline constexpr const char* AutoConnect = "ui.auto_connect";
inline constexpr const char* Scale = "ui.scale";                  
inline constexpr const char* ReduceMotion = "ui.reduce_motion";
inline constexpr const char* LogLevel = "ui.log_level";           
inline constexpr const char* LogOpen = "ui.log_open";
inline constexpr const char* Window = "ui.window";                
inline constexpr const char* LastPage = "ui.page";
inline constexpr const char* Collapsed = "ui.sidebar_collapsed";
inline constexpr const char* Pins = "ui.pins";
inline constexpr const char* Sticky = "ui.sticky";
inline constexpr const char* Recent = "ui.recent";
inline constexpr const char* StickyValue = "ui.sticky.";          
inline constexpr const char* Hotkey = "ui.hotkey.";               
inline constexpr const char* Tab = "ui.tab.";                     
inline constexpr const char* TipsHidden = "ui.tips_hidden";
inline constexpr const char* LogHeight = "ui.log_height";
}   
inline constexpr const char* kDefaultHost = "192.168.0.254";

 
Settings& settings();
void setString(const char* key, const std::string& value);

 
 
 

enum class ItemKind { Toggle, Value, Action, Section };

struct Item {
    ItemKind kind;
    int index;                   
    std::string id, label, help, tab, section;    
    Page page;                   
    std::string badge;
    std::string parent;          
    int depth = 0;               
    size_t seq = 0;              
    const ToggleDef* toggle() const;
    const ValueDef* value() const;
    const ActionDef* action() const;
    const SectionDef* sectionDef() const;
};

struct ItemState {
    bool on = false;
    bool pending = false;
    float value = 0;             
    float applied = 0;           
    bool valueKnown = false;
    bool changed = false;        
};

const std::vector<Item>& items();
const char* kindIcon(const Item& it);
const Item* findItem(const std::string& id);
 
 
 
const std::vector<const Item*>& childrenOf(const std::string& parentId);
bool childVisible(const Item& it);
const Item& revealTarget(const Item& it);
 
std::string pathLabel(const Item& it, const char* separator = "  \xE2\x80\xBA  ");
 
std::string valueText(const Item& it, float v);
ItemState& state(const Item& it);
void buildItems();
void reloadUiLists();           

 
void setToggle(const Item& it, bool on, bool user = true);
void applyValue(const Item& it, float v, bool user = true);
void runAction(const Item& it, bool user = true);        
void activate(const Item& it);                           
void reveal(const Item& it);                             
void navigate(int page, const std::string& tab = {});

void onConnected();
void onDisconnected();
void onPanicked();                                       
 
void panicNow();
inline constexpr const char* kPanicId = "app.panic";
 
void runPortCheck(const std::string& id);
void seedPortChecksDemo();                                

 
ImGuiKeyChord parseChord(const std::string& text);

 
bool pinned(const std::string& id);
bool sticky(const std::string& id);
void setSticky(const Item& it, bool on);
ImGuiKeyChord hotkey(const std::string& id);
std::string chordName(ImGuiKeyChord chord);
void setHotkey(const std::string& id, ImGuiKeyChord chord);
void handleHotkeys();
void openItemMenu(const std::string& id);
void openHotkeyCapture(const std::string& id);
 
void startListening(const std::string& id);
bool listening(const std::string& id);
void drawItemPopups();

 
struct CardSpec {
    std::string key, title, subtitle;
    const char* icon = nullptr;
    bool full = false;
    std::function<void()> body;
    bool flash = false;           
    std::string menuId;           
    std::function<void()> header;    
    std::string badge;            
    float pad = -1;               
};
void masonry(const std::vector<CardSpec>& cards, float gapPx);
 
void drawItemRow(const Item& it, bool connected, int depth = 0, bool actionAsRow = false);
 
void drawChildren(const Item& parent, bool connected, int depth);
void drawItemTile(const Item& it, float width, bool connected);
void drawRegistryPage(int page);
void drawHomePage();
void drawSettingsPage();

 
void openPalette();
bool paletteOpen();                  
void drawRailSearch(float width);    
void drawPalette();                  

 
 
 

class UiBridge final : public Ui {
public:
    bool connected() const override;
    bool ticking() const override;
    void post(const std::string& label, std::function<void(Ctx&)> job,
              std::function<void(bool ok, const std::string& error)> done = {}) override;
    void toast(const std::string& text) override;
    Settings& settings() override;
    bool busy(const std::string& label) const override;
};

 
void connectNow();                    
void toggleLogDrawer();
void toggleSidebar();
void setUserScale(float scale);       
void openStateFolder();

struct ShellState {
    int page = 0;
    int tab[kPageCount] = {};
    EngineStatus status;
    UiBridge bridge;
    std::string host;
    bool logOpen = false;
    bool collapsed = false;
    bool sidebarForced = false;  
    bool screenshot = false;
    std::string highlightId;
    double highlightUntil = 0;
    bool scrollToHighlight = false;
    double now = 0;
    float contentWidth = 0;      
    float userScale = 0;
    bool scaleDirty = false;
    double appStartEpoch = 0;    
};
ShellState& shell();
bool connected();

}   
