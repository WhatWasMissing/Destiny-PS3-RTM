#include "game/movement_stub.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "core/error.h"
#include "core/ppc.h"
#include "game/movement_addr.h"
#include "game/movement_pad.h"

namespace brps3::movement {

namespace {

using ppc::Asm;

void put32(std::vector<uint8_t>& b, uint32_t off, uint32_t v) {
    b[off] = uint8_t(v >> 24);
    b[off + 1] = uint8_t(v >> 16);
    b[off + 2] = uint8_t(v >> 8);
    b[off + 3] = uint8_t(v);
}
void putF(std::vector<uint8_t>& b, uint32_t off, float f) { put32(b, off, world::floatBits(f)); }

}   


const char* flyStatusText(uint32_t s) {
    switch (s) {
    case 0: return "Flying";
    case 1: return "Off";
    case 2: return "Physics body check failed";
    case 3: return "Waiting for your player";
    case 4: return "Controller not connected";
    case 5: return "Hovering: camera direction unreadable";
    }
    return "Unknown";
}

const char* opkStatusText(uint32_t s) {
    switch (s) {
    case 0: return "Running";
    case 1: return "Off";
    case 2: return "Physics body check failed";
    case 3: return "Waiting for your player";
    case 5: return "No physics world";
    case 6: return "No gather target (camera / position)";
    }
    return "Unknown";
}

const char* playerCheckText(uint32_t s) {
    switch (s) {
    case 0: return "ok";
    case 1: return "idle";
    case 2: return "body check failed";
    case 3: return "identity check failed";
    }
    return "unknown";
}

float stepPerFrame(float speed, float fps) {
    if (!std::isfinite(speed) || !(speed > 0.0f && speed <= d1::FLY_SPEED_MAX)) speed = d1::FLY_SPEED;
    if (!std::isfinite(fps) || fps < 10.0f || fps > 120.0f) fps = 30.0f;
     
    const float scale = std::clamp((1000.0f / fps) / d1::XBOX_TICK_MS, 1.0f, 4.0f);
    return speed * scale;
}

 

std::vector<uint8_t> encodeControl(const Control& c) {
    std::vector<uint8_t> b(ctl::SIZE, 0);
    put32(b, ctl::MAGIC, c.magic);
    put32(b, ctl::FRAMES, c.frames);
    put32(b, ctl::PAD_ON, c.padOn ? 1u : 0u);
    put32(b, ctl::PAD_HELD, c.held);
    put32(b, ctl::PAD_SEQ, c.seq);
    put32(b, ctl::PAD_CONN, c.connected);
    for (int i = 0; i < 4; ++i) {
        b[ctl::PAD_STICKS + 2 * i] = uint8_t(uint16_t(c.sticks[i]) >> 8);
        b[ctl::PAD_STICKS + 2 * i + 1] = uint8_t(c.sticks[i]);
    }
    for (uint32_t i = 0; i < 16; ++i) put32(b, ctl::PAD_RING + 4 * i, c.ring[i]);
    put32(b, ctl::PAD_MASKS, maskOf(Btn::L2));                     
    put32(b, ctl::PAD_MASKS + 4, maskOf(Btn::R2));
    for (uint32_t i = 0; i < uint32_t(pad::BUTTONS); ++i) put32(b, ctl::PAD_MASKS + 8 + 4 * i, kMaskOf[i]);    
    put32(b, ctl::P_PROBE, c.probe ? 1u : 0u);
    put32(b, ctl::P_HANDLE, c.handle);
    put32(b, ctl::P_ELEM, c.element);
    put32(b, ctl::P_ENTRY, c.entry);
    put32(b, ctl::P_ROW, c.row);
    put32(b, ctl::P_BODY, c.body);
    put32(b, ctl::P_PSLOT, c.physicsSlot);
    put32(b, ctl::P_STATUS, c.playerStatus);
    put32(b, ctl::FLY_ON, c.flyOn ? 1u : 0u);
    put32(b, ctl::FLY_BRAKE, c.brake ? 1u : 0u);
    put32(b, ctl::FLY_PREC, c.precision ? 1u : 0u);
    putF(b, ctl::FLY_STEP, c.step);
    putF(b, ctl::FLY_PFAC, std::clamp(c.precisionFactor, 0.01f, 1.0f));
    putF(b, ctl::FLY_HOVER, d1::HOVER_Z);
    putF(b, ctl::FLY_SCALE, d1::STICK_SCALE);
    put32(b, ctl::FLY_STATUS, c.flyStatus);
    put32(b, ctl::FLY_COUNT, c.flyCount);
    put32(b, ctl::FLY_MOVES, c.flyMoves);
    putF(b, ctl::EPS, d1::RIGHT_EPS);
    put32(b, ctl::OPK_GATHER, c.gather ? 1u : 0u);
    put32(b, ctl::OPK_EDGE, c.edge ? 1u : 0u);
    putF(b, ctl::OPK_DIST, std::clamp(c.dist, 1.0f, 20.0f));
    putF(b, ctl::OPK_EDGEZ, d1::EDGE_Z);
    putF(b, ctl::OPK_LIFT, d1::OPK_LIFT);
    putF(b, ctl::OPK_MIN, d1::OPK_MIN_LEN);
    putF(b, ctl::OPK_TX, c.target.x);
    putF(b, ctl::OPK_TY, c.target.y);
    putF(b, ctl::OPK_TZ, c.target.z);
    put32(b, ctl::OPK_MOVED, c.opkMoved);
    put32(b, ctl::OPK_SEEN, c.opkSeen);
    put32(b, ctl::OPK_STATUS, c.opkStatus);
    put32(b, ctl::WARP_N, c.warpFrames);
    putF(b, ctl::WARP_X, c.warp.x);
    putF(b, ctl::WARP_Y, c.warp.y);
    putF(b, ctl::WARP_Z, c.warp.z);
    putF(b, ctl::FWD_MIN, 0.25f);
    putF(b, ctl::FWD_MAX, 4.0f);
    return b;
}

std::vector<uint8_t> encodeConfig(const Control& c) {
    const std::vector<uint8_t> all = encodeControl(c);
    return std::vector<uint8_t>(all.begin() + ctl::CONFIG, all.begin() + ctl::CONFIG_END);
}

Control decodeControl(const world::Bytes& b) {
    Control c;
    c.magic = b.u32(ctl::MAGIC);
    c.frames = b.u32(ctl::FRAMES);
    c.padOn = b.u32(ctl::PAD_ON) != 0;
    c.held = b.u32(ctl::PAD_HELD);
    c.seq = b.u32(ctl::PAD_SEQ);
    c.connected = b.u32(ctl::PAD_CONN);
    for (uint32_t i = 0; i < 4; ++i) c.sticks[i] = int16_t(b.u16(ctl::PAD_STICKS + 2 * i));
    for (uint32_t i = 0; i < 16; ++i) c.ring[i] = b.u32(ctl::PAD_RING + 4 * i);
    c.probe = b.u32(ctl::P_PROBE) != 0;
    c.handle = b.u32(ctl::P_HANDLE);
    c.element = b.u32(ctl::P_ELEM);
    c.entry = b.u32(ctl::P_ENTRY);
    c.row = b.u32(ctl::P_ROW);
    c.body = b.u32(ctl::P_BODY);
    c.physicsSlot = b.u32(ctl::P_PSLOT);
    c.playerStatus = b.u32(ctl::P_STATUS);
    c.flyOn = b.u32(ctl::FLY_ON) != 0;
    c.brake = b.u32(ctl::FLY_BRAKE) != 0;
    c.precision = b.u32(ctl::FLY_PREC) != 0;
    c.step = b.f32(ctl::FLY_STEP);
    c.precisionFactor = b.f32(ctl::FLY_PFAC);
    c.flyStatus = b.u32(ctl::FLY_STATUS);
    c.flyCount = b.u32(ctl::FLY_COUNT);
    c.flyMoves = b.u32(ctl::FLY_MOVES);
    c.gather = b.u32(ctl::OPK_GATHER) != 0;
    c.edge = b.u32(ctl::OPK_EDGE) != 0;
    c.dist = b.f32(ctl::OPK_DIST);
    c.target = {b.f32(ctl::OPK_TX), b.f32(ctl::OPK_TY), b.f32(ctl::OPK_TZ)};
    c.opkMoved = b.u32(ctl::OPK_MOVED);
    c.opkSeen = b.u32(ctl::OPK_SEEN);
    c.opkStatus = b.u32(ctl::OPK_STATUS);
    c.warpFrames = b.u32(ctl::WARP_N);
    c.warp = {b.f32(ctl::WARP_X), b.f32(ctl::WARP_Y), b.f32(ctl::WARP_Z)};
    return c;
}

 

namespace {

 
struct Gen {
    Asm& a;
    int heapReg;                         
    int n = 0;
    std::string L(const char* s) { return std::string(s) + "_" + std::to_string(n++); }
     
     
    void heap(int r, const std::string& fail) {
        a.addis(4, r, -0x3000).cmplw(4, heapReg).bge(fail);
        a.andi_(4, r, 3).bne(fail);
    }
     
