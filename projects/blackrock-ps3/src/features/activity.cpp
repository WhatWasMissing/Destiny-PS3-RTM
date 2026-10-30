 
 
 
 
 
 
 
 
 
 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include "app/engine.h"
#include "app/registry.h"
#include "app/settings.h"
#include "core/error.h"
#include "core/runtime.h"
#include "features/activity_shared.h"
#include "game/activity_addr.h"
#include "game/activity_catalog.h"
#include "game/activity_changeworld.h"
#include "game/activity_client.h"
#include "game/activity_deploy.h"
#include "game/activity_factions.h"
#include "game/activity_monitor.h"
#include "game/activity_session.h"
#include "game/names_catalog.h"
#include "game/world_factions.h"
#include "game/world_mem.h"
#include "ui/widgets.h"

namespace brps3 {
namespace {

using namespace ui;

using activity_ui::uiDemo;
std::string hex8(uint32_t v) { return activity_ui::hx(v); }

 
 
 

constexpr const char* kMonitorLoop = "activity.monitor";

struct MonitorState {
    std::mutex m;
    bool have = false;
    activity::WorldSnapshot snap;
    activity::DescriptorRead desc;
    std::deque<activity::Transition> log;    
    std::string error;
    activity::TransitionTracker tracker;     
    std::atomic<bool> live{false};
    double lastRead = 0;
};
MonitorState& mon() {
    static MonitorState s;
    return s;
}

void readMonitor(Ctx& c) {
    c.require(activity::kMonitorFingerprints, "World-state monitor");
    c.require(activity::kDescriptorFingerprints, "Current activity (destination descriptor)");
    MonitorState& s = mon();
    world::RuntimeReader mem(c.rt());
    activity::WorldSnapshot snap;
    activity::DescriptorRead desc;
    std::string err;
    try {
        snap = activity::readWorldSnapshot(mem);
        desc = activity::readCurrentActivity(mem);
    } catch (const std::exception& e) {
        err = e.what();
    }
    std::lock_guard<std::mutex> lock(s.m);
    const auto lines = err.empty() ? s.tracker.feed(snap, ui::detail::activityClock()) : std::vector<activity::Transition>{};
    s.have = err.empty();
    s.snap = snap;
    s.desc = desc;
    s.error = err;
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) s.log.push_front(*it);
    while (s.log.size() > 200) s.log.pop_back();
    s.lastRead = ui::detail::activityClock();
}

void setMonitorLive(Ui& ui, bool on) {
    mon().live = on;
    if (on)
        ui.post("Watch world state", [](Ctx& c) {
            {
                MonitorState& s = mon();
                std::lock_guard<std::mutex> lock(s.m);
                s.tracker.reset();
            }
            c.startLoop(kMonitorLoop, 500, [](Ctx& lc) { readMonitor(lc); });
        });
    else
        ui.post("Stop watching world state", [](Ctx& c) { c.stopLoop(kMonitorLoop); });
}

std::string scenarioLabel(uint32_t tag) {
    if (!tag) return "none";
    if (const activity::NamedActivity* a = activity::catalog().byScenario(names::fromPs3Tag(tag)))
        return a->display + "  \xC2\xB7  " + hex8(tag);
    return hex8(tag);
}

std::string monitorReport() {
    MonitorState& s = mon();
    std::lock_guard<std::mutex> lock(s.m);
    std::string r = "World-state monitor\n";
    if (!s.error.empty()) return r + "error: " + s.error + "\n";
    const activity::WorldSnapshot& w = s.snap;
    r += "state: " + activity::stateLabel(w.state) + "\n";
    r += "requested: " + activity::stateLabel(w.requested) + "\n";
    r += std::string(w.aborting() ? "abort reason: " : "request parameter: ") + activity::abortReasonLabel(w.reason) + "\n";
    r += "scenario: " + scenarioLabel(w.scenario) + "\n";
    r += "transition target: " + (w.target.empty() ? std::string("(none)") : w.target) + "\n";
    r += "tag loader: " + std::string(w.loaderBusy() ? "busy" : "idle") + ", sObjects: " + std::to_string(w.sobjectCount) + "\n";
    if (s.desc.ok) r += "current activity: id " + std::to_string(s.desc.id) + " \"" + s.desc.name + "\"\n";
    else r += "current activity: " + s.desc.why + "\n";
    if (w.activeOk) r += "active activity name: \"" + w.activeName + "\"\n";
    for (auto it = s.log.rbegin(); it != s.log.rend(); ++it) r += "  " + it->text + "\n";
    return r;
}

void monitorTile(const char* caption, const std::string& value, float w, const ImVec4& color = pal::Text) {
    statTile(caption, caption, value, {}, w, color);
}

void seedMonitorDemo() {
    static bool done = false;
    if (done || !uiDemo()) return;
    done = true;
    MonitorState& s = mon();
    std::lock_guard<std::mutex> lock(s.m);
    s.have = true;
    s.snap.controllerOk = true;
    s.snap.state = 36;
    s.snap.requested = 36;
    s.snap.reason = 0;
    s.snap.scenario = 0x8108E004;
    s.snap.target = "moon_raid_1";
    s.snap.wtlCursor = 0xFFFF;
    s.snap.sobjectCount = 0x2C1;
    s.snap.clock = 918342;
    s.desc.ok = true;
    s.desc.id = 15;
    s.desc.name = "moon_raid_1";
    s.desc.fnvBefore = activity::kFnvBasis;
    auto add = [&](double t, const char* txt, bool warn = false) {
        activity::Transition tr;
        tr.time = t;
        tr.text = txt;
        tr.warn = warn;
        s.log.push_front(tr);
    };
    add(-5, "27 setup:orbit  \xE2\x86\x92  28 setup:activity_session_creation");    
    add(-3, "transition target \"moon_raid_1\"");
    add(-2, "tag loader busy (request 184)");
    add(0, "33 setup:activity_world_transition  \xE2\x86\x92  34 activity:initial_slice_set_loading");
}

void drawMonitor(Ui& ui) {
    MonitorState& s = mon();
    seedMonitorDemo();
     
    {
        auto off = requireConnection(ui);
        if (button("Refresh", {.icon = icon::Refresh, .busy = ui.busy("Read world state")}))
            ui.post("Read world state", [](Ctx& c) { readMonitor(c); });
        ImGui::SameLine(0, px(8));
        bool live = s.live.load();
        if (toggleSwitch("##monlive", &live)) setMonitorLive(ui, live);
        tooltip("Poll the world controller twice a second and log every transition (watch_launch.py)");
        ImGui::SameLine(0, px(8));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(7));
        caption(live ? "Live" : "Live Off", live ? pal::Text : pal::Muted);
    }
    ImGui::SameLine();
    alignRight(buttonWidth("Copy", icon::Copy, true));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(2));
    if (button("Copy##monrep", {.tone = Tone::Ghost, .icon = icon::Copy, .small = true, .tooltip = "Copy the readout"}))
        copyToClipboard(monitorReport(), "world-state report");
    gap(sp::M);

    activity::WorldSnapshot w;
    activity::DescriptorRead desc;
    std::deque<activity::Transition> log;
    std::string err;
    bool have;
    {
        std::lock_guard<std::mutex> lock(s.m);
        w = s.snap;
        desc = s.desc;
        log = s.log;
        err = s.error;
        have = s.have;
    }
    if (!ui.connected() && !have) {
        emptyState(icon::Console, "Connect to Watch the World", "State, Requested State, Abort Reason, Transition Target and Tag Loader.", 150);
        return;
    }
    if (!err.empty()) {
        pill(err.c_str(), pal::Danger, icon::Warning);
        gap(sp::S);
    }
    if (have) {
        const float avail = availWidth();
        const int cols = avail > px(560) ? 3 : 2;
        const float tw = std::floor((avail - px(8) * float(cols - 1)) / float(cols));
        struct Tile {
            const char* cap;
            std::string value;
            ImVec4 color;
        };
        const std::vector<Tile> tiles = {
            {"STATE", activity::stateLabel(w.state), w.state == 36 ? pal::Success : pal::Text},
            {"REQUESTED", w.requested == 0xFFFFFFFFu ? std::string("none") : activity::stateLabel(w.requested), pal::Muted},
            w.aborting() ? Tile{"ABORT REASON", activity::abortReasonLabel(w.reason), pal::Danger}
                         : Tile{"REQUEST PARAM", w.reason ? activity::abortReasonLabel(w.reason) : std::string("none"), pal::Muted},
            {"TAG LOADER", w.loaderBusy() ? ("busy #" + std::to_string(w.wtlCursor)) : std::string("idle"), w.loaderBusy() ? pal::Warning : pal::Muted},
            {"sOBJECTS (likely)", std::to_string(w.sobjectCount), pal::Muted},
            {"ENGINE CLOCK", std::to_string(w.clock), pal::Muted},
        };
        for (size_t i = 0; i < tiles.size(); ++i) {
            if (i % size_t(cols) != 0) ImGui::SameLine(0, px(8));
            else if (i) gap(sp::S);
            monitorTile(tiles[i].cap, tiles[i].value, tw, tiles[i].color);
        }
        gap(sp::M);

         
        fieldLabel("CURRENT ACTIVITY");
        gap(sp::XS);
        if (desc.ok) {
            labelf("id %u", desc.id);
            ImGui::SameLine(0, px(10));
            if (!desc.name.empty()) {
                mono(desc.name.c_str(), pal::Accent);
                ImGui::SameLine(0, px(8));
            }
            if (desc.overrideId && desc.overrideId != desc.id) {
                pill(("override " + std::to_string(desc.overrideId)).c_str(), pal::Info);
                ImGui::SameLine(0, px(6));
            }
            if (desc.fnvBefore != activity::kFnvBasis) pill("descriptor layout differs", pal::Warning);
        } else {
            caption(desc.why.c_str(), pal::Faint);
        }
        if (w.activeOk && !w.activeName.empty() && w.activeName != desc.name) {
            gap(sp::XS);
            caption(("Active Activity  \xC2\xB7  " + w.activeName).c_str(), pal::Faint);
        }
        gap(sp::XS);
        caption(("Scenario  \xC2\xB7  " + scenarioLabel(w.scenario)).c_str(), pal::Faint);
        if (!w.target.empty()) {
            gap(sp::XS);
            caption(("Transition Target  \xC2\xB7  " + w.target).c_str(), pal::Faint);
        }
    }
    if (s.live.load()) requestRedraw();

     
    gap(sp::M);
    {
        char hdr[48];
        std::snprintf(hdr, sizeof hdr, "TRANSITIONS  (%zu)", log.size());
        fieldLabel(hdr);
        if (!log.empty()) {
            ImGui::SameLine();
            alignRight(buttonWidth("Clear", icon::Delete, true));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(8));
            if (button("Clear##montr", {.tone = Tone::Ghost, .icon = icon::Delete, .small = true})) {
                std::lock_guard<std::mutex> lock(s.m);
                s.log.clear();
            }
        }
    }
    gap(sp::XS);
    if (log.empty()) {
        caption("No transitions yet. Turn Live on, then load an activity from the Director.", pal::Faint);
        return;
    }
    if (beginTable("##montrans", 2, 240, ImGuiTableFlags_BordersOuter)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("At", ImGuiTableColumnFlags_WidthFixed, px(52));
        ImGui::TableSetupColumn("Change", ImGuiTableColumnFlags_WidthStretch);
        tableHeaders();
        ImGui::PushFont(font(FontKind::Body), fontSize(FontKind::Body, type::Label));
        const double t0 = log.empty() ? 0 : log.front().time;
        for (const activity::Transition& tr : log) {
            ImGui::TableNextRow(0, px(28));
            ImGui::TableSetColumnIndex(0);
            char when[24];
            std::snprintf(when, sizeof when, "-%.0fs", std::max(0.0, t0 - tr.time));
            cellText(when, pal::Faint, FontKind::Body, type::Small, 28);
            ImGui::TableSetColumnIndex(1);
            cellText(tr.text.c_str(), tr.warn ? pal::Danger : pal::Text, FontKind::Body, type::Label, 28);
        }
        ImGui::PopFont();
        endTable();
    }
}

 
 
 

