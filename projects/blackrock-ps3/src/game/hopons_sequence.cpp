#include "game/hopons_sequence.h"

#include <cmath>
#include <cstring>

#include "core/error.h"
#include "core/runtime.h"
#include "game/hopons_addr.h"
#include "game/world_objects.h"

namespace brps3::hopons {

using guard::Cmp;
using guard::Cond;
using guard::Width;

namespace {
Cond eq32(uint32_t addr, uint32_t value, const char* what) { return Cond::abs(addr, Cmp::Eq, value, Width::U32, 0xFFFFFFFFu, what); }

void identityConds(guard::Request& q, const ScriptRef& r) { instanceConds(q, r); }

void inputConds(guard::Request& q, uint32_t root, const std::array<uint32_t, 8>& input) {
    static const char* names[] = {"input word 0", "input word 1", "input word 2", "input word 3", "input word 4", "input word 5", "input word 6", "input word 7"};
    for (uint32_t i = 0; i < 8; ++i) q.require(eq32(root + 0x108 + i * 4, input[i], names[i]));
}

std::vector<uint8_t> refBlock(uint32_t block, const std::array<uint32_t, 8>& input) {
    std::vector<uint8_t> b(0x30, 0);
    auto put = [&](uint32_t off, uint32_t v) {
        b[off] = uint8_t(v >> 24);
        b[off + 1] = uint8_t(v >> 16);
        b[off + 2] = uint8_t(v >> 8);
        b[off + 3] = uint8_t(v);
    };
    put(0, cls::SEQUENCE_INPUT);
    put(4, block + 0x10);
    for (uint32_t i = 0; i < 8; ++i) put(0x10 + i * 4, input[i]);
    return b;
}
}   

bool sequenceDelay(uint32_t mode, uint32_t durationBits, uint32_t rateBits, uint32_t& delay) {
    delay = 0;
    if (mode == 2) return true;
    float duration;
    std::memcpy(&duration, &durationBits, 4);
    if (duration <= 0.0f) return true;                     
    if ((durationBits & 0x7F800000u) == 0x7F800000u) return false;
    if (!rateBits || (rateBits & 0x80000000u) || (rateBits & 0x7F800000u) == 0x7F800000u) return false;
    float rate;
    std::memcpy(&rate, &rateBits, 4);
    const float ticks = 1000.0f / rate;
    if (!(ticks >= 1.0f && ticks < 2147483648.0f)) return false;
    const int integerRate = int(ticks);                    
    const float roundedRate = float(integerRate);
    const float product = roundedRate * duration;
    if (!(product >= 0.0f && product < 2147483648.0f)) return false;
    uint32_t rounded = uint32_t(product);
    if (float(rounded) < product) ++rounded;
    if (rounded > 0x7FFFFFFFu) return false;
    delay = rounded;
    return true;
}

std::optional<ClockOwner> readClockOwner(Reader& mem, uint32_t slotHash, std::string& why) {
    ClockOwner o;
    auto no = [&](const std::string& w) -> std::optional<ClockOwner> {
        why = w;
        return std::nullopt;
    };
    o.managerHandle = mem.bytes(addr::REFMGR_HANDLE, 2).u16(0);
    const auto rec = contextRecord(mem, o.managerHandle, &why);
    if (!rec) return std::nullopt;
    if (rec->name != "client reference manager" || rec->extent < 0x16E10 || !storage(rec->buffer, rec->extent) || rec->w54 != 25 || rec->w58 != 1)
        return no("context " + hexShort(o.managerHandle) + " is not the client reference manager");
    o.allocation = rec->addr;
    o.buffer = rec->buffer;
    const Bytes header = mem.bytes(o.buffer, 8);
    if (header.u32(0) != 2 || header.u32(4) > 64) return no("reference manager header is not version 2");
    const uint32_t count = header.u32(4);
    const Bytes rows = mem.bytes(o.buffer + 8, count * 20);
    for (uint32_t i = 0; i < count; ++i)
        if (rows.u32(i * 20 + 8) == slotHash) {
            if (o.directory) return no("slot hash " + hex8(slotHash) + " is ambiguous in the reference directory");
            o.directory = o.buffer + 8 + i * 20;
        }
    if (!o.directory) return no("slot hash " + hex8(slotHash) + " has no reference directory row");
    o.worldHandle = mem.u32(o.directory + 12);
    const uint32_t index = o.worldHandle & 0x1FFF;
    o.lines.push_back("reference manager " + hex8(o.allocation) + " buffer " + hex8(o.buffer) + ", row " + hex8(o.directory) + " -> world " + hex8(o.worldHandle));
    const auto world = poolSlot(mem, addr::CLIENT_WORLDS_POOL, index, &why);
    if (!world) return no("activity client world: " + why);
    if (world->handle != o.worldHandle) return no("activity client world " + hex8(o.worldHandle) + " is stale (now " + hex8(world->handle) + ")");
    o.worldIdentity = world->identity;
    const auto global = poolSlot(mem, addr::CLIENT_GLOBALS_POOL, index, &why);
    if (!global) return no("activity client world globals: " + why);
    o.globalHandle = global->handle;
    o.globalIdentity = global->identity;
    if (registrationOf(mem, o.worldHandle) != addr::CLIENT_WORLDS_POOL || registrationOf(mem, o.globalHandle) != addr::CLIENT_GLOBALS_POOL)
        return no("world / globals handles are not registered to their pools");
    const Bytes wp = mem.bytes(addr::CLIENT_WORLDS_POOL + 0x30, 8), gp = mem.bytes(addr::CLIENT_GLOBALS_POOL + 0x30, 0xC);
    if (wp.u32(4) != gp.u32(0) || gp.u32(8) != wp.u32(0)) return no("the worlds / globals pool cross-links differ");
    TagReader tags(mem);
    const auto m = tags.map(o.globalHandle, 0, &why);
    if (!m) return no("world globals body: " + why);
    if (m->registration != addr::CLIENT_GLOBALS_POOL || !storage(m->body, 0x54)) return no("world globals page is not the globals pool");
    o.globalBody = m->body;
    const Bytes body = mem.bytes(o.globalBody, 0x40);
    o.rateBits = body.u32(0x14);
    const uint32_t cached = body.u32(0x3C);
    std::memcpy(&o.rate, &o.rateBits, 4);
    if (!o.rateBits || (o.rateBits & 0x80000000u) || (o.rateBits & 0x7F800000u) == 0x7F800000u || (cached & 0x7F800000u) == 0x7F800000u)
        return no("clock rate is not a positive finite number");
    const float ticks = 1000.0f / o.rate;
    if (!(ticks >= 1.0f && ticks < 2147483648.0f)) return no("1000 / rate is out of the native integer range");
    o.lines.push_back("world globals " + hex8(o.globalHandle) + " body " + hex8(o.globalBody) + ": rate " + std::to_string(o.rate) + " (" + std::to_string(int(ticks)) + " ms per tick)");
     
    if (mem.u32(addr::CLOCK_SOURCE_PTR) != addr::CLOCK_SOURCE_OBJECT || mem.u32(addr::CLOCK_SOURCE_OBJECT) != addr::CLOCK_SOURCE_VTABLE ||
        mem.u32(addr::CLOCK_SOURCE_VTABLE + 0x2C) != addr::CLOCK_SOURCE_OPD || mem.u32(addr::CLOCK_SOURCE_OPD) != addr::CLOCK_SOURCE_CODE)
        return no("the clock source chain 0x1897C44 -> 0x1AB9D7C -> 0x13E7CF0 +0x2C -> 0x2FB484 differs");
    o.timeSource = mem.u32(addr::GAME_TIME_A);
    if (!storage(o.timeSource, 24)) return no("game time source is not set up");
    const Bytes ts = mem.bytes(o.timeSource, 8);
    o.frequency = ts.u32(4);
    if (ts.u8(0) != 1 || !o.frequency || (o.frequency & 0x80000000u)) return no("game time source is not running (byte0 / frequency)");
    o.lines.push_back("clock source chain ok; game time " + hex8(o.timeSource) + " frequency " + std::to_string(o.frequency));
    return o;
}

std::optional<BundleInfo> readBundle(Reader& mem, const ScriptRef& ref, std::string& why) {
    BundleInfo b;
    if (ref.registration != addr::BUNDLE_REGISTRATION) {
        why = "the instance page registration " + hex8(ref.registration) + " is not client bundle instances (" + hex8(addr::BUNDLE_REGISTRATION) + ")";
        return std::nullopt;
    }
    b.handle = instanceHandle(mem, ref, &why);
    if (!b.handle) return std::nullopt;
    b.identity = ref.entryMeta;
    const Bytes desc = mem.bytes(ref.desc, 0x40);
    b.wrapperLinked = desc.u32(8) == addr::BUNDLE_INSTANCES;
    if (registrationOf(mem, b.handle) != addr::BUNDLE_REGISTRATION) {
        why = "the instance handle is not registered to client bundle instances";
        return std::nullopt;
    }
    const Bytes wrap = mem.bytes(addr::BUNDLE_ARRAY, 0x18);
    if (wrap.u32(0) != desc.u32(0x10) || wrap.u32(0x10) != 4 || wrap.u32(0x14) != 12) {
        why = "bundle header (+0x18 array, +0x28 = 4, +0x2C = 12) differs";
        return std::nullopt;
    }
    b.size = ref.entryWord & 0x3FFFFFFFu;
    b.allocHandle = mem.bytes(addr::BUNDLE_ALLOC_HANDLE, 2).u16(0);
    const auto rec = contextRecord(mem, b.allocHandle, &why);
    if (!rec) return std::nullopt;
    if (rec->name != "client bundle instances") {
        why = "context " + hexShort(b.allocHandle) + " is \"" + rec->name + "\", not client bundle instances (0x1A1D4F0 is likely only)";
        return std::nullopt;
    }
    b.allocation = rec->addr;
    b.payload = rec->buffer;
    b.extent = rec->extent;
    if (!storage(b.payload, b.extent) || ref.address < b.payload || uint64_t(ref.address) + b.size > uint64_t(b.payload) + b.extent) {
        why = "the root lies outside the bundle payload";
        return std::nullopt;
    }
    b.lines = {"instance handle " + hex8(b.handle) + " (identity " + hex8(b.identity) + "), size " + std::to_string(b.size),
               std::string("page descriptor +8 ") + (b.wrapperLinked ? "names the bundle wrapper 0x1A1D460" : "does NOT name 0x1A1D460 (Xbox rule differs on PS3)"),
               "payload context " + hexShort(b.allocHandle) + " at " + hex8(b.allocation) + ": " + hex8(b.payload) + " + " + hexShort(b.extent)};
    return b;
}

RestartPreflight preflightRestart(Reader& mem, const ScriptInstance& s) {
    RestartPreflight p;
    auto ok = [&](const std::string& t) { p.lines.push_back("ok: " + t); };
    auto fail = [&](const std::string& t) {
        p.lines.push_back("FAIL: " + t);
        p.ok = false;
        return p;
    };
    const EncounterDef& d = s.ref.def;
    if (d.cls != cls::SEQUENCE || d.inputSchema != cls::SEQUENCE_INPUT || d.size < 0x13C) return fail("not a constructed sequence instance");
    std::string why;
    if (!validateScript(mem, s.ref, &why)) return fail(why);
    ok("instance identity and source binding");
    const Bytes fresh = mem.bytes(s.ref.address + 0x108, 0x2C);
    for (uint32_t i = 0; i < 8; ++i) p.state.input[i] = fresh.u32(i * 4);
    p.state.selector = fresh.u8(4);
    p.state.pairIdentity = fresh.u32(0x20);
    p.state.pairAccessor = fresh.u32(0x24);
    p.state.state = fresh.u8(0x28);
    p.state.completed = fresh.u8(0x29);
    if (p.state.state != 2) return fail(std::string("state is ") + sequenceStateText(p.state.state) + ": restart needs a created sequence");
    const auto acc = readAccessor(mem, p.state.pairAccessor, &why);
    if (!acc || acc->canonical != p.state.pairAccessor || acc->identity != p.state.pairIdentity || !world::readSObject(mem, acc->target))
        return fail("the tracked effect does not resolve" + (why.empty() ? std::string() : ": " + why));
    p.tracked = acc;
    ok("tracked effect " + hex8(acc->accessor) + " on " + hex8(acc->target));
    if ((p.state.input[1] >> 24) == 0xFF) return fail("the input is already stopped (selector FF)");
    if ((p.state.input[7] >> 24) != 0xFF && (p.state.input[7] & 0xFFFF) != 0xFFFF) return fail("the input carries a spatial reference (only the simple route is supported)");
    ok("simple input (no spatial reference)");
    if (!storage(s.ref.blobBody + d.binding - 4, 0x60)) return fail("binding is unreadable");
    const Bytes words = mem.bytes(s.ref.blobBody + d.binding - 4, 24 * 4);
    auto word = [&](uint32_t i) { return words.u32(i * 4); };
    if (word(23)) return fail("the binding has authored channels (count " + std::to_string(word(23)) + ")");
    ok("no authored channels");
    const uint32_t count = word(20), relative = word(21);
    if (count && !(count & 0x80000000u)) {
        if (count > 4096) return fail("dependency count is implausible");
        const int64_t offset = int64_t(d.binding) + 0x50 + 16 + int32_t(relative);
        if (offset < 0 || offset % 4 || !storage(s.ref.blobBody + uint32_t(offset), count * 8)) return fail("dependency rows are outside the blob");
        const Bytes rows = mem.bytes(s.ref.blobBody + uint32_t(offset), count * 8);
        TagReader tags(mem);
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t key = rows.u32(i * 8), marker = rows.u32(i * 8 + 4);
            if ((marker >> 24) == 0xFF || (marker & 0xFFFF) == 0xFFFF) continue;
            if ((marker >> 24) != 5) return fail("dependency " + std::to_string(i) + " has kind " + std::to_string(marker >> 24) + " (only sequence dependencies are supported)");
            std::array<uint8_t, 8> b{};
            for (int k = 0; k < 4; ++k) {
                b[size_t(k)] = uint8_t(key >> (24 - 8 * k));
                b[size_t(4 + k)] = uint8_t(marker >> (24 - 8 * k));
            }
            ScriptReference ref;
            if (!resolveReference(mem, b, nativeReferenceBank(marker >> 24), ref, why) || !ref.present) return fail("dependency " + std::to_string(i) + ": " + why);
            if (ref.target[2] != cls::SEQUENCE || ref.target[1]) return fail("dependency " + std::to_string(i) + " does not name a standalone sequence");
            const auto m = tags.map(ref.target[0], 0, &why);
            if (!m || mem.bytes(m->body + 0x130, 1).u8(0) > 2) return fail("dependency " + std::to_string(i) + " target is not a live sequence");
        }
        ok(std::to_string(count) + " dependency rows resolve to live sequences");
    } else {
        ok("no dependencies");
    }
    const auto owner = readClockOwner(mem, d.slotHash, why);
    if (!owner) return fail("clock owner: " + why);
    for (const auto& l : owner->lines) ok(l);
    if (!sequenceDelay(word(22) >> 24, word(19), owner->rateBits, p.delay)) return fail("the authored timing is outside the native conversion range");
    ok("timing mode " + std::to_string(word(22) >> 24) + ", delay " + std::to_string(p.delay) + " ticks");
    p.lines.push_back("not checked: effect definition, placement class and placement plan validators (port map: still to port)");
    p.lines.push_back("not checked: the effect's back-reference to this sequence (Xbox SequenceReciprocalEffect)");
    p.ok = true;
     
     
    p.complete = false;
    return p;
}

