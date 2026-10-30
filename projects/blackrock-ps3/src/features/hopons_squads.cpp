 
 
 
 
 
 
 
 
 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "app/engine.h"
#include "app/registry.h"
#include "app/settings.h"
#include "core/error.h"
#include "core/runtime.h"
#include "features/hopons_shared.h"
#include "game/hopons_addr.h"
#include "game/hopons_ai.h"
#include "game/hopons_catalog.h"
#include "game/hopons_scripts.h"
#include "game/hopons_swap.h"
#include "game/names_catalog.h"
#include "game/world.h"
#include "ui/widgets.h"

namespace brps3 {

void hoponsDrawAiDetail(Ui& ui, const hopons::ScriptInstance& s, const char* idScope);

namespace {

using namespace ui;
using hopons::ScriptInstance;
using hopons::SquadDefinition;
using hopons::SwapJournal;
using hoponsui::ago;
using hoponsui::hx;
using hoponsui::scanState;

constexpr const char* kAiSection = "Squads and spawners";
constexpr const char* kSwapSection = "Enemy swap";
constexpr const char* kPreflightSection = "AI spawn preflight";
constexpr const char* kSwapFavKey = "hopons.swap.favorites";
constexpr const char* kSwapRecentKey = "hopons.swap.recent";
constexpr const char* kEntityRecentKey = "hopons.swap.entities";
constexpr const char* kSwapJournal = "swap-recovery.tsv";
constexpr const char* kRestoreSwapsId = "hopons.swap.restore_all";
constexpr const char* kRestoreSwapsLabel = "Restore All Enemy Swaps";
constexpr const char* kPreflightId = "hopons.ai.preflight";
constexpr const char* kPreflightLabel = "Run AI Spawn Preflight";
constexpr float kRowH = 30;

struct SwapRow {
    uint32_t id = 0;
    SwapJournal j;
};

struct Preset {
    const char* label;
    uint32_t squad;
    uint32_t entity;
    const char* help;
};
 
const Preset kPresets[] = {
    {"Abyss Thrall \xE2\x86\x92 Ogre", 0x8108E0B1, 0x8108F83C, "sq_thrall[0] in raid1_abyss_maze: same container, no residency risk"},
    {"Abyss Thrall \xE2\x86\x92 Knight", 0x8108E0B1, 0x8108F83B, "sq_knights base entity, same container"},
    {"Abyss Thrall \xE2\x86\x92 Shrieker", 0x8108E0B1, 0x8108EB2A, "sq_shrieker entity, same container"},
    {"Abyss Thrall \xE2\x86\x92 Crota", 0x8108E0B1, 0x8108E476, "dark_lens.sq_crota, master container in the same slice (riskier)"},
};

struct State {
     
    uint32_t aiSelected = 0;             
    std::string aiFilter;
     
    std::mutex m;
    std::vector<std::string> report;
    std::string reportTitle;
    uint32_t reportFor = 0;
    uint32_t reportSquadRoot = 0;        
    std::string result;
    bool resultOk = true;
    uint32_t resultFor = 0;

     
    bool loaded = false;
    const hopons::SquadCatalog* cat = nullptr;
    std::vector<int> view;
    bool viewDirty = true;
    std::string filter;
    int kind = 0;                        
    std::unordered_set<uint32_t> favs;
    std::vector<uint32_t> recents;
    uint32_t selectedTag = 0;
    bool scrollToSelected = false;
    float detailH = 0;
    std::vector<char> siteOn;            
    uint32_t siteFor = 0;
    std::string entityQuery;
    uint32_t entity = 0;
    std::vector<uint32_t> entityRecents;
    uint32_t manualTag = 0;
     