constexpr const char* kFavKey = "activity.named.favs";
constexpr const char* kRecentKey = "activity.named.recent";
constexpr const char* kSelKey = "activity.launch.selected";    
constexpr const char* kPhaseKey = "activity.launch.phase";     
constexpr const char* kLaunchId = "activity.launch_selected";

struct LaunchState {
     
    bool init = false;
    std::string filter;
    int category = 0;                    
    int catIndex = 0;                    
    int selected = -1;                   
    int phase = 0;                       
    std::unordered_set<std::string> favs;
    uint32_t orbit = 0x80B52003;
     
    std::mutex m;
    std::string result;
    bool resultOk = true;
    std::string ready;                   
    bool readyOk = false;
    bool haveLiveScenario = false;
    uint32_t liveScenario = 0;           
};
LaunchState& launch() {
    static LaunchState s;
    return s;
}

void ensureLaunch() {
    LaunchState& s = launch();
    if (s.init) return;
    s.init = true;
    Settings& st = Engine::get().settings();
    for (const std::string& f : st.list(kFavKey)) s.favs.insert(f);
    s.orbit = activity_ui::orbitTag(st);
    s.phase = std::clamp(st.integer(kPhaseKey, 0), 0, 6);
    const std::string want = st.str(kSelKey, activity::kCrotaActivity);
    const auto& named = activity::catalog().named;
    for (int i = 0; i < int(named.size()); ++i)
        if (named[size_t(i)].name == want) {
            s.selected = i;
            break;
        }
}

