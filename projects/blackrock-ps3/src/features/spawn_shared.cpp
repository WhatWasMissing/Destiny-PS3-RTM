#include "features/spawn_shared.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "app/engine.h"
#include "app/settings.h"
#include "core/error.h"
#include "core/runtime.h"
#include "features/cmd_ctx.h"
#include "game/names_catalog.h"
#include "game/world_mem.h"
#include "game/world_player.h"
#include "game/world_tags.h"

namespace brps3::spawnui {

namespace {

constexpr const char* kKeySelected = "spawn.selected";
constexpr const char* kKeyFavourites = "spawn.favorites";
constexpr const char* kKeyRecent = "spawn.recent";
constexpr const char* kDot = " \xC2\xB7 ";

 
std::mutex g_autoMutex;
spawn::wtl::AutoLoad g_auto;

Settings& settings() { return Engine::get().settings(); }

std::vector<uint32_t> tagList(const std::string& key) {
    std::vector<uint32_t> out;
    for (const std::string& s : settings().list(key)) {
        uint32_t t = 0;
        if (spawn::parseTag(s, t)) out.push_back(t);
    }
    return out;
}

std::string fmt(const char* f, ...) {
    char buf[512];
    va_list a;
    va_start(a, f);
    std::vsnprintf(buf, sizeof buf, f, a);
    va_end(a);
    return buf;
}

void loadPlacementLocked(State& s) {
    if (s.placeLoaded) return;
    s.placeLoaded = true;
    Settings& c = settings();
    spawn::Placement p;
    p.mode = spawn::Mode(c.integer("spawn.place.mode", 0));
    p.distance = c.number("spawn.place.distance", 6.0f);
    p.screenX = c.number("spawn.place.sx", 0.5f);
    p.screenY = c.number("spawn.place.sy", 0.5f);
    p.offset = {c.number("spawn.place.ox", 0), c.number("spawn.place.oy", 0), c.number("spawn.place.oz", 0)};
    p.coords = {c.number("spawn.place.cx", 0), c.number("spawn.place.cy", 0), c.number("spawn.place.cz", 0)};
    p.count = c.integer("spawn.place.count", 1);
    p.spacing = c.number("spawn.place.spacing", 2.0f);
    p.scale = c.number("spawn.place.scale", 1.0f);
    p.facing = spawn::Facing(c.integer("spawn.place.facing", 0));
    p.source = spawn::PlayerSource(c.integer("spawn.place.source", 0));
    s.place = spawn::clamped(p);
}

uint32_t localPlayerHandle(Ctx& c, world::Reader& mem) {
    const auto p = c.state<world::PlayerCache>("world.player").get(mem, 500, true);
    return p ? p->handle : 0;
}

}   

State& st() {
    static State s;
    return s;
}

double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

bool demoMode() {
    static const bool on = [] {
        char* v = nullptr;
        size_t n = 0;
        const bool r = _dupenv_s(&v, &n, "BRPS3_UI_DEMO") == 0 && v && v[0] == '1';
        free(v);
        return r;
    }();
    return on;
}

 

spawn::Placement placement() {
    State& s = st();
    std::lock_guard<std::mutex> lock(s.m);
    loadPlacementLocked(s);
    return s.place;
}

void setPlacement(const spawn::Placement& in) {
    const spawn::Placement p = spawn::clamped(in);
    State& s = st();
    {
        std::lock_guard<std::mutex> lock(s.m);
        s.place = p;
        s.placeLoaded = true;
    }
    if (demoMode()) return;
    Settings& c = settings();
    c.set("spawn.place.mode", int(p.mode));
    c.set("spawn.place.distance", p.distance);
    c.set("spawn.place.sx", p.screenX);
    c.set("spawn.place.sy", p.screenY);
    c.set("spawn.place.ox", p.offset.x);
    c.set("spawn.place.oy", p.offset.y);
    c.set("spawn.place.oz", p.offset.z);
    c.set("spawn.place.cx", p.coords.x);
    c.set("spawn.place.cy", p.coords.y);
    c.set("spawn.place.cz", p.coords.z);
    c.set("spawn.place.count", p.count);
    c.set("spawn.place.spacing", p.spacing);
    c.set("spawn.place.scale", p.scale);
    c.set("spawn.place.facing", int(p.facing));
    c.set("spawn.place.source", int(p.source));
}

uint32_t selectedTag() {
    State& s = st();
    {
        std::lock_guard<std::mutex> lock(s.m);
        if (s.selected) return s.selected;
    }
    uint32_t t = 0;
    if (!spawn::parseTag(settings().str(kKeySelected, ""), t)) t = demoMode() ? 0x80C7A2F0 : 0;
    std::lock_guard<std::mutex> lock(s.m);
    if (!s.selected) s.selected = t;
    return s.selected;
}

void select(uint32_t tag) {
    State& s = st();
    {
        std::lock_guard<std::mutex> lock(s.m);
        if (s.selected == tag) return;
        s.selected = tag;
    }
    if (!demoMode()) settings().set(std::string(kKeySelected), tag ? spawn::tagText(tag) : std::string());
}

namespace {
std::vector<uint32_t>& demoFavourites() {
    static std::vector<uint32_t> f = {0x80C7A2F0, 0x80C7AE44, 0x80C883B0};
    return f;
}
}   

bool favourite(uint32_t tag) {
    if (demoMode()) {
        const auto& f = demoFavourites();
        return std::find(f.begin(), f.end(), tag) != f.end();
    }
    return settings().inList(kKeyFavourites, spawn::tagText(tag));
}

void toggleFavourite(uint32_t tag) {
    if (demoMode()) {
        auto& f = demoFavourites();
        auto it = std::find(f.begin(), f.end(), tag);
        if (it == f.end()) f.push_back(tag);
        else f.erase(it);
        return;
    }
    settings().toggleInList(kKeyFavourites, spawn::tagText(tag));
}

std::vector<uint32_t> favourites() { return demoMode() ? demoFavourites() : tagList(kKeyFavourites); }

std::vector<uint32_t> recents() {
    if (demoMode()) return {0x80C7A2F0, 0x8108E107, 0x80CCB26F, 0x80C7AE44};
    return tagList(kKeyRecent);
}

 

int catalogType(uint32_t tag) {
    if (const spawn::CatalogRow* r = spawn::catalog().find(tag)) return r->type;
    if (const names::Entity* e = names::entities().find(tag)) return e->type;
    return -1;
}

std::string location(uint32_t tag) {
    const names::Entity* e = names::entities().find(tag);
    if (!e) return {};
    const std::string loc = e->location(), act = e->activity();
    std::string out = names::locationName(loc);
    if (!out.empty()) out[0] = char(std::toupper(static_cast<unsigned char>(out[0])));
    if (!act.empty()) out += kDot + act;
    return out;
}

std::string knownName(uint32_t tag) {
    std::string n = names::tagName(tag);
    if (!n.empty()) return n;
    static const std::map<uint32_t, std::string> openerNames = [] {
        std::map<uint32_t, std::string> m;
        for (const spawn::Opener& o : spawn::openers())
            if (o.tags.size() <= 4)              
                for (uint32_t t : o.tags) m.emplace(t, o.name);
        return m;
    }();
    auto it = openerNames.find(tag);
    return it != openerNames.end() ? it->second : std::string();
}

std::string unnamedLabel(uint32_t tag) {
    const int type = catalogType(tag);
    std::string cat = type >= 0 ? spawn::categoryName(uint8_t(type)) : "entity";
    for (char& ch : cat) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return "Unnamed " + cat;
}

std::string displayName(uint32_t tag) {
    const std::string n = knownName(tag);
    return n.empty() ? unnamedLabel(tag) + kDot + spawn::tagText(tag) : n;
}

std::string shortName(uint32_t tag) {
    std::string n = displayName(tag);
    if (n.size() > 30) n = n.substr(0, 28) + "..";
    return n;
}

std::string vec3Text(const Vec3& v, int decimals) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "%.*f, %.*f, %.*f", decimals, double(v.x), decimals, double(v.y), decimals, double(v.z));
    return buf;
}

 

