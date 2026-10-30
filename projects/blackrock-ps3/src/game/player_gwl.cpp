#include "game/player_gwl.h"

#include <algorithm>
#include <cstring>

#include "core/error.h"
#include "core/ppc.h"
#include "core/runtime.h"
#include "game/world_mem.h"

namespace brps3::player::gwl {

namespace {

using ppc::Asm;

void put32(std::vector<uint8_t>& b, uint32_t off, uint32_t v) {
    b[off] = uint8_t(v >> 24);
    b[off + 1] = uint8_t(v >> 16);
    b[off + 2] = uint8_t(v >> 8);
    b[off + 3] = uint8_t(v);
}
uint32_t get32(const std::vector<uint8_t>& b, uint32_t off) {
    return uint32_t(b[off]) << 24 | uint32_t(b[off + 1]) << 16 | uint32_t(b[off + 2]) << 8 | b[off + 3];
}
uint32_t align16(uint32_t v) { return (v + 15) & ~15u; }

uint32_t groupSize(const Group& g) {
    return kGroupHeaderSize + kGuardSize * uint32_t(g.guards.size()) + kWriteSize * uint32_t(g.writes.size());
}

}   

 

std::vector<uint8_t> program(uint32_t base) {
     
     
    Asm a(base);
    auto chk = [&](const char* fail) { a.li(11, 3).bl("chk").beq(fail); };
     
    auto guard = [&](int maskOff, int valueOff, const char* fail) {
        chk(fail);
        a.lwz(0, 0, 12).lwz(11, maskOff, 6).and_(0, 0, 11).lwz(11, valueOff, 6).cmplw(0, 11).bne(fail);
    };

    a.mflr(4);
    a.mr(12, 3).li(11, 15).bl("chk").beq("out");                
    a.mr(3, 12);
    a.lwz(0, 0, 3).load32(11, kMagic).cmplw(0, 11).bne("out");
    a.lwz(10, kOffCount, 3).cmplwi(10, 0).beq("out").cmplwi(10, kMaxGroups).bgt("out");
    a.lwz(11, kOffFrames, 3).addi(11, 11, 1).stw(11, kOffFrames, 3);
    a.addi(9, 3, kHeaderSize);

     
    a.label("group");
    a.addi(12, 9, kGroupHeaderSize).lwz(0, kOffEnd, 3).cmplw(12, 0).bgt("out");
    a.lwz(8, 0, 9).lwz(7, 4, 9);
    a.cmplwi(8, kMaxGuards).bgt("out").cmplwi(7, kMaxWrites).bgt("out");
    a.mulli(0, 8, kGuardSize).add(12, 9, 0).mulli(0, 7, kWriteSize).add(12, 12, 0).addi(12, 12, kGroupHeaderSize);
    a.lwz(0, kOffEnd, 3).cmplw(12, 0).bgt("out");
    a.addi(6, 9, kGroupHeaderSize).li(5, 0);

     
    a.label("gloop").cmplw(5, 8).bge("gpass");
    a.lwz(12, 0, 6);
    guard(4, 8, "gfail");
    a.addi(6, 6, kGuardSize).addi(5, 5, 1).b("gloop");
    a.label("gfail").addi(5, 5, 1).stw(5, 0xC, 9).b("gnext");   
    a.label("gpass").li(11, 0).stw(11, 0xC, 9);
    a.lwz(11, 8, 9).addi(11, 11, 1).stw(11, 8, 9);               

     
    a.li(5, 0);
    a.label("wloop").cmplw(5, 7).bge("gnext");
    a.lwz(12, 0xC, 6).cmplwi(12, 0).beq("wg2");
    guard(0x10, 0x14, "wskip");
    a.label("wg2").lwz(12, 0x18, 6).cmplwi(12, 0).beq("wdo");
    guard(0x1C, 0x20, "wskip");
    a.label("wdo").lwz(12, 0, 6);
    chk("wskip");
    a.addis(0, 12, -0x3000).lis(11, 0x5000).cmplw(0, 11).bge("wskip");    
    a.lwz(11, 4, 6).stw(11, 0, 12);
    a.lwz(11, 8, 6).addi(11, 11, 1).stw(11, 8, 6);               
    a.label("wskip").addi(6, 6, kWriteSize).addi(5, 5, 1).b("wloop");

     
    a.label("gnext");
    a.mulli(0, 8, kGuardSize).add(9, 9, 0).mulli(0, 7, kWriteSize).add(9, 9, 0).addi(9, 9, kGroupHeaderSize);
    a.addi(10, 10, -1).cmplwi(10, 0).bne("group");
    a.label("out").mtlr(4).blr();

     
     
     
    a.label("chk").clrldi(12, 12, 32).andDot(0, 12, 11).bne("chk_bad");
    a.addis(0, 12, -0x3000).lis(11, 0x5000).cmplw(0, 11).bltlr();
    a.addis(0, 12, -0x1).lis(11, 0x1C7).cmplw(0, 11).bltlr();
    a.label("chk_bad").cmpw(0, 0).blr();
    return a.link();
}

uint32_t programSize() {
    static const uint32_t size = align16(uint32_t(program(0x183E000).size()));
    return size;
}

 

void validate(const std::vector<Group>& groups) {
    if (groups.size() > kMaxGroups) throw Error("write list: more than " + std::to_string(kMaxGroups) + " groups");
    for (const Group& g : groups) {
        const std::string where = "write list group \"" + g.name + "\"";
        if (g.guards.size() > kMaxGuards) throw Error(where + ": more than " + std::to_string(kMaxGuards) + " guards");
        if (g.writes.size() > kMaxWrites) throw Error(where + ": more than " + std::to_string(kMaxWrites) + " writes");
        auto checkGuard = [&](const Guard& x) {
            if (!world::mapped(x.addr, 4) || x.addr % 4) throw Error(where + ": guard address " + world::hex8(x.addr) + " is not plausible");
        };
        for (const Guard& x : g.guards) checkGuard(x);
        for (const Write& w : g.writes) {
            if (!world::heap(w.addr, 4) || w.addr % 4) throw Error(where + ": write target " + world::hex8(w.addr) + " is not a heap address");
            if (w.guards.size() > kMaxWriteGuards) throw Error(where + ": a write has more than 2 guards");
            for (const Guard& x : w.guards) checkGuard(x);
        }
    }
}

uint32_t tableSize(const std::vector<Group>& groups) {
    uint32_t n = kHeaderSize;
    for (const Group& g : groups) n += groupSize(g);
    return align16(n);
}

std::vector<uint8_t> encodeTable(const std::vector<Group>& groups, uint32_t tableAddr, bool armed) {
    validate(groups);
    const uint32_t size = tableSize(groups);
    std::vector<uint8_t> b(size, 0);
    put32(b, 0, kMagic);
    put32(b, kOffCount, armed ? uint32_t(groups.size()) : 0);
    put32(b, kOffEnd, tableAddr + size);
    uint32_t o = kHeaderSize;
    for (const Group& g : groups) {
        put32(b, o, uint32_t(g.guards.size()));
        put32(b, o + 4, uint32_t(g.writes.size()));
        o += kGroupHeaderSize;
        for (const Guard& x : g.guards) {
            put32(b, o, x.addr);
            put32(b, o + 4, x.mask);
            put32(b, o + 8, x.value);
            o += kGuardSize;
        }
        for (const Write& w : g.writes) {
            put32(b, o, w.addr);
            put32(b, o + 4, w.value);
            for (size_t k = 0; k < w.guards.size(); ++k) {
                const uint32_t go = o + 0xC + 0xC * uint32_t(k);
                put32(b, go, w.guards[k].addr);
                put32(b, go + 4, w.guards[k].mask);
                put32(b, go + 8, w.guards[k].value);
            }
            o += kWriteSize;
        }
    }
    return b;
}

TableStats parseStats(const std::vector<uint8_t>& t, const std::vector<Group>& layout) {
    TableStats s;
    if (t.size() < kHeaderSize || get32(t, 0) != kMagic) return s;
    s.valid = true;
    s.armedGroups = get32(t, kOffCount);
    s.frames = get32(t, kOffFrames);
    uint32_t o = kHeaderSize;
    for (const Group& g : layout) {
        if (o + groupSize(g) > t.size()) break;
        GroupStats gs;
        gs.passes = get32(t, o + 8);
        gs.failed = get32(t, o + 0xC);
        uint32_t w = o + kGroupHeaderSize + kGuardSize * uint32_t(g.guards.size());
        for (size_t i = 0; i < g.writes.size(); ++i, w += kWriteSize) gs.hits.push_back(get32(t, w + 8));
        s.groups.push_back(std::move(gs));
        o += groupSize(g);
    }
    return s;
}

 

void WriteList::fresh(Runtime& rt, uint32_t code, const std::vector<Group>& groups) {
     
     
    const uint32_t need = tableSize(groups);
    const uint32_t cap = std::max<uint32_t>(0x400, (need + 0xFF) & ~0xFFu);
    const uint32_t nb = rt.alloc(cap);
    try {
        const auto bytes = encodeTable(groups, nb, true);
        rt.write(nb, bytes.data(), bytes.size(), true);
        if (rt.hasTask(task_)) rt.removeTask(task_);
        rt.addTaskAt(task_, code, nb);
    } catch (...) {
        rt.freeLater(nb);
        throw;
    }
    if (block_) rt.freeLater(block_);
    block_ = nb;
    capacity_ = cap;
    groups_ = groups;
    dirty_ = false;
}

void WriteList::upload(Runtime& rt, const std::vector<Group>& groups) {
    validate(groups);
    const bool live = block_ && rt.hasTask(task_);
    if (live && !dirty_ && groups == groups_) return;            
    if (groups.empty()) {
        if (live) {
            dirty_ = true;                                       
            rt.writeU32(block_ + kOffCount, 0);                  
        }
        groups_.clear();
        dirty_ = false;
        return;
    }
    const uint32_t code = rt.sharedCode(kProgramName, [](uint32_t base) { return program(base); }, programSize());
    if (!block_ || tableSize(groups) > capacity_) {
        fresh(rt, code, groups);
        return;
    }
    const auto bytes = encodeTable(groups, block_, true);
    dirty_ = true;                                               
    if (!live) {                                                 
        rt.write(block_, bytes.data(), bytes.size(), true);
        rt.addTaskAt(task_, code, block_);
        groups_ = groups;
        dirty_ = false;
        return;
    }
     
     
    rt.writeU32(block_ + kOffCount, 0);
    if (!rt.waitFrames(2, 2000)) {
         
         
        fresh(rt, code, groups);
        return;
    }
    rt.write(block_ + kOffEnd, bytes.data() + kOffEnd, bytes.size() - kOffEnd, true);
    rt.writeU32(block_ + kOffCount, uint32_t(groups.size()));
    groups_ = groups;
    dirty_ = false;
}

void WriteList::stop(Runtime& rt) {
    dirty_ = true;                                               
    if (rt.hasTask(task_)) {
        if (block_) rt.writeU32(block_ + kOffCount, 0);          
        rt.removeTask(task_);
    }
    if (block_) rt.freeLater(block_);
    block_ = 0;
    capacity_ = 0;
    groups_.clear();
    dirty_ = false;
}

bool WriteList::running(Runtime& rt) const { return block_ && rt.hasTask(task_); }

TableStats WriteList::stats(Runtime& rt) const {
    if (!block_ || groups_.empty()) return {};
    return parseStats(rt.read(block_, tableSize(groups_)), groups_);
}

}   