    void finite(int r, const std::string& fail) { a.rlwinm(4, r, 9, 24, 31).cmpwi(4, 0xFF).beq(fail); }
     
     
     
     
    void identity(int rowReg, const std::string& fail) {
        a.lwz(3, ctl::P_HANDLE, 31);
        a.cmpwi(3, 0).beq(fail).cmpwi(3, -1).beq(fail);
        a.lwz(9, ctl::P_ELEM, 31);
        heap(9, fail);
        a.lwz(5, 0x84, 9).cmplw(5, 3).bne(fail);
        a.lwz(9, ctl::P_ENTRY, 31);
        heap(9, fail);
        a.lwz(5, 0xC, 9).cmplw(5, 3).bne(fail);
        a.lwz(5, 4, 9).andi_(5, 5, 4).bne(fail);
        a.lwz(rowReg, ctl::P_ROW, 31);
        heap(rowReg, fail);
        a.lwz(5, 0x54, rowReg).cmplw(5, 3).bne(fail);
    }
};

std::vector<int> regs(int first) {
    std::vector<int> r;
    for (int i = first; i <= 31; ++i) r.push_back(i);
    return r;
}

}   

std::vector<uint8_t> tickProgram(uint32_t base) {
    Asm a(base);
    Gen g{a, 24};
    using namespace enc;
    auto w = [&a](uint32_t word) { a.word(word); };
     
    auto dead = [&](int r) {
        const std::string ok = g.L("dz");
        a.cmpwi(r, d1::DEADZONE).bge(ok).cmpwi(r, -d1::DEADZONE).ble(ok).li(r, 0).label(ok);
    };
     
     
    const auto save = regs(24);
    constexpr int64_t frame = 0xC0;
    ppc::prologue(a, frame, save);
    a.mr(31, 3);
    a.lwz(0, ctl::MAGIC, 31).load32(11, kMagic).cmplw(0, 11).bne("exit");
    a.lwz(9, ctl::FRAMES, 31).addi(9, 9, 1).stw(9, ctl::FRAMES, 31);
    a.lis(24, 0x5000);
    a.addr(30, addr::PAD_FRAME0);                                  
    a.addi(29, 30, int64_t(addr::PAD_PORT0 - addr::PAD_FRAME0));   
    a.lbz(0, 0, 30).stw(0, ctl::PAD_CONN, 31);

     
    a.lwz(0, ctl::PAD_ON, 31).cmpwi(0, 0).beq("pad_done");
     
     
    a.li(12, 0);
    a.addi(10, 29, pad::TRIGGER_COUNTER - 1);
    a.addi(11, 31, ctl::PAD_MASKS - 4);
    a.li(0, 16).mtctr(0);
    a.label("pad_loop");
    w(lbzu(9, 1, 10));
    a.lwzu(8, 4, 11);
    a.cmpwi(9, 0).beq("pad_next").or_(12, 12, 8);
    a.label("pad_next").bdnz("pad_loop");
    a.lwz(0, pad::STICK_LX, 29).stw(0, ctl::PAD_STICKS, 31);
    a.lwz(0, pad::STICK_RX, 29).stw(0, ctl::PAD_STICKS + 4, 31);
    a.lwz(9, ctl::PAD_HELD, 31).cmplw(9, 12).beq("pad_done");
    a.stw(12, ctl::PAD_HELD, 31);
    a.lwz(9, ctl::PAD_SEQ, 31);
    a.rlwinm(8, 9, 2, 26, 29);                                     
    a.addi(7, 31, ctl::PAD_RING);
    a.stwx(12, 7, 8);
    a.lwsync();                                                    
    a.addi(9, 9, 1).stw(9, ctl::PAD_SEQ, 31);
    a.label("pad_done");

     
    a.li(27, 0).li(25, int64_t(PlayerCheck::Off));
    a.lwz(0, ctl::FLY_ON, 31);
    for (uint32_t off : {ctl::OPK_GATHER, ctl::OPK_EDGE, ctl::P_PROBE, ctl::WARP_N}) a.lwz(9, off, 31).or_(0, 0, 9);
    a.cmpwi(0, 0).beq("player_done");
    a.li(25, int64_t(PlayerCheck::Identity));
    g.identity(26, "player_done");
    a.li(25, int64_t(PlayerCheck::Body));
    a.lwz(9, ctl::P_BODY, 31);
    g.heap(9, "player_done");
    a.andi_(0, 9, 0xF).bne("player_done");
    a.lwz(0, 0, 9).load32(11, addr::VT_RIGID_BODY).cmplw(0, 11).bne("player_done");
    a.lwz(10, ctl::P_PSLOT, 31);
    g.heap(10, "player_done");
    a.lwz(10, 0, 10);                                              
    g.heap(10, "player_done");
    a.lwz(0, chain::PHYSICS_BODY, 10).cmplw(0, 9).bne("player_done");
    a.mr(27, 9).li(25, int64_t(PlayerCheck::Ok));
    a.label("player_done").stw(25, ctl::P_STATUS, 31);

     
    a.lwz(9, ctl::WARP_N, 31).cmpwi(9, 0).beq("warp_done");
    a.cmpwi(27, 0).beq("warp_done");
    a.lwz(0, ctl::WARP_X, 31).stw(0, hk::BODY_COM1, 27);
    a.lwz(0, ctl::WARP_Y, 31).stw(0, hk::BODY_COM1 + 4, 27);
    a.lwz(0, ctl::WARP_Z, 31).stw(0, hk::BODY_COM1 + 8, 27);
    a.li(0, 0).stw(0, hk::BODY_LINVEL, 27).stw(0, hk::BODY_LINVEL + 4, 27).stw(0, hk::BODY_LINVEL + 8, 27);
    a.addi(9, 9, -1).stw(9, ctl::WARP_N, 31);
    a.label("warp_done");

     
    a.li(0, int64_t(FlyStatus::Off));
    a.lwz(9, ctl::FLY_ON, 31).cmpwi(9, 0).beq("fly_status");
    a.mr(0, 25);
    a.cmpwi(27, 0).beq("fly_status");
    a.li(0, int64_t(FlyStatus::NoController));
    a.lbz(9, 0, 30).cmpwi(9, 0).beq("fly_status");                
    a.li(0, 0).stw(0, hk::BODY_LINVEL, 27).stw(0, hk::BODY_LINVEL + 4, 27);
    a.lwz(0, ctl::FLY_HOVER, 31).stw(0, hk::BODY_LINVEL + 8, 27);    
    a.lwz(9, ctl::FLY_COUNT, 31).addi(9, 9, 1).stw(9, ctl::FLY_COUNT, 31);
    a.li(0, int64_t(FlyStatus::Flying));
    a.lwz(9, ctl::FLY_BRAKE, 31).cmpwi(9, 0).bne("fly_status");
    w(lha(9, pad::STICK_LX, 29));
    w(lha(10, pad::STICK_LY, 29));
    dead(9);
    dead(10);
    a.lbz(11, pad::BUTTON_COUNTER + int(Btn::Cross), 29);          
    a.lbz(12, pad::BUTTON_COUNTER + int(Btn::Circle), 29);         
    a.or_(3, 9, 10).or_(3, 3, 11).or_(3, 3, 12).cmpwi(3, 0).beq("fly_status");
    a.lfs(1, 0x110, 26).lfs(2, 0x114, 26).lfs(3, 0x118, 26);       
    a.lfs(12, hk::BODY_COM1, 27).lfs(13, hk::BODY_COM1 + 4, 27).lfs(0, hk::BODY_COM1 + 8, 27);    
     
    w(fsubs(4, 1, 1));
    for (int f : {2, 3, 12, 13, 0}) {
        w(fsubs(5, f, f));
        w(fadds(4, 4, 5));
    }
    w(fcmpu(0, 4, 4));
    a.bne("fly_status");
     
     
    a.li(0, int64_t(FlyStatus::BadForward));
    w(fmuls(4, 1, 1));
    w(fmadds(4, 2, 2, 4));
    w(fmadds(4, 3, 3, 4));
    a.lfs(5, ctl::FWD_MIN, 31);
    w(fcmpu(0, 4, 5));
    a.blt("fly_status");
    a.lfs(5, ctl::FWD_MAX, 31);
    w(fcmpu(0, 4, 5));
    a.bgt("fly_status");
    a.li(0, int64_t(FlyStatus::Flying));
    w(fmuls(4, 2, 2));
    w(fmadds(4, 1, 1, 4));
    w(fsqrts(4, 4));                                               
    w(fmr(5, 2));                                                  
    w(fneg(6, 1));                                                 
    a.lfs(7, ctl::EPS, 31);
    w(fcmpu(0, 4, 7));
    a.ble("no_norm");                                              
    w(fdivs(5, 5, 4));
    w(fdivs(6, 6, 4));
    a.label("no_norm");
    a.lfs(8, ctl::FLY_STEP, 31);                                   
    a.lwz(3, ctl::FLY_PREC, 31).cmpwi(3, 0).beq("no_prec");
    a.lbz(3, pad::BUTTON_COUNTER + int(Btn::L3), 29).cmpwi(3, 0).beq("no_prec");
    a.lfs(9, ctl::FLY_PFAC, 31);
    w(fmuls(8, 8, 9));
    a.label("no_prec");
    a.std_(9, ctl::TMP64, 31).lfd(9, ctl::TMP64, 31);             
    w(fcfid(9, 9));
    w(frsp(9, 9));
    a.std_(10, ctl::TMP64, 31).lfd(10, ctl::TMP64, 31);           
    w(fcfid(10, 10));
    w(frsp(10, 10));
    a.lfs(11, ctl::FLY_SCALE, 31);
    w(fmuls(9, 9, 11));
    w(fmuls(10, 10, 11));
    w(fmuls(4, 10, 1));                                            
    w(fmadds(4, 9, 5, 4));
    w(fmuls(7, 10, 2));                                            
    w(fmadds(7, 9, 6, 7));
    w(fmuls(3, 10, 3));                                            
    w(fmadds(12, 4, 8, 12));                                       
    w(fmadds(13, 7, 8, 13));
    w(fmadds(0, 3, 8, 0));
    a.cmpwi(11, 0).beq("no_up");
    w(fadds(0, 0, 8));
    a.label("no_up").cmpwi(12, 0).beq("no_down");
    w(fsubs(0, 0, 8));
    a.label("no_down");
    a.stfs(12, hk::BODY_COM1, 27).stfs(13, hk::BODY_COM1 + 4, 27).stfs(0, hk::BODY_COM1 + 8, 27);
    a.lwz(3, ctl::FLY_MOVES, 31).addi(3, 3, 1).stw(3, ctl::FLY_MOVES, 31);
    a.label("fly_status").stw(0, ctl::FLY_STATUS, 31);

    a.label("exit");
    ppc::epilogue(a, frame, save);
    a.blr();
    return a.link();
}

std::vector<uint8_t> opkProgram(uint32_t base) {
    Asm a(base);
    Gen g{a, 21};
    using namespace enc;
    auto w = [&a](uint32_t word) { a.word(word); };
     
     
     
     
    const auto save = regs(14);
    constexpr int64_t frame = 0x110;
    ppc::prologue(a, frame, save);
    a.mr(31, 3);
    a.lwz(0, ctl::MAGIC, 31).load32(11, kMagic).cmplw(0, 11).bne("exit");
    a.lis(21, 0x5000);
    a.li(0, 0).stw(0, ctl::OPK_MOVED, 31).stw(0, ctl::OPK_SEEN, 31);
    a.li(0, int64_t(OpkStatus::Off));
    a.lwz(28, ctl::OPK_GATHER, 31).lwz(25, ctl::OPK_EDGE, 31);
    a.or_(3, 28, 25).cmpwi(3, 0).beq("opk_status");
    a.lwz(0, ctl::P_STATUS, 31).cmpwi(0, 0).bne("opk_status");    
    a.li(0, int64_t(OpkStatus::Identity));
    g.identity(26, "opk_status");                                  
    a.lwz(27, ctl::P_BODY, 31);
    a.li(0, int64_t(OpkStatus::NoWorld));
    a.addr(3, addr::HKP_WORLD_PTR).lwz(24, 0, 3);
    g.heap(24, "opk_status");
    a.lwz(3, 0, 24).load32(4, addr::VT_HKP_WORLD).cmplw(3, 4).bne("opk_status");
    a.cmpwi(28, 0).beq("gather_ready");
    for (uint32_t off : {0xE0u, 0xE4u, 0xE8u, 0x110u, 0x114u, 0x118u}) {    
        a.lwz(3, off, 26);
        g.finite(3, "gather_off");
    }
    a.lwz(3, 0xE0, 26).lwz(5, 0xE4, 26).or_(3, 3, 5).lwz(5, 0xE8, 26).or_(3, 3, 5);
    w(rlwinmDot(3, 3, 0, 1, 31));                                  
    a.beq("gather_off");
    a.lfs(1, 0xE0, 26).lfs(2, 0xE4, 26).lfs(3, 0xE8, 26).lfs(4, 0x110, 26).lfs(5, 0x114, 26);
    w(fmuls(6, 5, 5));
    w(fmadds(6, 4, 4, 6));
    w(fsqrts(6, 6));
    a.stfs(6, ctl::TMP64, 31).lwz(3, ctl::TMP64, 31);
    g.finite(3, "gather_off");                                     
    a.lfs(7, ctl::OPK_MIN, 31);
    w(fcmpu(0, 6, 7));
    a.blt("gather_off");                                           
    a.lfs(8, ctl::OPK_DIST, 31);
    w(fdivs(4, 4, 6));
    w(fdivs(5, 5, 6));
    w(fmadds(1, 4, 8, 1));                                         
    w(fmadds(2, 5, 8, 2));
    a.lfs(9, ctl::OPK_LIFT, 31);
    w(fadds(3, 3, 9));                                             
    a.stfs(1, ctl::OPK_TX, 31).stfs(2, ctl::OPK_TY, 31).stfs(3, ctl::OPK_TZ, 31);
    a.lwz(10, ctl::OPK_TX, 31).lwz(11, ctl::OPK_TY, 31).lwz(12, ctl::OPK_TZ, 31);
    a.b("gather_ready");
    a.label("gather_off").li(28, 0);
    a.label("gather_ready");
    a.li(0, int64_t(OpkStatus::NoTarget));
    a.or_(3, 28, 25).cmpwi(3, 0).beq("opk_status");
    a.load32(9, addr::VT_RIGID_BODY).load32(29, addr::VT_ISLAND).lwz(30, ctl::OPK_EDGEZ, 31);
    a.li(15, 0).li(14, 0).li(23, hk::WORLD_ISLANDS_ACTIVE);
    a.label("k_loop");
    a.lwzx(22, 24, 23);                                            
    a.addi(3, 23, 4).lwzx(20, 24, 3);                              
    a.cmpwi(20, 0).beq("k_next").cmplwi(20, hk::MAX_ISLANDS).bgt("k_next");
    g.heap(22, "k_next");
    a.li(19, 0);
    a.label("i_loop");
    a.slwi(3, 19, 2).lwzx(3, 22, 3);                               
    g.heap(3, "i_next");
    a.lwz(4, 0, 3).cmplw(4, 29).bne("i_next");                     
    a.lwz(18, hk::ISLAND_ENTITIES, 3).lwz(17, hk::ISLAND_ENTITY_COUNT, 3);
    a.cmpwi(17, 0).beq("i_next").cmplwi(17, hk::MAX_ENTITIES).bgt("i_next");
    g.heap(18, "i_next");
    a.li(16, 0);
    a.label("j_loop");
    a.slwi(3, 16, 2).lwzx(3, 18, 3);                               
    a.cmpwi(3, 0).beq("j_next").cmplw(3, 27).beq("j_next");        
    g.heap(3, "j_next");
    a.andi_(0, 3, 0xF).bne("j_next");
    a.lwz(4, 0, 3).cmplw(4, 9).bne("j_next");                      
    a.lwz(5, hk::BODY_COM1, 3).lwz(6, hk::BODY_COM1 + 4, 3).lwz(7, hk::BODY_COM1 + 8, 3);
    g.finite(5, "j_next");
    g.finite(6, "j_next");
    g.finite(7, "j_next");
    a.or_(4, 5, 6).or_(4, 4, 7);
    w(rlwinmDot(4, 4, 0, 1, 31));
    a.beq("j_next");                                               
    a.addi(14, 14, 1);
    a.li(8, 0);
    a.cmpwi(28, 0).beq("no_gather");
    a.stw(10, hk::BODY_COM1, 3).stw(11, hk::BODY_COM1 + 4, 3).stw(12, hk::BODY_COM1 + 8, 3).li(8, 1);
    a.label("no_gather");
    a.cmpwi(25, 0).beq("no_edge");
    a.cmpwi(28, 0).bne("do_edge");                                 
    a.cmplw(7, 30).beq("no_edge");                                 
    a.label("do_edge").stw(30, hk::BODY_COM1 + 8, 3).li(8, 1);
    a.label("no_edge").add(15, 15, 8);
    a.label("j_next").addi(16, 16, 1).cmplw(16, 17).blt("j_loop");
    a.label("i_next").addi(19, 19, 1).cmplw(19, 20).blt("i_loop");
    a.label("k_next").cmpwi(23, hk::WORLD_ISLANDS_INACTIVE).beq("k_done");
    a.li(23, hk::WORLD_ISLANDS_INACTIVE).b("k_loop");
    a.label("k_done").stw(15, ctl::OPK_MOVED, 31).stw(14, ctl::OPK_SEEN, 31);
    a.li(0, int64_t(OpkStatus::Ran));
    a.label("opk_status").stw(0, ctl::OPK_STATUS, 31);

    a.label("exit");
    ppc::epilogue(a, frame, save);
    a.blr();
    return a.link();
}

uint32_t tickProgramSize() {
    static const uint32_t size = [] { return (uint32_t(tickProgram(0x183E400).size()) + 15u) & ~15u; }();
    return size;
}

uint32_t opkProgramSize() {
    static const uint32_t size = [] { return (uint32_t(opkProgram(0x183E400).size()) + 15u) & ~15u; }();
    return size;
}

}   
