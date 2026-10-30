#include "game/patches_tick.h"

#include <algorithm>
#include <cstring>

#include "core/error.h"
#include "core/ppc.h"
#include "game/patches_sites.h"
#include "game/world_tags.h"

namespace brps3::patches::tick {

namespace {

using ppc::Asm;
constexpr int64_t FRAME = 0x80;
const std::vector<int> SAVE = {31};

void put32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24);
    p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);
    p[3] = uint8_t(v);
}

uint32_t get32(const std::vector<uint8_t>& b, uint32_t o) {
    if (o + 4 > b.size()) return 0;
    return uint32_t(b[o]) << 24 | uint32_t(b[o + 1]) << 16 | uint32_t(b[o + 2]) << 8 | b[o + 3];
}

 
void heapCheck(Asm& a, const std::string& skip, bool aligned) {
    a.srwi(0, 9, 28).cmplwi(0, 3).blt(skip);
    a.cmplwi(0, 7).bgt(skip);
    if (aligned) a.andi_(0, 9, 3).bne(skip);
}

 
void testFlag(Asm& a, uint32_t bit) { a.lwz(0, off::FLAGS, 31).andi_(0, 0, bit); }

void bump(Asm& a, uint32_t at) { a.lwz(12, at, 31).addi(12, 12, 1).stw(12, at, 31); }

}   

std::vector<uint8_t> program(uint32_t base) {
    Asm a(base);
    ppc::prologue(a, FRAME, SAVE);
    a.mr(31, 3);
    bump(a, off::FRAMES);

     
    testFlag(a, Fov);
    a.beq("no_fov");
    a.lwz(0, off::FOV, 31).addr(9, addr::FOV_OVERRIDE).stw(0, 0, 9);
    a.label("no_fov");

     
    testFlag(a, Afk);
    a.beq("no_afk");
    a.li(0, 0).addr(9, addr::NO_INACTIVITY_FLAG).stb(0, 0, 9);
    a.label("no_afk");

     
    testFlag(a, Suppress);
    a.beq("no_sup");
    a.addr(9, addr::FLYIN_SUPPRESS_PTR).lwz(9, 0, 9);
    heapCheck(a, "no_sup", false);
    a.li(0, 1).stb(0, 0, 9);
    bump(a, off::SUPPRESS_FRAMES);
    a.label("no_sup");

     
    testFlag(a, ValidBit);
    a.beq("no_valid");
    a.addr(9, addr::ENTITLE_ROOT_PTR).lwz(9, 0, 9);
    heapCheck(a, "no_valid", true);
    a.lwz(0, addr::ENTITLE_VALID_OFFSET, 9).ori(0, 0, 1).stw(0, addr::ENTITLE_VALID_OFFSET, 9);
    bump(a, off::VALID_FRAMES);
    a.label("no_valid");

     
    testFlag(a, Speed);
    a.beq("no_speed");
    a.addr(9, addr::GAME_TIME_PTR).lwz(9, 0, 9);
    heapCheck(a, "no_speed", true);
    a.lwz(8, addr::GAME_SPEED_OFFSET, 9).stw(8, off::SPEED_SEEN, 31);
    a.lwz(7, off::SPEED, 31).cmplw(8, 7).beq("no_speed");        
    a.lwz(6, off::SPEED_LAST, 31).cmplw(8, 6).beq("speed_write");    
    a.load32(6, kSpeedMinBits).cmplw(8, 6).blt("speed_skip");    
    a.load32(6, kSpeedMaxBits).cmplw(8, 6).bgt("speed_skip");
    a.label("speed_write").stw(7, addr::GAME_SPEED_OFFSET, 9).stw(7, off::SPEED_LAST, 31).b("no_speed");
    a.label("speed_skip");
    bump(a, off::SPEED_SKIPS);
    a.label("no_speed");

     
    testFlag(a, Viewmodel);
    a.beq("no_vm");
    a.addr(9, addr::VIEWMODEL_HANDLE).lwz(0, 0, 9).lwz(6, off::VM_HANDLE, 31).cmplw(0, 6).bne("no_vm");
    a.lwz(9, off::VM_ADDR, 31).lwz(0, off::VM_BITS, 31).stw(0, 0, 9);
    a.label("no_vm");

     
    testFlag(a, Respawn);
    a.beq("out");
    a.addr(9, addr::PLAYER_DESC_PTR).lwz(9, 0, 9).lwz(0, off::DESC, 31).cmplw(9, 0).bne("out");
    a.lwz(9, 4, 9).lwz(0, off::ELEMENTS, 31).cmplw(9, 0).bne("out");
    a.lwz(9, off::ELEMENT, 31).lwz(0, 0x84, 9).stw(0, off::SEEN, 31);
    a.cmpwi(0, 0).beq("dead");
    a.cmpwi(0, -1).bne("out");
    a.label("dead");
    a.lwz(0, off::MODE, 31).cmpwi(0, 1).bne("latched");
     
     
    a.addr(9, addr::LOCAL_PLAYER_HANDLE_SLOT).lwz(9, 0, 9);
    heapCheck(a, "out", true);
    a.li(3, 0).addr(12, addr::LOCAL_PLAYER_HANDLE).mtctr(12).bctrl();    
    a.cmpwi(3, 0).beq("out");
    a.cmpwi(3, -1).beq("out");
    a.b("call");
    a.label("latched").ld(3, off::ARG, 31);
    a.label("call").std_(3, off::LAST_ARG, 31);
    a.li(4, 0).li(5, -1).addr(12, addr::RESPAWN).mtctr(12).bctrl();       
    bump(a, off::CALLS);
    a.label("out");
    ppc::epilogue(a, FRAME, SAVE);
    a.blr();
    return a.link();
}

