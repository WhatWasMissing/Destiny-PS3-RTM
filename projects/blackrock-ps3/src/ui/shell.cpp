 
 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include "ui/shell.h"

#include <imgui_internal.h>
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <deque>

#include "core/error.h"
#include "ui/background.h"
#include "ui/shell_internal.h"

namespace brps3::ui {

 
 
 

namespace {
ShellState g_shell;
bool g_pageReady[kPageCount] = {};
}

ShellState& shell() { return g_shell; }
bool pageReady(int page) { return page >= 0 && page < kPageCount && g_pageReady[page]; }
bool connected() { return g_shell.status.state == EngineStatus::Connected; }
Settings& settings() { return Engine::get().settings(); }
void setString(const char* k, const std::string& value) { settings().set(std::string(k), value); }

bool UiBridge::connected() const { return g_shell.status.state == EngineStatus::Connected; }
bool UiBridge::ticking() const { return g_shell.status.ticking; }
void UiBridge::post(const std::string& label, std::function<void(Ctx&)> job, std::function<void(bool, const std::string&)> done) {
    Engine::get().post(label, std::move(job), std::move(done));
}
void UiBridge::toast(const std::string& text) { Engine::get().toast(LogLine::Info, text); }
Settings& UiBridge::settings() { return Engine::get().settings(); }
bool UiBridge::busy(const std::string& label) const { return Engine::get().busy(label); }

const char* pageLabel(int page) {
    if (page == kSettingsPage) return "Settings";
    if (page >= 0 && page < int(Page::Count)) return pageName(Page(page));
    return "";
}

const char* pageIcon(int page) {
    switch (page) {
    case int(Page::Home): return icon::Home;
    case int(Page::Spawner): return icon::Spawner;
    case int(Page::HopOns): return icon::HopOns;
    case int(Page::Console): return icon::Console;
    case int(Page::Loot): return icon::Loot;
    case int(Page::LootEvents): return icon::LootEvents;
    case int(Page::Vault): return icon::Vault;
    case int(Page::Testing): return icon::Testing;
    default: return icon::Settings;
    }
}

const char* pageSubtitle(int page) {
    switch (page) {
    case int(Page::Home): return "Connection, pinned features and recent actions";
    case int(Page::Spawner): return "Spawn and despawn, live objects, world loads, squads and encounters";
    case int(Page::HopOns): return "Attach hop-ons and abilities to you, your weapon or nearby objects";
    case int(Page::Console): return "Player, weapons, movement, camera, world and session";
    case int(Page::Loot): return "Prison of Elders loop, pickup filter, grimoire and the Lighthouse";
    case int(Page::LootEvents): return "Browse, call, replay and record game loot events";
    case int(Page::Vault): return "Inventory, transfers, equip, dismantle and the live item editor";
    case int(Page::Testing): return "Runtime, recovery, research tools and the port checks";
    default: return "Connection, appearance, shortcuts and storage";
    }
}

const char* pagePlans(int page) {
    switch (page) {
    case int(Page::Spawner): return "spawning enemies, vehicles and objects around you, ported from the Xbox Spawner";
    case int(Page::HopOns): return "attaching hop-ons to you, your weapon or nearby objects, ported from the Xbox Hop-ons";
    case int(Page::Console): return "player, weapon and world switches, ported from the Xbox Console page";
    case int(Page::Loot): return "the PoE loop, pickup filter, grimoire and Lighthouse, ported from the Xbox Loot page";
    case int(Page::Vault): return "inventory, vault and item editing, ported from the Xbox Vault page";
    default: return "";
    }
}

int pageFromName(const std::string& name) {
    auto lower = [](std::string s) {
        for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const std::string n = lower(name);
    for (int p = 0; p < kPageCount; ++p)
        if (lower(pageLabel(p)) == n) return p;
    if (n == "hopons" || n == "hop-ons" || n == "hop ons") return int(Page::HopOns);
    if (n == "testing") return int(Page::Testing);
    return -1;
}

void openStateFolder() {
    const std::wstring dir = state_dir().wstring();
    ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void connectNow() {
    if (g_shell.screenshot) return;
    std::string h = g_shell.host;
    h.erase(std::remove_if(h.begin(), h.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }), h.end());
    if (h.empty()) {
        notify("Enter the PS3 address first", Level::Warn);
        return;
    }
    g_shell.host = h;
    setString(key::Host, h);
    Engine::get().connect(h);
}

void setUserScale(float scale) {
    g_shell.userScale = scale;
    g_shell.scaleDirty = true;
    settings().set(std::string(key::Scale), scale);
}

 
 
 

namespace {

 
void insetTriangle(ImVec2& a, ImVec2& b, ImVec2& c, float d) {
    auto len = [](ImVec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); };
    const float la = len(b - c), lb = len(c - a), lc = len(a - b);
    const float perim = la + lb + lc;
    if (perim <= 0) return;
    const ImVec2 in = (a * la + b * lb + c * lc) / perim;
    const float area = std::fabs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) * 0.5f;
    const float r = 2.0f * area / perim;
    const float k = r > d ? (r - d) / r : 0.0f;
    a = in + (a - in) * k;
    b = in + (b - in) * k;
    c = in + (c - in) * k;
}

 
void drawEmblem(ImDrawList* d, ImVec2 topLeft, float width, ImU32 col) {
    const float h = width * 0.86f;
    auto P = [&](float x, float y) { return ImVec2(topLeft.x + x * width, topLeft.y + y * h); };
    const ImVec2 A = P(0.504f, 0.0f), L = P(0.0f, 1.0f), R = P(1.0f, 1.0f), C = P(0.482f, 0.63f);
    const float gapW = std::max(1.0f, width * 0.045f);
    const float stroke = std::max(1.0f, width * 0.055f);
    ImVec2 a = A, l = L, c = C;
    insetTriangle(a, l, c, gapW + stroke * 0.5f);
    d->AddTriangle(a, l, c, col, stroke);
    ImVec2 a2 = A, r2 = R, c2 = C;
    insetTriangle(a2, r2, c2, gapW);
    d->AddTriangleFilled(a2, r2, c2, col);
    ImVec2 l3 = L, r3 = R, c3 = C;
    insetTriangle(l3, r3, c3, gapW + stroke * 0.5f);
    d->AddTriangle(l3, r3, c3, col, stroke);
}

ImVec4 levelColor(LogLine::Level l) {
    switch (l) {
    case LogLine::Good: return pal::Success;
    case LogLine::Warn: return pal::Warning;
    case LogLine::Error: return pal::Danger;
    default: return pal::Text;
    }
}

const char* levelIcon(LogLine::Level l) {
    switch (l) {
    case LogLine::Good: return icon::Success;
    case LogLine::Warn: return icon::Warning;
    case LogLine::Error: return icon::Error;
    default: return icon::Info;
    }
}

LogLine::Level toLogLevel(Level l) {
    switch (l) {
    case Level::Good: return LogLine::Good;
    case Level::Warn: return LogLine::Warn;
    case Level::Error: return LogLine::Error;
    default: return LogLine::Info;
    }
}

struct LogEntry {
    double time;
    LogLine::Level level;
    std::string text;
    bool continuation;
};

struct ToastItem {
    std::string text;
    LogLine::Level level;
    double born;
    double life;
    float y = -1;            
    bool dismissed = false;
    double dismissedAt = 0;
};

constexpr const char* kIconSheetTitle = "Icon sheet";

}   

