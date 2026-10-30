#include "game/world_registry.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>
#include <sstream>

namespace brps3::world {

namespace {

std::string readText(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw Error("missing data file " + p.string());
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    if (s.rfind("\xEF\xBB\xBF", 0) == 0) s = s.substr(3);
    return s;
}

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (char c : text) {
        if (c == '"') quoted = !quoted;
        if ((c == '\n') && !quoted) {
            if (!cur.empty() && cur.back() == '\r') cur.pop_back();
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

uint32_t num(const std::string& s) {
    if (s.empty()) return 0;
    try {
        return uint32_t(std::stoul(s, nullptr, 0));
    } catch (const std::exception&) {
        throw Error("not a number: " + s);
    }
}

bool sameText(const std::string& a, const std::string& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return std::tolower((unsigned char)x) == std::tolower((unsigned char)y); });
}

template <class Row, class Fn>
std::vector<Row> parseCsv(const std::string& text, const char* what, Fn make) {
    auto ls = lines(text);
    if (ls.empty()) throw Error(std::string(what) + " catalogue is empty");
    const auto header = splitCsvLine(ls[0]);
    auto col = [&](const char* name) {
        auto it = std::find(header.begin(), header.end(), name);
        if (it == header.end()) throw Error(std::string(what) + " catalogue has no column " + name);
        return size_t(it - header.begin());
    };
    std::vector<Row> out;
    for (size_t i = 1; i < ls.size(); ++i) {
        if (ls[i].empty()) continue;
        auto cells = splitCsvLine(ls[i]);
        cells.resize(std::max(cells.size(), header.size()));
        try {
            if (auto row = make(cells, col)) out.push_back(std::move(*row));
        } catch (const Error& e) {
            throw Error(std::string(what) + " catalogue row " + std::to_string(i + 1) + ": " + e.what());
        }
    }
    return out;
}

struct Catalogs {
    std::mutex m;
    bool poolsLoaded = false, globalsLoaded = false;
    std::vector<PoolInfo> pools;
    std::vector<GlobalInfo> globals;
};
Catalogs& cats() {
    static Catalogs c;
    return c;
}

}   

std::vector<std::string> splitCsvLine(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '"' && i + 1 < line.size() && line[i + 1] == '"') {
                cur += '"';
                ++i;
            } else if (c == '"') {
                quoted = false;
            } else {
                cur += c;
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == ',') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

std::vector<PoolInfo> parsePoolCsv(const std::string& text) {
    return parseCsv<PoolInfo>(text, "pool", [](const std::vector<std::string>& c, auto col) -> std::optional<PoolInfo> {
        const std::string addr = c[col("PS3 address")];
        if (addr.empty() || addr == "-") return std::nullopt;
        PoolInfo p;
        p.name = c[col("Name")];
        p.category = c[col("Category")];
        p.confidence = c[col("Confidence")];
        p.ps3 = parseHex(addr, "PS3 address");
        p.elemSize = num(c[col("ElemSize")]);
        p.capacity = num(c[col("Capacity")]);
        p.align = num(c[col("Align")]);
        return p;
    });
}

std::vector<GlobalInfo> parseGlobalCsv(const std::string& text) {
    return parseCsv<GlobalInfo>(text, "globals", [](const std::vector<std::string>& c, auto col) -> std::optional<GlobalInfo> {
        const std::string addr = c[col("PS3 address")];
        if (addr.empty() || addr == "-") return std::nullopt;
        GlobalInfo g;
        g.name = c[col("Name")];
        g.confidence = c[col("Confidence")];
        g.slot = parseHex(addr, "PS3 address");
        g.size = num(c[col("Size")]);
        g.align = num(c[col("Align")]);
        return g;
    });
}

const std::vector<PoolInfo>& poolCatalog() {
    Catalogs& c = cats();
    std::lock_guard<std::mutex> lock(c.m);
    if (!c.poolsLoaded) {
        c.pools = parsePoolCsv(readText(dataFile("world/pools.csv")));
        c.poolsLoaded = true;
    }
    return c.pools;
}

const std::vector<GlobalInfo>& globalCatalog() {
    Catalogs& c = cats();
    std::lock_guard<std::mutex> lock(c.m);
    if (!c.globalsLoaded) {
        c.globals = parseGlobalCsv(readText(dataFile("world/globals.csv")));
        c.globalsLoaded = true;
    }
    return c.globals;
}

const PoolInfo* findPool(const std::string& name) {
    for (const auto& p : poolCatalog())
        if (sameText(p.name, name)) return &p;
    return nullptr;
}

const GlobalInfo* findGlobal(const std::string& name) {
    for (const auto& g : globalCatalog())
        if (sameText(g.name, name)) return &g;
    return nullptr;
}

PoolStatus readPoolStatus(Reader& mem, const PoolInfo& info) {
    PoolStatus s;
    s.info = info;
    s.header = readPool(mem, info.ps3);
    s.plausible = s.header.plausible();
    s.strideOk = strideMatches(s.header.stride, info.elemSize, info.align, s.header.identityOffset);
    s.capacityOk = info.capacity == 0 || s.header.capacity == info.capacity;
    return s;
}

BlockHeader parseBlockHeader(const Bytes& b) {
    BlockHeader h;
    h.index = b.u32(0);
    h.hash = b.u32(4);
    h.region = b.u8(8);
    h.category = b.u8(9);
    h.owner = b.u8(10);
    h.flags = b.u32(12);
    return h;
}

GlobalBlock readGlobal(Reader& mem, uint32_t slot) {
    GlobalBlock g;
    g.slot = slot;
    g.block = mem.u32(slot);
    g.initialised = validHandle(g.block) && g.block >= 16 && mapped(g.block - 16, 16);
    if (g.initialised) {
        if (auto b = mem.tryBytes(g.block - 16, 16)) g.header = parseBlockHeader(*b);
        else g.initialised = false;
    }
    return g;
}

std::vector<RegistryRow> parseGlobalsRegistry(const Bytes& reg) {
    std::vector<RegistryRow> out;
    const uint32_t count = std::min<uint32_t>(reg.u32(0x30), 256);
    for (uint32_t i = 0; i < count; ++i) {
        RegistryRow r;
        r.index = i;
        r.handle = reg.u16(0x34 + i * 16);
        r.offset = reg.u32(0x34 + i * 16 + 4);
        r.size = reg.u32(0x34 + i * 16 + 8);
        r.owner = reg.u32(0x34 + i * 16 + 12);
        r.slot = reg.u32(0x1034 + i * 8);
        out.push_back(r);
    }
    return out;
}

std::vector<RegistryRow> readGlobalsRegistry(Reader& mem) { return parseGlobalsRegistry(mem.bytes(addr::GLOBALS_REGISTRY, kRegistryReadSize)); }

}   