void requireReaders(Ctx& c) { c.require(world::kFingerprints, "World readers"); }

Vec3 readPlayerPosition(Ctx& c, spawn::PlayerSource source) {
    requireReaders(c);
    world::RuntimeReader mem(c.rt());
    const world::LocalPlayer p = c.state<world::PlayerCache>("world.player").require(mem, 500, true);
    const Vec3 v = source == spawn::PlayerSource::Engine ? world::readEnginePosition(mem, p) : world::readPosition(mem, p);
    if (!v.finite() || v == Vec3{}) throw Error("The player position is not readable right now (loading?)");
    return v;
}

std::optional<spawn::CameraPose> readCamera(Ctx& c, float sx, float sy) {
    world::RuntimeReader mem(c.rt());
    const auto b = mem.tryBytes(spawn::kViewMatrix, spawn::kCameraBytes);
    if (!b) return std::nullopt;
    spawn::CameraPose pose;
    if (!spawn::cameraFromMatrices(spawn::parseMatrices(*b), sx, sy, pose)) return std::nullopt;
    return pose;
}

void runSpawn(Ctx& c, uint32_t tag, const spawn::Placement& in, const std::string& label) {
     
     
    cmd::Handler& handler = cmdHandler(c);
    const spawn::Placement place = spawn::clamped(in);
    const uint32_t ps3 = names::toPs3Tag(tag);
    State& s = st();
    {
        std::lock_guard<std::mutex> lock(s.m);
        s.progress = "Spawning " + spawn::tagText(tag) + " in game...";
    }
    struct ClearProgress {
        ~ClearProgress() {
            std::lock_guard<std::mutex> lock(st().m);
            st().progress.clear();
        }
    } clearProgress;
    auto fail = [&](const std::string& why) {
        std::lock_guard<std::mutex> lock(s.m);
        s.lastSpawn = label + ": " + why;
        s.lastSpawnOk = false;
    };
    if (place.mode == spawn::Mode::Crosshair && (place.screenX != 0.5f || place.screenY != 0.5f))
        c.log("[spawn] note: the in-game spawn aims at the screen centre (the reticle offset is not used)");
     
    c.log(fmt("[spawn] posting %dx %s (%s) %s to the in-game handler", place.count, spawn::tagText(tag).c_str(), label.c_str(),
              spawn::summary(place).c_str()));
    spawn::InGameReport report;
    std::vector<spawn::SpawnResult> results;
    try {
        results = spawn::spawnInGame(
            c.rt(), handler, ps3, catalogType(tag), place, [&c] { return c.cancelled(); },
            [&s](size_t done, size_t total) {
                std::lock_guard<std::mutex> lock(s.m);
                s.progress = "Spawned " + std::to_string(done) + "/" + std::to_string(total);
            },
            &report);
    } catch (const std::exception& e) {
        fail(e.what());
        throw;
    }
    if (report.type && spawn::riskyType(report.type))
        c.log(std::string("[spawn] warning: type ") + spawn::categoryName(report.type) + " has no standalone form: the spawn may do nothing");
    size_t ok = 0;
    std::string firstError;
    {
        std::lock_guard<std::mutex> lock(s.m);
        for (const spawn::SpawnResult& r : results) {
            if (r.ok()) {
                ++ok;
                s.spawned.push_back({r.handle, tag, report.type, r.point.position, label, now(), false});
                s.tagCache[r.handle] = tag;
            } else if (firstError.empty()) {
                firstError = r.reason;
            }
        }
    }
    for (const spawn::SpawnResult& r : results)
        c.log(r.ok() ? fmt("[spawn] %s -> handle %s at %s", spawn::tagText(tag).c_str(), world::hex8(r.handle).c_str(), vec3Text(r.point.position, 2).c_str())
                     : "[spawn] " + spawn::tagText(tag) + " failed: " + r.reason);
    if (!report.stopped.empty()) c.log("[spawn] " + spawn::tagText(tag) + " stopped after " + std::to_string(results.size()) + ": " + report.stopped);
    c.settings().pushRecent(kKeyRecent, spawn::tagText(tag), 24);
     
    const size_t want = report.requested ? report.requested : size_t(place.count);
    const std::string why = !firstError.empty() ? firstError : !report.stopped.empty() ? report.stopped : std::string("stopped");
    const std::string summary = results.empty() ? label + ": " + (report.stopped.empty() ? std::string("stopped before spawning") : report.stopped)
                                : ok == want    ? (ok == 1 ? "Spawned " + label : fmt("Spawned %zux %s", ok, label.c_str()))
                                                : fmt("Spawned %zu of %zu %s: %s", ok, want, label.c_str(), why.c_str());
    {
        std::lock_guard<std::mutex> lock(s.m);
        s.lastSpawn = summary;
        s.lastSpawnOk = ok == want;
    }
    if (ok == 0) throw Error(label + ": " + (!firstError.empty() ? firstError : !report.stopped.empty() ? report.stopped : std::string("stopped before spawning")));
    c.toast(summary);
}

