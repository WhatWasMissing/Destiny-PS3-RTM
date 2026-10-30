#include "game/spawn_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/error.h"
#include "core/runtime.h"
#include "game/spawn_catalog.h"

namespace brps3::spawn {

using guard::Cmp;
using guard::Cond;
using guard::Width;
using world::hex8;

const Fingerprints& kSpawnFingerprints = world::kSobjectNewFingerprints;
const Fingerprints kDespawnFingerprints = {
    {world::addr::SOBJECT_DELETE, "7C0802A648C59559F821FF41F80100D0"},
};

namespace {

std::string typeText(uint8_t t) {
    char b[64];
    std::snprintf(b, sizeof b, "%u (%s)", unsigned(t), categoryName(t));
    return b;
}

}   

 

Preflight preflight(world::Reader& mem, uint32_t tag, int expectedType) {
    Preflight p;
    p.tag = tag;
    if (!world::validHandle(tag) || !world::isPackagePage(world::tagPage(tag)))
        throw Error("Tag " + hex8(tag) + " is not an entity tag (page " + hex8(world::tagPage(tag)) + " is not a content package)");
    const world::WorldGlobals g = world::readGlobals(mem);
    if (!g.sobjectsOk()) throw Error("The sObject pool is not set up: load into an activity first");
    p.pool = g.sobjects;
    p.sobjectGlobals = g.sobjectGlobals;
    if (!world::mapped(g.sobjectGlobals, kCreationDepthOffset + 4)) throw Error("The sObject globals are not set up: load into an activity first");
    p.creationDepth = mem.u32(g.sobjectGlobals + kCreationDepthOffset);
    if (p.creationDepth >= kMaxCreationDepth)
        throw Error("The game is creating objects right now (creation depth " + std::to_string(p.creationDepth) + "): try again");
    p.table = world::readTagTable(mem);
    p.resolved = world::resolve(mem, p.table, tag, true);
    if (!p.resolved.ok)
        throw Error("Tag " + hex8(tag) + " is not loaded here: " + p.resolved.why + ". Load the activity that uses it, or load it on the World load tab.");
    world::Residency& r = p.residency;
    r.tag = tag;
    r.element = p.resolved.element;
    r.body = p.resolved.body;
    r.classWord = p.resolved.item0;
    if (r.classWord != world::kEntityClass)
        throw Error((r.classWord & 0xFFFF) == 0xFEFE
                        ? "Tag " + hex8(tag) + " is not loaded on the PS3 right now (its tag-table slot is empty)"
                        : "Tag " + hex8(tag) + " is not an entity definition (item class " + hex8(r.classWord) + ")");
    if (!world::mapped(r.body, world::kTagRefOffset + world::kDefTypeOffset + 1))
        throw Error("Tag " + hex8(tag) + ": its definition body " + hex8(r.body) + " is not in mapped memory");
    r.definition = r.body + world::kTagRefOffset;
    r.type = mem.bytes(r.definition + world::kDefTypeOffset, 1).u8(0);
    if (r.type == 0) throw Error("Tag " + hex8(tag) + " resolves to an empty definition (type 0): its package is not loaded");
    r.resident = true;
    p.type = r.type;
    if (p.type == world::TYPE_PROP_GARBAGE) throw Error("Tag " + hex8(tag) + " is a garbage prop: the spawn factory refuses these");
    if (expectedType >= 0 && p.type != uint8_t(expectedType))
        throw Error("Tag " + hex8(tag) + " is type " + typeText(p.type) + " on this console but the catalogue says " + typeText(uint8_t(expectedType)) +
                    ": refusing, the catalogue disagrees with the loaded definition");
    if (riskyType(p.type))
        p.warnings.push_back(std::string("type ") + categoryName(p.type) + " has no standalone form: the spawn may do nothing");
    return p;
}

guard::Request spawnRequest(const Preflight& p, const std::vector<uint8_t>& desc) {
    if (desc.size() != kDescSize) throw Error("spawn descriptor must be 0x80 bytes");
    guard::Request q(world::addr::SOBJECT_NEW);
    q.scratch.assign(kScratchDesc, 0xFF);                        
    q.scratch.insert(q.scratch.end(), desc.begin(), desc.end());
    q.scratchArg(kScratchOut).scratchArg(kScratchDesc);
    const uint32_t pageDesc = p.table.base + p.resolved.page * p.table.pageStride;
    q.require(Cond::abs(world::addr::TAG_TABLE_PTR, Cmp::Eq, p.table.manager, Width::U32, 0xFFFFFFFFu, "tag table manager unchanged"))
        .require(Cond::ptr(world::addr::TAG_TABLE_PTR, {0}, Cmp::Eq, p.table.base, "tag table base unchanged"))
        .require(Cond::abs(world::addr::SOBJECT_POOL + world::pool::BASE, Cmp::Eq, p.pool.base, Width::U32, 0xFFFFFFFFu, "sObject pool base unchanged"))
        .require(Cond::abs(pageDesc + 0x10, Cmp::Eq, p.resolved.desc.items, Width::U32, 0xFFFFFFFFu, "tag page item array unchanged"))
        .require(Cond::abs(pageDesc + 0x18, Cmp::Eq, p.resolved.desc.reg, Width::U32, 0xFFFFFFFFu, "tag page registration unchanged"))
        .require(Cond::abs(p.resolved.desc.reg + 0x10, Cmp::GtU, p.resolved.index, Width::U32, 0xFFFFFFFFu, "tag index below the page limit"))
        .require(Cond::abs(p.resolved.element, Cmp::Eq, world::kEntityClass, Width::U32, 0xFFFFFFFFu, "item is an entity definition"))
        .require(Cond::abs(p.resolved.element + 8, Cmp::Eq, p.resolved.item8, Width::U32, 0xFFFFFFFFu, "definition body unchanged"))
        .require(Cond::abs(p.residency.definition + world::kDefTypeOffset, Cmp::Eq, p.type, Width::U8, 0xFF, "definition type unchanged"))
        .require(Cond::ptr(world::addr::SOBJECT_GLOBALS_PTR, {kCreationDepthOffset}, Cmp::LtU, kMaxCreationDepth, "creation stack depth < 5"));
    return q;
}

std::vector<SpawnResult> spawnAt(Runtime& rt, world::Reader& mem, const Preflight& p, const std::vector<SpawnPoint>& points, float scale,
                                 const std::function<bool()>& cancelled, const std::function<void(size_t, size_t)>& progress) {
    std::vector<SpawnResult> out;
    for (size_t i = 0; i < points.size(); i += kSpawnBatch) {
        if (cancelled && cancelled()) break;
        const size_t n = std::min(kSpawnBatch, points.size() - i);
        std::vector<guard::Request> reqs;
        for (size_t k = 0; k < n; ++k) reqs.push_back(spawnRequest(p, buildDescriptor(p.tag, points[i + k], scale)));
        const auto res = guard::callMany(rt, reqs, 5000, true);
        for (size_t k = 0; k < n; ++k) {
            SpawnResult s;
            s.point = points[i + k];
            s.called = res[k].called;
            if (s.called) {
                s.handle = res[k].scratchU32(kScratchOut);
                if (!world::validHandle(s.handle)) s.reason = "sobject_new returned no object (the engine refused it)";
            } else {
                s.reason = "refused before the call: " + res[k].reason;
            }
            out.push_back(std::move(s));
        }
        if (progress) progress(i + n, points.size());
    }
     
    const world::PoolHeader pool = world::readPool(mem, world::addr::SOBJECT_POOL);
    for (SpawnResult& s : out) {
        if (!s.called || !world::validHandle(s.handle)) continue;
        const auto o = world::readSObject(mem, pool, s.handle);
        if (!o) s.reason = "handle " + hex8(s.handle) + " is not a live sObject";
        else if (o->type != p.type) s.reason = "handle " + hex8(s.handle) + " is type " + typeText(o->type) + ", expected " + typeText(p.type);
        else s.verified = true;
    }
    return out;
}

 

cmd::SpawnArgs spawnArgs(uint32_t tag, int expectedType, const Placement& in) {
    const Placement p = clamped(in);
    cmd::SpawnArgs a;
    a.tag = tag;
    a.expectedType = expectedType >= 0 && expectedType <= 255 ? expectedType : -1;
    a.mode = uint32_t(p.mode);
    a.facing = uint32_t(p.facing);
    a.count = uint32_t(p.count);
    a.source = uint32_t(p.source);
    a.distance = p.distance;
    a.spacing = p.spacing;
    a.scale = p.scale;
    a.offset[0] = p.offset.x;
    a.offset[1] = p.offset.y;
    a.offset[2] = p.offset.z;
    a.coords[0] = p.coords.x;
    a.coords[1] = p.coords.y;
    a.coords[2] = p.coords.z;
    return a;
}

std::string refusalText(const cmd::SlotHeader& h, uint32_t tag, int expectedType) {
    const std::string t = "Tag " + hex8(tag);
    const uint32_t v = h.value;
    const uint32_t page = world::tagPage(tag);
    switch (h.code()) {
    case cmd::kOkResult: return {};
    case cmd::kCancelled: return "stopped";
    case cmd::kBadArgs: return "the command handler rejected the spawn arguments";
    case cmd::kNotEntityTag: return t + " is not an entity tag (page " + hex8(page) + " is not a content package)";
    case cmd::kNoPool: return "The sObject pool is not set up: load into an activity first";
    case cmd::kNoGlobals: return "The sObject globals are not set up: load into an activity first";
    case cmd::kBusy: return "The game is creating objects right now (creation depth " + std::to_string(v) + "): try again";
    case cmd::kNoTagTable:
        return world::validHandle(v) ? "the tag table at " + hex8(v) + " is not plausible"
                                     : "the tag table pointer at " + hex8(world::addr::TAG_TABLE_PTR) + " is not set";
    case cmd::kNotLoaded: {
        std::string why;
        switch (h.sub()) {
        case cmd::kNlBeyond: why = "page " + hex8(page) + " is beyond the table (" + std::to_string(v) + " pages)"; break;
        case cmd::kNlUnregistered: why = "page " + hex8(page) + " is not registered (package not loaded)"; break;
        case cmd::kNlUnmapped: why = "page " + hex8(page) + " points at unmapped memory"; break;
        case cmd::kNlStride: why = "page " + hex8(page) + " item stride " + hex8(v) + " is not 0x10"; break;
        case cmd::kNlIndex:
            why = "index " + std::to_string(world::tagIndex(tag)) + " is not below the page limit " + std::to_string(v);
            break;
        case cmd::kNlElement: why = "element " + hex8(v) + " is unreadable"; break;
        default: why = "unresolved"; break;
        }
        return t + " is not loaded here: " + why + ". Load the activity that uses it, or load it on the World load tab.";
    }
    case cmd::kNotEntityClass:    
        return (v & 0xFFFF) == 0xFEFE ? t + " is not loaded on the PS3 right now (its tag-table slot is empty)"
                                      : t + " is not an entity definition (item class " + hex8(v) + ")";
    case cmd::kBodyUnmapped: return t + ": its definition body " + hex8(v) + " is not in mapped memory";
    case cmd::kEmptyDefinition: return t + " resolves to an empty definition (type 0): its package is not loaded";
    case cmd::kGarbageProp: return t + " is a garbage prop: the spawn factory refuses these";
    case cmd::kTypeMismatch:
        if (expectedType >= 0 && h.done == 0)
            return t + " is type " + typeText(uint8_t(v)) + " on this console but the catalogue says " + typeText(uint8_t(expectedType)) +
                   ": refusing, the catalogue disagrees with the loaded definition";
        return t + " changed type to " + typeText(uint8_t(v)) + " while spawning";
    case cmd::kNoCamera: return "The rendered camera is not usable right now (menu, cutscene or loading?): use Player or Coordinates";
    case cmd::kNoPlayer: return "No player position: load into an activity first (or use Coordinates)";
    case cmd::kBadPoint: return "The spawn point is not a finite world position";
    default: return std::string("the command handler refused (") + cmd::resultName(h.code()) + ")";
    }
}

std::vector<SpawnResult> spawnResults(const cmd::Outcome& o) {
    std::vector<SpawnResult> out;
    if (o.tail.empty()) return out;
    const uint8_t type = uint8_t(o.header.type);
    for (const cmd::SpawnOut& r : cmd::parseSpawnResults(o.tail, o.header.done)) {
        SpawnResult s;
        s.point.position = {r.x, r.y, r.z};
        s.called = r.status != cmd::kSpawnNotRun;
        s.handle = r.handle;
        switch (r.status) {
        case cmd::kSpawnOk: s.verified = true; break;
        case cmd::kSpawnNone: s.reason = "sobject_new returned no object (the engine refused it)"; break;
        case cmd::kSpawnNotLive: s.reason = "handle " + hex8(r.handle) + " is not a live sObject"; break;
        case cmd::kSpawnWrongType:
            s.reason = "handle " + hex8(r.handle) + " is type " + typeText(r.liveType) + ", expected " + typeText(type);
            break;
        default: s.reason = "not spawned"; break;
        }
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<SpawnResult> spawnInGame(Runtime& rt, cmd::Handler& handler, uint32_t tag, int expectedType, const Placement& p,
                                     const std::function<bool()>& cancelled, const std::function<void(size_t, size_t)>& progress,
                                     InGameReport* report) {
    const cmd::SpawnArgs args = spawnArgs(tag, expectedType, p);
    const auto body = cmd::encodeSpawn(args, 0);                 
    const size_t total = args.count;
    const cmd::Outcome o = handler.run(
        rt, body, cancelled, [&](uint32_t done) {
            if (progress) progress(done, total);
        },
        true);
    if (o.header.state == cmd::kStateFailed) throw Error(refusalText(o.header, tag, args.expectedType));
    auto results = spawnResults(o);
    if (report) {
        report->type = o.header.started || o.header.done ? uint8_t(o.header.type) : 0;
        report->requested = total;
        report->header = o.header;
        if (o.panicked) report->stopped = "stopped by Panic";
        else if (o.header.code() != cmd::kOkResult) report->stopped = refusalText(o.header, tag, args.expectedType);
    }
    return results;
}

 

guard::Request despawnRequest(uint32_t handle) { return guard::Request(world::addr::SOBJECT_DELETE).sobject(handle).arg(handle); }

std::vector<DespawnResult> despawn(Runtime& rt, world::Reader& mem, const std::vector<uint32_t>& handles) {
    std::vector<DespawnResult> out;
    for (size_t i = 0; i < handles.size(); i += 16) {
        const size_t n = std::min<size_t>(16, handles.size() - i);
        std::vector<guard::Request> reqs;
        for (size_t k = 0; k < n; ++k) {
            if (!world::validHandle(handles[i + k])) throw Error("not a handle: " + hex8(handles[i + k]));
            reqs.push_back(despawnRequest(handles[i + k]));
        }
        const auto res = guard::callMany(rt, reqs, 3000, false);
        for (size_t k = 0; k < n; ++k) {
            DespawnResult d;
            d.handle = handles[i + k];
            d.called = res[k].called;
            if (!d.called) d.reason = res[k].reason;
            out.push_back(std::move(d));
        }
    }
    const world::PoolHeader pool = world::readPool(mem, world::addr::SOBJECT_POOL);
    for (DespawnResult& d : out) {
        if (!d.called) {
             
            d.gone = !world::readSObject(mem, pool, d.handle).has_value();
            continue;
        }
        const auto at = pool.entryOf(d.handle);
        const auto e = at ? mem.tryBytes(*at, 0x10) : std::nullopt;
        if (!e) {
            d.reason = "could not re-read the entry";
            continue;
        }
        d.gone = e->u32(world::sobj::HANDLE) != d.handle || (e->u32(world::sobj::FLAGS) & world::sobj::FLAG_DELETING) != 0;
        if (!d.gone) d.reason = "sobject_delete returned but the object is still there";
    }
    return out;
}

 

MoveResult moveObject(world::Reader& mem, const std::function<void(uint32_t, const std::vector<uint8_t>&)>& write, uint32_t handle,
                      const Vec3& to) {
    if (!to.finite() || std::fabs(to.x) > 1e7f || std::fabs(to.y) > 1e7f || std::fabs(to.z) > 1e7f)
        throw Error("Enter finite world coordinates within +/-10,000,000");
    const world::PoolHeader pool = world::readPool(mem, world::addr::SOBJECT_POOL);
    const auto o = world::readSObject(mem, pool, handle);
    if (!o) throw Error("Object " + hex8(handle) + " is gone (stale handle or being deleted)");
    MoveResult r;
    r.before = *o;
    const auto bytes = world::beVec3(to);
    r.wrote40 = o->vector40.finite() && world::distance(o->vector40, o->position) < 0.5f;
    if (r.wrote40) write(o->addr + world::sobj::VECTOR40, bytes);
    write(o->addr + world::sobj::POS90, bytes);
    write(o->addr + world::sobj::POSITION, bytes);
    const auto after = world::readSObject(mem, pool, handle);
    if (!after) throw Error("Object " + hex8(handle) + " disappeared right after the move");
    r.after = *after;
    r.held = world::distance(after->position, to) < 0.05f;
    return r;
}

 

std::map<uint32_t, uint32_t> tagsOfObjects(world::Reader& mem, const world::TagTable& t, const std::vector<world::SObject>& objects,
                                           uint32_t budget) {
    struct PageData {
        bool ok = false;
        world::TagPage desc;
        uint32_t limit = 0;
        world::Bytes items;
    };
    std::map<uint32_t, PageData> pages;
    auto pageData = [&](uint32_t page) -> const PageData& {
        auto it = pages.find(page);
        if (it != pages.end()) return it->second;
        PageData pd;
        try {
            if (page < t.pageCount) {
                pd.desc = world::readPage(mem, t, page);
                if (pd.desc.live() && world::mapped(pd.desc.items) && world::mapped(pd.desc.reg, 0x14) && pd.desc.itemStride != 0 &&
                    pd.desc.itemStride <= 0x100) {
                    pd.limit = mem.u32(pd.desc.reg + 0x10);
                    const uint64_t span = uint64_t(pd.limit) * pd.desc.itemStride;
                    if (pd.limit && span <= 0x100000 && world::mapped(pd.desc.items, uint32_t(span))) {
                        if (auto b = mem.tryBytes(pd.desc.items, uint32_t(span))) {
                            pd.items = std::move(*b);
                            pd.ok = true;
                        }
                    }
                }
            }
        } catch (const std::exception&) {
            pd.ok = false;
        }
        return pages.emplace(page, std::move(pd)).first->second;
    };
    struct Want {
        uint32_t handle, addr;
        size_t priority;
    };
    std::vector<Want> wants;
    for (size_t i = 0; i < objects.size(); ++i) {
        const world::SObject& o = objects[i];
        if (!world::validHandle(o.componentHead)) continue;
        const PageData& pd = pageData(world::tagPage(o.componentHead));
        const uint32_t index = world::tagIndex(o.componentHead);
        if (!pd.ok || index >= pd.limit) continue;
        const uint32_t off = index * pd.desc.itemStride;
        if (!pd.items.has(off + 8, 4)) continue;
        const uint32_t element = pd.desc.items + off;
        const uint32_t body = element - (pd.items.u32(off + 8) & pd.desc.mask);
        if (!world::mapped(body + 4, 4) || (body & 3)) continue;
        wants.push_back({o.handle, body + 4, i});
    }
     
    std::vector<size_t> byAddr(wants.size());
    for (size_t i = 0; i < byAddr.size(); ++i) byAddr[i] = i;
    std::sort(byAddr.begin(), byAddr.end(), [&](size_t a, size_t b) { return wants[a].addr < wants[b].addr; });
    struct Window {
        uint32_t start = 0, end = 0;    
        size_t priority = 0;
        std::vector<size_t> members;
    };
    std::vector<Window> windows;
    for (size_t i : byAddr) {
        const Want& w = wants[i];
        if (windows.empty() || w.addr + 4 - windows.back().start > 0x10000) windows.push_back({w.addr, w.addr + 4, w.priority, {}});
        Window& cur = windows.back();
        cur.end = std::max(cur.end, w.addr + 4);
        cur.priority = std::min(cur.priority, w.priority);
        cur.members.push_back(i);
    }
    std::sort(windows.begin(), windows.end(), [](const Window& a, const Window& b) { return a.priority < b.priority; });
    std::map<uint32_t, uint32_t> out;
    uint64_t used = 0;
    for (const Window& w : windows) {
        const uint32_t size = w.end - w.start;
        if (used && used + size > budget) continue;
        used += size;
        auto bytes = mem.tryBytes(w.start, size);
        for (size_t m : w.members) {
            const Want& want = wants[m];
            uint32_t tag = 0;
            if (bytes) tag = bytes->u32(want.addr - w.start);
            else if (w.members.size() <= 4) {
                if (auto one = mem.tryBytes(want.addr, 4)) tag = one->u32(0);
            }
            if (world::validHandle(tag) && world::isPackagePage(world::tagPage(tag))) out[want.handle] = tag;
        }
    }
    return out;
}

}   
