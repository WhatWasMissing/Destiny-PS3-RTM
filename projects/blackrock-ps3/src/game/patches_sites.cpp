#include "game/patches_sites.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <mutex>

#include "core/error.h"
#include "core/ppc.h"
#include "game/rtm_cheat_offsets.h"

namespace brps3::patches {

namespace {

constexpr uint32_t NOP = 0x60000000;

template <std::size_t N>
std::vector<Word> rtmWordList(const rtm_offsets::WordSite (&sites)[N]) {
    std::vector<Word> words;
    words.reserve(N);
    for (const auto& site : sites) words.push_back({site.address, site.original, site.patched});
    return words;
}

std::vector<Unit> buildUnits() {
    return {
        {"patches.stop_bullets", "Stop bullets", rtmWordList(rtm_offsets::kStopBullets)},
        {"rtm.instant_respawn", "RTM instant respawn", rtmWordList(rtm_offsets::kInstantRespawn)},
        {"rtm.no_recoil", "RTM no recoil", rtmWordList(rtm_offsets::kNoRecoil)},
        {"rtm.unlimited_abilities", "RTM unlimited abilities", rtmWordList(rtm_offsets::kUnlimitedAbilities)},
        {"rtm.unlimited_ammo", "RTM unlimited ammo", rtmWordList(rtm_offsets::kUnlimitedAmmo)},
        {"rtm.no_collision", "RTM no collision", rtmWordList(rtm_offsets::kNoCollision)},
        {"rtm.one_hit_kill", "RTM one-hit kill", rtmWordList(rtm_offsets::kOneHitKill)},
        {"rtm.rapid_fire", "RTM rapid fire", rtmWordList(rtm_offsets::kRapidFire)},
        {"rtm.no_target", "RTM no target", rtmWordList(rtm_offsets::kNoTarget)},
        {"rtm.god_mode", "RTM god mode", rtmWordList(rtm_offsets::kGodMode)},
        {"rtm.no_gravity", "RTM no gravity", rtmWordList(rtm_offsets::kNoGravity)},
        {"rtm.no_pushbacks", "RTM no pushbacks", rtmWordList(rtm_offsets::kNoPushbacks)},
        {"rtm.no_floors", "RTM no floors", rtmWordList(rtm_offsets::kNoFloors)},
        {"rtm.static_actors", "RTM static actors", rtmWordList(rtm_offsets::kStaticActors)},
        {"rtm.variable_buff", "RTM variable buff", rtmWordList(rtm_offsets::kVariableBuff)},
        {"patches.respawn_timer", "Respawn timer +0x30 (both copies)",                     
         {{0x71C138, 0x93BF0030, NOP}, {0x71C348, 0x93BF0030, NOP}}},
        {"patches.fast_respawn", "Auto respawn timers and checks",
         {
             {0x3AB80C, 0x30610144, 0x48000138},    
             {0x71C164, 0x909F002C, NOP},           
             {0x71C374, 0x909F002C, NOP},           
             {0x71C1A4, 0x93DF0034, NOP},           
             {0x71C3B4, 0x93DF0034, NOP},           
             {0x71C248, 0x907D083C, NOP},           
             {0x5AED54, 0x4801A7A5, NOP},           
         }},
        {"patches.joining_allies", "Joining allies", {{0x39BCE8, 0x7C0802A6, 0x4E800020}}},    
        {"patches.sparrow_energy", "Sparrow energy", rtmWordList(rtm_offsets::kUnlimitedSparrow)},
        {"patches.immune_bypass", "Immune bypass",
         {{0x44CD68, 0xC0840004, 0xEC831828},       
          {0x444CA4, 0xC04100F0, 0xC05A0000}}},     
        {"patches.fov", "FOV override",
         {{0x45AD68, 0x3C8001AD, 0x4BFFFF10},       
          {0x70B800, 0x41820024, 0x48000024}}},     
        {"patches.third_person", "Third person", {{0x7058A4, 0x38800004, 0x38800000}}},        
        {"patches.bullet_walls", "Bullets through walls", {{0x51E3DC, 0x3BA0000A, 0x3BA00011}}},    
        {"patches.instant_dismantle", "Instant dismantle", {{0x7CBAFC, 0x82990430, 0x3A800000}}, "Vault (inventory group)"},
        {"patches.viewmodel_fov", "Viewmodel FOV", {{0x301ADC, 0xC03E0764, 0x48000020}}},      
        {"patches.wt_dlc", "World-transition DLC gate", {{0x36EA24, 0x4182001C, 0x4800001C}}},     
        {"patches.sparrow_anywhere", "Sparrow summon anywhere", {{0x14DC84, 0x38800003, 0x38800001}}},    
        {"patches.hotload_suppress", "Hot-load notification", {{0x148D00, 0x4BFFE25D, NOP}}},     
        {"patches.deafen_authority", "Bubble authority",                                          
         {{0x91E194, 0x4847A88D, 0x4E800020}, {0x91E190, 0x7C0802A6, 0x38600001}}},
         
        {"site.0036EA5C", "World-transition permission gate", {{0x36EA5C, 0x4082001C, 0x4800001C}},    
         "the entitlement option, activity and movement"},
        {"site.00C35140", "Access predicate", {{0xC35140, 0x546307FE, 0x38600001}}, "Walkable Lighthouse"},    
        {"site.00C5BE34", "Unlock leaf",                                                                      
         {{0xC5BE38, 0x38A00000, 0x4E800020}, {0xC5BE34, 0x7C840734, 0x38600001}}, "Walkable Lighthouse"},
        {"site.008C5F98", "BAP send-failure store", {{0x8C5F98, 0x907E10F8, NOP}}, "Walkable Lighthouse"},
        {"site.008C6018", "BAP receive-failure store", {{0x8C6018, 0x907E10F8, NOP}}, "Walkable Lighthouse"},
        {"site.0033AA9C", "ASTAT 33 store", {{0x33AA9C, 0x907C04E0, NOP}}, "Walkable Lighthouse"},
        {"site.0033AAA8", "ASTAT demotion branch", {{0x33AAA8, 0x408200F4, 0x480000F4}}, "Walkable Lighthouse"},    
    };
}

std::vector<FeatureInfo> buildFeatures() {
    return {
        {Feature::StopBullets, "stop_bullets", "Stop bullets", {"patches.stop_bullets"}, {}},
        {Feature::InstantRespawn, "instant_respawn", "RTM instant respawn", {"rtm.instant_respawn"}, {}},
        {Feature::FastRespawn, "fast_respawn", "Auto respawn", {"patches.respawn_timer", "patches.fast_respawn"},
         {{0x3A86C0, "7C0802A6489F0355F821FF21F80100F0"},      
          {0x3AB578, "7C0802A6489ED48DF821FE11F8010200"},      
          {0x71C020, "7C0802A64867C9F9F821FF71F80100A0"},      
          {0x71C1B0, "7C0802A64867C869F821FF71F80100A0"},      
          {0x5AEC3C, "7C0802A6487E9DD9F821FEF1F8010120"}}},    
        {Feature::JoiningAllies, "joining_allies", "Joining allies", {"patches.joining_allies"}, {}},
        {Feature::SparrowEnergy, "sparrow_energy", "Sparrow energy", {"patches.sparrow_energy"}, {}},
        {Feature::ImmuneBypass, "immune_bypass", "Immune bypass", {"patches.immune_bypass"}, {}},
        {Feature::Fov, "fov", "FOV override", {"patches.fov"}, {}},
        {Feature::ThirdPerson, "third_person", "Third person", {"patches.third_person"},
         {{0x705078, "7C0802A63181FFE848693A3948693A9D"}}},
        {Feature::BulletWalls, "bullet_walls", "Bullets through walls", {"patches.bullet_walls"}, {}},
        {Feature::InstantDismantle, "instant_dismantle", "Instant dismantle", {"patches.instant_dismantle"}, {}},
        {Feature::ViewmodelFov, "viewmodel_fov", "Viewmodel FOV", {"patches.viewmodel_fov"}, {}},
        {Feature::WorldTransition, "world_transition", "World-transition bypass", {"patches.wt_dlc", "site.0036EA5C"},
         {{0x36E92C, "7C0802A648A2A0E9F821FF61F80100B0"}}},    
        {Feature::EntitleAccess, "entitle.access", "Access predicate", {"site.00C35140"}, {}},
        {Feature::EntitleUnlock, "entitle.unlock", "Unlock leaf", {"site.00C5BE34"}, {}},
        {Feature::EntitleCounter, "entitle.counter", "Counter leaf", {}, {}},
        {Feature::EntitleGate, "entitle.gate", "Permission gate", {"site.0036EA5C"},
         {{0x36E92C, "7C0802A648A2A0E9F821FF61F80100B0"}}},
        {Feature::Deafen, "deafen", "Deafen",
         {"site.008C5F98", "site.008C6018", "site.0033AA9C", "site.0033AAA8", "patches.deafen_authority"},
         {{0x8C5F1C, "7C0802A6484D2AF9F821FF61F80100B0"},      
          {0x33A808, "7C0802A648A5E20DF821FF41F80100D0"}}},    
        {Feature::SparrowAnywhere, "sparrow_anywhere", "Sparrow summon anywhere", {"patches.sparrow_anywhere"},
         {{0x14DB90, "7C0802A648C4AE8DF821FF71F80100A0"}}},    
        {Feature::HotloadSuppress, "hotload_suppress", "Hot-load suppression", {"patches.hotload_suppress"},
         {{0x148CBC, "7C0802A648C4FD65F821FF81F8010090"},      
          {0x146F5C, "7C0802A648C51AC5F821FF81F8010090"}}},
        {Feature::RtmNoRecoil, "rtm.no_recoil", "RTM no recoil", {"rtm.no_recoil"}, {}},
        {Feature::RtmUnlimitedAbilities, "rtm.unlimited_abilities", "RTM unlimited abilities", {"rtm.unlimited_abilities"}, {}},
        {Feature::RtmUnlimitedAmmo, "rtm.unlimited_ammo", "RTM unlimited ammo", {"rtm.unlimited_ammo"}, {}},
        {Feature::RtmNoCollision, "rtm.no_collision", "RTM no collision", {"rtm.no_collision"}, {}},
        {Feature::RtmOneHitKill, "rtm.one_hit_kill", "RTM one-hit kill", {"rtm.one_hit_kill"}, {}},
        {Feature::RtmRapidFire, "rtm.rapid_fire", "RTM rapid fire", {"rtm.rapid_fire"}, {}},
        {Feature::RtmNoTarget, "rtm.no_target", "RTM no target", {"rtm.no_target"}, {}},
        {Feature::RtmGodMode, "rtm.god_mode", "RTM god mode", {"rtm.god_mode"}, {}},
        {Feature::RtmNoGravity, "rtm.no_gravity", "RTM no gravity", {"rtm.no_gravity"}, {}},
        {Feature::RtmNoPushbacks, "rtm.no_pushbacks", "RTM no pushbacks", {"rtm.no_pushbacks"}, {}},
        {Feature::RtmNoFloors, "rtm.no_floors", "RTM no floors", {"rtm.no_floors"}, {}},
        {Feature::RtmStaticActors, "rtm.static_actors", "RTM static actors", {"rtm.static_actors"}, {}},
        {Feature::RtmVariableBuff, "rtm.variable_buff", "RTM variable buff", {"rtm.variable_buff"}, {}},
    };
}

std::string hex32s(uint32_t v) {
    char b[12];
    std::snprintf(b, sizeof b, "%08X", v);
    return b;
}

}   

const char* const kDismantleWindowHex =
    "8879042C2F1D00005463077A2C030000418200A82C9A0000409A00A04186093462E300008299043062DE00004BF87F2D4BF8826180990138907906F86083"
    "00002C030000408200088079013C2C030000418209003CA001B43081010080A56B1490A1010080A3000080A5000080A500007CA903A64E9E04212C030000"
    "418208D488990850548406B42C040000418208C480A30000309900F080A5000080A500007CA903A64E9E04218879085060630010987908504800089C";

const std::vector<Unit>& units() {
    static const std::vector<Unit> u = buildUnits();
    return u;
}

const Unit* findUnit(const std::string& name) {
    for (const Unit& u : units())
        if (name == u.name) return &u;
    return nullptr;
}

const std::vector<FeatureInfo>& features() {
    static const std::vector<FeatureInfo> f = buildFeatures();
    return f;
}

const FeatureInfo& info(Feature f) {
    for (const FeatureInfo& i : features())
        if (i.feature == f) return i;
    throw Error("unknown patch feature");
}

std::vector<Runtime::Word> runtimeWords(const Unit& u) {
    std::vector<Runtime::Word> out;
    for (const Word& w : u.words) {
        Runtime::Word r;
        r.addr = w.addr;
        r.original = {uint8_t(w.original >> 24), uint8_t(w.original >> 16), uint8_t(w.original >> 8), uint8_t(w.original)};
        r.replacement = {uint8_t(w.patched >> 24), uint8_t(w.patched >> 16), uint8_t(w.patched >> 8), uint8_t(w.patched)};
        out.push_back(std::move(r));
    }
    return out;
}

Fingerprints fingerprints(Feature f) {
     
    static std::vector<std::vector<std::string>> storage(static_cast<size_t>(Feature::Count));
    static std::mutex m;
    std::lock_guard<std::mutex> lock(m);
    const FeatureInfo& fi = info(f);
    auto& keep = storage[size_t(f)];
    if (keep.empty()) {
        for (const char* name : fi.units)
            if (const Unit* u = findUnit(name))
                for (const Word& w : u->words) keep.push_back(hex32s(w.original));
        if (f == Feature::EntitleCounter) keep.push_back(hex32s(kCounterLeafOriginal));
    }
    Fingerprints out = fi.code;
    size_t k = 0;
    for (const char* name : fi.units)
        if (const Unit* u = findUnit(name))
            for (const Word& w : u->words) out.push_back({w.addr, keep[k++].c_str()});
    if (f == Feature::EntitleCounter) out.push_back({kCounterLeaf, keep[k++].c_str()});
    return out;
}

SiteState classify(const Word& w, uint32_t current) {
    if (current == w.original) return SiteState::Original;
    if (current == w.patched) return SiteState::Patched;
    return SiteState::Foreign;
}

const char* siteStateName(SiteState s) {
    switch (s) {
    case SiteState::Original: return "original";
    case SiteState::Patched: return "patched";
    case SiteState::Foreign: return "FOREIGN";
    }
    return "?";
}

bool dismantleWindowOk(const std::vector<uint8_t>& bytes, std::string* why) {
    const std::vector<uint8_t> want = parseHexBytes(kDismantleWindowHex);
    if (bytes.size() != want.size()) {
        if (why) *why = "read " + std::to_string(bytes.size()) + " bytes, expected " + std::to_string(want.size());
        return false;
    }
    constexpr size_t site = 0x7CBAFC - kDismantleWindow;
    for (size_t i = 0; i < want.size(); ++i) {
        if (i >= site && i < site + 4) continue;    
        if (bytes[i] != want[i]) {
            if (why) *why = "byte +0x" + hex32s(uint32_t(i)).substr(4) + " differs (" + hex32s(kDismantleWindow + uint32_t(i)) + ")";
            return false;
        }
    }
    const uint32_t w = uint32_t(bytes[site]) << 24 | uint32_t(bytes[site + 1]) << 16 | uint32_t(bytes[site + 2]) << 8 | bytes[site + 3];
    if (w != 0x82990430u && w != 0x3A800000u) {
        if (why) *why = "the patch word holds " + hex32s(w);
        return false;
    }
    return true;
}

std::vector<uint8_t> counterLeafStub(uint32_t base) {
    ppc::Asm a(base);
    a.li(6, 9999).stw(6, 0, 3).blr();    
    return a.link();
}

}   