RestartOutcome restartSequence(Runtime& rt, Reader& mem, const ScriptInstance& s, const std::function<void(const std::string&)>& log) {
    RestartOutcome out;
    auto say = [&](const std::string& t) {
        out.lines.push_back(t);
        if (log) log(t);
    };
    const RestartPreflight pre = preflightRestart(mem, s);
    for (const auto& l : pre.lines) say(l);
    if (!pre.ok) {
        out.message = "Preflight failed: nothing changed";
        say(out.message);
        return out;
    }
    if (!pre.complete) {
        out.message = std::string("Blocked: ") + kRestartBlocked + " Nothing changed.";
        say(out.message);
        return out;
    }
    const ScriptRef& r = s.ref;
    const uint32_t root = r.address;
    std::array<uint32_t, 8> stop = pre.state.input;
    stop[1] = (stop[1] & 0x00FFFFFFu) | 0xFF000000u;
    uint32_t block = 0;
    {
        const auto b = refBlock(0, stop);    
        block = rt.alloc(uint32_t(b.size()));
        const auto real = refBlock(block, stop);
        rt.write(block, real.data(), real.size(), true);
    }
    bool released = false;
    auto release = [&] {
        if (released) return;
        released = true;
        try {
            rt.free(block);
        } catch (const std::exception&) {
            rt.freeLater(block);
        }
    };
    try {
         
        guard::Request q1(addr::SEQUENCE_SET_INPUT);
        q1.arg(root).arg(block);
        q1.sobject(pre.tracked->target);
        identityConds(q1, r);
        q1.require(Cond::abs(root + 0x130, Cmp::Eq, 2, Width::U8, 0xFF, "state created"));
        q1.require(eq32(root + 0x128, pre.state.pairIdentity, "tracked identity"));
        q1.require(eq32(root + 0x12C, pre.state.pairAccessor, "tracked accessor"));
        q1.require(eq32(pre.tracked->entry, pre.tracked->word, "effect accessor allocation"));
        q1.require(eq32(pre.tracked->body + 0x14, pre.tracked->accessor, "effect holds its accessor"));
        inputConds(q1, root, pre.state.input);
        const auto r1 = guard::call(rt, q1, 3000, false);
        if (!r1.called) {
            release();
            out.message = "Refused on the game thread: " + r1.reason + " (nothing changed)";
            say(out.message);
            return out;
        }
        out.step = 1;
        auto stopped = [&] {
            const Bytes a = mem.bytes(root + 0x108, 0x2C);
            for (uint32_t i = 0; i < 8; ++i)
                if (a.u32(i * 4) != stop[i]) return false;
            return a.u8(0x28) == 0 && a.u32(0x20) == 0xFFFFFFFFu && a.u32(0x24) == 0xFFFFFFFFu;
        };
        if (!stopped()) {
            release();
            out.message = "Partial: the stop input was set but the cleared state was not verified (do not retry)";
            say(out.message);
            return out;
        }
        say("step 1: stopped (state cleared, tracked effect deleted)");
         
        guard::Request q2(addr::SEQUENCE_TICKS);
        q2.arg(root);
        identityConds(q2, r);
        q2.require(Cond::abs(root + 0x130, Cmp::Eq, 0, Width::U8, 0xFF, "state cleared"));
        inputConds(q2, root, stop);
        const auto r2 = guard::call(rt, q2, 3000, true);
        if (!r2.called) {
            release();
            out.message = "Partial: stopped, but the clock sample was refused (" + r2.reason + "); the sequence stays stopped";
            say(out.message);
            return out;
        }
        out.step = 2;
        const uint32_t ticks = uint32_t(r2.r3);
        say("step 2: clock ticks " + std::to_string(ticks));
        if (!stopped()) {
            release();
            out.message = "Partial: the sequence changed after the clock sample (not started)";
            say(out.message);
            return out;
        }
         
        std::array<uint32_t, 8> start = pre.state.input;
        start[0] = ticks;
        const auto real = refBlock(block, start);
        rt.write(block, real.data(), real.size(), true);
        guard::Request q3(addr::SEQUENCE_SET_INPUT);
        q3.arg(root).arg(block);
        identityConds(q3, r);
        q3.require(Cond::abs(root + 0x130, Cmp::Eq, 0, Width::U8, 0xFF, "state cleared"));
        q3.require(eq32(root + 0x128, 0xFFFFFFFFu, "pair empty"));
        q3.require(eq32(root + 0x12C, 0xFFFFFFFFu, "pair empty"));
        inputConds(q3, root, stop);
        const auto r3 = guard::call(rt, q3, 3000, false);
        release();
        if (!r3.called) {
            out.message = "Partial: stopped, but the start was refused (" + r3.reason + "); the sequence stays stopped";
            say(out.message);
            return out;
        }
        out.step = 3;
         
        const Bytes a = mem.bytes(root + 0x108, 0x2C);
        const uint8_t state = a.u8(0x28);
        bool inputOk = true;
        for (uint32_t i = 0; i < 8; ++i) inputOk = inputOk && a.u32(i * 4) == start[i];
        const bool emptyPair = a.u32(0x20) == 0xFFFFFFFFu && a.u32(0x24) == 0xFFFFFFFFu;
        if (!inputOk) {
            out.message = "Partial: started, but the input differs from the start input";
        } else if (state == 0 && emptyPair) {
            out.message = "Restarted: the sequence returned to cleared";
            out.ok = true;
        } else if (state == 1 && emptyPair) {
            out.message = "Restarted: the sequence is deferred and may create its effect later";
            out.ok = true;
        } else if (state == 2) {
            std::string why;
            const auto created = readAccessor(mem, a.u32(0x24), &why);
            out.ok = created && created->identity == a.u32(0x20);
            out.message = out.ok ? "Restarted: a new effect was created (accessor " + hex8(a.u32(0x24)) + ")" : "Partial: created, but the new effect does not verify";
        } else {
            out.message = "Partial: unexpected state " + std::to_string(state);
        }
        say(out.message);
        return out;
    } catch (...) {
        if (!released) {
            released = true;
            rt.freeLater(block);    
        }
        throw;
    }
}

}   