void toggleLogDrawer() {
    g_shell.logOpen = !g_shell.logOpen;
    if (!g_shell.screenshot) settings().set(std::string(key::LogOpen), g_shell.logOpen);
}

void toggleSidebar() {
    g_shell.collapsed = !g_shell.collapsed;
    if (!g_shell.screenshot) settings().set(std::string(key::Collapsed), g_shell.collapsed);
}

 
 
 

struct Shell::Impl {
    LaunchOptions opts;
    std::vector<LogEntry> log;
    size_t logCursor = 0;
    uint64_t logVersion = 0;       
    int unseen = 0;                
    LogLine::Level unseenLevel = LogLine::Info;
    std::string logFilter;
    bool autoScroll = true;
    bool logNew = false;
    float logHeight = 0;
    struct {
        uint64_t version = ~0ull;
        std::string filter;
        int level = -1;
        std::vector<size_t> lines;
    } visibleCache;                
    std::deque<ToastItem> toasts;
    std::string lastJob;
    double jobSince = 0;
    double lastTextInputAt = -10;  
    double escArmedAt = -10;       
    int lastPage = -1;
    double pageChangedAt = -10;
    float pageScroll[kPageCount] = {};
    bool fullRate = true;
    int frames = 0;
    float contentScroll = 0;
    double statusChangedAt = 0;    

    explicit Impl(const LaunchOptions& o) : opts(o) {
        ShellState& S = g_shell;
        S.screenshot = o.screenshot;
        detail::setDemo(o.demo);
        S.appStartEpoch = double(std::time(nullptr));
        detail::setHost(&S.host);
        buildItems();
        for (const Item& it : items()) g_pageReady[int(it.page)] = true;
        g_pageReady[int(Page::Home)] = true;
        g_pageReady[kSettingsPage] = true;
        Settings& st = settings();
        S.host = st.str(key::Host, kDefaultHost);
        if (S.host.empty()) S.host = kDefaultHost;
        S.collapsed = o.collapsed || (!o.screenshot && st.flag(key::Collapsed, false));
        S.logOpen = o.openLog || (!o.screenshot && st.flag(key::LogOpen, false));
        S.userScale = st.number(key::Scale, 0.0f);
        setReduceMotion(st.flag(key::ReduceMotion, false));
        logHeight = std::clamp(st.number(key::LogHeight, 260.0f), 140.0f, 900.0f);
        for (int p = 0; p < int(Page::Count); ++p) S.tab[p] = st.integer(std::string(key::Tab) + std::to_string(p), 0);
        int page = o.page.empty() ? pageFromName(st.str(key::LastPage, "Home")) : pageFromName(o.page);
        S.page = page < 0 ? 0 : page;
        if (!o.tab.empty() && S.page < int(Page::Count)) {
            const auto tabs = Registry::get().tabs(Page(S.page));
            for (size_t i = 0; i < tabs.size(); ++i)
                if (_stricmp(tabs[i].c_str(), o.tab.c_str()) == 0) S.tab[S.page] = int(i);
        }

        Engine& e = Engine::get();
        e.onConnected = [] { onConnected(); };
        e.onDisconnected = [] { onDisconnected(); };
        e.onPanicked = [] { onPanicked(); };
        if (!o.screenshot && !o.demo && st.flag(key::AutoConnect, true)) e.connect(S.host);
        if (o.openPalette) openPalette();
        if (o.demo) seedDemo();
    }

     
     
     
     
    static constexpr const char* kDemoOn[] = {"patch.third_person", "movement.fly", "weapons.no_reload", "weapons.infinite_ammo",
                                              "patch.fov"};
    static constexpr const char* kDemoPending = "patch.game_speed";
    void demoStates() {
        for (const char* id : kDemoOn)
            if (const Item* it = findItem(id)) state(*it).on = true;
        if (const Item* it = findItem(kDemoPending)) state(*it).pending = true;
        if (const Item* it = findItem("movement.fly.speed")) state(*it).value = 1.6f * it->value()->def;
        if (const Item* it = findItem("patch.fov.degrees")) state(*it).value = 95.0f;
    }

