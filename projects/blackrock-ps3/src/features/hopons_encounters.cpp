 
 
 
 
 
 
 
 
 
 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "app/engine.h"
#include "app/registry.h"
#include "app/settings.h"
#include "core/error.h"
#include "core/runtime.h"
#include "features/hopons_shared.h"
#include "game/hopons_addr.h"
#include "game/hopons_catalog.h"
#include "game/hopons_scripts.h"
#include "game/hopons_sequence.h"
#include "game/names_catalog.h"
#include "game/world.h"
#include "ui/widgets.h"

namespace brps3 {

 
 
 
namespace hoponsui {

double nowSeconds() { return ui::detail::activityClock(); }

Flow::Flow() : width(ui::availWidth()) {}

void Flow::next(float w, float gap) {
    if (first) {
        first = false;
        used = w;
        return;
    }
    if (used + gap + w <= width) {
        ImGui::SameLine(0, gap);
        used += gap + w;
    } else {
        used = w;
    }
}

int64_t unixNow() { return int64_t(std::time(nullptr)); }

std::string ago(double s) {
    if (s < 5) return "just now";
    if (s < 60) return std::to_string(int(s)) + " s ago";
    if (s < 3600) return std::to_string(int(s / 60)) + " min ago";
    return std::to_string(int(s / 3600)) + " h ago";
}

std::string clockTime(int64_t t) {
    if (!t) return "-";
    std::tm tm{};
    const std::time_t tt = std::time_t(t);
    localtime_s(&tm, &tt);
    char b[16];
    std::snprintf(b, sizeof b, "%02d:%02d", tm.tm_hour, tm.tm_min);
    return b;
}

ScanState& scanState() {
    static ScanState s;
    return s;
}

void discover(Ctx& c) {
    c.require(world::kFingerprints, "Encounters");
    ScanState& S = scanState();
    if (S.running.exchange(true)) throw Error("a discovery is already running");
    struct Done {
        ~Done() {
            ScanState& T = scanState();
            T.running = false;
            std::lock_guard<std::mutex> lock(T.m);
            T.progress.clear();
            T.fraction = -1;
        }
    } done;
    const auto& cat = hopons::encounterCatalog();
    if (!cat.error.empty()) throw Error("encounter catalogue unavailable: " + cat.error);
    world::RuntimeReader mem(c.rt());
    const double t0 = nowSeconds();
    auto scan = hopons::scanScripts(
        mem, cat, [&c] { return c.cancelled(); },
        [&S](uint32_t d, uint32_t total, size_t found) {
            std::lock_guard<std::mutex> lock(S.m);
            S.progress = "Page " + std::to_string(std::min(d + 1, total)) + " of " + std::to_string(total) + "  \xC2\xB7  " + std::to_string(found) + " live instances";
            S.fraction = total ? float(d) / float(total) : -1.0f;
        });
    const double secs = nowSeconds() - t0;
    char msg[200];
    std::snprintf(msg, sizeof msg, "%zu live instances on %u pages (%u candidates, %u unmatched) in %.1f s%s", scan.instances.size(), scan.pages, scan.candidates,
                  scan.unmatched, secs, scan.cancelled ? ", stopped early" : "");
    c.log(std::string("[encounters] ") + msg + ", " + std::to_string(scan.reads) + " reads");
    armTimerRecovery(c);
    armSwapRecovery(c);
    {
        std::lock_guard<std::mutex> lock(S.m);
        S.scan = std::move(scan);
        S.have = true;
        S.at = nowSeconds();
        ++S.revision;
        S.status = msg;
        S.statusOk = true;
    }
    c.toast(std::string("Encounters: ") + msg);
}

void postDiscover(Ui& ui) {
    if (ui.busy(kDiscoverJob) || scanState().running.load()) return;
    if (!ui.connected()) {
        ui::notify(std::string(ui::kConnectFirst) + " to discover instances", ui::Level::Warn);
        return;
    }
    ui::noteQueued(kDiscoverJob);
    ui::trackActivity("hopons.discover", "Discovering encounter instances", Page::Spawner,
                      [] { return scanState().running.load() || Engine::get().busy(kDiscoverJob); }, [] { Engine::get().cancel(); }, ui::pal::Accent);
    ui.post(kDiscoverJob, discover);
}

void replaceInstance(uint32_t address, const hopons::ScriptInstance* fresh) {
    ScanState& S = scanState();
    std::lock_guard<std::mutex> lock(S.m);
    auto& v = S.scan.instances;
    const auto it = std::find_if(v.begin(), v.end(), [address](const hopons::ScriptInstance& i) { return i.ref.address == address; });
    if (it == v.end()) return;
    if (fresh) *it = *fresh;
    else v.erase(it);
    ++S.revision;
}

std::string journalPath(const char* name) {
    const auto dir = state_dir() / "hopons";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return (dir / name).string();
}

std::string readJournal(const char* name) {
    std::ifstream f(journalPath(name), std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void writeJournal(const char* name, const std::string& text) {
    const std::string path = journalPath(name), tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) throw Error("cannot create " + tmp);
        f << text;
        f.flush();
        if (!f) throw Error("cannot write " + tmp);
    }
    if (!MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw Error("cannot replace " + path);
}

}   

 
void hoponsDrawAiDetail(Ui& ui, const hopons::ScriptInstance& s, const char* idScope);

namespace {

using namespace ui;
using hopons::EncounterDef;
using hopons::ScriptEdit;
using hopons::ScriptInstance;
using hoponsui::ago;
using hoponsui::hx;
using hoponsui::scanState;

constexpr const char* kFavKey = "hopons.encounters.favorites";
constexpr const char* kSelectedKey = "hopons.encounters.selected";
constexpr const char* kJournal = "script-recovery.tsv";
constexpr const char* kScriptsSection = "Encounter scripts";
constexpr const char* kEditsSection = "Timer edits";
constexpr const char* kDiscoverId = "hopons.encounters.discover";
constexpr const char* kRestoreAllId = "hopons.encounters.restore_timers";
constexpr const char* kRestoreAllLabel = "Restore All Timer Edits";
constexpr float kRowH = 30;

struct EditRow {
    uint32_t id = 0;
    ScriptEdit e;
};

struct State {
     
