#include "game/player_inspect.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

#include "core/error.h"
#include "game/player_addr.h"
#include "game/player_editor.h"
#include "game/world_addr.h"
#include "game/world_components.h"

namespace brps3::player {

namespace {

using world::hex8;

}   

 

std::map<uint32_t, world::Bytes> readRanges(world::Reader& mem, const std::vector<std::pair<uint32_t, uint32_t>>& ranges, uint32_t gap,
                                            uint32_t maxSpan) {
    std::map<uint32_t, world::Bytes> out;
    std::vector<std::pair<uint32_t, uint32_t>> sorted = ranges;
    std::sort(sorted.begin(), sorted.end());
    size_t i = 0;
    while (i < sorted.size()) {
        uint32_t start = sorted[i].first;
        uint64_t end = uint64_t(start) + sorted[i].second;
        size_t j = i + 1;
        while (j < sorted.size()) {
            const uint64_t nEnd = std::max<uint64_t>(end, uint64_t(sorted[j].first) + sorted[j].second);
            if (sorted[j].first > end + gap || nEnd - start > maxSpan) break;
            end = nEnd;
            ++j;
        }
        const auto block = mem.tryBytes(start, uint32_t(end - start));
        for (size_t k = i; k < j; ++k) {
            if (block) {
                out[sorted[k].first] = block->slice(sorted[k].first - start, sorted[k].second);
            } else if (auto one = mem.tryBytes(sorted[k].first, sorted[k].second)) {
                out[sorted[k].first] = *one;    
            }
        }
        i = j;
    }
    return out;
}

 

std::vector<LoadoutRef> decodeLoadout(uint32_t cls, uint32_t owner, bool weapon, uint32_t component, const world::Bytes& b) {
    std::vector<LoadoutRef> out;
    auto base = [&]() {
        LoadoutRef r;
        r.owner = owner;
        r.weapon = weapon;
        r.cls = cls;
        r.component = component;
        return r;
    };
    if (cls == kAbilityLoadoutClass) {
        if (b.size() < 0x2E8) return out;
         
        static const char* const slots[6] = {"Grenade", "Ability slot 1", "Melee", "Movement", "Super", "Ability slot 5"};
        for (uint32_t i = 0; i < 6; ++i) {
            LoadoutRef r = base();
            r.offset = kAbilityTags + i * 4;
            r.tag = b.u32(r.offset);
            r.slot = i;
            r.arrayOffset = kAbilityTags;
            r.associated = b.u32(kAbilityWords + i * 4);
            r.kind = "Ability reference";
            r.slotName = slots[i];
            out.push_back(std::move(r));
        }
        return out;
    }
    if (cls != kWeaponArchetypeClass && cls != kTalentGridClass) return out;
    if (cls == kWeaponArchetypeClass && b.size() >= 0x80) {
        LoadoutRef r = base();
        r.offset = 0x7C;
        r.tag = b.u32(0x7C);
        r.kind = "Weapon archetype";
        r.slotName = "Archetype";
        out.push_back(std::move(r));
    }
    for (uint32_t h = 0; uint64_t(h) + 0x14 <= b.size(); h += 4) {
        if (b.u32(h) != kTalentHeader || b.u32(h + 8) != kTalentRecordTag || b.u32(h + 0xC) || b.u32(h + 0x10)) continue;
        const uint32_t count = b.u32(h + 4);
        if (count > 1024 || uint64_t(h) + 0x14 + uint64_t(count) * 0x40 > b.size()) continue;
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t p = h + 0x14 + i * 0x40;
            LoadoutRef r = base();
            r.offset = p + 0x30;
            r.tag = b.u32(p + 0x30);
            r.slot = i;
            r.arrayOffset = h;
            r.associated = b.u32(p + 0x20);
            r.flags = b.u32(p + 0x38);
            r.kind = "Talent reference";
            r.slotName = (cls == kTalentGridClass ? "Talent " : "Perk ") + std::to_string(i + 1);
            out.push_back(std::move(r));
        }
        if (count) h += count * 0x40 + 0x10;
    }
    return out;
}