    std::optional<SquadDefinition> def;
    std::string defError;
    uint32_t defFor = 0;
    std::map<uint32_t, std::string> liveDefs;    
    uint32_t pendingOpen = 0;            
    bool liveDefsKnown = false;
    std::vector<SwapRow> swaps;
    uint32_t nextSwap = 1;
    bool journalLoaded = false;
    std::string journalError;

     
    hopons::AiPreflight preflight;
    bool havePreflight = false;
    double preflightAt = 0;
    uint32_t extentTag = 0;
    std::string extentResult;
};

State& st() {
    static State s;
    return s;
}

struct SessionHooks {
    bool disconnectHooked = false;
};

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

std::string squadName(uint32_t tag) {
    if (const auto own = names::Labels::get().find(tag); own && !own->name.empty()) return own->name;
    if (const auto* r = hopons::squadCatalog().find(tag)) return r->name;
    return "squad " + hx(tag);
}

std::string entityName(uint32_t tag) {
    const std::string n = names::tagLabel(tag);
    return n.empty() ? hx(tag) : n;
}

 
 
 

void aiInspectJob(Ctx& c, uint32_t address, int what) {
    c.require(world::kFingerprints, "Squads");
    world::RuntimeReader mem(c.rt());
    const ScriptInstance s = requireInstance(address);
    std::vector<std::string> lines;
    std::string title;
    uint32_t squadRoot = 0;
    if (what == 0) {
        title = "Tracked actor";
        lines = hopons::inspectTrackedActor(mem, s);
    } else {
        title = "Squad reference";
        hopons::ScriptScan scan;
        {
            std::lock_guard<std::mutex> lock(scanState().m);
            scan = scanState().scan;
        }
        lines = hopons::inspectSquadReference(mem, s, scan, &squadRoot);
    }
    for (const auto& l : lines) c.log("[squads] " + l);
    setReport(address, title, lines);
    std::lock_guard<std::mutex> lock(st().m);
    st().reportSquadRoot = squadRoot;
}

void hookSwapTeardown(Ctx& c);

void removeActorJob(Ctx& c, uint32_t address) {
    c.require(world::kFingerprints, "Spawner removal");
    c.require(hopons::kSpawnerRemoveFingerprints, "Spawner removal");
    world::RuntimeReader mem(c.rt());
    const ScriptInstance s = requireInstance(address);
    const auto o = hopons::removeTrackedActor(c.rt(), mem, s, [&c](const std::string& m) { c.log("[squads] " + m); });
    ScriptInstance fresh;
    std::string why;
    if (hopons::refreshScript(mem, s.ref, fresh, &why)) hoponsui::replaceInstance(address, &fresh);
    setReport(address, "Remove tracked actor (development)", o.lines);
    setResult(address, o.cleared, o.message);
    if (!o.called) throw Error(o.message);
    c.toast(o.message);
}

void swapForSquadJob(Ctx& c, uint32_t address) {
    c.require(world::kFingerprints, "Enemy swap");
    world::RuntimeReader mem(c.rt());
    const ScriptInstance s = requireInstance(address);
    std::string why;
    const auto b = hopons::readSquadBinding(mem, s.ref, &why);
    if (!b) throw Error("squad definition reference: " + why);
    State& S = st();
    std::lock_guard<std::mutex> lock(S.m);
    S.liveDefs[b->tag] = s.ref.def.name;
    S.pendingOpen = b->tag;    
}

 
 
 

void loadSwapJournal() {
    State& S = st();
    std::lock_guard<std::mutex> lock(S.m);
    if (S.journalLoaded) return;
    S.journalLoaded = true;
    try {
        const std::string text = hoponsui::readJournal(kSwapJournal);
        if (text.empty()) return;
        for (auto& j : hopons::parseSwaps(text)) S.swaps.push_back({S.nextSwap++, std::move(j)});
    } catch (const std::exception& e) {
        S.journalError = std::string("swap recovery journal unreadable: ") + e.what();
    }
}

void saveSwapJournalLocked(State& S) {
    std::vector<SwapJournal> list;
    for (const auto& r : S.swaps) list.push_back(r.j);
    hoponsui::writeJournal(kSwapJournal, hopons::serialiseSwaps(list));
}

void readDefJob(Ctx& c, uint32_t tag) {
    c.require(world::kFingerprints, "Enemy swap");
    world::RuntimeReader mem(c.rt());
    std::string why;
    auto d = hopons::readSquadDefinition(mem, names::toPs3Tag(tag), why);
    State& S = st();
    std::lock_guard<std::mutex> lock(S.m);
    S.defFor = tag;
    S.def = std::move(d);
    S.defError = S.def ? std::string() : why;
}

void liveDefsJob(Ctx& c) {
    c.require(world::kFingerprints, "Enemy swap");
    world::RuntimeReader mem(c.rt());
    std::vector<ScriptInstance> squads;
    {
        std::lock_guard<std::mutex> lock(scanState().m);
        for (const auto& i : scanState().scan.instances)
            if (i.ref.def.cls == hopons::cls::SQUAD) squads.push_back(i);
    }
    if (squads.empty()) throw Error("no live squad controllers: discover encounter instances first (Encounters tab)");
    std::map<uint32_t, std::string> found;
    for (const auto& s : squads) {
        if (c.cancelled()) break;
        std::string why;
        if (const auto b = hopons::readSquadBinding(mem, s.ref, &why)) found[b->tag] = s.ref.def.name;
        else c.log("[swap] " + s.ref.def.name + ": " + why);
    }
    State& S = st();
    {
        std::lock_guard<std::mutex> lock(S.m);
        S.liveDefs = found;
        S.liveDefsKnown = true;
    }
    c.toast(std::to_string(found.size()) + " squad definitions used by " + std::to_string(squads.size()) + " live squads");
}

void restoreAllSwaps(Ctx& c, const char* why);

void hookSwapTeardown(Ctx& c) {
    c.onPanic("hopons.swaps", [](Ctx& k) { restoreAllSwaps(k, "Panic"); });
    auto& hooks = c.state<SessionHooks>("hopons.swap.hooks");
    if (!hooks.disconnectHooked) {
        hooks.disconnectHooked = true;
        c.onDisconnect([](Ctx& k) { restoreAllSwaps(k, "disconnect"); });
    }
}

void swapJob(Ctx& c, uint32_t tag, std::vector<uint32_t> offsets, uint32_t entity) {
    c.require(world::kFingerprints, "Enemy swap");
    loadSwapJournal();
    world::RuntimeReader mem(c.rt());
    State& S = st();
    std::optional<SquadDefinition> def;
    {
        std::lock_guard<std::mutex> lock(S.m);
        if (S.defFor == tag) def = S.def;
    }
    if (!def) throw Error("read the live definition first");
    std::string why;
    auto plan = hopons::planSwap(mem, *def, offsets, names::toPs3Tag(entity), why);
    if (!plan) throw Error("swap refused: " + why);
    plan->label = squadName(tag) + " \xE2\x86\x92 " + entityName(entity);
    {
        std::lock_guard<std::mutex> lock(S.m);
        S.swaps.push_back({S.nextSwap++, *plan});
        try {
            saveSwapJournalLocked(S);
        } catch (const std::exception& e) {
            S.swaps.pop_back();
            throw Error(std::string("could not save the swap journal (nothing written): ") + e.what());
        }
    }
    hookSwapTeardown(c);
    hopons::applySwap(c.rt(), mem, *plan);
    c.log("[swap] " + plan->label + ": " + std::to_string(plan->words.size()) + " sites written (journalled)");
    readDefJob(c, tag);
    c.toast("Swapped " + std::to_string(plan->words.size()) + " sites: " + plan->label);
}

bool restoreSwapOne(Ctx& c, world::Reader& mem, uint32_t id, std::string& msg) {
    State& S = st();
    SwapJournal j;
    {
        std::lock_guard<std::mutex> lock(S.m);
        const auto it = std::find_if(S.swaps.begin(), S.swaps.end(), [id](const SwapRow& r) { return r.id == id; });
        if (it == S.swaps.end()) return true;
        j = it->j;
    }
    const auto r = hopons::restoreSwap(c.rt(), mem, j);
    msg = j.label + ": " + std::to_string(r.restored) + " restored, " + std::to_string(r.already) + " already original, " + std::to_string(r.skipped) + " left alone";
    for (const auto& l : r.lines) c.log("[swap] " + l);
    std::lock_guard<std::mutex> lock(S.m);
    S.swaps.erase(std::remove_if(S.swaps.begin(), S.swaps.end(), [id](const SwapRow& x) { return x.id == id; }), S.swaps.end());
    saveSwapJournalLocked(S);
    return r.skipped == 0;
}

void restoreSwapJob(Ctx& c, uint32_t id, uint32_t tag) {
    c.require(world::kFingerprints, "Enemy swap");
    world::RuntimeReader mem(c.rt());
    std::string msg;
    restoreSwapOne(c, mem, id, msg);
    c.log("[swap] " + msg);
    c.toast(msg);
    if (tag) readDefJob(c, tag);
}

void restoreAllSwaps(Ctx& c, const char* why) {
    loadSwapJournal();
    State& S = st();
    std::vector<uint32_t> ids;
    {
        std::lock_guard<std::mutex> lock(S.m);
        for (auto it = S.swaps.rbegin(); it != S.swaps.rend(); ++it) ids.push_back(it->id);
    }
    if (ids.empty()) return;
    try {
        world::RuntimeReader mem(c.rt());
        for (uint32_t id : ids) {
            std::string msg;
            try {
                restoreSwapOne(c, mem, id, msg);
                c.log("[swap] " + std::string(why) + ": " + msg);
            } catch (const std::exception& e) {
                c.log(std::string("[swap] restore failed: ") + e.what());
            }
        }
    } catch (const std::exception& e) {
        c.log(std::string("[swap] ") + why + ": could not restore swaps: " + e.what());
    }
}

void restoreAllSwapsJob(Ctx& c) {
    restoreAllSwaps(c, "Restore all");
    c.toast("Enemy swaps restored");
}

 
 
 

void preflightJob(Ctx& c) {
    c.require(world::kFingerprints, "AI preflight");
    c.require(hopons::kAiFingerprints, "AI preflight");
    world::RuntimeReader mem(c.rt());
    auto p = hopons::runAiPreflight(mem);
    for (const auto& l : p.lines) c.log(std::string("[ai preflight] ") + hopons::checkStatusText(l.status) + ": " + l.label + ": " + l.detail);
    State& S = st();
    std::lock_guard<std::mutex> lock(S.m);
    S.preflight = std::move(p);
    S.havePreflight = true;
    S.preflightAt = hoponsui::nowSeconds();
}

void extentJob(Ctx& c, uint32_t tag) {
    c.require(world::kFingerprints, "AI preflight");
    world::RuntimeReader mem(c.rt());
    hopons::TagReader tags(mem);
    std::string why, out;
    const auto m = tags.map(names::toPs3Tag(tag), 0, &why);
    if (!m) {
        out = hx(tag) + ": " + why;
    } else {
        const auto e = hopons::definitionExtent(mem, m->body, names::toPs3Tag(tag));
        out = hx(tag) + " body " + hx(m->body) + ": " + (e.ok ? std::to_string(e.extent) + " bytes (" + e.source + ")" : "extent unknown: " + e.why);
    }
    c.log("[ai preflight] " + out);
    State& S = st();
    std::lock_guard<std::mutex> lock(S.m);
    S.extentResult = out;
}

 
 
 

 
void seedDemo() {
    State& s = st();
    std::lock_guard<std::mutex> lock(s.m);
    if (const auto* row = hopons::squadCatalog().find(0x8108E0B1)) {
        SquadDefinition d;
        d.tag = row->tag;
        d.body = 0x71A40200;
        d.size = row->size;
        d.members = uint32_t(row->members.size());
        d.structured = true;
        for (const auto& site : row->sites) {
            hopons::LiveSite l;
            l.offset = site.offset;
            l.address = d.body + site.offset;
            l.entity = site.entity;
            l.member = site.member;
            l.variant = site.variant;
            l.combatant = site.combatant;
            l.weight = 1;
            l.catalogEntity = site.entity;
            d.sites.push_back(l);
        }
        d.sites[0].entity = 0x8108F83C;
        s.def = d;
        s.defFor = row->tag;
        SwapJournal j;
        j.tag = row->tag;
        j.body = d.body;
        j.entity = 0x8108F83C;
        j.words = {{d.sites[0].address, row->sites[0].entity}};
        j.label = "sq_thrall[0] \xE2\x86\x92 Hive ogre major (archetype)";
        j.at = hoponsui::unixNow() - 240;
        s.swaps.push_back({s.nextSwap++, j});
        s.journalLoaded = true;
    }
    s.liveDefs = {{0x8108E0B1, "sq_thrall[0]"}, {0x8108E0B2, "sq_thrall[1]"}};
    s.liveDefsKnown = true;
    using L = hopons::CheckLine;
    s.preflight.lines = {{L::Ok, "placement class (80800906)", "*(018DED08) = 01AAA090 (expect 01AAA090), record 80800906"},
                         {L::Ok, "activation allocation (0xC8D988)", "record 01AB4E70 type 8080043F: definition 0110A2C0, size 48, alignment 16"},
                         {L::Ok, "placement header type", "size 128, alignment 16"},
                         {L::Warn, "AI payload schema", "type 808015BF, definition 010F4340, key 0086D95F (Xbox 0086D95F), backlink ok"},
                         {L::Ok, "AI payload budget", "header + payload need at most 196 of 2048 arena bytes (all 8 stack residues)"},
                         {L::Warn, "variable tag heap", "*(0x1892538) = 01917320 (inside the context pool): field offsets need a PS3 check"},
                         {L::Info, "fixed tag pools", "6 of 6 rows set, 6 plausible"}};
    s.preflight.ok = 4;
    s.preflight.warnings = 2;
    s.havePreflight = true;
    s.preflightAt = hoponsui::nowSeconds() - 75;
}

bool ensureSwapCatalog() {
    State& s = st();
    if (s.loaded) return s.cat != nullptr;
    s.loaded = true;
    const auto& cat = hopons::squadCatalog();
    if (!cat.error.empty() || cat.rows().empty()) return false;
    s.cat = &cat;
    Settings& settings = Engine::get().settings();
    auto readList = [&](const char* key, auto add) {
        for (const std::string& h : settings.list(key)) {
            try {
                add(world::parseHex(h));
            } catch (...) {
            }
        }
    };
    readList(kSwapFavKey, [&](uint32_t v) { s.favs.insert(v); });
    readList(kSwapRecentKey, [&](uint32_t v) { s.recents.push_back(v); });
    readList(kEntityRecentKey, [&](uint32_t v) { s.entityRecents.push_back(v); });
    char* demo = nullptr;
    size_t n = 0;
    if (_dupenv_s(&demo, &n, "BRPS3_UI_DEMO") == 0 && demo && demo[0] == '1' && !s.selectedTag) {
        s.selectedTag = 0x8108E0B1;
        s.scrollToSelected = true;
        s.entity = 0x8108F83C;
        s.favs.insert(0x8108E0B1);
        seedDemo();
    }
    free(demo);
    return true;
}

int swapIndexOf(uint32_t tag) {
    State& s = st();
    if (!s.cat) return -1;
    const auto& rows = s.cat->rows();
    const auto it = std::lower_bound(rows.begin(), rows.end(), tag, [](const hopons::SquadDefRow& r, uint32_t t) { return r.tag < t; });
    return it != rows.end() && it->tag == tag ? int(it - rows.begin()) : -1;
}

void selectSquad(uint32_t tag) {
    State& s = st();
    if (s.selectedTag == tag) return;
    s.selectedTag = tag;
    s.scrollToSelected = true;
}

void rebuildSwapView() {
    State& s = st();
    s.view.clear();
    if (!s.cat) return;
    const auto words = hopons::searchWords(s.filter);
    std::map<uint32_t, std::string> live;
    {
        std::lock_guard<std::mutex> lock(s.m);
        live = s.liveDefs;
    }
    const auto& rows = s.cat->rows();
    auto accept = [&](int i) {
        const auto& r = rows[size_t(i)];
        if (s.kind == 1 && !live.count(r.tag)) return false;
        if (s.kind == 2 && !s.favs.count(r.tag)) return false;
        return words.empty() || hopons::matchesAll(s.cat->hay(size_t(i)), words);
    };
    if (s.kind == 3) {
        for (uint32_t t : s.recents)
            if (const int i = swapIndexOf(t); i >= 0 && accept(i)) s.view.push_back(i);
    } else {
        for (int i = 0; i < int(rows.size()); ++i)
            if (accept(i)) s.view.push_back(i);
    }
    s.viewDirty = false;
}

}   

 
namespace hoponsui {
void armSwapRecovery(Ctx& c) {
    loadSwapJournal();
    bool any;
    {
        std::lock_guard<std::mutex> lock(st().m);
        any = !st().swaps.empty();
    }
    if (any) hookSwapTeardown(c);
}

void openSwapFor(uint32_t squadTag) {
    State& s = st();
    ensureSwapCatalog();
    if (swapIndexOf(squadTag) >= 0 && s.kind != 0) s.kind = 0;
    s.filter.clear();
    s.viewDirty = true;
    selectSquad(squadTag);
    ui::notify("Opened " + squadName(squadTag) + " in Enemy swap", ui::Level::Info);
}
}   

namespace {

 
 
 

void drawAiReport(uint32_t address) {
    State& s = st();
    std::vector<std::string> lines;
    std::string title, result;
    bool ok = true;
    uint32_t resultFor;
    {
        std::lock_guard<std::mutex> lock(s.m);
        if (s.reportFor == address) {
            lines = s.report;
            title = s.reportTitle;
        }
        result = s.result;
        ok = s.resultOk;
        resultFor = s.resultFor;
    }
    if (!result.empty() && resultFor == address) {
        gap(sp::M);
        pill(result.c_str(), ok ? pal::Success : pal::Danger, ok ? icon::Check : icon::Warning);
    }
    if (lines.empty()) return;
    gap(sp::M);
    beginCard("##aireport", nullptr, {.raised = true});
    heading(title.c_str());
    ImGui::SameLine();
    alignRight(px(28));
    std::string all;
    for (const auto& l : lines) all += l + "\n";
    if (iconButton("##copyai", icon::Copy, "Copy the report", false, 28)) copyToClipboard(all, "report");
    gap(sp::XS);
    for (const auto& l : lines) wrapped(l.c_str(), l.find("NOT") != std::string::npos || l.find("Refused") == 0 ? pal::Warning : pal::Muted, type::Small);
    uint32_t root;
    {
        std::lock_guard<std::mutex> lock(s.m);
        root = s.reportFor == address ? s.reportSquadRoot : 0;
    }
    if (root) {
        gap(sp::S);
        if (button("Open the Squad Controller", {.tone = Tone::Ghost, .icon = icon::Follow, .small = true,
                                                 .tooltip = "Selects it here and in the Encounters list"})) {
            hoponsui::selectEncounterRoot(root);
            std::lock_guard<std::mutex> lock(scanState().m);
            for (const auto& i : scanState().scan.instances)
                if (i.ref.def.root == root) {
                    s.aiSelected = i.ref.address;
                    break;
                }
        }
    }
    endCard();
}

void drawSpawner(Ui& ui, const ScriptInstance& s) {
    const auto sp = hopons::decodeSpawner(s.ref.def.cls, s.bytes);
    fieldLabel("AI Spawner");
    gap(sp::XS + 2);
    if (!sp) {
        caption("The spawner root is too small to decode.", pal::Warning);
        return;
    }
    const bool hasActor = world::validHandle(sp->actor);
    pill(hasActor ? "Tracks an Actor" : "No Tracked Actor", hasActor ? pal::Success : pal::Muted, hasActor ? icon::Check : nullptr);
    ImGui::SameLine(0, px(6));
    pill(sp->gate ? "Spawn Gate Closed" : "Spawn Gate Open", sp->gate ? pal::Warning : pal::Muted);
    ImGui::SameLine(0, px(6));
    pill(sp->initialized ? "Initialised" : "Not Initialised", pal::Muted);
    gap(sp::XS);
    char b[160];
    snprintf(b, sizeof b, "actor %08X  input key %08X  linked effect %08X", sp->actor, sp->inputKey, sp->linkedEffect);
    mono(b, pal::Muted, type::MonoSmall);
    if (const auto d = hopons::inspectRefresh(s.ref.def.cls, s.bytes, s.input, s.inputStable)) {
        gap(sp::XS);
        snprintf(b, sizeof b, "Authored spawn input: enabled %u, mode %u. ", d->enabled, d->mode);
        wrapped((std::string(b) + hopons::refreshActionText(d->action)).c_str(), pal::Muted);
    } else {
        gap(sp::XS);
        caption("Spawn gates need a stable joined input: Refresh the instance.", pal::Faint);
    }
    gap(sp::S);
    const uint32_t address = s.ref.address;
    auto offline = requireConnection(ui);
    {
        DisabledScope none(!hasActor, "No tracked actor");
        if (button("Tracked Actor", {.tone = Tone::Ghost, .icon = icon::View, .busy = ui.busy("Inspect tracked actor")}))
            ui.post("Inspect tracked actor", [address](Ctx& c) { aiInspectJob(c, address, 0); });
    }
    ImGui::SameLine(0, px(8));
    if (button("Squad Reference", {.tone = Tone::Ghost, .icon = icon::Link, .busy = ui.busy("Inspect squad reference")}))
        ui.post("Inspect squad reference", [address](Ctx& c) { aiInspectJob(c, address, 1); });
    gap(sp::S);
    fieldLabel("Development");
    ImGui::SameLine(0, px(8));
    badgePill(kBadgeDev);
    gap(sp::XS + 2);
    {
        DisabledScope none(!hasActor, "No tracked actor to remove");
        if (button("Remove Tracked Actor", {.tone = Tone::Danger, .icon = icon::Delete, .busy = ui.busy("Remove tracked actor")})) {
            confirm("Remove the Tracked Actor? (Development)",
                    "Calls the spawner's own removal (0x255FA8) on the game thread after the spawner, actor, object, allocation bit and the squad "
                    "roster entry were captured twice and still match in the same frame. The actor and its object are deleted and leave the squad "
                    "roster; the spawner may spawn again.",
                    "Remove Actor", [address] {
                        noteQueued("Remove tracked actor");
                        Engine::get().post("Remove tracked actor", [address](Ctx& c) { removeActorJob(c, address); });
                    });
        }
    }
}

void drawSquad(Ui& ui, const ScriptInstance& s) {
    fieldLabel("AI Squad");
    gap(sp::XS + 2);
    const auto roster = hopons::decodeRoster(s.ref.def.cls, s.bytes);
    if (!roster) {
        caption("Roster unavailable: truncated state or a count above 40.", pal::Warning);
    } else {
        pill(("Roster  " + std::to_string(roster->count) + " / 40").c_str(), roster->count ? pal::Success : pal::Muted);
        tooltip("Snapshot: actor references may be stale; generations are not object handles.");
        if (roster->count && beginTable("##roster", 3, std::min(200.0f, 30.0f + 26.0f * float(roster->count)), ImGuiTableFlags_BordersOuter)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, px(40));
            ImGui::TableSetupColumn("Actor", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Generation", ImGuiTableColumnFlags_WidthStretch);
            tableHeaders();
            for (uint32_t i = 0; i < roster->count; ++i) {
                ImGui::TableNextRow(0, px(26));
                ImGui::TableSetColumnIndex(0);
                cellText(std::to_string(i).c_str(), pal::Muted, FontKind::Body, type::Label, 26);
                ImGui::TableSetColumnIndex(1);
                const auto& e = roster->entries[i];
                cellText(e.actor == 0xFFFFFFFFu ? "empty" : hx(e.actor).c_str(), e.actor == 0xFFFFFFFFu ? pal::Faint : pal::Text, FontKind::Mono, type::MonoSmall, 26);
                ImGui::TableSetColumnIndex(2);
                cellText(hx(e.generation).c_str(), pal::Muted, FontKind::Mono, type::MonoSmall, 26);
            }
            endTable();
        }
    }
    if (const auto in = hopons::decodeSquadSpawnInput(s.ref.def.cls, s.input, s.inputStable)) {
        char b[120];
        snprintf(b, sizeof b, "Spawn variant %u, context flags %02X %02X %02X (raw; decoders not mapped on PS3)", in->variant, in->contextFlags[0], in->contextFlags[1],
                 in->contextFlags[2]);
        gap(sp::XS);
        caption(b, pal::Muted);
    }
    gap(sp::S);
    const uint32_t address = s.ref.address;
    auto offline = requireConnection(ui);
    if (button("Swap This Squad's Enemies", {.icon = icon::Switch, .tooltip = "Opens its squad definition in Enemy swap (Squads tab)", .busy = ui.busy("Find squad definition")}))
        ui.post("Find squad definition", [address](Ctx& c) { swapForSquadJob(c, address); }, [](bool ok, const std::string&) {
            if (!ok) return;
            uint32_t tag;
            {
                std::lock_guard<std::mutex> lock(st().m);
                tag = st().pendingOpen;
                st().pendingOpen = 0;
            }
            if (tag) hoponsui::openSwapFor(tag);
        });
}

}   

void hoponsDrawAiDetail(Ui& ui, const hopons::ScriptInstance& s, const char* idScope) {
    ImGui::PushID(idScope);
    if (s.ref.def.cls == hopons::cls::SPAWNER) drawSpawner(ui, s);
    else if (s.ref.def.cls == hopons::cls::SQUAD) drawSquad(ui, s);
    drawAiReport(s.ref.address);
    ImGui::PopID();
}

namespace {

 
 
 

void drawAi(Ui& ui) {
    State& s = st();
    hoponsui::ensureEncounterCatalog();
    std::vector<ScriptInstance> ai;
    bool have;
    double at;
    {
        auto& S = scanState();
        std::lock_guard<std::mutex> lock(S.m);
        have = S.have;
        at = S.at;
        for (const auto& i : S.scan.instances)
            if (i.ref.def.cls == hopons::cls::SPAWNER || i.ref.def.cls == hopons::cls::SQUAD) ai.push_back(i);
    }
    const bool running = scanState().running.load() || ui.busy(hoponsui::kDiscoverJob);
    {
        auto offline = requireConnection(ui);
        if (button(have ? "Discover Again" : "Discover Live Instances", {.tone = have ? Tone::Secondary : Tone::Primary, .icon = icon::Search,
                                                                         .tooltip = "Shares the discovery of the Encounters tab", .busy = running}))
            hoponsui::postDiscover(ui);
    }
    size_t spawners = 0;
    for (const auto& i : ai) spawners += i.ref.def.cls == hopons::cls::SPAWNER;
    auto count = [](size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); };
    if (have) {
        ImGui::SameLine(0, px(12));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(8));
        caption((count(spawners, "spawner", "spawners") + "  \xC2\xB7  " + count(ai.size() - spawners, "squad", "squads") + "  \xC2\xB7  " + ago(hoponsui::nowSeconds() - at)).c_str(),
                pal::Muted);
    }
    if (ai.empty()) {
        gap(sp::M);
        emptyState(icon::Spawner, have ? "No AI Controllers Live" : "Nothing Discovered Yet",
                   have ? "Discover Again Inside an Encounter"
                        : "Load into an Activity and Discover",
                   170);
        return;
    }
    if (ai.size() > 6) {
        ImGui::SameLine();
        const float w = std::min(px(260), availWidth() * 0.4f);
        alignRight(w);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(8));
        searchField("##aisearch", &s.aiFilter, "Filter Controllers", w / scale());
        const auto words = hopons::searchWords(s.aiFilter);
        if (!words.empty())
            ai.erase(std::remove_if(ai.begin(), ai.end(),
                                    [&](const ScriptInstance& i) { return !hopons::matchesAll(hopons::lowerCopy(i.ref.def.name + " " + hx(i.ref.address)), words); }),
                     ai.end());
    }
    gap(sp::M);
    if (ai.empty()) {
        caption("No controller matches the filter.", pal::Muted);
        return;
    }
    const float avail = availWidth();
    const bool split = avail >= px(760);
    const float detailW = split ? std::clamp(avail * 0.44f, px(320), px(500)) : avail;
    const float tableW = split ? avail - detailW - px(16) : avail;
    const float tableH = std::clamp(36.0f + kRowH * float(ai.size()), 100.0f, 420.0f);
    if (std::none_of(ai.begin(), ai.end(), [&](const ScriptInstance& i) { return i.ref.address == s.aiSelected; })) s.aiSelected = ai.front().ref.address;
    ImGui::BeginGroup();
    if (beginTable("##aitable", 3, tableH, ImGuiTableFlags_BordersOuter, tableW / scale())) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Controller", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, px(140));
        ImGui::TableSetupColumn("Instance", ImGuiTableColumnFlags_WidthFixed, px(84));
        tableHeaders();
        for (const auto& i : ai) {
            ImGui::TableNextRow(0, px(kRowH));
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(int(i.ref.address));
            const float y = ImGui::GetCursorPosY();
            const bool sel = s.aiSelected == i.ref.address;
            if (tableRowSelectable("##ai", sel, nullptr, kRowH - 2 * ImGui::GetStyle().CellPadding.y / scale())) s.aiSelected = i.ref.address;
            ImGui::SetCursorPosY(y);
            const std::string name = (i.ref.def.cls == hopons::cls::SPAWNER ? "Spawner  \xC2\xB7  " : "Squad  \xC2\xB7  ") + i.ref.def.name;
            cellText(name.c_str(), sel ? pal::Text : pal::Soft, FontKind::Body, type::Label, kRowH);
            ImGui::TableSetColumnIndex(1);
            std::string state;
            ImVec4 color = pal::Muted;
            if (const auto sp = hopons::decodeSpawner(i.ref.def.cls, i.bytes)) {
                state = world::validHandle(sp->actor) ? "actor " + hx(sp->actor) : (sp->gate ? "gate closed" : "idle");
                if (world::validHandle(sp->actor)) color = pal::Success;
            } else if (const auto r = hopons::decodeRoster(i.ref.def.cls, i.bytes)) {
                state = "roster " + std::to_string(r->count) + " / 40";
                if (r->count) color = pal::Success;
            }
            cellText(state.c_str(), color, FontKind::Body, type::Label, kRowH);
            ImGui::TableSetColumnIndex(2);
            cellText(hx(i.ref.address).c_str(), sel ? pal::Accent : pal::Muted, FontKind::Mono, type::MonoSmall, kRowH);
            ImGui::PopID();
        }
        endTable();
    }
    ImGui::EndGroup();
    const auto sel = std::find_if(ai.begin(), ai.end(), [&](const ScriptInstance& i) { return i.ref.address == s.aiSelected; });
    if (split) ImGui::SameLine(0, px(16));
    else gap(sp::M);
    ImGui::BeginGroup();
    beginCard("##aidetail", nullptr, {.width = detailW / scale(), .raised = true});
    if (sel != ai.end()) {
        heading(sel->ref.def.name.c_str());
        gap(sp::XS);
        hashLabel(sel->ref.def.root, "root tag", pal::Accent);
        ImGui::SameLine(0, px(10));
        pill(hopons::encounterCatalog().kind(sel->ref.def.cls).c_str(), pal::Info);
        gap(sp::XS);
        wrapped(sel->inputStatus.c_str(), sel->joined ? pal::Muted : pal::Faint);
        divider(sp::M);
        hoponsDrawAiDetail(ui, *sel, "squads");
    }
    endCard();
    ImGui::EndGroup();
}

 
 
 