    bool loaded = false;
    const hopons::EncounterCatalog* cat = nullptr;
    std::vector<int> view;
    bool viewDirty = true;
    std::string filter;
    int kind = 0;                        
    int cls = 0;                         
    std::vector<std::pair<uint32_t, std::string>> classes;
    std::vector<std::string> classLabels;
    std::unordered_set<uint32_t> favs;
    uint32_t selectedRoot = 0, selectedAddress = 0;
    bool scrollToSelected = false, revealDetail = false, lastSearchActive = false;
    float detailH = 0;
    uint64_t seenRevision = ~0ull;
    std::map<uint32_t, size_t> liveCounts;
    bool renaming = false;
    std::string renameText;
    std::map<uint32_t, double> values;   
    std::map<uint32_t, std::string> texts;    
    uint32_t valuesFor = 0;
    ImGuiTableColumnSortSpecs sortSpec{};
    bool sorted = false;

     
    std::mutex m;
    std::vector<EditRow> edits;          
    uint32_t nextEdit = 1;
    bool journalLoaded = false;
    std::string journalError;
    std::vector<std::string> report;
    std::string reportTitle;
    uint32_t reportFor = 0;
    std::string result;
    bool resultOk = true;
    uint32_t resultFor = 0;
};

State& st() {
    static State s;
    return s;
}

struct SessionHooks {
    bool disconnectHooked = false;
};

 
 
 

std::string rootName(const EncounterDef& d) {
    if (const auto own = names::Labels::get().find(d.root); own && !own->name.empty()) return own->name;
    return d.name.empty() ? "(unnamed " + st().cat->kind(d.cls) + ")" : d.name;
}

 
 
void seedDemoScan() {
    const auto& cat = hopons::encounterCatalog();
    auto put = [](std::vector<uint8_t>& b, uint32_t off, uint32_t v) {
        for (int i = 0; i < 4; ++i) b[off + uint32_t(i)] = uint8_t(v >> (24 - 8 * i));
    };
    auto make = [&](uint32_t root, uint32_t address) {
        ScriptInstance i;
        if (const auto* d = cat.byRoot(root)) {
            i.ref.def = *d;
            i.ref.address = address;
            i.bytes.assign(d->size, 0);
            put(i.bytes, 0, d->blob);
            put(i.bytes, 4, d->binding);
            put(i.bytes, 8, d->bindingClass);
        }
        return i;
    };
    std::vector<ScriptInstance> list;
    if (ScriptInstance t = make(0x80B46013, 0x60878D10); t.ref.address) {
        t.bytes[0x108] = 1;
        put(t.bytes, 0x10C, 0);
        put(t.bytes, 0x110, 120000);
        put(t.bytes, 0x114, 0);
        put(t.bytes, 0x118, 60000);
        put(t.bytes, 0x11C, 0x0012D687);
        put(t.bytes, 0x120, world::floatBits(1.0f));
        t.joined = t.inputStable = t.headerVerified = true;
        t.input.assign(36, 0);
        t.inputStatus = "Typed upstream input joined; stable on repeat";
        list.push_back(t);
    }
    if (ScriptInstance q = make(0x80C7A3DF, 0x60879000); q.ref.address) {
        put(q.bytes, 0x108, 0x0001F400);
        put(q.bytes, 0x10C, 0x01000000);
        put(q.bytes, 0x128, 0x0004A100);
        put(q.bytes, 0x12C, 0x3D6000A5);
        q.bytes[0x130] = 2;
        q.inputStatus = "Input not joined yet: refresh this instance to read it";
        list.push_back(q);
    }
    if (ScriptInstance p = make(0x80C7A04F, 0x6087A000); p.ref.address) {
        put(p.bytes, 0x108, 0x11223344);
        put(p.bytes, 0x174, 0x40A2000C);
        p.bytes[0x14D] = 1;
        put(p.bytes, 0x9FC, 0xFFFFFFFFu);
        p.input = {0x11, 0x22, 0x33, 0x44, 0, 0, 1, 0};
        p.joined = p.inputStable = p.headerVerified = true;
        p.inputStatus = "Typed upstream input joined; stable on repeat";
        list.push_back(p);
    }
    if (ScriptInstance g = make(0x80C7A04E, 0x6087B000); g.ref.address) {
        put(g.bytes, 0x1D8, 3);
        for (uint32_t k = 0; k < 3; ++k) {
            put(g.bytes, 0x1DC + k * 8, 0x00000101 + k);
            put(g.bytes, 0x1E0 + k * 8, 0x40A2000C + k * 0x10000);
        }
        g.input.assign(0x48, 0);
        g.input[0x40] = 1;
        g.input[0x41] = 0x02;
        g.joined = g.inputStable = true;
        g.inputStatus = "Typed upstream input joined; stable on repeat (record header unconfirmed on PS3)";
        list.push_back(g);
    }
    auto& S = scanState();
    std::lock_guard<std::mutex> lock(S.m);
    S.scan.instances = std::move(list);
    S.scan.pages = 41;
    S.scan.candidates = 88;
    S.scan.unmatched = 2;
    S.have = true;
    S.at = hoponsui::nowSeconds() - 30;
    S.status = "4 live instances on 41 pages (88 candidates, 2 unmatched) in 3.4 s";
    ++S.revision;
}

bool ensureCatalog() {
    State& s = st();
    if (s.loaded) return s.cat != nullptr;
    s.loaded = true;
    const auto& cat = hopons::encounterCatalog();
    if (!cat.error.empty() || cat.rows().empty()) return false;
    s.cat = &cat;
    s.classes = cat.classes();
    std::stable_sort(s.classes.begin(), s.classes.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    s.classLabels.push_back("All Controller Kinds");
    for (const auto& [c, k] : s.classes) s.classLabels.push_back(k);
    Settings& settings = Engine::get().settings();
    for (const std::string& h : settings.list(kFavKey)) {
        try {
            s.favs.insert(world::parseHex(h));
        } catch (...) {
        }
    }
    try {
        const std::string last = settings.str(kSelectedKey, "");
        if (!last.empty()) s.selectedRoot = world::parseHex(last);
    } catch (...) {
    }
    char* demo = nullptr;
    size_t n = 0;
    if (_dupenv_s(&demo, &n, "BRPS3_UI_DEMO") == 0 && demo && demo[0] == '1' && !s.selectedRoot) {
        s.selectedRoot = 0x80B46013;    
        s.favs.insert(0x80B46013);
        seedDemoScan();
    }
    free(demo);
    s.scrollToSelected = s.selectedRoot != 0;
    return true;
}

void select(uint32_t root) {
    State& s = st();
    if (s.selectedRoot == root) return;
    s.selectedRoot = root;
    s.selectedAddress = 0;
    s.scrollToSelected = true;
    s.renaming = false;
    s.values.clear();
    s.texts.clear();
    Engine::get().settings().set(std::string(kSelectedKey), hx(root));
}

void toggleFavourite(uint32_t root) {
    State& s = st();
    if (s.favs.count(root)) s.favs.erase(root);
    else s.favs.insert(root);
    Engine::get().settings().toggleInList(kFavKey, hx(root));
    if (s.kind == 2) s.viewDirty = true;
}

void syncScan() {
    State& s = st();
    auto& S = scanState();
    std::lock_guard<std::mutex> lock(S.m);
    if (S.revision == s.seenRevision) return;
    s.seenRevision = S.revision;
    s.liveCounts.clear();
    for (const auto& i : S.scan.instances) ++s.liveCounts[i.ref.def.root];
    s.viewDirty = true;
}

void rebuildView() {
    State& s = st();
    s.view.clear();
    if (!s.cat) return;
    const auto words = hopons::searchWords(s.filter);
    const uint32_t wantCls = s.cls > 0 && size_t(s.cls) <= s.classes.size() ? s.classes[size_t(s.cls - 1)].first : 0;
    const auto& rows = s.cat->rows();
    for (int i = 0; i < int(rows.size()); ++i) {
        const EncounterDef& d = rows[size_t(i)];
        if (wantCls && d.cls != wantCls) continue;
        if (s.kind == 1 && !s.liveCounts.count(d.root)) continue;
        if (s.kind == 2 && !s.favs.count(d.root)) continue;
        if (!words.empty()) {
            std::string hay = s.cat->hay(size_t(i));
            if (const auto own = names::Labels::get().find(d.root)) hay += " " + hopons::lowerCopy(own->name + " " + own->notes);
            if (!hopons::matchesAll(hay, words)) continue;
        }
        s.view.push_back(i);
    }
     
    const bool asc = !s.sorted || s.sortSpec.SortDirection != ImGuiSortDirection_Descending;
    const ImGuiID col = s.sorted ? s.sortSpec.ColumnUserID : 99;
    std::unordered_map<int, std::string> keys;    
    if (col == 1 || col == 2)
        for (int i : s.view) keys[i] = col == 1 ? hopons::lowerCopy(rootName(rows[size_t(i)])) : s.cat->kind(rows[size_t(i)].cls);
    std::stable_sort(s.view.begin(), s.view.end(), [&](int a, int b) {
        const EncounterDef& x = rows[size_t(a)];
        const EncounterDef& y = rows[size_t(b)];
        int c = 0;
        auto live = [&](uint32_t root) {
            const auto it = s.liveCounts.find(root);
            return it == s.liveCounts.end() ? 0 : int(it->second);
        };
        switch (col) {
        case 0: c = int(s.favs.count(y.root)) - int(s.favs.count(x.root)); break;
        case 1:
        case 2: c = keys[a].compare(keys[b]); break;
        case 3: c = x.root < y.root ? -1 : x.root > y.root ? 1 : 0; break;
        case 4: c = live(x.root) - live(y.root); break;
        default: c = live(y.root) - live(x.root); break;    
        }
        return asc ? c < 0 : c > 0;
    });
    s.viewDirty = false;
}

 
 
 

void setResult(uint32_t address, bool ok, const std::string& text) {
    State& S = st();
    std::lock_guard<std::mutex> lock(S.m);
    S.resultFor = address;
    S.resultOk = ok;
    S.result = text;
}

void setReport(uint32_t address, const std::string& title, std::vector<std::string> lines) {
    State& S = st();
    std::lock_guard<std::mutex> lock(S.m);
    S.reportFor = address;
    S.reportTitle = title;
    S.report = std::move(lines);
}

std::optional<ScriptInstance> instanceAt(uint32_t address) {
    auto& S = scanState();
    std::lock_guard<std::mutex> lock(S.m);
    for (const auto& i : S.scan.instances)
        if (i.ref.address == address) return i;
    return std::nullopt;
}

ScriptInstance requireInstance(uint32_t address) {
    auto i = instanceAt(address);
    if (!i) throw Error("that instance is gone from the scan: discover again");
    return *i;
}

void refreshAfter(world::Reader& mem, const ScriptInstance& s) {
    ScriptInstance fresh;
    std::string why;
    if (hopons::refreshScript(mem, s.ref, fresh, &why)) hoponsui::replaceInstance(s.ref.address, &fresh);
    else hoponsui::replaceInstance(s.ref.address, nullptr);
}

void loadJournal() {
    State& S = st();
    std::lock_guard<std::mutex> lock(S.m);
    if (S.journalLoaded) return;
    S.journalLoaded = true;
    try {
        const std::string text = hoponsui::readJournal(kJournal);
        if (text.empty()) return;
        for (auto& e : hopons::parseEdits(text)) S.edits.push_back({S.nextEdit++, std::move(e)});
    } catch (const std::exception& e) {
        S.journalError = std::string("recovery journal unreadable: ") + e.what();
    }
}

void saveJournalLocked(State& S) {
    std::vector<ScriptEdit> list;
    for (const auto& r : S.edits) list.push_back(r.e);
    hoponsui::writeJournal(kJournal, hopons::serialiseEdits(list));
}

void restoreAllEdits(Ctx& c, const char* why);

void hookTeardown(Ctx& c) {
    c.onPanic("hopons.timer_edits", [](Ctx& k) { restoreAllEdits(k, "Panic"); });
    auto& hooks = c.state<SessionHooks>("hopons.encounters.hooks");
    if (!hooks.disconnectHooked) {
        hooks.disconnectHooked = true;
        c.onDisconnect([](Ctx& k) { restoreAllEdits(k, "disconnect"); });
    }
}

void timerEditJob(Ctx& c, uint32_t address, hopons::ScriptField field, double value) {
    c.require(world::kFingerprints, "Timer edits");
    c.require(hopons::kTimerFingerprints, "Timer edits");
    loadJournal();
    world::RuntimeReader mem(c.rt());
    const ScriptInstance s = requireInstance(address);
    std::string why;
    const auto edit = hopons::prepareTimerEdit(mem, s, field, value, why);
    if (!edit) throw Error(field.name + ": " + why);
    State& S = st();
    {
         
        std::lock_guard<std::mutex> lock(S.m);
        S.edits.push_back({S.nextEdit++, *edit});
        try {
            saveJournalLocked(S);
        } catch (const std::exception& e) {
            S.edits.pop_back();
            throw Error(std::string("could not save the recovery journal (nothing written): ") + e.what());
        }
    }
    hookTeardown(c);
    try {
        hopons::writeTimerEdit(c.rt(), mem, *edit);
    } catch (...) {
        refreshAfter(mem, s);
        throw;
    }
    refreshAfter(mem, s);
    c.log("[encounters] " + s.ref.def.name + " " + field.name + " = " + std::to_string(value) + " (journalled)");
    setResult(address, true, field.name + " set (restore it from Timer edits)");
}

bool restoreOne(Ctx& c, world::Reader& mem, uint32_t id, std::string& why) {
    State& S = st();
    ScriptEdit e;
    {
        std::lock_guard<std::mutex> lock(S.m);
        const auto it = std::find_if(S.edits.begin(), S.edits.end(), [id](const EditRow& r) { return r.id == id; });
        if (it == S.edits.end()) {
            why = "already restored";
            return true;
        }
        e = it->e;
    }
    if (!hopons::restoreTimerEdit(c.rt(), mem, e, why)) return false;
    std::lock_guard<std::mutex> lock(S.m);
    S.edits.erase(std::remove_if(S.edits.begin(), S.edits.end(), [id](const EditRow& r) { return r.id == id; }), S.edits.end());
    saveJournalLocked(S);
    return true;
}

void restoreJob(Ctx& c, uint32_t id) {
    c.require(world::kFingerprints, "Timer edits");
    c.require(hopons::kTimerFingerprints, "Timer edits");
    world::RuntimeReader mem(c.rt());
    std::string why;
    if (!restoreOne(c, mem, id, why)) throw Error("restore refused: " + why);
    c.toast("Timer field restored");
}

void restoreAllEdits(Ctx& c, const char* why) {
    loadJournal();
    State& S = st();
    std::vector<uint32_t> ids;
    {
        std::lock_guard<std::mutex> lock(S.m);
        for (auto it = S.edits.rbegin(); it != S.edits.rend(); ++it) ids.push_back(it->id);    
    }
    if (ids.empty()) return;
    size_t ok = 0;
    try {
        c.require(hopons::kTimerFingerprints, "Timer edits");
        world::RuntimeReader mem(c.rt());
        for (uint32_t id : ids) {
            std::string w;
            try {
                if (restoreOne(c, mem, id, w)) ++ok;
                else c.log("[encounters] restore refused: " + w);
            } catch (const std::exception& e) {
                c.log(std::string("[encounters] restore failed: ") + e.what());
            }
        }
    } catch (const std::exception& e) {
        c.log(std::string("[encounters] ") + why + ": could not restore timer edits: " + e.what());
        return;
    }
    c.log("[encounters] " + std::string(why) + ": restored " + std::to_string(ok) + " of " + std::to_string(ids.size()) + " timer edits");
}

void restoreAllJob(Ctx& c) {
    restoreAllEdits(c, "Restore all");
    State& S = st();
    size_t left;
    {
        std::lock_guard<std::mutex> lock(S.m);
        left = S.edits.size();
    }
    c.toast(left ? std::to_string(left) + " timer edits could not be restored (see the log)" : "Every timer edit is restored");
}

void cleanupJob(Ctx& c, uint32_t address) {
    c.require(world::kFingerprints, "Sequence cleanup");
    c.require(hopons::kCleanupFingerprints, "Sequence cleanup");
    world::RuntimeReader mem(c.rt());
    const ScriptInstance s = requireInstance(address);
    const auto o = hopons::cleanupSequence(c.rt(), mem, s, [&c](const std::string& m) { c.log("[encounters] " + m); });
    refreshAfter(mem, s);
    setResult(address, o.cleared, o.message);
    if (!o.called) throw Error(o.message);
    c.toast(o.message);
}

void refreshJob(Ctx& c, uint32_t address) {
    c.require(world::kFingerprints, "Encounters");
    world::RuntimeReader mem(c.rt());
    const ScriptInstance s = requireInstance(address);
    ScriptInstance fresh;
    std::string why;
    if (!hopons::refreshScript(mem, s.ref, fresh, &why)) {
        hoponsui::replaceInstance(address, nullptr);
        throw Error("the instance changed or is gone: " + why);
    }
    hoponsui::replaceInstance(address, &fresh);
    setResult(address, true, "Refreshed: " + fresh.inputStatus);
}

void inspectJob(Ctx& c, uint32_t address, int what) {
    c.require(world::kFingerprints, "Encounters");
    world::RuntimeReader mem(c.rt());
    const ScriptInstance s = requireInstance(address);
    std::vector<std::string> lines;
    std::string title;
    std::string why;
    if (what == 0) {
        title = "Spatial reference";
        lines = hopons::inspectSpatialReference(mem, s);
    } else if (what == 1) {
        title = "Clock and bundle (development)";
        if (const auto o = hopons::readClockOwner(mem, s.ref.def.slotHash, why)) lines = o->lines;
        else lines.push_back("clock owner: " + why);
        if (const auto b = hopons::readBundle(mem, s.ref, why))
            for (const auto& l : b->lines) lines.push_back(l);
        else lines.push_back("bundle: " + why);
    } else {
        title = "Restart preflight (development)";
        lines = hopons::preflightRestart(mem, s).lines;
    }
    for (const auto& l : lines) c.log("[encounters] " + l);
    setReport(address, title, lines);
}

 
 
 

void drawReport(uint32_t address) {
    State& s = st();
    std::vector<std::string> lines;
    std::string title;
    {
        std::lock_guard<std::mutex> lock(s.m);
        if (s.reportFor != address) return;
        lines = s.report;
        title = s.reportTitle;
    }
    if (lines.empty()) return;
    gap(sp::M);
    beginCard("##report", nullptr, {.raised = true});
    {
        heading(title.c_str());
        ImGui::SameLine();
        alignRight(px(28));
        std::string all;
        for (const auto& l : lines) all += l + "\n";
        if (iconButton("##copyreport", icon::Copy, "Copy the report", false, 28)) copyToClipboard(all, "report");
    }
    gap(sp::XS);
    for (const auto& l : lines) {
        const ImVec4 c = l.rfind("FAIL", 0) == 0 ? pal::Danger : l.rfind("ok:", 0) == 0 ? pal::Success : l.rfind("not checked", 0) == 0 ? pal::Warning : pal::Muted;
        wrapped(l.c_str(), c, type::Small);
    }
    endCard();
}

void drawTimer(Ui& ui, const ScriptInstance& inst) {
    State& s = st();
    const auto fields = hopons::timerFields(inst);
    fieldLabel("Timer Fields");
    tooltip("Temporary overrides in native clock units. Every write is journalled first and restored on Panic or disconnect; restoring does "
            "not undo encounter transitions the timer already caused.");
    gap(sp::XS + 2);
    if (fields.empty()) {
        wrapped(("Timer edits are not available: " + hopons::timerFieldsUnavailable(inst)).c_str(), pal::Warning);
        return;
    }
    if (s.valuesFor != inst.ref.address) {
        s.values.clear();
        s.texts.clear();
        s.valuesFor = inst.ref.address;
    }
    auto post = [&](const hopons::ScriptField& f, double v) {
        const uint32_t address = inst.ref.address;
        const std::string job = "Set " + f.name;
        noteQueued(job);
        ui.post(job, [address, f, v](Ctx& c) { timerEditJob(c, address, f, v); }, [address](bool ok, const std::string& err) {
            if (!ok) {
                setResult(address, false, err);
                return;
            }
             
            st().values.clear();
            st().texts.clear();
        });
    };
     
    {
        auto offline = requireConnection(ui);
        const hopons::ScriptField* mult = nullptr;
        const hopons::ScriptField* run = nullptr;
        for (const auto& f : fields) {
            if (f.offset == 0x120) mult = &f;
            if (f.offset == 0x108) run = &f;
        }
        if (mult && button("Freeze", {.tone = Tone::Ghost, .icon = icon::Pause, .small = true, .tooltip = "Clock-delta multiplier 0: the timer stops moving",
                                      .selected = mult->value == 0}))
            post(*mult, 0);
        ImGui::SameLine(0, px(6));
        if (mult && button("Normal", {.tone = Tone::Ghost, .small = true, .tooltip = "Multiplier 1", .selected = mult->value == 1})) post(*mult, 1);
        ImGui::SameLine(0, px(6));
        if (mult && button("2x", {.tone = Tone::Ghost, .small = true, .tooltip = "Multiplier 2: twice as fast (the result can jump)", .selected = mult->value == 2}))
            post(*mult, 2);
        ImGui::SameLine(0, px(6));
        if (run && button(run->value ? "Hold" : "Run", {.tone = Tone::Ghost, .icon = run->value ? icon::Stop : icon::Play, .small = true,
                                                        .tooltip = "Turns elapsed-time subtraction off (hold) or on (run)"}))
            post(*run, run->value ? 0 : 1);
    }
    gap(sp::S);
    const float inputW = 132;
    const float setW = buttonWidth("Set", nullptr, true) / scale();
    auto format = [](const hopons::ScriptField& f, double v) {
        char b[48];
        if (f.kind == hopons::ScriptField::Float) snprintf(b, sizeof b, "%.4g", v);
        else snprintf(b, sizeof b, "%.0f", v);
        return std::string(b);
    };
    for (const auto& f : fields) {
        ImGui::PushID(int(f.offset));
        auto [it, inserted] = s.values.emplace(f.offset, f.value);
        double& v = it->second;
        if (inserted) s.texts[f.offset] = format(f, f.value);
        const bool changed = v != f.value;
        const float rowY = ImGui::GetCursorPosY();
         
        ImGui::SetCursorPosY(rowY + px(7));
        label(f.name.c_str(), changed ? pal::Accent : pal::Text);
        tooltip(f.help.c_str());
        ImGui::SameLine(0, px(6));
        caption(("+" + hoponsui::hx(f.offset).substr(5)).c_str(), pal::Faint);
        ImGui::SameLine();
        alignRight(px(inputW + 6 + setW));
        if (f.kind == hopons::ScriptField::Boolean) {
            ImGui::SetCursorPosY(rowY + px(6));
            bool on = v != 0;
            if (toggleSwitch("##b", &on)) v = on ? 1 : 0;
            ImGui::SameLine(0, px(inputW - 22 + 6));
        } else {
            ImGui::SetCursorPosY(rowY);
            std::string& text = s.texts[f.offset];
            if (inputMono("##v", &text, f.kind == hopons::ScriptField::Float ? "Multiplier" : "Integer", inputW)) {
                char* end = nullptr;
                const double parsed = std::strtod(text.c_str(), &end);
                if (end && end != text.c_str() && *end == 0 && std::isfinite(parsed)) v = parsed;
            }
            ImGui::SameLine(0, px(6));
        }
        ImGui::SetCursorPosY(rowY + px(3));
        {
            auto offline = requireConnection(ui);
            DisabledScope same(!changed, "Change the value first");
            if (button("Set", {.tone = changed ? Tone::Primary : Tone::Secondary, .small = true,
                               .tooltip = changed ? ("Live value " + format(f, f.value) + ": write " + format(f, v)).c_str() : nullptr,
                               .busy = ui.busy("Set " + f.name)}))
                post(f, v);
        }
        ImGui::SetCursorPosY(rowY + px(kControlH) + px(6));
        ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(0, 0));    
}

void drawSequence(Ui& ui, const ScriptInstance& inst) {
    const auto q = hopons::decodeSequence(inst);
    fieldLabel("Sequence");
    gap(sp::XS + 2);
    if (!q) {
        caption("The sequence root is too small to decode.", pal::Warning);
        return;
    }
    pill(("State: " + std::string(hopons::sequenceStateText(q->state))).c_str(), q->state == 2 ? pal::Success : q->state == 1 ? pal::Info : pal::Muted);
    ImGui::SameLine(0, px(6));
    char b[96];
    snprintf(b, sizeof b, "Selector %02X", q->selector);
    pill(b, pal::Muted);
    gap(sp::XS);
    const uint32_t effect = inst.ref.def.effect;
    caption(effect == 0xFFFFFFFFu ? "No effect configured" : ("Effect " + names::tagLabel(effect)).c_str(), pal::Muted);
    if (q->hasPair()) {
        snprintf(b, sizeof b, "Tracked effect %08X / %08X", q->pairIdentity, q->pairAccessor);
        mono(b, pal::Muted, type::MonoSmall);
    }
    gap(sp::S);
    const uint32_t address = inst.ref.address;
    auto offline = requireConnection(ui);
    {
        DisabledScope none(!q->hasPair() && q->state == 0, "The sequence is already cleared");
        if (button("Delete Current Effect", {.tone = Tone::Danger, .icon = icon::Delete, .busy = ui.busy("Delete sequence effect")})) {
            confirm("Delete the Current Effect?",
                    "Clears this sequence and deletes its tracked effect on the game thread (checked in the same frame). Upstream input may create it "
                    "again later; encounter progression is not undone.",
                    "Delete Effect", [address] {
                        noteQueued("Delete sequence effect");
                        Engine::get().post("Delete sequence effect", [address](Ctx& c) { cleanupJob(c, address); });
                    });
        }
    }
    ImGui::SameLine(0, px(8));
    if (button("Spatial Reference", {.tone = Tone::Ghost, .icon = icon::Link, .busy = ui.busy("Inspect spatial reference")})) {
        Engine::get().post("Inspect spatial reference", [address](Ctx& c) { inspectJob(c, address, 0); });
    }
    gap(sp::S);
    fieldLabel("Development");
    ImGui::SameLine(0, px(8));
    badgePill(kBadgeDev);
    ImGui::SameLine(0, px(4));
    badgePill(kBadgeNeedsCheck);
    gap(sp::XS + 2);
    if (button("Clock and Bundle", {.tone = Tone::Ghost, .icon = icon::History, .busy = ui.busy("Inspect sequence clock")}))
        Engine::get().post("Inspect sequence clock", [address](Ctx& c) { inspectJob(c, address, 1); });
    ImGui::SameLine(0, px(8));
    if (button("Restart Preflight", {.tone = Tone::Ghost, .icon = icon::Probe,
                                     .tooltip = "Read-only: what a restart would check (identity, input, tracked effect, dependencies, clock, timing)",
                                     .busy = ui.busy("Restart preflight")}))
        Engine::get().post("Restart preflight", [address](Ctx& c) { inspectJob(c, address, 2); });
    gap(sp::XS + 2);
     
    pill("Restart: Blocked", badgeTone(kBadgeBlocked), icon::Warning);
    tooltip(hopons::kRestartBlocked);
}

void drawInstance(Ui& ui, const EncounterDef& d, const std::vector<const ScriptInstance*>& instances) {
    State& s = st();
    if (instances.empty()) {
        gap(sp::S);
        bool have;
        {
            std::lock_guard<std::mutex> lock(scanState().m);
            have = scanState().have;
        }
        caption(have ? "No live instance of this controller in the last discovery." : "Discover live instances to act on this controller.", pal::Faint);
        return;
    }
    auto cur = std::find_if(instances.begin(), instances.end(), [&](const ScriptInstance* i) { return i->ref.address == s.selectedAddress; });
    if (cur == instances.end()) {
        s.selectedAddress = instances.front()->ref.address;
        cur = instances.begin();
    }
    const ScriptInstance& inst = **cur;
    fieldLabel("Live Instance");
    gap(sp::XS + 2);
    hoponsui::Flow row;
    if (instances.size() > 1) {
        std::vector<std::string> labels;
        int idx = 0;
        for (size_t i = 0; i < instances.size(); ++i) {
            labels.push_back(hx(instances[i]->ref.address));
            if (instances[i] == &inst) idx = int(i);
        }
        std::vector<const char*> items;
        for (auto& l : labels) items.push_back(l.c_str());
        row.next(px(140), 0);
        if (combo("##inst", &idx, items.data(), int(items.size()), 140)) s.selectedAddress = instances[size_t(idx)]->ref.address;
    } else {
        row.next(textSize(FontKind::Mono, type::Mono, "00000000").x + px(16), 0);
        copyable(hx(inst.ref.address).c_str(), "instance address");
    }
    const uint32_t address = inst.ref.address;
    {
        auto offline = requireConnection(ui);
        row.next(buttonWidth("Refresh", icon::Refresh, true), px(8));
        if (button("Refresh", {.tone = Tone::Ghost, .icon = icon::Refresh, .small = true, .tooltip = "Re-read this instance and join its input",
                               .busy = ui.busy("Refresh instance")}))
            ui.post("Refresh instance", [address](Ctx& c) { refreshJob(c, address); });
    }
    row.next(buttonWidth("Copy Bytes", icon::Copy, true), px(6));
    if (button("Copy Bytes", {.tone = Tone::Ghost, .icon = icon::Copy, .small = true})) copyToClipboard(hopons::hexBytes(inst.bytes), "instance bytes");
    if (!inst.input.empty()) {
        row.next(buttonWidth("Copy Input", icon::Copy, true), px(6));
        if (button("Copy Input", {.tone = Tone::Ghost, .icon = icon::Copy, .small = true})) copyToClipboard(hopons::hexBytes(inst.input), "input bytes");
    }
    gap(sp::XS);
    wrapped(inst.inputStatus.c_str(), inst.joined ? (inst.headerVerified ? pal::Muted : pal::Warning) : pal::Faint);
    divider(sp::M);
    if (d.cls == hopons::cls::TIMER) drawTimer(ui, inst);
    else if (d.cls == hopons::cls::SEQUENCE) drawSequence(ui, inst);
    else if (d.cls == hopons::cls::SPAWNER || d.cls == hopons::cls::SQUAD) hoponsDrawAiDetail(ui, inst, "enc");
    else caption("No tools for this controller kind yet: copy its bytes to study it.", pal::Faint);
    std::string result;
    bool ok;
    uint32_t forAddr;
    {
        std::lock_guard<std::mutex> lock(s.m);
        result = s.result;
        ok = s.resultOk;
        forAddr = s.resultFor;
    }
    if (!result.empty() && forAddr == address) {
        gap(sp::M);
        pill(result.c_str(), ok ? pal::Success : pal::Danger, ok ? icon::Check : icon::Warning);
    }
    drawReport(address);
}

void drawDetail(Ui& ui, float width, float height) {
    State& s = st();
    const bool child = height > 0;
    if (child) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, pal::Raised);
        ImGui::PushStyleColor(ImGuiCol_Border, pal::Border);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(kRowRounding));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(16), px(14)));
        ImGui::SetNextWindowSizeConstraints(ImVec2(width, height), ImVec2(width, FLT_MAX));
        ImGui::BeginChild("##encdetail", ImVec2(width, 0),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AutoResizeY);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
    } else {
        beginCard("##encdetailcard", nullptr, {.raised = true});
    }
    const EncounterDef* d = s.selectedRoot && s.cat ? s.cat->byRoot(s.selectedRoot) : nullptr;
    if (!d) {
        emptyState(icon::Spawner, "Select a Controller",
                   "Discover Live Instances, Then Pick a Live Row",
                   child ? std::max(200.0f, height / scale() - 40) : 200);
    } else {
        {
            const float iconsW = px(30) * 2 + px(4);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + availWidth() - iconsW - px(8));
            heading(rootName(*d).c_str());
            ImGui::PopTextWrapPos();
            const ImVec2 after = ImGui::GetCursorScreenPos();
            ImGui::SameLine();
            alignRight(iconsW);
            if (iconButton("##rename", icon::Tag, "Name this controller (saved with your labels)", s.renaming, 30)) {
                s.renaming = !s.renaming;
                const auto own = names::Labels::get().find(d->root);
                s.renameText = own ? own->name : d->name;
            }
            ImGui::SameLine(0, px(4));
            const bool fav = s.favs.count(d->root) != 0;
            if (iconButton("##fav", fav ? icon::StarFill : icon::Star, fav ? "Remove from favourites" : "Add to favourites", false, 30, fav ? pal::Accent : pal::Muted))
                toggleFavourite(d->root);
            ImGui::SetCursorScreenPos(after);
        }
        if (s.renaming) {
            gap(sp::S);
            const float saveW = buttonWidth("Save", icon::Check, true), cancelW = buttonWidth("Cancel", nullptr, true);
            const float w = std::max(120.0f, (availWidth() - saveW - cancelW - px(12)) / scale());
            const bool enter = inputText("##rename", &s.renameText, "Your Name for This Controller", w, ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine(0, px(6));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(3));
            if (button("Save##rename", {.tone = Tone::Primary, .icon = icon::Check, .small = true}) || enter) {
                if (s.renameText.empty() || s.renameText == d->name) names::Labels::get().remove(d->root);
                else names::Labels::get().set(d->root, {s.renameText, s.cat->kind(d->cls), false});
                s.renaming = false;
                s.viewDirty = true;
                notify("Saved the name", Level::Good);
            }
            ImGui::SameLine(0, px(6));
            if (button("Cancel##rename", {.tone = Tone::Ghost, .small = true})) s.renaming = false;
        }
        gap(sp::XS);
        hashLabel(d->root, "root tag", pal::Accent);
        ImGui::SameLine(0, px(10));
        pill(s.cat->kind(d->cls).c_str(), pal::Info);
        const auto lc = s.liveCounts.find(d->root);
        ImGui::SameLine(0, px(6));
        pill(lc == s.liveCounts.end() ? "Not Live" : ("Live  " + std::to_string(lc->second)).c_str(), lc == s.liveCounts.end() ? pal::Muted : pal::Success);
        gap(sp::XS);
        char b[200];
        if (availWidth() >= textSize(FontKind::Mono, type::MonoSmall, "class 80800497  blob 80B46028 +16C  size 304").x) {
            snprintf(b, sizeof b, "class %08X  blob %08X +%X  size %u", d->cls, d->blob, d->binding, d->size);
            mono(b, pal::Muted, type::MonoSmall);
            snprintf(b, sizeof b, "slot %08X/%08X  input %08X", d->slotHash, d->slotId, d->inputSchema);
            mono(b, pal::Faint, type::MonoSmall);
        } else {
            snprintf(b, sizeof b, "class %08X  size %u", d->cls, d->size);
            mono(b, pal::Muted, type::MonoSmall);
            snprintf(b, sizeof b, "blob %08X +%X", d->blob, d->binding);
            mono(b, pal::Muted, type::MonoSmall);
            snprintf(b, sizeof b, "slot %08X/%08X", d->slotHash, d->slotId);
            mono(b, pal::Faint, type::MonoSmall);
        }
        caption(d->evidence == 'c' ? "Name: controller context alias" : d->evidence == 'b' ? "Name: binding slot name" : "Name: generated label", pal::Faint);
        divider(sp::M);
        std::vector<ScriptInstance> copies;
        {
            std::lock_guard<std::mutex> lock(scanState().m);
            for (const auto& i : scanState().scan.instances)
                if (i.ref.def.root == d->root) copies.push_back(i);
        }
        std::vector<const ScriptInstance*> ptrs;
        for (const auto& i : copies) ptrs.push_back(&i);
        drawInstance(ui, *d, ptrs);
    }
    if (child) {
        gap(sp::XS);
        s.detailH = ImGui::GetCursorPosY() + px(14);
        ImGui::EndChild();
    } else {
        endCard();
    }
}

 
 
 

