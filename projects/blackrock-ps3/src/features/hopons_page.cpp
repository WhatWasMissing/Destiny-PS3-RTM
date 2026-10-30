 
 
 
 
 
 
 
 
 
 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>
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
#include "game/hopons_attach.h"
#include "game/hopons_catalog.h"
#include "game/names_catalog.h"
#include "game/world.h"
#include "ui/widgets.h"

namespace brps3 {
namespace {

using namespace ui;
using hopons::HopEntry;
using hopons::Life;
using hopons::Receipt;
using hoponsui::ago;
using hoponsui::hx;

constexpr const char* kFavKey = "hopons.favorites";
constexpr const char* kRecentKey = "hopons.recent";
constexpr const char* kSelectedKey = "hopons.selected";
constexpr const char* kTargetKey = "hopons.target_mode";
constexpr const char* kAutoRemoveKey = "hopons.remove_on_panic";
constexpr const char* kApplyId = "hopons.apply_selected";
constexpr const char* kApplyLabel = "Apply Selected Hop-On";
constexpr const char* kRemoveMineId = "hopons.remove_mine";
constexpr const char* kRemoveMineLabel = "Remove My Hop-Ons";
constexpr const char* kVerifyId = "hopons.verify";
constexpr const char* kVerifyLabel = "Verify Tracked Hop-Ons";
constexpr const char* kScanId = "hopons.scan_loaded";
constexpr const char* kScanLabel = "Scan Loaded Hop-Ons";
constexpr const char* kTargetsJob = "Read hop-on targets";
constexpr const char* kCatalogSection = "Hop-on catalog";
constexpr const char* kTrackedSection = "Tracked attachments";
constexpr float kRowH = 30;
constexpr float kTrackRowH = 40;    

enum TargetMode { TargetYou = 0, TargetWeapon = 1, TargetObject = 2 };
const char* const kTargetModes[] = {"You", "Your Weapon", "Nearby"};

struct TargetRow {
    uint32_t handle = 0;
    uint8_t type = 0;
    float distance = 0;
    std::string name;
    bool playerWeapon = false;
};

struct CheckResult {
    bool have = false;
    uint32_t tag = 0;
    hopons::HopResult status = hopons::HopResult::NotResident;
    std::string why;
    uint8_t type = 0;
    uint32_t item = 0, itemClass = 0, body = 0, definition = 0, page = 0, index = 0;
};

struct State {
     