    void seedDemo() {
        demoStates();
        char* dir = nullptr;
        size_t n = 0;
        const bool scratch = _dupenv_s(&dir, &n, "BRPS3_STATE_DIR") == 0 && dir && *dir;
        free(dir);
        if (scratch) {
            settings().setList(key::Pins, {"patch.third_person", "movement.fly", "movement.tp.save", "movement.tp.goto", "player.god",
                                           "weapons.kill_aura", "loot_events.catalog", "spawn.catalog"});
            settings().setList(key::Recent, {"movement.tp.save", "weapons.infinite_ammo", "patch.fov.degrees", "testing.status_report"});
            settings().setList(key::Sticky, {"weapons.infinite_ammo"});
            settings().set(std::string(key::Hotkey) + "movement.fly", int(ImGuiMod_Ctrl | ImGuiKey_F));
            settings().set(std::string(key::Hotkey) + "movement.tp.goto", int(ImGuiKey_F5));
            buildItems();
            demoStates();
        }
        Engine& e = Engine::get();
        e.log(LogLine::Info, "Connecting to 192.168.0.254...");
        e.log(LogLine::Info, "Found EBOOT.BIN (pid 0x1010200)");
        e.log(LogLine::Good, "Build 51539.16.07.26.1808.v230 verified; runtime installed");
        e.log(LogLine::Info, "[call] 4/10 emitted");
        e.log(LogLine::Warn, "Executor idle for 2 s: load into orbit or a patrol");
        e.log(LogLine::Error, "Call gather_material: no bound local player");
        e.log(LogLine::Info, "EBOOT.BIN pid 01010200; build 51539.16.07.26.1808.v230; executor ticking, heartbeat 18342\nnamed patches: none\nhooks: 1; tasks: 1; owned patches: 3");
        notify("Connected to EBOOT.BIN", Level::Good);
        seedPortChecksDemo();
    }

     
    void demoPopup() {
        char* popup = nullptr;
        size_t n = 0;
        if (_dupenv_s(&popup, &n, "BRPS3_UI_DEMO_POPUP") == 0 && popup) {
            const std::string which = popup;
            if (which == "menu") openItemMenu("movement.fly");
            if (which == "hotkey") openHotkeyCapture("movement.fly");
            if (which == "confirm")
                if (const Item* it = findItem("spawn.despawn_mine")) runAction(*it);
        }
        free(popup);
    }

     

    void ingest() {
        Engine& e = Engine::get();
        for (const LogLine& l : e.logSince(logCursor)) {
            size_t start = 0;
            bool first = true;
            while (start <= l.text.size()) {
                size_t nl = l.text.find('\n', start);
                if (nl == std::string::npos) nl = l.text.size();
                std::string part = l.text.substr(start, nl - start);
                if (!part.empty() && part.back() == '\r') part.pop_back();
                if (!part.empty() || first) log.push_back({l.time, l.level, std::move(part), !first});
                first = false;
                start = nl + 1;
            }
            if (!g_shell.logOpen && (l.level == LogLine::Warn || l.level == LogLine::Error)) {
                ++unseen;
                if (l.level > unseenLevel) unseenLevel = l.level;
            }
            logNew = true;
        }
        if (logNew) ++logVersion;
        if (log.size() > 6000) {
            log.erase(log.begin(), log.begin() + (log.size() - 5000));
            ++logVersion;
        }
        const double now = ImGui::GetTime();
        for (const Toast& t : e.takeToasts()) pushToast(t.text, t.level, now);
        for (const detail::Note& n : detail::takeNotes()) {
             
            const LogLine::Level lv = toLogLevel(n.level);
            log.push_back({now, lv, n.text, false});
            ++logVersion;
            logNew = true;
            if (!g_shell.logOpen && (lv == LogLine::Warn || lv == LogLine::Error)) {
                ++unseen;
                if (lv > unseenLevel) unseenLevel = lv;
            }
            pushToast(n.text, lv, now);
        }
    }

    void pushToast(const std::string& text, LogLine::Level level, double now) {
         
        for (ToastItem& t : toasts)
            if (!t.dismissed && t.text == text) {
                t.born = now;
                return;
            }
        const double life = level == LogLine::Error ? 6.0 : 2.5;
        toasts.push_back({text, level, now, life});
        while (toasts.size() > 5) toasts.pop_front();
    }

    void trackJob() {
        const std::string& job = g_shell.status.job;
        if (!job.empty()) {
            if (job != lastJob) {
                lastJob = job;
                jobSince = ImGui::GetTime();
            }
        } else {
            lastJob.clear();
        }
    }

    void stopJob() {
        Engine::get().cancel();
        notify("Stopping " + g_shell.status.job, Level::Info);
        escArmedAt = -10;
    }