void drawDiscoverBar(Ui& ui) {
    auto& S = scanState();
    std::string status, progress;
    float fraction;
    bool have;
    double at;
    size_t found;
    {
        std::lock_guard<std::mutex> lock(S.m);
        status = S.status;
        progress = S.progress;
        fraction = S.fraction;
        have = S.have;
        at = S.at;
        found = S.scan.instances.size();
    }
    const bool running = S.running.load() || ui.busy(hoponsui::kDiscoverJob);
    if (running) {
        if (button("Stop", {.tone = Tone::Danger, .icon = icon::Stop, .spinner = true})) Engine::get().cancel();
        ImGui::SameLine(0, px(12));
        const float w = std::min(px(360), availWidth() * 0.5f);
        ImGui::BeginGroup();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(6));
        caption(progress.empty() ? "Reading the tag table..." : progress.c_str(), pal::Muted);
        progressBar(fraction, w / scale(), 4);
        ImGui::EndGroup();
        requestRedraw();
    } else {
        {
            auto offline = requireConnection(ui);
            if (button("Discover Live Instances", {.tone = Tone::Primary, .icon = icon::Search,
                                                   .tooltip = "Scans the tag pages for live controller roots (a few seconds; Stop works)"}))
                hoponsui::postDiscover(ui);
        }
        ImGui::SameLine(0, px(8));
        if (iconButton("##discmenu", icon::More, "Pin \"Discover Encounter Instances\" to Home or give it a hotkey", false, kControlH)) openItemMenu(kDiscoverId);
        ImGui::SameLine(0, px(12));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(8));
        if (have) caption((std::to_string(found) + " live instances  \xC2\xB7  " + ago(hoponsui::nowSeconds() - at)).c_str(), pal::Muted);
        else caption("Nothing discovered yet: load into an activity, then Discover.", pal::Faint);
        if (have && !status.empty()) tooltip(status.c_str());
    }
}

 
void starHeaders() {
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers, px(30));
    const int n = ImGui::TableGetColumnCount();
    FontScope f(FontKind::Semibold, type::Small - 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, pal::Muted);
    for (int c = 0; c < n; ++c) {
        if (!ImGui::TableSetColumnIndex(c)) continue;
        ImGui::TableHeader(ImGui::TableGetColumnName(c));
        if (c == 0) {
            const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
            const ImVec2 is = textSize(FontKind::Icons, 11, icon::Star);
            const float cx = ImGui::TableGetCellBgRect(ImGui::GetCurrentTable(), 0).Min.x + ImGui::GetStyle().CellPadding.x + px(8);
            drawText(ImGui::GetWindowDrawList(), FontKind::Icons, 11, ImVec2(cx - is.x * 0.5f, (mn.y + mx.y - is.y) * 0.5f), u32(pal::Muted), icon::Star);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("Sort favourites first");
        }
    }
    ImGui::PopStyleColor();
}

