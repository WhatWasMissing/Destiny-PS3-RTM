#include "game/player_writes.h"

#include <algorithm>
#include <cstdio>
#include <map>

#include "core/error.h"
#include "core/runtime.h"
#include "game/world_addr.h"
#include "game/world_components.h"
#include "game/world_objects.h"
#include "game/world_tags.h"

namespace brps3::player {

namespace {

using world::hex8;
using Clock = std::chrono::steady_clock;

std::string words(int n) { return std::to_string(n) + (n == 1 ? " word" : " words"); }

double steadySeconds() { return std::chrono::duration<double>(Clock::now().time_since_epoch()).count(); }

}   

 

const char* featureName(Feature f) {
    switch (f) {
    case Feature::God: return "God mode";
    case Feature::Interact: return "Interact range";
    case Feature::Pickup: return "Pickup range";
    case Feature::Super: return "Super recharge";
    case Feature::Grenade: return "Grenade recharge";
    case Feature::Melee: return "Melee recharge";
    case Feature::Handling: return "Instant handling";
    case Feature::ZoomMult: return "Zoom multiplier";
    case Feature::ZoomSpeed: return "Zoom speed";
    case Feature::Magazine: return "No reload";
    case Feature::RateOfFire: return "Rate of fire";
    case Feature::Damage: return "Max damage";
    case Feature::Spread: return "No spread";
    case Feature::Recoil: return "No recoil";
    case Feature::Ammo: return "Infinite ammo";
    case Feature::FullAuto: return "Full auto";
    case Feature::Count: break;
    }
    return "?";
}

const char* featureKey(Feature f) {
    switch (f) {
    case Feature::God: return "god";
    case Feature::Interact: return "interact_range";
    case Feature::Pickup: return "pickup_range";
    case Feature::Super: return "super";
    case Feature::Grenade: return "grenade";
    case Feature::Melee: return "melee";
    case Feature::Handling: return "handling";
    case Feature::ZoomMult: return "zoom_mult";
    case Feature::ZoomSpeed: return "zoom_speed";
    case Feature::Magazine: return "no_reload";
    case Feature::RateOfFire: return "rate_of_fire";
    case Feature::Damage: return "damage";
    case Feature::Spread: return "no_spread";
    case Feature::Recoil: return "no_recoil";
    case Feature::Ammo: return "infinite_ammo";
    case Feature::FullAuto: return "full_auto";
    case Feature::Count: break;
    }
    return "?";
}

bool weaponFeature(Feature f) { return f >= Feature::Handling && f < Feature::Count; }

std::string ComponentChain::label() const { return weapon ? "weapon " + hex8(ownerHandle) : std::string("player component"); }

 

Resolver::Resolver(world::Reader& mem) : mem_(mem) {
    acc_ = world::readAccessorPool(mem);
    sobj_ = world::readPool(mem, world::addr::SOBJECT_POOL);
    try {
        tags_ = world::readTagTable(mem);
    } catch (const std::exception& e) {
        tagError_ = e.what();
    }
}

uint32_t Resolver::tagResolve(uint32_t h) {
    if (!tags_ || !world::validHandle(h)) return 0;
    const uint32_t page = world::tagPage(h);
    if (page >= tags_->pageCount || world::isPackagePage(page)) return 0;    
    auto it = pages_.find(page);
    if (it == pages_.end()) {
        PageInfo info;
        try {
            info.page = world::readPage(mem_, *tags_, page);
            if (info.page.live() && world::mapped(info.page.reg, 0x14)) info.limit = mem_.u32(info.page.reg + 0x10);
        } catch (const std::exception&) {
            info.limit = 0;
        }
        it = pages_.emplace(page, info).first;
    }
    const PageInfo& p = it->second;
    const uint32_t index = world::tagIndex(h);
    if (!p.page.live() || p.page.itemStride == 0 || p.page.itemStride > 0x1000 || index >= p.limit) return 0;
    const uint64_t element = uint64_t(p.page.items) + uint64_t(index) * p.page.itemStride;
    if (element > 0xFFFFFFF0ull || !world::mapped(uint32_t(element) + 8, 4)) return 0;
    const auto back = mem_.tryBytes(uint32_t(element) + 8, 4);
    if (!back) return 0;
    const uint32_t off = back->u32(0) & p.page.mask;
    if (off > element) return 0;
    const uint32_t base = uint32_t(element) - off;
    return world::heap(base) ? base : 0;
}

std::optional<Target> Resolver::component(const world::SObject& owner, bool weapon, std::string* problem, bool requireGate) {
    Target t;
    ComponentChain& c = t.chain;
    c.weapon = weapon;
    c.ownerHandle = owner.handle;
    c.ownerEntry = owner.addr;
    c.parent = owner.parent;
    c.handle = owner.component;
    c.sobjPoolBase = sobj_.base;
    c.accPoolBase = acc_.header.base;
    auto fail = [&](const std::string& why) -> std::optional<Target> {
        if (problem) *problem = c.label() + ": " + why;
        return std::nullopt;
    };
    if (!world::validHandle(c.handle)) return fail("no component handle at sObject +0x78");
    if (!acc_.plausible()) return fail("the component accessor pool is not set up");
    const auto acc = world::resolveComponent(mem_, acc_, c.handle, true);
    if (!acc) return fail("accessor entry for " + hex8(c.handle) + " is not live (identity mismatch)");
    if (!acc->allocated() || acc->size() < 0x20 || acc->size() > 0x400000 || !world::heap(acc->data, acc->size()))
        return fail("accessor " + hex8(acc->addr) + " does not describe a live component (flags " + hex8(acc->flags) + ")");
    c.accessor = acc->addr;
    c.accFlags = acc->flags;
    c.accBack = acc->backOffset;
    c.accInstance = acc->instance;
    c.accClass = acc->cls();
    c.data = acc->data;
    c.size = acc->size();
    c.tagBase = tagResolve(c.handle);
    if (c.tagBase && (c.tagBase < c.data || c.tagBase - c.data >= 0x40)) {
        tagNote_ = c.label() + ": tag-table resolve " + hex8(c.tagBase) + " is not next to the accessor data " + hex8(c.data) + " (ignored)";
        c.tagBase = 0;
    }
    const uint32_t want = weapon ? wfc::READ_SIZE + 0x10 : pc::READ_SIZE;
    const uint32_t n = std::min(c.size, want);
    const auto bytes = mem_.tryBytes(c.data, n);
    if (!bytes) return fail("component data at " + hex8(c.data) + " is unreadable");
    t.view.data = c.data;
    t.view.tagBase = c.tagBase;
    t.view.size = c.size;
    t.view.bytes = *bytes;
    if (weapon) {
        const uint32_t gateAt = c.pluginBase() + wfc::GATE;
        c.gate = t.view.has(gateAt, 4) ? t.view.u32(gateAt) : 0xFFFFFFFFu;
        if (c.gate != 1 && requireGate) return fail("gate +0x64 reads " + hex8(c.gate) + " (expected 1: not a live fire component)");
    }
    return t;
}

Targets resolveTargets(world::Reader& mem, bool weapons, world::PlayerCache* cache) {
    Targets t;
    std::optional<world::LocalPlayer> p;
    if (cache) {
        p = cache->get(mem, 1000, true);
        if (!p) t.note = cache->last().message;
    } else {
        const world::PlayerLookup r = world::findLocalPlayer(mem);
        p = r.player;
        if (!p) t.note = r.message;
    }
    if (!p) {
        if (t.note.empty()) t.note = "No spawned player: load into an activity.";
        return t;
    }
    Resolver r(mem);
    const auto obj = world::readSObject(mem, r.sobjectPool(), p->handle);
    if (!obj) {
        t.note = "The player's sObject " + hex8(p->handle) + " changed while reading: retrying.";
        if (cache) cache->invalidate();
        return t;
    }
    t.havePlayer = true;
    t.playerHandle = p->handle;
    t.playerEntry = obj->addr;
    std::string why;
    if (auto c = r.component(*obj, false, &why)) t.player = std::move(*c);
    else t.problems.push_back(why);
    if (weapons) {
        const world::ObjectScan scan = world::readObjects(mem);
        t.weaponsScanned = true;
        for (const world::SObject& o : world::childrenOf(scan, p->handle)) {
            if (o.type != world::TYPE_WEAPON || !o.live()) continue;
            why.clear();
            if (auto c = r.component(o, true, &why)) {
                t.weapons.push_back(std::move(*c));
            } else {
                if (!t.weaponsRefused++) t.weaponProblem = why;
                t.problems.push_back(why);
            }
        }
    }
    if (!r.tagNote().empty()) t.problems.push_back(r.tagNote());
    return t;
}

std::vector<gwl::Guard> chainGuards(const ComponentChain& c) {
    using namespace world::addr;
    std::vector<gwl::Guard> g;
    g.push_back({SOBJECT_POOL + 8, 0xFFFFFFFFu, c.sobjPoolBase, "sObject pool base"});
    g.push_back({c.ownerEntry + 0xC, 0xFFFFFFFFu, c.ownerHandle, "sObject +0xC == handle"});
    g.push_back({c.ownerEntry + 0x4, world::sobj::FLAG_DELETING, 0, "sObject not being deleted"});
    g.push_back({c.ownerEntry + 0x78, 0xFFFFFFFFu, c.handle, "sObject +0x78 == component handle"});
    if (c.weapon) {
        g.push_back({c.ownerEntry + 0x2C, 0xFFFFFFFFu, c.parent, "weapon +0x2C == player"});
        g.push_back({c.ownerEntry + 0x8, 0xFF000000u, uint32_t(world::TYPE_WEAPON) << 24, "sObject type == weapon"});
    }
    g.push_back({COMPONENT_ACCESSOR_POOL + 8, 0xFFFFFFFFu, c.accPoolBase, "accessor pool base"});
    g.push_back({c.accessor + 0xC, 0xFFFFFFFFu, c.handle, "accessor identity"});
    g.push_back({c.accessor + 0x8, 0xFFFFFFFFu, c.accBack, "accessor back offset"});
    g.push_back({c.accessor + 0x0, 0xFFFFFFFFu, c.accFlags, "accessor flags / size"});
    if (c.weapon) g.push_back({c.pluginBase() + wfc::GATE, 0xFFFFFFFFu, 1, "gate +0x64 == 1"});
    return g;
}

bool chainCurrent(world::Reader& mem, const ComponentChain& c) {
    try {
         
         
        if (mem.u32(world::addr::SOBJECT_POOL + 8) != c.sobjPoolBase || mem.u32(world::addr::COMPONENT_ACCESSOR_POOL + 8) != c.accPoolBase)
            return false;
        const world::Bytes e = mem.bytes(c.ownerEntry, 0x80);
        if (e.u32(0xC) != c.ownerHandle || (e.u32(4) & world::sobj::FLAG_DELETING) || e.u32(0x78) != c.handle) return false;
        if (c.weapon && (e.u32(0x2C) != c.parent || e.u8(8) != world::TYPE_WEAPON)) return false;
        const world::Bytes a = mem.bytes(c.accessor, 0x18);
        return a.u32(0) == c.accFlags && a.u32(8) == c.accBack && a.u32(0xC) == c.handle;
    } catch (const std::exception&) {
        return false;
    }
}

 

namespace {

struct Builder {
    Plan& plan;
    const Target& target;
    gwl::Group group;
    size_t groupIndex;
    bool anyLocated = false;

