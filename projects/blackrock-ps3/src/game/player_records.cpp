#include "game/player_records.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>

#include "game/player_addr.h"

namespace brps3::player {

bool isRecordAt(const world::Bytes& data, uint32_t v) {
    if (v < 8 || v % 4 || !data.has(v - 8, 0x1C)) return false;
    return data.u32(v + 8) == kRecordSignature && data.u32(v + 0x10) == v;
}

Record recordAt(const world::Bytes& data, uint32_t v) {
    Record r;
    r.offset = v;
    r.tag = data.u32(v - 8);
    r.id = data.u32(v - 4);
    r.value = data.u32(v);
    r.copy = data.u32(v + 4);
    return r;
}

std::vector<Record> decodeRecords(const world::Bytes& data) {
    std::vector<Record> out;
    if (data.size() < 0x1C) return out;
    for (uint32_t v = 8; uint64_t(v) + 0x14 <= data.size(); v += 4)
        if (isRecordAt(data, v)) out.push_back(recordAt(data, v));
    return out;
}

namespace {

struct Known {
    uint32_t id, tag;                    
    FieldInfo info;
};

 
 
const Known kKnown[] = {
    {10064, 0, {"ADS zoom", "Aiming", "multiplier", "stock 1.8 (auto rifle), 1.2 (shotgun)"}},
    {9360, 0, {"Aim speed", "Aiming", "value", "stock 0.333"}},
    {9408, 0, {"ADS forward speed", "Movement", "value", "stock 0.25"}},
    {10096, 0, {"ADS strafe speed", "Movement", "value", "stock 0.25"}},
    {9280, 0, {"Crosshair bloom", "Spread", "radians", "stock 0.0419 (2.4 deg)"}},
    {9892, 0, {"Spread cone", "Spread", "radians", "stock 0.1257 (7.2 deg); wider on a shotgun"}},
    {0, 0x8080221A, {"Rate of fire", "Firing", "value", "stock 7.5 (auto rifle), 1.071 (shotgun)"}},
    {14240, 0, {"Shots per trigger pull A", "Firing", "shots", "0 = full auto; set all four to 7 for a 7-round burst"}},
    {12208, 0, {"Shots per trigger pull B", "Firing", "shots", "burst count (see A)"}},
    {12240, 0, {"Shots per trigger pull C", "Firing", "shots", "burst count (see A)"}},
    {12224, 0, {"Shots per trigger pull D", "Firing", "shots", "burst count (see A)"}},
    {12932, 0, {"Fire rate multiplier", "Firing", "multiplier", "stock 1.0"}},
    {12884, 0, {"Weapon bounce", "Recoil", "value", "stock 1.0"}},
    {13968, 0, {"Recoil A", "Recoil", "radians", "stock -0.008"}},
    {14000, 0, {"Vertical recoil", "Recoil", "radians", "stock 0.0096"}},
    {14032, 0, {"Recoil B", "Recoil", "radians", "stock 0.0064"}},
    {14064, 0, {"Recoil C", "Recoil", "radians", "stock -0.0048"}},
    {13984, 0, {"Recoil D", "Recoil", "radians", "stock -0.0145"}},
    {14016, 0, {"Recoil E", "Recoil", "radians", "stock 0.0193"}},
    {14048, 0, {"Horizontal recoil", "Recoil", "radians", "stock 0.0145"}},
    {14112, 0, {"Recoil kick", "Recoil", "radians", "stock 0.0785 (4.5 deg)"}},
    {15788, 0, {"Reload speed", "Reload", "value", "stock 2.793"}},
    {15488, 0, {"Ammo-adjacent (not the live count)", "Ammo", "value", "stock 21"}},
     
    {10048, 0, {"No visible effect", "Other", "value", "Xbox test: changing it did nothing"}},
    {10144, 0, {"No visible effect", "Other", "value", "Xbox test: changing it did nothing"}},
    {9264, 0, {"No visible effect", "Other", "value", "Xbox test: writing 40 changed nothing"}},
    {9844, 0, {"No visible effect", "Other", "value", "Xbox test: changing it did nothing"}},
    {9860, 0, {"No visible effect", "Other", "value", "Xbox test: changing it did nothing"}},
    {9876, 0, {"No visible effect", "Other", "value", "Xbox test: changing it did nothing"}},
    {9908, 0, {"No visible effect", "Other", "value", "Xbox test: changing it did nothing"}},
    {9924, 0, {"No visible effect", "Other", "value", "Xbox test: changing it did nothing"}},
    {9940, 0, {"No visible effect", "Other", "value", "Xbox test: changing it did nothing"}},
    {9956, 0, {"No visible effect", "Other", "value", "Xbox test: changing it did nothing"}},
};

}   

const FieldInfo* fieldInfo(uint32_t id, uint32_t tag) {
    for (const Known& k : kKnown)
        if (k.id == id && (k.tag == 0 || k.tag == tag) && (id != 0 || k.tag == tag)) return &k.info;
    return nullptr;
}

int groupOrder(const std::string& group) {
    static const char* const order[] = {"Firing", "Ammo", "Reload", "Recoil", "Spread", "Aiming", "Movement", "Other"};
    for (int i = 0; i < int(std::size(order)); ++i)
        if (group == order[i]) return i;
    return int(std::size(order));
}

const char* viaName(Via v) {
    switch (v) {
    case Via::PluginBase: return "plugin base";
    case Via::AccessorPlus4: return "accessor + 4";
    case Via::Accessor: return "accessor";
    case Via::AmmoPair: return "ammo pair";
    case Via::None: break;
    }
    return "-";
}

Located locateRecord(const ComponentView& c, uint32_t pluginOffset) {
    Located out;
    struct Cand {
        uint64_t addr;
        Via via;
    };
    const Cand cands[] = {{uint64_t(c.pluginBase()) + pluginOffset, c.tagBase ? Via::PluginBase : Via::Accessor},
                          {uint64_t(c.data) + pluginOffset + 4, Via::AccessorPlus4},
                          {uint64_t(c.data) + pluginOffset, Via::Accessor}};
    std::vector<uint32_t> tried;
    for (const Cand& cand : cands) {
        if (cand.addr > 0xFFFFFFFFull) continue;
        const uint32_t a = uint32_t(cand.addr);
        if (std::find(tried.begin(), tried.end(), a) != tried.end()) continue;
        tried.push_back(a);
        if (a < c.data || a % 4) continue;
        const uint32_t v = a - c.data;
        if (!c.has(a, 0x14) || v < 8) continue;
        if (!isRecordAt(c.bytes, v)) continue;
        out.ok = true;
        out.addr = a;
        out.recordOffset = v;
        out.via = cand.via;
        out.record = recordAt(c.bytes, v);
        return out;
    }
    char buf[160];
    std::snprintf(buf, sizeof buf, "no record signature at +%X (tried %zu address%s from the plugin and accessor bases)", pluginOffset, tried.size(),
                  tried.size() == 1 ? "" : "es");
    out.why = buf;
    return out;
}

Located locateAmmo(const ComponentView& c) {
    Located r = locateRecord(c, wfc::AMMO);
    if (r.ok) return r;
    const uint32_t tagAt = c.data + wfc::AMMO_TAG_OFFSET;
    if (c.has(tagAt, 0xC) && c.u32(tagAt) == wfc::AMMO_TAG) {
        Located out;
        out.ok = true;
        out.addr = c.data + wfc::AMMO_CLIP;
        out.recordOffset = wfc::AMMO_CLIP;
        out.via = Via::AmmoPair;
        out.record.offset = wfc::AMMO_CLIP;
        out.record.tag = wfc::AMMO_TAG;
        out.record.value = c.u32(out.addr);
        out.record.copy = out.record.value;
        return out;
    }
    r.why = "no ammo record at +1998 and no ammo pair (tag 80802449 at +1920)";
    return r;
}

Located locateFullAuto(const ComponentView& c) {
    Located out;
    const uint64_t a64 = uint64_t(c.pluginBase()) + wfc::FULL_AUTO;
    const uint32_t a = uint32_t(a64);
    if (a64 > 0xFFFFFFFFull || !c.has(a, 4)) {
        out.why = "the fire-mode word +980 is outside the component";
        return out;
    }
    const uint32_t v = c.u32(a);
    out.addr = a;
    out.recordOffset = a - c.data;
    out.via = c.tagBase ? Via::PluginBase : Via::Accessor;
    out.record.offset = out.recordOffset;
    out.record.value = v;
    out.record.copy = v;
    if (v != wfc::SEMI_AUTO && v != 0) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "fire-mode word +980 reads %08X (expected 01010000 or 0)", v);
        out.why = buf;
        return out;
    }
    out.ok = true;
    return out;
}

}   