void drawScripts(Ui& ui) {
    State& s = st();
    if (!ensureCatalog()) {
        emptyState(icon::Warning, "Encounter Catalogue Unavailable",
                   (hopons::encounterCatalog().error.empty() ? std::string("data/hopons/encounters.tsv is empty") : hopons::encounterCatalog().error).c_str(), 220);
        return;
    }
    syncScan();
    drawDiscoverBar(ui);
    gap(sp::M);
    ImGuiIO& io = ImGui::GetIO();
    const float avail = availWidth();
    ImGuiID searchInput = 0;
    {
        const float searchW = std::min(px(300), avail * 0.32f);
        if (searchField("##encsearch", &s.filter, "Search Name, Kind or Tag", searchW / scale(), true)) s.viewDirty = true;
        ImGui::PushID("##encsearch");
        searchInput = ImGui::GetID("##input");
        ImGui::PopID();
        ImGui::SameLine(0, px(12));
        size_t live = 0;
        for (const auto& [root, n] : s.liveCounts) live += n ? 1 : 0;
        std::vector<std::string> kinds = {"All", live ? "Live  " + std::to_string(live) : std::string("Live"),
                                          s.favs.empty() ? std::string("Favourites") : "Favourites  " + std::to_string(s.favs.size())};
        if (pillTabs("##enckind", kinds, &s.kind)) s.viewDirty = true;
        if (avail >= px(960)) ImGui::SameLine(0, px(12));
        else gap(sp::S);
        std::vector<const char*> items;
        for (const auto& l : s.classLabels) items.push_back(l.c_str());
        if (combo("##enccls", &s.cls, items.data(), int(items.size()), 190)) s.viewDirty = true;
        if (!s.filter.empty() || s.kind || s.cls) {
            ImGui::SameLine(0, px(8));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(3));
            if (button("Clear", {.tone = Tone::Ghost, .icon = icon::Close, .small = true, .tooltip = "Clear the search and filters"})) {
                s.filter.clear();
                s.kind = 0;
                s.cls = 0;
                s.viewDirty = true;
            }
        }
        if (s.viewDirty) rebuildView();
        char buf[64];
        snprintf(buf, sizeof(buf), "%zu of %zu", s.view.size(), s.cat->rows().size());
        const ImVec2 ts = textSize(FontKind::Body, type::Small, buf);
        ImGui::SameLine();
        if (ImGui::GetCursorPosX() + ts.x < ImGui::GetWindowContentRegionMax().x) {
            alignRight(ts.x);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(8));
            caption(buf, pal::Faint);
        } else {
            ImGui::NewLine();
        }
    }
    gap(sp::M);
    const bool split = avail >= px(760);
    const float detailW = split ? std::clamp(avail * 0.42f, px(320), px(480)) : avail;
    const float tableW = split ? avail - detailW - px(16) : avail;
    const bool compactCols = tableW < px(520);
    const float viewH = ImGui::GetWindowHeight();
    const float baseH = std::clamp(viewH - px(300), px(360), px(820));
    const float tableH = split ? std::max(baseH, std::min(s.detailH, px(1000))) : baseH;
    const auto& rows = s.cat->rows();
    const ImVec2 tablePos = ImGui::GetCursorScreenPos();
    const bool tableHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseHoveringRect(tablePos, tablePos + ImVec2(tableW, tableH));
    const bool searchActive = ImGui::GetActiveID() == searchInput;
    const bool popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (!popup && !s.view.empty() && (searchActive || (tableHovered && !io.WantTextInput))) {
        int pos = -1;
        for (int i = 0; i < int(s.view.size()); ++i)
            if (rows[size_t(s.view[size_t(i)])].root == s.selectedRoot) pos = i;
        const int last = int(s.view.size()) - 1;
        int next = pos;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) next = std::min(last, pos + 1);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) next = std::max(0, pos - 1);
        if (next != pos && next >= 0) select(rows[size_t(s.view[size_t(next)])].root);
    }
    if (s.lastSearchActive && !searchActive && !s.view.empty() && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)))
        select(rows[size_t(s.view.front())].root);
    s.lastSearchActive = searchActive;

    ImGui::BeginGroup();
    if (beginTable("##enctable", compactCols ? 4 : 5, tableH / scale(), ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate | ImGuiTableFlags_BordersOuter,
                   tableW / scale())) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##fav", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, px(16), 0);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0, 1);
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, px(compactCols ? 96 : 120), 2);
        if (!compactCols) ImGui::TableSetupColumn("Root", ImGuiTableColumnFlags_WidthFixed, px(84), 3);
        ImGui::TableSetupColumn("Live", ImGuiTableColumnFlags_WidthFixed, px(40), 4);
        starHeaders();
        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsDirty) {
            s.sorted = specs->SpecsCount > 0;
            if (s.sorted) s.sortSpec = specs->Specs[0];
            specs->SpecsDirty = false;
            rebuildView();
        }
        ImGuiListClipper clip;
        clip.Begin(int(s.view.size()), px(kRowH));
        int scrollTo = -1;
        if (s.scrollToSelected) {
            for (int i = 0; i < int(s.view.size()); ++i)
                if (rows[size_t(s.view[size_t(i)])].root == s.selectedRoot) scrollTo = i;
            if (scrollTo >= 0) clip.IncludeItemByIndex(scrollTo);
            s.scrollToSelected = false;
        }
        uint32_t toggleFav = 0;
        ImGui::PushFont(font(FontKind::Body), fontSize(FontKind::Body, type::Label));
        while (clip.Step()) {
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
                const EncounterDef& d = rows[size_t(s.view[size_t(r)])];
                ImGui::TableNextRow(0, px(kRowH));
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(int(d.root));
                const float cellY = ImGui::GetCursorPosY();
                const bool sel = s.selectedRoot == d.root;
                if (tableRowSelectable("##row", sel, nullptr, kRowH - 2 * ImGui::GetStyle().CellPadding.y / scale())) {
                    select(d.root);
                    s.revealDetail = true;
                }
                if (r == scrollTo) ImGui::SetScrollHereY(0.4f);
                ImGui::SetCursorPosY(cellY - px(3));
                const bool fav = s.favs.count(d.root) != 0;
                const ImVec2 spos = ImGui::GetCursorScreenPos() - ImVec2(px(4), 0);
                ImGui::SetCursorScreenPos(spos);
                if (ImGui::InvisibleButton("##star", ImVec2(px(24), px(24)))) toggleFav = d.root;
                const bool sh = ImGui::IsItemHovered();
                const char* g = fav ? icon::StarFill : icon::Star;
                const ImVec2 is = textSize(FontKind::Icons, 12, g);
                drawText(ImGui::GetWindowDrawList(), FontKind::Icons, 12, spos + (ImVec2(px(24), px(24)) - is) * 0.5f, u32(fav ? pal::Accent : sh ? pal::Text : pal::Decor), g);
                if (sh) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                cellText(rootName(d).c_str(), sel ? pal::Text : pal::Soft, FontKind::Body, type::Label, kRowH);
                ImGui::TableSetColumnIndex(2);
                cellText(s.cat->kind(d.cls).c_str(), pal::Muted, FontKind::Body, type::Label, kRowH);
                if (!compactCols) {
                    ImGui::TableSetColumnIndex(3);
                    cellText(hx(d.root).c_str(), sel ? pal::Accent : pal::Muted, FontKind::Mono, type::MonoSmall, kRowH);
                }
                ImGui::TableSetColumnIndex(compactCols ? 3 : 4);
                const auto lc = s.liveCounts.find(d.root);
                cellText(lc == s.liveCounts.end() ? "-" : std::to_string(lc->second).c_str(), lc == s.liveCounts.end() ? pal::Decor : pal::Success, FontKind::Body,
                         type::Label, kRowH);
            }
        }
        ImGui::PopFont();
        if (s.view.empty()) {
            ImGui::TableNextRow(0, px(120));
            ImGui::TableSetColumnIndex(1);
            gap(sp::XL);
            caption(s.kind == 1 ? "No live instances: Discover first (in an activity)." : s.kind == 2 ? "No favourites yet: click the star of a row."
                                                                                                         : "No controllers match.",
                    pal::Muted);
        }
        endTable();
        if (toggleFav) toggleFavourite(toggleFav);
    }
    ImGui::EndGroup();
    if (split) {
        ImGui::SameLine(0, px(16));
        drawDetail(ui, detailW, tableH);
    } else {
        gap(sp::M);
        if (s.revealDetail) ImGui::SetScrollHereY(0.0f);
        drawDetail(ui, detailW, 0);
    }
    s.revealDetail = false;
}

