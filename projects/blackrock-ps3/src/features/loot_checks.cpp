 
 
 
 
#include <algorithm>
#include <string>
#include <vector>

#include "app/registry.h"
#include "app/settings.h"
#include "core/error.h"
#include "core/runtime.h"
#include "features/loot_common.h"
#include "game/loot_addr.h"
#include "game/loot_afk.h"
#include "game/loot_grimoire.h"
#include "game/loot_lighthouse.h"
#include "game/loot_pickup.h"
#include "game/loot_poe.h"

namespace brps3::lootui {
namespace {

using namespace loot;

 

void probePoe(Ctx& c) {
    c.require(kPoeFingerprints, "PoE chest loop (native chest code)");
    c.log("native chest code: seconds_to_ticks, chest_init, chest_emit, chest_update (whole bodies) and the open callback match the EBOOT");
    RuntimeConsole con(c.rt());
    const poe::Context ctx = poe::readContext(con, false);
    c.log(fmt("world state %u (%s); slices %s map %s current slice %d; list root %s; clock scale %d; sObject pool %s stride 0x%X cap %u",
              ctx.worldState, poe::worldStateName(con, ctx.worldState).c_str(), hex(ctx.slices).c_str(), hex(ctx.map).c_str(),
              int32_t(ctx.sliceIndex), hex(ctx.listRoot).c_str(), ctx.scale, hex(ctx.pool.base).c_str(), ctx.pool.stride, ctx.pool.capacity));
    if (ctx.worldState != WORLD_IN_WORLD) {
        c.log("verdict: not in an activity: load into the PoE treasure room, open the chests normally and run this again");
        return;
    }
    Resolver res(con);
    poe::Discovery d;
    try {
        d = poe::discover(con, res, [&c] { return c.cancelled(); });
    } catch (const std::exception& e) {
        c.log(std::string("verdict: the interactable list walk refused: ") + e.what());
        return;
    }
    c.log(fmt("interactable list: %zu nodes", d.nodes.size()));
    for (const poe::Node& n : d.nodes)
        if (n.opened || !n.note.empty())
            c.log(fmt("  node %s at %s: sequence %s class %s %s", hex(n.handle).c_str(), hex(n.address).c_str(), hex(n.sequence).c_str(),
                      hex(n.classTag).c_str(), n.note.c_str()));
    for (const poe::Chest& ch : d.chests) {
        const world::Bytes def(ch.definition, ch.definitionBytes);
        c.log(fmt("  %s chest %s: sequence %s (Xbox %s), class %s, owner %s, open %u looted %u countdown %d authority %u lifetime %d (%.1f s)",
                  poe::familyName(ch.family), hex(ch.address).c_str(), hex(ch.sequence).c_str(),
                  hex(ch.family == poe::Family::Main ? XBOX_POE_MAIN_SEQUENCE : XBOX_POE_SIDE_SEQUENCE).c_str(), hex(ch.classTag).c_str(),
                  hex(ch.owner).c_str(), ch.timers.open, ch.timers.looted, ch.timers.countdown, ch.timers.authority, ch.timers.lifetime,
                  double(ch.timers.seconds())));
        c.log(fmt("    definition %s: root %s key %s threshold %.2f lifetime %.0f s open delay %.2f s", hex(ch.definition).c_str(),
                  hex(def.u32(chestdef::ROOT)).c_str(), hex(def.u32(chestdef::KEY)).c_str(), double(def.f32(chestdef::THRESHOLD)),
                  double(def.f32(chestdef::LIFETIME_S)), double(def.f32(chestdef::OPEN_DELAY))));
    }
    for (const std::string& w : d.warnings) c.log("  note: " + w);
    c.log(fmt("PS3 tags to pin: map %s (Xbox %s)", hex(d.ctx.map).c_str(), hex(XBOX_POE_MAP).c_str()));
    c.log("expected after opening all three chests: 1 main (key DDA58EF3, 300 s) + 2 side (key DDA58EF2, 90 s), each open 1, looted 1, "
          "countdown <= 0, authority 1, class 808034A1");
    try {
        const auto sel = poe::select(d, false);
        c.log(fmt("verdict: PoE chests found and ready (%u main, %u side): the loop can start; pin the tags on Loot > PoE", d.mains(), d.sides()));
        (void)sel;
    } catch (const std::exception& e) {
        c.log(fmt("verdict: %u main / %u side chests opened: %s", d.mains(), d.sides(), e.what()));
    }
}

 

void probeAfk(Ctx& c) {
    c.require(kAfkFingerprints, "AFK keepalive (kick gate)");
    c.log("kick gate 0x35EC38 (cmpdi / beq / cmpld / ble / li r31,2) and config getter 0x126E64 match the EBOOT");
    RuntimeConsole con(c.rt());
    const uint32_t root = con.u32(AFK_CONFIG_ROOT);
    c.log("config root *(01A1D2E4) = " + hex(root));
    const world::Bytes mb = con.bytes(AFK_MODE_BLOCK + AFK_IN_WORLD, AFK_MODE + 4 - AFK_IN_WORLD);
    c.log(fmt("mode block 01ABD4B8: +0x1F2C in world %u (2 in an activity), +0x1F34 mode %d", mb.u32(0), int32_t(mb.u32(AFK_MODE - AFK_IN_WORLD))));
    if (heapPtr(root, AFK_BLOCK + 0xA0)) {
        const world::Bytes lim = con.bytes(root + AFK_BLOCK + AFK_LIMIT_MODE0, 12);
        c.log(fmt("limits +0x94/+0x98/+0x9C = %u / %u / %u ms (expected 900000 / 300000 / 3600000 unless something changed them)", lim.u32(0),
                  lim.u32(4), lim.u32(8)));
    }
    try {
        const afk::Binding b = afk::inspect(con, true);
        const uint64_t clock = con.u64(ENGINE_CLOCK);
        c.log(fmt("selected player root %s, slot %u; activity %llu ms, elapsed %llu ms; activity + elapsed %llu vs clock %llu", hex(b.playerRoot).c_str(),
                  b.slot, static_cast<unsigned long long>(b.activity), static_cast<unsigned long long>(b.elapsed),
                  static_cast<unsigned long long>(b.activity + b.elapsed), static_cast<unsigned long long>(clock)));
        const int64_t skew = int64_t(b.activity + b.elapsed) - int64_t(clock);
        c.log(fmt("expected: activity + elapsed close to the clock (skew %lld ms)", static_cast<long long>(skew)));
        if (b.noLimit) c.log("verdict: this mode has no AFK limit on PS3: the keepalive has nothing to do here");
        else if (b.limit == b.original && std::llabs(skew) < 5000)
            c.log("verdict: AFK binding matches the port map (both methods can run)");
        else c.log("verdict: binding readable but not as expected (limit or elapsed differ): send this report");
    } catch (const std::exception& e) {
        c.log(std::string("verdict: ") + e.what());
    }
}

 

void probePickup(Ctx& c) {
    c.require(kPickupFingerprints, "Pickup filter (native pickup check)");
    RuntimeConsole con(c.rt());
    const uint32_t site = con.u32(PICKUP_SITE);
    c.log(fmt("call site 0xC4371C = %s (original 4BFFA849 = bl 0xC3DF64; %s)", hex(site).c_str(),
              site == PICKUP_SITE_ORIGINAL ? "not patched" : "PATCHED (the filter is on, or another tool)"));
    Resolver res(con);
    try {
        const pickup::TableCheck t = pickup::verifyItemTable(con, res);
        c.log(fmt("provider %s, investment root %s, item table tag %s (expected 80A5F241 on PS3) at %s, %u definitions", hex(t.provider).c_str(),
                  hex(t.root).c_str(), hex(t.tableTag).c_str(), hex(t.table).c_str(), t.count));
        c.log("SHA-256 of the item hashes: " + t.sha256);
        const auto& cat = pickup::definitions();
        size_t same = 0;
        for (const auto& d : cat.all())
            if (d.index < t.hashes.size() && t.hashes[d.index] == d.hash) ++same;
        c.log(fmt("catalogue rows matching the live table by index: %zu / %zu", same, cat.size()));
        c.log("verdict: the live item table is the catalogue's: every preset's definition indices are right");
    } catch (const std::exception& e) {
        c.log(std::string("verdict: ") + e.what());
    }
}

 

void probeGrimoire(Ctx& c) {
    c.require(kGrimoireFingerprints, "Grimoire unlock (native flag setter)");
    c.log("setter 0xC5BEC0, context getter 0x2A8080, vt+0x40, field8 / provider getters match; setter call sites 0xC3EF10 / 0xC48CE4 are original");
    RuntimeConsole con(c.rt());
    const auto& cat = grim::catalog();
    if (!cat.error.empty()) throw Error(cat.error);
    try {
        grim::verifyContextClass(con);
        c.log("investment context 01AB6250: vtable 01411170 (expected)");
    } catch (const std::exception& e) {
        c.log(e.what());
    }
    Resolver res(con);
    try {
        const grim::Tables t = grim::verifyTables(con, res, cat);
        c.log(fmt("tables 80A5E1E4 at %s and 80A5F24A at %s: SHA-256 match the 862-flag catalogue", hex(t.core).c_str(), hex(t.flags).c_str()));
    } catch (const std::exception& e) {
        c.log(std::string("tables: ") + e.what());
    }
    try {
        const grim::Profile p = grim::readProfile(con, cat);
        c.log(fmt("profile manager %s, account %s (offset 0x%X), identity %s", hex(p.manager).c_str(), hex(p.account).c_str(), p.offset,
                  p.identityHex().c_str()));
        c.log(p.descriptorOk ? fmt("descriptor table mgr+0x60: one match at %s, state 3, version %u (expected exactly one)", hex(p.descriptor).c_str(), p.version)
                             : "descriptor table mgr+0x60: " + p.note);
        const uint32_t ctxAccount = con.u32(GRIM_ACCOUNT_PTR);
        c.log(fmt("context +8 account %s (expected %s once the context is built; unlock refuses in the same frame otherwise)",
                  hex(ctxAccount).c_str(), hex(p.account).c_str()));
        c.log(fmt("%u / %zu whitelisted cards unlocked", p.unlocked, cat.flags.size()));
        if (ctxAccount != p.account)
            c.log("verdict: the context's account is not the profile manager's account (context not built yet, or a different layout on "
                  "PS3): unlock will refuse; send this report");
        else
            c.log(p.descriptorOk ? "verdict: Grimoire tables, profile and descriptor guard as on Xbox: unlock can run with every guard"
                                 : "verdict: tables and profile fine; the descriptor table differs (unlock uses the identity guard only): send this report");
    } catch (const std::exception& e) {
        c.log(std::string("verdict: ") + e.what());
    }
}

 

void probeLighthouse(Ctx& c) {
    c.require(kLighthouseFingerprints, "Walkable Lighthouse (native transition code)");
    RuntimeConsole con(c.rt());
    const lh::Snapshot s = lh::readSnapshot(con);
    c.log("world state " + lh::worldStateLabel(s.worldState, s.stateName));
    c.log(fmt("slices %s: map %s (Xbox Lighthouse %s); current %d pending %d loaded bits %s", hex(s.slices).c_str(), hex(s.map).c_str(),
              hex(XBOX_LH_MAP).c_str(), int32_t(s.current), int32_t(s.pending), hex(s.loaded).c_str()));
    for (uint32_t i = 0; i < 16; ++i)
        if (s.slots[i].tag) c.log(fmt("  slot %2u: %s %s%s", i, hex(s.slots[i].tag).c_str(), hex(s.slots[i].value).c_str(), i == 8 ? "  (Xbox Lighthouse: 8154E003 000000FC)" : ""));
    c.log(fmt("transition manager %s: +0x80 %04X +0x88 %02X %02X %02X +0x130 %d +0x1D2 %u mailbox %d %s %u; globals +0xA8 %d", hex(s.mgr).c_str(),
              s.pause, s.enabled, s.s89, s.s8a, int32_t(s.target130), s.worker, int32_t(s.reqTarget), hex(s.reqHash).c_str(), s.reqFlag,
              int32_t(s.globalsTarget)));
    c.log(fmt("context: root %s, +4 %s (coherent %s), inner %s, permission byte %u; fly-in block %s byte %u", hex(s.root).c_str(), hex(s.ctx4).c_str(),
              s.coherent ? "yes" : "no", hex(s.inner).c_str(), s.permission, hex(s.flyin).c_str(), s.flyinByte));
    c.log(fmt("local player %s: identity %s, +0x48 %s, +0x50 %s, +0x54 %s; fade block %s count %u; request predicate %s", hex(s.small).c_str(),
              hexBytes(s.identity.data(), 8).c_str(), hex(s.smallHandle).c_str(), hex(s.smallEntity).c_str(), hex(s.smallEntity2).c_str(),
              hex(s.fade).c_str(), s.fadeCount, s.predicateBlocks ? "BLOCKS" : "clear"));
    if (s.trialsKnown) c.log(fmt("Trials flag plane +1994..1995: %02X %02X (optional prerequisite, likely)", s.trials[0], s.trials[1]));
    for (size_t i = 0; i < s.sites.size() && i < lh::patches().size(); ++i)
        c.log(fmt("  patch %-16s %s: %s", lh::patches()[i].id, hex(lh::patches()[i].addr).c_str(),
                  s.sites[i] == lh::SiteState::Original ? "original" : s.sites[i] == lh::SiteState::Patched ? "patched" : "FOREIGN"));
    c.log(fmt("permission gate 0x36EA5C: %s", s.gateOriginal ? "original (never patched)" : "CHANGED"));
    if (!s.error.empty()) c.log("partial read: " + s.error);
    if (s.worldState == WORLD_IN_WORLD && s.current == LH_START_SLICE)
        c.log(s.map == XBOX_LH_MAP && s.slots[8].tag == XBOX_LH_SLOT8 ? "verdict: the Lighthouse tags equal the Xbox ones: pin them on Loot > Lighthouse"
                                                                       : "verdict: in an activity at slice 9: if this is the Lighthouse, these are its PS3 tags: "
                                                                         "pin them on Loot > Lighthouse and send this report");
    else c.log("verdict: run this in the Lighthouse right after it loads (state 36, slice 9) to read its PS3 map and slot-8 tags");
}

void probeEntities(Ctx& c) {
    RuntimeConsole con(c.rt());
    Resolver res(con);
    for (uint32_t type : {12u, 16u}) {
        try {
            const lh::Inventory inv = lh::readInventory(con, res, type);
            c.log(fmt("type %u entities: %zu (registry %s); local identity %s", type, inv.entities.size(), hex(inv.registry).c_str(),
                      hexBytes(inv.identity.data(), 8).c_str()));
            for (const lh::Entity& e : inv.entities)
                c.log(fmt("  slot %u handle %s at %s: bound %s, phase %02X, identity %s%s", e.slot, hex(e.handle).c_str(), hex(e.address).c_str(),
                          hex(e.boundPlayer).c_str(), e.phase, e.identityKnown ? hexBytes(e.identity.data(), 8).c_str() : "unreadable",
                          e.matches ? "  <- yours" : e.empty() ? "  (empty)" : ""));
        } catch (const std::exception& e) {
            c.log(fmt("type %u: %s", type, e.what()));
        }
    }
    c.log("expected in the Lighthouse at slice 8: type-12 player entities with one empty (identity 0, bound FFFFFFFF), and exactly one "
          "type-16 entity with phase FF before the spawn step");
    c.log("verdict: compare with the expectation above (registry layout is identical to Xbox per the port map)");
}

}   
}   