Loadout readLoadout(world::Reader& mem) {
    Loadout l;
    const world::LocalPlayer p = world::requireLocalPlayer(mem);
    l.player = p.handle;
    const world::ObjectScan scan = world::readObjects(mem);
    const world::AccessorTable table = world::readAccessorTable(mem);
    std::vector<world::SObject> owners;
    for (const world::SObject& o : scan.objects)
        if (o.handle == p.handle && o.live()) owners.push_back(o);
    for (const world::SObject& o : world::childrenOf(scan, p.handle))
        if (o.type == world::TYPE_WEAPON && o.live()) owners.push_back(o);
    if (owners.empty()) throw Error("The player's sObject is not in the pool snapshot: refresh again.");
    for (const world::SObject& o : owners) {
        const bool weapon = o.handle != p.handle;
        for (const world::Accessor& a : world::componentsIn(table, world::arenaChain(table, o.componentHead))) {
            l.classes.push_back({o.handle, a.cls(), a.size(), weapon});
            if (a.cls() != kAbilityLoadoutClass && a.cls() != kWeaponArchetypeClass && a.cls() != kTalentGridClass) continue;
            if (a.size() > 0x10000) {
                ++l.unreadable;
                continue;
            }
             
             
             
            editor::ComponentRef ref;
            ref.owner = o.handle;
            ref.ownerEntry = o.addr;
            ref.ownerHead = o.componentHead;
            ref.accessor = a.addr;
            ref.flags = a.flags;
            ref.instance = a.instance;
            ref.back = a.backOffset;
            ref.cls = a.cls();
            ref.data = a.data;
            ref.size = a.size();
            std::string why;
            const auto b = editor::readComponent(mem, ref, why);
            if (!b || b->size() != a.size() || b->u32(0x1C) != o.handle) {    
                ++l.unreadable;
                continue;
            }
            auto rows = decodeLoadout(a.cls(), o.handle, weapon, a.data, *b);
            l.refs.insert(l.refs.end(), rows.begin(), rows.end());
        }
    }
    return l;
}

 

Attachments readAttachments(world::Reader& mem, uint32_t owner) {
    if (!world::validHandle(owner)) throw Error("No actor selected.");
    const world::PoolHeader objects = world::readPool(mem, world::addr::SOBJECT_POOL);
    const world::AccessorPool pool = world::readAccessorPool(mem);
    if (!objects.plausible() || objects.stride != world::sobj::STRIDE || !pool.plausible())
        throw Error("The actor / component pools are not set up: load into an activity.");
    const auto entry = objects.entryOf(owner);
    if (!entry) throw Error("The actor handle is out of range.");
    const world::Bytes object = mem.bytes(*entry, 0x40);
    if (object.u32(world::sobj::HANDLE) != owner) throw Error("The selected actor has gone; select its current instance.");
    Attachments out;
    out.owner = owner;
    out.type = object.u8(world::sobj::TYPE);
     
    std::vector<uint32_t> handles, entries;
    std::vector<bool> seen(pool.header.capacity, false);
    std::map<uint32_t, world::Bytes> slots;
    for (uint32_t h = object.u32(world::sobj::COMPONENT_HEAD); h != 0xFFFFFFFFu;) {
        const uint32_t index = h & 0x1FFF;
        if (index >= pool.header.capacity || seen[index] || handles.size() > 256) throw Error("Malformed attachment chain; snapshot discarded.");
        seen[index] = true;
        const uint32_t e = pool.entry(index);
        const auto slot = mem.tryBytes(e, world::kAccessorStride);
        if (!slot) throw Error("An attachment entry could not be read.");
        handles.push_back(h);
        entries.push_back(e);
        slots[e] = *slot;
        h = slot->u32(0x14);
    }
     
    std::vector<std::pair<uint32_t, uint32_t>> bodies;
    for (size_t i = 0; i < entries.size(); ++i) {
        const world::Bytes& s = slots[entries[i]];
        const uint32_t flags = s.u32(0), size = flags & 0x0FFFFFFF, body = entries[i] - (s.u32(8) & pool.mask);
        if ((flags & 0xF0000000u) != world::kAccessorAllocFlag || size < 20 || size > 0x400000 || !world::heap(body, size))
            throw Error("Attachment storage is not plausible; snapshot discarded.");
        bodies.push_back({body, 20});
        out.defs.push_back({0, handles[i], body, size, 0});
    }
    auto heads = readRanges(mem, bodies);
    for (Attached& d : out.defs) {
        auto it = heads.find(d.address);
        if (it == heads.end()) throw Error("An attachment header could not be read.");
        d.flags = it->second.u32(0);
        d.tag = it->second.u32(4);
    }
     
    std::vector<std::pair<uint32_t, uint32_t>> again;
    for (uint32_t e : entries) again.push_back({e, world::kAccessorStride});
    for (const auto& b : bodies) again.push_back(b);
    auto second = readRanges(mem, again);
    for (uint32_t e : entries) {
        auto it = second.find(e);
        if (it == second.end() || it->second.data() != slots[e].data()) throw Error("Attachments changed during the refresh; retry.");
    }
    for (const Attached& d : out.defs) {
        auto it = second.find(d.address);
        if (it == second.end() || it->second.u32(0) != d.flags || it->second.u32(4) != d.tag) throw Error("Attachments changed during the refresh; retry.");
    }
    const world::Bytes after = mem.bytes(*entry, 0x40);
    if (after.u32(world::sobj::HANDLE) != owner || after.u32(world::sobj::COMPONENT_HEAD) != object.u32(world::sobj::COMPONENT_HEAD) ||
        after.u8(world::sobj::TYPE) != out.type)
        throw Error("The actor changed during the refresh; snapshot discarded.");
     
    const world::PoolHeader objects2 = world::readPool(mem, world::addr::SOBJECT_POOL);
    const world::AccessorPool pool2 = world::readAccessorPool(mem);
    if (objects2.base != objects.base || objects2.stride != objects.stride || objects2.capacity != objects.capacity || !pool2.sameLayout(pool))
        throw Error("The pools changed during the refresh (activity change?); snapshot discarded.");
    return out;
}

 