const activity::NamedActivity* selectedActivity() {
    LaunchState& s = launch();
    const auto& named = activity::catalog().named;
    return s.selected >= 0 && s.selected < int(named.size()) ? &named[size_t(s.selected)] : nullptr;
}

void select(int index) {
    LaunchState& s = launch();
    s.selected = index;
    if (const activity::NamedActivity* a = selectedActivity()) Engine::get().settings().set(std::string(kSelKey), a->name);
}

void setPhase(int phase) {
    launch().phase = phase;
    Engine::get().settings().set(std::string(kPhaseKey), phase);
}

 
struct LaunchPick {
    std::string name;
    uint32_t phase = activity::kNoPhase;
    uint32_t orbit = 0;
};
LaunchPick currentPick(Settings& st) {
    LaunchPick p;
    p.name = st.str(kSelKey, activity::kCrotaActivity);
    const int ph = st.integer(kPhaseKey, 0);
    if (p.name == activity::kCrotaActivity && ph >= 1 && ph <= 6) p.phase = activity::kCrotaPhases[ph - 1];
    p.orbit = activity_ui::orbitTag(st);
    return p;
}

void runLaunch(Ctx& c) {
    const LaunchPick pick = currentPick(c.settings());
    const activity::NamedActivity* a = activity::catalog().byName(pick.name);
    if (!a) throw Error("pick an activity on Console > World > Launch activity first");
    LaunchState& s = launch();
    activity::ChangeWorldResult r;
    try {
        r = activity::changeWorld(c, pick.name, pick.phase, pick.orbit);
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(s.m);
        s.result = e.what();
        s.resultOk = false;
        throw;
    }
    const std::string msg = a->display + ": " + r.summary();
    c.log("Change world " + pick.name + ": " + r.summary());
    if (r.queued()) c.settings().pushRecent(kRecentKey, pick.name, 12);
    {
        std::lock_guard<std::mutex> lock(s.m);
        s.result = msg;
        s.resultOk = r.queued() && (!r.phaseArmed || r.phaseFired);
    }
    if (!r.called) throw Error("change world refused on the game thread: " + r.refused);
    c.toast("Change world: " + msg);
}