    bool loaded = false;
    const hopons::HopCatalog* cat = nullptr;
    std::vector<int> view;
    bool viewDirty = true;
    std::string filter;
    int kind = 0;                        
    int type = 0;                        
    bool namedOnly = false;              
    std::unordered_set<uint32_t> favs;
    std::vector<uint32_t> recents;
    ImGuiTableColumnSortSpecs sortSpec{};
    bool sorted = false;
    uint32_t selectedTag = 0;
    bool scrollToSelected = false;
    bool revealDetail = false;
    bool revealCatalog = false;
    double flashUntil = 0;
    float detailH = 0;
    bool lastSearchActive = false;
    bool renaming = false;
    std::string renameText;
    int targetMode = TargetYou;
    uint32_t objectTarget = 0;           
    uint32_t weaponTarget = 0;           
    int trackedFilter = 0;               
    uint32_t checkTag = 0x80A6A012;
    uint64_t residencyRevision = 0;

     
    std::mutex m;
    std::vector<Receipt> receipts;
    uint32_t nextId = 1;
    std::string lastResult;
    bool lastOk = true;
    uint32_t lastTag = 0;
    std::vector<TargetRow> targets;
    uint32_t playerHandle = 0;
    std::string targetsNote;
    double targetsAt = 0;
    std::unordered_set<uint32_t> loadedTags;
    bool residencyKnown = false;
    double residencyAt = 0;
    size_t residencyPages = 0;
    std::string residencyProgress;
    float residencyFraction = -1;
    std::string batchProgress;
    CheckResult check;
    uint32_t armedTag = 0;               
    int armedMode = TargetYou;
    uint32_t armedObject = 0;
    std::atomic<bool> scanStop{false}, batchStop{false};
    std::atomic<bool> scanning{false}, batching{false};
};

State& st() {
    static State s;
    return s;
}

struct SessionHooks {
    bool disconnectHooked = false;
};

 
 
 

std::string displayName(uint32_t tag) {
    if (const auto own = names::Labels::get().find(tag); own && !own->name.empty()) return own->name;
    const HopEntry* e = hopons::hopCatalog().find(tag);
    if (e && !e->name.empty()) return e->name;
    const std::string n = names::tagName(tag);
    if (!n.empty()) return n;
    if (e) return std::string(e->archetype.empty() ? hopons::hopTypeName(e->type) : e->archetype.c_str()) + " \xC2\xB7 " + hx(tag);
    return names::tagLabel(tag);
}

bool hasRealName(uint32_t tag) {
    if (const auto own = names::Labels::get().find(tag); own && !own->name.empty()) return true;
    const HopEntry* e = hopons::hopCatalog().find(tag);
    return (e && !e->name.empty()) || !names::tagName(tag).empty();
}

std::string shortName(uint32_t tag) {
    std::string n = displayName(tag);
    if (n.size() > 30) n = n.substr(0, 28) + "..";
    return n;
}

std::string targetText(const Receipt& r) {
    if (r.self) return "you";
    return std::string(world::typeName(r.targetType)) + " " + hx(r.target);
}

ImVec4 lifeColor(Life l) {
    switch (l) {
    case Life::Active: return pal::Success;
    case Life::Unverified: return pal::Warning;
    case Life::RemovalRequested: return pal::Info;
    case Life::Gone: return pal::Muted;
    case Life::Removed: return pal::Muted;
    }
    return pal::Muted;
}

 
 
 

 
void seedDemo() {
    State& s = st();
    std::lock_guard<std::mutex> lock(s.m);
    const int64_t now = hoponsui::unixNow();
    auto receipt = [&](uint32_t id, uint32_t tag, uint32_t target, bool self, uint8_t type, uint8_t targetType, Life life, int64_t ago) {
        Receipt r;
        r.id = id;
        r.tag = tag;
        r.target = target;
        r.self = self;
        r.type = type;
        r.targetType = targetType;
        r.life = life;
        r.appliedAt = now - ago;
        r.acc.accessor = 0x3D600000u + id * 0x11;
        r.note = hopons::lifeText(life);
        s.receipts.push_back(r);
    };
    receipt(1, 0x80A723C2, 0x5A5A0007, true, 20, 9, Life::Active, 300);
    receipt(2, 0x80A6A016, 0x5A5A0007, true, 20, 9, Life::Active, 120);
    receipt(3, 0x80A38B9B, 0x5A6B0031, false, 22, 11, Life::Active, 90);
    receipt(4, 0x80A723F6, 0x5A5A0007, true, 20, 9, Life::Removed, 600);
    s.nextId = 5;
    int n = 0;
    for (const HopEntry& e : hopons::hopCatalog().all())
        if (!e.name.empty() && (n++ % 3) == 0) s.loadedTags.insert(e.tag);
    s.loadedTags.insert(0x80A723C2);
    s.residencyKnown = true;
    s.residencyAt = hoponsui::nowSeconds() - 42;
    s.residencyPages = 9;
    s.playerHandle = 0x5A5A0007;
    s.targets = {{0x5A6B0031, 11, 0.4f, "Auto Rifle", true},       {0x5A6C0032, 11, 0.4f, "Hand Cannon", true},
                 {0x5B020044, 9, 12.5f, "Fallen Captain", false}, {0x5B030045, 9, 18.0f, "Fallen Dreg", false},
                 {0x5B100050, 12, 26.0f, "Pike", false},          {0x5B110051, 9, 41.0f, "", false}};
    s.targetsAt = hoponsui::nowSeconds() - 12;
    s.targetsNote = "612 objects scanned";
    s.lastTag = 0x80A723C2;
    s.lastOk = true;
    s.lastResult = "Attached to you";
}

bool ensureCatalog() {
    State& s = st();
    if (s.loaded) return s.cat != nullptr;
    s.loaded = true;
    const auto& cat = hopons::hopCatalog();
    if (!cat.error.empty() || !cat.size()) return false;
    s.cat = &cat;
    Settings& settings = Engine::get().settings();
    for (const std::string& h : settings.list(kFavKey)) {
        try {
            s.favs.insert(world::parseHex(h));
        } catch (...) {
        }
    }
    for (const std::string& h : settings.list(kRecentKey)) {
        try {
            s.recents.push_back(world::parseHex(h));
        } catch (...) {
        }
    }
    try {
        const std::string last = settings.str(kSelectedKey, "");
        if (!last.empty()) s.selectedTag = world::parseHex(last);
    } catch (...) {
    }
    s.targetMode = std::clamp(settings.integer(kTargetKey, TargetYou), 0, 2);
    char* demo = nullptr;
    size_t n = 0;
    if (_dupenv_s(&demo, &n, "BRPS3_UI_DEMO") == 0 && demo && demo[0] == '1' && !s.selectedTag) {
        s.selectedTag = 0x80A723C2;    
        s.favs.insert(0x80A723C2);
        s.favs.insert(0x80A6A016);
        s.recents = {0x80A6A016, 0x80A723F6};
        seedDemo();
    }
    free(demo);
    s.scrollToSelected = s.selectedTag != 0;
    return true;
}

int indexOf(uint32_t tag) {
    State& s = st();
    if (!s.cat) return -1;
    const auto& all = s.cat->all();
    const auto it = std::lower_bound(all.begin(), all.end(), tag, [](const HopEntry& e, uint32_t t) { return e.tag < t; });
    return it != all.end() && it->tag == tag ? int(it - all.begin()) : -1;
}

void select(uint32_t tag) {
    State& s = st();
    if (s.selectedTag == tag) return;
    s.selectedTag = tag;
    s.scrollToSelected = true;
    s.renaming = false;
    Engine::get().settings().set(std::string(kSelectedKey), hx(tag));
}

void toggleFavourite(uint32_t tag) {
    State& s = st();
    if (s.favs.count(tag)) s.favs.erase(tag);
    else s.favs.insert(tag);
    Engine::get().settings().toggleInList(kFavKey, hx(tag));
    if (s.kind == 1) s.viewDirty = true;
}

void pushRecent(uint32_t tag) {
    State& s = st();
    s.recents.erase(std::remove(s.recents.begin(), s.recents.end(), tag), s.recents.end());
    s.recents.insert(s.recents.begin(), tag);
    if (s.recents.size() > 20) s.recents.resize(20);
    Engine::get().settings().pushRecent(kRecentKey, hx(tag), 20);
    if (s.kind == 2) s.viewDirty = true;
}

void rebuildView() {
    State& s = st();
    s.view.clear();
    if (!s.cat) return;
    const auto words = hopons::searchWords(s.filter);
    std::unordered_set<uint32_t> loaded;
    bool known = false;
    {
        std::lock_guard<std::mutex> lock(s.m);
        loaded = s.loadedTags;
        known = s.residencyKnown;
    }
    const auto& all = s.cat->all();
    auto accept = [&](int i) {
        const HopEntry& e = all[size_t(i)];
        if (s.type && e.type != 19 + s.type) return false;
        if (s.namedOnly && !hasRealName(e.tag)) return false;
        if (s.kind == 1 && !s.favs.count(e.tag)) return false;
        if (s.kind == 3 && (!known || !loaded.count(e.tag))) return false;
        if (words.empty()) return true;
        std::string hay = s.cat->hay(size_t(i));
        if (const auto own = names::Labels::get().find(e.tag)) hay += " " + hopons::lowerCopy(own->name + " " + own->notes);
        return hopons::matchesAll(hay, words);
    };
    if (s.kind == 2) {
        for (uint32_t t : s.recents)
            if (const int i = indexOf(t); i >= 0 && accept(i)) s.view.push_back(i);
    } else {
        for (int i = 0; i < int(all.size()); ++i)
            if (accept(i)) s.view.push_back(i);
    }
    if (s.kind != 2) {
         
        const ImGuiID col = s.sorted ? s.sortSpec.ColumnUserID : 1;
        const bool asc = !s.sorted || s.sortSpec.SortDirection != ImGuiSortDirection_Descending;
         
        std::unordered_map<int, std::pair<bool, std::string>> keys;
        if (col == 1 || col == 3)
            for (int i : s.view) {
                const HopEntry& e = all[size_t(i)];
                keys[i] = col == 1 ? std::make_pair(hasRealName(e.tag), hopons::lowerCopy(displayName(e.tag))) : std::make_pair(true, hopons::lowerCopy(e.location()));
            }
        std::stable_sort(s.view.begin(), s.view.end(), [&](int a, int b) {
            const HopEntry& x = all[size_t(a)];
            const HopEntry& y = all[size_t(b)];
            int c = 0;
            switch (col) {
            case 0: c = int(s.favs.count(y.tag)) - int(s.favs.count(x.tag)); break;
            case 2: c = int(x.type) - int(y.type); break;
            case 4: c = x.tag < y.tag ? -1 : x.tag > y.tag ? 1 : 0; break;
            default: {
                const auto& kx = keys[a];
                const auto& ky = keys[b];
                c = kx.first != ky.first ? (kx.first ? -1 : 1) : kx.second.compare(ky.second);
            }
            }
            return asc ? c < 0 : c > 0;
        });
    }
    s.viewDirty = false;
}

 
 
 

void requireHop(Ctx& c) {
    c.require(world::kFingerprints, "Hop-ons");
    c.require(hopons::kHopFingerprints, "Hop-ons");
}

void setResult(uint32_t tag, bool ok, const std::string& text) {
    State& S = st();
    std::lock_guard<std::mutex> lock(S.m);
    S.lastTag = tag;
    S.lastOk = ok;
    S.lastResult = text;
}

void storeReceipt(const Receipt& r) {
    State& S = st();
    std::lock_guard<std::mutex> lock(S.m);
    for (Receipt& x : S.receipts)
        if (x.id == r.id) {
            x = r;
            return;
        }
    S.receipts.push_back(r);
}

void removeAllForTeardown(Ctx& c, const char* why);

 
void hookTeardown(Ctx& c) {
    c.onPanic("hopons.attachments", [](Ctx& k) { removeAllForTeardown(k, "Panic"); });
    auto& hooks = c.state<SessionHooks>("hopons.hooks");
    if (!hooks.disconnectHooked) {
        hooks.disconnectHooked = true;
        c.onDisconnect([](Ctx& k) { removeAllForTeardown(k, "disconnect"); });
    }
}

void removeAllForTeardown(Ctx& c, const char* why) {
    if (!c.settings().flag(kAutoRemoveKey, true)) return;
    State& S = st();
    std::vector<Receipt> live;
    {
        std::lock_guard<std::mutex> lock(S.m);
        for (const Receipt& r : S.receipts)
            if (r.life == Life::Active) live.push_back(r);
    }
    if (live.empty()) return;
    size_t removed = 0;
    try {
        requireHop(c);    
        world::RuntimeReader mem(c.rt());
        for (Receipt& r : live) {
            try {
                if (hopons::removeHop(c.rt(), mem, r) == hopons::HopResult::Ok) ++removed;
            } catch (const std::exception& e) {
                r.note = e.what();
            }
            storeReceipt(r);
        }
    } catch (const std::exception& e) {
        c.log(std::string("[hop-ons] ") + why + ": could not remove attachments: " + e.what());
        return;
    }
    c.log("[hop-ons] " + std::string(why) + ": removed " + std::to_string(removed) + " of " + std::to_string(live.size()) + " tracked attachments");
}

struct ResolvedTarget {
    uint32_t handle = 0;
    bool self = false;
    std::string text;
};

ResolvedTarget resolveTarget(world::Reader& mem, int mode, uint32_t object) {
    ResolvedTarget t;
    if (mode == TargetYou) {
        const world::LocalPlayer p = world::requireLocalPlayer(mem);
        t.handle = p.handle;
        t.self = true;
        t.text = "you";
        return t;
    }
    if (mode == TargetWeapon) {
        const world::LocalPlayer p = world::requireLocalPlayer(mem);
        const auto scan = world::readObjects(mem);
        std::vector<world::SObject> weapons;
        for (const auto& o : world::childrenOf(scan, p.handle))
            if (o.live() && o.type == world::TYPE_WEAPON) weapons.push_back(o);
        if (weapons.empty()) throw Error("you carry no weapon object right now");
        const auto chosen = std::find_if(weapons.begin(), weapons.end(), [&](const world::SObject& o) { return o.handle == object; });
        t.handle = chosen != weapons.end() ? chosen->handle : weapons.front().handle;
        t.text = "your weapon " + hx(t.handle);
        return t;
    }
    if (!world::validHandle(object)) throw Error("pick a target in the Nearby object list first");
    const auto o = world::readSObject(mem, object);
    if (!o) throw Error("target " + hx(object) + " is gone: refresh the list");
    t.handle = object;
    t.text = std::string(world::typeName(o->type)) + " " + hx(object);
    return t;
}

void applyJob(Ctx& c, uint32_t tag, int mode, uint32_t object) {
    requireHop(c);
    world::RuntimeReader mem(c.rt());
    const ResolvedTarget t = resolveTarget(mem, mode, object);
    State& S = st();
    std::vector<Receipt> snapshot;
    uint32_t id;
    {
        std::lock_guard<std::mutex> lock(S.m);
        snapshot = S.receipts;
        id = S.nextId++;
    }
     
    for (Receipt& r : snapshot) r.tag = names::toPs3Tag(r.tag);
    auto o = hopons::applyHop(c.rt(), mem, names::toPs3Tag(tag), t.handle, t.self, snapshot, id, [&c](const std::string& m) { c.log("[hop-ons] " + m); });
    if (o.receipt) {
        o.receipt->tag = tag;
        storeReceipt(*o.receipt);
        hookTeardown(c);
    }
    setResult(tag, o.result == hopons::HopResult::Ok, o.result == hopons::HopResult::Ok ? "Attached to " + t.text : o.message);
    if (o.result != hopons::HopResult::Ok) throw Error(displayName(tag) + ": " + o.message);
    c.toast("Applied " + displayName(tag) + " to " + t.text);
}

void removeJob(Ctx& c, uint32_t id) {
    requireHop(c);
    world::RuntimeReader mem(c.rt());
    State& S = st();
    Receipt r;
    {
        std::lock_guard<std::mutex> lock(S.m);
        const auto it = std::find_if(S.receipts.begin(), S.receipts.end(), [id](const Receipt& x) { return x.id == id; });
        if (it == S.receipts.end()) throw Error("that attachment is no longer tracked");
        r = *it;
    }
    const auto res = hopons::removeHop(c.rt(), mem, r, [&c](const std::string& m) { c.log("[hop-ons] " + m); });
    storeReceipt(r);
    setResult(r.tag, res == hopons::HopResult::Ok, res == hopons::HopResult::Ok ? std::string("Removed from ") + targetText(r) : r.note);
    if (res == hopons::HopResult::Refused) throw Error("Remove " + displayName(r.tag) + ": " + r.note);
    c.toast((res == hopons::HopResult::Ok ? "Removed " : "Not attached any more: ") + displayName(r.tag));
}

void removeManyJob(Ctx& c, std::vector<uint32_t> ids, const std::string& what) {
    requireHop(c);
    State& S = st();
    S.batchStop = false;
    S.batching = true;
    struct Done {
        ~Done() {
            st().batching = false;
            std::lock_guard<std::mutex> lock(st().m);
            st().batchProgress.clear();
        }
    } done;
    world::RuntimeReader mem(c.rt());
    size_t removed = 0, n = 0;
    for (uint32_t id : ids) {
        if (c.cancelled() || S.batchStop) {
            c.log("[hop-ons] removal stopped after " + std::to_string(n) + " of " + std::to_string(ids.size()));
            break;
        }
        {
            std::lock_guard<std::mutex> lock(S.m);
            S.batchProgress = "Removing " + std::to_string(n + 1) + "/" + std::to_string(ids.size());
        }
        Receipt r;
        {
            std::lock_guard<std::mutex> lock(S.m);
            const auto it = std::find_if(S.receipts.begin(), S.receipts.end(), [id](const Receipt& x) { return x.id == id; });
            if (it == S.receipts.end()) {
                ++n;
                continue;
            }
            r = *it;
        }
        if (hopons::removeHop(c.rt(), mem, r, [&c](const std::string& m) { c.log("[hop-ons] " + m); }) == hopons::HopResult::Ok) ++removed;
        storeReceipt(r);
        ++n;
    }
    setResult(0, true, "Removed " + std::to_string(removed) + " of " + std::to_string(ids.size()) + " (" + what + ")");
    c.toast("Removed " + std::to_string(removed) + " of " + std::to_string(ids.size()) + " hop-ons (" + what + ")");
}

std::vector<uint32_t> activeIds(bool selfOnly, uint32_t target = 0) {
    State& S = st();
    std::lock_guard<std::mutex> lock(S.m);
    std::vector<uint32_t> ids;
    for (const Receipt& r : S.receipts)
        if (r.life == Life::Active && (!selfOnly || r.self) && (!target || r.target == target)) ids.push_back(r.id);
    return ids;
}

void verifyJob(Ctx& c) {
    c.require(world::kFingerprints, "Hop-ons");
    world::RuntimeReader mem(c.rt());
    State& S = st();
    std::vector<Receipt> copy;
    {
        std::lock_guard<std::mutex> lock(S.m);
        copy = S.receipts;
    }
    const size_t changed = hopons::refreshReceipts(mem, copy);
    for (const Receipt& r : copy) storeReceipt(r);
    size_t active = 0;
    for (const Receipt& r : copy) active += r.life == Life::Active;
    c.toast("Verified " + std::to_string(copy.size()) + " tracked hop-ons: " + std::to_string(active) + " attached" +
            (changed ? ", " + std::to_string(changed) + " changed" : ""));
}

void targetsJob(Ctx& c) {
    c.require(world::kFingerprints, "Hop-ons");
    world::RuntimeReader mem(c.rt());
    const auto lookup = world::findLocalPlayer(mem);
    const auto scan = world::readObjects(mem);
    std::vector<TargetRow> rows;
    const world::Vec3 me = lookup.player ? lookup.player->object.position : world::Vec3{};
    const uint32_t player = lookup.player ? lookup.player->handle : 0;
    for (const world::SObject& o : scan.objects) {
        if (!o.live() || o.handle == player) continue;
        if (o.type != world::TYPE_BIPED && o.type != world::TYPE_CREATURE && o.type != world::TYPE_WEAPON && o.type != world::TYPE_VEHICLE &&
            o.type != world::TYPE_TURRET)
            continue;
        TargetRow r;
        r.handle = o.handle;
        r.type = o.type;
        r.distance = lookup.player ? world::distance(o.position, me) : 0;
        r.playerWeapon = player && o.parent == player && o.type == world::TYPE_WEAPON;
        if (!std::isfinite(r.distance)) r.distance = 1e9f;
        rows.push_back(r);
    }
    std::sort(rows.begin(), rows.end(), [](const TargetRow& a, const TargetRow& b) {
        if (a.playerWeapon != b.playerWeapon) return a.playerWeapon;
        return a.distance < b.distance;
    });
    if (rows.size() > 48) rows.resize(48);
     
    try {
        const world::TagTable t = world::readTagTable(mem);
        int named = 0;
        for (TargetRow& r : rows) {
            if (named >= 16 || c.cancelled()) break;
            if (const auto o = world::readSObject(mem, scan.pool, r.handle)) {
                const uint32_t tag = world::tagOfObject(mem, t, *o);
                if (tag) r.name = names::tagLabel(tag);
            }
            ++named;
        }
    } catch (const std::exception&) {
    }
    State& S = st();
    std::lock_guard<std::mutex> lock(S.m);
    S.targets = std::move(rows);
    S.playerHandle = player;
    S.targetsAt = hoponsui::nowSeconds();
    S.targetsNote = lookup.player ? std::to_string(scan.objects.size()) + " objects scanned" : std::string("no local player: ") + lookup.message;
}

void scanJob(Ctx& c) {
    c.require(world::kFingerprints, "Hop-ons");
    State& S = st();
    if (S.scanning.exchange(true)) throw Error("a residency scan is already running");
    S.scanStop = false;
    struct Done {
        ~Done() {
            st().scanning = false;
            std::lock_guard<std::mutex> lock(st().m);
            st().residencyProgress.clear();
            st().residencyFraction = -1;
        }
    } finished;
    world::RuntimeReader mem(c.rt());
    hopons::TagReader tags(mem);
    tags.loadAllPages();
    std::map<uint32_t, std::vector<uint32_t>> byPage;
    for (const HopEntry& e : hopons::hopCatalog().all()) byPage[world::tagPage(names::toPs3Tag(e.tag))].push_back(names::toPs3Tag(e.tag));
    std::vector<world::TagPage> pages;
    for (const auto& [page, list] : byPage) {
        if (page >= tags.table().pageCount) continue;
        const world::TagPage p = tags.page(page);
        if (p.live() && p.itemStride == world::kPackageItemStride && p.itemsEnd > p.items) pages.push_back(p);
    }
    std::unordered_set<uint32_t> loaded;
    size_t done = 0;
    for (const world::TagPage& p : pages) {
        if (c.cancelled() || S.scanStop) break;
        {
            std::lock_guard<std::mutex> lock(S.m);
            S.residencyProgress = "Reading page " + hx(p.page).substr(4) + "  (" + std::to_string(done + 1) + "/" + std::to_string(pages.size()) + ")";
            S.residencyFraction = pages.empty() ? -1.0f : float(done) / float(pages.size());
        }
        const uint32_t len = std::min<uint32_t>(p.itemsEnd - p.items, world::kPackageItems * world::kPackageItemStride);
        if (const auto items = mem.tryBytes(p.items, len))
            for (uint32_t tag : byPage[p.page]) {
                const uint32_t off = world::tagIndex(tag) * world::kPackageItemStride;
                if (items->has(off, 4) && items->u32(off) == world::kEntityClass) loaded.insert(names::fromPs3Tag(tag));
            }
        ++done;
    }
    const bool complete = done == pages.size();
    {
        std::lock_guard<std::mutex> lock(S.m);
        S.loadedTags = std::move(loaded);
        S.residencyKnown = true;
        S.residencyAt = hoponsui::nowSeconds();
        S.residencyPages = done;
    }
    size_t n;
    {
        std::lock_guard<std::mutex> lock(S.m);
        n = S.loadedTags.size();
    }
    c.toast(std::to_string(n) + " of " + std::to_string(hopons::hopCatalog().size()) + " hop-ons are loaded" + (complete ? "" : " (scan stopped early)"));
}

void checkJob(Ctx& c, uint32_t tag) {
    c.require(world::kFingerprints, "Hop-ons");
    world::RuntimeReader mem(c.rt());
    hopons::TagReader tags(mem);
    const auto d = hopons::checkDefinition(mem, tags, names::toPs3Tag(tag));
    CheckResult r;
    r.have = true;
    r.tag = tag;
    r.status = d.status;
    r.why = d.why;
    r.type = d.type;
    r.item = d.map.entry;
    r.itemClass = d.map.word;
    r.body = d.map.body;
    r.definition = d.definition;
    r.page = world::tagPage(tag);
    r.index = world::tagIndex(tag);
    c.log("[hop-ons] check " + hx(tag) + ": " + hopons::hopResultText(d.status) + (d.why.empty() ? "" : " (" + d.why + ")"));
    State& S = st();
    std::lock_guard<std::mutex> lock(S.m);
    S.check = r;
    if (d.status == hopons::HopResult::Ok) S.loadedTags.insert(tag);
}

 
void applySelected(Ctx& c) {
    State& S = st();
    uint32_t tag;
    int mode;
    uint32_t object;
    {
        std::lock_guard<std::mutex> lock(S.m);
        tag = S.armedTag;
        mode = S.armedMode;
        object = S.armedObject;
    }
    if (!tag) {
        try {
            tag = world::parseHex(c.settings().str(kSelectedKey, ""));
            mode = std::clamp(c.settings().integer(kTargetKey, TargetYou), 0, 2);
        } catch (...) {
        }
    }
    if (!tag) throw Error("select a hop-on on the Hop-ons page first");
    applyJob(c, tag, mode, object);
}

void removeMine(Ctx& c) {
    const auto ids = activeIds(true);
    if (ids.empty()) {
        c.toast("No hop-ons tracked on you");
        return;
    }
    removeManyJob(c, ids, "on you");
}

 
 
 

std::string applyLabel(uint32_t tag) { return "Apply " + shortName(tag); }

void postApply(uint32_t tag, int mode, uint32_t object) {
    if (!Engine::get().connected()) {
        notify(std::string(kConnectFirst) + " to apply " + shortName(tag), Level::Warn);
        return;
    }
    const std::string job = applyLabel(tag);
    noteQueued(job);
    pushRecent(tag);
    Engine::get().post(job, [tag, mode, object](Ctx& c) { applyJob(c, tag, mode, object); }, [tag](bool ok, const std::string& err) {
        if (ok) flashSuccess("hopons.apply");
        else setResult(tag, false, err);
    });
}

void postRemove(const Receipt& r) {
    const std::string job = "Remove " + shortName(r.tag);
    noteQueued(job);
    const uint32_t id = r.id;
    Engine::get().post(job, [id](Ctx& c) { removeJob(c, id); });
}

void postRemoveMany(std::vector<uint32_t> ids, const std::string& what) {
    if (ids.empty()) return;
    const std::string job = "Remove hop-ons (" + what + ")";
    noteQueued(job);
    Engine::get().post(job, [ids, what](Ctx& c) { removeManyJob(c, ids, what); });
}

void postTargets() {
    if (Engine::get().busy(kTargetsJob) || !Engine::get().connected()) return;
    Engine::get().post(kTargetsJob, targetsJob);
}

void postScan() {
    if (!Engine::get().connected()) {
        notify(std::string(kConnectFirst) + " to scan", Level::Warn);
        return;
    }
    noteQueued(kScanLabel);
    trackActivity("hopons.scan", "Scanning loaded hop-ons", Page::HopOns, [] { return st().scanning.load() || Engine::get().busy(kScanLabel); },
                  [] { st().scanStop = true; }, pal::Accent);
    Engine::get().post(kScanLabel, scanJob, [](bool ok, const std::string&) {
        if (ok) st().viewDirty = true;
    });
}

 
 
 

void residencyPill(uint32_t tag) {
    State& s = st();
    bool known, loaded;
    {
        std::lock_guard<std::mutex> lock(s.m);
        known = s.residencyKnown;
        loaded = s.loadedTags.count(tag) != 0;
    }
    if (!known) pill("Residency Unknown", pal::Muted, icon::Info);
    else if (loaded) pill("Loaded", pal::Success, icon::Check);
    else pill("Not Loaded", pal::Warning, icon::Warning);
}

void drawTargetPicker(Ui& ui, const HopEntry* e) {
    State& s = st();
    fieldLabel("Target");
    gap(sp::XS + 2);
    std::vector<std::string> modes(std::begin(kTargetModes), std::end(kTargetModes));
    if (pillTabs("##target", modes, &s.targetMode)) {
        Engine::get().settings().set(std::string(kTargetKey), s.targetMode);
        bool never;
        {
            std::lock_guard<std::mutex> lock(s.m);
            never = s.targetsAt == 0;
        }
        if (s.targetMode != TargetYou && never) postTargets();    
    }
    gap(sp::S);
    std::vector<TargetRow> rows;
    uint32_t player;
    std::string note;
    double at;
    {
        std::lock_guard<std::mutex> lock(s.m);
        rows = s.targets;
        player = s.playerHandle;
        note = s.targetsNote;
        at = s.targetsAt;
    }
    const bool reading = ui.busy(kTargetsJob);
    if (s.targetMode == TargetYou) {
        caption(player ? ("Your character  \xC2\xB7  " + hx(player)).c_str() : "Your character (resolved when you apply)", pal::Muted);
    } else {
        std::vector<TargetRow> list;
        for (const TargetRow& r : rows)
            if ((s.targetMode == TargetWeapon) == r.playerWeapon) list.push_back(r);
        if (s.targetMode == TargetWeapon && list.empty())
            caption(at > 0 ? "No weapon objects on you in the last read: Apply uses the first weapon it finds." : "Apply uses the first weapon you carry.",
                    pal::Muted);
        if (!list.empty() && beginTable("##targets", 3, std::min(168.0f, 30.0f + 28.0f * float(list.size())), ImGuiTableFlags_BordersOuter)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, px(64));
            ImGui::TableSetupColumn("Dist.", ImGuiTableColumnFlags_WidthFixed, px(52));
            tableHeaders();
            for (const TargetRow& r : list) {
                ImGui::TableNextRow(0, px(28));
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(int(r.handle));
                uint32_t& pick = s.targetMode == TargetWeapon ? s.weaponTarget : s.objectTarget;
                const bool sel = pick == r.handle;
                const float y = ImGui::GetCursorPosY();
                if (tableRowSelectable("##t", sel, nullptr, 28 - 2 * ImGui::GetStyle().CellPadding.y / scale())) pick = r.handle;
                ImGui::SetCursorPosY(y);
                const std::string label = r.name.empty() ? hx(r.handle) : r.name;
                cellText(label.c_str(), sel ? pal::Accent : pal::Text, r.name.empty() ? FontKind::Mono : FontKind::Body, r.name.empty() ? type::MonoSmall : type::Label, 28);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("Handle %s", hx(r.handle).c_str());
                ImGui::TableSetColumnIndex(1);
                cellText(world::typeName(r.type), pal::Muted, FontKind::Body, type::Label, 28);
                ImGui::TableSetColumnIndex(2);
                char d[24];
                if (r.distance > 1e8f) snprintf(d, sizeof d, "-");
                else snprintf(d, sizeof d, "%.0f m", double(r.distance));
                cellText(d, pal::Muted, FontKind::Body, type::Label, 28);
                ImGui::PopID();
            }
            endTable();
        } else if (s.targetMode == TargetObject && list.empty()) {
            caption(at > 0 ? "No bipeds, vehicles or weapons in the last read." : "Read the objects around you, then pick one.", pal::Muted);
        }
        gap(sp::XS);
        auto offline = requireConnection(ui);
        if (button(at > 0 ? "Refresh Objects" : "Read Objects", {.tone = Tone::Ghost, .icon = icon::Refresh, .small = true, .busy = reading}))
            postTargets();
        if (at > 0) {
            ImGui::SameLine(0, px(8));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(5));
            caption((note + "  \xC2\xB7  " + ago(hoponsui::nowSeconds() - at)).c_str(), pal::Faint);
        }
    }
     
