#include "game/hopons_attach.h"

#include <ctime>

#include "core/error.h"
#include "core/ppc.h"
#include "core/runtime.h"
#include "game/hopons_addr.h"
#include "game/world_addr.h"
#include "game/world_objects.h"

namespace brps3::hopons {

using guard::Cmp;
using guard::Cond;
using guard::Width;

const char* hopResultText(HopResult r) {
    switch (r) {
    case HopResult::Ok: return "Done";
    case HopResult::BuildMismatch: return "Game build does not match (class records differ)";
    case HopResult::InvalidTarget: return "Target is gone or changed";
    case HopResult::NotResident: return "Definition is not loaded";
    case HopResult::UnsupportedType: return "Definition and target types do not fit";
    case HopResult::BuildFailed: return "The game could not build the placement";
    case HopResult::AttachFailed: return "The game attached nothing";
    case HopResult::TrackingFailed: return "Created, but it could not be verified (do not retry)";
    case HopResult::NotOwned: return "Not removable (not an attached receipt)";
    case HopResult::Expired: return "No longer attached: nothing removed";
    case HopResult::Duplicate: return "Already attached to that target";
    case HopResult::Refused: return "Refused: the target or definition changed";
    }
    return "?";
}

const char* lifeText(Life l) {
    switch (l) {
    case Life::Unverified: return "Created, unverified";
    case Life::Active: return "Attached";
    case Life::RemovalRequested: return "Removal requested";
    case Life::Gone: return "No longer attached";
    case Life::Removed: return "Removed";
    }
    return "?";
}

 

HopDefinition checkDefinition(Reader& mem, TagReader& tags, uint32_t tag) {
    HopDefinition d;
    d.tag = tag;
    auto fail = [&](HopResult r, std::string why) {
        d.status = r;
        d.why = std::move(why);
        return d;
    };
    if (!world::isPackagePage(world::tagPage(tag))) return fail(HopResult::NotResident, hex8(tag) + " is not a package tag");
    std::string why;
    const auto m = tags.map(tag, world::kPackageItemStride, &why);
    if (!m) return fail(HopResult::NotResident, why);
    d.map = *m;
    const auto reg = mem.tryBytes(m->registration, 0x30);
    if (!reg) return fail(HopResult::NotResident, "page registration " + hex8(m->registration) + " is unreadable");
    d.limit = reg->u32(0x10);
    if (!d.limit || d.limit > 8192 || m->index >= d.limit) return fail(HopResult::NotResident, "index is beyond the page limit");
    d.nativeClass = reg->u32(0x2C);
    if (reg->u32(0x24) == 0xFFFFFFFFu) {
        const uint32_t rb = reg->u32(8), rs = reg->u32(0xC);
        if (rs != 0x10 || !storage(rb, d.limit * rs)) return fail(HopResult::NotResident, "per-entry class array is not plausible");
        d.nativeClass = mem.u32(rb + m->index * rs);
    }
    if (m->word != cls::ENTITY || d.nativeClass != m->word)
        return fail(HopResult::NotResident, "item class " + hex8(m->word) + " / registered " + hex8(d.nativeClass) + " is not an entity definition");
    d.classRecord = mem.u32(addr::CLASS_DEFINITION_PTR);
    if (!storage(d.classRecord) || mem.u32(d.classRecord) != cls::ENTITY)
        return fail(HopResult::BuildMismatch, "definition class record *(" + hex8(addr::CLASS_DEFINITION_PTR) + ") does not hold 80800734");
    if (!storage(m->body, 0x5B)) return fail(HopResult::NotResident, "body " + hex8(m->body) + " is not plausible");
    const uint32_t relative = mem.u32(m->body + 4);
    if (!relative) return fail(HopResult::NotResident, "body has no definition");
    d.definition = m->body + 4 + relative;
    if (!world::mapped(d.definition, 0x4F)) return fail(HopResult::NotResident, "definition " + hex8(d.definition) + " is not plausible");
    d.type = mem.bytes(d.definition + 0x4E, 1).u8(0);
    if (!isHopType(d.type))
        return fail(HopResult::UnsupportedType, "type " + std::to_string(d.type) + " (" + world::typeName(d.type) + ") is not a hop-on / attachment");
    d.status = HopResult::Ok;
    return d;
}

HopResult checkCompatible(uint8_t defType, uint8_t targetType, std::string* why) {
    if (defType == TYPE_BIPED_ATTACHMENT && targetType != world::TYPE_BIPED) {
        if (why) *why = std::string("biped attachments need a biped target (this is a ") + world::typeName(targetType) + ")";
        return HopResult::UnsupportedType;
    }
    if (defType == TYPE_WEAPON_ATTACHMENT && targetType != world::TYPE_WEAPON) {
        if (why) *why = std::string("weapon attachments need a weapon target (this is a ") + world::typeName(targetType) + ")";
        return HopResult::UnsupportedType;
    }
    return HopResult::Ok;
}

 

std::optional<AccessorPoolInfo> readAccessorPoolInfo(Reader& mem, std::string* why) {
    const Bytes b = mem.bytes(addr::ACCESSOR_POOL, 0x28);
    AccessorPoolInfo p;
    p.base = b.u32(8);
    p.stride = b.u32(0xC);
    p.capacity = b.u32(0x10);
    p.mask = b.u32(0x24);
    if (p.stride != 0x18 || !p.capacity || p.capacity > 8192 || !storage(p.base, p.capacity * p.stride)) {
        if (why) *why = "component accessor pool " + hex8(addr::ACCESSOR_POOL) + " is not plausible";
        return std::nullopt;
    }
    return p;
}

std::optional<uint32_t> handleIdentity(Reader& mem, uint32_t handle, std::string* why) {
    const uint32_t reg = registrationOf(mem, handle, why);
    if (!reg) return std::nullopt;
    const Bytes r = mem.bytes(reg, 0x20);
    const uint32_t desc = r.u32(0), base = r.u32(8), offset = r.u32(0x18), stride = r.u32(0x1C);
    if (!storage(desc, 0x20) || !base || !stride || stride > 0x10000 || offset > 0x100000) {
        if (why) *why = "registration " + hex8(reg) + " is not plausible";
        return std::nullopt;
    }
    const uint32_t cap = mem.bytes(desc + 0x1C, 2).u16(0), index = handle & 0x1FFF;
    if (!cap || cap > 8192 || index >= cap) {
        if (why) *why = "handle index is outside its pool";
        return std::nullopt;
    }
    const uint64_t a = uint64_t(base) + offset + uint64_t(index) * stride;
    if (a > 0xFFFFFFFCull || !storage(uint32_t(a))) {
        if (why) *why = "identity word is outside memory";
        return std::nullopt;
    }
    return mem.u32(uint32_t(a));
}

std::optional<AccessorInfo> readAccessor(Reader& mem, uint32_t accessor, std::string* why) {
    auto no = [&](std::string w) -> std::optional<AccessorInfo> {
        if (why) *why = std::move(w);
        return std::nullopt;
    };
    if (!world::validHandle(accessor)) return no("no accessor");
    const uint32_t reg = registrationOf(mem, accessor, why);
    if (reg != addr::ACCESSOR_POOL) return no("accessor " + hex8(accessor) + " is not registered to the component accessor pool");
    const auto pool = readAccessorPoolInfo(mem, why);
    if (!pool) return std::nullopt;
    const uint32_t idx = accessor & 0x1FFF;
    if (idx >= pool->capacity) return no("accessor index is outside the pool");
    AccessorInfo a;
    a.accessor = accessor;
    a.entry = pool->base + idx * pool->stride;
    const Bytes e = mem.bytes(a.entry, 0x18);
    a.word = e.u32(0);
    a.instance = e.u32(4);
    a.backOffset = e.u32(8);
    a.canonical = e.u32(0xC);
    const uint32_t size = a.word & 0x0FFFFFFFu;
    if ((a.word & 0xF0000000u) != 0xC0000000u || size < 0x20 || size > 0x100000) return no("accessor entry " + hex8(a.entry) + " is not a live allocation");
    a.body = a.entry - (a.backOffset & pool->mask);
    if (!storage(a.body, size)) return no("component body " + hex8(a.body) + " is not plausible");
    const Bytes b = mem.bytes(a.body + 0x14, 0xC);
    if (b.u32(0) != accessor) return no("component +0x14 holds " + hex8(b.u32(0)) + ", not the accessor (slot reused)");
    a.target = b.u32(8);
    const auto identity = handleIdentity(mem, a.canonical, why);
    if (!identity) return std::nullopt;
    a.identity = *identity;
    return a;
}

bool stillOwned(Reader& mem, const Receipt& r, std::string* why) {
    if (!world::readSObject(mem, r.target)) {
        if (why) *why = "target " + hex8(r.target) + " is gone";
        return false;
    }
    const auto now = readAccessor(mem, r.acc.accessor, why);
    if (!now) return false;
    if (now->target != r.target || now->body != r.acc.body || now->instance != r.acc.instance || now->canonical != r.acc.canonical ||
        now->identity != r.acc.identity) {
        if (why) *why = "the accessor now describes another instance";
        return false;
    }
    return true;
}

size_t refreshReceipts(Reader& mem, std::vector<Receipt>& receipts) {
    size_t changed = 0;
    for (Receipt& r : receipts) {
        const Life before = r.life;
        std::string why;
        switch (r.life) {
        case Life::Active:
            if (!stillOwned(mem, r, &why)) {
                r.life = Life::Gone;
                r.note = why;
            }
            break;
        case Life::Unverified: {
            const auto info = readAccessor(mem, r.acc.accessor, &why);
            if (info && info->target == r.target && world::readSObject(mem, r.target)) {
                r.acc = *info;
                r.life = Life::Active;
                r.note = "verified";
            } else {
                r.life = Life::Gone;
                r.note = why.empty() ? std::string("accessor does not describe this target") : why;
            }
            break;
        }
        case Life::RemovalRequested:
            if (!stillOwned(mem, r, &why)) {
                r.life = Life::Removed;
                r.note = "removed";
            }
            break;
        default: break;
        }
        if (r.life != before) ++changed;
    }
    return changed;
}

 

std::vector<uint8_t> applyStubProgram(uint32_t base) {
    using ppc::Asm;
    constexpr int64_t FRAME = 0xB0;
    const std::vector<int> save = {2, 28, 29, 30, 31};    
    Asm a(base);
    ppc::prologue(a, FRAME, save);
    a.mr(31, 3).mr(30, 4).mr(29, 5).mr(28, 6);
    a.li(0, -1).stw(0, 0, 28);                             
    a.mr(3, 31).mr(4, 30).bl(addr::PLACEMENT_DATA_NEW);    
    a.ld(2, 0x70, 1);
    a.andi_(0, 3, 0xFF).beq("fail");
    a.mr(3, 28).mr(4, 31).mr(5, 29).bl(addr::HOP_ATTACH);  
    a.ld(2, 0x70, 1);
    a.li(3, 1).b("out");
    a.label("fail").li(3, 0);
    a.label("out");
    ppc::epilogue(a, FRAME, save);
    a.blr();
    return a.link();
}

uint32_t applyStubSize() {
    static const uint32_t size = (uint32_t(applyStubProgram(0x183E400).size()) + 15u) & ~15u;
    return size;
}

uint32_t installApplyStub(Runtime& rt) {
    return rt.sharedCode(kApplyStubName, [](uint32_t base) { return applyStubProgram(base); }, applyStubSize());
}

std::vector<uint8_t> arenaBytes() {
    std::vector<uint8_t> a(kArenaSize, 0);
    auto put = [&](uint32_t off, uint32_t v) {
        a[off] = uint8_t(v >> 24);
        a[off + 1] = uint8_t(v >> 16);
        a[off + 2] = uint8_t(v >> 8);
        a[off + 3] = uint8_t(v);
    };
    put(0x8, 0xFFFFFFFFu);
    put(0xC, 0x800);
    put(0x10, 8);
    return a;
}

guard::Request applyRequest(uint32_t stub, const HopDefinition& d, uint32_t target, uint8_t targetType, uint32_t arena) {
    guard::Request q(stub);
    q.scratch.assign(0x10, 0xFF);                          
    q.arg(arena).arg(d.tag).arg(target).scratchArg(0);
    q.sobject(target);
    q.require(Cond::entry(world::sobj::TYPE, Cmp::Eq, targetType, Width::U8, 0xFF, "target type unchanged"));
    q.require(Cond::abs(addr::TAG_TABLE_PTR, Cmp::Eq, d.map.manager, Width::U32, 0xFFFFFFFFu, "tag table manager unchanged"));
    q.require(Cond::ptr(addr::TAG_TABLE_PTR, {0}, Cmp::Eq, d.map.tableBase, "tag table base unchanged"));
    q.require(Cond::abs(d.map.desc + 0x10, Cmp::Eq, d.map.array, Width::U32, 0xFFFFFFFFu, "definition page still loaded"));
    q.require(Cond::abs(d.map.entry, Cmp::Eq, cls::ENTITY, Width::U32, 0xFFFFFFFFu, "definition still resident"));
    q.require(Cond::abs(d.map.entry + 8, Cmp::Eq, d.map.backOffset, Width::U32, 0xFFFFFFFFu, "definition item unchanged"));
    q.require(Cond::abs(d.definition + 0x4E, Cmp::Eq, d.type, Width::U8, 0xFF, "definition type unchanged"));
    q.require(Cond::ptr(addr::CLASS_DEFINITION_PTR, {0}, Cmp::Eq, cls::ENTITY, "definition class record"));
    return q;
}

guard::Request removeRequest(const Receipt& r, uint32_t accessorPoolBase) {
    guard::Request q(addr::HOP_REMOVE);
    q.arg(r.acc.accessor);
    q.sobject(r.target);
    q.require(Cond::abs(addr::ACCESSOR_POOL + 8, Cmp::Eq, accessorPoolBase, Width::U32, 0xFFFFFFFFu, "accessor pool unchanged"));
    q.require(Cond::abs(r.acc.entry, Cmp::Eq, r.acc.word, Width::U32, 0xFFFFFFFFu, "accessor allocation word"));
    q.require(Cond::abs(r.acc.entry + 4, Cmp::Eq, r.acc.instance, Width::U32, 0xFFFFFFFFu, "accessor instance"));
    q.require(Cond::abs(r.acc.entry + 8, Cmp::Eq, r.acc.backOffset, Width::U32, 0xFFFFFFFFu, "accessor body offset"));
    q.require(Cond::abs(r.acc.entry + 0xC, Cmp::Eq, r.acc.canonical, Width::U32, 0xFFFFFFFFu, "owner arena handle"));
    q.require(Cond::abs(r.acc.body + 0x14, Cmp::Eq, r.acc.accessor, Width::U32, 0xFFFFFFFFu, "component holds this accessor"));
    q.require(Cond::abs(r.acc.body + 0x1C, Cmp::Eq, r.target, Width::U32, 0xFFFFFFFFu, "component is on this target"));
    return q;
}

ApplyOutcome applyHop(Runtime& rt, Reader& mem, uint32_t tag, uint32_t target, bool self, const std::vector<Receipt>& existing, uint32_t id,
                      const std::function<void(const std::string&)>& log) {
    auto say = [&](const std::string& s) {
        if (log) log(s);
    };
    ApplyOutcome out;
    auto fail = [&](HopResult r, const std::string& msg) {
        out.result = r;
        out.message = msg.empty() ? hopResultText(r) : std::string(hopResultText(r)) + ": " + msg;
        say(out.message);
        return out;
    };
    const auto obj = world::readSObject(mem, target);
    if (!obj) return fail(HopResult::InvalidTarget, "handle " + hex8(target) + " is stale or being deleted");
    TagReader tags(mem);
    const HopDefinition d = checkDefinition(mem, tags, tag);
    if (d.status != HopResult::Ok) return fail(d.status, d.why);
    std::string why;
    if (checkCompatible(d.type, obj->type, &why) != HopResult::Ok) return fail(HopResult::UnsupportedType, why);
    for (const Receipt& r : existing) {
        if (r.tag != tag || r.target != target) continue;
        if (r.life == Life::Active || r.life == Life::RemovalRequested) {
            if (stillOwned(mem, r)) return fail(HopResult::Duplicate, "remove the tracked one first");
        } else if (r.life == Life::Unverified) {
            return fail(HopResult::Duplicate, "an unverified attachment of it exists: verify first");
        }
    }
    say("definition " + hex8(tag) + ": " + std::string(hopResultText(HopResult::Ok)) + ", type " + std::to_string(d.type) + ", item " + hex8(d.map.entry) +
        ", definition " + hex8(d.definition) + "; target " + hex8(target) + " (" + world::typeName(obj->type) + ")");
    const uint32_t stub = installApplyStub(rt);
    const std::vector<uint8_t> arena = arenaBytes();
    const uint32_t arenaAddr = rt.alloc(kArenaSize, arena.data(), arena.size(), 16);
    guard::Result res;
    try {
        res = guard::call(rt, applyRequest(stub, d, target, obj->type, arenaAddr), 3000, true);
    } catch (...) {
        rt.freeLater(arenaAddr);    
        throw;
    }
    try {
        rt.free(arenaAddr);
    } catch (const std::exception&) {
        rt.freeLater(arenaAddr);
    }
    if (!res.called) return fail(HopResult::Refused, res.reason);
    if ((res.r3 & 0xFF) == 0) return fail(HopResult::BuildFailed, "0x1F7F74 returned false");
    const uint32_t accessor = res.scratchU32(0);
    if (accessor == 0xFFFFFFFFu) return fail(HopResult::AttachFailed, "0x1F7D7C left the accessor at -1");
    Receipt rc;
    rc.id = id;
    rc.tag = tag;
    rc.target = target;
    rc.self = self;
    rc.type = d.type;
    rc.targetType = obj->type;
    rc.acc.accessor = accessor;
    rc.life = Life::Unverified;
    rc.appliedAt = int64_t(std::time(nullptr));
    const auto info = readAccessor(mem, accessor, &why);
    if (!info || info->target != target) {
        rc.note = info ? "accessor targets " + hex8(info->target) : why;
        out.receipt = rc;
        return fail(HopResult::TrackingFailed, rc.note);
    }
    rc.acc = *info;
    rc.life = Life::Active;
    rc.note = "attached";
    out.receipt = rc;
    out.result = HopResult::Ok;
    out.message = "attached: accessor " + hex8(accessor) + ", body " + hex8(info->body);
    say(out.message);
    return out;
}

HopResult removeHop(Runtime& rt, Reader& mem, Receipt& r, const std::function<void(const std::string&)>& log) {
    auto say = [&](const std::string& s) {
        if (log) log(s);
    };
    if (r.life != Life::Active) {
        r.note = std::string("not removable (") + lifeText(r.life) + ")";
        say(r.note);
        return HopResult::NotOwned;
    }
    std::string why;
    if (!stillOwned(mem, r, &why)) {
        r.life = Life::Gone;
        r.note = why;
        say("no longer attached: " + why);
        return HopResult::Expired;
    }
    const auto pool = readAccessorPoolInfo(mem, &why);
    if (!pool) {
        r.note = why;
        return HopResult::Refused;
    }
    const guard::Result res = guard::call(rt, removeRequest(r, pool->base), 3000, false);
    if (!res.called) {
        r.note = "refused: " + res.reason;
        say(r.note);
        return HopResult::Refused;
    }
    r.life = Life::RemovalRequested;
    r.note = "removal requested";
    if (!stillOwned(mem, r)) {
        r.life = Life::Removed;
        r.note = "removed";
    }
    say(hex8(r.tag) + " on " + hex8(r.target) + ": " + r.note);
    return HopResult::Ok;
}

}   