    Builder(Plan& p, const Target& t) : plan(p), target(t), groupIndex(p.groups.size()) {
        group.name = t.chain.label();
        group.guards = chainGuards(t.chain);
    }
    void add(Feature f, uint32_t addr, uint32_t value, uint32_t original, std::vector<gwl::Guard> guards, std::string field) {
        gwl::Write w;
        w.addr = addr;
        w.value = value;
        w.guards = std::move(guards);
        w.what = field;
        PlannedWrite pw;
        pw.feature = f;
        pw.chain = target.chain;
        pw.addr = addr;
        pw.original = original;
        pw.applied = value;
        pw.field = std::move(field);
        pw.group = groupIndex;
        pw.write = group.writes.size();
        group.writes.push_back(std::move(w));
        plan.writes.push_back(std::move(pw));
        ++plan.report[size_t(f)].writes;
    }
     
    bool record(Feature f, uint32_t pluginOffset, uint32_t value) {
        const Located l = locateRecord(target.view, pluginOffset);
        if (!l.ok) {
            plan.report[size_t(f)].refused.push_back(target.chain.label() + ": " + l.why);
            return false;
        }
        recordAt(f, l, pluginOffset, value);
        return true;
    }
    void recordAt(Feature f, const Located& l, uint32_t pluginOffset, uint32_t value) {
        const std::vector<gwl::Guard> sig = {{l.addr + 8, 0xFFFFFFFFu, kRecordSignature, "record signature"},
                                             {l.addr + 0x10, 0xFFFFFFFFu, l.recordOffset, "record self-offset"}};
        char field[96];
        std::snprintf(field, sizeof field, "+%X -> record +%X id %u (%s)", pluginOffset, l.recordOffset, l.record.id, viaName(l.via));
        add(f, l.addr, value, l.record.value, sig, field);
        add(f, l.addr + 4, value, l.record.copy, sig, std::string(field) + " copy");
        anyLocated = true;
    }
};

}   

Plan plan(const Targets& t, const FeatureSet& enabled, const Magnitudes& m) {
    Plan p;
    auto on = [&](Feature f) { return enabled[size_t(f)]; };
    if (!t.havePlayer) return p;
     
    bool playerFeatures = false;
    for (Feature f : {Feature::God, Feature::Interact, Feature::Pickup, Feature::Super, Feature::Grenade, Feature::Melee})
        if (on(f)) {
            playerFeatures = true;
            ++p.report[size_t(f)].targets;
        }
    if (playerFeatures && t.player) {
        Builder b(p, *t.player);
        auto feature = [&](Feature f, std::initializer_list<uint32_t> offsets, float value) {
            if (!on(f)) return;
            bool any = false;
            for (uint32_t off : offsets) any |= b.record(f, off, world::floatBits(value));
            if (any) ++p.report[size_t(f)].targetsWritten;
        };
        feature(Feature::God, {pc::GOD}, kGodValue);
        feature(Feature::Interact, {pc::INTERACT}, m.range);
        if (on(Feature::Pickup)) {
            bool any = false;
            for (uint32_t k = 0; k < pc::PICKUP_COUNT; ++k) any |= b.record(Feature::Pickup, pc::PICKUP0 + k * kRecordStride, world::floatBits(m.range));
            if (any) ++p.report[size_t(Feature::Pickup)].targetsWritten;
        }
        feature(Feature::Super, {pc::SUPER}, m.ability);
        feature(Feature::Grenade, {pc::GRENADE}, m.ability);
        feature(Feature::Melee, {pc::MELEE}, m.ability);
        if (!b.group.writes.empty()) p.groups.push_back(std::move(b.group));
    } else if (playerFeatures) {
        for (Feature f : {Feature::God, Feature::Interact, Feature::Pickup, Feature::Super, Feature::Grenade, Feature::Melee})
            if (on(f)) p.report[size_t(f)].refused.push_back("the player component could not be resolved");
    }
     
    bool weaponFeatures = false;
    for (size_t i = size_t(Feature::Handling); i < kFeatureCount; ++i) weaponFeatures |= enabled[i];
    if (!weaponFeatures) return p;
    for (const Target& w : t.weapons) {
        Builder b(p, w);
        auto feature = [&](Feature f, std::initializer_list<uint32_t> offsets, uint32_t bits) {
            if (!on(f)) return;
            ++p.report[size_t(f)].targets;
            bool any = false;
            for (uint32_t off : offsets) any |= b.record(f, off, bits);
            if (any) ++p.report[size_t(f)].targetsWritten;
        };
        const uint32_t zero = world::floatBits(0.0f);
        feature(Feature::Handling, {wfc::HANDLING[0], wfc::HANDLING[1], wfc::HANDLING[2]}, zero);
        feature(Feature::ZoomMult, {wfc::ZOOM_MULT}, zero);
        feature(Feature::ZoomSpeed, {wfc::ZOOM_SPEED}, zero);
        feature(Feature::Magazine, {wfc::MAGAZINE[0], wfc::MAGAZINE[1]}, world::floatBits(m.magazine));
        feature(Feature::RateOfFire, {wfc::RPM[0], wfc::RPM[1], wfc::RPM[2], wfc::RPM[3]}, world::floatBits(m.rpm));
        feature(Feature::Damage, {wfc::DAMAGE}, world::floatBits(kDamageValue));
        feature(Feature::Spread, {wfc::SPREAD}, zero);
        if (on(Feature::Recoil)) {
            ++p.report[size_t(Feature::Recoil)].targets;
            bool any = false;
            for (uint32_t k = 0; k < wfc::RECOIL_COUNT; ++k) any |= b.record(Feature::Recoil, wfc::RECOIL0 + k * kRecordStride, zero);
            if (any) ++p.report[size_t(Feature::Recoil)].targetsWritten;
        }
        if (on(Feature::Ammo)) {
            FeatureReport& rep = p.report[size_t(Feature::Ammo)];
            ++rep.targets;
            const Located l = locateAmmo(w.view);
            if (!l.ok) {
                rep.refused.push_back(w.chain.label() + ": " + l.why);
            } else if (l.via == Via::AmmoPair) {
                b.add(Feature::Ammo, l.addr, m.ammo, l.record.value,
                      {{w.chain.data + wfc::AMMO_TAG_OFFSET, 0xFFFFFFFFu, wfc::AMMO_TAG, "ammo pair tag 80802449"}}, "clip ammo +1928 (ammo pair)");
                b.anyLocated = true;
                ++rep.targetsWritten;
            } else {
                b.recordAt(Feature::Ammo, l, wfc::AMMO, m.ammo);
                ++rep.targetsWritten;
            }
        }
        if (on(Feature::FullAuto)) {
            FeatureReport& rep = p.report[size_t(Feature::FullAuto)];
            ++rep.targets;
            const Located l = locateFullAuto(w.view);
            if (!l.ok) {
                rep.refused.push_back(w.chain.label() + ": " + l.why);
            } else {
                 
                b.add(Feature::FullAuto, l.addr, 0, l.record.value, {{l.addr, 0xFFFFFFFFu, wfc::SEMI_AUTO, "fire mode reads 01010000"}},
                      std::string("fire mode +980 (") + viaName(l.via) + ")");
                b.anyLocated = true;
                ++rep.targetsWritten;
            }
        }
        if (!b.group.writes.empty()) p.groups.push_back(std::move(b.group));
    }
    return p;
}

 

void WriteJournal::note(const PlannedWrite& w) {
    for (JournalEntry& e : entries_)
        if (e.addr == w.addr && e.chain.sameAs(w.chain)) {
            if (e.applied != w.applied) {
                if (e.applied != e.original && std::find(e.earlier.begin(), e.earlier.end(), e.applied) == e.earlier.end())
                    e.earlier.push_back(e.applied);
                if (e.earlier.size() > 16) e.earlier.erase(e.earlier.begin());
                e.applied = w.applied;
            }
            e.feature = w.feature;
            return;
        }
    if (w.original == w.applied) return;    
    JournalEntry e;
    e.feature = w.feature;
    e.chain = w.chain;
    e.addr = w.addr;
    e.original = w.original;
    e.applied = w.applied;
    e.field = w.field;
    entries_.push_back(std::move(e));
}

std::vector<JournalEntry> WriteJournal::take(Feature f) {
    std::vector<JournalEntry> out, keep;
    for (JournalEntry& e : entries_) (e.feature == f ? out : keep).push_back(std::move(e));
    entries_ = std::move(keep);
    return out;
}

std::vector<JournalEntry> WriteJournal::takeAll() {
    std::vector<JournalEntry> out = std::move(entries_);
    entries_.clear();
    return out;
}

const char* restoreResultName(RestoreResult r) {
    switch (r) {
    case RestoreResult::Restored: return "restored";
    case RestoreResult::AlreadyOriginal: return "already original";
    case RestoreResult::Changed: return "changed by the game (left as is)";
    case RestoreResult::Gone: return "object gone";
    }
    return "?";
}

RestoreResult restore(world::Reader& mem, const WordWriter& write, const JournalEntry& e, bool chainChecked) {
    if (!chainChecked && !chainCurrent(mem, e.chain)) return RestoreResult::Gone;
    const auto cur = mem.tryBytes(e.addr, 4);
    if (!cur) return RestoreResult::Gone;
    const uint32_t v = cur->u32(0);
    if (v == e.original) return RestoreResult::AlreadyOriginal;
    if (!e.ours(v)) return RestoreResult::Changed;               
    write(e.addr, e.original);
    return RestoreResult::Restored;
}

RestoreTally restoreEntries(world::Reader& mem, const WordWriter& write, const std::vector<JournalEntry>& entries) {
     
     
    RestoreTally t;
    std::vector<std::pair<const ComponentChain*, bool>> checked;
    for (const JournalEntry& e : entries) {
        bool ok = false, found = false;
        for (const auto& [c, r] : checked)
            if (c->sameAs(e.chain) && c->sobjPoolBase == e.chain.sobjPoolBase && c->accPoolBase == e.chain.accPoolBase) {
                ok = r;
                found = true;
                break;
            }
        if (!found) {
            ok = chainCurrent(mem, e.chain);
            checked.push_back({&e.chain, ok});
        }
        RestoreResult r = RestoreResult::Gone;
        if (ok) {
            try {
                r = restore(mem, write, e, true);
            } catch (const std::exception&) {
                r = RestoreResult::Gone;
            }
        }
        switch (r) {
        case RestoreResult::Restored: ++t.restored; break;
        case RestoreResult::AlreadyOriginal: ++t.already; break;
        case RestoreResult::Changed: ++t.changed; break;
        case RestoreResult::Gone: ++t.gone; break;
        }
    }
    return t;
}

 

namespace {

std::mutex g_boardMutex;
Board g_board;

}   

Board boardSnapshot() {
    std::lock_guard<std::mutex> lock(g_boardMutex);
    return g_board;
}

void resetBoard() {
    std::lock_guard<std::mutex> lock(g_boardMutex);
    g_board = Board{};
}

void publishBoard(Board b) {
    std::lock_guard<std::mutex> lock(g_boardMutex);
    g_board = std::move(b);
}

 

void WriteManager::setFeature(Runtime& rt, world::Reader& mem, Feature f, bool on, const Log& log) {
    std::lock_guard<std::recursive_mutex> lock(m_);
    const bool was = enabled_[size_t(f)];
    enabled_[size_t(f)] = on;
    if (!on && !was) {
        publish(nullptr);
        return;
    }
    if (on) {
        try {
            cache_.invalidate();
            sync(rt, mem, true, log);                            
        } catch (...) {
            enabled_[size_t(f)] = was;                           
            try {
                sync(rt, mem, false, log);
            } catch (const std::exception&) {
            }
            publish(nullptr);
            throw;
        }
    } else {
         
         
         
         
        bool stopped = true;
        try {
            sync(rt, mem, false, log);
        } catch (const std::exception& e) {
            stopped = false;
            lastError_ = e.what();
            if (log) log(std::string("[player] ") + featureName(f) + " off: " + e.what() + " (originals kept until the writes stop)");
        }
        if (stopped) {
            try {
                restoreFeature(rt, mem, f, log);
            } catch (const std::exception& e) {
                if (log) log(std::string("[player] ") + featureName(f) + " off, restoring: " + e.what());
            }
        }
    }
    publish(nullptr);
}

void WriteManager::setMagnitudes(Runtime& rt, world::Reader& mem, const Magnitudes& m, const Log& log) {
    std::lock_guard<std::recursive_mutex> lock(m_);
    if (m == mags_) return;
    mags_ = m;
    if (anyOf(enabled_)) sync(rt, mem, false, log);
    publish(nullptr);
}

Magnitudes WriteManager::magnitudes() const {
    std::lock_guard<std::recursive_mutex> lock(m_);
    return mags_;
}

bool WriteManager::anyOn() const {
    std::lock_guard<std::recursive_mutex> lock(m_);
    return anyOf(enabled_);
}

FeatureSet WriteManager::enabled() const {
    std::lock_guard<std::recursive_mutex> lock(m_);
    return enabled_;
}

size_t WriteManager::journalSize() const {
    std::lock_guard<std::recursive_mutex> lock(m_);
    return journal_.size();
}

std::vector<std::string> WriteManager::describe(Runtime& rt) {
    std::lock_guard<std::recursive_mutex> lock(m_);
    std::vector<std::string> out;
    char buf[256];
    if (!anyOf(enabled_)) {
        out.push_back("no switch is on: the write list is not running");
        return out;
    }
    gwl::TableStats st;
    try {
        st = list_.stats(rt);
    } catch (const std::exception& e) {
        out.push_back(std::string("could not read the table: ") + e.what());
    }
    std::snprintf(buf, sizeof buf, "task %s, table %s, %zu group(s), %u frame(s) run, %zu journalled original word(s)", list_.taskName().c_str(),
                  hex8(list_.block()).c_str(), plan_.groups.size(), st.frames, journal_.size());
    out.push_back(buf);
    for (size_t g = 0; g < plan_.groups.size(); ++g) {
        const gwl::Group& grp = plan_.groups[g];
        const gwl::GroupStats* gs = g < st.groups.size() ? &st.groups[g] : nullptr;
        std::snprintf(buf, sizeof buf, "  %s: %zu guards, %zu writes, passes %u, last frame %s", grp.name.c_str(), grp.guards.size(), grp.writes.size(),
                      gs ? gs->passes : 0u,
                      !gs ? "?" : gs->failed == 0 ? "all guards held" : ("guard " + std::to_string(gs->failed - 1) + " failed").c_str());
        out.push_back(buf);
        if (gs && gs->failed && gs->failed - 1 < grp.guards.size()) {
            const gwl::Guard& gd = grp.guards[gs->failed - 1];
            std::snprintf(buf, sizeof buf, "    failed guard: %s ([%s] & %s == %s)", gd.what.c_str(), hex8(gd.addr).c_str(), hex8(gd.mask).c_str(),
                          hex8(gd.value).c_str());
            out.push_back(buf);
        }
        for (size_t w = 0; w < grp.writes.size(); ++w) {
            const gwl::Write& wr = grp.writes[w];
            const uint32_t hits = gs && w < gs->hits.size() ? gs->hits[w] : 0;
            std::snprintf(buf, sizeof buf, "    %s <- %s  %s  hits %u", hex8(wr.addr).c_str(), hex8(wr.value).c_str(), wr.what.c_str(), hits);
            out.push_back(buf);
        }
    }
    return out;
}

void WriteManager::sync(Runtime& rt, world::Reader& mem, bool resolve, const Log& log) {
    if (!anyOf(enabled_)) {
        list_.stop(rt);
        plan_ = Plan{};
        lastHits_.clear();
        return;
    }
    bool needWeapons = false;
    for (size_t i = size_t(Feature::Handling); i < kFeatureCount; ++i) needWeapons |= enabled_[i];
    if (resolve || !haveTargets_ || (needWeapons && !targets_.weaponsScanned)) {
        try {
            targets_ = resolveTargets(mem, needWeapons, &cache_);
            lastError_.clear();
        } catch (const std::exception& e) {
            targets_ = Targets{};
            targets_.note = e.what();
            lastError_ = e.what();
        }
        haveTargets_ = true;
        resolvedAt_ = Clock::now();
        failing_ = false;
    }
    plan_ = plan(targets_, enabled_, mags_);
    for (const PlannedWrite& w : plan_.writes) journal_.note(w);    
    try {
        list_.upload(rt, plan_.groups);
    } catch (const std::exception& e) {
        lastError_ = std::string("console table: ") + e.what();
        throw;
    }
    lastHits_.assign(plan_.writes.size(), 0);
    if (log && resolve && targets_.havePlayer) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "[player] writes: player %s, %zu weapon(s), %zu word(s) per frame", hex8(targets_.playerHandle).c_str(),
                      targets_.weapons.size(), plan_.writes.size());
        log(buf);
    }
}