void spawnSelected(Ctx& c, std::optional<spawn::Mode> mode) {
    const uint32_t tag = selectedTag();
    if (!tag) throw Error("Select an entity on the Spawner page first");
    spawn::Placement p = placement();
    if (mode) p.mode = *mode;
    runSpawn(c, tag, p, displayName(tag));
}

void runDespawn(Ctx& c, const std::vector<uint32_t>& handles) {
    if (handles.empty()) throw Error("Nothing to despawn");
    requireReaders(c);
    c.require(spawn::kDespawnFingerprints, "Despawn (sobject_delete)");
    world::RuntimeReader mem(c.rt());
    const auto res = spawn::despawn(c.rt(), mem, handles);
    size_t gone = 0, refused = 0;
    std::string firstError;
    State& s = st();
    {
        std::lock_guard<std::mutex> lock(s.m);
        for (const spawn::DespawnResult& d : res) {
            if (d.gone) ++gone;
            if (!d.called) ++refused;
            if (!d.gone && firstError.empty()) firstError = d.reason;
            if (!d.gone) continue;
            for (SpawnedRow& r : s.spawned)
                if (r.handle == d.handle) r.gone = true;
            s.objects.erase(std::remove_if(s.objects.begin(), s.objects.end(), [&](const LiveRow& o) { return o.o.handle == d.handle; }),
                            s.objects.end());
        }
    }
    for (const spawn::DespawnResult& d : res)
        c.log("[despawn] " + world::hex8(d.handle) + ": " + (d.called ? (d.gone ? "despawned" : "called, still there: " + d.reason) : "refused: " + d.reason));
    if (gone == 0) throw Error("Nothing despawned: " + firstError);
    c.toast(gone == res.size() ? (gone == 1 ? std::string("Despawned 1 object") : fmt("Despawned %zu objects", gone))
                               : fmt("Despawned %zu of %zu (%zu already gone or refused)", gone, res.size(), refused));
}

void despawnLast(Ctx& c) {
    uint32_t h = 0;
    {
        State& s = st();
        std::lock_guard<std::mutex> lock(s.m);
        for (auto it = s.spawned.rbegin(); it != s.spawned.rend() && !h; ++it)
            if (!it->gone) h = it->handle;
    }
    if (!h) throw Error("Nothing spawned this session is still there");
    runDespawn(c, {h});
}

void despawnMine(Ctx& c) {
    std::vector<uint32_t> hs;
    {
        State& s = st();
        std::lock_guard<std::mutex> lock(s.m);
        for (const SpawnedRow& r : s.spawned)
            if (!r.gone) hs.push_back(r.handle);
    }
    if (hs.empty()) throw Error("Nothing spawned this session is still there");
    runDespawn(c, hs);
}

