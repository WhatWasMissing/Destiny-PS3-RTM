 
 
 
 
 
 
 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

#include "app/engine.h"
#include "app/registry.h"
#include "app/settings.h"
#include "core/error.h"
#include "core/runtime.h"
#include "features/inventory_shared.h"
#include "game/inventory_addr.h"
#include "game/inventory_verify.h"
#include "game/names_catalog.h"
#include "game/world_mem.h"
#include "ui/widgets.h"

namespace brps3::invui {

using namespace inventory;
using namespace ui;

namespace {

constexpr const char* kReadJob = "Read inventory";
constexpr const char* kConfirmLoop = "Inventory confirm";
constexpr int kConfirmPollMs = 300;    
constexpr const char* kSection = "Character & vault";
constexpr float kTile = 56, kTileGap = 6;

Settings& settings() { return Engine::get().settings(); }

std::string hx(uint32_t v) { return world::hex8(v); }

 
struct View {
    std::string query;
    int group = 0;                       
    std::set<int> tiers;                 
    int bucket = -1;                     
    uint64_t filterKey = 0, builtFilterKey = ~0ull;
    int view = 0;                        
    bool loaded = false;
    uint32_t quantity = 1;
    Selection quantityFor;
    bool scrollToSel = false;
    bool showTech = false;
    bool autoRead = false;               
    double lastAuto = 0;
    ImGuiTableColumnSortSpecs sort{};
    bool sorted = false;
     