uint32_t programSize() { return (uint32_t(program(0x183E400).size()) + 15) & ~15u; }

std::vector<uint8_t> Block::encode() const {
    std::vector<uint8_t> b(kBlockSize, 0);
    put32(&b[off::FLAGS], flags);
    put32(&b[off::FOV], fov);
    put32(&b[off::SPEED], speed);
    put32(&b[off::SPEED_LAST], speedLast);
    put32(&b[off::VM_ADDR], vmAddr);
    put32(&b[off::VM_BITS], vmBits);
    put32(&b[off::VM_HANDLE], vmHandle);
    put32(&b[off::DESC], desc);
    put32(&b[off::ELEMENTS], elements);
    put32(&b[off::ELEMENT], element);
    put32(&b[off::MODE], mode);
    put32(&b[off::ARG], uint32_t(arg >> 32));
    put32(&b[off::ARG + 4], uint32_t(arg));
    return b;
}

Counters decode(const std::vector<uint8_t>& b) {
    Counters c;
    c.flags = get32(b, off::FLAGS);
    c.frames = get32(b, off::FRAMES);
    c.calls = get32(b, off::CALLS);
    c.seen = get32(b, off::SEEN);
    c.speedSkips = get32(b, off::SPEED_SKIPS);
    c.speedSeen = get32(b, off::SPEED_SEEN);
    c.validFrames = get32(b, off::VALID_FRAMES);
    c.suppressFrames = get32(b, off::SUPPRESS_FRAMES);
    c.lastArg = uint64_t(get32(b, off::LAST_ARG)) << 32 | get32(b, off::LAST_ARG + 4);
    return c;
}

}   

namespace brps3::patches {

bool fovValid(float deg) { return std::isfinite(deg) && deg >= kFovMin && deg <= kFovMax; }

bool fovGlobalPlausible(float v) { return v == kFovOff || (std::isfinite(v) && v >= 1.0f && v <= 170.0f); }

bool speedValid(float mult) { return std::isfinite(mult) && mult >= kSpeedMin && mult <= kSpeedMax; }

bool viewmodelDegValid(float deg) { return std::isfinite(deg) && deg >= kViewmodelMin && deg <= kViewmodelMax; }

bool viewmodelRadiansValid(float rad) { return std::isfinite(rad) && rad > 0.0f && rad < 3.141593f; }

float clampFov(float deg) { return std::isfinite(deg) ? std::clamp(deg, kFovMin, kFovMax) : kFovDefault; }

float clampSpeed(float mult) { return std::isfinite(mult) ? std::clamp(mult, kSpeedMin, kSpeedMax) : kSpeedDefault; }

float clampViewmodel(float deg) { return std::isfinite(deg) ? std::clamp(deg, kViewmodelMin, kViewmodelMax) : kViewmodelDefault; }

uint32_t viewmodelBits(float deg) { return world::floatBits(float(double(clampViewmodel(deg)) * kDegToRad)); }

ViewmodelTarget resolveViewmodel(world::Reader& mem) {
    ViewmodelTarget t;
    t.handle = mem.u32(addr::VIEWMODEL_HANDLE);
    if (t.handle == 0xFFFFFFFFu) {
        t.base = addr::VIEWMODEL_DEFAULT;
    } else if (t.handle == 0) {
        t.why = "the viewmodel settings handle is 0 (not set up yet: load into the game)";
        return t;
    } else {
        const world::TagTable table = world::readTagTable(mem);
        const world::Resolved r = world::resolve(mem, table, t.handle, world::isPackagePage(world::tagPage(t.handle)));
        if (!r.ok) {
            t.why = "the viewmodel settings handle " + world::hex8(t.handle) + " does not resolve: " + r.why;
            return t;
        }
        if ((r.item8 & r.desc.mask) > r.element) {
            t.why = "the viewmodel settings element has a bad back offset";
            return t;
        }
        t.base = r.body;
    }
    if (!world::mapped(t.base, addr::VIEWMODEL_OFFSET + 8) || (t.base & 3)) {
        t.why = "the viewmodel settings block " + world::hex8(t.base) + " is not mapped";
        return t;
    }
    t.target = t.base + addr::VIEWMODEL_OFFSET;
    t.current = mem.u32(t.target);
    if (mem.u32(addr::VIEWMODEL_HANDLE) != t.handle) {    
        t.why = "the viewmodel settings handle changed while it was resolved: try again";
        return t;
    }
    t.ok = true;
    return t;
}

EntitleChain readEntitleChain(world::Reader& mem) {
    EntitleChain c;
    c.root = mem.u32(addr::ENTITLE_ROOT_PTR);
    c.rootOk = world::heap(c.root, addr::ENTITLE_VALID_OFFSET + 4) && !(c.root & 3);
    if (c.rootOk) c.valid = mem.u32(c.root + addr::ENTITLE_VALID_OFFSET);
    const world::Bytes p = mem.bytes(addr::ENTITLE_PROVIDER, 0x10);
    c.providerVtable = p.u32(0);
    c.providerRoot = p.u32(4);
    c.providerInner = p.u32(0xC);
    c.suppressObj = mem.u32(addr::FLYIN_SUPPRESS_PTR);
    c.suppressOk = world::heap(c.suppressObj, 1);
    if (c.suppressOk) c.suppress = mem.bytes(c.suppressObj, 1).u8(0);
    return c;
}

}   