void refreshReady(Ui& ui) {
    if (!ui.connected() || ui.busy("Check change-world readiness")) return;
    const uint32_t orbit = launch().orbit;
    ui.post(
        "Check change-world readiness",
        [orbit](Ctx& c) {
            const activity::ChangeWorldReady r = activity::readChangeWorldReady(c, orbit);
            LaunchState& s = launch();
            std::lock_guard<std::mutex> lock(s.m);
            s.ready = r.ready() ? "ready to launch (re-checked on the game thread at launch)" : r.blocker();
            s.readyOk = r.ready();
        },
        [](bool ok, const std::string& err) {
            if (ok) return;
            LaunchState& s = launch();
            std::lock_guard<std::mutex> lock(s.m);
            s.ready = err;
            s.readyOk = false;
        });
}

void useLiveScenario(Ui& ui) {
    ui.post("Read the activity scenario", [](Ctx& c) {
        world::RuntimeReader mem(c.rt());
        const uint32_t record = mem.u32(activity::addr::ACTIVITY_RECORD_PTR);
        if (!world::mapped(record, 0x274)) throw Error("no activity record yet (load into orbit first)");
        const uint32_t tag = mem.u32(record + activity::addr::RECORD_CHECKSUM);
        LaunchState& s = launch();
        std::lock_guard<std::mutex> lock(s.m);
        s.liveScenario = tag;
        s.haveLiveScenario = true;
    });
}