std::string statLabel(uint32_t id) {
    char b[96];
    if (id & 0x80000000u) std::snprintf(b, sizeof b, "Biped investment offset +%X", id & 0x3FFFFFFFu);
    else if (id & 0x40000000u) std::snprintf(b, sizeof b, "Weapon investment offset +%X", id & 0x3FFFFFFFu);
    else std::snprintf(b, sizeof b, "Named stat reference %08X", id);
    return b;
}

StatRegistry parseStatRegistry(const world::Bytes& hdr, const world::Bytes& data) {
    StatRegistry r;
    r.base = hdr.u32(8);
    r.stride = hdr.u32(0xC);
    r.capacity = hdr.u32(0x10);
    unsigned total = 0;
    for (size_t i = 0; i < kStatCategories; ++i) {
        r.counts[i] = hdr.u16(0x80 + uint32_t(i) * 2);
        total += r.counts[i];
    }
    if (r.stride != kStatsStride || r.capacity == 0 || r.capacity > 1024 || !world::heap(r.base, r.capacity * r.stride))
        throw Error("The registered stat pool layout differs from this build (stride " + hex8(r.stride) + ", base " + hex8(r.base) + ").");
    if (total > r.capacity) throw Error("The stat category counts exceed the pool capacity: refresh after the activity has loaded.");
    if (data.size() < size_t(r.capacity) * r.stride) throw Error("The stat array read is short.");
    std::set<std::pair<unsigned, unsigned>> seen;
    for (uint32_t i = 0; i < r.capacity; ++i) {
        const uint32_t off = i * r.stride;
        const uint32_t handle = data.u32(off + 0xC);
        if (!world::validHandle(handle) || (handle & 0x1FFF) != i) continue;
        const uint8_t category = data.u8(off + 4);
        const uint16_t index = data.u16(off + 2);
        if (category >= kStatCategories) continue;
        if (index >= r.counts[category] || !seen.emplace(category, index).second)
            throw Error("A registered stat category / index is inconsistent: refresh after the activity has loaded.");
        r.refs.push_back({handle, data.u32(off + 8), r.base + off, index, category});
    }
    return r;
}