    if (e) {
        const char* warn = nullptr;
        if (e->type == hopons::TYPE_WEAPON_ATTACHMENT && s.targetMode == TargetYou) warn = "Weapon attachments need a weapon: choose \"Your Weapon\".";
        if (e->type == hopons::TYPE_BIPED_ATTACHMENT && s.targetMode == TargetWeapon) warn = "Biped attachments need a biped: choose \"You\".";
        if (warn) {
            gap(sp::S);
            pill(warn, pal::Warning, icon::Warning);
        }
    }
}

void drawRename(uint32_t tag) {
    State& s = st();
    if (!s.renaming) return;
    gap(sp::S);
    const float saveW = buttonWidth("Save", icon::Check, true), cancelW = buttonWidth("Cancel", nullptr, true);
    const float w = std::max(120.0f, (availWidth() - saveW - cancelW - px(12)) / scale());
    const bool enter = inputText("##rename", &s.renameText, "Your Name for This Hop-On", w, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine(0, px(6));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(3));
    if (button("Save##rename", {.tone = Tone::Primary, .icon = icon::Check, .small = true}) || enter) {
        if (s.renameText.empty()) names::Labels::get().remove(tag);
        else names::Labels::get().set(tag, {s.renameText, "hop-on", false});
        s.renaming = false;
        s.viewDirty = true;
        notify("Saved the name", Level::Good);
    }
    ImGui::SameLine(0, px(6));
    if (button("Cancel##rename", {.tone = Tone::Ghost, .small = true})) s.renaming = false;
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
        ImGui::BeginChild("##hopdetail", ImVec2(width, 0),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AutoResizeY);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
    } else {
        beginCard("##hopdetailcard", nullptr, {.raised = true});
    }
    if (!s.selectedTag) {
        emptyState(icon::HopOns, "Select a Hop-On",
                   "Pick a Row, Choose a Target and Apply It",
                   child ? std::max(200.0f, height / scale() - 40) : 200);
    } else {
        const uint32_t tag = s.selectedTag;
        const HopEntry* e = s.cat ? s.cat->find(tag) : nullptr;
        {
            std::lock_guard<std::mutex> lock(s.m);
            s.armedTag = tag;
            s.armedMode = s.targetMode;
            s.armedObject = s.targetMode == TargetWeapon ? s.weaponTarget : s.objectTarget;
        }
         
        {
            const float iconsW = px(30) * 2 + px(4);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + availWidth() - iconsW - px(8));
            heading(displayName(tag).c_str(), hasRealName(tag) ? pal::Text : pal::Soft);
            ImGui::PopTextWrapPos();
            const ImVec2 after = ImGui::GetCursorScreenPos();
            ImGui::SameLine();
            alignRight(iconsW);
            if (iconButton("##rename", icon::Tag, "Name this hop-on (saved with your labels)", s.renaming, 30)) {
                s.renaming = !s.renaming;
                const auto own = names::Labels::get().find(tag);
                s.renameText = own ? own->name : (hasRealName(tag) ? displayName(tag) : std::string());
            }
            ImGui::SameLine(0, px(4));
            const bool fav = s.favs.count(tag) != 0;
            if (iconButton("##fav", fav ? icon::StarFill : icon::Star, fav ? "Remove from favourites" : "Add to favourites (they show first in Ctrl+K)", false, 30,
                           fav ? pal::Accent : pal::Muted))
                toggleFavourite(tag);
            ImGui::SetCursorScreenPos(after);
        }
        drawRename(tag);
        gap(sp::XS);
        hashLabel(tag, "tag", pal::Accent);
        ImGui::SameLine(0, px(10));
        pill(e ? hopons::hopTypeName(e->type) : "Not in the Catalogue", e ? pal::Info : pal::Muted);
        if (e && !e->location().empty()) {
            ImGui::SameLine(0, px(6));
            pill(e->location().c_str(), pal::Muted);
        }
        gap(sp::XS + 2);
        residencyPill(tag);
        if (e) {
            gap(sp::S);
            if (!e->package.empty()) caption(e->package.c_str(), pal::Faint);
            std::string evidence;
            if (e->evidence == "localized") evidence = "Localized name candidate";
            else if (e->evidence == "archetype") evidence = "Package archetype";
            else if (e->evidence == "saved") evidence = "Saved name";
            else if (e->evidence == "recovered") evidence = "Recovered name";
            if (!e->identifier.empty()) evidence += std::string(evidence.empty() ? "" : "  \xC2\xB7  ") + "identifier " + e->identifier;
            if (!e->archetype.empty()) evidence += std::string(evidence.empty() ? "" : "  \xC2\xB7  ") + e->archetype;
            if (!evidence.empty()) caption(evidence.c_str(), pal::Muted);
            if (!e->hints.empty()) wrapped(("Also seen as: " + e->hints).c_str(), pal::Faint);
        }
        divider(sp::L);
        drawTargetPicker(ui, e);
        divider(sp::L);
         
        const std::string job = applyLabel(tag);
        const bool applying = ui.busy(job) || ui.busy(kApplyLabel);
        const float menuW = px(kControlH);
        std::string result;
        bool ok;
        uint32_t resultTag;
        size_t onYou = 0, ofThis = 0;
        std::vector<uint32_t> thisIds;
        {
            std::lock_guard<std::mutex> lock(s.m);
            result = s.lastResult;
            ok = s.lastOk;
            resultTag = s.lastTag;
            for (const Receipt& r : s.receipts) {
                if (r.life != Life::Active) continue;
                onYou += r.self;
                if (r.tag == tag) {
                    ++ofThis;
                    thisIds.push_back(r.id);
                }
            }
        }
        {
            auto offline = requireConnection(ui);
            const bool needPick = s.targetMode == TargetObject && !s.objectTarget;
            const float removeW = thisIds.empty() ? 0.0f : buttonWidth("Remove", icon::Remove) + px(8);
            {
                DisabledScope pick(needPick, "Pick a target in the list first");
                if (button("Apply", {.tone = Tone::Primary, .icon = icon::Play, .width = (availWidth() - menuW - px(8) - removeW) / scale(),
                                     .tooltip = "Attach this hop-on to the target (Ctrl+Enter)", .busy = applying, .successKey = "hopons.apply"}))
                    postApply(tag, s.targetMode, s.targetMode == TargetWeapon ? s.weaponTarget : s.objectTarget);
            }
            if (!thisIds.empty()) {
                ImGui::SameLine(0, px(8));
                if (button("Remove", {.icon = icon::Remove, .tooltip = "Remove this hop-on from every target Blackrock attached it to",
                                      .busy = ui.busy("Remove hop-ons (this one)")}))
                    postRemoveMany(thisIds, "this one");
            }
        }
        ImGui::SameLine(0, px(8));
        if (iconButton("##applymenu", icon::More, "Pin \"Apply Selected Hop-On\" to Home or give it a hotkey", false, kControlH)) openItemMenu(kApplyId);
        if (!result.empty() && (resultTag == tag || resultTag == 0)) {
            gap(sp::M);
            pill(result.c_str(), ok ? pal::Success : pal::Danger, ok ? icon::Check : icon::Warning);
        }
        gap(sp::M);
        char buf[160];
        snprintf(buf, sizeof buf, "%zu attached on you  \xC2\xB7  %zu of this one tracked  \xC2\xB7  manage them in Tracked", onYou, ofThis);
        wrapped(buf, pal::Faint);
    }
    if (child) {
        gap(sp::XS);
        s.detailH = ImGui::GetCursorPosY() + px(14);
        ImGui::EndChild();
    } else {
        endCard();
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

void drawCatalog(Ui& ui) {
    State& s = st();
    if (!ensureCatalog()) {
        emptyState(icon::Warning, "Hop-On Catalogue Unavailable",
                   (hopons::hopCatalog().error.empty() ? std::string("data/hopons/hopons.tsv is empty") : hopons::hopCatalog().error).c_str(), 220);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(s.m);
        static uint64_t seen = 0;
        const uint64_t rev = s.residencyKnown ? uint64_t(s.residencyAt * 1000) + s.loadedTags.size() : 0;
        if (rev != seen) {
            seen = rev;
            s.viewDirty = true;
        }
    }
    if (s.revealCatalog) {
        ImGui::SetScrollY(std::max(0.0f, ImGui::GetCursorScreenPos().y - ImGui::GetWindowPos().y + ImGui::GetScrollY() - px(96)));
        s.revealCatalog = false;
    }
    ImGuiIO& io = ImGui::GetIO();
    const float avail = availWidth();
    ImGuiID searchInput = 0;
    {
        const float searchW = std::min(px(320), avail * 0.34f);
        if (searchField("##hopsearch", &s.filter, "Search Name, Tag, Package or Archetype", searchW / scale(), true)) s.viewDirty = true;
        ImGui::PushID("##hopsearch");
        searchInput = ImGui::GetID("##input");
        ImGui::PopID();
        ImGui::SameLine(0, px(12));
        size_t loadedCount;
        bool known;
        {
            std::lock_guard<std::mutex> lock(s.m);
            loadedCount = s.loadedTags.size();
            known = s.residencyKnown;
        }
        std::vector<std::string> kinds = {"All", s.favs.empty() ? std::string("Favourites") : "Favourites  " + std::to_string(s.favs.size()),
                                          s.recents.empty() ? std::string("Recent") : "Recent  " + std::to_string(s.recents.size()),
                                          known ? "Loaded  " + std::to_string(loadedCount) : std::string("Loaded")};
        if (pillTabs("##hopkind", kinds, &s.kind)) s.viewDirty = true;
        if (avail >= px(1040)) ImGui::SameLine(0, px(12));
        else gap(sp::S);
        static const char* const types[] = {"Any Type", "Hop-On", "Biped", "Weapon", "Ship"};
        if (combo("##hoptype", &s.type, types, 5, 118)) s.viewDirty = true;
        ImGui::SameLine(0, px(8));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(3));
        if (button("Named", {.tone = Tone::Ghost, .icon = icon::Tag, .small = true, .tooltip = "Only hop-ons with a recovered, localized or own name",
                             .selected = s.namedOnly})) {
            s.namedOnly = !s.namedOnly;
            s.viewDirty = true;
        }
        if (s.viewDirty) rebuildView();
        char buf[64];
        snprintf(buf, sizeof(buf), "%zu of %zu", s.view.size(), s.cat->size());
        const ImVec2 ts = textSize(FontKind::Body, type::Small, buf);
        ImGui::SameLine();
        if (ImGui::GetCursorPosX() + ts.x < ImGui::GetWindowContentRegionMax().x) {
            alignRight(ts.x);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(8));
            caption(buf, pal::Faint);
        } else {
            ImGui::NewLine();
        }
        if (s.kind == 3 && !known) {
            gap(sp::XS);
            caption("Residency is unknown: run \"Scan Loaded Hop-Ons\" on the Catalog tab (or Ctrl+K).", pal::Warning);
        }
    }
    gap(sp::M);

    const bool split = avail >= px(760);
    const float detailW = split ? std::clamp(avail * 0.38f, px(300), px(440)) : avail;
    const float tableW = split ? avail - detailW - px(16) : avail;
    const bool compactCols = tableW < px(560);
    const float viewH = ImGui::GetWindowHeight();
    const float baseH = std::clamp(viewH - px(250), px(360), px(820));
    const float tableH = split ? std::max(baseH, std::min(s.detailH, px(900))) : baseH;

     
    const ImVec2 tablePos = ImGui::GetCursorScreenPos();
    const bool tableHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseHoveringRect(tablePos, tablePos + ImVec2(tableW, tableH));
    const bool searchActive = ImGui::GetActiveID() == searchInput;
    const bool popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    const auto& all = s.cat->all();
    if (!popup && !s.view.empty() && (searchActive || (tableHovered && !io.WantTextInput))) {
        int pos = -1;
        for (int i = 0; i < int(s.view.size()); ++i)
            if (all[size_t(s.view[size_t(i)])].tag == s.selectedTag) pos = i;
        const int last = int(s.view.size()) - 1;
        const int page = std::max(1, int(tableH / px(kRowH)) - 2);
        int next = pos;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) next = std::min(last, pos + 1);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) next = std::max(0, pos - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) next = std::min(last, std::max(0, pos) + page);
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) next = std::max(0, pos - page);
        if (next != pos && next >= 0) select(all[size_t(s.view[size_t(next)])].tag);
    }
    if (s.lastSearchActive && !searchActive && !s.view.empty() && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))) {
        bool inView = false;
        for (int i : s.view) inView = inView || all[size_t(i)].tag == s.selectedTag;
        if (!inView) select(all[size_t(s.view.front())].tag);
    }
    s.lastSearchActive = searchActive;
    if (!popup && s.selectedTag && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Enter) && (searchActive || !io.WantTextInput) &&
        !ui.busy(applyLabel(s.selectedTag)))
        postApply(s.selectedTag, s.targetMode, s.targetMode == TargetWeapon ? s.weaponTarget : s.objectTarget);

    std::unordered_set<uint32_t> loaded;
    bool known;
    {
        std::lock_guard<std::mutex> lock(s.m);
        loaded = s.loadedTags;
        known = s.residencyKnown;
    }
    ImGui::BeginGroup();
    if (beginTable("##hopcatalog", compactCols ? 4 : 5, tableH / scale(), ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate | ImGuiTableFlags_BordersOuter,
                   tableW / scale())) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##fav", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, px(16), 0);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0, 1);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, px(64), 2);
        if (!compactCols) ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthFixed, px(112), 3);
        ImGui::TableSetupColumn("Tag", ImGuiTableColumnFlags_WidthFixed, px(84), 4);
        starHeaders();
        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsDirty) {
            s.sorted = specs->SpecsCount > 0;
            if (s.sorted) s.sortSpec = specs->Specs[0];
            specs->SpecsDirty = false;
            rebuildView();
        }
        const int tagCol = compactCols ? 3 : 4;
        ImGuiListClipper clip;
        clip.Begin(int(s.view.size()), px(kRowH));
        int scrollTo = -1;
        if (s.scrollToSelected) {
            for (int i = 0; i < int(s.view.size()); ++i)
                if (all[size_t(s.view[size_t(i)])].tag == s.selectedTag) scrollTo = i;
            if (scrollTo >= 0) clip.IncludeItemByIndex(scrollTo);
            s.scrollToSelected = false;
        }
        uint32_t toggleFav = 0;
        ImGui::PushFont(font(FontKind::Body), fontSize(FontKind::Body, type::Label));
        while (clip.Step()) {
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
                const HopEntry& e = all[size_t(s.view[size_t(r)])];
                ImGui::TableNextRow(0, px(kRowH));
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(int(e.tag));
                const float cellY = ImGui::GetCursorPosY();
                const bool sel = s.selectedTag == e.tag;
                if (tableRowSelectable("##row", sel, nullptr, kRowH - 2 * ImGui::GetStyle().CellPadding.y / scale())) {
                    select(e.tag);
                    s.revealDetail = true;
                }
                if (r == scrollTo) ImGui::SetScrollHereY(0.4f);
                ImGui::SetCursorPosY(cellY - px(3));
                const bool fav = s.favs.count(e.tag) != 0;
                const ImVec2 spos = ImGui::GetCursorScreenPos() - ImVec2(px(4), 0);
                ImGui::SetCursorScreenPos(spos);
                if (ImGui::InvisibleButton("##star", ImVec2(px(24), px(24)))) toggleFav = e.tag;
                const bool sh = ImGui::IsItemHovered();
                const char* g = fav ? icon::StarFill : icon::Star;
                const ImVec2 is = textSize(FontKind::Icons, 12, g);
                drawText(ImGui::GetWindowDrawList(), FontKind::Icons, 12, spos + (ImVec2(px(24), px(24)) - is) * 0.5f, u32(fav ? pal::Accent : sh ? pal::Text : pal::Decor), g);
                if (sh) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip(fav ? "Remove from favourites" : "Add to favourites");
                }
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                {
                     
                    const bool named = hasRealName(e.tag);
                    if (known) {
                        const ImVec2 p = ImGui::GetCursorScreenPos();
                        const float cy = p.y + px(kRowH) * 0.5f - ImGui::GetStyle().CellPadding.y;
                        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + px(4), cy), px(3.5f), u32(loaded.count(e.tag) ? pal::Success : pal::Decor));
                        ImGui::SetCursorScreenPos(ImVec2(p.x + px(14), p.y));
                    }
                    const std::string name = displayName(e.tag);
                    cellText(name.c_str(), sel ? pal::Text : named ? pal::Soft : pal::Muted, FontKind::Body, type::Label, kRowH);
                }
                ImGui::TableSetColumnIndex(2);
                cellText(hopons::hopTypeShort(e.type), pal::Muted, FontKind::Body, type::Label, kRowH);
                if (!compactCols) {
                    ImGui::TableSetColumnIndex(3);
                    const std::string loc = e.location();
                    cellText(loc.empty() ? "-" : loc.c_str(), loc.empty() ? pal::Faint : pal::Muted, FontKind::Body, type::Label, kRowH);
                }
                ImGui::TableSetColumnIndex(tagCol);
                cellText(hx(e.tag).c_str(), sel ? pal::Accent : pal::Muted, FontKind::Mono, type::MonoSmall, kRowH);
            }
        }
        ImGui::PopFont();
        if (s.view.empty()) {
            ImGui::TableNextRow(0, px(120));
            ImGui::TableSetColumnIndex(1);
            gap(sp::XL);
            const char* msg = s.kind == 1 && s.favs.empty()   ? "No favourites yet: click the star next to a hop-on."
                              : s.kind == 2 && s.recents.empty() ? "Nothing applied yet: recents fill up as you apply."
                              : s.kind == 3                    ? (known ? "No catalogue hop-on is loaded here." : "Scan first (Catalog tab).")
                                                               : "No hop-ons match.";
            caption(msg, pal::Muted);
        }
        endTable();
        if (toggleFav) toggleFavourite(toggleFav);
    }
    ImGui::EndGroup();
    if (ImGui::GetTime() < s.flashUntil) {
        const float t = std::clamp(float(s.flashUntil - ImGui::GetTime()) / 0.8f, 0.0f, 1.0f);
        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin() - ImVec2(px(2), px(2)), ImGui::GetItemRectMax() + ImVec2(px(2), px(2)), u32(pal::Accent, 0.8f * t),
                                            px(kRowRounding + 2), 0, px(1.5f));
        requestRedraw();
    }
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

 
 
 