void drawEntityPicker(Ui& ui) {
    State& s = st();
    fieldLabel("Replacement Enemy");
    gap(sp::XS + 2);
    if (s.entity) {
        pill(entityName(s.entity).c_str(), pal::Accent, icon::Switch);
        ImGui::SameLine(0, px(6));
        hashLabel(s.entity, "entity tag", pal::Muted);
        ImGui::SameLine(0, px(6));
        if (iconButton("##clearent", icon::Close, "Choose another", false, 26)) s.entity = 0;
        gap(sp::S);
    }
    searchField("##entsearch", &s.entityQuery, "Search Enemies (Bipeds) by Name or Tag", std::min(360.0f, availWidth() / scale()));
    const auto& ents = names::entities();
    if (s.entityQuery.size() >= 2) {
        const auto hits = ents.search(s.entityQuery, world::TYPE_BIPED, 8);
        gap(sp::XS);
        if (hits.empty()) caption("No catalogued biped matches.", pal::Faint);
        for (const names::Entity* e : hits) {
            ImGui::PushID(int(e->tag));
            const std::string l = (e->hasName() ? e->name : names::tagLabel(e->tag)) + "##e";
            if (button(l.c_str(), {.tone = Tone::Ghost, .small = true, .tooltip = (hx(e->tag) + "  \xC2\xB7  " + e->package).c_str(), .selected = s.entity == e->tag})) {
                s.entity = e->tag;
                s.entityQuery.clear();
            }
            ImGui::SameLine(0, px(4));
            ImGui::PopID();
        }
        ImGui::NewLine();
    }
    if (!s.entityRecents.empty()) {
        gap(sp::XS);
        caption("Recent", pal::Faint);
        ImGui::SameLine(0, px(8));
        int n = 0;
        for (uint32_t t : s.entityRecents) {
            if (n++ >= 6) break;
            ImGui::PushID(int(t));
            if (button((entityName(t) + "##r").c_str(), {.tone = Tone::Ghost, .small = true, .selected = s.entity == t})) s.entity = t;
            ImGui::SameLine(0, px(4));
            ImGui::PopID();
        }
        ImGui::NewLine();
    }
    (void)ui;
}