void checkSpawnedAlive(Ctx& c) {
    requireReaders(c);
    world::RuntimeReader mem(c.rt());
    std::vector<uint32_t> hs;
    State& s = st();
    {
        std::lock_guard<std::mutex> lock(s.m);
        for (const SpawnedRow& r : s.spawned) hs.push_back(r.handle);
    }
    if (hs.empty()) return;
    const world::PoolHeader pool = world::readPool(mem, world::addr::SOBJECT_POOL);
    std::map<uint32_t, bool> alive;
    for (uint32_t h : hs) alive[h] = world::readSObject(mem, pool, h).has_value();
    size_t n = 0;
    {
        std::lock_guard<std::mutex> lock(s.m);
        for (SpawnedRow& r : s.spawned) {
            auto it = alive.find(r.handle);
            if (it != alive.end()) r.gone = !it->second;
            n += r.gone ? 0 : 1;
        }
    }
    c.log(fmt("[spawn] %zu of %zu spawned objects are still live", n, hs.size()));
}

 

namespace {

 
struct PageList {
    std::vector<world::TagPage> content;    
    std::set<uint32_t> live;
    uint32_t registered = 0;
};
PageList readPageList(world::Reader& mem) {
    const world::TagTable t = world::readTagTable(mem);
    PageList l;
    for (const world::TagPage& p : world::readPages(mem, t)) {
        if (!p.live()) continue;
        ++l.registered;
        if (world::isPackagePage(p.page) && p.itemStride == world::kPackageItemStride) {
            l.live.insert(p.page);
            l.content.push_back(p);
        }
    }
    return l;
}

 
 
void applyPagesLocked(State& s, const PageList& l) {
    for (auto it = s.resident.begin(); it != s.resident.end();) it = l.live.count(world::tagPage(*it)) ? std::next(it) : s.resident.erase(it);
    for (auto it = s.scannedPages.begin(); it != s.scannedPages.end();) it = l.live.count(*it) ? std::next(it) : s.scannedPages.erase(it);
    if (l.live != s.livePages) s.checkedOut.clear();
    s.livePages = l.live;
    s.registeredPages = l.registered;
    s.pagesAt = now();
}

}   

void refreshPages(Ctx& c) {
    requireReaders(c);
    world::RuntimeReader mem(c.rt());
    const PageList l = readPageList(mem);
    State& s = st();
    std::lock_guard<std::mutex> lock(s.m);
    applyPagesLocked(s, l);
    ++s.residencyRev;
}

void scanEntries(Ctx& c) {
    requireReaders(c);
    State& s = st();
    if (s.scanning.exchange(true)) throw Error("A scan is already running");
    struct Done {
        ~Done() { st().scanning = false; }
    } done;
    world::RuntimeReader mem(c.rt());
    const PageList l = readPageList(mem);
    {
        std::lock_guard<std::mutex> lock(s.m);
        s.scanDone = 0;
        s.scanTotal = l.content.size();
    }
     
     
    std::set<uint32_t> resident, covered;
    size_t entities = 0;
    for (size_t i = 0; i < l.content.size() && !c.cancelled(); ++i) {
        const world::ResidentPage rp = world::scanResidentPage(mem, l.content[i]);
        covered.insert(l.content[i].page);
        entities += rp.indices.size();
        for (uint32_t idx : rp.indices) resident.insert(names::fromPs3Tag(world::makeTag(l.content[i].page, idx)));
        std::lock_guard<std::mutex> lock(s.m);
        s.scanDone = i + 1;
    }
    const bool complete = covered.size() == l.content.size();
    {
        std::lock_guard<std::mutex> lock(s.m);
        applyPagesLocked(s, l);
        if (complete) {
            s.resident = std::move(resident);
            s.scannedPages = covered;
            s.entriesAt = now();
        } else {
            s.resident.insert(resident.begin(), resident.end());
            s.scannedPages.insert(covered.begin(), covered.end());
        }
        for (uint32_t t : s.resident) s.checkedOut.erase(t);
        ++s.residencyRev;
    }
    c.log(fmt("[residency] %zu loaded entity definitions on %zu of %zu package pages%s", entities, covered.size(), l.content.size(),
              complete ? "" : " (stopped early)"));
}

TagCheck checkTag(Ctx& c, uint32_t tag) {
    requireReaders(c);
    world::RuntimeReader mem(c.rt());
    const world::TagTable t = world::readTagTable(mem);
    const world::Residency r = world::checkResident(mem, t, names::toPs3Tag(tag));
    TagCheck k;
    k.resident = r.resident;
    k.type = r.type;
    k.why = r.why;
    k.element = r.element;
    k.body = r.body;
    k.definition = r.definition;
    k.at = now();
    State& s = st();
    std::lock_guard<std::mutex> lock(s.m);
    s.checks[tag] = k;
    if (r.resident) {
        s.resident.insert(tag);
        s.checkedOut.erase(tag);
        s.livePages.insert(world::tagPage(names::toPs3Tag(tag)));
    } else {
        s.resident.erase(tag);
        s.checkedOut.insert(tag);
    }
    ++s.residencyRev;
    return k;
}

 

