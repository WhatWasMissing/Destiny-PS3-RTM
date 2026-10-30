#include "game/activity_client.h"

#include <cstdio>
#include <optional>

#include "game/activity_addr.h"
#include "game/world_objects.h"
#include "game/world_tags.h"

namespace brps3::activity {

const char* ClientSlot::verdict() const {
    if (!aclResolved) return "no client";
    if (aclValid) return "validated";
    if (aclVerdict20 == 0xFFFFFFFFu) return "rejected";
    return "pending";
}

namespace {

const char* kSlotNames[4] = {"CURRENT-PRIVATE", "CURRENT-PUBLIC", "TARGET-PRIVATE", "TARGET-PUBLIC"};

std::string hx(uint32_t v) {
    char b[12];
    std::snprintf(b, sizeof b, "%08X", v);
    return b;
}

 
std::optional<uint32_t> datumEntry(const world::PoolHeader& pool, uint32_t h, uint32_t need) {
    if (h == 0xFFFFFFFFu || !pool.base || !pool.stride) return std::nullopt;    
    const uint32_t idx = h & 0x1FFF;
    if (pool.capacity && idx >= pool.capacity) return std::nullopt;
    const uint32_t e = pool.base + idx * pool.stride;
    return world::mapped(e, need) ? std::optional<uint32_t>(e) : std::nullopt;
}

BapPeer readPeer(world::Reader& mem, int peerIndex) {
    BapPeer p;
    p.peerIndex = peerIndex;
    if (peerIndex < 0 || peerIndex > 64) return p;
    const uint32_t table = mem.u32(addr::PEER_TABLE_PTR);
    const uint32_t objects = mem.u32(addr::PEER_OBJECTS_PTR);
    if (!world::mapped(table, 24 * uint32_t(peerIndex) + 16) || !objects) return p;
    p.handle = mem.u32(table + 24 * uint32_t(peerIndex) + 8);
    if (p.handle == 0xFFFFFFFFu || p.handle > 0xFF) return p;    
    const uint32_t o = objects + addr::BAP_STRIDE * p.handle;
    if (!world::mapped(o + addr::BAP_STATE, addr::BAP_REASON + 4 - addr::BAP_STATE)) return p;
     
    const world::Bytes b = mem.bytes(o + addr::BAP_STATE, addr::BAP_REASON + 4 - addr::BAP_STATE);
    p.state = b.u32(0);
    p.reason = b.u32(addr::BAP_REASON - addr::BAP_STATE);
    p.endpoint = b.cstr(addr::BAP_ENDPOINT - addr::BAP_STATE, 0x20);
    p.ladder = peerLadder(p.state);
    p.ok = true;
    return p;
}

struct TagCache {
    bool tried = false;
    std::optional<world::TagTable> table;
};

 
std::optional<uint32_t> tagElement(world::Reader& mem, TagCache& tc, uint32_t chk, std::string& why) {
    if (!tc.tried) {
        tc.tried = true;
        try {
            tc.table = world::readTagTable(mem);
        } catch (const std::exception& e) {
            why = std::string("no tag table: ") + e.what();
        }
    }
    if (!tc.table) {
        if (why.empty()) why = "no tag table";
        return std::nullopt;
    }
    const world::Resolved r = world::resolve(mem, *tc.table, chk, false);
    if (!r.ok) {
        why = "tag " + hx(chk) + ": " + r.why;
        return std::nullopt;
    }
    return r.body;
}

void readDetail(world::Reader& mem, TagCache& tc, uint32_t chk, ClientSlot& s, const world::PoolHeader& connPool,
                const world::PoolHeader& aclPool) {
    ClientDetail& d = s.detail;
    d.read = true;
    std::string tagWhy;
    std::optional<uint32_t> te;
    if (chk != 0xFFFFFFFFu) te = tagElement(mem, tc, chk, tagWhy);
     
    if (auto acl = datumEntry(aclPool, s.aclHandle, 0x644)) {
        d.acl = *acl;
        if (auto a = mem.tryBytes(*acl, 0x644)) {
            d.nbubbles = a->u32(0x640);
            const uint32_t hs = a->u32(8);
            if (world::mapped(hs + 0x50B0, 0x1C)) {
                const world::Bytes st = mem.bytes(hs + 0x50B0 + 0x10, 0xC);
                const uint32_t have = st.u32(0);
                d.chosen = st.slice(4, 8).data();
                if (te) {
                    const uint32_t want = mem.u32(*te + 0x34);
                    d.bubblesOk = want == have;
                    d.bubbles = want == have ? "COUNT OK (" + std::to_string(have) + ")"
                                : have      ? "BUBBLE COUNT MISMATCH (have " + std::to_string(have) + ", want " + std::to_string(want) + ")"
                                            : "INVALID HOST ACTIVITY STATE (have 0, want " + std::to_string(want) + ")";
                } else {
                    d.bubbles = "have " + std::to_string(have) + "; want unknown (" + (chk == 0xFFFFFFFFu ? std::string("chk -1") : tagWhy) + ")";
                }
            } else {
                d.bubbles = "host state " + hx(hs) + " not set";
            }
        }
    } else {
        d.bubbles = "activity client not resolved";
    }
     
    if (auto conn = datumEntry(connPool, s.connHandle, 0xDF8)) {
        d.conn = *conn;
        if (chk == 0xFFFFFFFFu) {
            d.checksumOk = true;
            d.checksum = "chk = -1, nothing to check (passes)";
        } else {
            const uint32_t widx = mem.u32(*conn + 0x10);
            const uint32_t p1 = mem.u32(*conn + 0xDF4);
            std::optional<uint32_t> left, right;
            if (world::mapped(p1 + 8, 4)) {
                const uint32_t p2 = mem.u32(p1 + 8);
                if (world::mapped(p2 + 0x50B0 + 0x5C, 4)) left = mem.u32(p2 + 0x50B0 + 0x5C);
            }
            if (te && widx < 0x10000) right = mem.u32(*te + 4 * (widx + 2));
            d.checksumOk = left && right && *left == *right;
            d.checksum = std::string(d.checksumOk ? "MATCH" : "CHECKSUM MISMATCH") + " (left " + (left ? hx(*left) : std::string("??")) +
                         ", right " + (right ? hx(*right) : std::string("??")) + ", widx " + std::to_string(widx) + ")";
            if (!te) d.checksum += "; " + tagWhy;
        }
    } else {
        d.checksum = "connection not resolved";
    }
}

}   

std::string ClientSlot::key() const {
    char b[160];
    std::snprintf(b, sizeof b, "%s|%d|%d|%08X|%08X|%u|%d|%08X|%d|%08X|%u|%u", name, recordIndex, astat, connHandle, aclHandle, aclFlag,
                  aclValid ? 1 : 0, aclVerdict20, peer.peerIndex, peer.handle, peer.state, peer.reason);
    return b;
}

std::string ClientProbe::key() const {
    std::string k = hx(chk);
    for (const ClientSlot& s : slots) k += ";" + s.key();
    return k;
}

std::string describeSlot(const ClientSlot& s) {
    char b[200];
    std::snprintf(b, sizeof b, "%s idx%d ASTAT=%d comp=%u aclflag=%u conn=%08X acl=%08X -> %s", s.name, s.recordIndex, s.astat, s.comp, s.aclFlag,
                  s.connHandle, s.aclHandle, s.verdict());
    std::string out = b;
    if (s.peer.ok) {
        const char* st = bapStateName(s.peer.state);
        const char* rn = bapReasonName(s.peer.reason);
        out += "; BAP " + std::string(st ? st : "?") + " (ladder " + std::to_string(s.peer.ladder) + (s.peer.ready() ? " READY" : " NOT READY") +
               ")" + (s.peer.reason ? ", reason " + std::string(rn ? rn : "?") : std::string()) +
               (s.peer.endpoint.empty() ? std::string() : ", endpoint " + s.peer.endpoint);
    }
    return out;
}

ClientProbe readClientProbe(world::Reader& mem, bool details) {
    ClientProbe r;
    r.recordBase = mem.u32(addr::ACTIVITY_RECORD_PTR);
    r.slotTable = mem.u32(addr::SLOT_TABLE_PTR);
    r.clock = mem.u32(addr::ENGINE_CLOCK_LO);
    if (!world::mapped(r.recordBase, addr::RECORD_FIRST + 3 * addr::RECORD_STRIDE) || !world::mapped(r.slotTable, 0x28)) {
        r.why = "no activity session (record base " + world::hex8(r.recordBase) + ", slots " + world::hex8(r.slotTable) + ")";
        return r;
    }
    const world::Bytes hdr = mem.bytes(r.recordBase + 0x268, 12);
    r.chk268 = hdr.u32(0);
    r.chk26C = hdr.u32(4);
    r.chk = hdr.u32(8);                                
    const world::Bytes slotTab = mem.bytes(r.slotTable, 0x28);    
    const world::PoolHeader connPool = world::readPool(mem, addr::CONN_POOL);
    const world::PoolHeader aclPool = world::readPool(mem, addr::ACL_POOL);
    TagCache tc;
    for (int k = 0; k < 4; ++k) {
        const int32_t idx = int32_t(slotTab.u32(uint32_t(4 * k)));
        if (idx < 0 || idx > 2) continue;
        ClientSlot s;
        s.name = kSlotNames[k];
        s.recordIndex = idx;
        const uint32_t rec = r.recordBase + addr::RECORD_FIRST + addr::RECORD_STRIDE * uint32_t(idx);
        const world::Bytes record = mem.bytes(rec, addr::RECORD_PEER_INDEX + 4);
        s.astat = int32_t(record.u32(0));
        s.connHandle = record.u32(0x70);
        s.comp = record.u8(0x74);
        s.aclFlag = slotTab.u8(0x10 + 8 * uint32_t(idx));
        s.aclHandle = slotTab.u32(0x14 + 8 * uint32_t(idx));
        if (auto acl = datumEntry(aclPool, s.aclHandle, 0x18)) {
            if (auto a = mem.tryBytes(*acl + 0x10, 8)) {
                s.aclResolved = true;
                s.aclValid = a->u8(0) == 1;                
                s.aclVerdict20 = a->u32(4);
            }
        }
        s.peer = readPeer(mem, int32_t(record.u32(addr::RECORD_PEER_INDEX)));
        if (!s.populated()) continue;
        if (details) readDetail(mem, tc, r.chk, s, connPool, aclPool);
        r.slots.push_back(std::move(s));
    }
    r.ok = true;
    if (r.slots.empty()) r.why = "activity session present but no connection slots are filled (not matchmaking yet)";
    return r;
}

}   