void drawSwapDetail(Ui& ui, float width) {
    State& s = st();
    beginCard("##swapdetail", nullptr, {.width = width / scale(), .raised = true});
    if (!s.selectedTag) {
        emptyState(icon::Switch, "Select a Squad Definition", "Pick a Squad or a Preset", 200);
        endCard();
        return;
    }
    const uint32_t tag = s.selectedTag;
    const hopons::SquadDefRow* row = s.cat ? s.cat->find(tag) : nullptr;
    {
        const float starW = px(30);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + availWidth() - starW - px(8));
        heading(squadName(tag).c_str());
        ImGui::PopTextWrapPos();
        const ImVec2 after = ImGui::GetCursorScreenPos();
        ImGui::SameLine();
        alignRight(starW);
        const bool fav = s.favs.count(tag) != 0;
        if (iconButton("##sfav", fav ? icon::StarFill : icon::Star, fav ? "Remove from favourites" : "Add to favourites", false, 30, fav ? pal::Accent : pal::Muted)) {
            if (fav) s.favs.erase(tag);
            else s.favs.insert(tag);
            Engine::get().settings().toggleInList(kSwapFavKey, hx(tag));
            if (s.kind == 2) s.viewDirty = true;
        }
        ImGui::SetCursorScreenPos(after);
    }
    gap(sp::XS);
    hashLabel(tag, "squad definition tag", pal::Accent);
    if (row) {
        ImGui::SameLine(0, px(10));
        pill((std::to_string(row->members.size()) + (row->members.size() == 1 ? " member" : " members")).c_str(), pal::Muted);
        ImGui::SameLine(0, px(6));
        pill((std::to_string(row->sites.size()) + " sites in the Xbox file").c_str(), pal::Muted);
    }
    std::optional<SquadDefinition> def;
    std::string defError;
    std::vector<SwapRow> swaps;
    {
        std::lock_guard<std::mutex> lock(s.m);
        if (s.defFor == tag) {
            def = s.def;
            defError = s.defError;
        }
        swaps = s.swaps;
    }
    gap(sp::M);
    {
        auto offline = requireConnection(ui);
        if (button(def ? "Read Again" : "Read Live Definition", {.tone = def ? Tone::Secondary : Tone::Primary, .icon = icon::Refresh, .busy = ui.busy("Read squad definition")}))
            ui.post("Read squad definition", [tag](Ctx& c) { readDefJob(c, tag); });
    }
    if (!defError.empty()) {
        gap(sp::S);
        pill(defError.c_str(), pal::Warning, icon::Warning);
    }
    if (def) {
        gap(sp::S);
        char b[200];
        snprintf(b, sizeof b, "body %08X  size %X  members %u  %s", def->body, def->size, def->members,
                 def->structured ? "structure verified" : "marker scan only");
        mono(b, pal::Muted, type::MonoSmall);
        if (!def->structureNote.empty()) caption(def->structureNote.c_str(), pal::Warning);
        size_t sameAsCatalog = 0, compared = 0;
        for (const auto& site : def->sites)
            if (site.catalogEntity) {
                ++compared;
                sameAsCatalog += site.catalogEntity == site.entity;
            }
        if (compared) caption((std::to_string(sameAsCatalog) + " of " + std::to_string(compared) + " sites hold the Xbox catalogue's enemy").c_str(), pal::Faint);
        if (s.siteFor != tag || s.siteOn.size() != def->sites.size()) {
            s.siteFor = tag;
            s.siteOn.assign(def->sites.size(), 1);
        }
        gap(sp::S);
        size_t on = 0;
        for (char c : s.siteOn) on += c != 0;
        fieldLabel(("Sites  (" + std::to_string(on) + " of " + std::to_string(def->sites.size()) + ")").c_str());
        ImGui::SameLine();
        alignRight(buttonWidth("All", nullptr, true) + buttonWidth("None", nullptr, true) + px(6));
        const float chipY = ImGui::GetCursorPosY() - px(8);
        ImGui::SetCursorPosY(chipY);
        if (button("All##sites", {.tone = Tone::Ghost, .small = true, .tooltip = "Swap every site"})) std::fill(s.siteOn.begin(), s.siteOn.end(), char(1));
        ImGui::SameLine(0, px(6));
        ImGui::SetCursorPosY(chipY);
        if (button("None##sites", {.tone = Tone::Ghost, .small = true})) std::fill(s.siteOn.begin(), s.siteOn.end(), char(0));
        gap(sp::XS);
        if (beginTable("##sites", 4, std::min(240.0f, 30.0f + 28.0f * float(def->sites.size())), ImGuiTableFlags_BordersOuter)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("##on", ImGuiTableColumnFlags_WidthFixed, px(26));
            ImGui::TableSetupColumn("Site", ImGuiTableColumnFlags_WidthFixed, px(78));
            ImGui::TableSetupColumn("Enemy Now", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthFixed, px(52));
            tableHeaders();
            for (size_t i = 0; i < def->sites.size(); ++i) {
                const auto& site = def->sites[i];
                ImGui::TableNextRow(0, px(28));
                ImGui::PushID(int(i));
                ImGui::TableSetColumnIndex(0);
                bool v = s.siteOn[i] != 0;
                if (checkbox("##on", &v)) s.siteOn[i] = v ? 1 : 0;
                ImGui::TableSetColumnIndex(1);
                char m[32];
                if (site.member >= 0) snprintf(m, sizeof m, "%d.%d.%d", site.member, site.variant, site.combatant);
                else snprintf(m, sizeof m, "marker");
                cellText(m, pal::Muted, FontKind::Mono, type::MonoSmall, 28);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("member . variant . combatant (weight %u)", site.weight);
                ImGui::TableSetColumnIndex(2);
                const bool swapped = std::any_of(swaps.begin(), swaps.end(), [&](const SwapRow& r) {
                    return r.j.tag == tag && std::any_of(r.j.words.begin(), r.j.words.end(), [&](const auto& w) { return w.first == site.address; });
                });
                const std::string name = entityName(site.entity) + (swapped ? "  (swapped)" : "");
                cellText(name.c_str(), swapped ? pal::Accent : pal::Text, FontKind::Body, type::Label, 28);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip("%s at %s%s", hx(site.entity).c_str(), hx(site.address).c_str(),
                                      site.catalogEntity && site.catalogEntity != site.entity ? ("\nXbox file: " + entityName(site.catalogEntity)).c_str() : "");
                ImGui::TableSetColumnIndex(3);
                cellText(("+" + hopons::hexShort(site.offset)).c_str(), pal::Faint, FontKind::Mono, type::MonoSmall, 28);
                ImGui::PopID();
            }
            endTable();
        }
        gap(sp::M);
        drawEntityPicker(ui);
        gap(sp::M);
        std::vector<uint32_t> offsets;
        for (size_t i = 0; i < def->sites.size(); ++i)
            if (s.siteOn[i]) offsets.push_back(def->sites[i].offset);
        {
            auto offline = requireConnection(ui);
            DisabledScope need(!s.entity || offsets.empty(), !s.entity ? "Choose a replacement enemy" : "Choose at least one site");
            const std::string label = "Swap " + std::to_string(offsets.size()) + (offsets.size() == 1 ? " site" : " sites");
            if (button(label.c_str(), {.tone = Tone::Primary, .icon = icon::Switch, .busy = ui.busy("Swap enemies")})) {
                const uint32_t entity = s.entity;
                confirm("Swap " + squadName(tag) + "?",
                        "Writes " + entityName(entity) + " into " + std::to_string(offsets.size()) +
                            " entity sites of this squad definition (journalled; restored on Panic and disconnect). Squads that spawn from now on use it; "
                            "the replacement must be loaded in this area.",
                        "Swap", [tag, offsets, entity] {
                            State& T = st();
                            T.entityRecents.erase(std::remove(T.entityRecents.begin(), T.entityRecents.end(), entity), T.entityRecents.end());
                            T.entityRecents.insert(T.entityRecents.begin(), entity);
                            Engine::get().settings().pushRecent(kEntityRecentKey, hx(entity), 12);
                            T.recents.erase(std::remove(T.recents.begin(), T.recents.end(), tag), T.recents.end());
                            T.recents.insert(T.recents.begin(), tag);
                            Engine::get().settings().pushRecent(kSwapRecentKey, hx(tag), 20);
                            noteQueued("Swap enemies");
                            Engine::get().post("Swap enemies", [tag, offsets, entity](Ctx& c) { swapJob(c, tag, offsets, entity); },
                                               [](bool ok, const std::string&) {
                                                   if (ok) flashSuccess("Swap enemies");
                                               });
                        });
            }
        }
        for (const SwapRow& r : swaps)
            if (r.j.tag == tag) {
                ImGui::SameLine(0, px(8));
                auto offline = requireConnection(ui);
                if (button("Restore", {.icon = icon::Undo, .tooltip = "Put the original enemies back", .busy = ui.busy("Restore swap")})) {
                    const uint32_t id = r.id;
                    ui.post("Restore swap", [id, tag](Ctx& c) { restoreSwapJob(c, id, tag); });
                }
                break;
            }
    }
    endCard();
}

