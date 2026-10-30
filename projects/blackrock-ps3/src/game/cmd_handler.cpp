#include "game/cmd_handler.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>

#include "core/error.h"
#include "core/ppc.h"
#include "core/profile.h"
#include "core/runtime.h"
#include "game/world_addr.h"
#include "game/world_objects.h"
#include "game/world_tags.h"

namespace brps3::cmd {

namespace {

using ppc::Asm;
namespace PF = profile;
namespace A = world::addr;

constexpr int64_t kFrame = 0x480;                       
const std::vector<int> kSave = {2, 25, 28, 29, 30, 31};
constexpr int64_t kScratch = 0xC0;                      
constexpr uint32_t kViewMatrix = 0x1AE6F10;             
constexpr uint32_t kDescTag = 0x00, kDescRot = 0x10, kDescPos = 0x20, kDescScale = 0x2C, kDescOwner = 0x34, kDescTagOffset = 0x04,
                   kDescClass = 0x08;

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

void putF(std::vector<uint8_t>& b, uint32_t off, float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    put32(b, off, u);
}

uint32_t get32(const std::vector<uint8_t>& b, uint32_t off) {
    return uint32_t(b[off]) << 24 | uint32_t(b[off + 1]) << 16 | uint32_t(b[off + 2]) << 8 | b[off + 3];
}

float getF(const std::vector<uint8_t>& b, uint32_t off) {
    const uint32_t u = get32(b, off);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

std::string hx(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%08X", v);
    return buf;
}

uint64_t nowMs() {
    using namespace std::chrono;
    return uint64_t(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

}   

const Fingerprints& fingerprints() {
    static const Fingerprints fps = [] {
        Fingerprints out = world::kSobjectNewFingerprints;
        for (const auto& fp : PF::FINGERPRINTS)
            if (fp.first == PF::EMIT || fp.first == PF::EMIT_BASIC || fp.first == PF::PAYLOAD_INIT) out.push_back(fp);
        return out;
    }();
    return fps;
}

const char* resultName(uint32_t code) {
    switch (code & 0xFF) {
    case kOkResult: return "ok";
    case kCancelled: return "cancelled";
    case kBadArgs: return "bad arguments";
    case kNotEntityTag: return "not an entity tag";
    case kNoPool: return "sObject pool not set up";
    case kNoGlobals: return "sObject globals not set up";
    case kBusy: return "creation in progress";
    case kNoTagTable: return "tag table not set up";
    case kNotLoaded: return "tag not loaded";
    case kNotEntityClass: return "not an entity definition";
    case kBodyUnmapped: return "definition body unmapped";
    case kEmptyDefinition: return "empty definition";
    case kGarbageProp: return "garbage prop";
    case kTypeMismatch: return "type mismatch";
    case kNoCamera: return "camera unusable";
    case kNoPlayer: return "no player position";
    case kBadPoint: return "spawn point not finite";
    default: return "?";
    }
}

 

std::vector<uint8_t> handlerProgram(uint32_t base) {
     
     
     
     
    Asm a(base);
    auto vec3 = [&](bool store, int f0, int64_t off, int ra) {
        for (int i = 0; i < 3; ++i) store ? a.stfs(f0 + i, off + 4 * i, ra) : a.lfs(f0 + i, off + 4 * i, ra);
    };
     
    auto lo16 = [](uint32_t addr) { const uint32_t lo = addr & 0xFFFF; return (lo & 0x8000) ? int64_t(lo) - 0x10000 : int64_t(lo); };
    auto hi16 = [](uint32_t addr) { return int64_t(ppc::hiLo(addr).first); };
    auto ldAbs = [&](int rd, uint32_t addr) { a.lis(rd, hi16(addr)).lwz(rd, lo16(addr), rd); };
    auto code = [&](uint32_t c) { a.li(3, int64_t(c)); };

    ppc::prologue(a, kFrame, kSave);
    a.mr(12, 3).li(11, 15).bl("chk").beq("ret");                 
    a.mr(31, 12);
    a.lwz(0, 0, 31).load32(12, kMagic).cmplw(0, 12).bne("ret");
    a.lwz(0, kOffEnabled, 31).cmpwi(0, 1).bne("ret");
    a.lwz(12, kOffBeat, 31).addi(12, 12, 1).stw(12, kOffBeat, 31);
    a.li(25, kSpawnBatch);
    a.addi(30, 31, kSlotsOffset).li(29, kSlots);

     
    a.label("slot").lwz(0, kState, 30).cmpwi(0, kRunning).beq("step");
    a.cmpwi(0, kQueued).bne("next");
    a.lwarx(0, 0, 30).cmpwi(0, kQueued).bne("next").li(0, kRunning).stwcx(0, 0, 30).bne("next");
    a.label("step").lwz(12, kFrames, 30).addi(12, 12, 1).stw(12, kFrames, 30);
    a.lwz(0, kCancel, 30).cmpwi(0, 0).li(3, kCancelled).bne("finish_done");
    a.lwz(0, kOp, 30).cmpwi(0, kOpSpawn).beq("spawn");
    a.cmpwi(0, kOpLootEvent).beq("loot_event");
    a.label("bad_args").li(3, kBadArgs);
     
    a.label("refuse").lwz(0, kDone, 30).cmpwi(0, 0).bne("finish_done");
    a.li(0, kStateFailed).b("finish");
    a.label("complete").li(3, kOkResult);
    a.label("finish_done").li(0, kStateDone);
    a.label("finish").stw(3, kResult, 30).lwsync().stw(0, kState, 30);
    a.label("next").addi(30, 30, kSlotSize).addi(29, 29, -1).cmpwi(29, 0).bne("slot");
    a.label("ret");
    ppc::epilogue(a, kFrame, kSave);
    a.blr();

     
    a.label("spawn");
    a.lwz(12, kACount, 30).addi(12, 12, -1).cmplwi(12, kMaxSpawns - 1).bgt("bad_args");
    a.lwz(12, kAMode, 30).cmplwi(12, 2).bgt("bad_args");
    a.lwz(12, kAFacing, 30).cmplwi(12, 2).bgt("bad_args");
    a.lwz(12, kASource, 30).cmplwi(12, 1).bgt("bad_args");
     
    a.label("spawn_point").cmpwi(25, 0).ble("next");            
    a.lwz(28, kDone, 30);
     
    code(kNotEntityTag);
    a.lwz(5, kATag, 30).cmpwi(5, 0).bge("refuse");
    a.rlwinm(6, 5, 19, 18, 31).cmplwi(6, world::kFirstPackagePage).blt("refuse");
     
    code(kNoPool);
    a.lis(7, hi16(A::SOBJECT_POOL));
    a.lwz(9, lo16(A::SOBJECT_POOL) + world::pool::STRIDE, 7).cmplwi(9, world::sobj::STRIDE).bne("refuse");
    a.lwz(10, lo16(A::SOBJECT_POOL) + world::pool::CAPACITY, 7).addi(12, 10, -1).cmplwi(12, 0xFFFF).bgt("refuse");
    a.lwz(12, lo16(A::SOBJECT_POOL) + world::pool::BASE, 7).bl("chk3").beq("refuse");
    a.mulli(10, 10, world::sobj::STRIDE).add(12, 12, 10).addi(12, 12, -4).bl("chk3").beq("refuse");
     
    code(kNoGlobals);
    ldAbs(12, A::SOBJECT_GLOBALS_PTR);
    a.bl("chk3").beq("refuse").addi(12, 12, 0x3C4).bl("chk3").beq("refuse");
    code(kBusy);
    a.lwz(0, 0, 12).stw(0, kValue, 30).cmplwi(0, 4).bgt("refuse");
     
    code(kNoTagTable);
    ldAbs(12, A::TAG_TABLE_PTR);
    a.stw(12, kValue, 30).bl("chk3").beq("refuse");
    a.lwz(8, 0, 12).lwz(9, 8, 12).lwz(10, 0x10, 12);
    a.cmplwi(10, 0x40).bne("refuse").cmplw(9, 8).ble("refuse");
    a.subf(10, 8, 9).srwi(10, 10, 6).cmplwi(10, 0x4000).bgt("refuse");
    a.mr(12, 8).bl("chk3").beq("refuse").addi(12, 9, -4).bl("chk3").beq("refuse");
     
    code(kNotLoaded | kNlBeyond << 8);
    a.stw(10, kValue, 30).cmplw(6, 10).bge("refuse");
    a.slwi(12, 6, 6).add(12, 12, 8);                             
    a.lwz(7, 0x10, 12).lwz(9, 0x18, 12).lwz(10, 0x30, 12).lwz(4, 0x34, 12);    
    code(kNotLoaded | kNlUnregistered << 8);
    a.cmpwi(7, 0).beq("refuse").cmpwi(9, 0).beq("refuse");
    code(kNotLoaded | kNlUnmapped << 8);
    a.mr(12, 7).bl("chk3").beq("refuse").addi(12, 9, 0x10).bl("chk3").beq("refuse");
    code(kNotLoaded | kNlStride << 8);
    a.stw(10, kValue, 30).cmplwi(10, world::kPackageItemStride).bne("refuse");
    code(kNotLoaded | kNlIndex << 8);
    a.lwz(10, 0, 12).stw(10, kValue, 30);                        
    a.rlwinm(5, 5, 0, 19, 31).cmplw(5, 10).bge("refuse");
    code(kNotLoaded | kNlElement << 8);
    a.slwi(5, 5, 4).add(12, 7, 5).stw(12, kValue, 30).bl("chk3").beq("refuse");    
    code(kNotEntityClass);
    a.lwz(6, 0, 12).lwz(7, 8, 12);
    a.stw(6, kValue, 30).load32(0, world::kEntityClass).cmplw(6, 0).bne("refuse");
    code(kBodyUnmapped);
    a.and_(7, 7, 4).subf(12, 7, 12).stw(12, kValue, 30);         
    a.li(11, 0).bl("chk").beq("refuse");
    a.addi(12, 12, world::kTagRefOffset + world::kDefTypeOffset).li(11, 0).bl("chk").beq("refuse");
    a.lbz(6, 0, 12).stw(6, kValue, 30);                          
    code(kEmptyDefinition);
    a.cmpwi(6, 0).beq("refuse");
    code(kGarbageProp);
    a.cmpwi(6, world::TYPE_PROP_GARBAGE).beq("refuse");
    code(kTypeMismatch);
    a.lwz(7, kType, 30).cmpwi(7, -1).beq("type_ok").cmplw(6, 7).bne("refuse");
    a.label("type_ok").stw(6, kType, 30);
    a.lwz(0, kStarted, 30).cmpwi(0, 0).bne("point");

     
     
    const int64_t v = lo16(kViewMatrix);
    a.lis(9, hi16(kViewMatrix));
    a.lfs(13, kCEps3, 31).lfs(12, kCOne, 31);
    for (int64_t off : {0x0C, 0x1C, 0x2C}) a.lfs(1, v + off, 9).fabs(1, 1).fcmpu(0, 1, 13).bge("nocam");
    a.lfs(1, v + 0x3C, 9).fsubs(1, 1, 12).fabs(1, 1).fcmpu(0, 1, 13).bge("nocam");
    a.lfs(4, v + 0x08, 9).lfs(5, v + 0x18, 9).lfs(6, v + 0x28, 9);
    a.fmuls(7, 4, 4).fmadds(7, 5, 5, 7).fmadds(7, 6, 6, 7);
    a.fsubs(1, 7, 12).fabs(1, 1).lfs(13, kCEps2, 31).fcmpu(0, 1, 13).bge("nocam");
    a.fsqrts(7, 7).fneg(7, 7);
    a.fdivs(4, 4, 7).fdivs(5, 5, 7).fdivs(6, 6, 7);
    vec3(true, 4, kAim, 30);
    a.lfs(8, v + 0x30, 9).lfs(9, v + 0x34, 9).lfs(10, v + 0x38, 9);
    for (int i = 0; i < 3; ++i) {
        a.lfs(1, v + 16 * i, 9).lfs(2, v + 16 * i + 4, 9).lfs(3, v + 16 * i + 8, 9);
        a.fmuls(0, 1, 8).fmadds(0, 2, 9, 0).fmadds(0, 3, 10, 0).fneg(0, 0).stfs(0, kEye + 4 * i, 30);
    }
    a.li(0, 1).stw(0, kAimOk, 30).stw(0, kEyeOk, 30);
    a.label("nocam");
     
    a.lwz(0, kAMode, 30).cmpwi(0, 1).beq("player");
    a.lwz(0, kAFacing, 30).cmpwi(0, 0).bne("noplayer");
    a.lwz(0, kEyeOk, 30).cmpwi(0, 0).bne("noplayer");
    a.label("player");
    ldAbs(12, A::PLAYER_DESC_PTR);
    a.bl("chk3").beq("noplayer");
    a.lwz(0, 8, 12).cmplwi(0, world::player::DESC_ELEMENT_SIZE).bne("noplayer");
    a.lwz(12, 4, 12).bl("chk3").beq("noplayer");
    a.mr(9, 12).lwz(5, world::player::ELEM_HANDLE, 9);            
    a.bl("sobj").bne("noplayer");
    a.lwz(0, kASource, 30).cmpwi(0, 0).addi(12, 12, world::sobj::POSITION).beq("player_pos");
    a.addi(12, 9, world::player::ELEM_POSITION).bl("chk3").beq("noplayer");
    a.label("player_pos");
    vec3(false, 1, 0, 12);
    a.bl("bounded").bge("noplayer");
    a.lfs(0, kCZero, 31).fcmpu(0, 1, 0).bne("player_nz").fcmpu(0, 2, 0).bne("player_nz").fcmpu(0, 3, 0).beq("noplayer");
    a.label("player_nz");
    vec3(true, 1, kPlayer, 30);
    a.li(0, 1).stw(0, kPlayerOk, 30);
    a.lwz(12, kEyeOk, 30).cmpwi(12, 0).bne("noplayer");
    vec3(true, 1, kEye, 30);
    a.stw(0, kEyeOk, 30);
    a.label("noplayer");
     
    a.lwz(0, kAMode, 30).cmpwi(0, 0).bne("c_player");
    code(kNoCamera);
    a.lwz(12, kAimOk, 30).cmpwi(12, 0).beq("refuse");
    a.lfs(4, kADistance, 30);
    for (int i = 0; i < 3; ++i) a.lfs(1 + i, kEye + 4 * i, 30).lfs(0, kAim + 4 * i, 30).fmadds(1 + i, 0, 4, 1 + i);
    a.b("c_offset");
    a.label("c_player").cmpwi(0, 1).bne("c_coords");
    code(kNoPlayer);
    a.lwz(12, kPlayerOk, 30).cmpwi(12, 0).beq("refuse");
    vec3(false, 1, kPlayer, 30);
    a.label("c_offset");
    for (int i = 0; i < 3; ++i) a.lfs(0, kAOffset + 4 * i, 30).fadds(1 + i, 1 + i, 0);
    a.b("c_check");
    a.label("c_coords");
    vec3(false, 1, kACoords, 30);
    a.label("c_check");
    code(kBadPoint);
    a.bl("bounded").bge("refuse");
    vec3(true, 1, kCentre, 30);
     
    a.lfs(1, kCOne, 31).lfs(2, kCZero, 31);
    a.lwz(0, kAimOk, 30).cmpwi(0, 0).beq("basis");
    a.lfs(3, kAim, 30).lfs(4, kAim + 4, 30).fmuls(5, 3, 3).fmadds(5, 4, 4, 5).fsqrts(5, 5);
    a.lfs(6, kCEps6, 31).fcmpu(0, 5, 6).ble("basis");
    a.fdivs(1, 3, 5).fdivs(2, 4, 5);
    a.label("basis").stfs(1, kFwd, 30).stfs(2, kFwd + 4, 30).stfs(2, kRight, 30).fneg(1, 1).stfs(1, kRight + 4, 30);
    a.li(0, 1).stw(0, kStarted, 30);

     
    a.label("point");
    a.addi(11, 31, kOffTemplate - 4).addi(12, 31, kOffDesc - 4).li(0, 32).mtctr(0);
    a.label("copy").lwzu(0, 4, 11).stwu(0, 4, 12).bdnz("copy");
    a.lwz(0, kATag, 30).stw(0, kOffDesc + kDescTag, 31).lwz(0, kAScale, 30).stw(0, kOffDesc + kDescScale, 31);
     
    a.lwz(5, kACount, 30).li(6, 1);
    a.label("per_row").mullw(7, 6, 6).cmplw(7, 5).bge("per_row_ok").addi(6, 6, 1).b("per_row");
    a.label("per_row_ok").add(7, 5, 6).addi(7, 7, -1).divwu(7, 7, 6);
    a.divwu(8, 28, 6).mullw(9, 8, 6).subf(10, 9, 28).mr(4, 6);         
    a.addi(12, 7, -1).cmplw(8, 12).bne("in_row").subf(4, 9, 5);         
    a.label("in_row");
    a.add(10, 10, 10).subf(10, 4, 10).addi(10, 10, 1);                  
    a.add(8, 8, 8).subf(8, 7, 8).addi(8, 8, 1);                         
    a.std_(10, kScratch, 1).std_(8, kScratch + 8, 1);
    a.lfd(1, kScratch, 1).lfd(2, kScratch + 8, 1).fcfid(1, 1).fcfid(2, 2).frsp(1, 1).frsp(2, 2);
    a.lfs(3, kASpacing, 30).lfs(0, kCHalf, 31).fmuls(3, 3, 0).fmuls(1, 1, 3).fmuls(2, 2, 3);    
    for (int i = 0; i < 2; ++i) {
        a.lfs(7 + i, kCentre + 4 * i, 30).lfs(0, kRight + 4 * i, 30).fmuls(0, 0, 1).fadds(7 + i, 7 + i, 0);
        a.lfs(0, kFwd + 4 * i, 30).fmuls(0, 0, 2).fadds(7 + i, 7 + i, 0);
    }
    a.lfs(9, kCentre + 8, 30);
    vec3(true, 7, kOffDesc + kDescPos, 31);
    a.slwi(9, 28, 4).add(9, 9, 30);
    vec3(true, 7, kResults + 4, 9);
     
    a.lwz(0, kAFacing, 30).cmpwi(0, 1).beq("dir_aim").cmpwi(0, 0).bne("rot_identity");
    a.lwz(0, kEyeOk, 30).cmpwi(0, 0).beq("rot_identity");
    a.lfs(1, kEye, 30).fsubs(1, 1, 7).lfs(2, kEye + 4, 30).fsubs(2, 2, 8).lfs(3, kCZero, 31).b("rot");
    a.label("dir_aim").lwz(0, kAimOk, 30).cmpwi(0, 0).beq("rot_identity");
    vec3(false, 1, kAim, 30);
     
     
    a.label("rot").fmuls(4, 1, 1).fmadds(4, 2, 2, 4).fmadds(5, 3, 3, 4);    
    a.lfs(13, kCBig, 31).fcmpu(0, 5, 13).bge("rot_identity");           
    a.lfs(13, kCEps6, 31).fcmpu(0, 5, 13).blt("rot_identity");
    a.fsqrts(6, 4).fsqrts(7, 5);
    a.lfs(10, kCHalf, 31).lfs(11, kCOne, 31).lfs(12, kCZero, 31);
    a.fmr(8, 11).fmr(9, 12).fcmpu(0, 6, 13).blt("yaw");                
    a.fdivs(8, 1, 6).fdivs(9, 2, 6).bl("half");
    a.label("yaw").fmr(4, 8).fmr(5, 9);
    a.fdivs(8, 6, 7).fdivs(9, 3, 7).bl("half");
    a.fmuls(0, 5, 9).stfs(0, kOffDesc + kDescRot, 31);
    a.fmuls(0, 4, 9).fneg(0, 0).stfs(0, kOffDesc + kDescRot + 4, 31);
    a.fmuls(0, 5, 8).stfs(0, kOffDesc + kDescRot + 8, 31);
    a.fmuls(0, 4, 8).stfs(0, kOffDesc + kDescRot + 12, 31).b("call");
    a.label("rot_identity").lfs(0, kCZero, 31).stfs(0, kOffDesc + kDescRot, 31).stfs(0, kOffDesc + kDescRot + 4, 31);
    a.stfs(0, kOffDesc + kDescRot + 8, 31).lfs(0, kCOne, 31).stfs(0, kOffDesc + kDescRot + 12, 31);
     
     
    a.label("call").li(0, -1).stw(0, kOffOut, 31);
    a.load32(2, PF::TOC).addi(3, 31, kOffOut).addi(4, 31, kOffDesc).bl(A::SOBJECT_NEW);
    a.lwz(5, kOffOut, 31).slwi(9, 28, 4).add(9, 9, 30).stw(5, kResults, 9);
    a.li(4, kSpawnNone << 8).cmpwi(5, 0).beq("status").cmpwi(5, -1).beq("status");
    a.li(4, kSpawnNotLive << 8).bl("sobj").bne("status");
    a.lbz(0, world::sobj::TYPE, 12).lwz(6, kType, 30).cmplw(0, 6).ori(4, 0, kSpawnWrongType << 8).bne("status");
    a.ori(4, 0, kSpawnOk << 8).lwz(6, kOk, 30).addi(6, 6, 1).stw(6, kOk, 30);
    a.label("status").slwi(9, 28, 1).add(9, 9, 30).sth(4, kStatus, 9);
    a.addi(28, 28, 1).stw(28, kDone, 30).addi(25, 25, -1);
    a.lwz(0, kACount, 30).cmplw(28, 0).bge("complete").b("spawn_point");

     
     
    a.label("loot_event");
    code(kBadArgs);
    a.lwz(12, kAICount, 30).addi(12, 12, -1).cmplwi(12, kMaxLootEventCount - 1).bgt("refuse");
    a.load32(2, PF::TOC);
    a.lwz(0, kAIType, 30).cmpwi(0, 0).beq("emit_basic");
    a.lwz(7, kASize, 30).addi(12, 7, -24).cmplwi(12, kMaxPayload - 24).bgt("refuse");
    a.lwz(8, kANFields, 30).cmplwi(8, kMaxFields).bgt("refuse");
    a.lwz(3, kASchema, 30).addi(4, 1, kPayloadOffset).bl(PF::PAYLOAD_INIT);
    a.lwz(7, kASize, 30).lwz(8, kANFields, 30).addi(9, 30, kAFields);
    code(kBadArgs);
     
    a.label("field").cmpwi(8, 0).beq("emit_typed");
    a.lhz(10, 0, 9).lbz(6, 2, 9).lwz(5, 4, 9);
    a.cmplwi(10, 0x18).blt("refuse");
    a.add(12, 10, 6).cmplw(12, 7).bgt("refuse");
    a.addi(12, 6, -1).andDot(12, 12, 10).bne("refuse");
    a.addi(12, 1, kPayloadOffset).add(12, 12, 10);
    a.cmpwi(6, 1).bne("f2").stb(5, 0, 12).b("fnext");
    a.label("f2").cmpwi(6, 2).bne("f4").sth(5, 0, 12).b("fnext");
    a.label("f4").cmpwi(6, 4).bne("refuse").stw(5, 0, 12);
    a.label("fnext").addi(9, 9, kFieldSize).addi(8, 8, -1).b("field");
    a.label("emit_typed").load32(2, PF::TOC).addi(3, 30, kAHash).lwz(4, kASchema, 30).addi(5, 1, kPayloadOffset).bl(PF::EMIT).b("emitted");
    a.label("emit_basic").addi(3, 30, kAHash).bl(PF::EMIT_BASIC);
    a.label("emitted").lwz(12, kDone, 30).addi(12, 12, 1).stw(12, kDone, 30);
    a.lwz(0, kAICount, 30).cmplw(12, 0).bge("complete").b("next");

     
     
     
    a.label("sobj").mflr(10);
    a.cmpwi(5, 0).beq("sobj_bad").cmpwi(5, -1).beq("sobj_bad");
    a.lis(12, hi16(A::SOBJECT_POOL)).addi(12, 12, lo16(A::SOBJECT_POOL));
    a.lwz(6, world::pool::STRIDE, 12).cmplwi(6, world::sobj::STRIDE).bne("sobj_bad");
    a.lwz(7, world::pool::CAPACITY, 12).lwz(8, world::pool::BASE, 12).rlwinm(6, 5, 0, 19, 31).cmplw(6, 7).bge("sobj_bad");
    a.mulli(6, 6, world::sobj::STRIDE).add(12, 8, 6).bl("chk3").beq("sobj_bad");
    a.lwz(0, world::sobj::HANDLE, 12).cmplw(0, 5).bne("sobj_bad");
    a.lwz(0, world::sobj::FLAGS, 12).andi_(0, 0, world::sobj::FLAG_DELETING).mtlr(10).blr();
    a.label("sobj_bad").li(0, 1).cmpwi(0, 0).mtlr(10).blr();
     
    a.label("bounded").lfs(13, kCMax, 31);
    a.fabs(0, 1).fcmpu(0, 0, 13).bgelr().fabs(0, 2).fcmpu(0, 0, 13).bgelr().fabs(0, 3).fcmpu(0, 0, 13).blr();
     
    a.label("half").fcmpu(0, 8, 12).blt("half_neg");
    a.fadds(0, 11, 8).fmuls(0, 0, 10).fsqrts(0, 0).fmuls(9, 9, 10).fdivs(9, 9, 0).fmr(8, 0).blr();
    a.label("half_neg").fsubs(0, 11, 8).fmuls(0, 0, 10).fsqrts(0, 0).fcmpu(0, 9, 12).bge("half_pos").fneg(0, 0);
    a.label("half_pos").fmuls(9, 9, 10).fdivs(8, 9, 0).fmr(9, 0).blr();
     
     
     
    a.label("chk3").li(11, 3);
    a.label("chk").clrldi(12, 12, 32).andDot(0, 12, 11).bne("chk_bad");
    a.addis(0, 12, -0x3000).lis(11, 0x5000).cmplw(0, 11).bltlr();
    a.addis(0, 12, -0x1).lis(11, 0x1C7).cmplw(0, 11).bltlr();
    a.label("chk_bad").cmpw(0, 0).blr();

    auto out = a.link();
    if (out.size() > kProgramMax) throw Error("command handler is " + std::to_string(out.size()) + " bytes, larger than kProgramMax");
    return out;
}

uint32_t programAllocSize() {
    static const uint32_t size = uint32_t((handlerProgram(PF::CAVE).size() + 15) & ~size_t(15));
    return size;
}

 

std::vector<uint8_t> encodeBlock(const std::array<uint32_t, 32>& t) {
    std::vector<uint8_t> b(kBlockSize, 0);
    put32(b, 0, kMagic);
    put32(b, kOffEnabled, 1);
    putF(b, kCHalf, 0.5f);
    putF(b, kCOne, 1.0f);
    putF(b, kCEps3, 0.001f);
    putF(b, kCEps2, 0.01f);
    putF(b, kCEps6, 1e-6f);
    putF(b, kCMax, 1e7f);
    putF(b, kCZero, 0.0f);
    putF(b, kCBig, 1e30f);
    for (uint32_t i = 0; i < 32; ++i) put32(b, kOffTemplate + 4 * i, t[i]);
    put32(b, kOffTemplate + kDescTagOffset, world::kTagRefOffset);
    put32(b, kOffTemplate + kDescClass, world::kPlacementClass);
    put32(b, kOffTemplate + kDescOwner, 0xFFFFFFFFu);    
    return b;
}

namespace {
std::vector<uint8_t> body(uint32_t op, uint32_t seq) {
    std::vector<uint8_t> b(kArgs + kArgsSize, 0);
    put32(b, kOp, op);
    put32(b, kSeq, seq);
    return b;
}
bool finiteF(float f) { return std::isfinite(f); }
}   

std::vector<uint8_t> encodeSpawn(const SpawnArgs& a, uint32_t seq) {
    if (!world::validHandle(a.tag) || !world::isPackagePage(world::tagPage(a.tag)) || !(a.tag & 0x80000000u))
        throw Error("Tag " + hx(a.tag) + " is not an entity tag (page " + hx(world::tagPage(a.tag)) + " is not a content package)");
    if (a.count < 1 || a.count > kMaxSpawns) throw Error("spawn count must be 1.." + std::to_string(kMaxSpawns));
    if (a.mode > 2 || a.facing > 2 || a.source > 1) throw Error("bad spawn placement mode");
    if (a.expectedType > 255) throw Error("bad expected type");
    for (float f : {a.distance, a.spacing, a.scale, a.offset[0], a.offset[1], a.offset[2], a.coords[0], a.coords[1], a.coords[2]})
        if (!finiteF(f)) throw Error("spawn placement values must be finite");
    if (!(a.scale > 0)) throw Error("spawn scale must be positive");
    auto b = body(kOpSpawn, seq);
    put32(b, kType, a.expectedType < 0 ? 0xFFFFFFFFu : uint32_t(a.expectedType));
    put32(b, kATag, a.tag);
    put32(b, kAMode, a.mode);
    put32(b, kAFacing, a.facing);
    put32(b, kACount, a.count);
    put32(b, kASource, a.source);
    putF(b, kADistance, a.distance);
    putF(b, kASpacing, a.spacing);
    putF(b, kAScale, a.scale);
    for (uint32_t i = 0; i < 3; ++i) {
        putF(b, kAOffset + 4 * i, a.offset[i]);
        putF(b, kACoords + 4 * i, a.coords[i]);
    }
    return b;
}

std::vector<uint8_t> encodeLootEvent(const LootEventArgs& a, uint32_t seq) {
    const bool typed = a.type != 0;
    if (a.count < 1 || a.count > kMaxLootEventCount) throw Error("loot_event count must be 1.." + std::to_string(kMaxLootEventCount));
    if (typed && !(24 <= a.size && a.size <= kMaxPayload)) throw Error("unsupported payload size");
    if (a.fields.size() > kMaxFields) throw Error("at most " + std::to_string(kMaxFields) + " payload fields");
    for (const Field& f : a.fields) {
        const uint64_t w = f.width;
        if (!typed || (w != 1 && w != 2 && w != 4) || f.offset < 0x18 || uint64_t(f.offset) + w > a.size || f.offset % w ||
            (w < 4 && (uint64_t(f.value) >> (8 * w))))
            throw Error("invalid field 0x" + hx(f.offset) + ":" + std::to_string(f.width) + ":0x" + hx(f.value));
    }
    auto b = body(kOpLootEvent, seq);
    put32(b, kAHash, a.hash);
    put32(b, kASchema, a.schema);
    put32(b, kAIType, a.type);
    put32(b, kASize, a.size);
    put32(b, kAICount, a.count);
    put32(b, kANFields, uint32_t(a.fields.size()));
    for (size_t i = 0; i < a.fields.size(); ++i) {
        const uint32_t o = kAFields + kFieldSize * uint32_t(i);
        put16(b, o, a.fields[i].offset);
        b[o + 2] = uint8_t(a.fields[i].width);
        put32(b, o + 4, a.fields[i].value);
    }
    return b;
}

SlotHeader parseHeader(const std::vector<uint8_t>& b) {
    if (b.size() < kHeaderRead) throw Error("command slot read back short");
    SlotHeader h;
    h.state = get32(b, kState);
    h.op = get32(b, kOp);
    h.seq = get32(b, kSeq);
    h.result = get32(b, kResult);
    h.cancel = get32(b, kCancel);
    h.done = get32(b, kDone);
    h.ok = get32(b, kOk);
    h.frames = get32(b, kFrames);
    h.started = get32(b, kStarted);
    h.type = get32(b, kType);
    h.value = get32(b, kValue);
    return h;
}

std::vector<SpawnOut> parseSpawnResults(const std::vector<uint8_t>& b, uint32_t n) {
    n = std::min(n, kMaxSpawns);
    if (b.size() < kResults - kStatus + n * kResultSize) throw Error("command results read back short");
    std::vector<SpawnOut> out;
    for (uint32_t i = 0; i < n; ++i) {
        SpawnOut s;
        const uint32_t st = uint32_t(b[2 * i]) << 8 | b[2 * i + 1];
        s.status = st >> 8;
        s.liveType = uint8_t(st);
        const uint32_t r = kResults - kStatus + i * kResultSize;
        s.handle = get32(b, r);
        s.x = getF(b, r + 4);
        s.y = getF(b, r + 8);
        s.z = getF(b, r + 12);
        out.push_back(s);
    }
    return out;
}

 

uint32_t Handler::nextSeq() {
    std::lock_guard<std::mutex> lock(m_);
    return ++seq_;
}

void Handler::ensure(Runtime& rt) {
    std::lock_guard<std::mutex> lock(m_);
    if (!verified_) {
        std::string bad;
        for (const auto& [addr, want] : fingerprints()) {
            const auto w = parseHexBytes(want);
            if (hexBytes(rt.read(addr, uint32_t(w.size()))) != hexBytes(w)) bad += (bad.empty() ? "" : ", ") + hx(addr);
        }
        if (!bad.empty())
            throw Error(std::string("Command handler: game code at ") + bad + " differs from build " + PF::BUILD + " (or is patched by another tool)");
        verified_ = true;
    }
    code_ = rt.sharedCode(kCodeName, [](uint32_t b) { return handlerProgram(b); }, programAllocSize());
    if (!block_) {
        const auto b = encodeBlock(world::spawnDescTemplate());
        block_ = rt.alloc(kBlockSize, b.data(), b.size());
        busy_.fill(false);
        abandoned_.fill(false);
    }
    if (!rt.hasTask(kTaskName)) {
         
        if (generation_) {
            const auto all = rt.read(block_ + kSlotsOffset, kSlots * kSlotSize);
            for (uint32_t i = 0; i < kSlots; ++i) {
                const uint32_t st = get32(all, i * kSlotSize + kState);
                if (st == kQueued || st == kRunning) rt.writeU32(slotAddr(i), kFree, true);
                abandoned_[i] = false;
            }
        }
        rt.addTaskAt(kTaskName, code_, block_);
        ++generation_;
    }
}

int Handler::takeSlot(Runtime& rt) {
    for (uint32_t i = 0; i < kSlots; ++i)
        if (!busy_[i] && !abandoned_[i]) {
            busy_[i] = true;
            return int(i);
        }
     
    for (uint32_t i = 0; i < kSlots; ++i) {
        if (!abandoned_[i] || busy_[i]) continue;
        const uint32_t st = rt.u32(slotAddr(i));
        if (st == kQueued || st == kRunning) continue;
        abandoned_[i] = false;
        busy_[i] = true;
        return int(i);
    }
    throw Error("all " + std::to_string(kSlots) + " command slots are busy (the game is not running the command handler?)");
}

Outcome Handler::run(Runtime& rt, const std::vector<uint8_t>& bodyIn, const std::function<bool()>& cancelled,
                     const std::function<void(uint32_t)>& progress, bool readResults, int stallMs, int pollMs) {
    if (bodyIn.size() != kArgs + kArgsSize) throw Error("command body must be " + std::to_string(kArgs + kArgsSize) + " bytes");
    ensure(rt);
    uint32_t slot = 0, seq = 0, gen = 0;
    {
        std::lock_guard<std::mutex> lock(m_);
        slot = uint32_t(takeSlot(rt));
        seq = ++seq_;
        gen = generation_;
    }
    auto release = [&](bool abandoned) {
        std::lock_guard<std::mutex> lock(m_);
        busy_[slot] = false;
        abandoned_[slot] = abandoned;
    };
    const uint32_t at = slotAddr(slot);
    std::vector<uint8_t> b = bodyIn;
    put32(b, kState, kFree);
    put32(b, kSeq, seq);
    Outcome out;
    try {
        rt.write(at + 4, b.data() + 4, b.size() - 4, true);      
        rt.writeU32(at, kQueued, false);                        
    } catch (...) {
        release(false);
        throw;
    }
    bool cancelSent = false;
    uint32_t lastDone = 0, lastFrames = 0, lastState = kQueued;
    uint64_t lastMove = nowMs();
    bool stalledCancel = false;
    try {
        for (;;) {
            out.header = parseHeader(rt.read(at, kHeaderRead));
            const SlotHeader& h = out.header;
            if (h.seq != seq) throw Error("command slot " + std::to_string(slot) + " was overwritten (sequence " + std::to_string(h.seq) + ")");
            if (h.done != lastDone) {
                lastDone = h.done;
                if (progress) progress(h.done);
            }
            if (h.finished()) break;
            const uint64_t now = nowMs();
            if (h.frames != lastFrames || h.state != lastState) {
                lastFrames = h.frames;
                lastState = h.state;
                lastMove = now;
            }
            bool gone = false;
            {
                std::lock_guard<std::mutex> lock(m_);
                gone = generation_ != gen;
            }
            if (gone || !rt.hasTask(kTaskName)) {
                 
                rt.writeU32(at, kFree, false);
                out.panicked = true;
                break;
            }
            if (cancelled && !cancelSent && cancelled()) {
                rt.writeU32(at + kCancel, 1, false);
                cancelSent = true;
            }
            if (now - lastMove > uint64_t(std::max(stallMs, 100))) {
                if (h.state == kQueued) {
                    rt.writeU32(at, kFree, false);                  
                    const auto again = parseHeader(rt.read(at, kHeaderRead));
                    if (again.state == kFree) {
                        release(false);
                        throw Error("The game did not run the command (no frames for " + std::to_string(stallMs / 1000) +
                                    " s: loading screen, menu or paused?)");
                    }
                    lastMove = now;                                 
                    continue;
                }
                if (!stalledCancel) {
                    rt.writeU32(at + kCancel, 1, false);            
                    stalledCancel = true;
                    lastMove = now;
                    continue;
                }
                release(true);
                throw Error("The game stopped running the command after " + std::to_string(h.done) + " step(s) (loading screen?): it was cancelled");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(std::max(pollMs, 1)));
        }
        if (readResults && out.header.op == kOpSpawn) {
            const uint32_t n = std::min(out.header.done, kMaxSpawns);
            out.tail = rt.read(at + kStatus, kResults - kStatus + n * kResultSize);
        }
    } catch (...) {
        bool owned = false;
        {
            std::lock_guard<std::mutex> lock(m_);
            owned = busy_[slot];
        }
        if (owned) {
             
            try {
                rt.writeU32(at + kCancel, 1, false);
            } catch (...) {
            }
            release(true);
        }
        throw;
    }
    release(false);
    return out;
}

void Handler::shutdown(Runtime& rt) {
    std::lock_guard<std::mutex> lock(m_);
    try {
        rt.removeTask(std::string(kTaskName));
    } catch (const std::exception& e) {
        coreLog(std::string("command handler: task removal failed: ") + e.what());
    }
    if (block_) rt.freeLater(block_);                           
    block_ = 0;
    code_ = 0;
    verified_ = false;
    busy_.fill(false);
    abandoned_.fill(false);
}

}   