StatRegistry readStatRegistry(world::Reader& mem) {
    const world::Bytes hdr = mem.bytes(addr::KNOWN_STATS_POOL, 0x9E);
    const uint32_t base = hdr.u32(8), stride = hdr.u32(0xC), cap = hdr.u32(0x10);
    if (stride != kStatsStride || cap == 0 || cap > 1024 || !world::heap(base, cap * stride))
        throw Error("The registered stat pool is not set up (base " + hex8(base) + ", stride " + hex8(stride) + ").");
    const world::Bytes data = mem.bytes(base, cap * stride);
    StatRegistry r = parseStatRegistry(hdr, data);
    const world::Bytes hdr2 = mem.bytes(addr::KNOWN_STATS_POOL, 0x9E);
    const world::Bytes data2 = mem.bytes(base, cap * stride);
    if (hdr2.data() != hdr.data() || data2.data() != data.data()) throw Error("The registered stat table changed while reading; refresh.");
    return r;
}

StatusAggregate parseStatus(const world::Bytes& b, uint32_t handle) {
    if (b.size() < 0x74) throw Error("The status component is too small.");
    if (b.u32(0x14) != handle) throw Error("The status component's self-handle (+0x14) does not match the player's slot.");
    const uint32_t count = b.u32(0x20);
    if (count > kStatusMaxRecords) throw Error("The status aggregate holds " + std::to_string(count) + " records (native capacity 4).");
    StatusAggregate s;
    s.handle = handle;
    s.data = b.addr();
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t o = 0x24 + i * 20;
        StatusRecord r;
        r.id = b.u32(o);
        r.raw = b.u32(o + 4);
        r.value = world::bitsFloat(r.raw);
        r.priority = int32_t(b.u32(o + 8));
        r.flags = b.u8(o + 12);
        r.contributions = int32_t(b.u32(o + 16));
        s.records.push_back(r);
    }
    return s;
}

StatusAggregate readStatus(world::Reader& mem) {
    const world::LocalPlayer p = world::requireLocalPlayer(mem);
    const uint32_t handle = mem.u32(p.element + kStatusSlot);
    if (!world::validHandle(handle)) throw Error("The player has no registered status component (descriptor +0x25BC is empty).");
    const world::AccessorPool pool = world::readAccessorPool(mem);
    const auto acc = world::resolveComponent(mem, pool, handle, true);
    if (!acc || !acc->allocated() || acc->size() < 0x74 || !world::heap(acc->data, acc->size()))
        throw Error("The status component accessor for " + hex8(handle) + " is not live.");
     
     
    const uint32_t head = p.object.componentHead;
    const auto arenas = world::readArenaChain(mem, pool, head);
    const bool inside = std::any_of(arenas.begin(), arenas.end(), [&](const world::Arena& a) {
        return acc->data >= a.data && uint64_t(acc->data) + acc->size() <= uint64_t(a.data) + a.size;
    });
    const bool isArena = std::any_of(arenas.begin(), arenas.end(), [&](const world::Arena& a) { return pool.entry(a.index) == acc->addr; });
    if (!inside || isArena)
        throw Error("The status component " + hex8(handle) + " is not one of the player's components (outside its +0x3C arena chain).");
    const world::Bytes b = mem.bytes(acc->data, 0x80);
    StatusAggregate s = parseStatus(b, handle);
    s.accessor = acc->addr;
    s.size = acc->size();
    s.cls = acc->cls();
    s.owner = p.handle;
     
    if (mem.u32(p.element + kStatusSlot) != handle || mem.u32(p.element + world::player::ELEM_HANDLE) != p.handle ||
        mem.u32(p.entry + world::sobj::COMPONENT_HEAD) != head)
        throw Error("The player changed while reading; refresh.");
    const world::Bytes e2 = mem.bytes(acc->addr, world::kAccessorStride);
    const world::Bytes b2 = mem.bytes(acc->data, 0x80);
    if (e2.u32(0) != acc->flags || e2.u32(8) != acc->backOffset || e2.u32(0xC) != handle || b2.data() != b.data())
        throw Error("The status aggregate changed while reading; refresh.");
    return s;
}

 

const char* hostilityName(Hostility h) {
    switch (h) {
    case Hostility::Friendly: return "Friendly";
    case Hostility::Hostile: return "Hostile";
    case Hostility::Unknown: return "Unknown";
    }
    return "?";
}

Hostility hostilityOf(int mine, int theirs, const std::array<uint8_t, 64>* allegiance) {
    if (mine < 0 || theirs < 0) return Hostility::Unknown;
    if (mine == theirs) return Hostility::Friendly;
    if (allegiance && mine < world::kFactionCount && theirs < world::kFactionCount)
        return world::allegiancePair(*allegiance, mine, theirs) ? Hostility::Friendly : Hostility::Hostile;
    return Hostility::Hostile;
}