void WriteManager::restoreFeature(Runtime& rt, world::Reader& mem, Feature f, const Log& log) {
    auto entries = journal_.take(f);
    if (entries.empty()) return;
    rt.waitFrames(2, 1000);                                      
    const RestoreTally t = restoreEntries(mem, [&rt](uint32_t a, uint32_t v) { rt.writeU32(a, v, true); }, entries);
    if (log) {
        char buf[200];
        std::snprintf(buf, sizeof buf, "[player] %s off: %d original word(s) restored%s", featureName(f), t.restored,
                      t.left() ? (", " + std::to_string(t.left()) + " left (" + std::to_string(t.gone) + " object gone, " + std::to_string(t.changed) +
                                  " changed by the game)")
                                     .c_str()
                               : "");
        log(buf);
    }
}

void WriteManager::tick(Runtime& rt, world::Reader& mem, const Log& log) {
    std::lock_guard<std::recursive_mutex> lock(m_);
    if (!anyOf(enabled_)) return;
    gwl::TableStats st;
    bool haveStats = false;
    if (list_.running(rt) && !list_.groups().empty()) {
        try {
            st = list_.stats(rt);
            haveStats = st.valid;
        } catch (const std::exception&) {
        }
    }
    bool broken = false;
    landing_.fill(false);
    if (haveStats) {
        for (const gwl::GroupStats& g : st.groups) broken |= g.failed != 0;
        for (size_t i = 0; i < plan_.writes.size(); ++i) {
            const PlannedWrite& w = plan_.writes[i];
            if (w.group >= st.groups.size() || w.write >= st.groups[w.group].hits.size()) continue;
            const uint32_t hits = st.groups[w.group].hits[w.write];
            if (i < lastHits_.size()) {
                if (hits != lastHits_[i]) landing_[size_t(w.feature)] = true;
                lastHits_[i] = hits;
            }
        }
    }
    const auto now = Clock::now();
    if (broken && !failing_) failingSince_ = now;
    failing_ = broken;
    if (!broken && haveStats) brokenStreak_ = 0;
    const auto since = now - resolvedAt_;
    bool needWeapons = false;
    for (size_t i = size_t(Feature::Handling); i < kFeatureCount; ++i) needWeapons |= enabled_[i];
    const bool waiting = !targets_.havePlayer || (needWeapons && targets_.weapons.empty());
     
     
    const auto backoff = std::chrono::seconds(std::min(20, 2 << std::min(brokenStreak_, 3)));
    bool resolve = false;
    if (broken && now - failingSince_ >= std::chrono::milliseconds(800) && since >= backoff) {
        resolve = true;
        ++brokenStreak_;
    }
    if (waiting && since >= std::chrono::seconds(2)) resolve = true;
    if (list_.dirty() && since >= std::chrono::seconds(2)) resolve = true;    
    if (since >= std::chrono::seconds(20)) resolve = true;
    if (resolve) {
        cache_.invalidate();
        sync(rt, mem, true, log);
    }
    publish(haveStats ? &st : nullptr);
}

