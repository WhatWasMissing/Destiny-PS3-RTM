#include "game/player_aura.h"

#include <algorithm>

#include "core/error.h"
#include "core/ppc.h"
#include "core/runtime.h"
#include "game/player_addr.h"
#include "game/world_mem.h"

namespace brps3::player::aura {

std::vector<uint8_t> stub(uint32_t base, uint32_t block, uint32_t site, uint32_t original) {
    ppc::Asm a(base);
    a.addr(12, block);
    a.lwz(0, 0, 12).cmpwi(0, 0).beq("replay");
    a.lwz(0, 4, 12).stw(0, 0x10, 3);
    a.label("replay").word(original);
    a.b(site + 4);
    auto code = a.link();
    if (code.size() > kStubSize) throw Error("kill aura stub larger than its allocation");
    return code;
}

void KillAura::enable(Runtime& rt, float value) {
    const uint32_t site = addr::KILL_AURA_SITE;
    if (block_ && rt.hooked(site)) {
        setValue(rt, value);
        return;
    }
    if (rt.hooked(site)) throw Error("the kill aura site " + world::hex8(site) + " is already hooked by something else");
    if (block_) {                                                
        rt.freeLater(block_);
        block_ = 0;
    }
    std::vector<uint8_t> init(kBlockSize, 0);
    const auto v = world::beF32(value);
    std::copy(v.begin(), v.end(), init.begin() + 4);             
    const uint32_t blk = rt.alloc(kBlockSize, init.data(), init.size());
    try {
        rt.hook(site, [blk, site](uint32_t base, uint32_t original) { return stub(base, blk, site, original); }, addr::KILL_AURA_ORIGINAL,
                kStubSize);
    } catch (...) {
        rt.freeLater(blk);
        throw;
    }
    block_ = blk;
    rt.writeU32(block_, 1);                                      
}

void KillAura::setValue(Runtime& rt, float value) {
    if (!block_ || !rt.hooked(addr::KILL_AURA_SITE)) return;
    rt.writeF32(block_ + 4, value);
}

void KillAura::disable(Runtime& rt) {
    if (block_) {
        try {
            rt.writeU32(block_, 0);                              
        } catch (const std::exception&) {
        }
    }
    if (rt.hooked(addr::KILL_AURA_SITE)) rt.unhook(addr::KILL_AURA_SITE);
    if (block_) rt.freeLater(block_);
    block_ = 0;
}

bool KillAura::active(const Runtime& rt) const { return block_ && rt.hooked(addr::KILL_AURA_SITE); }

}   