void toggleFav(const std::string& name) {
    LaunchState& s = launch();
    if (s.favs.count(name)) s.favs.erase(name);
    else s.favs.insert(name);
    Engine::get().settings().toggleInList(kFavKey, name);
}

void postLaunch() {
    if (!Engine::get().connected()) {
        notify(kConnectFirst, Level::Warn);
        return;
    }
    noteQueued("Launch activity");
    Engine::get().post("Launch activity", [](Ctx& c) { runLaunch(c); }, [](bool ok, const std::string&) {
        if (ok) flashSuccess("activity.launch");
    });
}

void drawLaunch(Ui& ui) {
    ensureLaunch();
    LaunchState& s = launch();
    const activity::Catalog& cat = activity::catalog();
    if (!cat.error.empty()) {
        emptyState(icon::Warning, "Activity Catalogue Unavailable", cat.error.c_str(), 150);
        return;
    }
    {    
        std::lock_guard<std::mutex> lock(s.m);
        if (s.haveLiveScenario) {
            s.haveLiveScenario = false;
            s.orbit = s.liveScenario;
            ui.settings().set(std::string(activity_ui::kOrbitKey), hex8(s.orbit));
            notify("Orbit tag set to " + hex8(s.orbit), Level::Good);
        }
    }
     
    const float searchW = std::min(px(320), availWidth() * 0.5f);
    const bool searched = searchField("##actsearch", &s.filter, "Search Activities", searchW / scale(), true);
    ImGui::PushID("##actsearch");
    const bool searchFocused = ImGui::GetActiveID() == ImGui::GetID("##input");
    ImGui::PopID();
    ImGui::SameLine(0, px(10));
     
    static const char* const kOrder[] = {"Raids", "Prison of Elders", "Strikes", "Social spaces", "Patrols", "Crucible", "Quests",
                                         "Story (Y1)", "Story (DLC)", "Cinematics", "Ambient layers", "Orbit", "Other"};
    std::vector<std::string> ordered = cat.namedCategories;
    std::stable_sort(ordered.begin(), ordered.end(), [](const std::string& a, const std::string& b) {
        auto rank = [](const std::string& n) {
            for (int i = 0; i < int(std::size(kOrder)); ++i)
                if (n == kOrder[i]) return i;
            return 99;
        };
        return rank(a) < rank(b);
    });
     
    const std::vector<std::string> quick = {"All", "Favourites", "Recent"};
    s.category = std::clamp(s.category, 0, 2);
    pillTabs("##actquick", quick, &s.category);
    ImGui::SameLine(0, px(10));
    std::vector<std::string> cats = {"Any Category"};
    for (const std::string& c0 : ordered) cats.push_back(c0);
    std::vector<const char*> catNames;
    for (const std::string& c0 : cats) catNames.push_back(c0.c_str());
    s.catIndex = std::clamp(s.catIndex, 0, int(cats.size()) - 1);
    combo("##actcat", &s.catIndex, catNames.data(), int(catNames.size()), 190);
    gap(sp::M);

     
    const auto words = activity::queryWords(s.filter);
    const std::vector<std::string> recent = ui.settings().list(kRecentKey);
    auto recentRank = [&](const std::string& name) {
        for (size_t i = 0; i < recent.size(); ++i)
            if (recent[i] == name) return int(i);
        return -1;
    };
    std::vector<int> view;
    const std::string wantCat = s.catIndex > 0 ? cats[size_t(s.catIndex)] : std::string();
    for (int i = 0; i < int(cat.named.size()); ++i) {
        const activity::NamedActivity& a = cat.named[size_t(i)];
        if (s.category == 1 && !s.favs.count(a.name)) continue;
        if (s.category == 2 && recentRank(a.name) < 0) continue;
        if (!wantCat.empty() && a.category != wantCat) continue;
        if (!activity::matchesWords(activity::lower(a.display + " " + a.name + " " + a.category), words)) continue;
        view.push_back(i);
    }
     
    std::stable_sort(view.begin(), view.end(), [&](int a, int b) {
        const activity::NamedActivity& na = cat.named[size_t(a)];
        const activity::NamedActivity& nb = cat.named[size_t(b)];
        if (s.category == 2) return recentRank(na.name) < recentRank(nb.name);
        const bool fa = s.favs.count(na.name) != 0, fb = s.favs.count(nb.name) != 0;
        if (fa != fb) return fa;
        return activity::lower(na.display) < activity::lower(nb.display);
    });
     
    if (!view.empty()) {
        auto pos = std::find(view.begin(), view.end(), s.selected);
        if (searchFocused && ImGui::IsKeyPressed(ImGuiKey_DownArrow)) select(pos == view.end() || pos + 1 == view.end() ? view.front() : *(pos + 1));
        else if (searchFocused && ImGui::IsKeyPressed(ImGuiKey_UpArrow)) select(pos == view.end() || pos == view.begin() ? view.back() : *(pos - 1));
        else if (searched && !s.filter.empty() && pos == view.end()) select(view.front());
    }

    const float avail = availWidth();
    const bool split = avail >= px(720);
    const float listW = split ? std::clamp(avail * 0.52f, px(320), px(520)) : avail;
    const float detailW = split ? avail - listW - px(16) : avail;

    ImGui::BeginGroup();
    if (beginTable("##actlist", 3, split ? 360 : 240, ImGuiTableFlags_BordersOuter, listW / scale())) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##fav", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, px(16));
        ImGui::TableSetupColumn("Activity", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Phases", ImGuiTableColumnFlags_WidthFixed, px(50));
        tableHeaders();
        ImGui::PushFont(font(FontKind::Body), fontSize(FontKind::Body, type::Label));
        std::string toggle;
        ImGuiListClipper clip;
        clip.Begin(int(view.size()), px(30));
        while (clip.Step()) {
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
                const int idx = view[size_t(r)];
                const activity::NamedActivity& a = cat.named[size_t(idx)];
                ImGui::TableNextRow(0, px(30));
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(idx);
                const float cy = ImGui::GetCursorPosY();
                if (tableRowSelectable("##r", s.selected == idx, nullptr, 30 - 2 * ImGui::GetStyle().CellPadding.y / scale())) select(idx);
                if (ImGui::IsItemHovered()) tooltip(a.name.c_str());
                ImGui::SetCursorPosY(cy - px(3));
                const bool fav = s.favs.count(a.name) != 0;
                const ImVec2 sp = ImGui::GetCursorScreenPos() - ImVec2(px(4), 0);
                ImGui::SetCursorScreenPos(sp);
                if (ImGui::InvisibleButton("##star", ImVec2(px(24), px(24)))) toggle = a.name;
                const bool sh = ImGui::IsItemHovered();
                const char* g = fav ? icon::StarFill : icon::Star;
                const ImVec2 is = textSize(FontKind::Icons, 12, g);
                drawText(ImGui::GetWindowDrawList(), FontKind::Icons, 12, sp + (ImVec2(px(24), px(24)) - is) * 0.5f,
                         u32(fav ? pal::Accent : sh ? pal::Text : pal::Decor), g);
                if (sh) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    tooltip(fav ? "Remove from favourites" : "Add to favourites");
                }
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                cellText(a.display.c_str(), s.selected == idx ? pal::Accent : pal::Text, FontKind::Body, type::Label, 30);
                ImGui::TableSetColumnIndex(2);
                cellText(a.phases ? std::to_string(a.phases).c_str() : "-", pal::Muted, FontKind::Body, type::Label, 30);
            }
        }
        ImGui::PopFont();
        if (view.empty()) {
            ImGui::TableNextRow(0, px(80));
            ImGui::TableSetColumnIndex(1);
            gap(sp::L);
            caption(s.category == 1 ? "No favourites yet: click a star." : s.category == 2 ? "Nothing launched yet." : "No activities match.", pal::Muted);
        }
        endTable();
        if (!toggle.empty()) toggleFav(toggle);
    }
    ImGui::EndGroup();

    auto drawDetail = [&] {
        const activity::NamedActivity* a = selectedActivity();
        if (!a) {
            emptyState(icon::Console, "Pick an Activity", "Change World Launches It by Name. Crota's End Can Set the Phase.", 150);
            return;
        }
        heading(a->display.c_str());
        gap(sp::XS);
        mono(a->name.c_str(), pal::Accent);
        ImGui::SameLine(0, px(8));
        pill(a->category.c_str(), pal::Muted);
        if (!a->scenarios.empty()) {
            ImGui::SameLine(0, px(6));
            pill(("scenario " + hex8(names::toPs3Tag(a->scenarios.front()))).c_str(), pal::Info);
        }
        divider(sp::M);
         
        if (a->name == activity::kCrotaActivity) {
            fieldLabel("PHASE");
            tooltip("Overrides the Crota's End starting phase (writes the event's phase hash). Fresh start uses the authored first phase.");
            gap(sp::XS + 2);
            std::vector<std::string> phases = {"None (Fresh Start)"};
            for (const char* p : activity::kCrotaPhaseLabels) phases.push_back(p);
            for (int i = 0; i < int(phases.size()); ++i) {
                if (i % 2 != 0) ImGui::SameLine(0, px(6));
                const std::string lbl = phases[size_t(i)] + "##ph" + std::to_string(i);
                if (button(lbl.c_str(), {.tone = Tone::Ghost, .small = true, .selected = s.phase == i})) setPhase(i);
            }
            divider(sp::M);
        }
         
        std::string ready, result;
        bool readyOk, resultOk;
        {
            std::lock_guard<std::mutex> lock(s.m);
            ready = s.ready;
            readyOk = s.readyOk;
            result = s.result;
            resultOk = s.resultOk;
        }
        fieldLabel("ORBIT SCENARIO TAG");
        gap(sp::XS + 2);
        if (inputHex("##orbit", &s.orbit, 120)) ui.settings().set(std::string(activity_ui::kOrbitKey), hex8(s.orbit));
        tooltip("The live activity scenario that means 'in orbit' (Xbox 80B52003). In orbit, press Use Live once to store the PS3 value. "
                "Change world only runs from orbit.");
        ImGui::SameLine(0, px(8));
        {
            auto offline = requireConnection(ui);
            if (button("Use Live", {.tone = Tone::Ghost, .icon = icon::ArrowDown, .small = true,
                                    .tooltip = "Store the current activity scenario as the orbit tag (be in orbit)",
                                    .busy = ui.busy("Read the activity scenario")}))
                useLiveScenario(ui);
            ImGui::SameLine(0, px(6));
            if (button("Check", {.tone = Tone::Ghost, .icon = icon::Refresh, .small = true, .tooltip = "Read the change-world guards (no change is made)",
                                 .busy = ui.busy("Check change-world readiness")}))
                refreshReady(ui);
        }
        if (s.orbit != activity_ui::defaultOrbitTag()) {
            gap(sp::XS);
            captionf("Catalogue default: %s", hex8(activity_ui::defaultOrbitTag()).c_str());
        }
        if (!ready.empty()) {
            gap(sp::XS);
            pill(ready.c_str(), readyOk ? pal::Success : pal::Warning, readyOk ? icon::Check : icon::Info);
        }
        gap(sp::M);
        {
            auto offline = requireConnection(ui);
            const bool busy = ui.busy("Launch activity");
            if (button("Launch", {.tone = Tone::Primary, .icon = icon::Play, .tooltip = "Queue a change-world to this activity (must be in orbit)", .busy = busy,
                                  .successKey = "activity.launch"})) {
                const std::string phaseText = a->name == activity::kCrotaActivity && s.phase >= 1 && s.phase <= 6
                                                  ? std::string(" with the ") + activity::kCrotaPhaseLabels[s.phase - 1] + " phase"
                                                  : std::string();
                confirm("Launch " + a->display,
                        "Queue a change-world to \"" + a->name + "\"" + phaseText +
                            "?\n\nThe game thread re-checks orbit, world state 36 and a free change-world slot in the same frame. Untested on the "
                            "private server (Needs check).",
                        "Launch", [] { postLaunch(); }, false);
            }
            ImGui::SameLine(0, px(8));
            if (iconButton("##launchmenu", icon::More, "Pin \"Launch Selected Activity\" or give it a hotkey", false, kControlH)) openItemMenu(kLaunchId);
        }
        if (!result.empty()) {
            gap(sp::S);
            pill(result.c_str(), resultOk ? pal::Success : pal::Danger, resultOk ? icon::Check : icon::Warning);
        }
    };

    if (split) {
        ImGui::SameLine(0, px(16));
        ImGui::BeginChild("##actdetail", ImVec2(detailW, 0), ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AutoResizeY);
        drawDetail();
        ImGui::EndChild();
    } else {
        gap(sp::M);
        drawDetail();
    }
}