void WriteManager::shutdown(Runtime& rt, world::Reader& mem, const Log& log) {
    std::lock_guard<std::recursive_mutex> lock(m_);
    enabled_.fill(false);
    try {
        list_.stop(rt);
    } catch (const std::exception&) {
    }
    auto entries = journal_.takeAll();
    if (!entries.empty()) {
        try {
            rt.waitFrames(2, 1000);
        } catch (const std::exception&) {
        }
        const RestoreTally t = restoreEntries(mem, [&rt](uint32_t a, uint32_t v) { rt.writeU32(a, v, true); }, entries);
        if (log)
            log("[player] writes stopped: " + std::to_string(t.restored) + " original word(s) restored" +
                (t.left() ? ", " + std::to_string(t.left()) + " left (object gone or changed by the game)" : std::string()));
    }
    plan_ = Plan{};
    targets_ = Targets{};
    haveTargets_ = false;
    lastHits_.clear();
    landing_.fill(false);
    publish(nullptr);
}

void WriteManager::publish(const gwl::TableStats* stats) {
    Board b;
    b.running = !plan_.groups.empty() && !list_.dirty();
    b.frames = stats ? stats->frames : 0;
    b.playerHandle = targets_.havePlayer ? targets_.playerHandle : 0;
    b.weapons = int(targets_.weapons.size());
    b.problems = targets_.problems;
    b.lastError = lastError_;
    for (size_t i = 0; i < kFeatureCount; ++i) {
        const Feature f = Feature(i);
        FeatureState& s = b.features[i];
        if (!enabled_[i]) continue;
        const FeatureReport& rep = plan_.report[i];
        s.writes = rep.writes;
        s.landing = landing_[i];
        if (!haveTargets_ || !targets_.havePlayer) {
            s.level = FeatureState::Waiting;
            s.text = targets_.note.empty() ? "Waiting for a spawned player" : targets_.note;
        } else if (weaponFeature(f) && targets_.weapons.empty() && targets_.weaponsRefused) {
            s.level = FeatureState::Refused;
            s.text = "No usable fire component (" + std::to_string(targets_.weaponsRefused) + " weapon" +
                     (targets_.weaponsRefused == 1 ? "" : "s") + " skipped): " + targets_.weaponProblem;
        } else if (weaponFeature(f) && targets_.weapons.empty()) {
            s.level = FeatureState::Waiting;
            s.text = "Waiting for an equipped weapon";
        } else if (rep.writes && list_.dirty()) {
            s.level = FeatureState::Waiting;
            s.text = "The console table did not take the last change: retrying" + (lastError_.empty() ? std::string() : " (" + lastError_ + ")");
        } else if (rep.writes == 0) {
            s.level = FeatureState::Refused;
            s.text = rep.refused.empty() ? std::string("No matching field on this build") : "No matching record: " + rep.refused.front();
        } else if (rep.targetsWritten < rep.targets) {
            s.level = FeatureState::Partial;
            s.text = "Writing " + words(rep.writes) + " on " + std::to_string(rep.targetsWritten) + " of " + std::to_string(rep.targets) +
                     " weapons (" + (rep.refused.empty() ? std::string("no record on the others") : rep.refused.front()) + ")";
        } else {
            s.level = FeatureState::Active;
            s.text = weaponFeature(f) ? "Writing " + words(rep.writes) + " on " + std::to_string(rep.targetsWritten) +
                                            (rep.targetsWritten == 1 ? " weapon" : " weapons")
                                      : "Writing " + words(rep.writes) + " every frame";
        }
    }
    b.updated = steadySeconds();
    publishBoard(std::move(b));
}

}   