    uint64_t builtFor = ~0ull;
    std::string builtQuery;
    int builtGroup = -1;
    size_t builtFavs = 0;
    std::vector<size_t> visible;         
    std::map<size_t, RollView> rolls;    
    std::shared_ptr<const Snapshot> builtSnap;
    std::set<uint32_t> favs;             
};

View& view() {
    static View v;
    return v;
}

 

ImVec4 rarityColor(int r) {
    switch (r) {
    case 1: return rgb(0xC9C4BC);
    case 2: return rgb(0x5BAF6A);
    case 3: return rgb(0x5B8FD6);
    case 4: return rgb(0xA071D6);
    case 5: return rgb(0xE6C445);
    default: return pal::Muted;
    }
}

std::string monogram(const std::string& name) {
    static const std::set<std::string> small = {"of", "the", "to", "a", "an", "and"};
    std::string out, word;
    auto take = [&](const std::string& w) {
        if (w.empty() || out.size() >= 2) return;
        std::string lw = lowerAscii(w);
        if (small.count(lw) && !out.empty()) return;
        for (char c : w)
            if (std::isalnum(static_cast<unsigned char>(c))) {
                out += char(std::toupper(static_cast<unsigned char>(c)));
                return;
            }
    };
    for (char c : name) {
        if (c == ' ' || c == '-') {
            take(word);
            word.clear();
        } else {
            word += c;
        }
    }
    take(word);
    return out.empty() ? "?" : out;
}

const std::vector<int>& characterOrder() {
    static const std::vector<int> o = {2, 3, 4, 7, 8, 9, 10, 11, 14, 35, 6, 12, 13, 25, 27, 28, 29, 1, 5, 15, 18, 19, 26, 36, 16, 17, 20, 30, 31, 32, 33, 34};
    return o;
}
const std::vector<int>& vaultOrder() {
    static const std::vector<int> o = {24, 22, 23, 21, 30, 33, 34};
    return o;
}

bool groupMatches(const Item& it, const Catalog& cat, int group, const std::set<uint32_t>& favs, const std::set<uint32_t>& recent) {
    const ItemDef* d = cat.item(it.def);
    const int g = std::max(bucketGroup(it.bucket), d ? bucketGroup(d->home) : 0);
    switch (group) {
    case 1: return g == 1;
    case 2: return g == 2;
    case 3: return g == 0;
    case 4: return favs.count(it.hash) != 0;
    case 5: return recent.count(it.hash) != 0;
    default: return true;
    }
}

std::set<uint32_t> hashSet(const char* key) {
    std::set<uint32_t> out;
    for (const std::string& h : settings().list(key)) {
        try {
            out.insert(uint32_t(std::stoul(h, nullptr, 16)));
        } catch (...) {
        }
    }
    return out;
}

void setLast(bool ok, const std::string& text) {
    Shared& S = shared();
    std::lock_guard<std::mutex> lock(S.m);
    S.last = {ok, text, detail::activityClock()};
    S.progress.clear();
}

void setProgress(const std::string& text) {
    Shared& S = shared();
    std::lock_guard<std::mutex> lock(S.m);
    S.progress = text;
}

}   

 

Shared& shared() {
    static Shared s;
    return s;
}

std::string rarityText(int r) {
    if (r >= 1 && r <= 5) return names::rarityName(uint8_t(r));
    return "";
}

std::string shortHex(uint64_t soid) { return hex16(soid).substr(8); }

void publish(std::shared_ptr<const Snapshot> s, const std::string& error, bool notSignedIn) {
    Shared& S = shared();
    std::lock_guard<std::mutex> lock(S.m);
    if (s) {
        S.snap = std::move(s);
        ++S.generation;
        S.readAt = detail::activityClock();
        S.error.clear();
        S.notSignedIn = false;
    } else {
        S.error = error;
        S.notSignedIn = notSignedIn;
        if (notSignedIn) {
            S.snap.reset();
            ++S.generation;
        }
    }
}

std::shared_ptr<const Snapshot> current() {
    Shared& S = shared();
    std::lock_guard<std::mutex> lock(S.m);
    return S.snap;
}

Selection selection() {
    Shared& S = shared();
    std::lock_guard<std::mutex> lock(S.m);
    return S.sel;
}

void select(const Selection& s) {
    Shared& S = shared();
    std::lock_guard<std::mutex> lock(S.m);
    S.sel = s;
}

Selection selectionOf(const Item& it) {
    Selection s;
    s.valid = true;
    s.owner = it.owner;
    s.slot = it.slot;
    s.def = it.def;
    s.soid = it.soid;
    s.hash = it.hash;
    return s;
}

const Item* findSelected(const Snapshot& s, const Selection& sel) {
    if (!sel.valid) return nullptr;
    if (sel.soid) return s.find(sel.soid);
    const Item* it = s.at(sel.owner, sel.slot);
    return it && it->def == sel.def ? it : nullptr;
}

bool favourite(uint32_t hash) { return settings().inList(kFavKey, hx(hash)); }
void toggleFavourite(uint32_t hash) { settings().toggleInList(kFavKey, hx(hash)); }
void noteRecent(uint32_t hash) { settings().pushRecent(kRecentKey, hx(hash), 30); }
std::vector<uint32_t> recents() {
    std::vector<uint32_t> out;
    for (const std::string& h : settings().list(kRecentKey)) {
        try {
            out.push_back(uint32_t(std::stoul(h, nullptr, 16)));
        } catch (...) {
        }
    }
    return out;
}

void revealItem(const Selection& s) {
    select(s);
    View& v = view();
    v.scrollToSel = true;
    if (v.group != 0 || !v.query.empty() || !v.tiers.empty() || v.bucket >= 0) {
        v.group = 0;
        v.query.clear();
        v.tiers.clear();
        v.bucket = -1;
    }
    openVaultTab("Inventory");
}

bool actionBusy() { return shared().acting.load(); }

std::shared_ptr<const Snapshot> readAndPublish(Ctx& c, bool talents) {
    Io& io = c.state<Io>("inventory.io");
    c.require(kReadFingerprints, "Inventory reader");
    std::shared_ptr<const Snapshot> s;
    try {
        std::lock_guard<std::mutex> lock(io.m);
        world::RuntimeReader mem(c.rt());
        ReadOptions o;
        o.talents = talents;
        s = std::make_shared<const Snapshot>(readSnapshotRetry(mem, io.cache, o, 3, [&c] { return c.cancelled(); }));
    } catch (const SnapshotError& e) {
        publish(nullptr, e.what(), e.kind == FailKind::NotSignedIn);
        throw Error(std::string("Inventory: ") + e.what());
    }
    publish(s);
    return s;
}

void postRefresh(Ui& ui, bool quiet) {
    if (ui.busy(kReadJob)) return;
    if (!quiet) noteQueued(kReadJob);
    ui.post(
        kReadJob,
        [](Ctx& c) {
            const auto s = readAndPublish(c, true);
            char buf[160];
            std::snprintf(buf, sizeof buf, "Inventory: %zu items, family version %u, %llu reads in %.0f ms", s->items.size(), s->familyVersion,
                          static_cast<unsigned long long>(s->reads), s->ms);
            c.log(buf);
        },
        [](bool ok, const std::string&) {
            if (ok) flashSuccess("inventory.refresh");
        });
}

 

namespace {

 
 
 
 
struct CatalogProof {
    std::mutex m;
    bool ok = false;
    uint32_t iface = 0, mode = 0;
};

void requireVerifiedCatalog(Ctx& c) {
    CatalogProof& p = c.state<CatalogProof>("inventory.catalog_proof");
    c.require(kCatalogFingerprints, "Native catalogue");
    world::RuntimeReader mem(c.rt());
    const InvestmentInterface i = readInvestmentInterface(mem);
    {
        std::lock_guard<std::mutex> lock(p.m);
        if (p.ok && p.iface == i.iface && p.mode == i.mode) return;
    }
    setProgress("Verifying the native catalogue (once per session)");
    const VerifyReport rep = verifyCatalog(mem, catalog());
    if (!rep.ok) {
        for (const VerifyLine& l : rep.lines)
            if (l.level == VerifyLine::Fail) c.log("[inventory] catalogue check FAILED: " + l.what + (l.detail.empty() ? "" : ": " + l.detail));
        throw Error("transfers refused: " + rep.verdict + " (the failed checks are in the log; TESTING > Inventory > Verify shows the full report)");
    }
    c.log("[inventory] " + rep.verdict + " (mode " + std::to_string(rep.mode) + ", transfer pairs " +
          (rep.contextSource.empty() ? std::string("bundled") : rep.contextSource) + ")");
    std::lock_guard<std::mutex> lock(p.m);
    p.ok = true;
    p.iface = i.iface;
    p.mode = i.mode;
}

void runRequest(Ctx& c, const ActionRequest& r) {
    Shared& S = shared();
    if (S.acting.exchange(true)) throw Error("another inventory request is still being confirmed; wait for it (or stop watching it)");
    bool handedOver = false;
    struct Release {
        bool& handed;
        ~Release() {
            if (!handed) shared().acting = false;
        }
    } release{handedOver};
    {
        std::lock_guard<std::mutex> lock(S.m);
        S.progress = "Preparing";
        S.lastConditions.clear();
    }
    try {
        c.require(kReadFingerprints, "Inventory reader");
        c.require(fingerprintsFor(r.action), actionName(r.action));
        if (r.action == Action::Deposit || r.action == Action::Withdraw) requireVerifiedCatalog(c);
    } catch (const std::exception& e) {
        setLast(false, std::string("Refused: ") + e.what());
        throw;
    }
    Io& io = c.state<Io>("inventory.io");
    ActionResult res;
    DefinitionCache copy;
    ActionHooks hooks;
    hooks.progress = setProgress;
    hooks.cancelled = [&c] { return c.cancelled(); };
    try {
        std::lock_guard<std::mutex> lock(io.m);
        world::RuntimeReader mem(c.rt());
        res = sendAction(c.rt(), mem, io.cache, r, catalog(), hooks);
        copy = io.cache;
    } catch (const std::exception& e) {
        setLast(false, std::string(actionVerb(r.action)) + ": " + e.what() + " (nothing was sent)");
        throw;
    }
    {
        std::lock_guard<std::mutex> lock(S.m);
        S.lastConditions = res.conditions;
    }
    noteRecent(res.plan.hash);
    char gate[96];
    std::snprintf(gate, sizeof gate, " (gate: provider %08X, state %d)", res.sent.provider, int32_t(res.sent.state));
    c.log("[inventory] " + res.plan.summary() + ": op " + std::to_string(opFor(r.action)) + ", " + std::to_string(res.conditions.size()) +
          " same-frame checks" + gate);
    if (!needsConfirmation(res)) {
        setLast(false, res.message);
        c.toast(res.message);
        return;
    }
    S.stopWatch = false;
    {
        std::lock_guard<std::mutex> lock(S.m);
        S.actionLabel = res.plan.summary();
    }
    trackActivity("inventory.confirm", "Confirming: " + res.plan.summary(), Page::Vault, [] { return shared().acting.load(); },
                  [] { shared().stopWatch = true; }, pal::Accent);
    handedOver = true;
     
     
    struct Watch {
        ActionResult res;
        DefinitionCache cache;
        ConfirmWatch w;
    };
    auto watch = std::make_shared<Watch>();
    watch->res = std::move(res);
    watch->cache = std::move(copy);
    c.startLoop(kConfirmLoop, kConfirmPollMs, [watch](Ctx& lc) {
        world::RuntimeReader mem(lc.rt());
        ActionHooks h;
        h.progress = setProgress;
        h.cancelled = [&lc] { return lc.cancelled() || shared().stopWatch.load(); };
        ActionResult& out = watch->res;
        bool done = false;
        try {
            done = confirmStep(mem, watch->cache, out, watch->w, catalog(), h);
        } catch (const std::exception& e) {
            out.confirmed = false;
            out.message = out.plan.summary() + ": confirmation failed (" + e.what() + "); not retried";
            done = true;
        }
        if (!done) return;
        const bool stopping = lc.cancelled();    
        setLast(out.confirmed, out.message);
        lc.log("[inventory] " + out.message);
        lc.toast(out.message);
        std::shared_ptr<const Snapshot> after = out.haveAfter ? std::make_shared<const Snapshot>(out.after) : nullptr;
        shared().acting = false;
        lc.stopLoop(kConfirmLoop);    
        if (stopping) return;
         
        Engine::get().post(kReadJob, [after](Ctx& jc) {
            try {
                readAndPublish(jc, true);
            } catch (const std::exception& e) {
                if (after) publish(after);
                jc.log(std::string("[inventory] refresh after the request failed: ") + e.what());
            }
        });
    });
}

ActionRequest requestFor(const Snapshot& s, const Item& it, Action a, uint32_t quantity) {
    ActionRequest r;
    r.action = a;
    r.owner = it.owner;
    r.slot = it.slot;
    r.def = it.def;
    r.soid = it.soid;
    r.quantity = quantity;
    r.expectedCharacter = s.characterSoid;
    return r;
}

std::string jobLabel(const Item& it, Action a, uint32_t quantity) {
    const std::string name = itemName(it, catalog());
    switch (a) {
    case Action::Deposit: return "Transfer " + name + (it.soid ? "" : " x" + std::to_string(quantity)) + " to the vault";
    case Action::Withdraw: return "Transfer " + name + (it.soid ? "" : " x" + std::to_string(quantity)) + " to your character";
    default: return std::string(actionName(a)) + " " + name;
    }
}

}   

void startAction(Action a, uint32_t quantity) {
    if (!Engine::get().connected()) {
        notify(std::string(kConnectFirst) + " to change your inventory", Level::Warn);
        return;
    }
    if (actionBusy()) {
        notify("One inventory request at a time: the last one is still being confirmed", Level::Warn);
        return;
    }
    const auto s = current();
    const Selection sel = selection();
    const Item* it = s ? findSelected(*s, sel) : nullptr;
    if (!it) {
        notify("Select an item on the Vault page first", Level::Warn);
        return;
    }
    const Catalog& cat = catalog();
    const Eligibility e = eligibility(*s, *it, a, cat);
    if (!e.ok) {
        notify(std::string(actionVerb(a)) + ": " + e.reason, Level::Warn);
        return;
    }
    quantity = std::clamp<uint32_t>(quantity, 1, std::max<uint32_t>(1, e.maxQuantity));
    const ActionRequest r = requestFor(*s, *it, a, quantity);
    const std::string label = jobLabel(*it, a, quantity);
    auto go = [r, label] {
        noteQueued(label);
        Engine::get().post(label, [r](Ctx& c) { runRequest(c, r); });
    };
    if (a == Action::Dismantle || a == Action::Use) {
        const std::string name = itemName(*it, cat);
        const int rarity = rarityOf(*it, cat);
        std::string msg;
        if (a == Action::Dismantle) {
            msg = name + (rarity >= 4 ? " is " + rarityText(rarity) + ". " : ". ") +
                  "Dismantling destroys it for good; Blackrock sends one request after re-checking that it is still this unequipped item.";
        } else {
            msg = "Uses one " + name + " (" + std::to_string(it->quantity) + " in the stack). The game consumes it; this can't be undone.";
            if (it->def == kThreeOfCoins && (s->exoticFlag & 2))
                msg += " Note: the Exotic Particles flag (+0x549A, unverified offset) already reads set.";
        }
        confirm(std::string(actionName(a)) + " " + name + "?", msg, actionName(a), go, true);
    } else {
        go();
    }
}

void runSelectedAction(Ctx& c, Action a) {
    const auto s = current();
    const Selection sel = selection();
    const Item* it = s ? findSelected(*s, sel) : nullptr;
    if (!it) throw Error("select an item on the Vault page first");
    const Eligibility e = eligibility(*s, *it, a, catalog());
    if (!e.ok) throw Error(std::string(actionVerb(a)) + ": " + e.reason);
    runRequest(c, requestFor(*s, *it, a, 1));
}

 

bool demoMode() {
    static const bool on = [] {
        char* v = nullptr;
        size_t n = 0;
        const bool d = _dupenv_s(&v, &n, "BRPS3_UI_DEMO") == 0 && v && v[0] == '1';
        free(v);
        return d;
    }();
    return on;
}

void seedDemo() {
    static bool done = false;
    if (done) return;
    done = true;
    const Catalog& cat = catalog();
    if (!cat.error.empty()) return;
    auto s = std::make_shared<Snapshot>();
    s->manager = 0x600C4140;
    s->accountSoid = 0x4000000007B6510Full;
    s->characterSoid = 0x2305843009213694ull;
    s->familyVersion = 2491;
    s->mode = 3;
    s->descriptorIndex = 253;
    s->defCount = uint32_t(cat.size());
    s->reads = 21;
    s->ms = 1184;
    s->talents = true;
    s->character.assign(off::CHARACTER_ROWS, Row{});
    s->account.assign(off::ACCOUNT_ROWS, Row{});
    uint64_t nextSoid = 0x60000019DA700000ull;
    auto add = [&](Owner o, int slot, uint16_t def, uint32_t qty, bool instance) {
        const ItemDef* d = cat.item(def);
        if (!d) return;
        Row& row = (o == Owner::Account ? s->account : s->character)[size_t(slot)];
        row.def = def;
        row.soid = instance ? (nextSoid += 0x1D3F) : 0;
        row.quantity = qty;
        Item it;
        it.owner = o;
        it.slot = uint16_t(slot);
        it.def = def;
        it.hash = d->hash;
        it.soid = row.soid;
        it.quantity = qty;
        if (const Bucket* b = cat.bucketAt(o == Owner::Account, slot)) it.bucket = b->key;
        if (instance && d->grid != 0xFFFF) {
            for (int k = 0; k < 8; ++k) it.detail[size_t(k)] = uint8_t(it.soid >> (56 - 8 * k));
            it.detail[off::D_DEF] = uint8_t(def >> 8);
            it.detail[off::D_DEF + 1] = uint8_t(def);
            it.detail[off::D_GRID] = uint8_t(d->grid >> 8);
            it.detail[off::D_GRID + 1] = uint8_t(d->grid);
            for (size_t k = 0; k < 36; ++k) it.detail[off::D_NODES + k] = uint8_t((k * 7 + def) % 3 == 0 ? 0 : 1);
            for (size_t k = 0; k < 8; ++k) it.detail[off::D_ROLLS + k] = uint8_t((def * 31 + k * 17) & 0xFF);
            it.hasDetail = true;
        }
        s->items.push_back(it);
    };
    auto byName = [&](const char* name) -> int {
        for (const ItemDef& d : cat.items())
            if (d.name == name && d.perInstance) return d.def;
        return -1;
    };
     
    int slot = 93;
    for (const char* n : {"Lord High Fixer", "Hawkmoon", "Thorn", "The Last Word", "Fatebringer"})
        if (int d = byName(n); d >= 0) add(Owner::Character, slot++, uint16_t(d), 1, true);
    slot = 103;
    for (const char* n : {"Two To The Morgue", "Black Hammer", "Invective", "Plan C"})
        if (int d = byName(n); d >= 0) add(Owner::Character, slot++, uint16_t(d), 1, true);
    slot = 113;
    for (const char* n : {"Gjallarhorn", "Thunderlord", "Raze-Lighter"})
        if (int d = byName(n); d >= 0) add(Owner::Character, slot++, uint16_t(d), 1, true);
    std::map<int, int> nextSlot = {{7, 43}, {8, 53}, {9, 63}, {10, 73}, {11, 83}, {14, 153}, {24, 200}, {22, 20}, {23, 128}};
    std::map<int, int> want = {{7, 4}, {8, 3}, {9, 4}, {10, 3}, {11, 3}, {14, 2}};
    for (const ItemDef& d : cat.items()) {
        auto w = want.find(d.home);
        if (w == want.end() || w->second <= 0 || !d.perInstance || d.rarity < 4 || d.name.empty()) continue;
        add(Owner::Character, nextSlot[d.home]++, d.def, 1, true);
        --w->second;
    }
    add(Owner::Character, 20, kThreeOfCoins, 150, false);
    slot = 21;
    for (const ItemDef& d : cat.items())
        if (d.home == 1 && !d.perInstance && d.rarity >= 2 && !d.name.empty() && d.def != kThreeOfCoins && slot < 27) add(Owner::Character, slot++, d.def, 5 + d.def % 40, false);
    slot = 0;
    for (const ItemDef& d : cat.items())
        if (d.home == 5 && !d.perInstance && !d.name.empty() && slot < 6) add(Owner::Character, slot++, d.def, 20 + d.def % 180, false);
     
    std::set<int> equippedBuckets;
    for (Item& it : s->items) {
        if (it.owner != Owner::Character || !it.soid || equippedBuckets.count(it.bucket)) continue;
        if (!isWeaponBucket(it.bucket) && !isArmorBucket(it.bucket) && it.bucket != 14) continue;
        equippedBuckets.insert(it.bucket);
        it.equipped = true;
        const int eslot = it.bucket >= 2 && it.bucket <= 4 ? it.bucket + 4 : it.bucket >= 7 && it.bucket <= 11 ? it.bucket - 6 : 11;
        s->equipped[size_t(eslot)] = it.soid;
    }
     
    int weapons = 46, armor = 31, general = 0;
    for (const ItemDef& d : cat.items()) {
        if (d.name.empty() || !d.perInstance || d.rarity < 4) continue;
        if (isWeaponBucket(d.home) && weapons > 0 && (d.def % 5 == 0)) {
            add(Owner::Account, nextSlot[24]++, d.def, 1, true);
            --weapons;
        } else if (isArmorBucket(d.home) && armor > 0 && (d.def % 9 == 0)) {
            add(Owner::Account, nextSlot[22]++, d.def, 1, true);
            --armor;
        }
    }
    for (const ItemDef& d : cat.items())
        if ((d.home == 12 || d.home == 13 || d.home == 25 || d.home == 27) && d.perInstance && d.rarity >= 3 && !d.name.empty() && general < 14) {
            add(Owner::Account, nextSlot[23]++, d.def, 1, true);
            ++general;
        }
    s->postmasterCount = 1;
    s->postmaster.push_back({kThreeOfCoins, 1, 0});
    s->detailCount = 646;
    for (const Item& it : s->items) s->detailMatched += it.hasDetail;
    Shared& S = shared();
    {
        std::lock_guard<std::mutex> lock(S.m);
        S.snap = s;
        ++S.generation;
        S.readAt = detail::activityClock() - 14;
        S.demo = true;
        for (const Item& it : s->items)
            if (it.owner == Owner::Character && itemName(it, cat) == "Lord High Fixer") S.sel = selectionOf(it);
        S.last = {true, "Transfer Gjallarhorn to the vault: confirmed (now in Vault weapons)", detail::activityClock() - 40};
    }
    settings().set(std::string(kFavKey), std::string());
    for (const Item& it : s->items)
        if (itemName(it, cat) == "Gjallarhorn" || itemName(it, cat) == "Lord High Fixer") settings().toggleInList(kFavKey, hx(it.hash));
}

 

namespace {

void rebuild(View& v, const std::shared_ptr<const Snapshot>& s, uint64_t generation) {
    const std::set<uint32_t> favs = hashSet(kFavKey), recent = hashSet(kRecentKey);
    v.favs = favs;
    v.filterKey = uint64_t(uint32_t(v.bucket + 1)) << 8;
    for (int t : v.tiers) v.filterKey |= 1ull << t;
    if (v.builtFor == generation && v.builtQuery == v.query && v.builtGroup == v.group && v.builtFavs == favs.size() + recent.size() * 1000 &&
        v.builtFilterKey == v.filterKey)
        return;
    v.builtFilterKey = v.filterKey;
    v.builtFor = generation;
    v.builtQuery = v.query;
    v.builtGroup = v.group;
    v.builtFavs = favs.size() + recent.size() * 1000;
    v.visible.clear();
    const Catalog& cat = catalog();
    if (v.builtSnap != s) {
        v.builtSnap = s;
        v.rolls.clear();
        if (s)
            for (size_t i = 0; i < s->items.size(); ++i)
                if (s->items[i].hasDetail) v.rolls[i] = describeRoll(s->items[i], cat);
    }
    if (!s) return;
    const Query q(v.query);
    for (size_t i = 0; i < s->items.size(); ++i) {
        const Item& it = s->items[i];
        if (!groupMatches(it, cat, v.group, favs, recent)) continue;
        if (!v.tiers.empty() && !v.tiers.count(rarityOf(it, cat))) continue;
        if (v.bucket >= 0 && it.bucket != v.bucket) continue;
        auto r = v.rolls.find(i);
        if (!q.matches(it, cat, r != v.rolls.end() ? &r->second : nullptr)) continue;
        v.visible.push_back(i);
    }
}

const RollView* rollOf(View& v, size_t index) {
    auto it = v.rolls.find(index);
    return it == v.rolls.end() ? nullptr : &it->second;
}

void itemTooltip(const Item& it, const RollView* roll) {
    const Catalog& cat = catalog();
    const int r = rarityOf(it, cat);
    ImGui::BeginTooltip();
    {
        FontScope f(FontKind::Semibold, type::Body);
        ImGui::TextUnformatted(itemName(it, cat).c_str());
    }
    const ItemDef* d = cat.item(it.def);
    std::string line = rarityText(r);
    if (d && !d->type.empty()) line += (line.empty() ? "" : "  \xC2\xB7  ") + d->type;
    caption(line.c_str(), rarityColor(r));
    std::string where = locationName(it) + "  \xC2\xB7  " + bucketName(it.bucket);
    if (it.quantity > 1 || !it.soid) where += "  \xC2\xB7  x" + std::to_string(it.quantity);
    if (it.equipped) where += "  \xC2\xB7  equipped";
    caption(where.c_str(), pal::Muted);
    if (roll && roll->readable) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + px(320));
        caption(roll->summary.c_str(), pal::Text);
        ImGui::PopTextWrapPos();
    }
    ImGui::EndTooltip();
}

 
bool tile(const Item& it, size_t index, bool selected, View& v) {
    const Catalog& cat = catalog();
    const float size = px(kTile);
    ImGui::PushID(int(index));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##tile", ImVec2(size, size));
    const bool hov = ImGui::IsItemHovered();
    if (selected && v.scrollToSel) {
        ImGui::SetScrollHereY(0.35f);
        v.scrollToSel = false;
    }
    ImDrawList* d = ImGui::GetWindowDrawList();
    const int r = rarityOf(it, cat);
    const ImVec4 rc = rarityColor(r);
    const float round = px(7);
    d->AddRectFilled(p, p + ImVec2(size, size), u32(mix(pal::Raised, rc, hov ? 0.20f : 0.12f)), round);
    d->AddRectFilledMultiColor(p + ImVec2(1, 1), p + ImVec2(size - 1, size * 0.5f), u32(alpha(pal::Text, 0.035f)), u32(alpha(pal::Text, 0.035f)),
                               u32(alpha(pal::Text, 0.0f)), u32(alpha(pal::Text, 0.0f)));
    d->AddRectFilled(ImVec2(p.x, p.y + size - px(4)), p + ImVec2(size, size), u32(rc), round, ImDrawFlags_RoundCornersBottom);
    d->AddRect(p, p + ImVec2(size, size), u32(selected ? pal::Accent : alpha(rc, hov ? 0.75f : 0.38f)), round, 0, selected ? px(2) : 1.0f);
    const std::string m = monogram(itemName(it, cat));
    const ImVec2 ts = textSize(FontKind::Semibold, 15, m.c_str());
    drawText(d, FontKind::Semibold, 15, ImVec2(p.x + (size - ts.x) * 0.5f, p.y + (size - px(4) - ts.y) * 0.5f), u32(mix(rc, pal::Text, 0.55f)), m.c_str());
    if (it.equipped) {
        const ImVec2 a = p + ImVec2(px(4), px(4));
        d->AddRectFilled(a, a + ImVec2(px(15), px(15)), u32(pal::Accent), px(4));
        const ImVec2 es = textSize(FontKind::Semibold, 10.5f, "E");
        drawText(d, FontKind::Semibold, 10.5f, a + (ImVec2(px(15), px(15)) - es) * 0.5f, u32(pal::OnAccent), "E");
    }
    if (v.favs.count(it.hash)) {
        const ImVec2 is = textSize(FontKind::Icons, 10, icon::StarFill);
        drawText(d, FontKind::Icons, 10, ImVec2(p.x + size - is.x - px(5), p.y + px(5)), u32(pal::Accent), icon::StarFill);
    }
    if (!it.soid || it.quantity > 1) {
        char q[16];
        std::snprintf(q, sizeof q, "%u", it.quantity);
        const ImVec2 qs = textSize(FontKind::Semibold, 11, q);
        const ImVec2 a(p.x + size - qs.x - px(9), p.y + size - qs.y - px(8));
        d->AddRectFilled(a - ImVec2(px(3), px(1)), a + qs + ImVec2(px(3), px(1)), u32(alpha(pal::Window, 0.85f)), px(3));
        drawText(d, FontKind::Semibold, 11, a, u32(pal::Text), q);
    }
    if (hov) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) itemTooltip(it, rollOf(v, index));
        requestRedraw();
    }
    ImGui::PopID();
    return clicked;
}