void drawSwap(Ui& ui) {
    State& s = st();
    loadSwapJournal();
    if (!ensureSwapCatalog()) {
        emptyState(icon::Warning, "Squad Catalogue Unavailable",
                   (hopons::squadCatalog().error.empty() ? std::string("data/hopons/squads.tsv is empty") : hopons::squadCatalog().error).c_str(), 200);
        return;
    }
     
    {
        fieldLabel("Presets");
        gap(sp::XS + 2);
        for (const Preset& p : kPresets) {
            ImGui::PushID(p.label);
            if (button(p.label, {.tone = Tone::Ghost, .small = true, .tooltip = p.help, .selected = s.selectedTag == p.squad && s.entity == p.entity})) {
                selectSquad(p.squad);
                s.entity = p.entity;
            }
            ImGui::SameLine(0, px(6));
            ImGui::PopID();
        }
        ImGui::NewLine();
        gap(sp::S);
    }
    std::map<uint32_t, std::string> live;
    bool liveKnown;
    std::vector<SwapRow> swaps;
    std::string journalError;
    {
        std::lock_guard<std::mutex> lock(s.m);
        live = s.liveDefs;
        liveKnown = s.liveDefsKnown;
        swaps = s.swaps;
        journalError = s.journalError;
    }
    const float avail = availWidth();
    {
        const float searchW = std::min(px(300), avail * 0.32f);
        if (searchField("##swapsearch", &s.filter, "Search Squads, Members or Enemies", searchW / scale(), false)) s.viewDirty = true;
        ImGui::SameLine(0, px(12));
        std::vector<std::string> kinds = {"All", liveKnown ? "Live  " + std::to_string(live.size()) : std::string("Live"),
                                          s.favs.empty() ? std::string("Favourites") : "Favourites  " + std::to_string(s.favs.size()),
                                          s.recents.empty() ? std::string("Recent") : "Recent  " + std::to_string(s.recents.size())};
        if (pillTabs("##swapkind", kinds, &s.kind)) s.viewDirty = true;
        if (avail >= px(820)) ImGui::SameLine(0, px(12));
        else gap(sp::S);
        {
            auto offline = requireConnection(ui);
            if (button("Find Live Squads", {.tone = Tone::Ghost, .icon = icon::Search, .tooltip = "Reads the squad definition of every live squad controller (discover first)",
                                            .busy = ui.busy("Find live squad definitions")}))
                ui.post("Find live squad definitions", liveDefsJob, [](bool ok, const std::string&) {
                    if (ok) {
                        st().kind = 1;
                        st().viewDirty = true;
                    }
                });
        }
        if (s.viewDirty) rebuildSwapView();
    }
    gap(sp::M);
    const bool split = avail >= px(760);
    const float detailW = split ? std::clamp(avail * 0.50f, px(340), px(560)) : avail;
    const float tableW = split ? avail - detailW - px(16) : avail;
    const float tableH = std::clamp(ImGui::GetWindowHeight() - px(320), px(320), px(620));
    const auto& rows = s.cat->rows();
    ImGui::BeginGroup();
    if (beginTable("##swaptable", 3, tableH / scale(), ImGuiTableFlags_BordersOuter, tableW / scale())) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Squad", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Enemies", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Tag", ImGuiTableColumnFlags_WidthFixed, px(84));
        tableHeaders();
        ImGuiListClipper clip;
        clip.Begin(int(s.view.size()), px(kRowH));
        int scrollTo = -1;
        if (s.scrollToSelected) {
            for (int i = 0; i < int(s.view.size()); ++i)
                if (rows[size_t(s.view[size_t(i)])].tag == s.selectedTag) scrollTo = i;
            if (scrollTo >= 0) clip.IncludeItemByIndex(scrollTo);
            s.scrollToSelected = false;
        }
        ImGui::PushFont(font(FontKind::Body), fontSize(FontKind::Body, type::Label));
        while (clip.Step()) {
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
                const auto& row = rows[size_t(s.view[size_t(r)])];
                ImGui::TableNextRow(0, px(kRowH));
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(int(row.tag));
                const float y = ImGui::GetCursorPosY();
                const bool sel = s.selectedTag == row.tag;
                if (tableRowSelectable("##sq", sel, nullptr, kRowH - 2 * ImGui::GetStyle().CellPadding.y / scale())) selectSquad(row.tag);
                if (r == scrollTo) ImGui::SetScrollHereY(0.4f);
                ImGui::SetCursorPosY(y);
                const auto lv = live.find(row.tag);
                const std::string name = squadName(row.tag) + (lv != live.end() ? "  \xE2\x80\xA2 live" : "");
                cellText(name.c_str(), sel ? pal::Text : pal::Soft, FontKind::Body, type::Label, kRowH);
                ImGui::TableSetColumnIndex(1);
                std::string ents;
                for (uint32_t e : row.entities()) {
                    if (!ents.empty()) ents += ", ";
                    ents += entityName(e);
                    if (ents.size() > 60) break;
                }
                cellText(ents.c_str(), pal::Muted, FontKind::Body, type::Label, kRowH);
                ImGui::TableSetColumnIndex(2);
                cellText(hx(row.tag).c_str(), sel ? pal::Accent : pal::Muted, FontKind::Mono, type::MonoSmall, kRowH);
                ImGui::PopID();
            }
        }
        ImGui::PopFont();
        if (s.view.empty()) {
            ImGui::TableNextRow(0, px(100));
            ImGui::TableSetColumnIndex(0);
            gap(sp::L);
            caption(s.kind == 1 ? (liveKnown ? "No live squad definitions." : "Press Find Live Squads (after a discovery).") : "No squads match.", pal::Muted);
        }
        endTable();
    }
    gap(sp::S);
     
    {
        caption("Or a Tag:", pal::Faint);
        ImGui::SameLine(0, px(8));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(8));
        inputHex("##manual", &s.manualTag, 110);
        ImGui::SameLine(0, px(6));
        if (button("Open##manual", {.tone = Tone::Ghost, .small = true}) && s.manualTag) selectSquad(s.manualTag);
    }
    ImGui::EndGroup();
    if (split) ImGui::SameLine(0, px(16));
    else gap(sp::M);
    ImGui::BeginGroup();
    drawSwapDetail(ui, detailW);
    ImGui::EndGroup();
     
    gap(sp::L);
    {
        fieldLabel(("Active Swaps  (" + std::to_string(swaps.size()) + ")").c_str());
        if (!swaps.empty()) {
            ImGui::SameLine();
            alignRight(buttonWidth(kRestoreSwapsLabel, icon::Undo, true));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(8));
            auto offline = requireConnection(ui);
            if (button(kRestoreSwapsLabel, {.tone = Tone::Ghost, .icon = icon::Undo, .small = true, .busy = ui.busy(kRestoreSwapsLabel)}))
                ui.post(kRestoreSwapsLabel, restoreAllSwapsJob);
        }
        gap(sp::XS + 2);
        if (!journalError.empty()) caption(journalError.c_str(), pal::Danger);
        if (swaps.empty()) caption("None. Every swap is journalled here before it is written, and restored on Panic and disconnect.", pal::Faint);
        for (const SwapRow& r : swaps) {
            ImGui::PushID(int(r.id));
            ImGui::BeginGroup();
            label(r.j.label.c_str());
            caption((hoponsui::clockTime(r.j.at) + "  \xC2\xB7  " + std::to_string(r.j.words.size()) + (r.j.words.size() == 1 ? " site" : " sites") +
                     "  \xC2\xB7  body " + hx(r.j.body))
                        .c_str(),
                    pal::Faint);
            ImGui::EndGroup();
            ImGui::SameLine();
            alignRight(buttonWidth("Restore", icon::Undo, true) + px(4) + px(28));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(4));
            {
                auto offline = requireConnection(ui);
                if (button("Restore", {.tone = Tone::Ghost, .icon = icon::Undo, .small = true})) {
                    const uint32_t id = r.id, tag = r.j.tag;
                    ui.post("Restore swap", [id, tag](Ctx& c) { restoreSwapJob(c, id, tag); });
                }
            }
            ImGui::SameLine(0, px(4));
            if (iconButton("##forget", icon::Close, "Forget this swap without writing (when the definition was unloaded)", false, 28)) {
                const uint32_t id = r.id;
                confirm("Forget This Swap?", "Removes it from the swap journal without restoring anything. Use it after the definition was unloaded.",
                        "Forget", [id] {
                            State& T = st();
                            std::lock_guard<std::mutex> lock(T.m);
                            T.swaps.erase(std::remove_if(T.swaps.begin(), T.swaps.end(), [id](const SwapRow& x) { return x.id == id; }), T.swaps.end());
                            try {
                                saveSwapJournalLocked(T);
                            } catch (const std::exception& e) {
                                notify(std::string("Could not save the journal: ") + e.what(), Level::Error);
                            }
                        });
            }
            gap(sp::XS);
            ImGui::PopID();
        }
    }
}

 
 
 

