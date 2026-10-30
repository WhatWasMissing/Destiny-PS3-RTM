#include "game/inventory_capture.h"

#include <cstdio>

#include "core/error.h"
#include "core/ppc.h"
#include "game/inventory_addr.h"

namespace brps3::inventory {

namespace {

uint32_t be32(const std::vector<uint8_t>& b, size_t at) {
    return uint32_t(b[at]) << 24 | uint32_t(b[at + 1]) << 16 | uint32_t(b[at + 2]) << 8 | b[at + 3];
}
uint64_t be64(const std::vector<uint8_t>& b, size_t at) { return uint64_t(be32(b, at)) << 32 | be32(b, at + 4); }

}   

const std::vector<CaptureSite>& captureSites() {
    static const std::vector<CaptureSite> sites = {
        {402, addr::CAPTURE_402, 0x487DC7C9, addr::BUILDER_402, 7, 4, "item action (use / dismantle)"},
        {403, addr::CAPTURE_403, 0x487DC80D, addr::BUILDER_403, 4, 3, "equip"},
        {404, addr::CAPTURE_404, 0x487DC7A5, addr::BUILDER_404, 4, 3, "unequip"},
        {405, addr::CAPTURE_405, 0x487DC75D, addr::BUILDER_405, 8, 5, "transfer"},
        {904, addr::CAPTURE_904, 0x487DC435, addr::BUILDER_904, 3, -1, "vendor claim"},
    };
    return sites;
}

uint32_t branchTarget(uint32_t site, uint32_t word) {
    if ((word >> 26) != 18) throw Error("not a branch at " + hex32(site) + ": " + hex32(word));
    int32_t disp = int32_t(word & 0x03FFFFFC);
    if (disp & 0x02000000) disp -= 0x04000000;
    return (word & 2) ? uint32_t(disp) : uint32_t(int64_t(site) + disp);
}

std::vector<uint8_t> captureStub(uint32_t base, const CaptureSite& s, uint32_t buffer) {
    if (branchTarget(s.site, s.word) != s.builder) throw Error("capture site " + hex32(s.site) + " does not call its builder");
    using namespace ppc;
    Asm a(base);
     
    a.load32(11, buffer);
    a.lwz(12, 4, 11);                        
    a.rlwinm(0, 12, 7, 22, 24);              
    a.add(12, 11, 0).addi(12, 12, int64_t(kCaptureHeader));
    a.stw(12, 8, 11);                        
    for (int i = 0; i < 6; ++i) a.std_(3 + i, 8 * i, 12);
    a.std_(28, 0x30, 12);
    if (s.soidReg >= 0) a.lwz(0, 0, s.soidReg).stw(0, 0x38, 12).lwz(0, 4, s.soidReg).stw(0, 0x3C, 12);
    a.stw(s.requestReg, 0x60, 12);
    a.li(0, s.op).stw(0, 0x64, 12);
    a.lwz(0, 4, 11).stw(0, 0x68, 12);        
     
    a.bl(s.builder);
     
    a.load32(11, buffer);
    a.lwz(12, 8, 11);
    a.lwz(11, 0x60, 12);
    for (int i = 0; i < 8; ++i) a.lwz(0, 4 * i, 11).stw(0, 0x40 + 4 * i, 12);
     
    a.load32(11, buffer);
    a.lwz(12, 4, 11).addi(12, 12, 1).stw(12, 4, 11);
    a.b(s.site + 4);
    return a.link();
}

std::vector<uint8_t> emptyCaptureBuffer() {
    std::vector<uint8_t> b(kCaptureBuffer, 0);
    for (int i = 0; i < 4; ++i) b[size_t(i)] = uint8_t((kCaptureMagic >> (24 - 8 * i)) & 0xFFu);
    return b;
}

std::vector<CaptureRecord> parseCaptures(const std::vector<uint8_t>& b, uint32_t& seen) {
    std::vector<CaptureRecord> out;
    if (b.size() < kCaptureBuffer || be32(b, 0) != kCaptureMagic) throw Error("capture buffer was overwritten");
    const uint32_t count = be32(b, 4);
    const uint32_t from = count > kCaptureRecords && count - kCaptureRecords > seen ? count - kCaptureRecords : seen;
    for (uint32_t n = from; n < count; ++n) {
        const size_t at = kCaptureHeader + (n % kCaptureRecords) * kCaptureRecord;
        if (be32(b, at + 0x68) != n) continue;               
        CaptureRecord r;
        r.sequence = n + 1;
        r.op = int(be32(b, at + 0x64));
        for (size_t i = 0; i < 6; ++i) r.r[i] = be64(b, at + 8 * i);
        r.callback = be64(b, at + 0x30);
        r.soid = be64(b, at + 0x38);
        r.requestPtr = be32(b, at + 0x60);
        for (size_t i = 0; i < 32; ++i) r.request[i] = b[at + 0x40 + i];
        out.push_back(r);
    }
    seen = count;
    return out;
}

std::string CaptureRecord::describe() const {
    char buf[256];
    auto lo = [](uint64_t v) { return static_cast<unsigned long long>(v); };
    switch (op) {
    case 402:
        std::snprintf(buf, sizeof buf, "402 presence=%llu soid=%016llX def=%llu qty=%lld cb=%016llX", lo(r[0] & 0xFF), lo(soid), lo(r[2] & 0xFFFF),
                      static_cast<long long>(int32_t(uint32_t(r[3]))), lo(callback));
        break;
    case 403:
    case 404: std::snprintf(buf, sizeof buf, "%d soid=%016llX cb=%016llX", op, lo(soid), lo(callback)); break;
    case 405:
        std::snprintf(buf, sizeof buf, "405 ctx=%d key=%d soid=%016llX def=%llu qty=%lld cb=%016llX", int(int8_t(r[0])), int(int8_t(r[1])), lo(soid),
                      lo(r[3] & 0xFFFF), static_cast<long long>(int32_t(uint32_t(r[4]))), lo(callback));
        break;
    case 904:
        std::snprintf(buf, sizeof buf, "904 vendor=%d interaction=%d reply=%d selection=%d cb=%016llX", int(int8_t(r[1])), int(int16_t(r[2])),
                      int(int16_t(r[3])), int32_t(uint32_t(r[4])), lo(callback));
        break;
    default: std::snprintf(buf, sizeof buf, "op %d", op); break;
    }
    return std::string(buf) + "  request " + hexBytes(request.data(), request.size());
}

}   