void bucketHeader(const std::string& name, int used, int capacity) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = availWidth();
    ImDrawList* d = ImGui::GetWindowDrawList();
    const std::string up = titleCase(name);
    drawText(d, FontKind::Body, type::Small, p, u32(pal::Muted), up.c_str());
    char cnt[32];
    if (capacity > 0) std::snprintf(cnt, sizeof cnt, "%d / %d", used, capacity);
    else std::snprintf(cnt, sizeof cnt, "%d", used);
    const ImVec2 cs = textSize(FontKind::Body, type::Tiny, cnt);
    const bool full = capacity > 0 && used >= capacity;
    drawText(d, FontKind::Body, type::Tiny, ImVec2(p.x + w - cs.x - px(2), p.y), u32(full ? pal::Warning : pal::Faint), cnt);
    const float lh = textSize(FontKind::Semibold, type::Micro, "A").y;
    d->AddLine(ImVec2(p.x, p.y + lh + px(4)), ImVec2(p.x + w, p.y + lh + px(4)), u32(pal::Border), 1.0f);
    ImGui::Dummy(ImVec2(w, lh + px(10)));
}

 
void gridColumn(const char* id, const char* title, const char* glyph, bool account, const Snapshot& s, View& v, const Selection& sel, float width,
                float height) {
    const Catalog& cat = catalog();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0.25f));
    ImGui::PushStyleColor(ImGuiCol_Border, pal::Border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(kRowRounding));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(14), px(12)));
    ImGui::BeginChild(id, ImVec2(width, height), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
     
    {
        size_t shown = 0, total = 0;
        for (const Item& it : s.items) total += (it.owner == Owner::Account) == account;
        for (size_t i : v.visible) shown += (s.items[i].owner == Owner::Account) == account;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* d = ImGui::GetWindowDrawList();
        const ImVec2 is = textSize(FontKind::Icons, 15, glyph);
        drawText(d, FontKind::Icons, 15, ImVec2(p.x, p.y + px(2)), u32(pal::Accent), glyph);
        drawText(d, FontKind::Semibold, type::H2, ImVec2(p.x + is.x + px(8), p.y), u32(pal::Text), title);
        char cnt[48];
        if (shown == total) std::snprintf(cnt, sizeof cnt, "%zu items", total);
        else std::snprintf(cnt, sizeof cnt, "%zu of %zu", shown, total);
        const ImVec2 cs = textSize(FontKind::Body, type::Small, cnt);
        drawText(d, FontKind::Body, type::Small, ImVec2(p.x + availWidth() - cs.x, p.y + px(2)), u32(pal::Faint), cnt);
        ImGui::Dummy(ImVec2(availWidth(), px(26)));
    }
    gap(sp::XS);
    std::map<int, std::vector<size_t>> byBucket;
    for (size_t i : v.visible) {
        const Item& it = s.items[i];
        if ((it.owner == Owner::Account) != account) continue;
        byBucket[it.bucket].push_back(i);
    }
    std::vector<int> order = account ? vaultOrder() : characterOrder();
    for (const auto& [key, _] : byBucket)
        if (std::find(order.begin(), order.end(), key) == order.end()) order.push_back(key);
    bool any = false;
    const float size = px(kTile), gapPx = px(kTileGap);
    for (int key : order) {
        auto found = byBucket.find(key);
        const Bucket* b = cat.bucket(key);
        const bool alwaysShow = v.query.empty() && v.group == 0 && b && (account ? isVaultBucket(key) : isWeaponBucket(key));
        if (found == byBucket.end() && !alwaysShow) continue;
        any = true;
        bucketHeader(bucketName(key), b ? usedRows(s, *b) : int(found == byBucket.end() ? 0 : found->second.size()), b ? b->count : 0);
        if (found == byBucket.end()) {
            caption("Empty", pal::Faint);
            gap(sp::M);
            continue;
        }
        const int cols = std::max(1, int((availWidth() + gapPx) / (size + gapPx)));
        int col = 0;
        for (size_t i : found->second) {
            if (col) ImGui::SameLine(0, gapPx);
            const Item& it = s.items[i];
            const bool isSel = sel.valid && ((sel.soid && sel.soid == it.soid) || (!sel.soid && sel.owner == it.owner && sel.slot == it.slot));
            if (tile(it, i, isSel, v)) select(selectionOf(it));
            if (++col == cols) col = 0;
        }
        gap(sp::M);
    }
    if (!any) {
        gap(sp::XL);
        caption(v.query.empty() ? "Nothing Here" : "No Items Match the Search", pal::Faint);
    }
    ImGui::EndChild();
}

