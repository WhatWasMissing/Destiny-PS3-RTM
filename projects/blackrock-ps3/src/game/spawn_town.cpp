#include "game/spawn_town.h"

#include <algorithm>
#include <cstdio>

#include "core/error.h"

namespace brps3::spawn::town {

const world::Fingerprints kFingerprints = {
    {GET, "3C6001AB80636894886300004E800020"},
    {SET_LATCH, "3C8001AB5465063E8064689488C30003"},
    {RECOMPUTE, "7C0802A648ACCFB9F821FF41F80100D0"},
};

std::array<uint8_t, 8> State::controls() const {
    std::array<uint8_t, 8> c{};
    std::copy_n(bytes.begin(), 8, c.begin());
    return c;
}

std::optional<State> read(world::Reader& mem, std::string* why) {
    auto no = [&](const std::string& w) -> std::optional<State> {
        if (why) *why = w;
        return std::nullopt;
    };
    const uint32_t block = mem.u32(STATE_PTR);
    if (!world::validHandle(block) || !world::mapped(block, 12)) return no("the town state pointer at " + world::hex8(STATE_PTR) + " is not set");
    const auto b = mem.tryBytes(block, 12);
    if (!b) return no("the town state block " + world::hex8(block) + " is unreadable");
    State s;
    s.block = block;
    std::copy_n(b->data().begin(), 12, s.bytes.begin());
    for (int i = 0; i < 4; ++i)
        if (s.bytes[size_t(i)] > 1) return no("the town state bytes are not flags (" + hexBytes(s.bytes.data(), 4) + ")");
    return s;
}

std::string describe(const State& s) {
    char buf[160];
    std::snprintf(buf, sizeof buf, "applied %s / requested %s / dirty %u / latch %u / refs %u", s.current() ? "town" : "normal",
                  s.desired() ? "town" : "normal", unsigned(s.dirty()), unsigned(s.latch()), s.refs());
    return buf;
}

std::array<uint8_t, 8> requested(const std::array<uint8_t, 8>& original, bool town) {
    std::array<uint8_t, 8> a = original;
    a[1] = town ? 1 : 0;
    a[2] = 1;
    a[3] = town ? 0 : 1;
    a[4] = a[5] = a[6] = 0;
    a[7] = town ? 1 : 0;
    return a;
}

namespace {

 
State owned(world::Reader& mem, const PlayerFn& player, uint32_t block, uint32_t playerHandle) {
    std::string why;
    const auto now = read(mem, &why);
    if (!now) throw Error("Town presence: " + why);
    if (now->block != block) throw Error("Town presence: the town state was replaced (new activity?); refresh first");
    if (player() != playerHandle) throw Error("Town presence: the player changed; refresh first");
    return *now;
}

 
void writeControls(world::Reader& mem, const Writer& write, const PlayerFn& player, uint32_t block, uint32_t playerHandle,
                   const std::array<uint8_t, 8>& to) {
    owned(mem, player, block, playerHandle);
    write(block + 3, std::vector<uint8_t>(to.begin() + 3, to.end()));
    owned(mem, player, block, playerHandle);
    write(block + 1, {to[1], 1});
    const auto b = mem.bytes(block, 8);
    if (b.u8(1) != to[1] || !std::equal(to.begin() + 3, to.end(), b.data().begin() + 3))
        throw Error("Town presence: the control bytes did not verify (the game changed them right away?)");
}

}   

Edit request(world::Reader& mem, const Writer& write, const PlayerFn& player, const State& expected, bool town) {
    const uint32_t playerHandle = player();
    if (!world::validHandle(playerHandle)) throw Error("Town presence: no spawned player (load into the Tower or an activity first)");
    if (expected.player && expected.player != playerHandle) throw Error("Town presence: the player changed; refresh first");
    const State now = owned(mem, player, expected.block, playerHandle);
     
    if (now.bytes[1] != expected.bytes[1] || !std::equal(expected.bytes.begin() + 3, expected.bytes.begin() + 8, now.bytes.begin() + 3))
        throw Error("Town presence: the inputs changed since they were read; refresh and try again");
    Edit e;
    e.block = now.block;
    e.player = playerHandle;
    e.original = now.controls();
    e.applied = requested(e.original, town);
    e.town = town;
    writeControls(mem, write, player, e.block, e.player, e.applied);
    return e;
}

void restore(world::Reader& mem, const Writer& write, const PlayerFn& player, const Edit& e) {
    const State now = owned(mem, player, e.block, e.player);
    for (size_t i = 1; i < 8; ++i) {
        if (i == 2) continue;    
        if (now.bytes[i] != e.original[i] && now.bytes[i] != e.applied[i])
            throw Error("Town presence: the inputs were changed by something else; not overwriting them");
    }
    writeControls(mem, write, player, e.block, e.player, e.original);
}

}   