void drawTracked(Ui& ui) {
    State& s = st();
    ensureCatalog();
    std::vector<Receipt> rows;
    std::string progress;
    {
        std::lock_guard<std::mutex> lock(s.m);
        rows = s.receipts;
        progress = s.batchProgress;
    }
    size_t active = 0, mine = 0;
    for (const Receipt& r : rows) {
        active += r.life == Life::Active;
        mine += r.life == Life::Active && r.self;
    }
     
    {
        std::vector<std::string> filters = {"All  " + std::to_string(rows.size()), "Attached  " + std::to_string(active), "On You  " + std::to_string(mine)};
        pillTabs("##trackfilter", filters, &s.trackedFilter);
        const bool onYou = s.trackedFilter == 2;
        const char* removeLabel = onYou ? "Remove All on You" : "Remove All";
        const float w = buttonWidth("Verify All", icon::Refresh) + buttonWidth(removeLabel, icon::Delete) + buttonWidth("Clear Finished", icon::Close) + px(16);
        ImGui::SameLine();
        alignRight(w);
        {
            auto offline = requireConnection(ui);
            {
                DisabledScope none(rows.empty(), "Nothing tracked yet");
                if (button("Verify All", {.icon = icon::Refresh, .tooltip = "Re-check every tracked attachment against the game", .busy = ui.busy(kVerifyLabel)})) {
                    noteQueued(kVerifyLabel);
                    ui.post(kVerifyLabel, verifyJob);
                }
            }
            ImGui::SameLine(0, px(8));
            const size_t count = onYou ? mine : active;
            DisabledScope none(!count || s.batching.load(), count ? "A removal is running" : "Nothing attached");
            if (button(removeLabel, {.tone = Tone::Danger, .icon = icon::Delete, .tooltip = "Remove them one per frame (Stop works)"})) {
                const auto ids = activeIds(onYou);
                confirm(onYou ? "Remove Your Hop-Ons?" : "Remove All Hop-Ons?",
                        "Removes the " + std::to_string(ids.size()) + " attachments Blackrock is tracking" + (onYou ? " on you" : "") +
                            ", one at a time. Attachments Blackrock did not make stay.",
                        "Remove", [ids, onYou] { postRemoveMany(ids, onYou ? "on you" : "all"); });
            }
        }
        ImGui::SameLine(0, px(8));
        {
            DisabledScope none(rows.size() == active, "Only attached ones are listed");
            if (button("Clear Finished", {.tone = Tone::Ghost, .icon = icon::Close, .tooltip = "Forget removed / gone entries"})) {
                std::lock_guard<std::mutex> lock(s.m);
                s.receipts.erase(std::remove_if(s.receipts.begin(), s.receipts.end(), [](const Receipt& r) { return r.life == Life::Gone || r.life == Life::Removed; }),
                                 s.receipts.end());
            }
        }
    }
    if (s.batching.load()) {
        gap(sp::S);
        progressBar(-1, 0, 4);
        gap(sp::XS);
        caption(progress.empty() ? "Removing..." : progress.c_str(), pal::Muted);
        ImGui::SameLine();
        alignRight(buttonWidth("Stop", icon::Stop, true));
        if (button("Stop##batch", {.tone = Tone::Danger, .icon = icon::Stop, .small = true})) s.batchStop = true;
    }
    gap(sp::M);
    std::vector<const Receipt*> view;
    for (const Receipt& r : rows)
        if (s.trackedFilter == 0 || (s.trackedFilter == 1 && r.life == Life::Active) || (s.trackedFilter == 2 && r.self && r.life == Life::Active)) view.push_back(&r);
    if (view.empty()) {
        emptyState(icon::HopOns, rows.empty() ? "Nothing Tracked Yet" : "Nothing in This View",
                   rows.empty() ? "Apply a Hop-On from the Hop-Ons Tab"
                                : "Switch the Filter Above to See the Others",
                   180);
        return;
    }
    if (beginTable("##tracked", 6, std::min(560.0f, 42.0f + kTrackRowH * float(view.size())), ImGuiTableFlags_BordersOuter)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Hop-On", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthFixed, px(150));
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, px(150));
        ImGui::TableSetupColumn("Applied", ImGuiTableColumnFlags_WidthFixed, px(64));
        ImGui::TableSetupColumn("Accessor", ImGuiTableColumnFlags_WidthFixed, px(84));
        ImGui::TableSetupColumn("##act", ImGuiTableColumnFlags_WidthFixed, px(84));
        tableHeaders();
        for (const Receipt* r : view) {
            ImGui::TableNextRow(0, px(kTrackRowH));
            ImGui::PushID(int(r->id));
            ImGui::TableSetColumnIndex(0);
            cellText(displayName(r->tag).c_str(), pal::Text, FontKind::Body, type::Label, kTrackRowH);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s\n%s", hx(r->tag).c_str(), r->note.c_str());
            ImGui::TableSetColumnIndex(1);
            cellText(targetText(*r).c_str(), r->self ? pal::Accent : pal::Muted, FontKind::Body, type::Label, kTrackRowH);
            ImGui::TableSetColumnIndex(2);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(3));
            pill(hopons::lifeText(r->life), lifeColor(r->life));
            ImGui::TableSetColumnIndex(3);
            cellText(hoponsui::clockTime(r->appliedAt).c_str(), pal::Muted, FontKind::Body, type::Label, kTrackRowH);
            ImGui::TableSetColumnIndex(4);
            cellText(r->acc.accessor ? hx(r->acc.accessor).c_str() : "-", pal::Muted, FontKind::Mono, type::MonoSmall, kTrackRowH);
            ImGui::TableSetColumnIndex(5);
            {
                auto offline = requireConnection(ui);
                DisabledScope live(r->life != Life::Active, "Only attached hop-ons can be removed");
                if (button("Remove", {.tone = Tone::Ghost, .icon = icon::Remove, .small = true, .busy = ui.busy("Remove " + shortName(r->tag))})) postRemove(*r);
            }
            ImGui::PopID();
        }
        endTable();
    }
}