void refreshObjects(Ctx& c, bool resolveTags) {
    requireReaders(c);
    world::RuntimeReader mem(c.rt());
    world::ObjectScan scan;
    try {
        scan = world::readObjects(mem);
    } catch (const std::exception& e) {
        State& s = st();
        std::lock_guard<std::mutex> lock(s.m);
        s.objectsError = e.what();
        throw;
    }
    const uint32_t ph = localPlayerHandle(c, mem);
    std::optional<Vec3> player;
    std::set<uint32_t> mine, tried;
    std::map<uint32_t, uint32_t> cache;
    State& s = st();
    {
        std::lock_guard<std::mutex> lock(s.m);
        for (const SpawnedRow& r : s.spawned) mine.insert(r.handle);
        cache = s.tagCache;
        tried = s.tagTried;
    }
    for (const world::SObject& o : scan.objects)
        if (o.handle == ph && o.position.finite()) player = o.position;
    std::vector<LiveRow> rows;
    for (const world::SObject& o : scan.objects) {
        if (o.deleting()) continue;
        LiveRow r;
        r.o = o;
        r.mine = mine.count(o.handle) != 0;
        if (auto it = cache.find(o.handle); it != cache.end()) r.tag = it->second;
        if (player && o.position.finite()) r.dist = world::distance(*player, o.position);
        rows.push_back(r);
    }
    if (resolveTags) {
         
        std::vector<world::SObject> missing;
        std::vector<const LiveRow*> order;
        for (const LiveRow& r : rows) order.push_back(&r);
        std::stable_sort(order.begin(), order.end(), [](const LiveRow* a, const LiveRow* b) {
            const float da = a->dist < 0 ? 1e30f : a->dist, db = b->dist < 0 ? 1e30f : b->dist;
            return da < db;
        });
        for (const LiveRow* r : order)
            if (!r->tag && world::validHandle(r->o.componentHead) && !tried.count(r->o.handle)) missing.push_back(r->o);
        for (const world::SObject& o : missing) tried.insert(o.handle);
        if (!missing.empty()) {
            try {
                const world::TagTable t = world::readTagTable(mem);
                const auto tags = spawn::tagsOfObjects(mem, t, missing, 0x100000);
                for (LiveRow& r : rows)
                    if (auto it = tags.find(r.o.handle); it != tags.end()) r.tag = names::fromPs3Tag(it->second);
            } catch (const std::exception& e) {
                c.log(std::string("[objects] tags not resolved: ") + e.what());
            }
        }
    }
    std::map<uint32_t, uint32_t> keep;
    for (const LiveRow& r : rows)
        if (r.tag) keep[r.o.handle] = r.tag;
    std::lock_guard<std::mutex> lock(s.m);
    s.objects = std::move(rows);
    s.pool = scan.pool;
    s.objectsAt = now();
    s.objectsError.clear();
    s.player = player;
    s.playerHandle = ph;
    for (const auto& [h, t] : keep) s.tagCache[h] = t;
    std::set<uint32_t> present;
    for (const LiveRow& r : s.objects) present.insert(r.o.handle);
    s.tagTried.clear();
    for (uint32_t h : tried)
        if (present.count(h)) s.tagTried.insert(h);
     
    for (auto it = s.tagCache.begin(); it != s.tagCache.end();)
        it = keep.count(it->first) || mine.count(it->first) ? std::next(it) : s.tagCache.erase(it);
}

spawn::MoveResult moveObject(Ctx& c, uint32_t handle, const Vec3& to) {
    requireReaders(c);
    world::RuntimeReader mem(c.rt());
    Runtime& rt = c.rt();
    const auto r = spawn::moveObject(mem, [&rt](uint32_t a, const std::vector<uint8_t>& b) { rt.write(a, b.data(), b.size(), false); }, handle, to);
    State& s = st();
    {
        std::lock_guard<std::mutex> lock(s.m);
        for (LiveRow& row : s.objects)
            if (row.o.handle == handle) row.o = r.after;
        for (SpawnedRow& row : s.spawned)
            if (row.handle == handle) row.position = r.after.position;
    }
    c.log(fmt("[objects] moved %s from %s to %s%s%s", world::hex8(handle).c_str(), vec3Text(r.before.position, 2).c_str(), vec3Text(to, 2).c_str(),
              r.wrote40 ? " (+0x40, +0x90, +0xB0)" : " (+0x90, +0xB0)", r.held ? "" : "; the engine moved it back right away"));
    return r;
}

 

void refreshTown(Ctx& c) {
    requireReaders(c);                   
    c.require(spawn::town::kFingerprints, "Town presence");
    world::RuntimeReader mem(c.rt());
    std::string why;
    auto t = spawn::town::read(mem, &why);
    if (t) t->player = localPlayerHandle(c, mem);
    State& s = st();
    std::lock_guard<std::mutex> lock(s.m);
    s.town = t;
    s.townError = t ? std::string() : why;
}