void listView(const Snapshot& s, View& v, const Selection& sel, float width, float height) {
    const Catalog& cat = catalog();
    constexpr float rowH = 30;
    const bool soidCol = width >= px(940);
    if (!beginTable("##invlist", soidCol ? 7 : 6, height / scale(), ImGuiTableFlags_Sortable | ImGuiTableFlags_BordersOuter, width / scale())) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("##fav", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort, px(18), 0);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort, 2.6f, 1);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, px(104), 2);
    ImGui::TableSetupColumn("Where", ImGuiTableColumnFlags_WidthFixed, px(118), 3);
    ImGui::TableSetupColumn("Qty", ImGuiTableColumnFlags_WidthFixed, px(38), 4);
    ImGui::TableSetupColumn("Perks", ImGuiTableColumnFlags_WidthStretch, 2.0f, 5);
    if (soidCol) ImGui::TableSetupColumn("SOID", ImGuiTableColumnFlags_WidthFixed, px(136), 6);
    tableHeaders();
    std::vector<size_t> rows = v.visible;
    if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsCount > 0) {
        v.sort = specs->Specs[0];
        v.sorted = true;
        specs->SpecsDirty = false;
    }
    if (v.sorted) {
        const ImGuiID col = v.sort.ColumnUserID;
        const bool asc = v.sort.SortDirection != ImGuiSortDirection_Descending;
        std::stable_sort(rows.begin(), rows.end(), [&](size_t a, size_t b) {
            const Item& x = s.items[a];
            const Item& y = s.items[b];
            int c = 0;
            switch (col) {
            case 2: {
                const ItemDef* dx = cat.item(x.def);
                const ItemDef* dy = cat.item(y.def);
                c = (dx ? dx->type : std::string()).compare(dy ? dy->type : std::string());
                break;
            }
            case 3: c = int(x.owner) != int(y.owner) ? int(x.owner) - int(y.owner) : x.bucket != y.bucket ? x.bucket - y.bucket : int(x.slot) - int(y.slot); break;
            case 4: c = x.quantity < y.quantity ? -1 : x.quantity > y.quantity ? 1 : 0; break;
            case 6: c = x.soid < y.soid ? -1 : x.soid > y.soid ? 1 : 0; break;
            default: c = lowerAscii(itemName(x, cat)).compare(lowerAscii(itemName(y, cat))); break;
            }
            return asc ? c < 0 : c > 0;
        });
    }
    ImGuiListClipper clip;
    clip.Begin(int(rows.size()), px(rowH));
    ImGui::PushFont(font(FontKind::Body), fontSize(FontKind::Body, type::Label));
    while (clip.Step()) {
        for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
            const size_t idx = rows[size_t(r)];
            const Item& it = s.items[idx];
            const bool isSel = sel.valid && ((sel.soid && sel.soid == it.soid) || (!sel.soid && sel.owner == it.owner && sel.slot == it.slot));
            ImGui::TableNextRow(0, px(rowH));
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(int(idx));
            const float cellY = ImGui::GetCursorPosY();
            if (tableRowSelectable("##row", isSel, nullptr, rowH - 2 * ImGui::GetStyle().CellPadding.y / scale())) select(selectionOf(it));
            if (isSel && v.scrollToSel) {
                ImGui::SetScrollHereY(0.4f);
                v.scrollToSel = false;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) itemTooltip(it, rollOf(v, idx));
            ImGui::SetCursorPosY(cellY);
            const bool fav = v.favs.count(it.hash) != 0;
            cellText(fav ? icon::StarFill : "", pal::Accent, FontKind::Icons, 11, rowH);
            ImGui::PopID();
            const int rar = rarityOf(it, cat);
            ImGui::TableSetColumnIndex(1);
            {
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const float cy = p.y + (px(rowH) - 2 * ImGui::GetStyle().CellPadding.y) * 0.5f;
                ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + px(4), cy), px(3.5f), u32(rarityColor(rar)));
                ImGui::SetCursorScreenPos(ImVec2(p.x + px(14), p.y));
                std::string name = itemName(it, cat);
                if (it.equipped) name += "  (equipped)";
                cellText(name.c_str(), isSel ? pal::Accent : pal::Text, FontKind::Body, type::Label, rowH);
            }
            ImGui::TableSetColumnIndex(2);
            const ItemDef* d = cat.item(it.def);
            cellText(d ? d->type.c_str() : "", pal::Muted, FontKind::Body, type::Label, rowH);
            ImGui::TableSetColumnIndex(3);
            cellText(bucketName(it.bucket).c_str(), it.owner == Owner::Character ? pal::Info : pal::Muted, FontKind::Body, type::Small, rowH);
            ImGui::TableSetColumnIndex(4);
            cellText(std::to_string(it.quantity).c_str(), pal::Muted, FontKind::Body, type::Label, rowH);
            ImGui::TableSetColumnIndex(5);
            const RollView* roll = rollOf(v, idx);
            cellText(roll && roll->readable ? roll->summary.c_str() : it.soid ? "-" : "", roll && roll->readable ? pal::Soft : pal::Faint,
                     FontKind::Body, type::Small, rowH);
            if (soidCol) {
                ImGui::TableSetColumnIndex(6);
                cellText(it.soid ? hex16(it.soid).c_str() : "stack", it.soid ? pal::Muted : pal::Faint, FontKind::Mono, type::MonoSmall, rowH);
            }
        }
    }
    ImGui::PopFont();
    if (rows.empty()) {
        ImGui::TableNextRow(0, px(100));
        ImGui::TableSetColumnIndex(1);
        gap(sp::XL);
        caption("No Items Match the Search", pal::Faint);
    }
    endTable();
}

 
void perkPills(const RollView& roll) {
    const float maxX = ImGui::GetCursorScreenPos().x + availWidth();
    bool first = true;
    std::set<std::string> shown;
    for (const RollPerk& p : roll.perks) {
        if (!shown.insert(p.name).second) continue;
        const float w = textSize(FontKind::Body, type::Small, p.name.c_str()).x + px(26);
        if (!first) {
            ImGui::SameLine(0, px(6));
            if (ImGui::GetCursorScreenPos().x + w > maxX) ImGui::NewLine();
        }
        first = false;
        pill(p.name.c_str(), p.active ? pal::Soft : pal::Faint, p.active ? nullptr : icon::Remove);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip(p.active ? "Node %d: unlocked" : "Node %d: rolled, not unlocked yet", p.node);
    }
}