    void shortcuts() {
        ImGuiIO& io = ImGui::GetIO();
        const double now = ImGui::GetTime();
        if (io.WantTextInput) lastTextInputAt = now;
        const bool popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) || paletteOpen();
        if (popup) return;
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_K)) openPalette();
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_L)) toggleLogDrawer();
        if (!io.WantTextInput && ImGui::IsKeyChordPressed(ImGuiKey_GraveAccent)) toggleLogDrawer();
         
        static const int order[] = {int(Page::Home), int(Page::Spawner), int(Page::HopOns), int(Page::Console),
                                    int(Page::Loot), int(Page::LootEvents), int(Page::Vault), int(Page::Testing)};
        for (int i = 0; i < int(std::size(order)); ++i)
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey(ImGuiKey_1 + i))) navigate(order[i]);
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Comma)) navigate(kSettingsPage);
         
        if (!io.WantTextInput && !io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_Slash, false)) detail::requestSearchFocus();
         
        if (!ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape, false) && now - lastTextInputAt > 0.5) {
            if (!g_shell.status.job.empty()) {
                if (now - jobSince < 3.0 || now - escArmedAt < 2.0) stopJob();
                else {
                    escArmedAt = now;
                    notify("Press Esc again to stop " + g_shell.status.job, Level::Info);
                }
            } else if (g_shell.logOpen) {
                toggleLogDrawer();
            }
        }
        handleHotkeys();
    }

     
     
     
     
     

    static constexpr int kRailOrder[] = {int(Page::Home), int(Page::Spawner), int(Page::HopOns), int(Page::Console),
                                         int(Page::Loot), int(Page::LootEvents), int(Page::Vault), int(Page::Testing)};

     
    bool railButton(ImDrawList* d, const char* id, const char* text, ImVec2 p, float w, bool sel, const ImVec4& dot, const char* tip) {
        const float h = px(32);
        ImGui::SetCursorScreenPos(p);
        const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, h), ImGuiButtonFlags_EnableNav);
        const bool hovered = ImGui::IsItemHovered();
        const ImVec4 bg = animateColor(ImGui::GetItemID(), sel ? pal::Selection : hovered ? ImVec4(1, 1, 1, 0.05f) : ImVec4(1, 1, 1, 0.0f), 22.0f);
        if (bg.w > 0.002f) d->AddRectFilled(p, p + ImVec2(w, h), u32(bg), px(7));
        const FontKind fk = sel ? FontKind::Semibold : FontKind::Body;
        const ImVec4 fg = sel ? pal::Accent : hovered ? pal::Text : pal::Muted;
        drawText(d, fk, type::Label, ImVec2(p.x + px(12), textTop(fk, type::Label, p.y + h * 0.5f)), u32(fg), text);
        if (dot.w > 0) {    
            const float t = reduceMotion() ? 0.0f : float(std::fmod(ImGui::GetTime(), 1.4) / 1.4);
            const ImVec2 dc(p.x + w - px(14), p.y + h * 0.5f);
            if (t > 0) d->AddCircleFilled(dc, px(3) + px(5) * t, u32(dot, 0.35f * (1.0f - t)), 16);
            d->AddCircleFilled(dc, px(3), u32(dot), 12);
            requestRedraw();
        }
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", tip);
        return clicked;
    }

    void rail(float width, float height) {
        ImDrawList* d = ImGui::GetWindowDrawList();
        const ImVec2 o(0, 0);
        d->AddRectFilled(o, o + ImVec2(width, height), u32(pal::Rail));
        d->AddRectFilled(ImVec2(width - 1.0f, 0), ImVec2(width, height), u32(ImVec4(1, 1, 1, 0.05f)));
        ImGui::PushItemFlag(ImGuiItemFlags_NoTabStop, true);
        const float padX = px(12);
        const float innerW = width - padX * 2;
        float y = px(20);

         
        {
            const float bh = px(22);
            const float emW = px(16);
            const ImVec2 ep(padX + px(6), y + (bh - emW * 0.86f) * 0.5f);
            drawEmblem(d, ep, emW, u32(pal::Accent));
            const char* brand = "Blackrock";
            drawText(d, FontKind::Semibold, 15, ImVec2(ep.x + emW + px(9), textTop(FontKind::Semibold, 15, y + bh * 0.5f)), u32(pal::Text), brand);
            y += bh;
        }
         
        y += px(8);
        ImGui::SetCursorScreenPos(ImVec2(padX, y));
        ImGui::PopItemFlag();
        drawRailSearch(innerW);
        ImGui::PushItemFlag(ImGuiItemFlags_NoTabStop, true);
        y += px(kControlH) + px(14);

         
        const std::vector<detail::Activity> acts = detail::activities();
        int n = 0;
        for (int p : kRailOrder) {
            ++n;
            ImVec4 dot(0, 0, 0, 0);
            for (const detail::Activity& a : acts)
                if (int(a.page) == p) dot = a.color;
            char tip[64];
            snprintf(tip, sizeof(tip), "Ctrl+%d", n);
            ImGui::PushID(p);
            if (railButton(d, "##nav", pageLabel(p), ImVec2(padX, y), innerW, g_shell.page == p, dot, tip)) navigate(p);
            ImGui::PopID();
            y += px(34);
        }

         
        const float settingsY = height - px(12) - px(32);
        const EngineStatus& s = g_shell.status;
        if (s.state != EngineStatus::Connected) {
            const char* text;
            ImVec4 dotCol;
            bool pulse = false;
            if (s.state == EngineStatus::Connecting) {
                text = "Connecting";
                dotCol = pal::Text;
                pulse = true;
            } else if (s.state == EngineStatus::Disconnecting) {
                text = "Disconnecting";
                dotCol = pal::Text;
                pulse = true;
            } else if (s.journalPending) {
                text = "Recovery Pending";
                dotCol = pal::Warning;
            } else if (!s.error.empty()) {
                text = "Connection Failed";
                dotCol = pal::Red;
            } else {
                text = "Disconnected";
                dotCol = pal::Faint;
            }
            const float lh = px(22);
            const ImVec2 lp(padX, settingsY - px(6) - lh);
            ImGui::SetCursorScreenPos(lp);
            const bool clicked = ImGui::InvisibleButton("##railstatus", ImVec2(innerW, lh));
            const bool hov = ImGui::IsItemHovered();
            const ImVec2 dc(lp.x + px(12) + px(3), lp.y + lh * 0.5f);
            if (pulse && !reduceMotion()) {
                const float t = float(std::fmod(ImGui::GetTime(), 1.2) / 1.2);
                d->AddCircleFilled(dc, px(3) + px(4) * t, u32(dotCol, 0.3f * (1.0f - t)), 16);
                requestRedraw();
            }
            d->AddCircleFilled(dc, px(3), u32(dotCol), 12);
            drawText(d, FontKind::Body, type::Caption, ImVec2(dc.x + px(9), textTop(FontKind::Body, type::Caption, dc.y)), u32(hov ? pal::Text : pal::Muted), text);
            if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("%s", !s.error.empty() && s.state == EngineStatus::Disconnected ? s.error.c_str() : "Open the connection card on Home");
            if (clicked) navigate(int(Page::Home));
        }
        if (railButton(d, "##settings", "Settings", ImVec2(padX, settingsY), innerW, g_shell.page == kSettingsPage, ImVec4(0, 0, 0, 0), "Ctrl+,"))
            navigate(kSettingsPage);
        ImGui::PopItemFlag();
    }

     
     
     
     
     

    void header(ImVec2 pos, float width, float height) {
        ImDrawList* d = ImGui::GetWindowDrawList();
        const EngineStatus& s = g_shell.status;
        const int page = g_shell.page;
        const float cy = pos.y + height * 0.5f;
        const bool isConn = s.state == EngineStatus::Connected;
        const std::string titleText = pageLabel(page);    
        const ImVec2 ts = textSize(FontKind::Semibold, type::H1, titleText.c_str());
        drawText(d, FontKind::Semibold, type::H1, ImVec2(pos.x + px(24), textTop(FontKind::Semibold, type::H1, cy)), u32(pal::Text), titleText.c_str());
        const float titleRight = pos.x + px(24) + ts.x;

         
        const float ib = px(30), ibGap = px(4);
        float x = pos.x + width - px(16);
        auto iconAt = [&](const char* id, const char* glyph, const char* tip, bool active, const ImVec4& color = pal::Muted) {
            x -= ib;
            ImGui::SetCursorScreenPos(ImVec2(x, cy - ib * 0.5f));
            const bool r = iconButton(id, glyph, tip, active, 30, color);
            x -= ibGap;
            return r;
        };
        if (isConn) {
            if (iconAt("##disconnect", icon::Power, "Disconnect: stops loops and restores every byte Blackrock wrote", false)) Engine::get().disconnect();
            const std::string hk = chordName(hotkey(kPanicId));
            const std::string tip = "Panic: all off" + (hk.empty() ? std::string() : "   " + hk) +
                                    "\nEvery switch off, changed values back to default, loops stopped, every patch, hook and task reverted. Stays connected.";
            const float px0 = x - ib;
            if (iconAt("##panic", icon::Panic, tip.c_str(), false, mix(pal::Red, pal::Muted, 0.35f))) panicNow();
            if (Engine::get().busy(Engine::kPanicJob)) {
                ImGui::SetCursorScreenPos(ImVec2(px0 + ib * 0.5f - px(8), cy - px(8)));
                spinner(7, pal::Red, 1.5f);
            }
        }
        {
            std::string tip = std::string(g_shell.logOpen ? "Hide the log" : "Show the log") + "  (Ctrl+L)";
            if (unseen > 0) tip += "\n" + std::to_string(unseen) + (unseenLevel == LogLine::Error ? " new errors or warnings" : " new warnings");
            const float lx = x - ib;
            if (iconAt("##log", icon::List, tip.c_str(), g_shell.logOpen)) toggleLogDrawer();
            if (unseen > 0 && !g_shell.logOpen)
                d->AddCircleFilled(ImVec2(lx + ib - px(7), cy - ib * 0.5f + px(7)), px(3), u32(unseenLevel == LogLine::Error ? pal::Red : pal::Warning), 12);
        }
        const std::vector<detail::Activity> acts = detail::activities();
        if ((!lastJob.empty() && ImGui::GetTime() - jobSince > 0.3) || !acts.empty()) {
            std::string tip;
            if (!lastJob.empty()) tip = "Stop " + lastJob + "  (Esc)";
            for (const detail::Activity& a : acts) tip += (tip.empty() ? "" : "\n") + std::string("Stop ") + a.label;
            const float sx = x - ib;
            if (iconAt("##stop", icon::Stop, tip.c_str(), false, pal::Text)) {
                if (!lastJob.empty()) stopJob();
                else
                    for (const detail::Activity& a : acts)
                        if (a.stop) a.stop();
            }
            if (!lastJob.empty()) drawSpinnerRing(d, ImVec2(sx + ib * 0.5f, cy), px(11));
        }
        const float iconsLeft = x;

         
        if (page < int(Page::Count)) {
            const auto tabs = Registry::get().tabs(Page(page));
            if (tabs.size() > 1) {
                int& sel = g_shell.tab[page];
                sel = std::clamp(sel, 0, int(tabs.size()) - 1);
                const int before = sel;
                const float room = iconsLeft - px(16) - (titleRight + px(24));
                float need = segmentedWidth(tabs, 16);
                if (need > room) need = segmentedWidth(tabs, 9);
                if (need <= room) {
                    ImGui::SetCursorScreenPos(ImVec2(iconsLeft - px(12) - need, cy - px(kControlH) * 0.5f));
                    segmented("##tabs", tabs, &sel, need, true);
                } else {
                     
                    std::vector<std::string> names;
                    for (const std::string& t : tabs) names.push_back(titleCase(t));
                    std::vector<const char*> ptrs;
                    for (const std::string& t : names) ptrs.push_back(t.c_str());
                    const float cw = std::max(px(120), std::min(px(240), room));
                    ImGui::SetCursorScreenPos(ImVec2(iconsLeft - px(12) - cw, cy - px(kControlH) * 0.5f));
                    combo("##tabcombo", &sel, ptrs.data(), int(ptrs.size()), cw / scale());
                }
                if (sel != before && !g_shell.screenshot) settings().set(std::string(key::Tab) + std::to_string(page), sel);
            }
        }

         
        const float sh = std::clamp(contentScroll / px(24), 0.0f, 1.0f);
        if (sh > 0.01f) d->AddRectFilled(ImVec2(pos.x, pos.y + height - 1.0f), ImVec2(pos.x + width, pos.y + height), u32(pal::Line, sh));
    }

    static void drawSpinnerRing(ImDrawList* d, ImVec2 c, float r) {
        const float t = float(ImGui::GetTime());
        const float start = t * 7.0f;
        d->PathClear();
        d->PathArcTo(c, r, start, start + 3.14159265f * 0.7f, 20);
        d->PathStroke(u32(ImVec4(1, 1, 1, 0.55f)), 0, px(1.3f));
        requestRedraw();
    }

     

    void logDrawer(ImVec2 pos, float width, float height) {
        if (height < 1) return;
        ImDrawList* d = ImGui::GetWindowDrawList();
        d->AddRectFilled(pos, pos + ImVec2(width, height), u32(rgb(0x121316, 0.97f)));
        d->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + 1.0f), u32(ImVec4(1, 1, 1, 0.08f)));
         
        ImGui::SetCursorScreenPos(pos - ImVec2(0, px(3)));
        ImGui::InvisibleButton("##logresize", ImVec2(width, px(7)));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            d->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + px(2)), u32(pal::FillActive));
        }
        if (ImGui::IsItemActive()) {
            logHeight = std::clamp(logHeight - ImGui::GetIO().MouseDelta.y / scale(), 140.0f, 900.0f);
        }
        if (ImGui::IsItemDeactivated() && !g_shell.screenshot) settings().set(std::string(key::LogHeight), logHeight);

        const float headerH = px(44);
        const float padX = px(20);
        float hy = pos.y + (headerH - px(30)) * 0.5f + px(2);
        {
            const ImVec2 ts = textSize(FontKind::Semibold, type::Nav, "Log");
            drawText(d, FontKind::Semibold, type::Nav, ImVec2(pos.x + padX, textTop(FontKind::Semibold, type::Nav, pos.y + headerH * 0.5f + px(2))), u32(pal::Text),
                     "Log");
            char cnt[32];
            snprintf(cnt, sizeof(cnt), "%zu", log.size());
            ImGui::SetCursorScreenPos(ImVec2(pos.x + padX + ts.x + px(8), pos.y + (headerH - px(18)) * 0.5f + px(2)));
            badge(cnt);
        }
         
        int level = std::clamp(settings().integer(key::LogLevel, 0), 0, 2);
        {
            ImGui::SetCursorScreenPos(ImVec2(pos.x + padX + px(90), hy - px(2)));
            static const std::vector<std::string> levels = {"All", "Warnings", "Errors"};
            ImGui::PushID("##loglevel");
            if (pillTabs("##lvl", levels, &level) && !g_shell.screenshot) settings().set(std::string(key::LogLevel), level);
            ImGui::PopID();
        }
         
        float rx = pos.x + width - padX;
        const float ib = px(30);
        rx -= ib;
        ImGui::SetCursorScreenPos(ImVec2(rx, hy));
        if (iconButton("##logclose", icon::ChevronDown, "Close (Esc / Ctrl+L)")) toggleLogDrawer();
        rx -= ib + px(2);
        ImGui::SetCursorScreenPos(ImVec2(rx, hy));
        if (iconButton("##logclear", icon::Delete, "Clear the log view")) {
            log.clear();
            ++logVersion;
        }
        rx -= ib + px(2);
        ImGui::SetCursorScreenPos(ImVec2(rx, hy));
         
        if (visibleCache.version != logVersion || visibleCache.level != level || visibleCache.filter != logFilter) {
            visibleCache.version = logVersion;
            visibleCache.level = level;
            visibleCache.filter = logFilter;
            std::vector<size_t>& out = visibleCache.lines;
            out.clear();
            out.reserve(log.size());
            std::string f = logFilter;
            for (char& c : f) c = char(std::tolower(static_cast<unsigned char>(c)));
            std::string t;
            for (size_t i = 0; i < log.size(); ++i) {
                const LogEntry& e = log[i];
                if (level == 1 && e.level != LogLine::Warn && e.level != LogLine::Error) continue;
                if (level == 2 && e.level != LogLine::Error) continue;
                if (!f.empty()) {
                    t = e.text;
                    for (char& c : t) c = char(std::tolower(static_cast<unsigned char>(c)));
                    if (t.find(f) == std::string::npos) continue;
                }
                out.push_back(i);
            }
        }
        const std::vector<size_t>& visible = visibleCache.lines;
        if (iconButton("##logcopy", icon::Copy, "Copy the visible lines")) {
            std::string all;
            for (size_t i : visible) all += log[i].text + "\n";
            copyToClipboard(all, nullptr);
        }
        rx -= ib + px(2);
        ImGui::SetCursorScreenPos(ImVec2(rx, hy));
        if (iconButton("##logauto", icon::Follow, autoScroll ? "Following new lines (click to stop)" : "Follow new lines", autoScroll))
            autoScroll = !autoScroll;
        const float fw = std::min(px(260), std::max(px(120), rx - (pos.x + padX + px(360))));
        rx -= fw + px(8);
        ImGui::SetCursorScreenPos(ImVec2(rx, hy - px(2)));
        searchField("##logfilter", &logFilter, "Filter Log", fw / scale());

         
        const ImVec2 lp(pos.x, pos.y + headerH + px(4));
        ImGui::SetCursorScreenPos(lp);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padX, px(4)));
        ImGui::BeginChild("##loglines", ImVec2(width, std::max(1.0f, height - headerH - px(4))), ImGuiChildFlags_AlwaysUseWindowPadding);
        ImDrawList* ld = ImGui::GetWindowDrawList();
        const float lineH = textSize(FontKind::Body, type::Compact, "Ag").y + px(5);
        if (visible.empty()) {
            const char* msg = log.empty() ? "Nothing Logged Yet" : "No Lines Match the Filter";
            const ImVec2 p = ImGui::GetCursorScreenPos();
            drawText(ld, FontKind::Body, type::Small, p + ImVec2(0, px(6)), u32(pal::Faint), msg);
            ImGui::Dummy(ImVec2(1, px(24)));
        } else {
            ImGuiListClipper clip;
            clip.Begin(int(visible.size()), lineH);
            const float timeW = textSize(FontKind::Mono, type::Caption, "00:00:00").x;
            while (clip.Step()) {
                for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
                    const LogEntry& e = log[visible[size_t(r)]];
                    const ImVec2 p = ImGui::GetCursorScreenPos();
                    ImGui::PushID(r);
                    const bool dbl = ImGui::InvisibleButton("##line", ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), lineH)) &&
                                     ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                    const bool hov = ImGui::IsItemHovered();
                    ImGui::PopID();
                    if (hov) ld->AddRectFilled(p - ImVec2(px(8), 0), p + ImVec2(ImGui::GetContentRegionAvail().x + px(8), lineH), u32(pal::Fill), px(4));
                    if (dbl) copyToClipboard(e.text, "log line");
                    const ImVec4 lc = levelColor(e.level);
                    const float ty = textTop(FontKind::Body, type::Compact, p.y + lineH * 0.5f);
                    const float baseline = std::floor(ty + ascent(FontKind::Body, type::Compact) + 0.5f);
                    if (!e.continuation) {
                        const std::time_t t = std::time_t(g_shell.appStartEpoch + e.time);
                        std::tm tm{};
                        localtime_s(&tm, &t);
                        char tb[16];
                        strftime(tb, sizeof(tb), "%H:%M:%S", &tm);
                        drawText(ld, FontKind::Mono, type::Caption, ImVec2(p.x, baselineTop(FontKind::Mono, type::Caption, baseline)), u32(pal::Faint), tb);
                        const char* gi = levelIcon(e.level);
                        const ImVec2 is = textSize(FontKind::Icons, 12, gi);
                        drawText(ld, FontKind::Icons, 12, ImVec2(p.x + timeW + px(12), p.y + (lineH - is.y) * 0.5f),
                                 u32(e.level == LogLine::Info ? pal::Decor : lc), gi);
                    }
                    drawText(ld, FontKind::Body, type::Compact, ImVec2(p.x + timeW + px(34), baselineTop(FontKind::Body, type::Compact, baseline)),
                             u32(e.level == LogLine::Info ? pal::Soft : lc), e.text.c_str());
                }
            }
            if (autoScroll && logNew) ImGui::SetScrollHereY(1.0f);
        }
        logNew = false;
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

     
     
     
     

    void drawToasts(float centerX, float bottom) {
        const double now = ImGui::GetTime();
        while (toasts.size() > 1) toasts.pop_front();
        if (toasts.empty()) return;
        ToastItem& t = toasts.back();
        if (!t.dismissed && now - t.born > t.life) {
            t.dismissed = true;
            t.dismissedAt = now;
        }
        if (t.dismissed && now - t.dismissedAt > 0.25) {
            toasts.clear();
            return;
        }
        requestRedraw();
        ImDrawList* d = ImGui::GetForegroundDrawList();
        const float maxW = std::min(px(640), ImGui::GetMainViewport()->Size.x - px(48));
        const std::string text = ellipsize(t.text.c_str(), maxW - px(24), FontKind::Body, type::Label);
        const ImVec2 ts = textSize(FontKind::Body, type::Label, text.c_str());
        const ImVec2 size(ts.x + px(24), ts.y + px(14));
        const float appear = reduceMotion() ? 1.0f : std::clamp(float(now - t.born) / 0.15f, 0.0f, 1.0f);
        const float vanish = t.dismissed ? std::clamp(1.0f - float(now - t.dismissedAt) / 0.25f, 0.0f, 1.0f) : 1.0f;
        const float a = std::min(appear, vanish);
        const ImVec2 p(std::floor(centerX - size.x * 0.5f), std::floor(bottom - size.y + (1.0f - appear) * px(6)));
        const ImRect r(p, p + size);
        const bool hov = r.Contains(ImGui::GetIO().MousePos);
        if (hov && !t.dismissed) t.born = std::max(t.born, now - t.life + 1.0);    
        if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !t.dismissed) {
            t.dismissed = true;
            t.dismissedAt = now;
        }
        auto A = [&](ImVec4 col) { return ImGui::ColorConvertFloat4ToU32(ImVec4(col.x, col.y, col.z, col.w * a)); };
        d->AddRectFilled(p + ImVec2(0, px(3)), p + size + ImVec2(0, px(5)), A(ImVec4(0, 0, 0, 0.25f)), px(9));
        d->AddRectFilled(p, p + size, A(pal::RaisedFill), px(8));
        d->AddRect(p, p + size, A(ImVec4(1, 1, 1, 0.08f)), px(8), 0, 1.0f);
        d->AddText(font(FontKind::Body), px(fontSize(FontKind::Body, type::Label)), ImVec2(p.x + px(12), p.y + px(7)),
                   A(t.level == LogLine::Error ? pal::Red : pal::Text), text.c_str());
        if (hov && text != t.text) ImGui::SetTooltip("%s", t.text.c_str());
    }

     

    void iconSheet() {
        ImDrawList* d = ImGui::GetWindowDrawList();
        const ImVec2 o = ImGui::GetCursorScreenPos();
        const int perRow = 32;
        const float cell = px(46);
        drawText(d, FontKind::Semibold, type::H2, o, u32(pal::Text), kIconSheetTitle);
        for (int i = 0; i < 0x300; ++i) {
            const unsigned cp = opts.iconSheet + unsigned(i);
            char utf8[4] = {char(0xE0 | (cp >> 12)), char(0x80 | ((cp >> 6) & 0x3F)), char(0x80 | (cp & 0x3F)), 0};
            const ImVec2 p(o.x + float(i % perRow) * cell, o.y + px(30) + float(i / perRow) * cell);
            drawText(d, FontKind::Icons, 18, p + ImVec2(px(12), px(4)), u32(pal::Text), utf8);
            char lab[8];
            snprintf(lab, sizeof(lab), "%X", cp);
            drawText(d, FontKind::Mono, 9, p + ImVec2(px(8), px(28)), u32(pal::Muted), lab);
        }
        ImGui::Dummy(ImVec2(cell * perRow, px(30) + cell * 25));
    }

     

    void frame() {
        ShellState& S = g_shell;
        detail::beginFrame();
        S.now = ImGui::GetTime();
        Engine::get().pump();
        {
            const EngineStatus prev = S.status;
            S.status = Engine::get().status();
            const EngineStatus& s = S.status;
            if (s.state != prev.state || s.job != prev.job || s.queued != prev.queued || s.loops != prev.loops || s.error != prev.error ||
                s.journalPending != prev.journalPending)
                statusChangedAt = S.now;
        }
        if (opts.demo) {
            EngineStatus& s = S.status;
            s.state = EngineStatus::Connected;
            s.host = S.host;
            s.process = "EBOOT.BIN";
            s.build = "51539.16.07.26.1808.v230";
            s.ticking = true;
            s.fps = 30;
            s.heartbeat = 18342 + uint32_t(S.now * 30);
            s.patches = 3;
            s.hooks = 1;
            s.tasks = 1;
            s.loops = 1;
            s.latencyMs = 41;
            s.error.clear();
        }
        ingest();
        trackJob();
         
        detail::pruneActivities();
        detail::runFrameHooks();
        if (opts.demo && frames == 1) demoPopup();
        shortcuts();
         
        bool restoreScroll = false;
        if (S.page != lastPage) {
            if (lastPage >= 0) pageScroll[lastPage] = contentScroll;
            lastPage = S.page;
            pageChangedAt = S.now;
            restoreScroll = true;
        }

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->Pos);
        ImGui::SetNextWindowSize(vp->Size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::SetNextWindowBgAlpha(0.0f);    
        ImGui::Begin("##main", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar(3);
        const float W = vp->Size.x, H = vp->Size.y;

        if (opts.iconSheet) {
            ImGui::SetCursorScreenPos(ImVec2(px(16), px(16)));
            iconSheet();
            ImGui::End();
            return;
        }

        S.sidebarForced = false;
        const float railW = px(176);
        const float logTarget = S.logOpen ? std::min(px(logHeight), H * 0.6f) : 0.0f;
        const float logH = animate(ImGui::GetID("##logH"), logTarget, 18.0f);
        const float topH = px(64);
        const float contentX = railW;
        const float contentW = W - railW;
        S.contentWidth = contentW;

        drawBackground(ImGui::GetWindowDrawList(), ImVec2(0, 0), ImVec2(W, H));
        rail(railW, H);
        header(ImVec2(contentX, 0), contentW, topH);

         
        const float contentH = std::max(1.0f, H - topH - logH);
        ImGui::SetCursorScreenPos(ImVec2(contentX, topH));
        const float appear = reduceMotion() ? 1.0f : std::clamp(float(S.now - pageChangedAt) / 0.16f, 0.0f, 1.0f);
        if (appear < 1.0f) requestRedraw();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(sp::Page), 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, px(6));
        if (restoreScroll) ImGui::SetNextWindowScroll(ImVec2(0, pageScroll[S.page]));
        ImGui::BeginChild("##content", ImVec2(contentW, contentH), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened);
        ImGui::PopStyleVar(2);
        {
             
             
            ImGuiWindow* cw = ImGui::GetCurrentWindow();
            const float rightEdge = cw->Pos.x + cw->Size.x - px(sp::Page);
            cw->WorkRect.Max.x = rightEdge;
            cw->ContentRegionRect.Max.x = rightEdge;
        }
        contentScroll = ImGui::GetScrollY();
        {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.35f + 0.65f * appear);
            const float slide = (1.0f - appear) * (1.0f - appear) * px(8);
            if (slide > 0.5f) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + slide);
            ImGui::PushID(S.page);
            switch (S.page) {
            case int(Page::Home): drawHomePage(); break;
            case kSettingsPage: drawSettingsPage(); break;
            default: drawRegistryPage(S.page); break;
            }
            ImGui::PopID();
            ImGui::Dummy(ImVec2(1, px(20)));
            ImGui::PopStyleVar();
        }
        ImGui::EndChild();

        logDrawer(ImVec2(contentX, H - logH), contentW, logH);
         
        if (detail::takeSearchFocusRequest()) openPalette();
        if (S.logOpen) {
            unseen = 0;
            unseenLevel = LogLine::Info;
        }
        ImGui::End();

        drawToasts(W * 0.5f, H - logH - px(16));
        drawPalette();
        drawItemPopups();
        detail::drawModals();

        fullRate = detail::takeRedrawRequest() || S.status.state == EngineStatus::Connecting ||
                   S.status.state == EngineStatus::Disconnecting || !S.status.job.empty() || !toasts.empty() ||
                   ImGui::GetIO().WantTextInput || !detail::activities().empty();
        ++frames;
    }
};

Shell::Shell(const LaunchOptions& o) : d_(std::make_unique<Impl>(o)) {}
Shell::~Shell() = default;
void Shell::frame() { d_->frame(); }
bool Shell::wantsFullRate() const { return d_->fullRate; }

bool Shell::needsFrame() const {
    const EngineStatus s = Engine::get().status();
     
     
    if (s.state != EngineStatus::Disconnected || ImGui::GetTime() - d_->statusChangedAt < 3.0) return true;
    const EngineStatus& last = g_shell.status;
    if (s.state != last.state || s.job != last.job || s.queued != last.queued || s.loops != last.loops || s.error != last.error ||
        s.journalPending != last.journalPending || s.host != last.host)
        return true;
    size_t cursor = d_->logCursor;    
    if (!Engine::get().logSince(cursor).empty()) return true;
    return !detail::activities().empty();
}

unsigned Shell::idleWaitMs() const { return g_shell.status.state == EngineStatus::Disconnected ? 1000u : 250u; }
float Shell::userScale() const { return g_shell.userScale; }

bool Shell::takeScaleChange(float* userScale) {
    if (!g_shell.scaleDirty) return false;
    g_shell.scaleDirty = false;
    if (userScale) *userScale = g_shell.userScale;
    return true;
}

}   