void requestTown(Ctx& c, bool town) {
    c.require(spawn::town::kFingerprints, "Town presence");
    requireReaders(c);
    world::RuntimeReader mem(c.rt());
    State& s = st();
    std::optional<spawn::town::State> expected;
    {
        std::lock_guard<std::mutex> lock(s.m);
        expected = s.town;
    }
    if (!expected) {
        refreshTown(c);
        std::lock_guard<std::mutex> lock(s.m);
        expected = s.town;
        if (!expected) throw Error("Town presence: " + s.townError);
    }
    Runtime& rt = c.rt();
    auto& cache = c.state<world::PlayerCache>("world.player");
    const auto e = spawn::town::request(
        mem, [&rt](uint32_t a, const std::vector<uint8_t>& b) { rt.write(a, b.data(), b.size(), true); },
        [&] {
            cache.invalidate();
            const auto p = cache.get(mem, 0, false);
            return p ? p->handle : 0u;
        },
        *expected, town);
    {
        std::lock_guard<std::mutex> lock(s.m);
        s.townEdits.push_back(e);
    }
     
    c.onPanic("world.town", [](Ctx& pc) { restoreAllTown(pc); });
    int& hooked = c.state<int>("spawn.town.hooked");
    if (!hooked) {
        hooked = 1;
        c.onDisconnect([](Ctx& dc) { restoreAllTown(dc); });
    }
    c.log(std::string("[town] requested ") + (town ? "town" : "normal") + " equipment (latch/refcount, then desired; original " +
          hexBytes(e.original.data(), 8) + ")");
    refreshTown(c);
}

void restoreTown(Ctx& c, size_t index) {
    State& s = st();
    spawn::town::Edit e;
    {
        std::lock_guard<std::mutex> lock(s.m);
        if (index >= s.townEdits.size()) throw Error("That town edit is no longer listed");
        e = s.townEdits[index];
    }
    c.require(spawn::town::kFingerprints, "Town presence");
    world::RuntimeReader mem(c.rt());
    Runtime& rt = c.rt();
    auto& cache = c.state<world::PlayerCache>("world.player");
    spawn::town::restore(
        mem, [&rt](uint32_t a, const std::vector<uint8_t>& b) { rt.write(a, b.data(), b.size(), true); },
        [&] {
            cache.invalidate();
            const auto p = cache.get(mem, 0, false);
            return p ? p->handle : 0u;
        },
        e);
    {
        std::lock_guard<std::mutex> lock(s.m);
        if (index < s.townEdits.size()) s.townEdits.erase(s.townEdits.begin() + std::ptrdiff_t(index));
    }
    c.log("[town] restored the original inputs " + hexBytes(e.original.data(), 8));
    refreshTown(c);
}

void restoreAllTown(Ctx& c) {
    State& s = st();
    for (;;) {
        size_t n;
        {
            std::lock_guard<std::mutex> lock(s.m);
            n = s.townEdits.size();
        }
        if (n == 0) return;
        try {
            restoreTown(c, n - 1);
        } catch (const std::exception& e) {
             
            c.log(std::string("[town] not restored: ") + e.what());
            std::lock_guard<std::mutex> lock(s.m);
            if (!s.townEdits.empty()) s.townEdits.pop_back();
        }
    }
}

 

namespace {

std::string wtlText(const spawn::wtl::State& r, uint32_t tag, bool waited) {
    namespace W = spawn::wtl;
    switch (r.status) {
    case W::Status::Fired:
         
        return fmt("Submitted %s: request %s, handle %s%s", spawn::tagText(tag).c_str(), world::hex8(r.request).c_str(),
                   world::hex8(r.handle).c_str(), waited ? ", loader drained" : "");
    case W::Status::BadTag: return spawn::tagText(tag) + ": its page is not registered here (the package is not loaded at all)";
    case W::Status::NoGlobals: return "The world tag loader is not set up (menus?)";
    case W::Status::CreateFailed: return "The loader did not create a request (pool full?)";
    case W::Status::BadRequest: return "The request address was not plausible: nothing was submitted";
    default: return std::string("Not loaded: ") + W::statusName(r.status);
    }
}

}   

void loadTagNow(Ctx& c, const spawn::wtl::Params& in) {
    requireReaders(c);
    c.require(spawn::wtl::kFingerprints, "World tag load");
    spawn::wtl::Params p = in;
    p.tag = names::toPs3Tag(in.tag);
    c.log(fmt("[wtl] loading %s (%s%s) into the live world", spawn::tagText(p.tag).c_str(), p.kind == spawn::wtl::Kind::Manifest ? "manifest" : "on-demand",
              p.wait ? ", waiting for the loader" : ""));
    const auto r = spawn::wtl::loadNow(c.rt(), p, p.wait ? 30000 : 3000);
    const std::string text = wtlText(r, in.tag, p.wait);
    State& s = st();
    {
        std::lock_guard<std::mutex> lock(s.m);
        s.wtlResult = text;
        s.wtlOk = r.status == spawn::wtl::Status::Fired;
    }
    c.log("[wtl] " + text + (r.status == spawn::wtl::Status::Fired && !p.wait ? " (submitted, not yet loaded: Check the tag in a moment)" : ""));
    if (r.status != spawn::wtl::Status::Fired) throw Error(text);
    c.toast(text);
}

void disarmAutoLoad(Ctx& c) {
    c.stopLoop(kLoopAuto);
    {
        std::lock_guard<std::mutex> lock(g_autoMutex);
        g_auto.stop(c.rt());
    }
    State& s = st();
    std::lock_guard<std::mutex> lock(s.m);
    s.autoLoad.running = false;
}