void drawPreflight(Ui& ui) {
    State& s = st();
    ensureSwapCatalog();
    hopons::AiPreflight p;
    bool have;
    double at;
    std::string extent;
    {
        std::lock_guard<std::mutex> lock(s.m);
        p = s.preflight;
        have = s.havePreflight;
        at = s.preflightAt;
        extent = s.extentResult;
    }
    {
        auto offline = requireConnection(ui);
        if (button(kPreflightLabel, {.tone = have ? Tone::Secondary : Tone::Primary, .icon = icon::Probe,
                                     .tooltip = "Read-only development report: the placement anchors (fingerprinted by their EBOOT values), the class "
                                                "records, the three AI spawn-context schemas, the variable tag heap and the six fixed tag pools. Nothing "
                                                "is called or written.",
                                     .busy = ui.busy(kPreflightLabel)})) {
            noteQueued(kPreflightLabel);
            ui.post(kPreflightLabel, preflightJob);
        }
    }
    ImGui::SameLine(0, px(8));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(6));
    badgePill(kBadgeDev);
    if (have) {
        ImGui::SameLine(0, px(12));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(8));
        caption((std::to_string(p.ok) + " ok  \xC2\xB7  " + std::to_string(p.warnings) + " to check  \xC2\xB7  " + std::to_string(p.failures) + " failed  \xC2\xB7  " +
                 ago(hoponsui::nowSeconds() - at))
                    .c_str(),
                p.failures ? pal::Danger : pal::Muted);
    }
    gap(sp::M);
    if (have && beginTable("##preflight", 3, std::min(420.0f, 30.0f + 30.0f * float(p.lines.size())), ImGuiTableFlags_BordersOuter)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##s", ImGuiTableColumnFlags_WidthFixed, px(64));
        ImGui::TableSetupColumn("Check", ImGuiTableColumnFlags_WidthFixed, px(200));
        ImGui::TableSetupColumn("What Was Read", ImGuiTableColumnFlags_WidthStretch);
        tableHeaders();
        for (size_t i = 0; i < p.lines.size(); ++i) {
            const auto& l = p.lines[i];
            ImGui::TableNextRow(0, px(30));
            ImGui::PushID(int(i));
            ImGui::TableSetColumnIndex(0);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(2));
            const ImVec4 c = l.status == hopons::CheckLine::Ok ? pal::Success : l.status == hopons::CheckLine::Warn ? pal::Warning
                             : l.status == hopons::CheckLine::Fail                                       ? pal::Danger
                                                                                                          : pal::Info;
            pill(hopons::checkStatusText(l.status), c);
            ImGui::TableSetColumnIndex(1);
            cellText(l.label.c_str(), pal::Text, FontKind::Body, type::Label, 30);
            ImGui::TableSetColumnIndex(2);
            cellText(l.detail.c_str(), pal::Muted, FontKind::Body, type::Small, 30);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", l.detail.c_str());
            ImGui::PopID();
        }
        endTable();
        gap(sp::XS);
        std::string all;
        for (const auto& l : p.lines) all += std::string(hopons::checkStatusText(l.status)) + ": " + l.label + ": " + l.detail + "\n";
        if (button("Copy Report", {.tone = Tone::Ghost, .icon = icon::Copy, .small = true})) copyToClipboard(all, "preflight report");
    }
    gap(sp::M);
    fieldLabel("Definition Extent");
    gap(sp::XS + 2);
    inputHex("##extenttag", &s.extentTag, 110);
    ImGui::SameLine(0, px(6));
    {
        auto offline = requireConnection(ui);
        DisabledScope none(!s.extentTag, "Enter a tag");
        if (button("Find Extent", {.tone = Tone::Ghost, .icon = icon::Search,
                                   .tooltip = "Which fixed tag pool slot or tag heap block holds a definition (the size its validators may read)",
                                   .busy = ui.busy("Find definition extent")})) {
            const uint32_t tag = s.extentTag;
            ui.post("Find definition extent", [tag](Ctx& c) { extentJob(c, tag); });
        }
    }
    if (!extent.empty()) {
        gap(sp::XS);
        wrapped(extent.c_str(), pal::Muted);
    }
}