void techRow(const char* name, const std::string& value, bool copy = true) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    drawText(ImGui::GetWindowDrawList(), FontKind::Body, type::Small, ImVec2(p.x, p.y + px(3)), u32(pal::Faint), name);
    ImGui::SetCursorScreenPos(ImVec2(p.x + px(96), p.y));
    if (copy) copyable(value.c_str(), name, pal::Muted);
    else mono(value.c_str(), pal::Muted, type::MonoSmall);
}

void detailPanel(Ui& ui, const Snapshot& s, View& v, float width, float height) {
    const Catalog& cat = catalog();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, pal::Raised);
    ImGui::PushStyleColor(ImGuiCol_Border, pal::Border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(kRowRounding));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(16), px(14)));
    ImGui::BeginChild("##invdetail", ImVec2(width, height), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    const Selection sel = selection();
    const Item* it = findSelected(s, sel);
    if (!it) {
        emptyState(icon::Vault, "Select an Item",
                   "Pick a tile to see its roll and to transfer, equip, use or dismantle it. Press / to search; is:weapon, is:vault, perk:\"Full Auto\" "
                   "and -is:equipped narrow the view.",
                   std::max(220.0f, height / scale() - 40));
        ImGui::EndChild();
        return;
    }
    const ItemDef* d = cat.item(it->def);
    const int r = rarityOf(*it, cat);
    const ImVec4 rc = rarityColor(r);
     
    if (!rarityText(r).empty()) {
        pill(rarityText(r).c_str(), rc);
        ImGui::SameLine(0, px(6));
    }
    pill(locationName(*it).c_str(), it->owner == Owner::Character ? pal::Info : pal::Muted, it->owner == Owner::Character ? icon::Home : icon::Vault);
    if (it->equipped) {
        ImGui::SameLine(0, px(6));
        pill("Equipped", pal::Accent, icon::Check);
    }
    ImGui::SameLine();
    alignRight(px(30));
    const bool fav = favourite(it->hash);
    if (iconButton("##fav", fav ? icon::StarFill : icon::Star, fav ? "Remove from favourites" : "Add to favourites (Favourites filter, Ctrl+K)", false, 30,
                   fav ? pal::Accent : pal::Muted)) {
        toggleFavourite(it->hash);
        v.builtFor = ~0ull;
    }
    gap(sp::S);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + availWidth());
    title(itemName(*it, cat).c_str());
    ImGui::PopTextWrapPos();
    std::string sub = d && !d->type.empty() ? d->type : std::string("Item");
    sub += "  \xC2\xB7  " + bucketName(it->bucket) + "  \xC2\xB7  slot " + std::to_string(it->slot);
    caption(sub.c_str(), pal::Muted);
    if (d && !d->alias.empty()) caption(("Display name: " + d->alias).c_str(), pal::Faint);
    gap(sp::M);
     
    {
        const float w = std::floor((availWidth() - px(8) * 2) / 3);
        statTile("q", "QUANTITY", std::to_string(it->quantity), it->soid ? "instance" : "stack", w);
        ImGui::SameLine(0, px(8));
        const Bucket* b = cat.bucket(it->bucket);
        statTile("b", "BUCKET", b ? std::to_string(usedRows(s, *b)) + "/" + std::to_string(b->count) : "-", "rows used", w);
        ImGui::SameLine(0, px(8));
        const int eq = s.equippedIndex(it->soid);
        statTile("e", "EQUIPPED", eq >= 0 ? "Yes" : "No", eq >= 0 ? "slot " + std::to_string(eq) : "", w, eq >= 0 ? pal::Accent : pal::Text);
    }
     
    gap(sp::M);
    if (it->soid) {
        fieldLabel("ROLL");
        gap(sp::XS + 2);
        const RollView* roll = nullptr;
        for (size_t i = 0; i < s.items.size(); ++i)
            if (&s.items[i] == it) roll = rollOf(v, i);
        if (roll && roll->readable) {
            perkPills(*roll);
        } else {
            wrapped(roll ? ("Roll unavailable: " + roll->why).c_str() : "Roll unavailable: no detail row for this item.", pal::Faint, type::Small);
        }
        gap(sp::M);
    }
     
    fieldLabel("ACTIONS");
    gap(sp::XS + 2);
    const bool busy = actionBusy();
    const bool conn = ui.connected();
    const bool demo = shared().demo && !conn;
    const Action transfer = it->owner == Owner::Character ? Action::Deposit : Action::Withdraw;
    const Eligibility te = eligibility(s, *it, transfer, cat);
    if (!it->soid && te.ok) {
        if (!(v.quantityFor == sel)) {
            v.quantityFor = sel;
            v.quantity = 1;
        }
        int q = int(std::min(v.quantity, te.maxQuantity));
        inputInt("##qty", &q, 1, int(std::max<uint32_t>(1, te.maxQuantity)), 140);
        v.quantity = uint32_t(std::max(1, q));
        ImGui::SameLine(0, px(6));
        if (button("All", {.tone = Tone::Ghost, .small = true, .selected = v.quantity == te.maxQuantity})) v.quantity = te.maxQuantity;
        ImGui::SameLine(0, px(10));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(6));
        captionf("%u can move", te.maxQuantity);
        gap(sp::S);
    }
    {
        const std::string reason = !conn && !demo ? std::string(kConnectFirst) : busy ? "Another inventory request is being confirmed" : te.reason;
        DisabledScope off(!te.ok || busy || (!conn && !demo), reason.c_str());
        std::string lbl = actionVerb(transfer);
        if (!it->soid && te.ok) lbl += " x" + std::to_string(v.quantity);
        if (button(lbl.c_str(), {.tone = Tone::Primary, .icon = transfer == Action::Deposit ? icon::Vault : icon::Home, .width = -1,
                                 .tooltip = "Fresh snapshot, then one guarded request; confirmed from the inventory within 20 s (never retried). Ctrl+Enter",
                                 .busy = busy, .successKey = "inventory.transfer"}))
            startAction(transfer, it->soid ? 1 : v.quantity);
    }
    gap(sp::S);
    {
        const Action eqA = it->equipped ? Action::Unequip : Action::Equip;
        const Eligibility ee = eligibility(s, *it, eqA, cat);
        const Eligibility ue = eligibility(s, *it, Action::Use, cat);
        const Eligibility de = eligibility(s, *it, Action::Dismantle, cat);
        const float w = (availWidth() - px(8) * 2) / 3 / scale();
        auto why = [&](const Eligibility& e) {
            return !conn && !demo ? std::string(kConnectFirst) : busy ? std::string("Another inventory request is being confirmed") : e.reason;
        };
        std::string r1 = why(ee), r2 = why(ue), r3 = why(de);
        {
            DisabledScope off(!ee.ok || busy || (!conn && !demo), r1.c_str());
            if (button(actionName(eqA), {.icon = it->equipped ? icon::Remove : icon::Check, .width = w, .tooltip = "Op 403 / 404. Needs check: new on PS3."}))
                startAction(eqA);
        }
        ImGui::SameLine(0, px(8));
        {
            DisabledScope off(!ue.ok || busy || (!conn && !demo), r2.c_str());
            if (button("Use", {.icon = icon::Play, .width = w, .tooltip = "Op 402 on a stack (consumables). Asks first. Needs check: new on PS3."}))
                startAction(Action::Use);
        }
        ImGui::SameLine(0, px(8));
        {
            DisabledScope off(!de.ok || busy || (!conn && !demo), r3.c_str());
            if (button("Dismantle", {.tone = Tone::Danger, .icon = icon::Delete, .width = w, .tooltip = "Op 402 on an instance. Asks first. Delete"}))
                startAction(Action::Dismantle);
        }
        gap(sp::XS + 2);
        badgePill(kBadgeNeedsCheck);
    }
     
    std::string progress, actionLabel;
    LastResult last;
    {
        Shared& S = shared();
        std::lock_guard<std::mutex> lock(S.m);
        progress = S.progress;
        actionLabel = S.actionLabel;
        last = S.last;
    }
    if (busy || !progress.empty()) {
        gap(sp::M);
        progressBar(-1, 0, 4);
        gap(sp::XS);
        caption(progress.empty() ? "Working..." : progress.c_str(), pal::Muted);
        if (busy) {
            ImGui::SameLine();
            alignRight(buttonWidth("Stop Watching", icon::Stop, true));
            if (button("Stop Watching", {.tone = Tone::Ghost, .icon = icon::Stop, .small = true,
                                          .tooltip = "Stops waiting for the confirmation. A request already sent is not undone."}))
                shared().stopWatch = true;
        }
        requestRedraw();
    } else if (!last.text.empty()) {
        gap(sp::M);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + availWidth());
        wrapped(last.text.c_str(), last.ok ? pal::Success : mix(pal::Warning, pal::Text, 0.2f), type::Small);
        ImGui::PopTextWrapPos();
    }
     
    gap(sp::M);
    divider(sp::S);
    if (button("Technical Details", {.tone = Tone::Ghost, .icon = v.showTech ? icon::ChevronUp : icon::ChevronDown, .small = true}))
        v.showTech = !v.showTech;
    if (v.showTech) {
        gap(sp::XS);
        techRow("Definition", std::to_string(it->def), false);
        techRow("Item Hash", hx(it->hash));
        if (d) techRow("Item Tag", hx(d->tag) + " (Xbox id)");
        techRow("SOID", it->soid ? hex16(it->soid) : std::string("0 (stack)"));
        techRow("Holder", std::string(it->owner == Owner::Account ? "account" : "character") + " slot " + std::to_string(it->slot) + ", bucket " +
                              std::to_string(it->bucket), false);
        techRow("Row +2..+7", hexBytes(it->raw.data(), it->raw.size()));
        techRow("Row Address", hx(s.profileOffset(it->owner) + s.manager +
                                  (it->owner == Owner::Account ? off::ACCOUNT_HOLDER : off::CHARACTER_HOLDER) + it->slot * off::ROW_SIZE));
        if (it->hasDetail) techRow("Detail Row", hx(it->detailAddr));
        if (d) techRow("Stacks", d->perInstance ? std::string("instance") : "max " + std::to_string(d->maxStack), false);
    }
    ImGui::EndChild();
}

 
void filterPopup(View& v, const std::shared_ptr<const Snapshot>& snap) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(12), px(10)));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, px(10));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, pal::RaisedFill);
    ImGui::PushStyleColor(ImGuiCol_Border, pal::Border);
    if (ImGui::BeginPopup("##invfilters")) {
        fieldLabel("TIER");
        gap(sp::XS + 2);
        const std::pair<int, const char*> tiers[] = {{5, "Exotic"}, {4, "Legendary"}, {3, "Rare"}, {2, "Uncommon"}, {1, "Common"}};
        for (auto [t, label] : tiers) {
            bool on = v.tiers.count(t) != 0;
            if (checkbox(label, &on)) {
                if (on) v.tiers.insert(t);
                else v.tiers.erase(t);
            }
        }
        gap(sp::S);
        fieldLabel("BUCKET");
        gap(sp::XS + 2);
        std::map<int, int> counts;
        if (snap)
            for (const Item& it : snap->items) ++counts[it.bucket];
        ImGui::BeginChild("##buckets", ImVec2(px(240), px(220)));
        if (ImGui::Selectable("Any Bucket", v.bucket < 0)) v.bucket = -1;
        for (const auto& [key, n] : counts) {
            const std::string text = bucketName(key) + "  (" + std::to_string(n) + ")";
            if (ImGui::Selectable(text.c_str(), v.bucket == key)) v.bucket = key;
        }
        ImGui::EndChild();
        gap(sp::S);
        if (button("Clear Filters", {.tone = Tone::Ghost, .icon = icon::Close, .small = true})) {
            v.tiers.clear();
            v.bucket = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

void filterChips(View& v) {
    if (v.tiers.empty() && v.bucket < 0) return;
    gap(sp::XS);
    std::vector<std::string> chips;
    for (int t : v.tiers) chips.push_back(rarityText(t));
    if (v.bucket >= 0) chips.push_back(bucketName(v.bucket));
    for (const std::string& c : chips) {
        pill(c.c_str(), pal::Accent, icon::Filter);
        ImGui::SameLine(0, px(6));
    }
    if (button("Clear##filters", {.tone = Tone::Ghost, .icon = icon::Close, .small = true, .tooltip = "Remove the tier and bucket filters"})) {
        v.tiers.clear();
        v.bucket = -1;
    }
}

void drawInventory(Ui& ui) {
    Shared& S = shared();
    View& v = view();
    const Catalog& cat = catalog();
    if (!v.loaded) {
        v.loaded = true;
        v.view = std::clamp(settings().integer(kViewKey, 0), 0, 1);
        v.group = std::clamp(settings().integer(kGroupKey, 0), 0, 5);
        if (demoMode()) seedDemo();
    }
    if (!cat.error.empty()) {
        emptyState(icon::Warning, "Item Catalogue Unavailable", cat.error.c_str(), 200);
        return;
    }
    std::shared_ptr<const Snapshot> snap;
    uint64_t generation;
    double readAt;
    std::string error;
    bool notSignedIn;
    {
        std::lock_guard<std::mutex> lock(S.m);
        snap = S.snap;
        generation = S.generation;
        readAt = S.readAt;
        error = S.error;
        notSignedIn = S.notSignedIn;
    }
    const bool conn = ui.connected();
    const bool reading = ui.busy(kReadJob);
     
    if (conn && !v.autoRead && !reading && !snap && error.empty()) {
        v.autoRead = true;
        postRefresh(ui, true);
    }
    const double now = detail::activityClock();
    if (conn && snap && settings().flag(kAutoKey, false) && !reading && !actionBusy() && now - readAt > 20 && now - v.lastAuto > 20) {
        v.lastAuto = now;
        postRefresh(ui, true);
    }
     
    const float avail = availWidth();
    {
        const float searchW = std::min(px(380), avail * 0.34f);
        searchField("##invsearch", &v.query, "Search Name, Perk, Type, is:weapon...", searchW / scale(), false);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Words match name, type, rarity, bucket, hash, SOID and perks.\nFilters: is:weapon armor general equipped vault "
                              "character stack instance exotic legendary; perk:\"Full Auto\"; -term negates.");
        const size_t favCount = settings().list(kFavKey).size();
        std::vector<std::string> groups = {"All", "Weapons", "Armor", "General", favCount ? "Favourites  " + std::to_string(favCount) : "Favourites", "Recent"};
        const float bw = buttonWidth("Refresh", icon::Refresh);
        const float rightW = bw + px(8) + px(kControlH) * 3 + px(4) * 2;
        float pillsW = px(8);
        for (const std::string& g : groups) pillsW += textSize(FontKind::Semibold, type::Label, g.c_str()).x + px(30);
        const bool oneRow = searchW + px(12) + pillsW + px(16) + rightW <= avail;
        if (oneRow) {
            ImGui::SameLine(0, px(12));
            if (pillTabs("##invgroup", groups, &v.group)) settings().set(std::string(kGroupKey), v.group);
        }
         
        ImGui::SameLine();
        alignRight(rightW);
        const bool filtered = !v.tiers.empty() || v.bucket >= 0;
        if (iconButton("##filters", icon::Filter, "Filter by tier and bucket", filtered, kControlH)) ImGui::OpenPopup("##invfilters");
        filterPopup(v, snap);
        ImGui::SameLine(0, px(4));
        if (iconButton("##grid", icon::Vault, "Grid view (character | vault)", v.view == 0, kControlH)) {
            v.view = 0;
            settings().set(std::string(kViewKey), 0);
        }
        ImGui::SameLine(0, px(4));
        if (iconButton("##list", icon::List, "List view (sortable, every item)", v.view == 1, kControlH)) {
            v.view = 1;
            settings().set(std::string(kViewKey), 1);
        }
        ImGui::SameLine(0, px(8));
        {
            auto offline = requireConnection(ui);
            if (button("Refresh", {.tone = Tone::Primary, .icon = icon::Refresh,
                                   .tooltip = "Read a coherent snapshot of your character and vault (about a second)", .busy = reading,
                                   .successKey = "inventory.refresh"}))
                postRefresh(ui);
        }
        if (!oneRow) {
            gap(sp::S);
            if (pillTabs("##invgroup", groups, &v.group)) settings().set(std::string(kGroupKey), v.group);
        }
    }
    gap(sp::S);
     
    {
        char buf[200];
        if (snap) {
            const double age = std::max(0.0, now - readAt);
            if (age < 90) std::snprintf(buf, sizeof buf, "Read %.0f s ago", age);
            else std::snprintf(buf, sizeof buf, "Read %.0f min ago", age / 60);
            std::string line = buf;
            std::snprintf(buf, sizeof buf, "  \xC2\xB7  family version %u  \xC2\xB7  %zu items  \xC2\xB7  %u definitions  \xC2\xB7  %llu reads, %.1f s",
                          snap->familyVersion, snap->items.size(), snap->defCount, static_cast<unsigned long long>(snap->reads), snap->ms / 1000.0);
            line += buf;
            if (shared().demo && !conn) line += "  \xC2\xB7  sample data (review mode)";
            caption(line.c_str(), pal::Faint);
            requestRedraw();
        } else if (reading) {
            caption("Reading your inventory...", pal::Muted);
        } else {
            caption(conn ? "No snapshot yet." : "Connect to read your inventory.", pal::Faint);
        }
        ImGui::SameLine();
        bool autoOn = settings().flag(kAutoKey, false);
        const float tw = px(22) + px(8) + textSize(FontKind::Body, type::Small, "Auto Refresh").x;
        alignRight(tw);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        if (toggleSwitch("##auto", &autoOn)) settings().set(std::string(kAutoKey), autoOn);
        tooltip("Read again every 20 s while this page is open (after each action it reads again anyway)");
        drawText(ImGui::GetWindowDrawList(), FontKind::Body, type::Small, ImVec2(at.x + px(30), textTop(FontKind::Body, type::Small, at.y + px(11))),
                 u32(autoOn ? pal::Text : pal::Muted), "Auto Refresh");
        ImGui::SetCursorScreenPos(ImVec2(at.x, at.y + px(22)));
        ImGui::Dummy(ImVec2(1, 1));
    }
    if (!error.empty()) {
        gap(sp::XS);
        pill(error.c_str(), notSignedIn ? pal::Info : pal::Warning, notSignedIn ? icon::Info : icon::Warning);
    }
    filterChips(v);
    gap(sp::M);
    if (!snap) {
        if (notSignedIn) {
            emptyState(icon::Home, "Load Your Character",
                       "The inventory profiles are empty: sign in and pick a character (orbit or an activity), then Refresh.", 260);
        } else if (reading) {
            emptyState(icon::Vault, "Reading Your Inventory", "A coherent snapshot takes about a second: every range is read twice and compared.", 260);
        } else {
            emptyState(icon::Vault, conn ? "No Inventory Read Yet" : "Connect to See Your Vault",
                       conn ? "Press Refresh to read your character and vault."
                            : "Connect to the PS3 from the top bar. The Vault page reads your character and vault, shows each weapon's roll, and "
                              "transfers, equips, uses or dismantles single items with a fresh preflight and a confirmation.",
                       260);
        }
        return;
    }
    rebuild(v, snap, generation);
     
    {
        const float w = std::floor((avail - px(8) * 4) / 5);
        size_t onChar = 0;
        for (const Item& it : snap->items) onChar += it.owner == Owner::Character;
        statTile("c", "CHARACTER", std::to_string(onChar), "items", w);
        const int keys[3] = {24, 22, 23};
        const char* names[3] = {"VAULT WEAPONS", "VAULT ARMOR", "VAULT GENERAL"};
        for (int k = 0; k < 3; ++k) {
            ImGui::SameLine(0, px(8));
            const Bucket* b = cat.bucket(keys[k]);
            const int used = b ? usedRows(*snap, *b) : 0, capa = b ? b->count : 0;
            statTile(names[k], names[k], std::to_string(used) + " / " + std::to_string(capa), std::to_string(capa - used) + " free", w,
                     capa && used >= capa ? pal::Warning : pal::Text);
        }
        ImGui::SameLine(0, px(8));
        std::string pm;
        for (const PostmasterEntry& e : snap->postmaster) {
            const ItemDef* d = cat.item(e.def);
            if (pm.size() < 60) pm += (pm.empty() ? "" : ", ") + (d ? d->label() : std::to_string(e.def));
        }
        statTile("pm", "POSTMASTER", std::to_string(snap->postmaster.size()), pm.empty() ? "nothing waiting" : pm, w,
                 snap->postmaster.empty() ? pal::Text : pal::Info);
    }
    gap(sp::M);
     
    const Selection sel = selection();
    if (sel.valid && !findSelected(*snap, sel)) select({});
    const bool wide = avail >= px(980);
    const float detailW = wide ? std::clamp(avail * 0.30f, px(320), px(400)) : avail;
    const float bodyW = wide ? avail - detailW - px(14) : avail;
    const float viewH = ImGui::GetWindowHeight();
    const float h = std::clamp(viewH - px(300), px(440), px(960));
    if (v.view == 1) {
        listView(*snap, v, selection(), bodyW, h);
    } else {
        const float colW = std::floor((bodyW - px(12)) * 0.5f);
        gridColumn("##char", "Character", icon::Home, false, *snap, v, selection(), colW, h);
        ImGui::SameLine(0, px(12));
        gridColumn("##vault", "Vault", icon::Vault, true, *snap, v, selection(), bodyW - colW - px(12), h);
    }
    if (wide) {
        ImGui::SameLine(0, px(14));
        detailPanel(ui, *snap, v, detailW, h);
    } else {
        gap(sp::M);
        detailPanel(ui, *snap, v, detailW, px(520));
    }
     
    const bool popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (!popup && !ImGui::GetIO().WantTextInput && conn) {
        if (const Item* it = findSelected(*snap, selection())) {
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Enter))
                startAction(it->owner == Owner::Character ? Action::Deposit : Action::Withdraw, it->soid ? 1 : v.quantity);
            else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
                startAction(Action::Dismantle);    
        }
    }
}

 
void resetOnPanic() {
    Shared& S = shared();
    S.acting = false;
    S.stopWatch = false;
    std::lock_guard<std::mutex> lock(S.m);
    S.progress.clear();
}

 
void resetOnDisconnect() {
    Shared& S = shared();
    S.acting = false;
    S.stopWatch = false;
    std::lock_guard<std::mutex> lock(S.m);
    S.progress.clear();
    view().autoRead = false;
    if (!S.demo) {
        S.snap.reset();    
        ++S.generation;
    }
    S.error.clear();
}

 