void drawSafety(Ui&) {
    Settings& settings = Engine::get().settings();
    bool on = settings.flag(kAutoRemoveKey, true);
    if (toggleRow("hopons.autoremove", "Remove My Hop-Ons on Panic and Disconnect", &on,
                  {.help = "Panic (Ctrl+Shift+P) and Disconnect remove every attachment Blackrock made this session, through the same guarded call as "
                           "Remove. Off: they stay in the game. Remove is checked on the game thread in the same frame: the target must be the same "
                           "object, and the accessor must still describe this attachment, or nothing is called."}))
        settings.set(std::string(kAutoRemoveKey), on);
}

 
 
 

void drawResidency(Ui& ui) {
    State& s = st();
    ensureCatalog();
    size_t loaded, pages;
    bool known;
    double at;
    std::string progress;
    float fraction;
    {
        std::lock_guard<std::mutex> lock(s.m);
        loaded = s.loadedTags.size();
        known = s.residencyKnown;
        at = s.residencyAt;
        pages = s.residencyPages;
        progress = s.residencyProgress;
        fraction = s.residencyFraction;
    }
    const size_t total = hopons::hopCatalog().size();
    const float w = (availWidth() - px(24)) / 3.0f;
    statTile("##loaded", "Loaded Now", known ? std::to_string(loaded) : "-", known ? "of " + std::to_string(total) + " catalogued" : "not scanned yet", w,
             known && loaded ? pal::Success : pal::Text);
    ImGui::SameLine(0, px(12));
    statTile("##pages", "Package Pages", known ? std::to_string(pages) : "-", "read in the last scan", w);
    ImGui::SameLine(0, px(12));
    statTile("##when", "Last Scan", known ? ago(hoponsui::nowSeconds() - at) : "never", known ? "rescan after a load" : "", w);
    gap(sp::M);
    const bool running = s.scanning.load() || ui.busy(kScanLabel);
    if (running) {
        progressBar(fraction, 0, 5);
        gap(sp::XS);
        caption(progress.empty() ? "Starting..." : progress.c_str(), pal::Muted);
        ImGui::SameLine();
        alignRight(buttonWidth("Stop", icon::Stop, true));
        if (button("Stop##scan", {.tone = Tone::Danger, .icon = icon::Stop, .small = true})) s.scanStop = true;
        requestRedraw();
    } else {
        auto offline = requireConnection(ui);
        if (button(kScanLabel, {.tone = Tone::Primary, .icon = icon::Search,
                                .tooltip = "Reads the item arrays of the package pages that hold catalogued hop-ons (one 128 KiB read per page)"}))
            postScan();
        ImGui::SameLine(0, px(8));
        if (iconButton("##scanmenu", icon::More, "Pin the scan to Home or give it a hotkey", false, kControlH)) openItemMenu(kScanId);
        if (known) {
            ImGui::SameLine(0, px(8));
            if (button("Show Loaded in the Catalog", {.tone = Tone::Ghost, .icon = icon::Filter})) {
                s.kind = 3;
                s.viewDirty = true;
                openPage(Page::HopOns, "Hop-ons");
                notify("Listing loaded hop-ons only", Level::Info);
            }
        }
    }
}