void drawEdits(Ui& ui) {
    ensureCatalog();
    loadJournal();
    State& s = st();
    std::vector<EditRow> rows;
    std::string error;
    {
        std::lock_guard<std::mutex> lock(s.m);
        rows = s.edits;
        error = s.journalError;
    }
    if (!error.empty()) {
        caption(error.c_str(), pal::Danger);
        gap(sp::S);
    }
    if (rows.empty()) {
        caption("No timer edits. Every edit is journalled here before it is written, and restored on Panic, disconnect or demand.", pal::Faint);
        return;
    }
    {
        auto offline = requireConnection(ui);
        if (button(kRestoreAllLabel, {.icon = icon::Undo, .tooltip = "Newest first; refuses fields that changed independently", .busy = ui.busy(kRestoreAllLabel)})) {
            noteQueued(kRestoreAllLabel);
            ui.post(kRestoreAllLabel, restoreAllJob);
        }
    }
    gap(sp::S);
    for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
        const EditRow& r = *it;
        ImGui::PushID(int(r.id));
        const float bw = buttonWidth("Restore", icon::Undo, true) + px(4) + px(28);
        ImGui::BeginGroup();
        label((r.e.ref.def.name + "  \xC2\xB7  " + r.e.label).c_str());
        const std::string sub = hoponsui::clockTime(r.e.at) + "  \xC2\xB7  " + hx(r.e.ref.address) + "  \xC2\xB7  " + hopons::hexBytes(r.e.original) + " \xE2\x86\x92 " +
                                hopons::hexBytes(r.e.applied);
        caption(sub.c_str(), pal::Faint);
        ImGui::EndGroup();
        ImGui::SameLine();
        alignRight(bw);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(4));
        {
            auto offline = requireConnection(ui);
            if (button("Restore", {.tone = Tone::Ghost, .icon = icon::Undo, .small = true})) {
                const uint32_t id = r.id;
                ui.post("Restore timer field", [id](Ctx& c) { restoreJob(c, id); });
            }
        }
        ImGui::SameLine(0, px(4));
        if (iconButton("##forget", icon::Close, "Forget this entry without writing (when its instance is gone)", false, 28)) {
            const uint32_t id = r.id;
            confirm("Forget This Timer Edit?", "Removes it from the recovery journal without restoring anything. Use it when the instance no longer exists.",
                    "Forget", [id] {
                        State& T = st();
                        std::lock_guard<std::mutex> lock(T.m);
                        T.edits.erase(std::remove_if(T.edits.begin(), T.edits.end(), [id](const EditRow& x) { return x.id == id; }), T.edits.end());
                        try {
                            saveJournalLocked(T);
                        } catch (const std::exception& e) {
                            notify(std::string("Could not save the journal: ") + e.what(), Level::Error);
                        }
                    });
        }
        gap(sp::XS);
        ImGui::PopID();
    }
}

