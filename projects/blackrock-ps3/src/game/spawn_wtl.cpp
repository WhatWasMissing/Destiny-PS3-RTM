#include "game/spawn_wtl.h"

#include <cstring>

#include "core/error.h"
#include "core/ppc.h"
#include "core/profile.h"
#include "core/runtime.h"
#include "game/world_mem.h"
#include "game/world_tags.h"

namespace brps3::spawn {

namespace {

using ppc::Asm;

constexpr int64_t FRAME = 0x100;

uint32_t align16(uint32_t v) { return (v + 15u) & ~15u; }

void put32(std::vector<uint8_t>& b, uint32_t off, uint32_t v) {
    b[off] = uint8_t(v >> 24);
    b[off + 1] = uint8_t(v >> 16);
    b[off + 2] = uint8_t(v >> 8);
    b[off + 3] = uint8_t(v);
}

uint32_t get32(const std::vector<uint8_t>& b, uint32_t off) {
    return uint32_t(b[off]) << 24 | uint32_t(b[off + 1]) << 16 | uint32_t(b[off + 2]) << 8 | b[off + 3];
}

 
 
 
void emitChk(Asm& a) {
    a.label("chk").clrldi(12, 12, 32).andDot(0, 12, 11).bne("chk_bad");
    a.addis(0, 12, -0x3000).lis(11, 0x5000).cmplw(0, 11).bltlr();
    a.addis(0, 12, -0x1).lis(11, 0x1C7).cmplw(0, 11).bltlr();
    a.label("chk_bad").cmpw(0, 0).blr();
}

 
void callNative(Asm& a, uint32_t code, int64_t tocOff) {
    a.lwz(2, tocOff, 31).load32(12, code).mtctr(12).bctrl();
}

 
void chkOr(Asm& a, int mask, const char* fail) { a.li(11, mask).bl("chk").beq(fail); }

 
 
 
 
void emitTagHandleValid(Asm& a, const char* fail) {
    a.addr(12, world::addr::TAG_TABLE_PTR).lwz(12, 0, 12);
    chkOr(a, 3, fail);
    a.mr(6, 12).lwz(7, 0, 6).lwz(8, 8, 6);                        
    a.srwi(4, 5, 13).cmpwi(5, 0).blt("tag_neg");
    a.andi_(4, 4, 0x3FF).b("tag_page");
    a.label("tag_neg").andi_(4, 4, 0x3FFF);
    a.label("tag_page").slwi(4, 4, 6).add(12, 7, 4);              
    a.addi(0, 12, 0x40).cmplw(0, 8).bgt(fail);                    
    chkOr(a, 3, fail);
    a.mr(6, 12);
    a.lwz(0, 0x10, 6).cmplwi(0, 0).beq(fail);                     
    a.lwz(0, 0x30, 6).cmplwi(0, 0x10).bne(fail);                  
    a.lwz(12, 0x18, 6).addi(12, 12, 0x10);                        
    chkOr(a, 3, fail);
    a.lwz(0, 0, 12).andi_(4, 5, 0x1FFF).cmplw(4, 0).bge(fail);
}

}   

 

namespace wtl {

const Fingerprints kFingerprints = {
    {GET_GLOBALS, "3C6001A28063DCE04E800020F821FF91"},
    {CREATE, "7C0802A648C468B5F821FF81F8010090"},
    {ADD_TAG, "7C0802A648C476B9F821FF71F80100A0"},
    {RELEASE, "7C0802A648C4663DF821FF41F80100D0"},
    {SUBMIT, "7C0802A648C46835F821FF61F80100B0"},
    {WAIT, "7C0802A648C47DB1F821FF81F8010090"},
};

const char* worldStateName(uint32_t s) {
    static const char* const names[] = {"pregame",
                                        "bootflow:media_check",
                                        "bootflow:bootload",
                                        "bootflow:intro_screen_display",
                                        "bootflow:start",
                                        "bootflow:platform_signin",
                                        "bootflow:platform_account_warnings",
                                        "bootflow:enumerate_content_rights",
                                        "bootflow:account_signin",
                                        "bootflow:eula",
                                        "bootflow:upsell",
                                        "bootflow:email",
                                        "bootflow:confirm_account_transfer",
                                        "bootflow:content_check",
                                        "bootflow:content_install",
                                        "bootflow:insufficient_content_space",
                                        "bootflow:illegal_dlc_media",
                                        "bootflow:queuing",
                                        "bootflow:package_registration",
                                        "bootflow:bap_signin",
                                        "bootflow:investment_signin",
                                        "bootflow:profile_setup",
                                        "bootflow:prepare_for_orbit",
                                        "character:signin",
                                        "character:prologue",
                                        "cleanup",
                                        "setup:orbit_loading",
                                        "setup:orbit",
                                        "setup:activity_session_creation",
                                        "setup:matchmaking",
                                        "setup:activity_host_setup",
                                        "setup:prologue_intro_loading",
                                        "setup:orbit_outro",
                                        "setup:activity_world_transition",
                                        "activity:initial_slice_set_loading",
                                        "activity:physics_join",
                                        "activity:in_world",
                                        "activity:watch_video"};
    return s < sizeof(names) / sizeof(names[0]) ? names[s] : "?";
}

const char* triggerName(Trigger t) {
    switch (t) {
    case Trigger::Now: return "now";
    case Trigger::LowObjects: return "few live objects";
    case Trigger::LoaderBusy: return "loader busy";
    case Trigger::WorldState: return "world state";
    case Trigger::Never: return "observe only";
    }
    return "?";
}

const char* statusName(Status s) {
    switch (s) {
    case Status::Pending: return "pending";
    case Status::Fired: return "fired";
    case Status::Waiting: return "waiting";
    case Status::BadTag: return "tag not valid here";
    case Status::NoGlobals: return "loader not set up";
    case Status::CreateFailed: return "request not created";
    case Status::BadRequest: return "request address not plausible";
    case Status::BadBlock: return "bad block";
    case Status::Disarmed: return "disarmed";
    }
    return "?";
}

std::vector<uint8_t> stubProgram(uint32_t base) {
     
    Asm a(base);
    auto chk = [&](int mask, const char* fail) { a.li(11, mask).bl("chk").beq(fail); };
    const std::vector<int> save = {2, 28, 29, 30, 31};
    ppc::prologue(a, FRAME, save);
    a.mr(12, 3);
    chk(15, "ret_bad");
    a.mr(31, 12);
    a.lwz(0, 0, 31).load32(12, kMagic).cmplw(0, 12).bne("ret_bad");
    a.lwz(12, kOffFrames, 31).addi(12, 12, 1).stw(12, kOffFrames, 31);

     
    a.li(28, -1);
    a.addr(12, WORLD_CONTROLLER).lwz(12, 0, 12).addi(12, 12, WORLD_STATE_OFFSET);
    chk(3, "obs_state");
    a.lwz(28, 0, 12);
    a.label("obs_state").stw(28, kOffLastState, 31);
    a.cmplwi(28, STATE_WORLD_TRANSITION).beq("in_transition").cmplwi(28, STATE_SLICE_LOADING).bne("obs_rest");
    a.label("in_transition").lwz(12, kOffTransitionFrames, 31).addi(12, 12, 1).stw(12, kOffTransitionFrames, 31);
    a.label("obs_rest");
    a.addr(12, DRAIN_CURSOR).lhz(0, 0, 12).stw(0, kOffLastCursor, 31);
    a.addr(12, SOBJECT_COUNT).lwz(0, 0, 12).stw(0, kOffLastCount, 31);

     
    a.lwz(9, kOffFlags, 31).andi_(0, 9, kFlagArmed).beq("ret_disarmed");
    a.lwz(0, kOffLastCount, 31).lwz(12, kOffMinCount, 31).cmplw(0, 12).bge("trigger").stw(0, kOffMinCount, 31);

     
    a.label("trigger").lwz(9, kOffTrigger, 31).lwz(10, kOffValue, 31);
    a.cmplwi(9, uint32_t(Trigger::Now)).beq("fire");
    a.cmplwi(9, uint32_t(Trigger::LowObjects)).bne("t_busy");
    a.lwz(0, kOffLastCount, 31).cmplw(0, 10).bgt("ret_waiting").b("fire");
    a.label("t_busy").cmplwi(9, uint32_t(Trigger::LoaderBusy)).bne("t_state");
    a.lwz(0, kOffLastCursor, 31).cmplwi(0, 0xFFFF).beq("ret_waiting").b("fire");
    a.label("t_state").cmplwi(9, uint32_t(Trigger::WorldState)).bne("t_never");
    a.lwz(0, kOffLastState, 31).cmplw(0, 10).bne("ret_waiting").b("fire");
    a.label("t_never").cmplwi(9, uint32_t(Trigger::Never)).beq("ret_waiting").b("ret_bad");

     
    a.label("fire").lwz(5, kOffTag, 31);
    emitTagHandleValid(a, "bad_tag");

     
    callNative(a, GET_GLOBALS, kOffToc);
    a.clrldi(30, 3, 32).stw(30, kOffGlobals, 31).cmplwi(30, 0).beq("no_globals");

     
    a.li(0, -1).stw(0, kOffOut, 31);
    a.addi(3, 31, kOffOut).mr(4, 30).li(5, 5).li(6, 1).li(7, 0);
    callNative(a, CREATE, kOffToc);
    a.lwz(29, kOffOut, 31).stw(29, kOffHandle, 31);
    a.cmpwi(29, -1).beq("create_failed").cmpwi(29, 0).beq("create_failed");

     
    a.addr(12, DET_POOL).lwz(8, 8, 12).lwz(9, 0xC, 12).lwz(10, 0x10, 12);
    a.andi_(4, 29, 0x1FFF).cmplw(4, 10).bge("bad_request");
    a.cmplwi(9, 0).beq("bad_request");
    a.mullw(4, 4, 9).add(12, 8, 4);
    chk(3, "bad_request");
    a.mr(28, 12).stw(28, kOffRequest, 31);

     
    a.lwz(0, kOffKind, 31).stw(0, kOffPair, 31).lwz(0, kOffTag, 31).stw(0, kOffPair + 4, 31);
    a.mr(3, 28).addi(4, 31, kOffPair);
    callNative(a, ADD_TAG, kOffToc);
    a.mr(3, 30).mr(4, 29);
    callNative(a, RELEASE, kOffToc);
    a.mr(3, 30).mr(4, 29);
    callNative(a, SUBMIT, kOffToc);
    a.lwz(9, kOffFlags, 31).andi_(0, 9, kFlagWait).beq("fired");
    a.mr(3, 30);
    callNative(a, WAIT, kOffToc);
    a.label("fired").lwz(12, kOffFired, 31).addi(12, 12, 1).stw(12, kOffFired, 31);
    a.li(3, int64_t(Status::Fired)).b("finish");

     
    a.label("bad_tag").li(3, int64_t(Status::BadTag)).b("not_ready");
    a.label("no_globals").li(3, int64_t(Status::NoGlobals));
    a.label("not_ready").lwz(9, kOffTrigger, 31).cmplwi(9, uint32_t(Trigger::Now)).bne("ret_waiting").b("finish");
    a.label("create_failed").li(3, int64_t(Status::CreateFailed)).b("finish");
    a.label("bad_request").li(3, int64_t(Status::BadRequest));

     
    a.label("finish").lwz(9, kOffFlags, 31).andi_(0, 9, kFlagOnce).beq("store");
    a.andi_(9, 9, 0xFFFF & ~kFlagArmed).stw(9, kOffFlags, 31);
    a.label("store").lwsync().stw(3, kOffStatus, 31).b("out");
    a.label("ret_waiting").li(3, int64_t(Status::Waiting)).stw(3, kOffStatus, 31).b("out");
    a.label("ret_disarmed").li(3, int64_t(Status::Disarmed)).b("out");    
    a.label("ret_bad").li(3, int64_t(Status::BadBlock));
    a.label("out");
    ppc::epilogue(a, FRAME, save);
    a.blr();
    emitChk(a);
    return a.link();
}

uint32_t stubAllocSize() {
    static const uint32_t size = align16(uint32_t(stubProgram(profile::CAVE).size()));
    return size;
}

uint32_t installStub(Runtime& rt) {
    return rt.sharedCode("spawn.wtl.v1", [](uint32_t base) { return stubProgram(base); }, stubAllocSize());
}

void validate(const Params& p) {
    if (!world::validHandle(p.tag) || !world::isPackagePage(world::tagPage(p.tag)))
        throw Error("world tag load: " + world::hex8(p.tag) + " is not a content tag");
    if (p.kind != Kind::OnDemand && p.kind != Kind::Manifest) throw Error("world tag load: kind must be 1 (on-demand) or 2 (manifest)");
    if (uint32_t(p.trigger) > uint32_t(Trigger::Never)) throw Error("world tag load: unknown trigger");
    if (p.trigger == Trigger::WorldState && p.value > 64) throw Error("world tag load: world state out of range");
}

std::vector<uint8_t> encodeBlock(const Params& p, bool armed) {
    if (p.trigger != Trigger::Never) validate(p);
    std::vector<uint8_t> b(kBlockSize, 0);
    put32(b, 0, kMagic);
    put32(b, kOffFlags, (armed ? kFlagArmed : 0) | (p.wait ? kFlagWait : 0) | (p.once ? kFlagOnce : 0));
    put32(b, kOffStatus, uint32_t(Status::Pending));
    put32(b, kOffTag, p.tag);
    put32(b, kOffKind, uint32_t(p.kind));
    put32(b, kOffTrigger, uint32_t(p.trigger));
    put32(b, kOffValue, p.value);
    put32(b, kOffLastState, 0xFFFFFFFFu);
    put32(b, kOffLastCursor, 0xFFFFFFFFu);
    put32(b, kOffLastCount, 0xFFFFFFFFu);
    put32(b, kOffMinCount, 0xFFFFFFFFu);
    put32(b, kOffHandle, 0xFFFFFFFFu);
    put32(b, kOffToc, profile::TOC);
    put32(b, kOffOut, 0xFFFFFFFFu);
    return b;
}

State decodeBlock(const std::vector<uint8_t>& b) {
    if (b.size() < kBlockSize) throw Error("world tag load block read back short");
    if (get32(b, 0) != kMagic) throw Error("world tag load block was overwritten");
    State s;
    const uint32_t st = get32(b, kOffStatus);
    s.status = st <= uint32_t(Status::Disarmed) ? Status(st) : Status::BadBlock;
    s.armed = (get32(b, kOffFlags) & kFlagArmed) != 0;
    s.frames = get32(b, kOffFrames);
    s.transitionFrames = get32(b, kOffTransitionFrames);
    s.fired = get32(b, kOffFired);
    s.handle = get32(b, kOffHandle);
    s.globals = get32(b, kOffGlobals);
    s.request = get32(b, kOffRequest);
    s.lastState = get32(b, kOffLastState);
    s.lastCursor = get32(b, kOffLastCursor);
    s.lastCount = get32(b, kOffLastCount);
    s.minCount = get32(b, kOffMinCount);
    s.tag = get32(b, kOffTag);
    s.trigger = Trigger(get32(b, kOffTrigger));
    s.value = get32(b, kOffValue);
    return s;
}

State loadNow(Runtime& rt, const Params& in, int timeoutMs) {
    Params p = in;
    p.trigger = Trigger::Now;
    p.once = true;
    validate(p);
    const uint32_t stub = installStub(rt);
    const uint32_t block = rt.alloc(kBlockSize);
    State s;
    try {
        const auto b = encodeBlock(p, true);
        rt.write(block, b.data(), b.size(), true);
        rt.call(stub, {block}, {}, timeoutMs);
        s = decodeBlock(rt.read(block, kBlockSize));
    } catch (...) {
         
        try {
            const uint8_t zero[4] = {0, 0, 0, 0};
            rt.write(block + kOffFlags, zero, 4, false);
        } catch (const std::exception&) {
        }
        rt.freeLater(block);
        throw;
    }
    try {
        rt.free(block);
    } catch (const std::exception&) {
        rt.freeLater(block);
    }
    return s;
}

void AutoLoad::start(Runtime& rt, const Params& p) {
    if (p.trigger == Trigger::Now) throw Error("auto-load needs a trigger (use Load now for an immediate load)");
    if (p.trigger != Trigger::Never) validate(p);
    stop(rt);
    const uint32_t stub = installStub(rt);
    const uint32_t block = rt.alloc(kBlockSize);
    try {
        const auto b = encodeBlock(p, p.trigger != Trigger::Never);
        rt.write(block, b.data(), b.size(), true);
        rt.addTaskAt(kTask, stub, block);
    } catch (...) {
        rt.freeLater(block);
        throw;
    }
    block_ = block;
    params_ = p;
}

void AutoLoad::stop(Runtime& rt) {
    try {
        rt.removeTask(kTask);
    } catch (const std::exception&) {
        if (rt.hasTask(kTask)) throw;                            
    }
    if (block_) rt.freeLater(block_);
    block_ = 0;
}

bool AutoLoad::running(Runtime& rt) const { return block_ && rt.hasTask(kTask); }

State AutoLoad::poll(Runtime& rt) const {
    if (!block_) throw Error("auto-load is not running");
    return decodeBlock(rt.read(block_, kBlockSize));
}

}   

 

namespace rsc {

const std::vector<Poster>& posters() {
    static const std::vector<Poster> p = {
        {1, "load_manifest", 0xE570C, "7C0802A648CB3309F821FF71F80100A0", true},
        {2, "desire_loaded_manifest", 0xE5798, "7C0802A648CB3281F821FF71F80100A0", false},
        {3, "unload_manifest", 0xE5820, "7C0802A648CB31F9F821FF71F80100A0", false},
        {4, "load_on_demand_tag", 0xE5400, "7C0802A648CB3615F821FF71F80100A0", true},
        {5, "desire_loaded_on_demand_tag", 0xE548C, "7C0802A648CB358DF821FF71F80100A0", false},
        {6, "unload_on_demand_tag", 0xE5510, "7C0802A648CB3509F821FF71F80100A0", false},
    };
    return p;
}

const Poster* findPoster(uint32_t event) {
    for (const Poster& p : posters())
        if (p.event == event) return &p;
    return nullptr;
}

const Fingerprints kFingerprints = {
    {BEGIN, "7C0802A648CB317DF821FF71F80100A0"},
    {END, "7C0802A648CB3001F821FF51F80100C0"},
    {RELEASE, "7C0802A648CB780DF821FF71F80100A0"},
};

const char* statusName(Status s) {
    switch (s) {
    case Status::Pending: return "pending";
    case Status::Done: return "posted";
    case Status::BeginFailed: return "begin returned no request";
    case Status::BadBlock: return "bad block";
    case Status::Disarmed: return "disarmed";
    case Status::BadTag: return "tag not valid here (page not registered or index past its limit)";
    }
    return "?";
}

std::vector<uint8_t> stubProgram(uint32_t base) {
    Asm a(base);
    auto chk = [&](int mask, const char* fail) { a.li(11, mask).bl("chk").beq(fail); };
    const std::vector<int> save = {2, 29, 31};
    ppc::prologue(a, FRAME, save);
    a.mr(12, 3);
    chk(15, "ret_bad");
    a.mr(31, 12);
    a.lwz(0, 0, 31).load32(12, kMagic).cmplw(0, 12).bne("ret_bad");
    a.lwz(9, kOffFlags, 31).andi_(0, 9, kFlagArmed).beq("ret_disarmed");
    a.li(0, 0).stw(0, kOffFlags, 31);                            
    a.lwz(12, kOffPoster, 31);
    chk(3, "ret_bad");
     
    a.lwz(5, kOffTag, 31);
    emitTagHandleValid(a, "bad_tag");
    a.andi_(0, 9, kFlagRaw).beq("txn");
     
    a.lwz(29, kOffCategory, 31).andi_(29, 29, 0xFFFF).stw(29, kOffRequest, 31);
    a.lwz(3, kOffTag, 31).mr(4, 29).lwz(2, kOffToc, 31).lwz(12, kOffPoster, 31).mtctr(12).bctrl();
    a.stw(3, kOffEndR3, 31);
    a.lwz(12, kOffCalls, 31).addi(12, 12, 1).stw(12, kOffCalls, 31);
    a.li(3, int64_t(Status::Done)).b("store");
     
    a.label("txn").lwz(3, kOffCategory, 31);
    callNative(a, BEGIN, kOffToc);
    a.andi_(29, 3, 0xFFFF).stw(29, kOffRequest, 31);
    a.cmplwi(29, 0xFFFF).beq("begin_failed");
     
    a.lwz(3, kOffTag, 31).mr(4, 29).lwz(2, kOffToc, 31).lwz(12, kOffPoster, 31).mtctr(12).bctrl();
     
    a.mr(3, 29);
    callNative(a, END, kOffToc);
    a.stw(3, kOffEndR3, 31);
    a.lwz(12, kOffCalls, 31).addi(12, 12, 1).stw(12, kOffCalls, 31);
    a.li(3, int64_t(Status::Done)).b("store");
    a.label("bad_tag").li(3, int64_t(Status::BadTag)).b("store");
    a.label("begin_failed").li(3, int64_t(Status::BeginFailed));
    a.label("store").lwsync().stw(3, kOffStatus, 31).b("out");
    a.label("ret_disarmed").li(3, int64_t(Status::Disarmed)).b("out");
    a.label("ret_bad").li(3, int64_t(Status::BadBlock));
    a.label("out");
    ppc::epilogue(a, FRAME, save);
    a.blr();
    emitChk(a);
    return a.link();
}

uint32_t stubAllocSize() {
    static const uint32_t size = align16(uint32_t(stubProgram(profile::CAVE).size()));
    return size;
}

uint32_t installStub(Runtime& rt) {
    return rt.sharedCode("spawn.rsc.v2", [](uint32_t base) { return stubProgram(base); }, stubAllocSize());
}

std::vector<uint8_t> encodeBlock(uint32_t poster, uint32_t tag, uint32_t category, bool raw) {
    bool known = false;
    for (const Poster& p : posters()) known = known || p.addr == poster;
    if (!known) throw Error("resourcerer: " + world::hex8(poster) + " is not one of the six event posters");
    std::vector<uint8_t> b(kBlockSize, 0);
    put32(b, 0, kMagic);
    put32(b, kOffFlags, kFlagArmed | (raw ? kFlagRaw : 0));
    put32(b, kOffStatus, uint32_t(Status::Pending));
    put32(b, kOffPoster, poster);
    put32(b, kOffTag, tag);
    put32(b, kOffCategory, category);
    put32(b, kOffRequest, 0xFFFF);
    put32(b, kOffToc, profile::TOC);
    return b;
}

Result decodeBlock(const std::vector<uint8_t>& b) {
    if (b.size() < kBlockSize) throw Error("resourcerer block read back short");
    if (get32(b, 0) != kMagic) throw Error("resourcerer block was overwritten");
    Result r;
    const uint32_t st = get32(b, kOffStatus);
    r.status = st <= uint32_t(Status::BadTag) ? Status(st) : Status::BadBlock;
    r.request = get32(b, kOffRequest);
    r.endR3 = get32(b, kOffEndR3);
    return r;
}

Result run(Runtime& rt, uint32_t event, uint32_t tag, uint32_t category, int timeoutMs, bool raw) {
    const Poster* p = findPoster(event);
    if (!p) throw Error("resourcerer: unknown event type " + std::to_string(event) + " (1..6)");
    if (!world::validHandle(tag)) throw Error("resourcerer: " + world::hex8(tag) + " is not a tag");
    const uint32_t stub = installStub(rt);
    const uint32_t block = rt.alloc(kBlockSize);
    Result r;
    try {
        const auto b = encodeBlock(p->addr, tag, category, raw);
        rt.write(block, b.data(), b.size(), true);
        rt.call(stub, {block}, {}, timeoutMs);
        r = decodeBlock(rt.read(block, kBlockSize));
    } catch (...) {
        try {
            const uint8_t zero[4] = {0, 0, 0, 0};
            rt.write(block + kOffFlags, zero, 4, false);
        } catch (const std::exception&) {
        }
        rt.freeLater(block);
        throw;
    }
    try {
        rt.free(block);
    } catch (const std::exception&) {
        rt.freeLater(block);
    }
    return r;
}

}   

}   