void paletteItems(const std::string& query, std::vector<PaletteEntry>& out) {
    const auto s = current();
    if (!s) return;
    const Catalog& cat = catalog();
    const std::set<uint32_t> favs = hashSet(kFavKey);
    const Query q(query);
    int n = 0;
    for (const Item& it : s->items) {
        const bool fav = favs.count(it.hash) != 0;
        if (!fav && query.size() < 2) continue;
        if (!q.matches(it, cat)) continue;
        if (++n > 40) break;
        PaletteEntry e;
        e.label = itemName(it, cat) + (it.soid ? "" : " x" + std::to_string(it.quantity));
        e.where = "Vault  \xE2\x80\xBA  " + locationName(it) + "  \xC2\xB7  " + bucketName(it.bucket) + (it.equipped ? "  \xC2\xB7  equipped" : "");
        e.icon = fav ? icon::StarFill : icon::Vault;
        e.badge = rarityText(rarityOf(it, cat));
        e.boost = fav ? 250 : 0;
        const Selection sel = selectionOf(it);
        e.run = [sel] { revealItem(sel); };
        const Action a = it.owner == Owner::Character ? Action::Deposit : Action::Withdraw;
        if (eligibility(*s, it, a, cat).ok) {
            e.alt = [sel, a] {
                select(sel);
                startAction(a);
            };
            e.altHint = a == Action::Deposit ? "To Vault" : "To Character";
        }
        out.push_back(std::move(e));
    }
}

