#include "game/lab_effects.h"

#include "core/error.h"
#include "game/guard_call.h"

namespace brps3::lab {

using world::hex8;

const Fingerprints kEffectFingerprints = {
    {kEffectTypeGetter, "7C0802A648BAA9BDF821FF61F80100B0"},
    {kComponentLookupByType, "7C0802A648C516D9F821F951F80106C0"},
};

EffectType readEffectType(world::Reader& mem, const world::TagTable& t) {
    EffectType e;
    e.type = readTypeKey(mem, t, kTypeAnchorEffect);
    e.descriptorExpected = e.type.descriptor == kEffectDescriptor;
    if (e.type.ok() && !e.descriptorExpected)
        e.type.why = "the effect anchor holds " + hex8(e.type.descriptor) + ", expected " + hex8(kEffectDescriptor);
    return e;
}

bool channelOffset(uint32_t field, uint32_t relative, uint32_t count, uint32_t stride, uint32_t extent, uint32_t& out) {
    const int64_t displacement = (relative & 0x80000000u) ? int64_t(relative) - 0x100000000LL : int64_t(relative);
    const int64_t at = int64_t(field) + 16 + displacement;
    if (at < 0 || at % 4 || uint64_t(at) > extent || !stride || count > (extent - uint32_t(at)) / stride) return false;
    out = uint32_t(at);
    return true;
}

size_t EffectParameters::keyCount() const {
    size_t n = 0;
    for (const ParamComponent& c : components) n += c.keys.size();
    return n;
}

namespace {

 
bool selfRelative(uint32_t field, uint32_t relative, uint32_t extent, uint32_t need, uint32_t& out) {
    const int64_t delta = (relative & 0x80000000u) ? int64_t(relative) - 0x100000000LL : int64_t(relative);
    const int64_t at = int64_t(field) + delta;
    if (!relative || at < 0 || at % 4 || uint64_t(at) > extent || need > extent - uint32_t(at)) return false;
    out = uint32_t(at);
    return true;
}

}   

EffectParameters decodeEffectParameters(WordSource& def, uint32_t defExtent, WordSource& rec, uint32_t recSize, uint32_t defTag) {
    EffectParameters p;
    p.recordSize = recSize;
    try {
        if (recSize < 0x60 || recSize > 0x100000) {
            p.why = "the effect record size " + hex8(recSize) + " is outside 0x60..0x100000";
            return p;
        }
        if (rec.word(0) != defTag) {
            p.why = "record +0 is " + hex8(rec.word(0)) + ", not the definition tag " + hex8(defTag);
            return p;
        }
        p.root = rec.word(4);
        if (p.root % 4 || p.root > defExtent || 0xC8 > defExtent - p.root) {
            p.why = "root +" + hex8(p.root) + " is outside the definition";
            return p;
        }
        p.count = def.word(p.root + 0xC0);
        uint32_t array = 0;
        if (p.count > 1024 || !channelOffset(p.root + 0xC4, def.word(p.root + 0xC4), p.count, 4, defExtent, array)) {
            p.why = "the parameter component array (count " + std::to_string(p.count) + ") is outside the definition";
            return p;
        }
        uint32_t table = 0;
        const bool tableOk = channelOffset(0x5C, rec.word(0x5C), p.count, 4, recSize, table);
        for (uint32_t ci = 0; ci < p.count; ++ci) {
            ParamComponent c;
            c.index = ci;
            try {
                const uint32_t field = array + ci * 4;
                if (!selfRelative(field, def.word(field), defExtent, 0x20, c.offset)) {
                    c.why = "authored component pointer is invalid";
                    p.components.push_back(std::move(c));
                    continue;
                }
                c.kind = def.byte(c.offset + 0x1C);
                if (c.kind == 2 && 0x34 <= defExtent - c.offset) {
                    const uint32_t raw = def.word(c.offset + 0x2C);
                    uint32_t keysAt = 0;
                    if (raw && !(raw & 0x80000000u)) {
                        if (raw > 4096 || !channelOffset(c.offset + 0x30, def.word(c.offset + 0x30), raw, 4, defExtent, keysAt))
                            c.why = "the key array is outside the definition";
                        else
                            for (uint32_t i = 0; i < raw; ++i) c.keys.push_back(def.word(keysAt + i * 4));
                    }
                }
                if (!tableOk) {
                    if (c.why.empty()) c.why = "the runtime table (record +0x5C) is outside the record";
                } else {
                    const uint32_t rf = table + ci * 4;
                    if (!selfRelative(rf, rec.word(rf), recSize, 0x1C, c.runtime)) {
                        c.runtime = 0;
                        if (c.why.empty()) c.why = "runtime component pointer is invalid";
                    } else {
                        c.notification = rec.word(c.runtime + 0x10);
                        if (!c.keys.empty()) {
                            if (!channelOffset(c.runtime + 0x18, rec.word(c.runtime + 0x18), uint32_t(c.keys.size()), 4, recSize, c.valuesAt)) {
                                c.valuesAt = 0;
                                if (c.why.empty()) c.why = "the runtime value array is outside the record";
                            } else {
                                for (size_t i = 0; i < c.keys.size(); ++i) c.values.push_back(rec.word(c.valuesAt + uint32_t(i) * 4));
                            }
                        }
                    }
                }
            } catch (const std::exception& e) {
                c.why = e.what();
            }
            p.components.push_back(std::move(c));
        }
    } catch (const std::exception& e) {
        p.why = e.what();
    }
    return p;
}

EffectParameters readEffectParameters(world::Reader& mem, const world::TagTable& t, uint32_t record, uint32_t recordSize) {
    EffectParameters p;
    p.record = record;
    p.recordSize = recordSize;
    if (recordSize < 0x60 || recordSize > 0x100000 || !world::heap(record, recordSize)) {
        p.why = "the effect record " + hex8(record) + " (" + hex8(recordSize) + " bytes) is not a plausible heap allocation";
        return p;
    }
    const uint32_t tag = mem.u32(record);
    p.definition = readDefinition(mem, t, tag);
    if (!p.definition.ok()) {
        p.why = p.definition.why;
        return p;
    }
    MemorySource def(mem, p.definition.body, p.definition.extent);
    MemorySource rec(mem, record, recordSize);
    EffectParameters out = decodeEffectParameters(def, p.definition.extent, rec, recordSize, tag);
    out.definition = p.definition;
    out.record = record;
    return out;
}

NotificationPlan notificationPlan(const uint32_t keys[3], const uint32_t values[3], uint32_t key, uint32_t value) {
    NotificationPlan plan;
    for (uint32_t i = 0; i < 3; ++i) {
         
        if (keys[i] == 0x811C9DC5u) {
            plan.slot = i;
            plan.stored = plan.inserted = true;
            return plan;
        }
        if (keys[i] == key) {
            plan.slot = i;
            plan.stored = true;
            plan.forward = values[i] != value;
            return plan;
        }
    }
    return plan;    
}

NativeEffect nativeEffectOf(Runtime& rt, uint32_t owner, uint32_t typeHandle) {
    if (!world::validHandle(typeHandle)) throw Error("the effect type handle is not published (" + hex8(typeHandle) + "): load into an activity");
    guard::Request q(kEffectTypeGetter);
    q.scratch.assign(0x10, 0xFF);    
    q.scratchArg(0)
        .arg(owner)
        .sobject(owner)
        .require(guard::Cond::abs(kTypeAnchorEffect, guard::Cmp::Eq, kEffectDescriptor, guard::Width::U32, 0xFFFFFFFFu, "effect anchor -> 0x1AAF520"))
        .require(guard::Cond::abs(kEffectDescriptor, guard::Cmp::Eq, typeHandle, guard::Width::U32, 0xFFFFFFFFu,
                                  "effect type handle " + hex8(typeHandle) + " still published"));
    const guard::Result r = guard::call(rt, q, 3000, true);
    NativeEffect n;
    n.called = r.called;
    if (!r.called) n.refused = r.reason.empty() ? std::string(guard::statusName(r.status)) : r.reason;
    else n.handle = r.scratchU32(0);
    return n;
}

}   
