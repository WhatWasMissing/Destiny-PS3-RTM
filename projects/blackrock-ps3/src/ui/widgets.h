#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <imgui.h>

#include "ui/icons.h"
#include "ui/theme.h"

namespace brps3 {
class Ui;
enum class Page : int;
}

namespace brps3::ui {

 
 
 

 
void requestRedraw();

 
 
float animate(ImGuiID id, float target, float speed = 14.0f);
float animate(const char* strId, float target, float speed = 14.0f);
ImVec4 animateColor(ImGuiID id, const ImVec4& target, float speed = 16.0f);

enum class Level { Info, Good, Warn, Error };
 
void notify(const std::string& text, Level level = Level::Info);

 
void copyToClipboard(const std::string& text, const char* what = nullptr);

 
 
void flashSuccess(const std::string& key);

 
 
void noteQueued(const std::string& label);

 
std::string currentHost();

 
 
bool takeSearchFocus();

 
 
 

 
 
std::string titleCase(const std::string& text);
 
std::string trimZeros(const std::string& text);

void title(const char* text);                                        
void heading(const char* text, const ImVec4& color = pal::Text);     
void label(const char* text, const ImVec4& color = pal::Text);       
void muted(const char* text);                                        
void caption(const char* text, const ImVec4& color = pal::Muted);    
void wrapped(const char* text, const ImVec4& color = pal::Muted, float size = type::Small);
void labelf(const char* fmt, ...) IM_FMTARGS(1);
void mutedf(const char* fmt, ...) IM_FMTARGS(1);
void captionf(const char* fmt, ...) IM_FMTARGS(1);
void mono(const char* text, const ImVec4& color = pal::Text, float size = type::Mono);
void monof(const char* fmt, ...) IM_FMTARGS(1);
 
void fieldLabel(const char* text);

 
std::string ellipsize(const char* text, float maxWidth, FontKind kind = FontKind::Body, float em = type::Body, bool middle = false);
 
 
void cellText(const char* text, const ImVec4& color = pal::Text, FontKind kind = FontKind::Body, float em = 14, float rowHeight = 30);

 
 
bool copyable(const char* text, const char* what = nullptr, const ImVec4& color = pal::Text);
 
bool hashLabel(uint32_t value, const char* what = "hash", const ImVec4& color = pal::Text);
 
void keys(const char* chord, bool dim = false);

 
 
 

void gap(float logical = sp::S);               
void hgap(float logical = sp::S);              
float availWidth();
 
void alignRight(float width);
void divider(float spacing = sp::M);           
 
float buttonWidth(const char* label, const char* icon = nullptr, bool small = false);

 
 
 

struct CardOptions {
    const char* subtitle = nullptr;    
    const char* icon = nullptr;        
     
     
    float width = 0;
    bool raised = false;               
    ImVec4 accent = ImVec4(0, 0, 0, 0);    
    const char* badge = nullptr;       
    float pad = -1;                    
};
 
 
void beginCard(const char* id, const char* title = nullptr, const CardOptions& o = {});
void endCard();
 
bool cardHeaderButton(const char* icon, const char* tooltip, bool active = false);
 
bool cardHeaderHovered();

 
 
 

enum class Tone { Secondary, Primary, Ghost, Danger, Success };
struct ButtonOptions {
    Tone tone = Tone::Secondary;
    const char* icon = nullptr;        
    float width = 0;                   
    bool small = false;                
    const char* tooltip = nullptr;
    bool busy = false;                 
    bool spinner = false;              
    bool selected = false;             
    const char* successKey = nullptr;  
};
bool button(const char* label, const ButtonOptions& o = {});
bool primaryButton(const char* label, const char* icon = nullptr, float width = 0);
bool dangerButton(const char* label, const char* icon = nullptr, float width = 0);
 
bool iconButton(const char* id, const char* icon, const char* tooltip = nullptr, bool active = false,
                float size = 0, const ImVec4& color = pal::Muted);

 
 
 

 
 
bool toggleSwitch(const char* id, bool* on, bool pending = false);
 
void drawLight(ImDrawList* d, ImVec2 c, bool on, bool hovered, bool pending = false);

 
inline constexpr float kRowHeight = 40, kControlColumn = 200, kSubIndent = 14;

struct RowOptions {
    const char* help = nullptr;        
    bool pending = false;              
    const char* hotkey = nullptr;      
    bool sticky = false;               
    bool highlight = false;            
    bool* menu = nullptr;              
    const char* badge = nullptr;       
    int depth = 0;                     
    bool chip = false;                 
    bool listening = false;            
    bool* chipClicked = nullptr;       
    bool chipAlways = false;           
};
 
 
 
bool toggleRow(const char* id, const char* label, bool* on, const RowOptions& o = {});

 
 
enum class Edit { None, Changing, Committed, Reset };
 
 
Edit slider(const char* id, float* v, float min, float max, float step = 0, float width = 0, bool logScale = false);

struct ValueOptions {
    const char* help = nullptr;
    float min = 0, max = 1, step = 0, def = 0;
    const char* format = "%.2f";
    bool pending = false;
    const char* hotkey = nullptr;
    bool sticky = false;
    bool highlight = false;
    bool* menu = nullptr;              
    const char* badge = nullptr;       
    const char* unit = nullptr;        
    bool logScale = false;             
    bool integer = false;              
    int depth = 0;                     
};
 
 
 
Edit valueControl(const char* id, float* v, const ValueOptions& o, float width = 0);
 
 
 
Edit valueRow(const char* id, const char* label, float* v, const ValueOptions& o);

bool inputText(const char* id, std::string* s, const char* hint = nullptr, float width = 0,
               ImGuiInputTextFlags flags = 0);
bool inputMono(const char* id, std::string* s, const char* hint = nullptr, float width = 0,
               ImGuiInputTextFlags flags = 0);
 
 
bool searchField(const char* id, std::string* s, const char* hint = "Search", float width = 0, bool browseKeys = false);
 
bool inputHex(const char* id, uint32_t* v, float width = 0, int digits = 8);
 
bool inputInt(const char* id, int* v, int min, int max, float width = 0, int step = 1);
bool inputFloat(const char* id, float* v, const char* format = "%.2f", float width = 0);
bool checkbox(const char* label, bool* v);
bool combo(const char* id, int* current, const char* const items[], int count, float width = 0);

 
bool pillTabs(const char* id, const std::vector<std::string>& tabs, int* current);
 
 
bool segmented(const char* id, const std::vector<std::string>& options, int* current, float width = 0, bool tabs = false);
 
float segmentedWidth(const std::vector<std::string>& options, float padX = 14);

 
 
 
float hotkeyChipWidth(const char* binding);
bool hotkeyChip(const char* id, ImVec2 pos, const char* binding, bool listening, bool visible);

 
 
 
bool colorSwatch(const char* id, ImVec4* color);
bool colorSwatch(const char* id, uint32_t* rgb);    

 
 
 
void beginRow(const char* id, const char* label, float controlWidth = 0, const RowOptions& o = {});
void endRow();

 
 
bool actionRow(const char* id, const char* label, const char* buttonLabel, const RowOptions& o = {});

 
 
 

void pill(const char* text, const ImVec4& color, const char* icon = nullptr);    
void badge(const char* text, const ImVec4& color = pal::Muted);                  
 
 
ImVec4 badgeTone(const char* text);
float badgePillWidth(const char* text);                                          
void badgePill(const char* text);                                                
void drawBadgePill(ImDrawList* d, ImVec2 topLeft, const char* text);             
void statusDot(const ImVec4& color, bool pulse = false);
 
void statTile(const char* id, const char* caption, const std::string& value, const std::string& sub, float width,
              const ImVec4& color = pal::Text);

 
 
 

void spinner(float radius = 7, const ImVec4& color = pal::Accent, float thickness = 2);
 
void progressBar(float fraction, float width = 0, float height = 4);

 
 
 

inline constexpr const char* kConnectFirst = "Connect to the PS3 first";
inline constexpr const char* kTooNarrow = "Window too narrow: widen it to expand the sidebar";

 
 
void tooltip(const char* text);

 
class DisabledScope {
public:
    explicit DisabledScope(bool disabled, const char* reason = kConnectFirst);
    ~DisabledScope();
    DisabledScope(const DisabledScope&) = delete;
    DisabledScope& operator=(const DisabledScope&) = delete;
private:
    bool active_;
};
 
[[nodiscard]] DisabledScope requireConnection(const Ui& ui);
 
const char* disabledReason();

 
 
 

void emptyState(const char* icon, const char* title, const char* text, float height = 0);

struct HexViewOptions {
    uint32_t base = 0;                 
    const uint8_t* previous = nullptr;  
    size_t previousSize = 0;
    int bytesPerRow = 0;               
    float height = 0;                  
};
 
void hexView(const char* id, const uint8_t* data, size_t size, const HexViewOptions& o = {});

 
bool beginTable(const char* id, int columns, float height = 0, ImGuiTableFlags extra = 0, float width = 0);
void endTable();
 
void tableHeaders();
 
 
bool tableRowSelectable(const char* id, bool selected, bool* doubleClicked = nullptr, float height = 0);

 
 
 

 
 
void confirm(std::string title, std::string message, std::string confirmLabel,
             std::function<void()> onConfirm, bool danger = true);

 
 
 

 
 
 
void trackActivity(const std::string& id, std::string label, Page page, std::function<bool()> running,
                   std::function<void()> stop, const ImVec4& color = pal::Danger);

 
void openPage(Page page);
 
 
void openPage(Page page, const std::string& tab);
void openItemMenu(const std::string& itemId);    

 
 
 
 
 
 

struct RegistryItem {
    enum class Kind { Switch, Value, Action, Section } kind = Kind::Action;
    std::string id, label, help, tab, section, badge;
    Page page{};
    std::string parent;                
    std::string where;                 
    bool dangerous = false;            
    bool portCheck = false;            
    const char* icon = nullptr;        
};
std::vector<RegistryItem> registryItems();                       
bool findRegistryItem(const std::string& id, RegistryItem* out);
bool revealItem(const std::string& id);                          
bool activateItem(const std::string& id);                        
bool setItemSwitch(const std::string& id, bool on);              
bool applyItemValue(const std::string& id, float value);         
bool itemSwitchOn(const std::string& id);                        
bool itemPending(const std::string& id);                         
float itemValue(const std::string& id, float fallback);          

 
 
 

 
 
bool isDemo();
 
void demoShowSwitch(const std::string& id, bool on);

 
 
void addFrameHook(std::function<void()> fn);
 
 
void wake();

 
 
 

struct PaletteEntry {
    std::string label, where;          
    const char* icon = nullptr;
    std::string badge;                 
    std::function<void()> run;         
    std::function<void()> alt;         
    const char* altHint = nullptr;     
    int boost = 0;                     
};
 
 
using PaletteProvider = std::function<void(const std::string& query, std::vector<PaletteEntry>& out)>;
void addPaletteProvider(PaletteProvider provider);

 
 
 

 
 
 
struct SectionStyle {
    bool fullWidth = false;            
    std::string subtitle;              
    const char* icon = nullptr;        
    bool ownsActions = false;          
                                       
};
void styleSection(const std::string& sectionTitle, SectionStyle style);
const SectionStyle* findSectionStyle(const std::string& sectionTitle);

 
 
 
namespace detail {
struct Activity {
    std::string id, label;
    Page page;
    ImVec4 color;
    std::function<bool()> running;
    std::function<void()> stop;
    double since = 0;                  
};
std::vector<Activity> activities();    
void pruneActivities();                
double activityClock();                
std::vector<PaletteProvider>& paletteProviders();
void requestSearchFocus();
bool takeSearchFocusRequest();             
void setHost(const std::string* host);     
bool takeRedrawRequest();
struct Note { std::string text; Level level; };
std::vector<Note> takeNotes();
void drawModals();
bool modalOpen();
void beginFrame();                     
void runFrameHooks();                  
void setWakeHandler(void (*fn)());     
bool takeWake();                       
void setDemo(bool on);                 
std::vector<std::string>& sectionStyleConflicts();    
}   

}   