void selectedAction(Ctx& c, Action a) { runSelectedAction(c, a); }

}   

}   

namespace brps3 {

BR_FEATURES(inventory_vault) {
    using namespace invui;
    using inventory::Action;
    r.section({.id = "vault.inventory", .page = Page::Vault, .tab = "Inventory", .section = "Character & vault", .order = 10, .draw = drawInventory,
               .onDisconnect = resetOnDisconnect, .onPanic = resetOnPanic});
    const std::string sec = "Character & vault";
    r.action({.id = "vault.refresh", .page = Page::Vault, .tab = "Inventory", .section = sec, .label = "Refresh Inventory",
              .help = "Read a coherent snapshot of your character and vault and show it on the Vault page.",
              .run = [](Ctx& c) {
                  const auto s = readAndPublish(c, true);
                  c.toast("Inventory read: " + std::to_string(s->items.size()) + " items");
              }});
    r.action({.id = "vault.transfer_selected", .page = Page::Vault, .tab = "Inventory", .section = sec, .label = "Transfer Selected Item",
              .help = "Send the item selected on the Vault page to the vault (or back to your character): native catalogue check (once per "
                      "session) and the item's live definition, fresh preflight, one guarded request, confirmed from the inventory, never retried.",
              .run = [](Ctx& c) {
                  const auto s = current();
                  const Item* it = s ? findSelected(*s, selection()) : nullptr;
                  if (!it) throw Error("select an item on the Vault page first");
                  selectedAction(c, it->owner == inventory::Owner::Character ? Action::Deposit : Action::Withdraw);
              }});
    r.action({.id = "vault.equip_selected", .page = Page::Vault, .tab = "Inventory", .section = sec, .label = "Equip Selected Item",
              .help = "Equip the item selected on the Vault page (op 403). New on PS3: confirm the first one in game.",
              .run = [](Ctx& c) { selectedAction(c, Action::Equip); }, .badge = kBadgeNeedsCheck});
    r.action({.id = "vault.unequip_selected", .page = Page::Vault, .tab = "Inventory", .section = sec, .label = "Unequip Selected Item",
              .help = "Unequip the item selected on the Vault page (op 404). New on PS3: confirm the first one in game.",
              .run = [](Ctx& c) { selectedAction(c, Action::Unequip); }, .badge = kBadgeNeedsCheck});
    r.action({.id = "vault.use_selected", .page = Page::Vault, .tab = "Inventory", .section = sec, .label = "Use Selected Item",
              .help = "Use one of the consumable stack selected on the Vault page (op 402). The game consumes it.",
              .run = [](Ctx& c) { selectedAction(c, Action::Use); }, .dangerous = true, .badge = kBadgeNeedsCheck});
    r.action({.id = "vault.dismantle_selected", .page = Page::Vault, .tab = "Inventory", .section = sec, .label = "Dismantle Selected Item",
              .help = "Dismantle the weapon or armor selected on the Vault page (op 402). It is destroyed for good.",
              .run = [](Ctx& c) { selectedAction(c, Action::Dismantle); }, .dangerous = true});
    ui::styleSection(sec, {.fullWidth = true,
                           .icon = ui::icon::Vault,
                           .ownsActions = true});
    ui::addPaletteProvider(paletteItems);
}

}   
