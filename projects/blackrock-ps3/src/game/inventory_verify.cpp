#include "game/inventory_verify.h"

#include <chrono>
#include <cstdio>
#include <thread>
#include <tuple>

#include "core/error.h"
#include "game/inventory_addr.h"
#include "game/inventory_snapshot.h"

namespace brps3::inventory {

namespace {

using world::hex8;

uint32_t be32(const std::vector<uint8_t>& b, size_t at) {
    return uint32_t(b[at]) << 24 | uint32_t(b[at + 1]) << 16 | uint32_t(b[at + 2]) << 8 | b[at + 3];
}

struct Report {
    VerifyReport r;
    std::function<void(const std::string&)> progress;
    void add(VerifyLine::Level l, std::string what, std::string detail = {}) {
        if (progress) progress(what);
        r.lines.push_back({l, std::move(what), std::move(detail)});
    }
};

}   

std::vector<std::pair<int, int>> parseContextPairs(const std::vector<uint8_t>& t) {
    if (t.size() < 0x30) throw Error("context table too short");
    const uint32_t n = be32(t, 0x2C);
    const size_t header = 0x30 + size_t(be32(t, 0x30));
    if (n > 256 || header < 4 || header + 16 + size_t(n) * 2 > t.size()) throw Error("context table header invalid");
    if (be32(t, header - 4) != off::DEF_MARKER || be32(t, header) != n || be32(t, header + 4) != off::CONTEXT_CLASS)
        throw Error("context table class/count mismatch");
    std::vector<std::pair<int, int>> out;
    for (uint32_t i = 0; i < n; ++i) out.push_back({t[header + 16 + i * 2], t[header + 16 + i * 2 + 1]});
    return out;
}

VerifyReport verifyCatalog(world::Reader& mem, const Catalog& cat, const std::function<void(const std::string&)>& progress) {
    Report rep;
    rep.progress = progress;
    using L = VerifyLine;
    if (!cat.error.empty()) rep.add(L::Fail, "bundled catalogue unavailable", cat.error);
     
    InvestmentInterface i;
    try {
        i = readInvestmentInterface(mem);
        rep.r.mode = i.mode;
        rep.add(L::Ok, "investment interface", hex8(i.iface) + ": vtable " + hex8(i.vtable) + ", mode " + std::to_string(i.mode) + ", tag " + hex8(i.tag));
    } catch (const std::exception& e) {
        rep.add(L::Fail, "investment interface", e.what());
        rep.r.verdict = "verdict: the investment provider is not ready (load a character) - nothing else checked";
        return rep.r;
    }
     
    try {
        const world::Bytes vt = mem.bytes(addr::INVESTMENT_VTABLE, 0x294);
        const uint32_t offs[] = {0x4, 0xC, 0x4C, 0xB4, 0x160, 0x1FC, 0x290};
        int good = 0;
        std::string bad;
        for (size_t k = 0; k < kCatalogFingerprints.size(); ++k) {
            const uint32_t opd = vt.u32(offs[k]);
            const uint32_t code = mem.u32(opd);
            const auto want = parseHexBytes(kCatalogFingerprints[k].second);
            const world::Bytes got = mem.bytes(code, uint32_t(want.size()));
            if (code == kCatalogFingerprints[k].first && got.data() == want) {
                ++good;
            } else {
                char b[160];
                std::snprintf(b, sizeof b, "vtable+%X -> OPD %s -> %s (expected %s%s); ", offs[k], hex8(opd).c_str(), hex8(code).c_str(),
                              hex8(kCatalogFingerprints[k].first).c_str(), got.data() == want ? "" : ", bytes differ");
                bad += b;
            }
        }
        rep.add(bad.empty() ? L::Ok : L::Fail, "investment methods", std::to_string(good) + " of 7 match the port map" + (bad.empty() ? "" : ": " + bad));
    } catch (const std::exception& e) {
        rep.add(L::Fail, "investment methods", e.what());
    }
     
    uint32_t globals = 0, buckets = 0, defs = 0, index = 0, context = 0;
    uint32_t bucketsTag = 0, defsTag = 0, indexTag = 0;
    try {
        globals = resolveInvestmentTag(mem, off::GLOBALS_TAG, i.mode);
        const world::Bytes refs = mem.bytes(globals + off::GLOBALS_BUCKETS, off::GLOBALS_INDEX - off::GLOBALS_BUCKETS + 4);
        bucketsTag = refs.u32(0);
        defsTag = refs.u32(off::GLOBALS_DEFS - off::GLOBALS_BUCKETS);
        indexTag = refs.u32(off::GLOBALS_INDEX - off::GLOBALS_BUCKETS);
        const bool expected = bucketsTag == 0x80A5F239 && defsTag == 0x80A5F241 && indexTag == 0x80A5F24C;
        rep.add(expected ? L::Ok : L::Warn, "globals references (+0x84 / +0x144 / +0x29C)",
                hex8(bucketsTag) + " / " + hex8(defsTag) + " / " + hex8(indexTag) +
                    (expected ? " (as seen live on PS3)" : " (the port map probe saw 80A5F239 / 80A5F241 / 80A5F24C; followed anyway)"));
        buckets = resolveInvestmentTag(mem, bucketsTag, i.mode);
        defs = resolveInvestmentTag(mem, defsTag, i.mode);
        index = resolveInvestmentTag(mem, indexTag, i.mode);
        const world::Bytes caches = mem.bytes(addr::TAG_CACHE_GLOBALS, addr::TAG_CACHE_INDEX - addr::TAG_CACHE_GLOBALS + 4);
        auto cached = [&](uint32_t a) { return caches.u32(a - addr::TAG_CACHE_GLOBALS); };
        std::string detail;
        bool ok = true;
        for (auto [slot, body, name] : {std::tuple{addr::TAG_CACHE_GLOBALS, globals, "globals"}, std::tuple{addr::TAG_CACHE_BUCKETS, buckets, "buckets"},
                                         std::tuple{addr::TAG_CACHE_DEFS, defs, "definitions"}, std::tuple{addr::TAG_CACHE_INDEX, index, "index"}}) {
            const uint32_t v = cached(slot);
            const bool agree = v == 0 || v == body;
            ok &= agree;
            detail += std::string(name) + " " + (v ? hex8(v) : std::string("empty")) + (agree ? "" : " != " + hex8(body)) + "; ";
        }
        rep.add(ok ? L::Ok : L::Fail, "tag caches agree with fresh resolution", detail);
    } catch (const std::exception& e) {
        rep.add(L::Fail, "tag resolution", e.what());
    }
     
    if (buckets) {
        if (const auto* want = cat.table(0x80A5F237)) {
            try {
                const world::Bytes got = mem.bytes(buckets, uint32_t(want->size()));
                size_t diff = 0;
                for (size_t k = 0; k < want->size(); ++k) diff += got.data()[k] != (*want)[k];
                rep.add(diff == 0 ? L::Ok : L::Fail, "bucket table equals the bundled bytes",
                        diff == 0 ? std::to_string(want->size()) + " bytes identical (36 buckets)" : std::to_string(diff) + " bytes differ");
            } catch (const std::exception& e) {
                rep.add(L::Fail, "bucket table", e.what());
            }
        }
    }
     
    if (index) {
        try {
            const world::Bytes head = mem.bytes(index, 0x20);
            const uint32_t n = head.u32(4);
            const uint32_t header = index + 8 + head.u32(8);
            const world::Bytes meta = mem.bytes(header - 4, 20);
            if (n <= off::TRANSFER_CONTEXT || meta.u32(0) != off::DEF_MARKER || meta.u32(4) != n || meta.u32(8) != off::INDEX_CLASS) {
                rep.add(L::Fail, "list/index table", "unexpected header (count " + std::to_string(n) + ")");
            } else {
                const uint32_t ctxTag = mem.u32(header + 16 + off::TRANSFER_CONTEXT * 12 + 8);
                rep.add(L::Info, "list row 26 (transfer context)", "tag " + hex8(ctxTag) + (ctxTag == 0x80A633CD ? " (as seen live on PS3)" : ""));
                try {
                    context = resolveInvestmentTag(mem, ctxTag, i.mode);
                } catch (const std::exception& e) {
                    rep.r.contextSource = "bundled";
                    rep.add(i.mode == 2 ? L::Info : L::Warn, "transfer context table not resident",
                            std::string(e.what()) + ": transfers use the bundled pairs (DIM360 does the same in mode 2)");
                }
                if (context) {
                    const world::Bytes body = mem.bytes(context, 122);
                    const auto pairs = parseContextPairs(body.data());
                    bool same = pairs.size() == cat.routes().size();
                    for (auto [s, d] : pairs) {
                        auto r = cat.route(s);
                        same &= r && *r == d;
                    }
                    rep.r.contextSource = "resident";
                    rep.add(same ? L::Ok : L::Fail, "transfer pairs equal the bundled pairs",
                            std::to_string(pairs.size()) + " pairs" + (same ? "" : " - DIFFERENT: do not trust withdraw routes"));
                }
            }
        } catch (const std::exception& e) {
            rep.add(L::Fail, "list/index table", e.what());
        }
    }
     
    if (defs) {
        try {
            DefinitionCache c;
            const auto hashes = readDefinitionHashes(mem, c);
            size_t diff = 0;
            for (size_t k = 0; k < hashes.size() && k < cat.size(); ++k) diff += hashes[k] != cat.items()[k].hash;
            const bool ok = hashes.size() == cat.size() && diff == 0;
            rep.add(ok ? L::Ok : L::Fail, "definition table",
                    std::to_string(hashes.size()) + " definitions (bundled " + std::to_string(cat.size()) + "), " + std::to_string(diff) +
                        " hashes differ from the item catalogue");
        } catch (const std::exception& e) {
            rep.add(L::Fail, "definition table", e.what());
        }
    }
     
    try {
        const uint64_t a = mem.bytes(addr::ENGINE_CLOCK, 8).u64(0);
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        const uint64_t b = mem.bytes(addr::ENGINE_CLOCK, 8).u64(0);
        rep.add(b != a ? L::Ok : L::Warn, "engine clock 0x1A1D678", b != a ? "ticking" : "did not advance in 120 ms (paused or loading?)");
    } catch (const std::exception& e) {
        rep.add(L::Warn, "engine clock", e.what());
    }
    size_t fails = 0, warns = 0;
    for (const VerifyLine& l : rep.r.lines) {
        fails += l.level == L::Fail;
        warns += l.level == L::Warn;
    }
    rep.r.ok = fails == 0;
    rep.r.verdict = fails ? "verdict: " + std::to_string(fails) + " check(s) FAILED - do not trust transfers until they are explained"
                          : "verdict: native catalogue verified (" + std::to_string(warns) + " warning(s))";
    return rep.r;
}

}   
