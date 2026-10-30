 
 
 
 
 
 
 
 
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "app/registry.h"
#include "core/error.h"
#include "core/runtime.h"
#include "features/player_shared.h"
#include "game/names_catalog.h"
#include "game/player_addr.h"
#include "game/player_editor.h"
#include "game/player_inspect.h"
#include "game/player_records.h"
#include "game/player_writes.h"
#include "game/world.h"

namespace brps3 {
namespace {

using world::Allegiance;
using world::childrenOf;
using world::factionOf;
using world::hex8;
using world::kFingerprints;
using world::LocalPlayer;
using world::ObjectScan;
using world::readAllegiance;
using world::readEnginePosition;
using world::readObjects;
using world::readSObject;
using world::requireLocalPlayer;
using world::RuntimeReader;
using world::SObject;
using world::TYPE_WEAPON;
using world::typeName;
using world::Vec3;
namespace sobj = world::sobj;
using player::Located;

std::string fmt(const char* f, ...) {
    char buf[512];
    va_list a;
    va_start(a, f);
    std::vsnprintf(buf, sizeof buf, f, a);
    va_end(a);
    return buf;
}

std::string v3(const Vec3& v) { return fmt("(%.2f, %.2f, %.2f)", double(v.x), double(v.y), double(v.z)); }

 
std::string words(const player::ComponentView& c, uint32_t addr) {
    std::string out;
    static const int offs[] = {-8, -4, 0, 4, 8, 0xC, 0x10};
    static const char* const names[] = {"tag", "id", "val", "copy", "sig", "hdl", "self"};
    for (size_t i = 0; i < 7; ++i) {
        const uint32_t a = addr + uint32_t(offs[i]);
        out += std::string(names[i]) + " " + (c.has(a, 4) ? hex8(c.u32(a)) : std::string("--------")) + (i < 6 ? "  " : "");
    }
    return out;
}

void logLocated(Ctx& c, const player::ComponentView& v, const char* name, uint32_t off, const Located& l) {
    if (l.ok) {
        c.log(fmt("  %-16s +%04X -> record +%04X via %-12s id %-6u tag %s value %s (%.6g) copy %s", name, off, l.recordOffset, player::viaName(l.via),
                  l.record.id, hex8(l.record.tag).c_str(), hex8(l.record.value).c_str(), double(l.record.f()), hex8(l.record.copy).c_str()));
        if (const player::FieldInfo* info = player::fieldInfo(l.record.id, l.record.tag))
            c.log(fmt("  %-16s   (the Xbox editor calls record id %u \"%s\")", "", l.record.id, info->name));
    } else {
        c.log(fmt("  %-16s +%04X -> NO RECORD: %s", name, off, l.why.c_str()));
        c.log(fmt("  %-16s   at plugin base   %s", "", words(v, v.pluginBase() + off).c_str()));
        if (v.pluginBase() != v.data) c.log(fmt("  %-16s   at accessor data %s", "", words(v, v.data + off).c_str()));
        c.log(fmt("  %-16s   at accessor + 4  %s", "", words(v, v.data + off + 4).c_str()));
    }
}

void logChain(Ctx& c, const player::Target& t) {
    const player::ComponentChain& k = t.chain;
    c.log(fmt("%s: sObject %s at %s, +0x78 %s; accessor %s flags %s back %s class %s", k.label().c_str(), hex8(k.ownerHandle).c_str(),
              hex8(k.ownerEntry).c_str(), hex8(k.handle).c_str(), hex8(k.accessor).c_str(), hex8(k.accFlags).c_str(), hex8(k.accBack).c_str(),
              hex8(k.accClass).c_str()));
    if (k.tagBase)
        c.log(fmt("  data (accessor resolve) %s, tag-table resolve %s: delta %+d (expected 0 or +4; the Xbox plugin offsets are the editor's - 4)",
                  hex8(k.data).c_str(), hex8(k.tagBase).c_str(), int(int64_t(k.tagBase) - int64_t(k.data))));
    else
        c.log(fmt("  data (accessor resolve) %s, tag-table resolve: did not resolve (the plugin base falls back to the accessor data)",
                  hex8(k.data).c_str()));
    c.log(fmt("  size %u bytes, %zu records in the snapshot", k.size, player::decodeRecords(t.view.bytes).size()));
}

LocalPlayer player0(Ctx& c, RuntimeReader& mem) {
    c.require(kFingerprints, "World readers");
    return requireLocalPlayer(mem);
}

 

void probePlayerRecords(Ctx& c) {
    RuntimeReader mem(c.rt());
    const LocalPlayer p = player0(c, mem);
    player::Resolver r(mem);
    if (!r.tagError().empty()) c.log("tag table: " + r.tagError());
    const auto obj = readSObject(mem, r.sobjectPool(), p.handle);
    if (!obj) throw Error("the player's sObject changed while reading: run again");
    std::string why;
    const auto t = r.component(*obj, false, &why);
    if (!t) {
        c.log(why);
        c.log("verdict: the player component did not resolve (send this report)");
        return;
    }
    logChain(c, *t);
    struct F {
        const char* name;
        uint32_t off;
    };
    std::vector<F> fields = {{"god mode", player::pc::GOD}, {"interact range", player::pc::INTERACT}, {"super", player::pc::SUPER},
                             {"grenade", player::pc::GRENADE}, {"melee", player::pc::MELEE}};
    static const char* const pickupNames[player::pc::PICKUP_COUNT] = {"pickup range 0", "pickup range 1", "pickup range 2", "pickup range 3",
                                                                     "pickup range 4", "pickup range 5", "pickup range 6"};
    for (uint32_t k = 0; k < player::pc::PICKUP_COUNT; ++k) fields.push_back({pickupNames[k], player::pc::PICKUP0 + k * 0x28});
    int ok = 0;
    std::map<std::string, int> vias;
    for (const F& f : fields) {
        const Located l = player::locateRecord(t->view, f.off);
        logLocated(c, t->view, f.name, f.off, l);
        if (l.ok) {
            ++ok;
            ++vias[player::viaName(l.via)];
        }
    }
    std::string how;
    for (const auto& [v, n] : vias) how += (how.empty() ? "" : ", ") + std::to_string(n) + " via " + v;
    c.log(fmt("expected: every field has a record (sig 9951DAEF at +8, self offset at +0x10); found %d of %zu (%s)", ok, fields.size(),
              how.empty() ? "none" : how.c_str()));
    if (ok == int(fields.size())) c.log("verdict: player component records confirmed: god mode, recharge and range switches will write");
    else if (ok) c.log(fmt("verdict: partial: %d of %zu player fields located; the rest stay idle (send this report)", ok, fields.size()));
    else c.log("verdict: no player records at the Xbox offsets: those switches stay idle on this build (send this report)");
}

 

void probeWeaponRecords(Ctx& c) {
    RuntimeReader mem(c.rt());
    const LocalPlayer p = player0(c, mem);
    player::Resolver r(mem);
    const ObjectScan scan = readObjects(mem);
    int weapons = 0, good = 0;
    for (const SObject& o : childrenOf(scan, p.handle)) {
        if (o.type != TYPE_WEAPON || !o.live()) continue;
        if (++weapons > 3) break;
        std::string why;
         
         
        const auto t = r.component(o, true, &why, false);
        if (!t) {
            c.log(why);
            continue;
        }
        logChain(c, *t);
        auto gateAt = [&](uint32_t a) { return t->view.has(a, 4) ? hex8(t->view.u32(a)) : std::string("--------"); };
        c.log(fmt("  gate [plugin base + 0x64] = %s (expected 1); [accessor + 0x64] = %s, [accessor + 0x68] = %s%s", hex8(t->chain.gate).c_str(),
                  gateAt(t->view.data + 0x64).c_str(), gateAt(t->view.data + 0x68).c_str(),
                  t->chain.gate == 1 ? "" : "  <- REFUSED: the weapon switches skip this weapon"));
        struct F {
            const char* name;
            uint32_t off;
        };
        const F fields[] = {{"handling A", 0x94}, {"handling B", 0xBC}, {"handling C", 0xE4}, {"zoom mult", 0x10C}, {"zoom speed", 0x134},
                            {"magazine A", 0x24C}, {"magazine B", 0x29C}, {"rpm A", 0xA34}, {"rpm B", 0xA5C}, {"rpm C", 0xA84},
                            {"rpm D", 0xAAC}, {"damage", 0xF34}, {"spread", 0xF84}, {"recoil 0", 0x10C4}, {"recoil 7", 0x10C4 + 7 * 0x28}};
        int ok = 0;
        for (const F& f : fields) {
            const Located l = player::locateRecord(t->view, f.off);
            logLocated(c, t->view, f.name, f.off, l);
            ok += l.ok;
        }
        const Located ammo = player::locateAmmo(t->view);
        if (ammo.ok) c.log(fmt("  %-16s via %s at +%04X, value %u", "infinite ammo", player::viaName(ammo.via), ammo.recordOffset, ammo.record.value));
        else c.log(fmt("  %-16s %s", "infinite ammo", ammo.why.c_str()));
        const Located fa = player::locateFullAuto(t->view);
        c.log(fmt("  %-16s word at plugin base +980 = %s; at accessor +980 = %s, +984 = %s (expected 01010000 semi or 0 auto)", "full auto",
                  t->view.has(t->view.pluginBase() + 0x980, 4) ? hex8(t->view.u32(t->view.pluginBase() + 0x980)).c_str() : "--------",
                  t->view.has(t->view.data + 0x980, 4) ? hex8(t->view.u32(t->view.data + 0x980)).c_str() : "--------",
                  t->view.has(t->view.data + 0x984, 4) ? hex8(t->view.u32(t->view.data + 0x984)).c_str() : "--------"));
        c.log(fmt("  => %d of %zu record fields located, ammo %s, full auto %s", ok, std::size(fields), ammo.ok ? "ok" : "no", fa.ok ? "ok" : "no"));
        if (t->chain.gate == 1 && ok == int(std::size(fields)) && ammo.ok && fa.ok) ++good;
    }
    if (!weapons) {
        c.log("no weapon sObject has +0x2C == the player (equip a weapon)");
        c.log("verdict: no weapons to check");
        return;
    }
    if (good == std::min(weapons, 3)) c.log("verdict: weapon fire records confirmed: every weapon switch will write");
    else c.log(fmt("verdict: %d of %d weapons fully located: switches write where a record matched (send this report)", good, std::min(weapons, 3)));
}

 

void probeLiveWrites(Ctx& c) {
    const auto lines = playerui::writes(c).describe(c.rt());
    for (const std::string& l : lines) c.log(l);
    const player::Board b = player::boardSnapshot();
    int on = 0, writing = 0, landing = 0;
    for (size_t i = 0; i < player::kFeatureCount; ++i) {
        const auto& s = b.features[i];
        if (s.level == player::FeatureState::Off) continue;
        ++on;
        writing += s.level == player::FeatureState::Active || s.level == player::FeatureState::Partial;
        landing += s.landing;
        c.log(fmt("%-18s %s", player::featureName(player::Feature(i)), s.text.c_str()));
    }
    if (!on) c.log("verdict: nothing on (switch some on in an activity, then run this again)");
    else c.log(fmt("verdict: %d switch(es) on, %d writing, %d landing (hits advancing)", on, writing, landing));
}

 

void probeKillAura(Ctx& c) {
    const uint32_t site = player::addr::KILL_AURA_SITE;
    const auto b = c.rt().read(site, 40);
    const std::string got = hexBytes(b);
    const std::string want = player::kKillAuraFingerprints.front().second;
    c.log("0x21F258 (folded TTK accessor): " + got);
    c.log("expected:                        " + want);
    if (c.rt().hooked(site)) {
        const bool rest = got.substr(8) == want.substr(8);
        c.log(rest ? "verdict: hooked by Blackrock (kill aura on); the other nine words are intact" : "verdict: hooked, but the body differs (send this report)");
    } else {
        c.log(got == want ? "verdict: site intact: kill aura can hook it" : "verdict: the site differs from the EBOOT (another tool patched it?)");
    }
}

 

void probeEditor(Ctx& c) {
    RuntimeReader mem(c.rt());
    (void)player0(c, mem);
    const player::editor::Scan s = player::editor::scanWeapons(mem);
    int withFields = 0;
    for (const auto& w : s.weapons) {
        size_t named = 0;
        for (const auto& f : w.fields) named += f.known();
        c.log(fmt("weapon %s \"%s\": %zu components; fire component class %s, %u bytes; %zu fields (%zu named); archetype %s", hex8(w.handle).c_str(),
                  w.name.c_str(), w.components.size(), hex8(w.fire.cls).c_str(), w.fire.size, w.fields.size(), named, hex8(w.archetype).c_str()));
        for (const auto& f : w.fields)
            if (f.known()) c.log(fmt("  +%04X id %-6u %-26s %.6g", f.offset, f.id, f.name.c_str(), f.value()));
        withFields += !w.fields.empty();
    }
    for (const auto& n : s.notes) c.log(n);
    c.log("expected: about 93 records per primary weapon (Xbox auto rifle / shotgun), the ammo pair, an archetype tag");
    if (s.weapons.empty()) c.log("verdict: no weapons to check (equip one)");
    else if (withFields == int(s.weapons.size())) c.log("verdict: record signature 0x9951DAEF confirmed on PS3 weapons: the editor works");
    else c.log(fmt("verdict: %d of %zu weapons have records (send this report)", withFields, s.weapons.size()));
}

 

void probeLoadout(Ctx& c) {
    RuntimeReader mem(c.rt());
    (void)player0(c, mem);
    const player::Loadout l = player::readLoadout(mem);
    std::map<uint32_t, std::pair<int, std::string>> classes;
    for (const auto& k : l.classes) {
        auto& e = classes[k.cls];
        ++e.first;
        if (e.second.find(std::to_string(k.size)) == std::string::npos) e.second += (e.second.empty() ? "" : ",") + std::to_string(k.size);
    }
    c.log(fmt("%zu components on the player and its weapons, %zu classes:", l.classes.size(), classes.size()));
    for (const auto& [cls, e] : classes) {
        const bool known = cls == player::kAbilityLoadoutClass || cls == player::kWeaponArchetypeClass || cls == player::kTalentGridClass;
        c.log(fmt("  class %s x%d sizes %s%s", hex8(cls).c_str(), e.first, e.second.c_str(), known ? "   <- loadout class" : ""));
    }
    for (const auto& r : l.refs)
        if (r.kind != "Talent reference" || r.slot < 3)
            c.log(fmt("  %-18s %-12s owner %s tag %s %s", r.kind.c_str(), r.slotName.c_str(), hex8(r.owner).c_str(), hex8(r.tag).c_str(),
                      names::tagLabel(r.tag).c_str()));
    const bool ability = classes.count(player::kAbilityLoadoutClass) != 0, archetype = classes.count(player::kWeaponArchetypeClass) != 0;
    c.log(fmt("expected: class 80AA28D4 on the player (ability tags at +0x240), class 8080271E on each weapon (archetype at +0x7C); found %s / %s",
              ability ? "yes" : "no", archetype ? "yes" : "no"));
    c.log(ability && archetype ? "verdict: loadout classes confirmed" : "verdict: loadout class tags differ on PS3 (send this report)");
}

 

void probeBuffs(Ctx& c) {
    RuntimeReader mem(c.rt());
    const LocalPlayer p = player0(c, mem);
    const player::Attachments a = player::readAttachments(mem, p.handle);
    c.log(fmt("player %s (type %u): %zu arenas in the +0x3C chain, read twice and unchanged", hex8(a.owner).c_str(), a.type, a.defs.size()));
    int tags = 0;
    for (size_t i = 0; i < a.defs.size(); ++i) {
        const auto& d = a.defs[i];
        const bool tagLike = (d.tag & 0xFE000000u) == 0x80000000u;
        tags += tagLike;
        if (i < 16)
            c.log(fmt("  arena %s at %s size %6u flags %s +4 %s %s", hex8(d.arenaHandle).c_str(), hex8(d.address).c_str(), d.size, hex8(d.flags).c_str(),
                      hex8(d.tag).c_str(), tagLike ? names::tagLabel(d.tag).c_str() : "(not a tag)"));
    }
    c.log(fmt("expected: arena +4 holds a definition tag (80xxxxxx / 81xxxxxx) for most arenas; %d of %zu do", tags, a.defs.size()));
    c.log(a.defs.empty() ? "verdict: empty chain (load into an activity)"
                         : tags * 2 >= int(a.defs.size()) ? "verdict: arena header layout confirmed (+4 = definition tag)"
                                                          : "verdict: arena +4 does not look like tags (send this report)");
}

 

void probeStats(Ctx& c) {
    RuntimeReader mem(c.rt());
    const LocalPlayer p = player0(c, mem);
    c.require(player::kStatsFingerprints, "Stats");
    bool regOk = false, stOk = false;
    try {
        const player::StatRegistry r = player::readStatRegistry(mem);
        unsigned total = 0;
        for (uint16_t n : r.counts) total += n;
        c.log(fmt("registry 01AB9820: base %s stride 0x%X capacity %u; category counts sum %u; %zu live references", hex8(r.base).c_str(), r.stride,
                  r.capacity, total, r.refs.size()));
        for (size_t i = 0; i < r.refs.size() && i < 6; ++i)
            c.log(fmt("  %s cat %u index %u: %s", hex8(r.refs[i].handle).c_str(), r.refs[i].category, r.refs[i].index, player::statLabel(r.refs[i].id).c_str()));
        regOk = true;
    } catch (const std::exception& e) {
        c.log(std::string("registry: ") + e.what());
    }
    try {
        const uint32_t slot = mem.u32(p.element + player::kStatusSlot);
        c.log(fmt("descriptor element %s +0x25BC = %s", hex8(p.element).c_str(), hex8(slot).c_str()));
        const player::StatusAggregate s = player::readStatus(mem);
        c.log(fmt("status component %s: accessor %s, data %s, class %s, +0x14 == handle, %zu record(s) (<= 4)", hex8(s.handle).c_str(),
                  hex8(s.accessor).c_str(), hex8(s.data).c_str(), hex8(s.cls).c_str(), s.records.size()));
        for (const auto& r : s.records) c.log(fmt("  id %s value %.6g priority %d flags %02X contributions %d", hex8(r.id).c_str(), double(r.value), r.priority, r.flags, r.contributions));
        stOk = true;
    } catch (const std::exception& e) {
        c.log(std::string("status aggregate: ") + e.what());
    }
    c.log("expected: registry stride 0x14 with a heap base (0x33xxxxxx seen); status component +0x14 == the +0x25BC handle, count <= 4, "
          "inside the player's +0x3C arena chain");
    c.log(regOk && stOk ? "verdict: stat registry and status aggregate confirmed"
                        : regOk ? "verdict: registry ok; the status component did not check out (send this report)"
                                : "verdict: the stat registry did not read (send this report)");
}

 

void probeEsp(Ctx& c) {
    RuntimeReader mem(c.rt());
    const LocalPlayer p = player0(c, mem);
    c.log("player sObject +0xB0 (engine position, verified): " + v3(readEnginePosition(mem, p)));
    c.log("player sObject +0x40 (Xbox ESP vector):           " + v3(mem.bytes(p.entry + sobj::VECTOR40, 12).vec3(0)));
    if (p.playerEntry) c.log("player entry +0xE0 (Xbox ESP origin, likely):     " + v3(mem.bytes(p.playerEntry + world::player::ENTRY_POSITION, 12).vec3(0)));
    const int8_t tableFaction = factionOf(mem, p.handle);
    c.log(fmt("faction: player entry +0x90 = %d, faction table byte = %d (expected equal)", p.faction, tableFaction));
    const Allegiance a = readAllegiance(mem);
    c.log(a.owned() ? "allegiance bitset: ownership proven (hostility uses it)" : "allegiance bitset: NOT proven (" + a.proof + "): hostility falls back to faction != yours");
    player::EspOptions o;
    o.radius = 500;
    player::EspScanner sc;
    const player::EspSnapshot s = sc.scan(mem, o);
    c.log(fmt("%zu objects scanned, %zu bipeds within 500 m", s.scanned, s.inRange));
    for (size_t i = 0; i < s.entities.size() && i < 6; ++i) {
        const auto& e = s.entities[i];
        c.log(fmt("  %s %-8s %6.1f m faction %3d %-8s %s", hex8(e.handle).c_str(), typeName(e.type), double(e.distance), e.faction,
                  player::hostilityName(e.hostility), e.tag ? names::tagLabel(e.tag).c_str() : ""));
    }
    const bool facOk = p.faction >= 0 && tableFaction == p.faction;
    c.log(facOk ? "verdict: ESP sources confirmed (engine positions, faction table matches the player entry)"
                : "verdict: the faction sources disagree or are empty (in an activity? send this report)");
}

}   

BR_FEATURES(player_checks) {
    auto probe = [&](const char* id, const char* label, const char* help, void (*run)(Ctx&), const char* badge = kBadgeNeedsCheck) {
        r.action({.id = id, .page = Page::Testing, .tab = kPortChecksTab, .section = playerui::kProbeSection, .label = label, .help = help, .run = run,
                  .badge = badge});
    };
    probe("player.check.records", "Player Component Records (God, Recharge, Ranges)",
          "Spawned, in an activity. Resolves the player's +0x78 component both ways (accessor / tag table) and looks for the record "
          "signature at every Xbox offset (god 0x25A4, interact 0x2D14, pickup 0x2D3C x7, super / grenade / melee). Expected: every field "
          "found, delta 0 or +4.",
          probePlayerRecords);
    probe("weapons.check.records", "Weapon Fire Records (Every Weapon Switch)",
          "Weapon equipped. For each owned weapon: the +0x78 component, gate +0x64 == 1, the record at every Xbox offset (handling, zoom, "
          "magazine, rate of fire, damage, spread, recoil), the ammo record / pair and the full-auto word +0x980. Expected: all found.",
          probeWeaponRecords);
    probe("weapons.check.live_writes", "Live Write List (Switches That Are On)",
          "Switch a few on first. Lists the console's write table: every group's guards, which guard failed last frame and how often each "
          "word was written. Expected: all guards held and hits advancing.",
          probeLiveWrites, "");
    probe("weapons.check.kill_aura", "Kill Aura Hook Site 0x21F258",
          "Reads the ten words of the folded TTK accessor. Expected: F821FFC1 38A00030 ... 4E800020 (or our branch first while kill aura is on).",
          probeKillAura, "");
    probe("weapons.check.editor", "Weapon Editor Records (Signature 0x9951DAEF)",
          "Scans your weapons like the editor does and lists every named field with its live value. Expected: ~93 records per primary "
          "weapon, archetype tag found.",
          probeEditor);
    probe("player.check.loadout", "Loadout Class Tags",
          "Lists every component class on you and your weapons and the decoded ability / archetype / perk references. Expected: 80AA28D4 on "
          "you, 8080271E on each weapon.",
          probeLoadout);
    probe("player.check.buffs", "Attached Definitions (Arena +4 Layout)",
          "Walks your +0x3C arena chain twice. Expected: most arenas carry a definition tag at +4 and the chain is stable.", probeBuffs);
    probe("player.check.stats", "Stat Registry and Status Aggregate",
          "Reads 0x1AB9820 (stride 0x14) with its 15 counts and the status component at descriptor +0x25BC. Expected: +0x14 == the slot "
          "handle, at most 4 records, and the component inside your own +0x3C arena chain.",
          probeStats);
    probe("player.check.esp", "Nearby (ESP) Positions and Factions",
          "Compares the position sources (+0xB0 / +0x40 / entry +0xE0) and the player's faction (entry +0x90 vs the faction table) and "
          "lists the six nearest actors with hostility. Expected: the two factions equal.",
          probeEsp);
}

}   