std::vector<EspEntity> nearby(const world::ObjectScan& scan, uint32_t playerHandle, const world::Vec3& origin, const EspOptions& o,
                              const world::FactionTable* factions, int mine, const std::array<uint8_t, 64>* allegiance, size_t* inRange) {
    std::vector<EspEntity> out;
    size_t n = 0;
    for (const world::SObject& s : scan.objects) {
        if (!s.live() || s.handle == playerHandle) continue;
        const bool biped = s.type == world::TYPE_BIPED;
        const bool vehicle = s.type == world::TYPE_VEHICLE || s.type == world::TYPE_TURRET;
        if (!(biped ? o.bipeds : vehicle ? o.vehicles : o.others)) continue;
        if (!biped && !vehicle && s.type != world::TYPE_CREATURE) continue;    
        const world::Vec3& p = s.position;
        if (!p.finite() || (p.x == 0 && p.y == 0 && p.z == 0)) continue;
        const float d = world::distance(p, origin);
        if (!(d <= o.radius)) continue;
        ++n;
        EspEntity e;
        e.handle = s.handle;
        e.entry = s.addr;
        e.type = s.type;
        e.position = p;
        e.distance = d;
        e.faction = factions ? factions->of(s.handle) : int8_t(-1);
        e.hostility = hostilityOf(mine, e.faction, allegiance);
        if ((e.hostility == Hostility::Friendly && o.hideFriendly) || (e.hostility == Hostility::Hostile && o.hideHostile) ||
            (e.hostility == Hostility::Unknown && o.hideUnknown))
            continue;
        out.push_back(e);
    }
    std::sort(out.begin(), out.end(), [](const EspEntity& a, const EspEntity& b) { return a.distance < b.distance; });
    if (out.size() > o.maxEntities) out.resize(o.maxEntities);
    if (inRange) *inRange = n;
    return out;
}

void EspScanner::reset() {
    cache_.invalidate();
    tags_.clear();
    tagTable_.reset();
    allegiance_.reset();
    allegianceAt_ = {};
}

EspSnapshot EspScanner::scan(world::Reader& mem, const EspOptions& o) {
    EspSnapshot s;
    const world::LocalPlayer p = cache_.require(mem, 2000, true);
    s.player = p.handle;
    s.playerFaction = p.faction;
    const world::ObjectScan objects = world::readObjects(mem);
    s.scanned = objects.objects.size();
    s.origin = p.object.position;
    for (const world::SObject& obj : objects.objects)
        if (obj.handle == p.handle) s.origin = obj.position;    
    std::optional<world::FactionTable> factions;
    try {
        factions = world::readFactionTable(mem);
    } catch (const std::exception&) {
    }
    const auto now = std::chrono::steady_clock::now();
    if (!allegiance_ || now - allegianceAt_ > std::chrono::seconds(30)) {
        try {
            const world::Allegiance a = world::readAllegiance(mem);
            if (a.owned()) allegiance_ = a.bits;
            else allegiance_.reset();
        } catch (const std::exception&) {
            allegiance_.reset();
        }
        allegianceAt_ = now;
    }
    s.allegianceKnown = allegiance_.has_value();
    s.entities = nearby(objects, p.handle, s.origin, o, factions ? &*factions : nullptr, p.faction, allegiance_ ? &*allegiance_ : nullptr,
                        &s.inRange);
     
    if (!tagTable_) {
        try {
            tagTable_ = world::readTagTable(mem);
        } catch (const std::exception&) {
        }
    }
    int budget = 6;
    for (EspEntity& e : s.entities) {
        auto it = tags_.find(e.handle);
        if (it != tags_.end()) {
            e.tag = it->second;
            continue;
        }
        if (!tagTable_ || budget-- <= 0) continue;
        world::SObject so;
        for (const world::SObject& obj : objects.objects)
            if (obj.handle == e.handle) so = obj;
        e.tag = world::tagOfObject(mem, *tagTable_, so);
        tags_[e.handle] = e.tag;
    }
    if (tags_.size() > 4096) tags_.clear();
    return s;
}

}   
