 
 
 
 
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "app/registry.h"
#include "core/error.h"
#include "core/runtime.h"
#include "features/hopons_shared.h"
#include "game/hopons_addr.h"
#include "game/hopons_ai.h"
#include "game/hopons_attach.h"
#include "game/hopons_catalog.h"
#include "game/hopons_scripts.h"
#include "game/hopons_sequence.h"
#include "game/hopons_swap.h"
#include "game/names_catalog.h"
#include "game/world.h"

namespace brps3 {
namespace {

using namespace hopons;

constexpr const char* kSection = "Hop-ons & encounters";

std::string fmt(const char* f, ...) {
    char buf[600];
    va_list a;
    va_start(a, f);
    std::vsnprintf(buf, sizeof buf, f, a);
    va_end(a);
    return buf;
}

std::string h8(uint32_t v) { return world::hex8(v); }

 
 
ScriptScan freshScan(Ctx& c) {
    auto& S = hoponsui::scanState();
    {
        std::lock_guard<std::mutex> lock(S.m);
        if (S.have && hoponsui::nowSeconds() - S.at < 120) {
            c.log("(using the discovery from " + hoponsui::ago(hoponsui::nowSeconds() - S.at) + ")");
            return S.scan;
        }
    }
    hoponsui::discover(c);
    std::lock_guard<std::mutex> lock(S.m);
    return S.scan;
}

 

void probeFingerprints(Ctx& c) {
    size_t total = 0, bad = 0;
    for (const auto& [name, set] : allFingerprintSets()) {
        size_t okHere = 0;
        for (const auto& [addr, hexText] : *set) {
            ++total;
            const std::string want = hexText;
            const auto got = c.rt().read(addr, uint32_t(want.size() / 2));
            const std::string have = hopons::hexBytes(got);
            if (have == want) {
                ++okHere;
            } else {
                ++bad;
                c.log(fmt("  %s %s: live %s, expected %s", name, h8(addr).c_str(), have.c_str(), want.c_str()));
            }
        }
        c.log(fmt("%s: %zu of %zu code fingerprints match", name, okHere, set->size()));
    }
    c.log(bad ? fmt("verdict: %zu of %zu fingerprints DIFFER (wrong build or patched code)", bad, total)
              : fmt("verdict: all %zu fingerprints match the supported build", total));
}

 

void probeAccessors(Ctx& c) {
    c.require(world::kFingerprints, "Hop-ons check");
    world::RuntimeReader mem(c.rt());
    std::string why;
    const auto pool = readAccessorPoolInfo(mem, &why);
    if (!pool) {
        c.log(why);
        c.log("verdict: the component accessor pool is not set up (load into an activity)");
        return;
    }
    c.log(fmt("accessor pool %s: base %s stride 0x%X capacity %u mask %s", h8(addr::ACCESSOR_POOL).c_str(), h8(pool->base).c_str(), pool->stride, pool->capacity,
              h8(pool->mask).c_str()));
    const auto table = world::readAccessorTable(mem);
    size_t live = 0, sampled = 0, selfOk = 0, regOk = 0;
    for (uint32_t i = 0; i < table.count() && sampled < 12; ++i) {
        const world::Accessor a = table.at(i);
        if (!a.allocated() || a.size() < 0x20) continue;
        ++live;
        if (i % 7) continue;    
        const uint32_t body = a.addr - (a.backOffset & pool->mask);
        const auto b = mem.tryBytes(body + 0x14, 12);
        if (!b) continue;
        ++sampled;
        const uint32_t self = b->u32(0), target = b->u32(8);
        const bool indexOk = (self & 0x1FFF) == i;
        const bool registered = indexOk && registrationOf(mem, self) == addr::ACCESSOR_POOL;
        selfOk += indexOk;
        regOk += registered;
        c.log(fmt("  #%u body %s: +0x14 %s (%s), +0x1C %s, owner +0xC %s", i, h8(body).c_str(), h8(self).c_str(),
                  indexOk ? (registered ? "own handle, page registered to the pool" : "own index, page NOT registered") : "not this accessor", h8(target).c_str(),
                  h8(a.identity).c_str()));
    }
    c.log(fmt("%zu live accessors; %zu sampled: %zu hold their own handle at body +0x14, %zu registered through 0x1896D4C", live, sampled, selfOk, regOk));
    c.log(sampled && regOk * 2 >= sampled ? "verdict: the HopAccessor layout (body +0x14 own handle, +0x1C target) holds on PS3"
                                          : "verdict: few accessors match the HopAccessor layout here (components without a target are normal; send the report)");
}

 

void probeDefinitions(Ctx& c) {
    c.require(world::kFingerprints, "Hop-ons check");
    world::RuntimeReader mem(c.rt());
    TagReader tags(mem);
    tags.loadAllPages();
    const auto& cat = hopCatalog();
    std::map<uint32_t, std::vector<const HopEntry*>> byPage;
    for (const HopEntry& e : cat.all()) byPage[world::tagPage(names::toPs3Tag(e.tag))].push_back(&e);
    size_t pages = 0, resident = 0, checked = 0, typeOk = 0;
    for (const auto& [page, list] : byPage) {
        if (pages >= 10 || checked >= 12 || c.cancelled()) break;
        if (page >= tags.table().pageCount) continue;
        const world::TagPage p = tags.page(page);
        if (!p.live() || p.itemStride != world::kPackageItemStride) continue;
        ++pages;
        const auto items = mem.tryBytes(p.items, std::min<uint32_t>(p.itemsEnd - p.items, world::kPackageItems * 16));
        if (!items) continue;
        int perPage = 0;
        for (const HopEntry* e : list) {
            const uint32_t off = world::tagIndex(e->tag) * 16;
            if (!items->has(off, 4) || items->u32(off) != world::kEntityClass) continue;
            ++resident;
            if (perPage++ >= 2 || checked >= 12) continue;
            const HopDefinition d = checkDefinition(mem, tags, names::toPs3Tag(e->tag));
            ++checked;
            typeOk += d.status == HopResult::Ok && d.type == e->type;
            c.log(fmt("  %s %s: %s, definition %s type %u (catalogue %u)", h8(e->tag).c_str(), e->name.empty() ? hopTypeName(e->type) : e->name.c_str(),
                      hopResultText(d.status), h8(d.definition).c_str(), d.type, e->type));
        }
    }
    c.log(fmt("%zu live pages hold catalogued hop-ons; %zu resident; %zu checked, %zu with the catalogue type", pages, resident, checked, typeOk));
    c.log(checked == 0 ? "verdict: no catalogued hop-on is loaded here (go where hop-ons exist, e.g. an activity with abilities loaded)"
          : typeOk == checked ? "verdict: hop-on definitions resolve like Xbox (class record, relative definition, type byte)"
                              : "verdict: some definitions do not match the catalogue type (send the report)");
}

 

void probeSelfTarget(Ctx& c) {
    c.require(world::kFingerprints, "Hop-ons check");
    world::RuntimeReader mem(c.rt());
    const auto r = world::findLocalPlayer(mem);
    if (!r.player) {
        c.log(std::string("no local player: ") + r.message);
        c.log("verdict: load into an activity first");
        return;
    }
    const auto& p = *r.player;
    c.log(fmt("self target *(*(0x1B29450)+4)+0x84 = %s; sObject %s type %u (%s), flags %s", h8(p.handle).c_str(), h8(p.entry).c_str(), p.object.type,
              world::typeName(p.object.type), h8(p.object.flags).c_str()));
    std::string why;
    const bool biped = checkCompatible(TYPE_BIPED_ATTACHMENT, p.object.type, &why) == HopResult::Ok;
    c.log(std::string("biped attachments on you: ") + (biped ? "allowed" : why));
    const auto scan = world::readObjects(mem);
    size_t weapons = 0;
    for (const auto& o : world::childrenOf(scan, p.handle))
        if (o.live() && o.type == world::TYPE_WEAPON) {
            ++weapons;
            c.log(fmt("  your weapon %s (sObject %s)", h8(o.handle).c_str(), h8(o.addr).c_str()));
        }
    c.log(fmt("expected: a live biped and 1..3 weapons parented to it; found %zu weapons", weapons));
    c.log(p.object.type == world::TYPE_BIPED && weapons ? "verdict: self and weapon targets resolve" : "verdict: self / weapon target NOT as expected (send the report)");
}

 

void probeScan(Ctx& c) {
    c.require(world::kFingerprints, "Encounters check");
    const auto& cat = encounterCatalog();
    if (!cat.error.empty()) throw Error(cat.error);
    world::RuntimeReader mem(c.rt());
    hoponsui::discover(c);    
    ScriptScan scan;
    {
        auto& S = hoponsui::scanState();
        std::lock_guard<std::mutex> lock(S.m);
        scan = S.scan;
    }
    c.log(fmt("%u pages with 12-byte items, %u candidates, %u unmatched, %u source-binding mismatches, %zu verified instances (%llu reads)", scan.pages,
              scan.candidates, scan.unmatched, scan.sourceMismatch, scan.instances.size(), static_cast<unsigned long long>(scan.reads)));
    std::map<std::string, size_t> kinds;
    for (const auto& i : scan.instances) ++kinds[cat.kind(i.ref.def.cls)];
    for (const auto& [k, n] : kinds) c.log(fmt("  %-24s %zu", k.c_str(), n));
    size_t joined = 0, header = 0, sizeOk = 0, shown = 0;
    for (const auto& i : scan.instances) {
        if (!i.joined) continue;
        ++joined;
        header += i.headerVerified;
        const uint32_t want = expectedInputSize(i.ref.def);
        sizeOk += want && i.input.size() == want;
        if (shown++ < 4) {
            c.log(fmt("  %s %s: upstream %s record %s", cat.kind(i.ref.def.cls).c_str(), i.ref.def.name.c_str(), h8(i.upstream).c_str(), h8(i.record).c_str()));
            c.log("    record +00: " + hopons::hexBytes(std::vector<uint8_t>(i.recordBytes.begin(), i.recordBytes.begin() + std::min<size_t>(0x30, i.recordBytes.size()))));
            c.log(fmt("    expected +0 slot %s/%s schemas %s/%s; +0x24 size %zu (want %u), %s", h8(i.ref.def.slotHash).c_str(), h8(i.ref.def.slotId).c_str(),
                      h8(i.ref.def.outputSchema).c_str(), h8(i.ref.def.inputSchema).c_str(), i.input.size(), want, i.inputStatus.c_str()));
        }
    }
    c.log(fmt("joined %zu instances: %zu with the Xbox record header at +0, %zu with the expected input size from +0x24", joined, header, sizeOk));
    if (scan.instances.empty()) c.log("verdict: no live instances (load into an activity with encounters)");
    else if (joined && header == joined) c.log("verdict: catalogue keys and the joinInput record layout (+0 header, +0x24 size, +0x28 offset, +0x2C context) confirmed");
    else if (joined) c.log("verdict: instances match the catalogue; the record header / size offsets differ from the expectation (send the record bytes above)");
    else c.log("verdict: instances match the catalogue, but no timer / spawner / squad input joined (send the report)");
}

 

void probeActors(Ctx& c) {
    c.require(world::kFingerprints, "Squads check");
    world::RuntimeReader mem(c.rt());
    const auto pool = world::readPool(mem, addr::ACTOR_POOL);
    c.log(fmt("actor pool %s: base %s stride 0x%X (expect 0x28E0) capacity %u identity +0x%X / 0x%X namespace %s", h8(addr::ACTOR_POOL).c_str(), h8(pool.base).c_str(),
              pool.stride, pool.capacity, pool.identityOffset, pool.identityStride, h8(pool.nameSpace).c_str()));
    size_t found = 0, objects = 0, squads = 0, rosters = 0;
    for (uint32_t i = 0; i < std::min<uint32_t>(pool.capacity, 64) && found < 6 && !c.cancelled(); ++i) {
        std::string why;
        const auto slot = poolSlot(mem, addr::ACTOR_POOL, i, &why);
        if (!slot) continue;
        ++found;
        const auto a = readActorObject(mem, slot->handle, &why);
        if (!a) {
            c.log(fmt("  actor %s: %s", h8(slot->handle).c_str(), why.c_str()));
            continue;
        }
        ++objects;
        c.log(fmt("  actor %s: object %s (%s), squad %s +%X, spawner %s +%X", h8(a->actor).c_str(), h8(a->object).c_str(), world::typeName(a->objectType),
                  h8(a->squadTag).c_str(), a->squadOffset, h8(a->ownerTag).c_str(), a->ownerOffset));
         
        if (a->squadTag == 0xFFFFFFFFu) continue;
        ++squads;
        if (const auto r = captureSquadRoster(mem, a->squadTag, a->squadOffset, a->actor, a->generation, why)) {
            ++rosters;
            c.log(fmt("    squad controller %s: roster %u, this actor is entry %u with its generation", h8(r->root).c_str(), r->count, r->selected));
        } else {
            c.log("    squad roster: " + why);
        }
    }
    c.log(fmt("%zu allocated actors read, %zu with a live object at +0x30; %zu name a squad controller, %zu found in its roster", found, objects, squads, rosters));
    c.log(!found ? "verdict: no AI actors are allocated here (find enemies first)"
          : objects != found ? "verdict: some actors do not resolve their object (send the report)"
          : squads && rosters != squads
              ? "verdict: actors resolve, but some squad rosters do not list their actor (spawner removal will refuse; send the report)"
              : "verdict: actor handles reconstruct, +0x30 holds the object and the squad rosters list their actors (spawn.squads layout confirmed)");
}

 

void probeSquadReferences(Ctx& c) {
    c.require(world::kFingerprints, "Squads check");
    world::RuntimeReader mem(c.rt());
    const auto scan = freshScan(c);
    size_t spawners = 0, ok = 0;
    for (const auto& s : scan.instances) {
        if (s.ref.def.cls != cls::SPAWNER || spawners >= 4) continue;
        ++spawners;
        std::string why;
        const auto b = readSpawnerBinding(mem, s.ref, &why);
        c.log(fmt("spawner %s %s:", s.ref.def.name.c_str(), h8(s.ref.address).c_str()));
        if (!b) {
            c.log("  binding: " + why);
            continue;
        }
        c.log("  blob+binding+0x44: " + hopons::hexBytes(std::vector<uint8_t>(b->squadReference.begin(), b->squadReference.end())) + fmt(" member %u", b->member));
        uint32_t root = 0;
        for (const auto& l : inspectSquadReference(mem, s, scan, &root)) c.log("  " + l);
        ok += root != 0;
    }
    c.log(fmt("%zu spawners checked, %zu resolved to a live squad controller", spawners, ok));
    c.log(!spawners ? "verdict: no live spawners (load an encounter)"
          : ok ? "verdict: the spawner squad reference (+0x44) and the squad member array decode on PS3"
               : "verdict: no spawner reference resolved (send the report)");
}

 

void probeSwap(Ctx& c) {
    c.require(world::kFingerprints, "Enemy swap check");
    world::RuntimeReader mem(c.rt());
    const auto scan = freshScan(c);
    std::set<uint32_t> defs;
    for (const auto& s : scan.instances) {
        if (s.ref.def.cls != cls::SQUAD || defs.size() >= 3) continue;
        std::string why;
        if (const auto b = readSquadBinding(mem, s.ref, &why)) defs.insert(b->tag);
    }
    size_t structured = 0, matches = 0;
    for (uint32_t tag : defs) {
        std::string why;
        const auto d = readSquadDefinition(mem, tag, why);
        if (!d) {
            c.log(fmt("squad definition %s: %s", h8(tag).c_str(), why.c_str()));
            continue;
        }
        structured += d->structured;
        const auto* row = squadCatalog().find(tag);
        size_t same = 0;
        for (const auto& s : d->sites) same += s.catalogEntity && s.catalogEntity == s.entity;
        matches += row && same == row->sites.size() && same == d->sites.size();
        c.log(fmt("squad definition %s (%s): body %s size 0x%X, %zu marker sites, structure %s; %zu of %zu catalogue sites equal", h8(tag).c_str(),
                  row ? row->name.c_str() : "not catalogued", h8(d->body).c_str(), d->size, d->sites.size(), d->structured ? "verified" : d->structureNote.c_str(), same,
                  row ? row->sites.size() : size_t(0)));
        for (size_t i = 0; i < d->sites.size() && i < 4; ++i)
            c.log(fmt("  site +0x%X %d.%d.%d = %s (%s)", d->sites[i].offset, d->sites[i].member, d->sites[i].variant, d->sites[i].combatant, h8(d->sites[i].entity).c_str(),
                      names::tagLabel(d->sites[i].entity).c_str()));
    }
    c.log(defs.empty() ? "verdict: no live squad definitions (load an encounter, then run again)"
          : structured == defs.size() && matches == defs.size() ? "verdict: PS3 squad definitions decode like Xbox and match the catalogue (swap ready)"
          : structured == defs.size()                          ? "verdict: definitions decode; entity tags differ from the Xbox catalogue (ids or content differ)"
                                                               : "verdict: the squad structure did not validate (send the report)");
}

 

void probeClock(Ctx& c) {
    c.require(world::kFingerprints, "Sequence check");
    c.require(kRestartFingerprints, "Sequence check");
    world::RuntimeReader mem(c.rt());
    const auto scan = freshScan(c);
    size_t sequences = 0, clocks = 0, bundles = 0;
    for (const auto& s : scan.instances) {
        if (s.ref.def.cls != cls::SEQUENCE || sequences >= 3) continue;
        ++sequences;
        c.log(fmt("sequence %s %s:", s.ref.def.name.c_str(), h8(s.ref.address).c_str()));
        std::string why;
        if (const auto o = readClockOwner(mem, s.ref.def.slotHash, why)) {
            ++clocks;
            for (const auto& l : o->lines) c.log("  " + l);
        } else {
            c.log("  clock owner: " + why);
        }
        if (const auto b = readBundle(mem, s.ref, why)) {
            ++bundles;
            for (const auto& l : b->lines) c.log("  " + l);
        } else {
            c.log("  bundle: " + why);
        }
    }
    c.log(fmt("%zu sequences: %zu clock owners, %zu bundles resolved", sequences, clocks, bundles));
    c.log(!sequences ? "verdict: no live sequences (load an activity)"
          : clocks == sequences && bundles == sequences ? "verdict: the restart clock and bundle chains resolve on PS3 (incl. 0x1A1D4F0)"
                                                        : "verdict: part of the clock / bundle chain did not resolve (send the report)");
}

 

void probeAi(Ctx& c) {
    c.require(world::kFingerprints, "AI preflight check");
    c.require(kAiFingerprints, "AI preflight check");
    world::RuntimeReader mem(c.rt());
    const auto p = runAiPreflight(mem);
    for (const auto& l : p.lines) c.log(std::string(checkStatusText(l.status)) + ": " + l.label + ": " + l.detail);
    c.log(fmt("%zu ok, %zu to check, %zu failed", p.ok, p.warnings, p.failures));
    c.log(p.failures ? "verdict: some AI / placement anchors differ (send the report)"
                     : "verdict: anchors and schemas resolve; confirm the tag heap / pool lines look plausible (offsets +0x30..+0x7A)");
}

}   

BR_FEATURES(hopons_checks) {
    auto probe = [&](const char* id, const char* label, const char* help, void (*run)(Ctx&), const char* badge) {
        r.action({.id = id, .page = Page::Testing, .tab = kPortChecksTab, .section = kSection, .label = label, .help = help, .run = run, .badge = badge});
    };
    probe("hopons.check.fingerprints", "Code Fingerprints (Hop-Ons, Timers, Sequences, Spawners, AI)",
          "Reads the first bytes of every native these features call or depend on. Expected: all match.", probeFingerprints, "");
    probe("hopons.check.accessors", "Accessor Layout for Hop-On Receipts (spawn.hopons)",
          "Samples live component accessors. Expected: body +0x14 holds the accessor's own handle and its page is registered to 0x1A21CB0.", probeAccessors, "");
    probe("hopons.check.definitions", "Hop-On Definitions Resolve (spawn.hopons)",
          "Checks a few resident catalogued hop-ons like Apply does (class record, relative definition, type byte). Expected: type equals the catalogue.",
          probeDefinitions, "");
    probe("hopons.check.self_target", "Self and Weapon Targets (P5)",
          "Resolves your sObject (element +0x84) and the weapons parented to it. Expected: a live biped with 1..3 weapons.", probeSelfTarget, kBadgeNeedsCheck);
    probe("hopons.check.scan", "Encounter Scan and the Upstream Input Record (enc.scripts)",
          "Runs a discovery and prints the upstream records of joined instances. Expected: record +0 = {slot hash, slot id, schemas} and the size at +0x24 "
          "(timers 36 bytes). Run in an activity.",
          probeScan, kBadgeNeedsCheck);
    probe("hopons.check.actors", "AI Actors (spawn.squads)",
          "Reads allocated actors of pool 0x1A1E160 and the squad rosters they name. Expected: the handle reconstructs from the identity word, +0x30 is a "
          "live object and the squad controller's roster (+0x1D8 / +0x1DC) lists the actor with its generation.",
          probeActors,
          kBadgeNeedsCheck);
    probe("hopons.check.squad_refs", "Spawner Squad References (enc.scripts)",
          "Decodes blob+binding+0x44 of live spawners and resolves it through the reference manager. Expected: a live squad controller with the member "
          "index in range.",
          probeSquadReferences, kBadgeNeedsCheck);
    probe("hopons.check.swap", "Squad Definitions for Enemy Swap (swap.enemy)",
          "Reads the squad definitions of live squads. Expected: the member structure verifies and the 80800406 sites equal the Xbox catalogue.", probeSwap,
          kBadgeNeedsCheck);
    probe("hopons.check.clock", "Sequence Clock and Bundle (enc.sequence_dev)",
          "Resolves the clock owner and the client bundle of live sequences. Expected: both chains resolve; the bundle handle 0x1A1D4F0 names \"client bundle "
          "instances\".",
          probeClock, kBadgeNeedsCheck);
    probe("hopons.check.ai", "AI Spawn Context Anchors (spawn.ai_preflight)",
          "The read-only development preflight. Expected: anchors and schemas ok; tag heap and pool values plausible.", probeAi, kBadgeNeedsCheck);
}

}   