void drawCheckTag(Ui& ui) {
    State& s = st();
    ensureCatalog();
    fieldLabel("Tag");
    gap(sp::XS + 2);
    inputHex("##checktag", &s.checkTag, 120);
    ImGui::SameLine(0, px(8));
    {
        auto offline = requireConnection(ui);
        if (button("Check", {.tone = Tone::Primary, .icon = icon::Probe,
                             .tooltip = "Reads the tag's package item and definition (no call): resident, class, type and which targets fit",
                             .busy = ui.busy("Check hop-on tag")})) {
            const uint32_t tag = s.checkTag;
            noteQueued("Check hop-on tag");
            ui.post("Check hop-on tag", [tag](Ctx& c) { checkJob(c, tag); });
        }
    }
    ImGui::SameLine(0, px(8));
    if (s.selectedTag && button("Use Selected", {.tone = Tone::Ghost, .icon = icon::HopOns, .tooltip = "Check the hop-on selected in the catalogue"}))
        s.checkTag = s.selectedTag;
    CheckResult r;
    {
        std::lock_guard<std::mutex> lock(s.m);
        r = s.check;
    }
    if (!r.have) return;
    gap(sp::M);
    const bool ok = r.status == hopons::HopResult::Ok;
    pill(ok ? "Ready to Apply" : hopons::hopResultText(r.status), ok ? pal::Success : pal::Warning, ok ? icon::Check : icon::Warning);
    ImGui::SameLine(0, px(8));
    hashLabel(r.tag, "tag", pal::Accent);
    if (!r.why.empty()) {
        gap(sp::XS);
        wrapped(r.why.c_str(), pal::Muted);
    }
    gap(sp::S);
    char buf[200];
    snprintf(buf, sizeof buf, "page %X  index %X  item %08X  class %08X", r.page, r.index, r.item, r.itemClass);
    mono(buf, pal::Muted, type::MonoSmall);
    if (r.definition) {
        snprintf(buf, sizeof buf, "body %08X  definition %08X  type %u (%s)", r.body, r.definition, r.type, ok ? hopons::hopTypeName(r.type) : world::typeName(r.type));
        mono(buf, pal::Muted, type::MonoSmall);
    }
    if (ok) {
        const uint8_t need = hopons::requiredTargetType(r.type);
        gap(sp::XS);
        caption(need ? (std::string("Needs a ") + world::typeName(need) + " target").c_str() : "Any target type", pal::Muted);
        gap(sp::S);
        if (indexOf(r.tag) >= 0 && button("Select in Catalog", {.tone = Tone::Ghost, .icon = icon::List})) {
            select(r.tag);
            s.kind = 0;
            s.filter.clear();
            s.viewDirty = true;
            openPage(Page::HopOns, "Hop-ons");
        }
        ImGui::SameLine(0, px(8));
        auto offline = requireConnection(ui);
        if (button("Apply to You", {.icon = icon::Play, .tooltip = need == world::TYPE_WEAPON ? "Applies to your weapon" : nullptr}))
            postApply(r.tag, need == world::TYPE_WEAPON ? TargetWeapon : TargetYou, 0);
    }
}