void armAutoLoad(Ctx& c, const spawn::wtl::Params& in) {
    requireReaders(c);
    c.require(spawn::wtl::kFingerprints, "World tag load");
    spawn::wtl::Params p = in;
    p.tag = names::toPs3Tag(in.tag);
    p.once = true;
    {
        std::lock_guard<std::mutex> lock(g_autoMutex);
        g_auto.start(c.rt(), p);
    }
    State& s = st();
    {
        std::lock_guard<std::mutex> lock(s.m);
        s.autoLoad = AutoStatus{};
        s.autoLoad.running = true;
        s.autoLoad.params = in;
    }
    c.onPanic("spawn.wtl.auto", [](Ctx& pc) { disarmAutoLoad(pc); });
    const std::string what = p.trigger == spawn::wtl::Trigger::Never ? std::string("observing the loader (no load)")
                                                                     : "armed " + spawn::tagText(in.tag) + " (" + spawn::wtl::triggerName(p.trigger) + ")";
    c.log("[wtl] auto-load " + what);
     
    c.startLoop(kLoopAuto, 250, [](Ctx& lc) {
        spawn::wtl::State ps;
        {
            std::lock_guard<std::mutex> lock(g_autoMutex);
            if (!g_auto.block()) {
                lc.stopLoop(kLoopAuto);
                return;
            }
            ps = g_auto.poll(lc.rt());
        }
        State& S = st();
        bool fired = false, failed = false;
        {
            std::lock_guard<std::mutex> lock(S.m);
            AutoStatus& a = S.autoLoad;
            fired = ps.fired > 0 && (!a.havePoll || a.state.fired == 0);
             
             
            failed = !fired && ps.fired == 0 && !ps.armed && a.params.trigger != spawn::wtl::Trigger::Never &&
                     (ps.status == spawn::wtl::Status::CreateFailed || ps.status == spawn::wtl::Status::BadRequest);
            a.state = ps;
            a.havePoll = true;
            if (ps.lastState != 0xFFFFFFFFu) a.statesSeen.insert(ps.lastState);
            a.transitionFramesSeen = ps.transitionFrames;
            if (failed) a.error = std::string("The trigger fired but nothing was loaded: ") + spawn::wtl::statusName(ps.status);
        }
        if (failed) {
            const std::string text = "Auto-load stopped in " + std::string(spawn::wtl::worldStateName(ps.lastState)) + ": " +
                                     spawn::wtl::statusName(ps.status) + " (nothing was submitted)";
            lc.log("[wtl] " + text);
            lc.toast(text);
            {
                std::lock_guard<std::mutex> lock(S.m);
                S.wtlResult = text;
                S.wtlOk = false;
            }
            disarmAutoLoad(lc);
            return;
        }
        if (fired) {
            const std::string text = "Auto-load fired in " + std::string(spawn::wtl::worldStateName(ps.lastState)) + " (" +
                                     std::to_string(ps.lastCount) + " live objects): request " + world::hex8(ps.request);
            lc.log("[wtl] " + text);
            lc.toast(text);
            {
                std::lock_guard<std::mutex> lock(S.m);
                S.wtlResult = text;
                S.wtlOk = true;
            }
            disarmAutoLoad(lc);
        }
    });
}

void runRsc(Ctx& c, uint32_t event, uint32_t tag, bool raw, uint32_t category) {
    requireReaders(c);
    const spawn::rsc::Poster* p = spawn::rsc::findPoster(event);
    if (!p) throw Error("Unknown resourcerer event " + std::to_string(event));
    c.require(spawn::rsc::kFingerprints, "Resourcerer transaction");
    c.require({{p->addr, p->fingerprint}}, p->name);
    const uint32_t ps3 = names::toPs3Tag(tag);
    if (raw)
        c.log(fmt("[rsc] raw post %s(%s, %u) (control: no begin / end; tag checked in the same visit)", p->name, spawn::tagText(ps3).c_str(),
                  category & 0xFFFF));
    else
        c.log(fmt("[rsc] TagHandleValid -> begin(%u) -> %s(%s, req) -> end(req), one game-thread visit", category, p->name,
                  spawn::tagText(ps3).c_str()));
    const auto r = spawn::rsc::run(c.rt(), event, ps3, category, 3000, raw);
    const bool ok = r.status == spawn::rsc::Status::Done;
    std::string text;
    if (!ok) text = std::string("Not posted: ") + spawn::rsc::statusName(r.status);
    else if (raw) text = fmt("Raw %s posted with ctx %04X: r3 %08X (the Xbox control: a bare post does nothing)", p->name, r.request, r.endR3);
    else text = fmt("%s posted in request %04X (end returned %08X)", p->name, r.request, r.endR3);
    State& s = st();
    {
        std::lock_guard<std::mutex> lock(s.m);
        s.rscResult = text;
        s.rscOk = ok;
    }
    c.log("[rsc] " + text);
    if (!ok) throw Error(text);
    c.toast(text);
}

 

