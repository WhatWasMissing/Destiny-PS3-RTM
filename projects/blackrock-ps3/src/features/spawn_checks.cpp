 
 
 
 
 
 
 
 
 
 
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "app/registry.h"
#include "core/error.h"
#include "core/runtime.h"
#include "features/spawn_shared.h"
#include "game/guard_call.h"
#include "game/names_catalog.h"
#include "game/spawn_catalog.h"
#include "game/spawn_ops.h"
#include "game/spawn_place.h"
#include "game/spawn_town.h"
#include "game/spawn_wtl.h"
#include "game/world.h"

namespace brps3 {
namespace {

using namespace world;
namespace S = spawn;
namespace W = spawn::wtl;

std::string fmt(const char* f, ...) {
    char buf[600];
    va_list a;
    va_start(a, f);
    std::vsnprintf(buf, sizeof buf, f, a);
    va_end(a);
    return buf;
}

std::string v3(const Vec3& v) { return fmt("(%.3f, %.3f, %.3f)", double(v.x), double(v.y), double(v.z)); }

 

void probePlacement(Ctx& c) {
    c.require(kFingerprints, "World readers");
    RuntimeReader mem(c.rt());
    const auto raw = mem.tryBytes(S::kViewMatrix, S::kCameraBytes);
    if (!raw) throw Error("the view matrix at 01AE6F10 is unreadable");
    const S::CameraMatrices m = S::parseMatrices(*raw);
    for (int r = 0; r < 4; ++r)
        c.log(fmt("view row %d: %9.4f %9.4f %9.4f %9.4f", r, double(m.view[size_t(r * 4)]), double(m.view[size_t(r * 4 + 1)]),
                  double(m.view[size_t(r * 4 + 2)]), double(m.view[size_t(r * 4 + 3)])));
    c.log(fmt("projection P0 %.3f P5 %.3f P10 %.3f P11 %.3f P14 %.3f P15 %.3f (orbit read: 1.93, 3.43, -1, -1, -0.15, 0)", double(m.projection[0]),
              double(m.projection[5]), double(m.projection[10]), double(m.projection[11]), double(m.projection[14]), double(m.projection[15])));
    S::CameraPose view, aim;
    const bool viewOk = S::cameraFromView(m.view.data(), view);
    const bool aimOk = S::cameraFromMatrices(m, 0.5f, 0.5f, aim);
    c.log(viewOk ? "camera position " + v3(view.position) + ", forward " + v3(view.forward) : std::string("view matrix is NOT an orthonormal camera"));
    c.log(aimOk ? "screen-centre ray forward " + v3(aim.forward) + "; crosshair point at 6 m " + v3(aim.position + aim.forward * 6.0f)
                : std::string("screen-centre ray: NOT resolvable (projection not perspective?)"));
    const PlayerLookup pl = findLocalPlayer(mem);
    if (!pl.player) {
        c.log(std::string("no local player: ") + pl.message);
        c.log(aimOk ? "verdict: camera usable; run again in an activity for the player check" : "verdict: camera NOT usable here (menu?), run again in an activity");
        return;
    }
    const Vec3 eng = readEnginePosition(mem, *pl.player);
    const Vec3 elem = readPosition(mem, *pl.player);
    c.log("player engine position sObject +0xB0 (verified): " + v3(eng));
    c.log("player element +0x2810 (likely):                " + v3(elem) + fmt("  distance %.3f", double(distance(eng, elem))));
    if (aimOk) c.log(fmt("camera to player: %.2f m (expected under ~8 m: first or third person)", double(distance(view.position, eng))));
     
     
     
    const Vec3 swz{view.position.x, view.position.z, view.position.y};
    if (viewOk)
        c.log(fmt("camera origin native XYZ %s: %.2f m from you; with the Y/Z swizzle %s: %.2f m", v3(view.position).c_str(),
                  double(distance(view.position, eng)), v3(swz).c_str(), double(distance(swz, eng))));
    const bool camNear = aimOk && distance(view.position, eng) < 8.0f;
    const bool swzNear = viewOk && distance(swz, eng) < 8.0f;
    const bool posAgree = elem.finite() && distance(eng, elem) < 0.5f;
    c.log(std::string("verdict: crosshair ") +
          (camNear ? "OK (camera sits at the player, native XYZ as the Spawner reads it)"
                   : swzNear ? "WRONG AXES: only the Y/Z-swizzled origin sits at the player: send this report"
                             : "NOT confirmed (camera far from the player)") +
          "; player sources " + (posAgree ? "agree (both usable)" : "DISAGREE: keep the engine position source (default)"));
}

 

uint32_t findLoadedEntity(Ctx& c, Reader& mem, const TagTable& t) {
    const uint32_t sel = spawnui::selectedTag();
    if (sel && checkResident(mem, t, names::toPs3Tag(sel)).resident) return sel;
     
    size_t pagesRead = 0;
    for (const TagPage& p : readPages(mem, t, kFirstPackagePage)) {
        if (!p.live() || p.itemStride != kPackageItemStride) continue;
        if (++pagesRead > 10 || c.cancelled()) break;
        for (uint32_t idx : scanResidentPage(mem, p).indices) {
            const uint32_t tag = names::fromPs3Tag(makeTag(p.page, idx));
            const S::CatalogRow* row = S::catalog().find(tag);
            if (row && (row->type == TYPE_BIPED || row->type == 8) && checkResident(mem, t, names::toPs3Tag(tag)).resident) return tag;
        }
    }
    return 0;
}

void probePreflight(Ctx& c) {
    c.require(kFingerprints, "World readers");
    c.require(S::kSpawnFingerprints, "Spawn (sobject_new)");
    RuntimeReader mem(c.rt());
    const TagTable t = readTagTable(mem);
    const uint32_t tag = findLoadedEntity(c, mem, t);
    if (!tag) {
        c.log("no loaded catalogued biped / interactive found in the first 10 package pages (and the selection is not loaded)");
        c.log("verdict: nothing to check here: run it in an activity (or select a loaded entity on the Spawner page)");
        return;
    }
    c.log("entity " + S::tagText(tag) + " (" + spawnui::displayName(tag) + ")");
    const S::Preflight p = S::preflight(mem, names::toPs3Tag(tag), spawnui::catalogType(tag));
    c.log(fmt("tag table: manager %s base %s, %u pages; page %03X desc items %s reg %s limit %u", hex8(p.table.manager).c_str(), hex8(p.table.base).c_str(),
              p.table.pageCount, p.resolved.page, hex8(p.resolved.desc.items).c_str(), hex8(p.resolved.desc.reg).c_str(), p.resolved.limit));
    c.log(fmt("item %s: class %s, back offset %s -> body %s, definition %s, type %u (%s)", hex8(p.resolved.element).c_str(), hex8(p.resolved.item0).c_str(),
              hex8(p.resolved.item8).c_str(), hex8(p.residency.body).c_str(), hex8(p.residency.definition).c_str(), unsigned(p.type),
              S::categoryName(p.type)));
    c.log(fmt("sObject pool base %s stride 0x%X cap %u count %u; creation depth %u (must be < 5)", hex8(p.pool.base).c_str(), p.pool.stride,
              p.pool.capacity, p.pool.count, p.creationDepth));
    for (const std::string& w : p.warnings) c.log("warning: " + w);
     
    S::SpawnPoint at;
    at.position = {0, 0, 0};
    const auto desc = S::buildDescriptor(names::toPs3Tag(tag), at, 1.0f);
    c.log("descriptor +0x00..+0x3F: " + hexBytes(desc.data(), 0x40));
    c.log("descriptor +0x40..+0x7F: " + hexBytes(desc.data() + 0x40, 0x40) + " (+0x68 = 0x10, +0x78 = 0x0C)");
    const guard::Request q = S::spawnRequest(p, desc);
    size_t pass = 0;
    for (size_t i = 0; i < q.conds.size(); ++i) {
        const guard::Cond& k = q.conds[i];
        uint32_t a = k.addr;
        bool ok = true;
        for (uint32_t off : k.derefs) {
            const auto v = mem.tryBytes(a, 4);
            if (!v) {
                ok = false;
                break;
            }
            a = v->u32(0) + off;
        }
        uint32_t value = 0;
        if (ok) {
            const auto v = mem.tryBytes(a, uint32_t(k.width));
            ok = v.has_value();
            if (ok) value = k.width == guard::Width::U8 ? v->u8(0) : k.width == guard::Width::U16 ? v->u16(0) : v->u32(0);
        }
        value &= k.mask;
        bool holds = false;
        if (ok) switch (k.cmp) {
            case guard::Cmp::Eq: holds = value == k.value; break;
            case guard::Cmp::Ne: holds = value != k.value; break;
            case guard::Cmp::LtU: holds = value < k.value; break;
            case guard::Cmp::GeU: holds = value >= k.value; break;
            case guard::Cmp::LeU: holds = value <= k.value; break;
            case guard::Cmp::GtU: holds = value > k.value; break;
            }
        pass += holds ? 1 : 0;
        c.log(fmt("  condition %zu %-34s value %08X %s %08X -> %s", i, k.describe().c_str(), value, guard::cmpText(k.cmp), k.value, holds ? "holds" : "FAILS"));
    }
    c.log(fmt("verdict: preflight OK and %zu of %zu same-frame conditions hold now; one live spawn of a loaded entity is the remaining check "
              "(template +0x68 / +0x78 and the handle post-check)",
              pass, q.conds.size()));
}

 

void probeObjects(Ctx& c) {
    c.require(kFingerprints, "World readers");
    RuntimeReader mem(c.rt());
    const ObjectScan scan = readObjects(mem);
    size_t live = 0, same90 = 0, same40 = 0, unitScale = 0;
    std::map<uint8_t, size_t> types;
    for (const SObject& o : scan.objects) {
        if (o.deleting()) continue;
        ++live;
        ++types[o.type];
        if (distance(o.pos90, o.position) < 0.05f) ++same90;
        if (distance(o.vector40, o.position) < 0.5f) ++same40;
        if (std::fabs(o.scale - 1.0f) < 0.01f) ++unitScale;
    }
    c.log(fmt("pool %s: base %s stride 0x%X cap %u count %u; %zu live rows read", hex8(scan.pool.addr).c_str(), hex8(scan.pool.base).c_str(), scan.pool.stride,
              scan.pool.capacity, scan.pool.count, live));
    std::string mix;
    for (const auto& [t, n] : types) mix += fmt("%s %zu, ", S::categoryName(t), n);
    if (!mix.empty()) c.log("types: " + mix.substr(0, mix.size() - 2));
    c.log(fmt("+0x90 == +0xB0 on %zu of %zu (expected: all); +0x40 == +0xB0 on %zu (Move writes +0x40 only for those); +0x9C == 1.0 on %zu", same90, live,
              same40, unitScale));
    int shown = 0;
    for (const SObject& o : scan.objects) {
        if (o.deleting() || shown >= 6 || S::noiseType(o.type)) continue;
        ++shown;
        c.log(fmt("  %s %-14s +B0 %s  +90 %s  +40 %s", hex8(o.handle).c_str(), S::categoryName(o.type), v3(o.position).c_str(), v3(o.pos90).c_str(),
                  v3(o.vector40).c_str()));
    }
    c.log(live && same90 * 10 >= live * 9 ? "verdict: the two transform copies agree: Move's +0x90 / +0xB0 writes match the layout"
                                          : "verdict: +0x90 and +0xB0 often differ: send this report before relying on Move");
}

 

void probeWtl(Ctx& c) {
    c.require(kFingerprints, "World readers");
    c.require(W::kFingerprints, "World tag load");
    RuntimeReader mem(c.rt());
    const uint32_t g = mem.u32(W::GLOBALS);
    const uint16_t cursor = mem.bytes(W::DRAIN_CURSOR, 2).u16(0);
    const PoolHeader det = readPool(mem, W::DET_POOL), nondet = readPool(mem, W::NONDET_POOL);
    const uint32_t count = mem.u32(W::SOBJECT_COUNT);
    const uint32_t ctl = mem.u32(W::WORLD_CONTROLLER);
    uint32_t state = 0xFFFFFFFF;
    if (mapped(ctl + W::WORLD_STATE_OFFSET, 4))
        if (const auto b = mem.tryBytes(ctl + W::WORLD_STATE_OFFSET, 4)) state = b->u32(0);
    c.log(fmt("globals *(01A1DCE0) = %s (what 0x14E858 returns); drain cursor %04X (FFFF = idle)", hex8(g).c_str(), cursor));
    c.log(fmt("deterministic requests 01A1DD20: base %s stride 0x%X cap %u count %u (expected stride 0x48, identity at +0x44)", hex8(det.base).c_str(),
              det.stride, det.capacity, det.count));
    c.log(fmt("nondeterministic requests 01A1DDA0: base %s stride 0x%X cap %u", hex8(nondet.base).c_str(), nondet.stride, nondet.capacity));
    c.log(fmt("world state %u (%s); live sObjects %u", state, W::worldStateName(state), count));
    const bool ok = g && det.plausible() && det.stride == 0x48;
    c.log(ok ? "verdict: loader globals and the request pool look as mapped (stride read live, as the stub does)"
             : "verdict: loader NOT set up as expected here (menus?) or stride differs: send this report");
}

 

void probeTick(Ctx& c) {
    c.require(kFingerprints, "World readers");
    c.require(W::kFingerprints, "World tag load");    
    Runtime& rt = c.rt();
    constexpr const char* kTask = "spawn.wtl.probe";
    if (rt.hasTask(W::AutoLoad::kTask)) throw Error("auto-load is armed: disarm it on the World load tab first");
    W::Params p;
    p.tag = 0x80C7A2F0;
    p.trigger = W::Trigger::Never;           
    const uint32_t stub = W::installStub(rt);
    const uint32_t block = rt.alloc(W::kBlockSize);
    struct Cleanup {
        Runtime& rt;
        uint32_t block;
        ~Cleanup() {
            try {
                rt.removeTask("spawn.wtl.probe");
            } catch (const std::exception&) {
            }
            rt.freeLater(block);
        }
    } cleanup{rt, block};
    const auto b = W::encodeBlock(p, false);
    rt.write(block, b.data(), b.size(), true);
    rt.addTaskAt(kTask, stub, block);
    c.log("observing for up to 75 s: launch an activity (or travel) now; Stop ends it early");
    std::map<uint32_t, std::pair<uint32_t, uint32_t>> frames;    
    uint32_t lastState = 0xFFFFFFFE, transitionFrames = 0;
    bool sawLoading = false, done = false;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(75);
    while (!done && std::chrono::steady_clock::now() < end && !c.cancelled()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        const W::State s = W::decodeBlock(rt.read(block, W::kBlockSize));
        auto& f = frames[s.lastState];
        if (!f.first) f.first = s.frames;
        f.second = s.frames;
        transitionFrames = s.transitionFrames;
        if (s.lastState != lastState) {
            c.log(fmt("  state %u %-34s at frame %u", s.lastState, W::worldStateName(s.lastState), s.frames));
            lastState = s.lastState;
        }
        if (s.lastState == W::STATE_WORLD_TRANSITION || s.lastState == W::STATE_SLICE_LOADING) sawLoading = true;
        if (sawLoading && s.lastState == W::STATE_IN_WORLD) done = true;
    }
    for (const auto& [st, f] : frames) c.log(fmt("  %u %-34s frames %u..%u", st, W::worldStateName(st), f.first, f.second));
    if (!sawLoading && transitionFrames == 0)
        c.log("verdict: no loading state seen (33 / 34): run it again and launch an activity while it observes");
    else
        c.log(transitionFrames ? fmt("verdict: the tick RUNS during loading (%u frames in states 33/34): the auto-load trigger can fire there", transitionFrames)
                               : std::string("verdict: loading states were reached but the tick did NOT run in them: auto-load on state 33/34 cannot "
                                             "fire; use the few-objects trigger and send this report"));
}

 

void probeTown(Ctx& c) {
    c.require(kFingerprints, "World readers");
    c.require(S::town::kFingerprints, "Town presence");
    RuntimeReader mem(c.rt());
    std::string why;
    const auto t = S::town::read(mem, &why);
    if (!t) {
        c.log("town state: " + why);
        c.log("verdict: town block not set up here (menus?): run it in the Tower");
        return;
    }
    c.log(fmt("block *(01AB6894) = %s: %s", hex8(t->block).c_str(), hexBytes(t->bytes.data(), 12).c_str()));
    c.log("decoded: " + S::town::describe(*t));
    const PlayerLookup pl = findLocalPlayer(mem);
    c.log(pl.player ? "player " + hex8(pl.player->handle) : "no spawned player: " + pl.message);
    c.log(std::string("verdict: ") + (t->current() ? "town equipment applied (expected in the Tower)" : "normal equipment applied (expected in an activity)") +
          "; the bytes are flags as the Xbox layout says");
}

}   

BR_FEATURES(spawn_checks) {
    auto probe = [&](const char* id, const char* label, const char* help, void (*run)(Ctx&), const char* badge = kBadgeNeedsCheck) {
        r.action({.id = id, .page = Page::Testing, .tab = kPortChecksTab, .section = spawnui::kProbeSection, .label = label, .help = help, .run = run,
                  .badge = badge});
    };
    probe("spawn.check.placement", "Placement Inputs: Crosshair Camera and Player Position",
          "In an activity, standing still. Reads the view / projection matrices (0x1AE6F10) and both player position sources. Expected: an "
          "orthonormal view, a camera within ~8 m of the player in native XYZ (it also tries the Y/Z swizzle the plugin w2s uses), +0x2810 "
          "within 0.5 of +0xB0.",
          probePlacement);
    probe("spawn.check.preflight", "Spawn Preflight on a Loaded Entity",
          "Finds a loaded biped / interactive (or uses the Spawner selection), runs the spawn preflight and evaluates the 10 same-frame "
          "conditions of its guarded sobject_new from the PC. Expected: preflight OK and 10 of 10 hold. Calls nothing.",
          probePreflight);
    probe("spawn.check.objects", "Object Pool: Position Copies the Move Tool Writes",
          "Reads the whole sObject pool. Expected: +0x90 equals +0xB0 on (almost) every live row; reports how many also hold the position at +0x40.",
          probeObjects);
    probe("spawn.check.wtl", "World Tag Load: Loader Globals and Request Pool",
          "Reads the loader globals, drain cursor, both request pools and the world state. Expected in an activity: globals set, cursor FFFF when "
          "idle, deterministic pool stride 0x48.",
          probeWtl, "");
    probe("spawn.check.tick", "World Load: Does the Tick Run While Loading (P4)",
          "Start in orbit, run it, then launch an activity within 75 s. Observes the world state every frame from Blackrock's own task (no load, no "
          "native call). Expected: frames counted in states 33/34, so the auto-load trigger can fire there.",
          probeTick);
    probe("spawn.check.town", "Town Presence Block",
          "In the Tower. Reads the 12 town presence bytes at *(0x1AB6894). Expected: flags 0/1, applied = town in the Tower.", probeTown, "");
}

}   
