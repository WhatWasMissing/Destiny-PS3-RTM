#include "game/guard_call.h"

#include <cstdio>
#include <cstring>

#include "core/error.h"
#include "core/ppc.h"
#include "core/profile.h"
#include "core/runtime.h"
#include "game/world_addr.h"
#include "game/world_mem.h"

namespace brps3::guard {

namespace {

using ppc::Asm;

constexpr int64_t FRAME = 0x100;
 
const std::vector<int> SAVE = {2, 31};

void put32(std::vector<uint8_t>& b, uint32_t off, uint32_t v) {
    b[off] = uint8_t(v >> 24);
    b[off + 1] = uint8_t(v >> 16);
    b[off + 2] = uint8_t(v >> 8);
    b[off + 3] = uint8_t(v);
}

void put16(std::vector<uint8_t>& b, uint32_t off, uint32_t v) {
    b[off] = uint8_t(v >> 8);
    b[off + 1] = uint8_t(v);
}

void put64(std::vector<uint8_t>& b, uint32_t off, uint64_t v) {
    put32(b, off, uint32_t(v >> 32));
    put32(b, off + 4, uint32_t(v));
}

uint32_t get32(const std::vector<uint8_t>& b, uint32_t off) {
    return uint32_t(b[off]) << 24 | uint32_t(b[off + 1]) << 16 | uint32_t(b[off + 2]) << 8 | b[off + 3];
}

uint64_t get64(const std::vector<uint8_t>& b, uint32_t off) { return uint64_t(get32(b, off)) << 32 | get32(b, off + 4); }

std::string hx(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%08X", v);
    return buf;
}

std::string hxShort(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%X", v);
    return buf;
}

uint32_t align16(uint32_t v) { return (v + 15u) & ~15u; }

const char* widthText(Width w) {
    switch (w) {
    case Width::U8: return "u8";
    case Width::U16: return "u16";
    case Width::U32: return "u32";
    }
    return "?";
}

}   

 

const char* cmpText(Cmp c) {
    switch (c) {
    case Cmp::Eq: return "==";
    case Cmp::Ne: return "!=";
    case Cmp::LtU: return "<";
    case Cmp::GeU: return ">=";
    case Cmp::LeU: return "<=";
    case Cmp::GtU: return ">";
    }
    return "?";
}

uint32_t acceptBits(Cmp c) {
    constexpr uint32_t LT = 8, GT = 4, EQ = 2;    
    switch (c) {
    case Cmp::Eq: return EQ;
    case Cmp::Ne: return LT | GT;
    case Cmp::LtU: return LT;
    case Cmp::GeU: return GT | EQ;
    case Cmp::LeU: return LT | EQ;
    case Cmp::GtU: return GT;
    }
    return 0;
}

const char* statusName(Status s) {
    switch (s) {
    case Status::Pending: return "pending";
    case Status::Called: return "called";
    case Status::Refused: return "refused";
    case Status::BadBlock: return "bad block";
    case Status::Disarmed: return "disarmed";
    }
    return "?";
}

Cond Cond::abs(uint32_t address, Cmp c, uint32_t v, Width w, uint32_t m, std::string what) {
    Cond k;
    k.addr = address;
    k.cmp = c;
    k.value = v;
    k.width = w;
    k.mask = m;
    k.what = std::move(what);
    return k;
}

Cond Cond::entry(uint32_t offset, Cmp c, uint32_t v, Width w, uint32_t m, std::string what) {
    Cond k = abs(offset, c, v, w, m, std::move(what));
    k.entryBased = true;
    return k;
}

Cond Cond::ptr(uint32_t slot, std::vector<uint32_t> offsets, Cmp c, uint32_t v, std::string what, Width w, uint32_t m) {
    Cond k = abs(slot, c, v, w, m, std::move(what));
    k.derefs = std::move(offsets);
    return k;
}

std::string Cond::describe() const {
    if (!what.empty()) return what;
    std::string p = entryBased ? "entry+" + hxShort(addr) : hx(addr);
    for (uint32_t off : derefs) p = "[" + p + "]+" + hxShort(off);
    std::string s = std::string(widthText(width)) + " [" + p + "]";
    const uint32_t full = width == Width::U8 ? 0xFFu : width == Width::U16 ? 0xFFFFu : 0xFFFFFFFFu;
    if ((mask & full) != full) s += " & " + hxShort(mask);
    return s + " " + cmpText(cmp) + " " + hxShort(value);
}

Request& Request::sobject(uint32_t h) {
    check(HandleCheck{world::addr::SOBJECT_POOL, h, world::sobj::HANDLE, 0x1FFF});
    return require(Cond::entry(world::sobj::FLAGS, Cmp::Eq, 0, Width::U32, world::sobj::FLAG_DELETING, "sObject not being deleted"));
}

uint32_t Result::scratchU32(uint32_t off) const {
    if (uint64_t(off) + 4 > scratch.size())
        throw Error("guard scratch +" + hxShort(off) + " is beyond the " + std::to_string(scratch.size()) + " bytes read back");
    return get32(scratch, off);
}

 

std::vector<uint8_t> stubProgram(uint32_t base) {
     
     
     
     
     
    Asm a(base);
    auto chk = [&](int alignMask, const char* fail) { a.li(11, alignMask).bl("chk").beq(fail); };

    ppc::prologue(a, FRAME, SAVE);
    a.mr(12, 3);
    chk(15, "ret_bad");                                          
    a.mr(31, 12);
    a.lwz(0, 0, 31).load32(12, kMagic).cmplw(0, 12).bne("ret_bad");
    a.lwz(9, kOffFlags, 31).andi_(0, 9, kFlagArmed).beq("ret_disarmed");
    a.li(0, 0).stw(0, kOffEntry, 31);
    a.andi_(0, 9, kFlagHandle).beq("conds");

     
    a.lwz(12, kOffPool, 31);
    chk(3, "fail_pool");
    a.mr(8, 12);
    a.lwz(10, 0x10, 8).lwz(9, 0xC, 8).lwz(12, 8, 8);            
    a.cmplwi(10, 0).beq("fail_pool").cmplwi(9, 0).beq("fail_pool");
    chk(3, "fail_pool");
    a.lwz(5, kOffHandle, 31).lhz(4, kOffMask, 31).and_(4, 5, 4);
    a.cmplw(4, 10).bge("fail_index");
    a.mullw(4, 4, 9).add(12, 12, 4);
    chk(3, "fail_entry");
    a.stw(12, kOffEntry, 31).mr(6, 12);
    a.lhz(4, kOffIdentity, 31).add(12, 6, 4);
    chk(3, "fail_entry");
    a.lwz(4, 0, 12).cmplw(4, 5).bne("fail_identity");

     
    a.label("conds");
    a.lwz(8, kOffCondCount, 31).cmplwi(8, int64_t(kMaxConds)).bgt("ret_bad");
    a.li(7, 0).addi(5, 31, kCondOffset);
    a.label("loop").cmplw(7, 8).bge("call");
    a.lwz(9, 0, 5).lwz(12, 4, 5);                                
    a.srwi(0, 9, 24).cmpwi(0, 0).beq("absolute");
    a.lwz(11, kOffEntry, 31).add(12, 12, 11);
    a.label("absolute");
    a.andi_(6, 9, 0xFF).cmplwi(6, int64_t(kMaxDerefs)).bgt("ret_bad");
    a.addi(4, 5, 8);                                             
    a.label("deref").cmpwi(6, 0).beq("load");
    chk(3, "fail_cond");
    a.lwz(12, 0, 12).lwz(11, 0, 4).add(12, 12, 11);
    a.addi(4, 4, 4).addi(6, 6, -1).b("deref");
    a.label("load").rlwinm(6, 9, 16, 24, 31);                    
    a.addi(11, 6, -1).bl("chk").beq("fail_cond");                
    a.cmpwi(6, 1).bne("w2").lbz(10, 0, 12).b("compare");
    a.label("w2").cmpwi(6, 2).bne("w4").lhz(10, 0, 12).b("compare");
    a.label("w4").cmpwi(6, 4).bne("ret_bad").lwz(10, 0, 12);
     
    a.label("compare").lwz(11, 0x14, 5).and_(10, 10, 11).lwz(11, 0x18, 5);
    a.cmplw(10, 11).mfcr(0).rlwinm(0, 0, 4, 28, 31);
    a.rlwinm(11, 9, 24, 28, 31).andDot(0, 0, 11).beq("fail_cond");
    a.addi(7, 7, 1).addi(5, 5, kCondSize).b("loop");

     
    a.label("call");
    for (int i = 0; i < 8; ++i) a.ld(3 + i, kOffArgs + 8 * i, 31);
    for (int i = 0; i < 4; ++i) a.lfd(1 + i, kOffFloats + 8 * i, 31);
    a.lwz(12, kOffNative, 31).lwz(2, kOffToc, 31).mtctr(12).bctrl();
    a.std_(3, kOffR3, 31).stfd(1, kOffF1, 31);
    a.lwz(12, kOffCalls, 31).addi(12, 12, 1).stw(12, kOffCalls, 31);
    a.lwz(12, kOffFlags, 31).andi_(0, 12, kFlagOnce).beq("called");
    a.andi_(12, 12, 0xFFFF & ~kFlagArmed).stw(12, kOffFlags, 31);    
    a.label("called").li(4, -1).li(3, int64_t(Status::Called)).b("store");

    a.label("fail_pool").li(4, kFailPool).b("refuse");
    a.label("fail_index").li(4, kFailIndex).b("refuse");
    a.label("fail_entry").li(4, kFailEntry).b("refuse");
    a.label("fail_identity").li(4, kFailIdentity).b("refuse");
    a.label("fail_cond").mr(4, 7);
    a.label("refuse").li(3, int64_t(Status::Refused));
    a.label("store").stw(4, kOffFailed, 31).lwsync().stw(3, kOffStatus, 31);
    a.rlwinm(4, 4, 8, 8, 23).or_(3, 3, 4).b("out");              
    a.label("ret_bad").li(3, int64_t(Status::BadBlock)).b("out");     
    a.label("ret_disarmed").li(3, int64_t(Status::Disarmed));         
    a.label("out");
    ppc::epilogue(a, FRAME, SAVE);
    a.blr();

     
     
     
    a.label("chk").clrldi(12, 12, 32).andDot(0, 12, 11).bne("chk_bad");
    a.addis(0, 12, -0x3000).lis(11, 0x5000).cmplw(0, 11).bltlr();
    a.addis(0, 12, -0x1).lis(11, 0x1C7).cmplw(0, 11).bltlr();
    a.label("chk_bad").cmpw(0, 0).blr();
    auto code = a.link();
    if (code.size() > kStubSize) throw Error("guard stub is " + std::to_string(code.size()) + " bytes, larger than kStubSize");
    return code;
}

uint32_t stubAllocSize() {
    static const uint32_t size = align16(uint32_t(stubProgram(profile::CAVE).size()));
    return size;
}

uint32_t installStub(Runtime& rt) {
    return rt.sharedCode(kStubName, [](uint32_t base) { return stubProgram(base); }, stubAllocSize());
}

 

uint32_t scratchOffset(const Request& r) { return kCondOffset + kCondSize * uint32_t(r.conds.size()); }

uint32_t blockSize(const Request& r) { return align16(scratchOffset(r) + uint32_t(r.scratch.size())); }

void validate(const Request& r) {
    if (!(r.native >= 0x10000 && r.native < profile::TEXT_END && r.native % 4 == 0))
        throw Error("guarded call: " + hx(r.native) + " is not a code address");
    if (r.args.size() > 8) throw Error("guarded call: more than 8 integer arguments");
    if (r.floats.size() > 4) throw Error("guarded call: more than 4 float arguments");
    if (r.conds.size() > kMaxConds) throw Error("guarded call: more than " + std::to_string(kMaxConds) + " conditions");
    if (r.scratch.size() > kMaxScratch) throw Error("guarded call: scratch larger than " + std::to_string(kMaxScratch) + " bytes");
    for (int i : r.scratchArgs) {
        if (i < 0 || size_t(i) >= r.args.size()) throw Error("guarded call: scratch argument index out of range");
        if (r.args[size_t(i)] >= r.scratch.size())
            throw Error("guarded call: scratch argument r" + std::to_string(3 + i) + " points beyond the " + std::to_string(r.scratch.size()) +
                        "-byte scratch area");
    }
    if (r.handle) {
        const HandleCheck& h = *r.handle;
        if (!world::ebootData(h.pool, 0x14) || h.pool % 4) throw Error("guarded call: pool " + hx(h.pool) + " is not an EBOOT data address");
        if (h.identityOffset > 0xFFFC || h.identityOffset % 4) throw Error("guarded call: bad identity offset " + hxShort(h.identityOffset));
        if (h.indexMask == 0 || h.indexMask > 0xFFFF) throw Error("guarded call: bad index mask " + hxShort(h.indexMask));
        if (!world::validHandle(h.handle)) throw Error("guarded call: handle " + hx(h.handle) + " is not a handle");
    }
    for (size_t i = 0; i < r.conds.size(); ++i) {
        const Cond& c = r.conds[i];
        const std::string where = "guarded call: condition " + std::to_string(i) + " (" + c.describe() + ")";
        if (c.derefs.size() > kMaxDerefs) throw Error(where + ": more than 3 dereferences");
        if (c.width != Width::U8 && c.width != Width::U16 && c.width != Width::U32) throw Error(where + ": bad width");
        if (uint8_t(c.cmp) > uint8_t(Cmp::GtU)) throw Error(where + ": bad comparison");
        if (c.entryBased && !r.handle) throw Error(where + ": entry-relative without a handle check");
        if (!c.entryBased) {
             
            const uint32_t align = c.derefs.empty() ? uint32_t(c.width) : 4u;
            if (!world::mapped(c.addr, align) || c.addr % align) throw Error(where + ": " + hx(c.addr) + " is not a plausible address");
        }
    }
}

std::vector<uint8_t> encodeBlock(const Request& r, uint32_t blockAddr, bool armed, bool once) {
    validate(r);
    std::vector<uint8_t> b(blockSize(r), 0);
    const uint32_t scratch = blockAddr + scratchOffset(r);
    put32(b, 0, kMagic);
    put32(b, kOffFlags, (r.handle ? kFlagHandle : 0) | (armed ? kFlagArmed : 0) | (once ? kFlagOnce : 0));
    put32(b, kOffStatus, uint32_t(Status::Pending));
    put32(b, kOffFailed, kNoFailure);
    put32(b, kOffNative, r.native);
    put32(b, kOffToc, profile::TOC);
    put32(b, kOffCondCount, uint32_t(r.conds.size()));
    if (r.handle) {
        put32(b, kOffPool, r.handle->pool);
        put32(b, kOffHandle, r.handle->handle);
        put16(b, kOffIdentity, r.handle->identityOffset);
        put16(b, kOffMask, r.handle->indexMask);
    }
    for (size_t i = 0; i < r.args.size(); ++i) {
        uint64_t v = r.args[i];
        for (int s : r.scratchArgs)
            if (size_t(s) == i) v += scratch;
        put64(b, kOffArgs + 8 * uint32_t(i), v);
    }
    for (size_t i = 0; i < r.floats.size(); ++i) {
        uint64_t bits;
        std::memcpy(&bits, &r.floats[i], 8);
        put64(b, kOffFloats + 8 * uint32_t(i), bits);
    }
    for (size_t i = 0; i < r.conds.size(); ++i) {
        const Cond& c = r.conds[i];
        const uint32_t o = kCondOffset + kCondSize * uint32_t(i);
        put32(b, o, uint32_t(c.entryBased ? 1 : 0) << 24 | uint32_t(c.width) << 16 | acceptBits(c.cmp) << 8 | uint32_t(c.derefs.size()));
        put32(b, o + 4, c.addr);
        for (size_t k = 0; k < c.derefs.size(); ++k) put32(b, o + 8 + 4 * uint32_t(k), c.derefs[k]);
        put32(b, o + 0x14, c.mask);
        put32(b, o + 0x18, c.value);
    }
    std::memcpy(b.data() + scratchOffset(r), r.scratch.data(), r.scratch.size());
    return b;
}

std::string describeFailure(const Request& r, uint32_t failed) {
    const std::string h = r.handle ? "handle " + hx(r.handle->handle) : std::string("handle");
    switch (failed) {
    case kNoFailure: return {};
    case kFailPool: return "the pool at " + (r.handle ? hx(r.handle->pool) : std::string("?")) + " is not set up (no game session?)";
    case kFailIndex: return h + ": index beyond the pool capacity";
    case kFailEntry: return h + ": entry address is not plausible";
    case kFailIdentity: return h + " is stale (salt mismatch: the object is gone or its slot was reused)";
    default: break;
    }
    if (failed < r.conds.size()) return "condition " + std::to_string(failed) + " failed: " + r.conds[failed].describe();
    return "refused (code " + std::to_string(failed) + ")";
}

namespace {
void finish(Result& out, const Request& r, uint32_t status, uint32_t failed) {
    out.status = status <= uint32_t(Status::Disarmed) ? Status(status) : Status::BadBlock;
    out.called = out.status == Status::Called;
    out.failed = out.called ? kNoFailure : failed;
    switch (out.status) {
    case Status::Called: out.reason.clear(); break;
    case Status::Refused: out.reason = describeFailure(r, failed); break;
    case Status::BadBlock:
        out.reason = status <= uint32_t(Status::Disarmed) ? "the guard stub rejected the parameter block" : "unexpected guard stub result " + std::to_string(status);
        break;
    case Status::Disarmed: out.reason = "disarmed"; break;
    case Status::Pending: out.reason = "not evaluated yet"; break;
    }
}
}   

Result decodeR3(uint64_t r3, const Request& r) {
    Result out;
    const uint32_t f = uint32_t(r3 >> 8) & 0xFFFF;
    finish(out, r, uint32_t(r3 & 0xFF), f == 0xFFFF ? kNoFailure : f);
    return out;
}

void decodeBlock(const std::vector<uint8_t>& block, const Request& r, Result& out) {
    if (block.size() < blockSize(r)) throw Error("guard block read back short");
    finish(out, r, get32(block, kOffStatus), get32(block, kOffFailed));
    out.entry = get32(block, kOffEntry);
    out.r3 = get64(block, kOffR3);
    const uint64_t bits = get64(block, kOffF1);
    std::memcpy(&out.f1, &bits, 8);
    const uint32_t s = scratchOffset(r);
    out.scratch.assign(block.begin() + s, block.begin() + s + std::ptrdiff_t(r.scratch.size()));
}

 

std::vector<Result> callMany(Runtime& rt, const std::vector<Request>& rs, int timeoutMs, bool readBack) {
    if (rs.empty() || rs.size() > 16) throw Error("1..16 guarded calls per frame");
    for (const Request& r : rs) validate(r);
    const uint32_t stub = installStub(rt);
    std::vector<uint32_t> offsets;
    uint32_t total = 0;
    for (const Request& r : rs) {
        offsets.push_back(total);
        total += blockSize(r);
    }
    const uint32_t block = rt.alloc(total);
    std::vector<Result> out;
    try {
        std::vector<uint8_t> all(total, 0);
        for (size_t i = 0; i < rs.size(); ++i) {
            const auto b = encodeBlock(rs[i], block + offsets[i]);
            std::memcpy(all.data() + offsets[i], b.data(), b.size());
        }
        rt.write(block, all.data(), all.size(), true);
        std::vector<NativeCall> calls;
        for (size_t i = 0; i < rs.size(); ++i) calls.push_back(NativeCall{stub, {block + offsets[i]}, {}, profile::TOC});
        const auto results = rt.callMany(calls, timeoutMs);
        for (size_t i = 0; i < rs.size(); ++i) out.push_back(decodeR3(results[i].r3, rs[i]));
        if (readBack) {
            const auto back = rt.read(block, total);
            for (size_t i = 0; i < rs.size(); ++i) {
                const uint32_t n = blockSize(rs[i]);
                decodeBlock(std::vector<uint8_t>(back.begin() + offsets[i], back.begin() + offsets[i] + n), rs[i], out[i]);
            }
        }
    } catch (...) {
        rt.freeLater(block);                                    
        throw;
    }
    try {
        rt.free(block);
    } catch (const std::exception&) {
        rt.freeLater(block);
    }
    return out;
}

Result call(Runtime& rt, const Request& r, int timeoutMs, bool readBack) { return callMany(rt, {r}, timeoutMs, readBack).front(); }

 

void GuardTask::start(Runtime& rt, const std::string& name, const Request& r, bool once) {
    if (name.empty()) throw Error("a guard task needs a name");
    if (rt.hasTask(name)) throw Error("task " + name + " is already running");
    validate(r);
    if (block_) stop(rt);
    const uint32_t stub = installStub(rt);
    const uint32_t size = blockSize(r);
    const uint32_t block = rt.alloc(size);
    try {
        const auto b = encodeBlock(r, block, true, once);
        rt.write(block, b.data(), b.size(), true);
        rt.addTaskAt(name, stub, block);
    } catch (...) {
        rt.freeLater(block);
        throw;
    }
    name_ = name;
    block_ = block;
    size_ = size;
    once_ = once;
    req_ = r;
}

void GuardTask::update(Runtime& rt, const Request& r) {
    if (!block_ || !rt.hasTask(name_)) throw Error("guard task " + (name_.empty() ? std::string("?") : name_) + " is not running");
    validate(r);
    if (blockSize(r) > size_) {
        const std::string name = name_;
        const bool once = once_;
        stop(rt);
        start(rt, name, r, once);
        return;
    }
     
     
    auto b = encodeBlock(r, block_, true, once_);
    std::vector<uint8_t> flags(b.begin() + kOffFlags, b.begin() + kOffFlags + 4);
    uint8_t off[4] = {0, 0, 0, uint8_t((r.handle ? kFlagHandle : 0) | (once_ ? kFlagOnce : 0))};
    rt.write(block_ + kOffFlags, off, 4, true);
    rt.waitFrames(2, 1000);
    rt.write(block_ + kOffStatus, b.data() + kOffStatus, b.size() - kOffStatus, true);
    rt.write(block_ + kOffFlags, flags.data(), 4, true);
    req_ = r;
}

void GuardTask::stop(Runtime& rt) {
    if (!name_.empty()) {
        try {
            rt.removeTask(name_);
        } catch (const std::exception&) {
            if (rt.hasTask(name_)) throw;                       
        }
    }
    if (block_) rt.freeLater(block_);
    block_ = size_ = 0;
}

bool GuardTask::running(Runtime& rt) const { return !name_.empty() && block_ && rt.hasTask(name_); }

GuardTask::Stats GuardTask::stats(Runtime& rt) const {
    Stats s;
    if (!block_) return s;
    const auto b = rt.read(block_, kOffR3);
    if (get32(b, 0) != kMagic) throw Error("guard task block " + hx(block_) + " was overwritten");
    const uint32_t st = get32(b, kOffStatus);
    s.status = st <= uint32_t(Status::Disarmed) ? Status(st) : Status::BadBlock;
    s.failed = get32(b, kOffFailed);
    s.calls = get32(b, kOffCalls);
    s.entry = get32(b, kOffEntry);
    s.armed = (get32(b, kOffFlags) & kFlagArmed) != 0;
    if (s.status == Status::Refused) s.reason = describeFailure(req_, s.failed);
    return s;
}

}   
