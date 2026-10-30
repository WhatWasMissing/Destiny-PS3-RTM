#include "game/activity_resourcer.h"

#include <algorithm>
#include <map>

#include "game/activity_addr.h"

namespace brps3::activity {

namespace {

 
 
 
class PageCache final : public world::Reader {
public:
    explicit PageCache(world::Reader& inner) : inner_(inner) {}
    std::vector<uint8_t> read(uint32_t addr, uint32_t size) override {
        ++reads_;
        std::vector<uint8_t> out;
        out.reserve(size);
        uint32_t a = addr;
        const uint64_t end = uint64_t(addr) + size;
        while (a < end) {
            const uint32_t page = a & ~(kPage - 1);
            const std::vector<uint8_t>* p = pageAt(page);
            if (!p) return inner_.read(addr, size);    
            const uint32_t off = a - page;
            const uint32_t n = uint32_t(std::min<uint64_t>(end - a, kPage - off));
            out.insert(out.end(), p->begin() + off, p->begin() + off + n);
            a += n;
        }
        return out;
    }

private:
    static constexpr uint32_t kPage = 0x4000;
    const std::vector<uint8_t>* pageAt(uint32_t page) {
        auto it = pages_.find(page);
        if (it != pages_.end()) return it->second.empty() ? nullptr : &it->second;
        std::vector<uint8_t> data;
        try {
            data = inner_.read(page, kPage);
        } catch (const std::exception&) {
            data.clear();
        }
        if (data.size() != kPage) data.clear();
        auto& slot = pages_[page];
        slot = std::move(data);
        return slot.empty() ? nullptr : &slot;
    }
    world::Reader& inner_;
    std::map<uint32_t, std::vector<uint8_t>> pages_;
};

}   

RscState readRscState(world::Reader& mem) {
    RscState s;
    if (auto bt = mem.tryBytes(addr::RSC_BEGIN_TABLE, 4)) {
        s.beginCat0 = bt->u16(0);
        s.beginCat1 = bt->u16(2);
    } else {
        s.why = "resourcerer begin table " + world::hex8(addr::RSC_BEGIN_TABLE) + " is unmapped";
        return s;
    }
    s.mode = mem.u32(addr::RSC_MODE);
    if (auto h = mem.tryBytes(addr::RSC_HANDLE_A, 4)) {
        s.handleA = h->u16(0);
        s.handleB = h->u16(2);
    }
    if (auto d = mem.tryBytes(addr::RSC_DIRTY_ADD, 2)) {
        s.dirtyAdd = d->u8(0);
        s.dirtyDel = d->u8(1);
    }
    s.cursor = mem.bytes(addr::WTL_CURSOR, 2).u16(0);
    s.odHead = mem.bytes(addr::RSC_OD_HEAD, 2).u16(0);
     
    const uint32_t reqRel = mem.u32(addr::RSC_REQUESTS);
    const uint32_t reqStride = mem.u32(addr::RSC_REQUEST_STRIDE);
    const uint32_t reqBase = addr::RSC_REQUESTS + reqRel;
    if (reqStride >= 8 && reqStride <= 0x1000 && world::mapped(reqBase, reqStride * 2)) {
        for (int i = 0; i < 2; ++i) {
            const uint32_t a = reqBase + uint32_t(i) * reqStride;
            const world::Bytes b = mem.bytes(a, 8);
            s.requests.push_back({i, a, b.u32(0), b.u8(4), b.u8(5)});
        }
    }
     
    const world::Bytes odPool = mem.bytes(addr::RSC_OD_POOL, 12);
    const uint32_t odBase = addr::RSC_OD_POOL + odPool.u32(0);
    const uint32_t linkBase = addr::RSC_OD_POOL + 4 + odPool.u32(4);
    const uint32_t odStride = odPool.u32(8);
    if (odStride >= 5 && odStride <= 0x1000 && s.odHead != 0xFFFF && world::mapped(odBase, odStride)) {
        PageCache walk(mem);
        uint16_t idx = s.odHead;
        for (size_t guard = 0; idx != 0xFFFF && guard < kOdWalkGuard; ++guard) {
            const uint32_t rec = odBase + uint32_t(idx) * odStride;
            if (!world::mapped(rec, 5) || !world::mapped(linkBase + uint32_t(idx) * 4, 2)) break;
            ++s.odCount;
            if (s.onDemand.size() < kMaxShown) {
                const world::Bytes r = walk.bytes(rec, 5);
                s.onDemand.push_back({idx, r.u32(0), r.u8(4)});
            }
            const uint16_t next = walk.bytes(linkBase + uint32_t(idx) * 4, 2).u16(0);
            if (next == idx) break;
            idx = next;
        }
        s.odTruncated = s.odCount > s.onDemand.size();
    }
    s.ok = true;
    return s;
}

WtlState readWtlState(world::Reader& mem) {
    WtlState s;
    s.globals = mem.u32(addr::WTL_GLOBALS_PTR);
    s.cursor = mem.bytes(addr::WTL_CURSOR, 2).u16(0);
    if (!world::mapped(s.globals, addr::WTL_GLOBALS_SIZE)) {
        s.why = "world tag loader globals " + world::hex8(s.globals) + " are not set up";
        return s;
    }
    const world::Bytes g = mem.bytes(s.globals, addr::WTL_GLOBALS_SIZE);
    s.inflightDet = g.u32(addr::WTL_INFLIGHT_DET);
    s.inflightNondet = g.u32(addr::WTL_INFLIGHT_NONDET);
    s.queueA = g.u32(addr::WTL_QUEUE_A);
    s.queueB = g.u32(addr::WTL_QUEUE_B);
    s.queueC = g.u32(addr::WTL_QUEUE_C);
    s.ok = true;
    return s;
}

}   