void drawCatalogFacts(Ui&) {
    const auto& cat = hopons::hopCatalog();
    if (!cat.error.empty()) {
        caption(cat.error.c_str(), pal::Danger);
        return;
    }
    const float w = (availWidth() - px(36)) / 4.0f;
    statTile("##t20", "Hop-Ons", std::to_string(cat.countOfType(20)), "on anything", w);
    ImGui::SameLine(0, px(12));
    statTile("##t21", "Biped", std::to_string(cat.countOfType(21)), "on bipeds", w);
    ImGui::SameLine(0, px(12));
    statTile("##t22", "Weapon", std::to_string(cat.countOfType(22)), "on weapons", w);
    ImGui::SameLine(0, px(12));
    statTile("##t23", "Ship", std::to_string(cat.countOfType(23)), "ship attachments", w);
    gap(sp::M);
    char buf[200];
    snprintf(buf, sizeof buf, "%zu definitions, %zu with a recovered or localized name, %zu of your own labels.", cat.size(), cat.namedCount(),
             names::Labels::get().size());
    label(buf, pal::Muted);
    tooltip("From the Xbox tool's hop-on catalogue (data/hopons/hopons.tsv, rebuilt by tools/hopons_build.py). Tag ids are the Xbox ids; the owner "
            "reports PS3 ids are identical (TESTING > Port checks has the live comparison). Names are evidence, not proof: Check a tag to see what is "
            "really loaded.");
}

 
 
 