void resetOnDisconnect() {
    State& s = st();
    {
        std::lock_guard<std::mutex> lock(s.m);
        s.report.clear();
        s.result.clear();
    }
    if (!Engine::get().connected()) {
        auto& S = scanState();
        std::lock_guard<std::mutex> lock(S.m);
        S.scan = {};
        S.have = false;
        ++S.revision;
        S.status.clear();
    }
    scanState().running = false;
}

void paletteEntries(const std::string& query, std::vector<PaletteEntry>& out) {
    if (!ensureCatalog()) return;
    State& s = st();
    auto add = [&](const EncounterDef& d, bool fav) {
        PaletteEntry pe;
        pe.label = rootName(d);
        pe.where = "Encounter  \xE2\x80\xBA  " + s.cat->kind(d.cls) + "  \xC2\xB7  " + hx(d.root);
        pe.icon = fav ? icon::StarFill : icon::Spawner;
        const auto lc = s.liveCounts.find(d.root);
        if (lc != s.liveCounts.end()) pe.badge = "Live " + std::to_string(lc->second);
        pe.boost = (fav ? 300 : 0) + (lc != s.liveCounts.end() ? 100 : 0);
        const uint32_t root = d.root;
        pe.run = [root] {
            State& T = st();
            T.kind = 0;
            T.cls = 0;
            T.filter.clear();
            T.viewDirty = true;
            select(root);
            openPage(Page::Spawner, "Encounters");
        };
        out.push_back(std::move(pe));
    };
    for (uint32_t r : s.favs)
        if (const auto* d = s.cat->byRoot(r)) add(*d, true);
    if (query.size() < 3) return;
    const auto words = hopons::searchWords(query);
    int n = 0;
    const auto& rows = s.cat->rows();
    for (size_t i = 0; i < rows.size() && n < 25; ++i) {
        if (s.favs.count(rows[i].root)) continue;
        if (!hopons::matchesAll(s.cat->hay(i), words)) continue;
        add(rows[i], false);
        ++n;
    }
}

}   

