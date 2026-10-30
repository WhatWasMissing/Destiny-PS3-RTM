#include "game/lab_research.h"

#include <algorithm>

#include "core/error.h"

namespace brps3::lab {

using world::hex8;

const ResearchFingerprints kAudioFingerprints = {
    {kAudioRegistrar, "7C0802A6487AF2C5F821F5B1F8010A60"},
};

const ResearchFingerprints kProbeFingerprints = {
    {kProfileLatchClear, "3C6001AB388000003063638480630008"},
    {kProviderGet, "3C60018980637CEC4E800020F821FF91"},
    {kInvestVt1FCCode, "7C0802A648BCBFC5F821FF71F80100A0"},
};

namespace {

bool allOk(const std::vector<Check>& c) {
    return !c.empty() && std::all_of(c.begin(), c.end(), [](const Check& x) { return x.ok; });
}

}   

bool AudioState::ok() const { return allOk(checks); }
bool ProbeAnchors::ok() const { return allOk(checks); }

AudioState readAudioState(world::Reader& mem) {
    AudioState a;
    a.state = world::readGlobal(mem, kAudioStateSlot);
    a.spatial = mem.u32(kAudioSpatialSlot);
    const world::BlockHeader& h = a.state.header;
    a.checks.push_back({"state block *(" + hex8(kAudioStateSlot) + ")", hex8(a.state.block), "a heap block", a.state.initialised});
    if (a.state.initialised) {
        a.checks.push_back({"header hash", hex8(h.hash), hex8(kAudioStateHash), h.hash == kAudioStateHash});
        a.checks.push_back({"header category", std::to_string(h.category), std::to_string(kAudioStateCategory), h.category == kAudioStateCategory});
        a.checks.push_back({"header owner", std::to_string(h.owner), std::to_string(kAudioStateOwner), h.owner == kAudioStateOwner});
    }
    a.checks.push_back({"spatial data *(" + hex8(kAudioSpatialSlot) + ")", hex8(a.spatial), "a mapped block or 0", a.spatial == 0 || world::mapped(a.spatial)});
    return a;
}

ProbeAnchors readProbeAnchors(world::Reader& mem) {
    ProbeAnchors p;
    p.latch = mem.u32(kProfileErrorLatch);
    if (world::mapped(p.latch, 2))
        if (const auto b = mem.tryBytes(p.latch, 2)) {
            p.latchBytes[0] = b->u8(0);
            p.latchBytes[1] = b->u8(1);
            p.latchRead = true;
        }
    p.checks.push_back({"profile error latch *(" + hex8(kProfileErrorLatch) + ")", hex8(p.latch), "a mapped pointer", p.latchRead});
    p.sessionPool = mem.u32(kSessionPool);
    p.checks.push_back({"session pool *(" + hex8(kSessionPool) + ")", hex8(p.sessionPool), "a mapped pointer", world::mapped(p.sessionPool)});
    p.provider = mem.u32(kInvestmentIface);
    const bool providerOk = world::mapped(p.provider, 4);
    p.checks.push_back({"investment provider *(" + hex8(kInvestmentIface) + ")", hex8(p.provider), "a mapped object", providerOk});
    if (providerOk) {
        p.vtable = mem.u32(p.provider);
        p.checks.push_back({"provider vtable", hex8(p.vtable), hex8(kInvestmentVtable), p.vtable == kInvestmentVtable});
        if (p.vtable == kInvestmentVtable) {
            p.slot1FC = mem.u32(p.vtable + 0x1FC);
            p.checks.push_back({"vtable +0x1FC (OPD)", hex8(p.slot1FC), hex8(kInvestmentVt1FC), p.slot1FC == kInvestmentVt1FC});
            if (world::mapped(p.slot1FC, 4)) {
                p.code1FC = mem.u32(p.slot1FC);
                p.checks.push_back({"OPD -> code", hex8(p.code1FC), hex8(kInvestVt1FCCode), p.code1FC == kInvestVt1FCCode});
            }
        }
    }
    return p;
}

}   