void launchSelected(Ctx& c) { runLaunch(c); }

}   

 
void registerActivitySession(Registry& r);
void registerActivityDeploy(Registry& r);
void registerActivityFactions(Registry& r);
void registerActivityPortChecks(Registry& r);

BR_FEATURES(activity_world) {
    r.section({.id = "activity.monitor", .page = Page::Console, .tab = "World", .section = "Activity monitor", .order = 60, .draw = drawMonitor,
               .onDisconnect = [] {
                   MonitorState& s = mon();
                   s.live = false;
                   std::lock_guard<std::mutex> lock(s.m);
                   s.have = false;
                   s.log.clear();
                   s.tracker.reset();
               }});
    r.section({.id = "activity.launch", .page = Page::Console, .tab = "World", .section = "Launch activity", .order = 70, .draw = drawLaunch,
               .badge = kBadgeNeedsCheck});
    r.action({.id = kLaunchId, .page = Page::Console, .tab = "World", .section = "Launch activity", .label = "Launch Selected Activity",
              .help = "Queue a change-world to the activity (and Crota phase) selected on Console > World > Launch activity. Must be in orbit; the "
                      "game thread re-checks the guards in the call's frame.",
              .run = launchSelected, .dangerous = true, .badge = kBadgeNeedsCheck});
    ui::styleSection("Activity monitor", {.fullWidth = true, .icon = ui::icon::Console});
    ui::styleSection("Launch activity", {.fullWidth = true, .icon = ui::icon::Play, .ownsActions = true});
    registerActivityDeploy(r);
    registerActivityFactions(r);
    registerActivitySession(r);
    registerActivityPortChecks(r);
}

}   