namespace hoponsui {
void armTimerRecovery(Ctx& c) {
    loadJournal();
    bool any;
    {
        std::lock_guard<std::mutex> lock(st().m);
        any = !st().edits.empty();
    }
    if (any) hookTeardown(c);
}
void ensureEncounterCatalog() { ensureCatalog(); }
void selectEncounterRoot(uint32_t root) {
    ensureCatalog();
    State& s = st();
    s.filter.clear();
    s.kind = 0;
    s.cls = 0;
    s.viewDirty = true;
    select(root);
}
}   

BR_FEATURES(hopons_encounters) {
    r.section({.id = "hopons.encounters", .page = Page::Spawner, .tab = "Encounters", .section = kScriptsSection, .order = 10, .draw = drawScripts,
               .onDisconnect = resetOnDisconnect, .badge = kBadgeNeedsCheck});
    r.section({.id = "hopons.timer_edits", .page = Page::Spawner, .tab = "Encounters", .section = kEditsSection, .order = 20, .draw = drawEdits});
    r.action({.id = kDiscoverId, .page = Page::Spawner, .tab = "Encounters", .section = kScriptsSection, .label = hoponsui::kDiscoverJob,
              .help = "Scan the tag pages for the live timers, sequences, AI spawners and squads of the current activity.", .run = hoponsui::discover});
    r.action({.id = kRestoreAllId, .page = Page::Spawner, .tab = "Encounters", .section = kEditsSection, .label = kRestoreAllLabel,
              .help = "Write back the original bytes of every journalled timer edit (newest first).", .run = restoreAllJob});
    ui::styleSection(kScriptsSection, {.fullWidth = true, .icon = ui::icon::Spawner, .ownsActions = true});
    ui::styleSection(kEditsSection, {.fullWidth = true, .icon = ui::icon::History, .ownsActions = true});
    ui::addPaletteProvider(paletteEntries);
}

}   