void resetOnDisconnect() {
    State& s = st();
    std::lock_guard<std::mutex> lock(s.m);
    s.report.clear();
    s.result.clear();
    if (!Engine::get().connected()) {
        s.def.reset();
        s.defError.clear();
        s.defFor = 0;
        s.liveDefs.clear();
        s.liveDefsKnown = false;
        s.havePreflight = false;
        s.extentResult.clear();
    }
}

void paletteEntries(const std::string& query, std::vector<PaletteEntry>& out) {
    if (query.size() < 3 || !ensureSwapCatalog()) return;
    State& s = st();
    const auto words = hopons::searchWords(query);
    int n = 0;
    const auto& rows = s.cat->rows();
    for (size_t i = 0; i < rows.size() && n < 12; ++i) {
        if (!hopons::matchesAll(s.cat->hay(i), words)) continue;
        PaletteEntry pe;
        pe.label = squadName(rows[i].tag);
        pe.where = "Enemy Swap  \xE2\x80\xBA  Squad " + hx(rows[i].tag);
        pe.icon = icon::Switch;
        const uint32_t tag = rows[i].tag;
        pe.run = [tag] {
            hoponsui::openSwapFor(tag);
            openPage(Page::Spawner, "Squads");
        };
        out.push_back(std::move(pe));
        ++n;
    }
}

}   

