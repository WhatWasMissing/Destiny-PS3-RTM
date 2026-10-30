 
 
 
 
 
 
 
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "app/registry.h"
#include "core/error.h"
#include "core/runtime.h"
#include "game/guard_call.h"
#include "game/names_catalog.h"
#include "game/world.h"
#include "game/world_addr.h"
#include "game/world_factions.h"
#include "game/world_objects.h"
#include "game/world_player.h"
#include "game/world_registry.h"
#include "game/world_tags.h"

namespace brps3 {
namespace {

using namespace world;

constexpr const char* kSection = "World (shared layer)";

std::string fmt(const char* f, ...) {
    char buf[512];
    va_list a;
    va_start(a, f);
    std::vsnprintf(buf, sizeof buf, f, a);
    va_end(a);
    return buf;
}

std::string v3(const Vec3& v) { return fmt("(%.3f, %.3f, %.3f)", double(v.x), double(v.y), double(v.z)); }
std::string q4(const Quat& q) { return fmt("(%.3f, %.3f, %.3f, %.3f)", double(q.x), double(q.y), double(q.z), double(q.w)); }

LocalPlayer player(Ctx& c, RuntimeReader& mem) {
    c.require(kFingerprints, "World readers");
    return requireLocalPlayer(mem);
}

 

void probeReaders(Ctx& c) {
    c.require(kFingerprints, "World readers");
    RuntimeReader mem(c.rt());
    const WorldGlobals g = readGlobals(mem);
    c.log(fmt("engine clock %llu", static_cast<unsigned long long>(g.engineClock)));
    auto showPool = [&](const char* name, const PoolHeader& p, uint32_t wantStride) {
        c.log(fmt("%-18s %s: base %s stride 0x%X cap %u count %u -> %s", name, hex8(p.addr).c_str(), hex8(p.base).c_str(), p.stride, p.capacity,
                  p.count, p.plausible() && (!wantStride || p.stride == wantStride) ? "ok" : "NOT plausible"));
    };
    showPool("sObjects", g.sobjects, sobj::STRIDE);
    showPool("player array", g.players, 0x1B0);
    showPool("attachment roots", g.attachments, 0x14);
    c.log(fmt("sObject globals *(01A1D9D4) = %s, faction bank *(01A1DA08) = %u", hex8(g.sobjectGlobals).c_str(), g.bank));
    try {
        const TagTable t = readTagTable(mem);
        size_t live = 0, packages = 0;
        for (const TagPage& p : readPages(mem, t)) {
            live += p.live();
            packages += p.live() && isPackagePage(p.page);
        }
        c.log(fmt("tag table %s: %u pages, %zu registered, %zu package pages (expected ~234 / ~54 in an activity)", hex8(t.base).c_str(),
                  t.pageCount, live, packages));
    } catch (const std::exception& e) {
        c.log(std::string("tag table: ") + e.what());
    }
    const auto rows = readGlobalsRegistry(mem);
    size_t matched = 0;
    for (const GlobalInfo& gi : globalCatalog())
        for (const RegistryRow& r : rows)
            if (r.slot == gi.slot) {
                ++matched;
                break;
            }
    c.log(fmt("globals registry 019F2E38: %zu rows; %zu of %zu catalogue slots registered", rows.size(), matched, globalCatalog().size()));
    const Allegiance a = readAllegiance(mem);
    c.log(fmt("faction allegiance block %s: %s; %u allied pairs of 496", hex8(a.body).c_str(),
              a.owned() ? "ownership proven" : ("NOT proven: " + a.proof).c_str(), allegianceCount(a.bits)));
    const bool ok = g.sobjectsOk() && g.players.plausible() && a.owned();
    c.log(ok ? "verdict: shared readers OK" : "verdict: something above is not plausible (in an activity? see the lines marked NOT)");
}

 

void probePlayerChain(Ctx& c) {
    c.require(kFingerprints, "World readers");
    RuntimeReader mem(c.rt());
    const PlayerLookup r = findLocalPlayer(mem);
    if (!r.player) {
        c.log(std::string("no local player: ") + playerStatusName(r.status) + ": " + r.message);
        return;
    }
    const LocalPlayer& p = *r.player;
    c.log(fmt("descriptor *(01B29450) = %s: elements %s, element size 0x%X (expect 0x2930), count %u", hex8(p.descriptor).c_str(),
              hex8(p.elements).c_str(), p.elementSize, p.elementCount));
    c.log(fmt("element %u at %s: +0x84 handle %s", p.elementIndex, hex8(p.element).c_str(), hex8(p.handle).c_str()));
    c.log(fmt("sObject entry %s: +0xC %s, type %u (%s), flags %s", hex8(p.entry).c_str(), hex8(p.object.handle).c_str(), p.object.type,
              typeName(p.object.type), hex8(p.object.flags).c_str()));
    if (p.playerEntry) {
        const uint32_t row54 = mem.u32(p.playerEntry + player::ENTRY_HANDLE);
        c.log(fmt("player array row %s: +0x54 %s, +0x4C %s (players-datum handle), +0x90 faction %d", hex8(p.playerEntry).c_str(),
                  hex8(row54).c_str(), hex8(p.playerDatum).c_str(), p.faction));
    } else {
        c.log("player array: no row with +0x54 == the handle (expected one)");
    }
    const bool same = p.object.handle == p.handle && p.playerEntry != 0;
    c.log(fmt("expected: element +0x84 == sObject +0xC == player row +0x54 -> %s", same ? "all equal" : "MISMATCH"));
    c.log(same ? "verdict: local player chain confirmed" : "verdict: chain MISMATCH (send this report)");
}

 

void probePlayerPosition(Ctx& c) {
    RuntimeReader mem(c.rt());
    const LocalPlayer p = player(c, mem);
     
    const Vec3 elem = readPosition(mem, p);
    const Vec3 eng = readEnginePosition(mem, p);
    c.log("element +0x2810 (teleport target, likely): " + v3(elem));
    c.log("sObject +0xB0 (engine position, verified): " + v3(eng));
    c.log("sObject +0x40 (Xbox ESP vector):           " + v3(mem.bytes(p.entry + sobj::VECTOR40, 12).vec3(0)));
    if (p.playerEntry) c.log("player row +0xE0 (likely):                 " + v3(mem.bytes(p.playerEntry + player::ENTRY_POSITION, 12).vec3(0)));
    const float d = distance(elem, eng);
    c.log(fmt("distance +0x2810 to +0xB0: %.3f (expected < 0.5 while standing still)", double(d)));
    if (!elem.finite()) c.log("verdict: +0x2810 is not a position (not finite): do NOT use it for teleport");
    else if (d < 0.5f) c.log("verdict: +0x2810 matches the engine position: confirmed (move and run again to be sure)");
    else if (d < 5.0f) c.log("verdict: close but not equal: run again standing still");
    else c.log("verdict: +0x2810 is NOT the player position here: teleport must use another target");
}

 

void probeTransform(Ctx& c) {
    RuntimeReader mem(c.rt());
    const LocalPlayer p = player(c, mem);
    const Bytes e = mem.bytes(p.entry, sobj::STRIDE);
    const SObject o = parseSObject(e, p.handle & 0x1FFF);
    c.log("+0x40 vector (Xbox ESP position): " + v3(o.vector40));
    c.log("+0x80 quaternion:                 " + q4(o.rot));
    c.log("+0x90 position (transform 1):     " + v3(o.pos90) + fmt("  +0x9C scale %.4f (likely)", double(o.scale)));
    c.log("+0xA0 quaternion:                 " + q4(o.rot2));
    c.log("+0xB0 position (transform 2):     " + v3(o.position) + fmt("  +0xBC %.4f", double(o.scale2)));
    const float d = distance(o.pos90, o.position);
    c.log(fmt("expected: +0x90 == +0xB0 (distance %.3f), +0x9C == 1.0 and +0xBC == 1.0 for an unscaled player", double(d)));
    const bool ok = d < 0.5f && std::fabs(o.scale - 1.0f) < 0.01f;
    c.log(ok ? "verdict: transform layout and the +0x9C scale look right"
             : "verdict: transform differs from the Xbox layout (send this report before using move.scale)");
}

 

void probeTagIds(Ctx& c) {
    c.require(kFingerprints, "World readers");
    RuntimeReader mem(c.rt());
    const auto& cat = names::entities();
    if (!cat.error.empty()) throw Error("entity catalogue unavailable: " + cat.error);
    const auto& st = names::translationStatus();
    c.log(std::string("assumption: ") + st.assumption + "; port map: " + st.portMap);
    const TagTable t = readTagTable(mem);
    std::vector<TagPage> pages;
    for (const TagPage& p : readPages(mem, t, kFirstPackagePage))
        if (p.live() && p.itemStride == kPackageItemStride) pages.push_back(p);
    c.log(fmt("%zu live package pages", pages.size()));
     
     
    std::vector<std::pair<uint32_t, uint8_t>> live;
    size_t resident = 0, scanned = 0;
    for (size_t i = 0; i < pages.size() && live.size() < 48 && scanned < 16; ++i) {
        if (c.cancelled()) break;
        ++scanned;
        const ResidentPage rp = scanResidentPage(mem, pages[i]);
        resident += rp.indices.size();
        int taken = 0;
        for (uint32_t idx : rp.indices) {
            if (taken >= 4 || live.size() >= 48) break;
            const uint32_t tag = makeTag(rp.page, idx);
            if (!cat.find(names::fromPs3Tag(tag))) continue;
            const Residency r = checkResident(mem, t, tag);
            if (!r.resident) continue;
            live.push_back({tag, r.type});
            ++taken;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    c.log(fmt("scanned %zu pages: %zu resident entity definitions, %zu catalogued samples", scanned, resident, live.size()));
    const names::TranslationReport rep = names::compareLiveTypes(cat, live);
    for (const auto& [tag, types] : rep.mismatches)
        c.log(fmt("  mismatch %s: catalogue type %u (%s), live %u (%s)", hex8(tag).c_str(), types.first, typeName(types.first), types.second,
                  typeName(types.second)));
    c.log("verdict: " + rep.verdict);
}

 

void probeGuard(Ctx& c) {
    c.require({{addr::FACTIONS_ARE_ALLIED, "7C0802A648BFDBE1F821FF71F80100A0"}}, "Guard check (factions_are_allied)");
    RuntimeReader mem(c.rt());
    const LocalPlayer p = player(c, mem);
    Runtime& rt = c.rt();
     
    const auto request = [&](uint32_t h) { return guard::Request(addr::FACTIONS_ARE_ALLIED).sobject(h).arg(0).arg(1); };
    const uint32_t stale = (p.handle & 0x1FFF) | ((p.handle + 0x10000) & 0xFFFF0000);
    const auto results = guard::callMany(rt, {request(stale), request(p.handle)}, 3000, true);
    const guard::Result& refused = results[0];
    const guard::Result& called = results[1];
    c.log(fmt("stub %s (%u bytes), both calls in one frame", hex8(guard::installStub(rt)).c_str(), guard::stubAllocSize()));
    c.log(fmt("stale handle %s: %s (%s); expected refused, stale", hex8(stale).c_str(), guard::statusName(refused.status), refused.reason.c_str()));
    c.log(fmt("player handle %s: %s, entry %s, r3 %llu; expected called, entry %s", hex8(p.handle).c_str(), guard::statusName(called.status),
              hex8(called.entry).c_str(), static_cast<unsigned long long>(called.r3), hex8(p.entry).c_str()));
    const Allegiance a = readAllegiance(mem);
    if (called.called && a.owned()) {
        const bool pc = allegiancePair(a.bits, 0, 1);
        c.log(fmt("factions_are_allied(0, 1) = %llu; the PC read of the bitset says %d", static_cast<unsigned long long>(called.r3), pc ? 1 : 0));
    }
    const bool ok = refused.status == guard::Status::Refused && refused.failed == guard::kFailIdentity && called.called && called.entry == p.entry;
    c.log(ok ? "verdict: guard stub works on the console (refuses stale handles, calls for live ones)" : "verdict: guard stub did NOT behave as expected");
}

}   

BR_FEATURES(world_checks) {
    auto probe = [&](const char* id, const char* label, const char* help, void (*run)(Ctx&), const char* badge = "") {
        r.action({.id = id, .page = Page::Testing, .tab = kPortChecksTab, .section = kSection, .label = label, .help = help, .run = run, .badge = badge});
    };
    probe("world.check.readers", "Shared Readers: Pools, Tags, Factions",
          "Reads the sObject / player / attachment pool headers, the tag table, the globals registry and the faction allegiance block. "
          "Expected in an activity: every pool 'ok', ~234 registered tag pages, ownership proven.",
          probeReaders);
    probe("world.check.player_chain", "Local Player Chain (core.player)",
          "Resolves descriptor -> element +0x84 -> sObject -> player row. Expected: element +0x84 == sObject +0xC == player row +0x54.",
          probePlayerChain);
    probe("world.check.player_position", "Player Position +0x2810 (Teleport Target)",
          "Stand still in an activity. Compares element +0x2810 (likely) with the engine position sObject +0xB0 (verified). "
          "Expected: distance below 0.5. Teleport and spawn-at-player use +0x2810.",
          probePlayerPosition, kBadgeNeedsCheck);
    probe("world.check.transform", "Player sObject Transform and Scale (+0x80..+0xBC)",
          "Logs both transform copies. Expected: +0x90 == +0xB0 and scale +0x9C == 1.0 for the player (move.scale writes +0x9C).",
          probeTransform, kBadgeNeedsCheck);
    probe("world.check.tag_ids", "Tag IDs: Catalogue vs Live Types",
          "Samples resident entity definitions and compares their live type byte with the (Xbox) catalogue's type for the same id. "
          "Expected: 100% match, which confirms PS3 tag ids equal the Xbox ids (x.catalog_translation).",
          probeTagIds, kBadgeNeedsCheck);
    probe("world.check.guard", "Guard Stub on the Console (Same-Frame Handle Check)",
          "Runs the shared guard stub twice in one frame on factions_are_allied (a read-only native): with a stale handle (expected: "
          "refused) and with the player's handle (expected: called). Writes only Blackrock's own cave and arena.",
          probeGuard, kBadgeNeedsCheck);
}

}   