void seedDemo() {
    static bool done = false;
    if (done || !demoMode()) return;
    done = true;
    State& s = st();
    std::lock_guard<std::mutex> lock(s.m);
    s.selected = 0x80C7A2F0;
    const double t = now();
    s.spawned = {
        {0x00440014, 0x80C7A2F0, 9, {-412.6f, 118.2f, 23.9f}, "Patches 99-40 'Research Assistant'", t - 312, false},
        {0x00450015, 0x8108E107, 9, {-409.1f, 121.0f, 23.9f}, "Thrall", t - 95, true},
        {0x00460016, 0x80CCB26F, 8, {-405.4f, 116.7f, 24.1f}, "Spinmetal node", t - 22, false},
    };
    s.lastSpawn = "Spawned Spinmetal node";
    s.livePages = {0x4FC, 0x50B, 0x63D, 0x644, 0x6A5, 0x847};
    s.scannedPages = s.livePages;
    s.registeredPages = 234;
    s.pagesAt = t - 40;
    s.resident = {0x80C7A2F0, 0x80C7AE44, 0x80C7AD45, 0x80C883B0, 0x80CCB26F, 0x80C7A2ED, 0x80C7A2EE, 0x80C7A2EF};
    s.entriesAt = t - 38;
    s.checks[0x80C7A2F0] = {true, 9, "", 0x403D2F00, 0x71542AF0, 0x71542AFC, t - 5};
     
    const Vec3 me{-410.0f, 120.0f, 23.9f};
    s.player = me;
    s.playerHandle = 0x00100003;
    struct D {
        uint32_t h;
        uint8_t type;
        Vec3 off;
        uint32_t tag;
    };
    const D rows[] = {{0x00100003, 9, {0, 0, 0}, 0},           {0x00440014, 9, {-2.6f, -1.8f, 0}, 0x80C7A2F0},
                      {0x00460016, 8, {4.6f, -3.3f, 0.2f}, 0x80CCB26F}, {0x00210041, 11, {0.2f, 0.4f, 1.1f}, 0x80A6A07E},
                      {0x00330052, 9, {14.2f, 6.0f, 0.1f}, 0x80C7AE44},  {0x00350071, 9, {22.8f, -9.4f, 2.5f}, 0x80C7AD45},
                      {0x00370099, 12, {-31.0f, 12.0f, -0.4f}, 0},       {0x0039010A, 8, {8.0f, 26.0f, 0.0f}, 0x80C9921A},
                      {0x003B0120, 2, {-6.5f, 18.0f, 0.0f}, 0},          {0x003D0133, 9, {41.0f, 3.0f, 4.0f}, 0x80C883B0},
                      {0x00410150, 13, {-55.0f, -20.0f, 8.0f}, 0},       {0x0043016C, 3, {12.0f, -40.0f, 0.0f}, 0},
                      {0x00450171, 18, {1.5f, 2.0f, 0.0f}, 0},           {0x00470190, 14, {3.0f, 3.0f, 2.0f}, 0},
                      {0x004901A2, 15, {9.0f, 0.5f, 1.4f}, 0},           {0x004B01B7, 1, {-80.0f, 30.0f, 0.0f}, 0}};
    for (const D& d : rows) {
        LiveRow r;
        r.o.handle = d.h;
        r.o.index = d.h & 0x1FFF;
        r.o.addr = 0x60100000 + r.o.index * 0xC0;
        r.o.type = d.type;
        r.o.flags = 0x00200000;
        r.o.position = me + d.off;
        r.o.pos90 = r.o.position;
        r.o.vector40 = r.o.position;
        r.o.componentHead = 0x807AC000 | r.o.index;
        r.o.attachment = d.type == 9 ? 0x00120000 | r.o.index : 0xFFFFFFFF;
        r.o.parent = d.type == 11 ? 0x00100003 : 0xFFFFFFFF;
        r.o.rot2 = r.o.rot;
        r.tag = d.tag;
        r.mine = d.h == 0x00440014 || d.h == 0x00460016;
        r.dist = world::distance(me, r.o.position);
        s.objects.push_back(r);
    }
    s.pool.base = 0x60100000;
    s.pool.stride = 0xC0;
    s.pool.capacity = 1500;
    s.pool.count = 312;
    s.objectsAt = t - 3;
     
    spawn::town::State town;
    town.block = 0x602A11C0;
    town.bytes = {1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0};
    town.player = 0x00100003;
    s.town = town;
     
    s.autoLoad.running = true;
    s.autoLoad.params.tag = 0x80C7A2F0;
    s.autoLoad.params.trigger = spawn::wtl::Trigger::LowObjects;
    s.autoLoad.params.value = 8;
    s.autoLoad.havePoll = true;
    s.autoLoad.state.status = spawn::wtl::Status::Waiting;
    s.autoLoad.state.armed = true;
    s.autoLoad.state.frames = 1843;
    s.autoLoad.state.transitionFrames = 0;
    s.autoLoad.state.lastState = 27;
    s.autoLoad.state.lastCursor = 0xFFFF;
    s.autoLoad.state.lastCount = 37;
    s.autoLoad.state.minCount = 37;
    s.autoLoad.statesSeen = {27};
    s.wtlResult = "Submitted 80C7A2F0: request 6031A4C8, handle 00110003";
    ++s.residencyRev;
}

void resetSession() {
     
     
    const bool ended = !Engine::get().connected();
    State& s = st();
    {
        std::lock_guard<std::mutex> lock(s.m);
        s.autoLoad.running = false;
        s.progress.clear();
        s.townEdits.clear();             
        if (!ended) return;
        s.objects.clear();
        s.objectsAt = 0;
        s.objectsError.clear();
        s.player.reset();
        s.livePages.clear();
        s.resident.clear();
        s.scannedPages.clear();
        s.checkedOut.clear();
        s.pagesAt = s.entriesAt = 0;
        s.checks.clear();
        ++s.residencyRev;
        s.town.reset();
        s.townError.clear();
        for (SpawnedRow& r : s.spawned) r.gone = true;    
    }
    std::lock_guard<std::mutex> alock(g_autoMutex);
    g_auto = spawn::wtl::AutoLoad{};     
}

}   