BR_FEATURES(hopons_squads) {
    r.section({.id = "hopons.ai", .page = Page::Spawner, .tab = "Squads", .section = kAiSection, .order = 10, .draw = drawAi, .onDisconnect = resetOnDisconnect});
    r.section({.id = "hopons.swap", .page = Page::Spawner, .tab = "Squads", .section = kSwapSection, .order = 20, .draw = drawSwap, .badge = kBadgeNeedsCheck});
     
    r.section({.id = "hopons.preflight", .page = Page::Spawner, .tab = "Squads", .section = kPreflightSection, .order = 30, .draw = drawPreflight,
               .badge = kBadgeNeedsCheck});
    r.action({.id = kRestoreSwapsId, .page = Page::Spawner, .tab = "Squads", .section = kSwapSection, .label = kRestoreSwapsLabel,
              .help = "Put the original enemies back into every squad definition Blackrock swapped.", .run = restoreAllSwapsJob});
    r.action({.id = kPreflightId, .page = Page::Spawner, .tab = "Squads", .section = kPreflightSection, .label = kPreflightLabel,
              .help = "Read-only development report on the AI spawn context and placement anchors.", .run = preflightJob, .badge = kBadgeDev});
    ui::styleSection(kAiSection, {.fullWidth = true, .icon = ui::icon::Spawner});
    ui::styleSection(kSwapSection, {.fullWidth = true, .icon = ui::icon::Switch, .ownsActions = true});
    ui::styleSection(kPreflightSection, {.fullWidth = true, .icon = ui::icon::Probe, .ownsActions = true});
    ui::addPaletteProvider(paletteEntries);
}

}   