void paletteEntries(const std::string& query, std::vector<PaletteEntry>& out) {
    {
         
        const bool on = Engine::get().settings().flag(kAutoRemoveKey, true);
        PaletteEntry pe;
        pe.label = std::string("Remove My Hop-Ons on Panic and Disconnect: Turn ") + (on ? "Off" : "On");
        pe.where = "Hop-Ons  \xE2\x80\xBA  Tracked  \xE2\x80\xBA  Cleanup on Panic";
        pe.icon = icon::Shield;
        pe.badge = on ? "On" : "Off";
        pe.run = [] {
            Settings& settings = Engine::get().settings();
            const bool now = !settings.flag(kAutoRemoveKey, true);
            settings.set(std::string(kAutoRemoveKey), now);
            notify(now ? "Panic and disconnect remove your hop-ons" : "Panic and disconnect leave your hop-ons in the game", Level::Info);
        };
        out.push_back(std::move(pe));
    }
    if (!ensureCatalog()) return;
    State& s = st();
    auto add = [&](const HopEntry& e, bool fav) {
        PaletteEntry pe;
        pe.label = displayName(e.tag);
        pe.where = std::string("Hop-On  \xE2\x80\xBA  ") + hx(e.tag) + (fav ? "  \xC2\xB7  favourite" : "");
        pe.icon = fav ? icon::StarFill : icon::HopOns;
        pe.badge = hopons::hopTypeShort(e.type);
        pe.boost = fav ? 300 : 0;
        const uint32_t tag = e.tag;
        pe.run = [tag] {
            State& T = st();
            if (T.kind != 0 || !T.filter.empty() || T.type) {
                T.kind = 0;
                T.type = 0;
                T.filter.clear();
                T.viewDirty = true;
            }
            select(tag);
            T.revealCatalog = true;
            T.flashUntil = ImGui::GetTime() + 1.2;
            openPage(Page::HopOns, "Hop-ons");
        };
        pe.alt = [tag, type = e.type] { postApply(tag, type == hopons::TYPE_WEAPON_ATTACHMENT ? TargetWeapon : TargetYou, 0); };
        pe.altHint = "Apply";
        out.push_back(std::move(pe));
    };
    for (uint32_t t : s.favs)
        if (const HopEntry* e = s.cat->find(t)) add(*e, true);
    if (query.size() < 2) return;
    const auto words = hopons::searchWords(query);
    int n = 0;
    const auto& all = s.cat->all();
    for (size_t i = 0; i < all.size() && n < 30; ++i) {
        if (s.favs.count(all[i].tag)) continue;
        std::string hay = s.cat->hay(i);
        if (const auto own = names::Labels::get().find(all[i].tag)) hay += " " + hopons::lowerCopy(own->name);
        if (!hopons::matchesAll(hay, words)) continue;
        add(all[i], false);
        ++n;
    }
}

void resetOnDisconnect() {
    State& s = st();
    s.scanning = false;
    s.batching = false;
    {
        std::lock_guard<std::mutex> lock(s.m);
        s.batchProgress.clear();
        s.residencyProgress.clear();
        if (!Engine::get().connected()) {
             
            s.receipts.clear();
            s.targets.clear();
            s.playerHandle = 0;
            s.targetsAt = 0;
            s.loadedTags.clear();
            s.residencyKnown = false;
            s.check = {};
        }
    }
    s.viewDirty = true;
}

}   

BR_FEATURES(hopons_page) {
    r.section({.id = "hopons.catalog", .page = Page::HopOns, .tab = "Hop-ons", .section = kCatalogSection, .order = 10, .draw = drawCatalog,
               .onDisconnect = resetOnDisconnect});
    r.section({.id = "hopons.tracked", .page = Page::HopOns, .tab = "Tracked", .section = kTrackedSection, .order = 10, .draw = drawTracked});
    r.section({.id = "hopons.safety", .page = Page::HopOns, .tab = "Tracked", .section = "Cleanup on Panic", .order = 20, .draw = drawSafety});
    r.section({.id = "hopons.residency", .page = Page::HopOns, .tab = "Catalog", .section = "Loaded hop-ons", .order = 10, .draw = drawResidency});
    r.section({.id = "hopons.check", .page = Page::HopOns, .tab = "Catalog", .section = "Check a hop-on tag", .order = 20, .draw = drawCheckTag});
    r.section({.id = "hopons.facts", .page = Page::HopOns, .tab = "Catalog", .section = "Catalogue sources", .order = 30, .draw = drawCatalogFacts});
    r.action({.id = kApplyId, .page = Page::HopOns, .tab = "Hop-ons", .section = kCatalogSection, .label = kApplyLabel,
              .help = "Attach the hop-on selected on the Hop-ons page to the chosen target (you, your weapon or the picked object).", .run = applySelected});
    r.action({.id = kRemoveMineId, .page = Page::HopOns, .tab = "Tracked", .section = kTrackedSection, .label = kRemoveMineLabel,
              .help = "Remove every hop-on Blackrock attached to your character this session.", .run = removeMine});
    r.action({.id = kVerifyId, .page = Page::HopOns, .tab = "Tracked", .section = kTrackedSection, .label = kVerifyLabel,
              .help = "Re-check every tracked attachment against the game (attached, gone or removed).", .run = verifyJob});
    r.action({.id = kScanId, .page = Page::HopOns, .tab = "Catalog", .section = "Loaded hop-ons", .label = kScanLabel,
              .help = "Find which catalogued hop-ons are loaded in the current area (reads package item arrays).", .run = scanJob});
    ui::styleSection(kCatalogSection, {.fullWidth = true, .icon = ui::icon::HopOns, .ownsActions = true});
    ui::styleSection(kTrackedSection, {.fullWidth = true, .icon = ui::icon::List, .ownsActions = true});
    ui::styleSection("Cleanup on Panic", {.icon = ui::icon::Shield});
    ui::styleSection("Loaded hop-ons", {.fullWidth = true, .icon = ui::icon::Search, .ownsActions = true});
    ui::styleSection("Check a hop-on tag", {.icon = ui::icon::Probe});
    ui::styleSection("Catalogue sources", {.icon = ui::icon::Tag});
    ui::addPaletteProvider(paletteEntries);
}

}   
