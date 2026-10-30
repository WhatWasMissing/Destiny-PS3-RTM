#include "game/world_player.h"

#include <algorithm>

namespace brps3::world {

namespace {

constexpr uint32_t kElementRead = player::ELEM_POSITION + 0x10;    

PlayerLookup fail(PlayerStatus s, std::string message) {
    PlayerLookup r;
    r.status = s;
    r.message = std::move(message);
    return r;
}

}   

const char* playerStatusName(PlayerStatus s) {
    switch (s) {
    case PlayerStatus::Ok: return "ok";
    case PlayerStatus::NoDescriptor: return "no descriptor";
    case PlayerStatus::NotSpawned: return "not spawned";
    case PlayerStatus::BadPool: return "bad sObject pool";
    case PlayerStatus::IdentityMismatch: return "identity mismatch";
    }
    return "?";
}

PlayerLookup findLocalPlayer(Reader& mem) {
    const WorldGlobals g = readGlobals(mem);
    LocalPlayer p;
    p.descriptor = mem.u32(addr::PLAYER_DESC_PTR);
    if (!validHandle(p.descriptor) || !heap(p.descriptor, 0x10))
        return fail(PlayerStatus::NoDescriptor, "The player descriptor is not set (menus or loading): load into orbit or an activity.");
    const Bytes dh = mem.bytes(p.descriptor, 0x10);
    p.elements = dh.u32(4);
    p.elementSize = dh.u32(8);
    p.elementCount = dh.u32(0xC);
    if (p.elementSize != player::DESC_ELEMENT_SIZE || !heap(p.elements, player::DESC_ELEMENT_SIZE))
        return fail(PlayerStatus::NoDescriptor, "The player descriptor at " + hex8(p.descriptor) + " is not plausible (elements " +
                                                    hex8(p.elements) + ", element size " + hex8(p.elementSize) + ").");
    if (!g.sobjectsOk())
        return fail(PlayerStatus::BadPool, "The sObject pool is not set up (base " + hex8(g.sobjects.base) + ", stride " +
                                               hex8(g.sobjects.stride) + "): load into a game session.");

     
    Bytes elem = mem.bytes(p.elements, kElementRead);
    p.handle = elem.u32(player::ELEM_HANDLE);
    if (!validHandle(p.handle))
        return fail(PlayerStatus::NotSpawned, "No spawned player (orbit, menus or character select): load into an activity first.");

     
    std::optional<Bytes> rows;
    if (g.players.plausible() && g.players.stride >= player::ENTRY_FORWARD + 12 && g.players.capacity <= 64)
        rows = mem.tryBytes(g.players.base, g.players.capacity * g.players.stride);
    auto findRow = [&](uint32_t h) -> std::optional<uint32_t> {
        if (!rows) return std::nullopt;
        for (uint32_t i = 0; i < g.players.capacity; ++i)
            if (rows->u32(i * g.players.stride + player::ENTRY_HANDLE) == h) return i;
        return std::nullopt;
    };
    auto row = findRow(p.handle);
    if (row) {
        const uint32_t idx = rows->u32(*row * g.players.stride + player::ENTRY_DATUM) & 0xFFFF;
        if (idx != 0 && idx < p.elementCount && idx < 64) {
            Bytes other = mem.bytes(p.elements + idx * p.elementSize, kElementRead);
            if (other.u32(player::ELEM_HANDLE) == p.handle) {
                elem = std::move(other);
                p.elementIndex = idx;
            }
        }
    }
    p.element = p.elements + p.elementIndex * p.elementSize;
    p.position = elem.vec3(player::ELEM_POSITION);

    const auto obj = readSObject(mem, g.sobjects, p.handle);
    if (!obj)
        return fail(PlayerStatus::IdentityMismatch, "The player's sObject " + hex8(p.handle) +
                                                        " failed the identity check (+0xC != handle, or being deleted): try again.");
    p.object = *obj;
    p.entry = obj->addr;
    if (row) {
        const uint32_t off = *row * g.players.stride;
        p.playerEntry = g.players.base + off;
        p.playerDatum = rows->u32(off + player::ENTRY_DATUM);
        p.faction = rows->s8(off + player::ENTRY_FACTION);
        p.entryPosition = rows->vec3(off + player::ENTRY_POSITION);
        p.forward = rows->vec3(off + player::ENTRY_FORWARD);
    }
    PlayerLookup r;
    r.status = PlayerStatus::Ok;
    r.player = p;
    return r;
}

LocalPlayer requireLocalPlayer(Reader& mem) {
    PlayerLookup r = findLocalPlayer(mem);
    if (r.status != PlayerStatus::Ok || !r.player) throw Error(r.message.empty() ? "No local player" : r.message);
    return *r.player;
}

bool stillValid(Reader& mem, const LocalPlayer& p) {
    try {
        if (mem.u32(p.element + player::ELEM_HANDLE) != p.handle) return false;
        const Bytes e = mem.bytes(p.entry, 0x10);
        return e.u32(sobj::HANDLE) == p.handle && !(e.u32(sobj::FLAGS) & sobj::FLAG_DELETING);
    } catch (const std::exception&) {
        return false;
    }
}

Vec3 readPosition(Reader& mem, const LocalPlayer& p) { return mem.bytes(p.element + player::ELEM_POSITION, 12).vec3(0); }

Vec3 readEnginePosition(Reader& mem, const LocalPlayer& p) { return mem.bytes(p.entry + sobj::POSITION, 12).vec3(0); }

 

std::optional<LocalPlayer> PlayerCache::get(Reader& mem, int maxAgeMs, bool validate) {
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(m_);
        const bool fresh = have_ && now - at_ < std::chrono::milliseconds(std::max(maxAgeMs, 0));
        if (fresh && last_.player && (!validate || stillValid(mem, *last_.player))) return last_.player;
    }
    PlayerLookup r = findLocalPlayer(mem);
    std::lock_guard<std::mutex> lock(m_);
    last_ = r;
    at_ = now;
    have_ = true;
    return r.player;
}

LocalPlayer PlayerCache::require(Reader& mem, int maxAgeMs, bool validate) {
    auto p = get(mem, maxAgeMs, validate);
    if (!p) {
        std::lock_guard<std::mutex> lock(m_);
        throw Error(last_.message.empty() ? "No local player" : last_.message);
    }
    return *p;
}

void PlayerCache::invalidate() {
    std::lock_guard<std::mutex> lock(m_);
    have_ = false;
}

PlayerLookup PlayerCache::last() const {
    std::lock_guard<std::mutex> lock(m_);
    return last_;
}

}   