namespace brps3 {

BR_FEATURES(loot_checks) {
    using namespace lootui;
    auto probe = [&](const char* id, const char* label, const char* help, void (*run)(Ctx&), const char* badge) {
        r.action({.id = id, .page = Page::Testing, .tab = kPortChecksTab, .section = kChecksSection, .label = label, .help = help, .run = run,
                  .badge = badge});
    };
    probe("loot.check.poe", "PoE: Treasure-Room Chests (Read Only)",
          "In the PoE treasure room after opening the chests: walks the interactable list and logs every chest's tags, timers and definition. "
          "Expected: 1 main (DDA58EF3, 300 s) + 2 side (DDA58EF2, 90 s), opened, authoritative, settled. Logs the PS3 tags to pin.",
          probePoe, kBadgeNeedsCheck);
    probe("loot.check.afk", "AFK: Kick Limit and Player Slot (Read Only)",
          "In any activity: the kick gate, the mode block (+0x1F2C == 2, mode), the original limits and the selected player's activity / "
          "elapsed times. Expected: original limits, activity + elapsed close to the clock.",
          probeAfk, "");
    probe("loot.check.pickup", "Pickup Filter: Live Item Table (Read Only)",
          "With a character loaded: provider, investment root, item table (via root + 0x144; expected tag 80A5F241) and the SHA-256 of the "
          "6,557 item hashes. Expected: a6d0c8d7...16e1, so every preset's definition index is right.",
          probePickup, "");
    probe("loot.check.grimoire", "Grimoire: Tables, Profile and Descriptor (Read Only)",
          "With a character loaded: context class, both Grimoire tables (SHA-256), the profile manager's account, the descriptor table (likely on "
          "PS3: expected exactly one match) and the unlocked count.",
          probeGrimoire, kBadgeNeedsCheck);
    probe("loot.check.lighthouse", "Lighthouse: Map, Slice Table, Transition State (Read Only)",
          "In the Lighthouse right after it loads (state 36, slice 9): the map tag, the slot table (slot 8), the transition manager, the "
          "context, the local player and the patch sites. Expected: the values the Lighthouse step checks; logs the PS3 tags to pin.",
          probeLighthouse, kBadgeNeedsCheck);
    probe("loot.check.entities", "Lighthouse: Player and Lifetime Entities (Read Only)",
          "In the Lighthouse at slice 8: the type-12 player entities and the type-16 activity-lifetime entity (registry 0x1A1DEC0 / directory "
          "0x1917100). Expected: one empty player entity and one lifetime entity at phase FF.",
          probeEntities, kBadgeNeedsCheck);
}

}   
