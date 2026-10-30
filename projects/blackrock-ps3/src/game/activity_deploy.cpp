#include "game/activity_deploy.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <optional>
#include <sstream>

#include "core/error.h"
#include "game/activity_addr.h"
#include "game/activity_monitor.h"
#include "game/activity_session.h"

namespace brps3::activity {

namespace {

std::string hexBytes(const std::vector<uint8_t>& b) {
    std::string s;
    char t[4];
    for (uint8_t v : b) {
        std::snprintf(t, sizeof t, "%02X", v);
        s += t;
    }
    return s;
}

std::vector<uint8_t> parseHexBytes(const std::string& s) {
    if (s.size() % 2) throw Error("odd hex length");
    std::vector<uint8_t> out;
    for (size_t i = 0; i < s.size(); i += 2) out.push_back(uint8_t(world::parseHex(s.substr(i, 2), "byte")));
    return out;
}

 
struct FieldSpan {
    uint32_t lo, hi;
};
std::vector<FieldSpan> sessionSpans(world::Reader& mem) {
    std::vector<FieldSpan> spans;
    const SessionRead sr = readSessionFields(mem);
    if (!sr.ok) return spans;
    for (const SessionSlot& slot : {sr.slots[0], sr.slots[1]}) {
        if (!slot.ok) continue;
        for (const FieldValue& f : slot.fields) spans.push_back({f.addr, f.addr + 0x180});    
    }
    return spans;
}

}   

MirrorScan scanMirrors(world::Reader& mem, uint32_t id, const std::vector<HeapRange>& ranges,
                       const std::function<void(uint64_t, uint64_t)>& progress, const std::function<bool()>& cancelled) {
    MirrorScan s;
    s.id = id;
     
    try {
        s.state = readWorldSnapshot(mem).state;
    } catch (const std::exception&) {
    }
    try {
        s.root = mem.u32(addr::SESSION_ROOT_PTR);
    } catch (const std::exception&) {
    }
    std::vector<FieldSpan> spans;
    try {
        const DescriptorRead d = readCurrentActivity(mem);
        if (d.ok) s.activity = d.name;
        spans = sessionSpans(mem);
    } catch (const std::exception&) {
    }

    uint64_t total = 0, done = 0;
    for (const HeapRange& r : ranges) total += r.hi - r.lo;
    const std::vector<uint8_t> pattern = {uint8_t(id >> 24), uint8_t(id >> 16), uint8_t(id >> 8), uint8_t(id),
                                          0x01, 0x00, 0x00, 0x00};
    for (const HeapRange& range : ranges) {
        for (uint32_t a = range.lo; a < range.hi;) {
            if (cancelled && cancelled()) {
                s.cancelled = true;
                return s;
            }
            const uint32_t want = std::min(kScanChunk + 24u, range.hi - a);    
            std::vector<uint8_t> blk;
            try {
                blk = mem.read(a, want);
            } catch (const std::exception&) {
                a += kScanChunk;
                done += kScanChunk;
                if (progress) progress(done, total);
                continue;
            }
            for (size_t i = 0; i + pattern.size() <= blk.size() && i < kScanChunk; ++i) {
                if (std::memcmp(blk.data() + i, pattern.data(), pattern.size()) != 0) continue;
                const uint32_t ea = a + uint32_t(i);
                if (ea % 4) continue;
                MirrorHit h;
                h.addr = ea;
                 
                 
                const uint32_t ctxLo = ea - 8;
                std::optional<world::Bytes> ctx;
                if (i >= 8 && i + 24 <= blk.size())
                    ctx = world::Bytes(ctxLo, std::vector<uint8_t>(blk.begin() + std::ptrdiff_t(i - 8), blk.begin() + std::ptrdiff_t(i + 24)));
                else ctx = mem.tryBytes(ctxLo, 32);
                if (ctx) {
                    h.context = hexBytes(ctx->data());
                    h.fnvBefore = ctx->u32(4) == kFnvBasis;
                    h.tailZero = ctx->u64(0x10) == 0;    
                }
                for (const FieldSpan& span : spans)
                    if (ea >= span.lo && ea < span.hi) h.inSession = true;
                s.hits.push_back(std::move(h));
            }
            a += kScanChunk;
            done += kScanChunk;
            if (progress) progress(std::min(done, total), total);
        }
    }
    return s;
}

std::vector<HeapRange> parseRanges(const std::string& text) {
    std::vector<HeapRange> out;
    std::string cur;
    uint64_t total = 0;
    auto flush = [&] {
        if (cur.empty()) return;
        const size_t dash = cur.find('-');
        if (dash == std::string::npos) throw Error("range \"" + cur + "\" must be LO-HI (hex)");
        HeapRange r{world::parseHex(cur.substr(0, dash), "range start"), world::parseHex(cur.substr(dash + 1), "range end")};
        if (r.hi <= r.lo) throw Error("range " + cur + " is empty");
        if (!world::heap(r.lo, r.hi - r.lo)) throw Error("range " + cur + " is outside the heap (0x30000000..0x80000000)");
        total += r.hi - r.lo;
        out.push_back(r);
        cur.clear();
    };
    for (char ch : text) {
        if (ch == ' ' || ch == ',' || ch == ';' || ch == '\t' || ch == '\n' || ch == '\r') flush();
        else cur += ch;
    }
    flush();
    if (out.empty()) throw Error("no scan range given");
    if (total > kMaxScanBytes) throw Error("scan ranges cover " + std::to_string(total >> 20) + " MiB; keep them under " + std::to_string(kMaxScanBytes >> 20) + " MiB");
    return out;
}

std::string rangesText(const std::vector<HeapRange>& ranges) {
    std::string s;
    for (const HeapRange& r : ranges) s += (s.empty() ? "" : " ") + world::hex8(r.lo) + "-" + world::hex8(r.hi);
    return s;
}

std::vector<uint32_t> intersect(const MirrorScan& a, const MirrorScan& b) {
    std::vector<uint32_t> out;
    for (const MirrorHit& ha : a.hits)
        for (const MirrorHit& hb : b.hits)
            if (ha.addr == hb.addr) {
                out.push_back(ha.addr);
                break;
            }
    return out;
}

std::string toJson(const MirrorScan& s) {
    std::ostringstream o;
    std::string act;
    for (char ch : s.activity)
        if (ch != '"' && ch != '\\' && uint8_t(ch) >= 0x20) act += ch;    
    o << "{\"id\":" << s.id << ",\"state\":" << int32_t(s.state) << ",\"activity\":\"" << act << "\",\"root\":" << s.root
      << ",\"cancelled\":" << (s.cancelled ? 1 : 0) << ",\"hits\":[";
    for (size_t i = 0; i < s.hits.size(); ++i) {
        const MirrorHit& h = s.hits[i];
        o << (i ? "," : "") << "{\"addr\":" << h.addr << ",\"fnv\":" << (h.fnvBefore ? 1 : 0) << ",\"tail0\":" << (h.tailZero ? 1 : 0)
          << ",\"session\":" << (h.inSession ? 1 : 0) << ",\"ctx\":\"" << h.context << "\"}";
    }
    o << "]}";
    return o.str();
}

namespace {

 
long long num(const std::string& t, const std::string& key) {
    const size_t k = t.find("\"" + key + "\":");
    if (k == std::string::npos) throw Error("missing " + key + " in scan file");
    return std::strtoll(t.c_str() + k + key.size() + 3, nullptr, 10);
}
std::string strField(const std::string& t, const std::string& key) {
    const size_t k = t.find("\"" + key + "\":\"");
    if (k == std::string::npos) return {};
    const size_t start = k + key.size() + 4;
    return t.substr(start, t.find('"', start) - start);
}

}   

MirrorScan fromJson(const std::string& text) {
    MirrorScan s;
    s.id = uint32_t(num(text, "id"));
    s.state = uint32_t(num(text, "state"));
    s.root = uint32_t(num(text, "root"));
    s.activity = strField(text, "activity");
    const size_t arr = text.find("\"hits\":[");
    if (arr == std::string::npos) throw Error("missing hits array in scan file");
    size_t pos = arr;
    while ((pos = text.find("{\"addr\":", pos)) != std::string::npos) {
        MirrorHit h;
        h.addr = uint32_t(std::strtoll(text.c_str() + pos + 8, nullptr, 10));
        const size_t end = text.find('}', pos);
        const std::string obj = text.substr(pos, end - pos);
        h.fnvBefore = num(obj, "fnv") != 0;
        h.tailZero = num(obj, "tail0") != 0;
        h.inSession = num(obj, "session") != 0;
        h.context = strField(obj, "ctx");
        s.hits.push_back(std::move(h));
        pos = end + 1;
    }
    return s;
}

}   
