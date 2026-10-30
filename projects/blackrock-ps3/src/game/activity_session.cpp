#include "game/activity_session.h"

#include <cstdio>

#include "core/error.h"
#include "core/ppc.h"
#include "game/activity_addr.h"

namespace brps3::activity {

const std::vector<LobbyField> kLobbyFields = {
    {0x01, "active-join-controls", 0xF0},
    {0x02, "atomic-cascade-join-data", 0x1D0},
    {0x03, "activity-host", 0x338},
    {0x04, "orbit", 0x450},
    {0x05, "desired-activity", 0x530},
    {0x06, "desired-roster-join-activity", 0x690},
    {0x07, "activity-start-status", 0x768},
    {0x08, "activity", 0x858},
    {0x09, "user-join-controls", 0x9D0},
    {0x0A, "desired-join-controls", 0xAA8},
    {0x0B, "language", 0xB88},
    {0x0C, "countdown-timer", 0xC68},
    {0x0D, "requested-remote-join-data", 0xD78},
    {0x0E, "remote-join-data", 0xF10},
    {0x0F, "remote-join-result", 0x10A8},
    {0x10, "matchmaking-host-selected", 0x11A0},
    {0x11, "matchmaking-messaging", 0x1278},
    {0x12, "matchmaking-abort-requested", 0x1368},
    {0x13, "session-disband-reason", 0x1440},
    {0x14, "matchmaking-progress", 0x1520},
    {0x15, "initial-slice-set-status", 0x1618},
};

const LobbyField* lobbyField(int index) {
    for (const LobbyField& f : kLobbyFields)
        if (f.index == index) return &f;
    return nullptr;
}

const FieldValue* SessionSlot::field(int fieldIndex) const {
    for (const FieldValue& f : fields)
        if (f.index == fieldIndex) return &f;
    return nullptr;
}

SessionSlot parseSessionSlot(int slot, uint32_t slotBase, uint32_t subIndex, const world::Bytes& g) {
    SessionSlot s;
    s.slot = slot;
    s.slotBase = slotBase;
    s.index = subIndex;
    s.sub = slotBase + subIndex * addr::SUB_STRIDE + addr::SUB_BASE;
    s.goal = s.sub + addr::GOAL_OFFSET;
    s.gate = g.u32(addr::GATE_OFFSET - addr::GOAL_OFFSET);
    for (const LobbyField& f : kLobbyFields) {
        FieldValue v;
        v.index = f.index;
        v.addr = s.goal + f.offset;
        v.has = (g.u8(f.offset + addr::FIELD_HAS_VALUE) & 1) != 0;
        v.value = g.slice(f.offset + addr::FIELD_VALUE, kFieldValueBytes).data();
        s.fields.push_back(std::move(v));
    }
    s.ok = true;
    return s;
}

SessionRead readSessionFields(world::Reader& mem) {
    SessionRead r;
    r.root = mem.u32(addr::SESSION_ROOT_PTR);
    if (!world::mapped(r.root, addr::SLOT_STRIDE * 2)) {
        r.why = "session root " + world::hex8(r.root) + " is not set (title still booting?)";
        return r;
    }
     
    if (mem.bytes(r.root, 1).u8(0) == 0) {
        r.why = "session root " + world::hex8(r.root) + " is not set up yet (first byte 0)";
        return r;
    }
    for (int slot = 0; slot < 2; ++slot) {
        SessionSlot& s = r.slots[slot];
        s.slot = slot;
        s.slotBase = r.root + uint32_t(slot) * addr::SLOT_STRIDE;
        const uint32_t index = mem.u32(s.slotBase + 8);
        if (index > 1) {
            s.why = "slot " + std::to_string(slot) + " sub index " + std::to_string(index) + " is out of range";
            continue;
        }
        const uint32_t sub = s.slotBase + index * addr::SUB_STRIDE + addr::SUB_BASE;
        auto g = mem.tryBytes(sub + addr::GOAL_OFFSET, kGoalReadSize);
        if (!g) {
            s.why = "goal data at " + world::hex8(sub + addr::GOAL_OFFSET) + " is unreadable";
            continue;
        }
        s = parseSessionSlot(slot, s.slotBase, index, *g);
        if (!s.active()) s.why = "slot " + std::to_string(slot) + ": no goal data (gate " + std::to_string(s.gate) + ", the game uses 4..10)";
    }
    r.ok = r.slots[0].ok || r.slots[1].ok;
    if (!r.ok) r.why = r.slots[0].why;
    return r;
}

namespace {

uint32_t beWordAt(const std::vector<uint8_t>& v, size_t off) {
    if (off + 4 > v.size()) return 0;
    return uint32_t(v[off]) << 24 | uint32_t(v[off + 1]) << 16 | uint32_t(v[off + 2]) << 8 | v[off + 3];
}

std::string hx(uint32_t v) {
    char b[12];
    std::snprintf(b, sizeof b, "%08X", v);
    return b;
}

}   

namespace {

 
 
 
std::string describeHost(const uint32_t w[7]) {
    const bool gave = (w[0] | w[1] | w[4]) != 0;
    return "host id " + hx(w[0]) + hx(w[1]) + ", addr " + hx(w[4]) + ", +8 " + hx(w[2]) + hx(w[3]) + ", +14 " + hx(w[5]) + ", +18 " +
           hx(w[6] >> 16).substr(4) + (gave ? "  (server gave a host)" : "  (empty: null host)");
}

 
 
std::string describeStartStatus(uint32_t w0, uint32_t w1, uint32_t w2) {
    const int16_t act = int16_t(w0 >> 16);
    return "activity index " + (act < 0 ? std::string("-1") : std::to_string(act)) + ", flag " + std::to_string(w1 & 0xFF) + ", status " +
           hx(w2 & 0xFFFFF);
}

}   

std::string describeFieldValue(int index, const std::vector<uint8_t>& value) {
    const uint32_t w0 = beWordAt(value, 0);
    switch (index) {
    case 0x03: {    
        uint32_t w[7];
        for (int i = 0; i < 7; ++i) w[i] = beWordAt(value, size_t(4 * i));
        return describeHost(w);
    }
    case 0x07:      
        return describeStartStatus(w0, beWordAt(value, 4), beWordAt(value, 8));
    case 0x13:      
        return "disband reason " + std::to_string(w0 & 0x3F) + ((w0 & 0x3F) < 0x22 ? "" : " (out of range)");
    case 0x08:      
        return "+48 " + hx(beWordAt(value, 0x48)) + ", +40 " + hx(beWordAt(value, 0x40)) + hx(beWordAt(value, 0x44));
    default:
        return hx(w0) + " " + hx(beWordAt(value, 4)) + " " + hx(beWordAt(value, 8));
    }
}

const CaptureSpec kCaptureSpecs[kCaptureKinds] = {
    {CaptureKind::JoinRefuse, "join_refuse", "Join refuse", addr::JOIN_REFUSE_DECODER, 5, 5},
    {CaptureKind::ActivityHost, "activity_host", "Activity host", addr::LOBBY_ACTIVITY_HOST, 4, 7},
    {CaptureKind::SessionDisband, "session_disband", "Session disband reason", addr::LOBBY_SESSION_DISBAND, 4, 1},
    {CaptureKind::ActivityStatus, "activity_status", "Activity start status", addr::LOBBY_ACTIVITY_STATUS, 4, 3},
};

const CaptureSpec& captureSpec(CaptureKind k) { return kCaptureSpecs[int(k)]; }

std::vector<uint8_t> captureWrap(uint32_t base, uint32_t site, uint32_t after, uint32_t block, int destReg) {
    if (destReg < 3 || destReg > 10) throw Error("capture destination must be an argument register");
    if (block % 8) throw Error("capture block must be 8-aligned");
    using namespace ppc;
    Asm a(base);
     
    a.mflr(0);                         
    a.addr(12, block);                 
    a.std_(0, int64_t(kCapLr), 12);    
    a.stw(destReg, int64_t(kCapDest), 12);
    a.load32(0, after);                
    a.b(site + 4);                     
    auto code = a.link();
    if (code.size() > kCaptureWrapSize) throw Error("capture WRAP larger than its allocation");
    return code;
}

std::vector<uint8_t> captureAfter(uint32_t base, uint32_t block, int words) {
    if (words < 1 || words > 7) throw Error("capture word count out of range");
    if (block % 8) throw Error("capture block must be 8-aligned");
    using namespace ppc;
    Asm a(base);
     
     
    a.addr(12, block);
    a.lwz(11, int64_t(kCapDest), 12);
    for (int i = 0; i < words; ++i) {
        a.lwz(10, int64_t(4 * i), 11);
        a.stw(10, int64_t(kCapWords + 4 * i), 12);
    }
    a.stw(3, int64_t(kCapR3), 12);     
    a.lwz(10, int64_t(kCapCounter), 12);
    a.addi(10, 10, 1);
    a.stw(10, int64_t(kCapCounter), 12);
    a.ld(0, int64_t(kCapLr), 12);
    a.mtlr(0);
    a.blr();
    auto code = a.link();
    if (code.size() > kCaptureAfterSize) throw Error("capture AFTER larger than its allocation");
    return code;
}

std::string captureAfterName(CaptureKind k) { return std::string("activity.capture.after.") + captureSpec(k).id; }

Capture parseCapture(const world::Bytes& area, CaptureKind k) {
    const uint32_t off = uint32_t(int(k)) * kCaptureBlockSize;
    Capture c;
    c.lr = area.u64(off + kCapLr);
    c.dest = area.u32(off + kCapDest);
    c.counter = area.u32(off + kCapCounter);
    c.r3 = area.u32(off + kCapR3);
    const int words = captureSpec(k).words;
    for (int i = 0; i < words; ++i) c.words.push_back(area.u32(off + kCapWords + uint32_t(4 * i)));
    return c;
}

JoinRefuse joinRefuse(const Capture& c) {
    JoinRefuse j;
    if (c.words.size() >= 5) {
        j.session = uint64_t(c.words[0]) << 32 | c.words[1];
        j.peer = uint64_t(c.words[2]) << 32 | c.words[3];
        j.reason = c.words[4] & 0x3F;
    }
    j.decoded = c.r3 != 0;
    return j;
}

std::string describeCapture(CaptureKind k, const Capture& c) {
    if (c.counter == 0) return "not seen yet";
    char head[64];
    std::snprintf(head, sizeof head, "#%u%s: ", c.counter, c.r3 ? "" : " (partial decode)");
    if (k == CaptureKind::JoinRefuse) {
        const JoinRefuse j = joinRefuse(c);
        const char* name = joinRefuseReasonName(j.reason);
        char b[128];
        std::snprintf(b, sizeof b, "%sreason %u (%s), session %08X%08X, peer %08X%08X", head, j.reason, name ? name : "?",
                      uint32_t(j.session >> 32), uint32_t(j.session), uint32_t(j.peer >> 32), uint32_t(j.peer));
        return b;
    }
    if (k == CaptureKind::SessionDisband) {
         
        const uint32_t r = c.words.empty() ? 0 : c.words[0];
        return std::string(head) + "server disbanded the lobby, reason " + std::to_string(r & 0x3F) + ((r & 0x3F) < 0x22 ? "" : " (out of range)");
    }
    if (k == CaptureKind::ActivityStatus) {
        const auto w = [&](size_t i) { return i < c.words.size() ? c.words[i] : 0u; };
        return std::string(head) + describeStartStatus(w(0), w(1), w(2));
    }
     
    uint32_t w[7] = {};
    for (size_t i = 0; i < c.words.size() && i < 7; ++i) w[i] = c.words[i];
    return std::string(head) + describeHost(w);
}

}   
