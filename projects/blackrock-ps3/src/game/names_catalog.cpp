#include "game/names_catalog.h"

#include <unordered_map>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "core/error.h"
#include "game/world_addr.h"
#include "game/world_mem.h"

namespace brps3::names {

namespace {

constexpr const char* kDot = " \xC2\xB7 ";    

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string hex8(uint32_t v) {
    char b[12];
    std::snprintf(b, sizeof b, "%08X", v);
    return b;
}

std::string readText(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw Error("missing data file " + p.string());
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    if (s.rfind("\xEF\xBB\xBF", 0) == 0) s = s.substr(3);
    return s;
}

std::vector<std::string> splitTab(const std::string& line) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        const size_t t = line.find('\t', start);
        out.push_back(line.substr(start, t == std::string::npos ? std::string::npos : t - start));
        if (t == std::string::npos) break;
        start = t + 1;
    }
    return out;
}

 
template <class Fn>
void forEachRow(const std::string& tsv, const char* what, const std::vector<const char*>& need, Fn row) {
    std::istringstream in(tsv);
    std::string line;
    if (!std::getline(in, line)) throw Error(std::string(what) + ": empty file");
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto header = splitTab(line);
    std::vector<size_t> idx;
    for (const char* n : need) {
        auto it = std::find(header.begin(), header.end(), n);
        if (it == header.end()) throw Error(std::string(what) + ": no column " + n);
        idx.push_back(size_t(it - header.begin()));
    }
    size_t lineNo = 1;
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        auto cells = splitTab(line);
        cells.resize(std::max(cells.size(), header.size()));
        std::vector<std::string> picked;
        for (size_t i : idx) picked.push_back(std::move(cells[i]));
        try {
            row(picked);
        } catch (const std::exception& e) {
            throw Error(std::string(what) + " line " + std::to_string(lineNo) + ": " + e.what());
        }
    }
}

uint32_t hexField(const std::string& s) {
    if (s.empty()) return 0;
    return world::parseHex(s, "hex field");
}

uint32_t decField(const std::string& s) {
    if (s.empty()) return 0;
    size_t used = 0;
    const unsigned long v = std::stoul(s, &used, 10);
    if (used != s.size()) throw Error("not a number: " + s);
    return uint32_t(v);
}

 
std::vector<std::string> words(const std::string& query) {
    std::vector<std::string> out;
    std::istringstream in(lower(query));
    std::string w;
    while (in >> w) {
        if (w.size() > 2 && w[0] == '0' && w[1] == 'x') w = w.substr(2);
        if (!w.empty()) out.push_back(w);
    }
    return out;
}

bool matches(const std::string& hay, const std::vector<std::string>& ws) {
    for (const auto& w : ws)
        if (hay.find(w) == std::string::npos) return false;
    return true;
}

}   

 

uint32_t fnv1(std::string_view s) {
    uint32_t h = kFnvBasis;
    for (unsigned char c : s) h = (h * kFnvPrime) ^ c;
    return h;
}

uint32_t fnv1a(std::string_view s) {
    uint32_t h = kFnvBasis;
    for (unsigned char c : s) h = (h ^ c) * kFnvPrime;
    return h;
}

 

void packageParts(const std::string& pkg, std::string& location, std::string& activity) {
    location.clear();
    activity.clear();
    if (pkg.rfind("360_", 0) != 0 && pkg.rfind("ps3_", 0) != 0) return;
    std::vector<std::string> parts;
    std::string cur;
    for (char c : pkg) {
        if (c == '_') {
            parts.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    parts.push_back(cur);
    if (parts.size() > 1) location = parts[1];
    if (parts.size() > 2) {
        const std::string& p2 = parts[2];
        const bool hexId = p2.size() == 4 && p2.find_first_not_of("0123456789abcdef") == std::string::npos;
        if (!hexId) activity = p2;
    }
}

std::string locationName(const std::string& loc) {
    static const std::pair<const char*, const char*> kNames[] = {
        {"cosmo", "Cosmodrome"}, {"moon", "Moon"},       {"venus", "Venus"},         {"mars", "Mars"},          {"city", "Tower / City"},
        {"pvp", "Crucible"},     {"race", "SRL"},        {"arch", "Architecture (shared)"}, {"globals", "Globals"}, {"gear", "Gear"},
        {"investment", "Investment"}, {"cinematic", "Cinematic"}, {"ui", "UI"},     {"env", "Environment"},    {"client", "Client"},
        {"shared", "Shared"},    {"bootstrap", "Bootstrap"}, {"orbit", "Orbit"},
    };
    for (const auto& [key, name] : kNames)
        if (loc == key) return name;
    return loc;
}

std::string Entity::location() const {
    std::string l, a;
    packageParts(package, l, a);
    return l;
}

std::string Entity::activity() const {
    std::string l, a;
    packageParts(package, l, a);
    return a;
}

 

EntityCatalog EntityCatalog::parse(const std::string& tsv) {
    EntityCatalog c;
    forEachRow(tsv, "entities.tsv", {"tag", "type", "package", "size", "name", "confidence", "aliases"}, [&](std::vector<std::string>& f) {
        Entity e;
        e.tag = hexField(f[0]);
        const uint32_t type = decField(f[1]);
        if (!e.tag || type > 255) throw Error("bad tag or type");
        e.type = uint8_t(type);
        e.package = std::move(f[2]);
        e.size = decField(f[3]);
        e.name = std::move(f[4]);
        e.confidence = std::move(f[5]);
        e.aliases = std::move(f[6]);
        c.rows_.push_back(std::move(e));
    });
    std::sort(c.rows_.begin(), c.rows_.end(), [](const Entity& a, const Entity& b) { return a.tag < b.tag; });
    c.rows_.erase(std::unique(c.rows_.begin(), c.rows_.end(), [](const Entity& a, const Entity& b) { return a.tag == b.tag; }), c.rows_.end());
    c.hay_.reserve(c.rows_.size());
    for (const Entity& e : c.rows_) {
        if (e.hasName()) ++c.named_;
        const std::string loc = e.location();
        c.hay_.push_back(lower(e.name + "|" + e.aliases + "|" + e.package + "|" + locationName(loc) + "|" + world::typeName(e.type) + "|" +
                               hex8(e.tag)));
    }
    return c;
}

const Entity* EntityCatalog::find(uint32_t tag) const {
    auto it = std::lower_bound(rows_.begin(), rows_.end(), tag, [](const Entity& e, uint32_t t) { return e.tag < t; });
    return it != rows_.end() && it->tag == tag ? &*it : nullptr;
}

std::vector<const Entity*> EntityCatalog::search(const std::string& query, int type, size_t limit) const {
    const auto ws = words(query);
    std::vector<const Entity*> named, unnamed;
    for (size_t i = 0; i < rows_.size(); ++i) {
        const Entity& e = rows_[i];
        if (type >= 0 && e.type != type) continue;
        if (!matches(hay_[i], ws)) continue;
        (e.hasName() ? named : unnamed).push_back(&e);
        if (limit && named.size() >= limit) break;
    }
    named.insert(named.end(), unnamed.begin(), unnamed.end());
    if (limit && named.size() > limit) named.resize(limit);
    return named;
}

const EntityCatalog& entities() {
    static const EntityCatalog c = [] {
        try {
            return EntityCatalog::parse(readText(world::dataFile("names/entities.tsv")));
        } catch (const std::exception& e) {
            EntityCatalog empty;
            empty.error = e.what();
            return empty;
        }
    }();
    return c;
}

 

const char* rarityName(uint8_t r) {
    switch (r) {
    case 1: return "Common";
    case 2: return "Uncommon";
    case 3: return "Rare";
    case 4: return "Legendary";
    case 5: return "Exotic";
    case 0: return "Tier 0";
    case 6: return "Tier 6";
    case 255: return "None";
    default: return "Tier ?";
    }
}

ItemCatalog ItemCatalog::parse(const std::string& tsv) {
    ItemCatalog c;
    forEachRow(tsv, "items.tsv", {"hash", "item_tag", "display_tag", "rarity", "type", "name"}, [&](std::vector<std::string>& f) {
        Item it;
        it.hash = hexField(f[0]);
        it.itemTag = hexField(f[1]);
        it.displayTag = hexField(f[2]);
        const uint32_t r = decField(f[3]);
        if (r > 255) throw Error("bad rarity");
        it.rarity = uint8_t(r);
        it.type = std::move(f[4]);
        it.name = std::move(f[5]);
        c.rows_.push_back(std::move(it));
    });
    std::sort(c.rows_.begin(), c.rows_.end(), [](const Item& a, const Item& b) { return a.hash < b.hash; });
    for (const Item& it : c.rows_)
        c.hay_.push_back(lower(it.name + "|" + it.type + "|" + rarityName(it.rarity) + "|" + hex8(it.hash) + "|" + hex8(it.itemTag) + "|" +
                               hex8(it.displayTag)));
    return c;
}

const Item* ItemCatalog::find(uint32_t hash) const {
    auto it = std::lower_bound(rows_.begin(), rows_.end(), hash, [](const Item& e, uint32_t h) { return e.hash < h; });
    return it != rows_.end() && it->hash == hash ? &*it : nullptr;
}

const Item* ItemCatalog::findByTag(uint32_t tag) const {
    for (const Item& it : rows_)
        if (it.itemTag == tag) return &it;
    for (const Item& it : rows_)
        if (it.displayTag == tag) return &it;
    return nullptr;
}

std::vector<const Item*> ItemCatalog::search(const std::string& query, size_t limit) const {
    const auto ws = words(query);
    std::vector<const Item*> out;
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (!matches(hay_[i], ws)) continue;
        out.push_back(&rows_[i]);
        if (limit && out.size() >= limit) break;
    }
    return out;
}

const ItemCatalog& items() {
    static const ItemCatalog c = [] {
        try {
            return ItemCatalog::parse(readText(world::dataFile("names/items.tsv")));
        } catch (const std::exception& e) {
            ItemCatalog empty;
            empty.error = e.what();
            return empty;
        }
    }();
    return c;
}

 

std::vector<std::pair<uint32_t, Label>> Labels::parse(const std::string& tsv) {
    std::vector<std::pair<uint32_t, Label>> out;
    forEachRow(tsv, "labels", {"tag", "name", "notes", "verified"}, [&](std::vector<std::string>& f) {
        const uint32_t tag = hexField(f[0]);
        if (!tag) throw Error("bad tag");
        out.push_back({tag, Label{std::move(f[1]), std::move(f[2]), f[3] == "1"}});
    });
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return out;
}

std::string Labels::serialise(const std::vector<std::pair<uint32_t, Label>>& rows) {
    auto clean = [](std::string s) {
        for (char& c : s)
            if (c == '\t' || c == '\n' || c == '\r') c = ' ';
        return s;
    };
    std::string out = "tag\tname\tnotes\tverified\n";
    for (const auto& [tag, l] : rows) out += hex8(tag) + "\t" + clean(l.name) + "\t" + clean(l.notes) + "\t" + (l.verified ? "1" : "0") + "\n";
    return out;
}

Labels& Labels::get() {
    static Labels l;
    return l;
}

void Labels::reloadFrom(const std::filesystem::path& userPath, const std::filesystem::path& seedPath) {
    std::lock_guard<std::mutex> lock(m_);
    userPath_ = userPath;
    seedPath_ = seedPath;
    loaded_ = false;
    loadLocked();
}

void Labels::loadLocked() {
    if (loaded_) return;
    loaded_ = true;
    error_.clear();
    rows_.clear();
    if (userPath_.empty()) userPath_ = state_dir() / "names_user.tsv";
    if (seedPath_.empty()) seedPath_ = world::dataFile("names/labels.tsv");
    std::error_code ec;
    const bool user = std::filesystem::exists(userPath_, ec);
    try {
        rows_ = parse(readText(user ? userPath_ : seedPath_));
    } catch (const std::exception& e) {
        error_ = e.what();
    }
}

void Labels::saveLocked() {
    try {
        const std::filesystem::path& path = userPath_;
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
        std::filesystem::path tmp = path;
        tmp += ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f) throw Error("cannot write " + tmp.string());
            f << serialise(rows_);
            if (!f) throw Error("cannot write " + tmp.string());
        }
        std::filesystem::rename(tmp, path);
        error_.clear();
    } catch (const std::exception& e) {
        error_ = std::string("labels not saved: ") + e.what();
    }
}

std::optional<Label> Labels::find(uint32_t tag) const {
    std::lock_guard<std::mutex> lock(m_);
    const_cast<Labels*>(this)->loadLocked();
    auto it = std::lower_bound(rows_.begin(), rows_.end(), tag, [](const auto& r, uint32_t t) { return r.first < t; });
    if (it == rows_.end() || it->first != tag) return std::nullopt;
    return it->second;
}

void Labels::set(uint32_t tag, const Label& l) {
    std::lock_guard<std::mutex> lock(m_);
    loadLocked();
    auto it = std::lower_bound(rows_.begin(), rows_.end(), tag, [](const auto& r, uint32_t t) { return r.first < t; });
    if (it != rows_.end() && it->first == tag) it->second = l;
    else rows_.insert(it, {tag, l});
    saveLocked();
}

void Labels::remove(uint32_t tag) {
    std::lock_guard<std::mutex> lock(m_);
    loadLocked();
    auto it = std::lower_bound(rows_.begin(), rows_.end(), tag, [](const auto& r, uint32_t t) { return r.first < t; });
    if (it == rows_.end() || it->first != tag) return;
    rows_.erase(it);
    saveLocked();
}

std::vector<std::pair<uint32_t, Label>> Labels::all() const {
    std::lock_guard<std::mutex> lock(m_);
    const_cast<Labels*>(this)->loadLocked();
    return rows_;
}

size_t Labels::size() const {
    std::lock_guard<std::mutex> lock(m_);
    const_cast<Labels*>(this)->loadLocked();
    return rows_.size();
}

std::string Labels::error() const {
    std::lock_guard<std::mutex> lock(m_);
    return error_;
}

 

std::string tagName(uint32_t tag) {
    if (auto l = Labels::get().find(tag); l && !l->name.empty()) return l->name;
    if (const Entity* e = entities().find(fromPs3Tag(tag)); e && e->hasName()) return e->name;
    return {};
}

std::string tagLabel(uint32_t tag) {
    std::string n = tagName(tag);
    if (!n.empty()) return n;
    if (const Entity* e = entities().find(fromPs3Tag(tag))) {
        const std::string loc = e->location();
        return std::string(world::typeName(e->type)) + kDot + (loc.empty() ? e->package : loc) + kDot + hex8(tag);
    }
    return hex8(tag);
}

std::string itemLabel(uint32_t hash) {
    if (const Item* it = items().find(hash); it && !it->name.empty()) return it->name;
    return "item " + hex8(hash);
}

 

namespace {
struct TagMap {
    std::unordered_map<uint32_t, uint32_t> toPs3, fromPs3;
};
const TagMap& tagMap() {
    static const TagMap m = [] {
        TagMap t;
        try {
            const std::string text = readText(world::dataFile("names/tagmap.tsv"));
            size_t pos = text.find('\n');                       
            while (pos != std::string::npos && pos + 1 < text.size()) {
                const size_t end = text.find('\n', pos + 1);
                const std::string line = text.substr(pos + 1, end == std::string::npos ? std::string::npos : end - pos - 1);
                pos = end;
                if (line.size() < 17) continue;
                const uint32_t x = uint32_t(std::stoul(line.substr(0, 8), nullptr, 16));
                const uint32_t p = uint32_t(std::stoul(line.substr(9, 8), nullptr, 16));
                t.toPs3[x] = p;
                t.fromPs3[p] = x;
            }
        } catch (...) {
             
        }
        return t;
    }();
    return m;
}
}   

uint32_t toPs3Tag(uint32_t catalogTag) {
    const auto& m = tagMap().toPs3;
    auto it = m.find(catalogTag);
    return it == m.end() ? catalogTag : it->second;
}

uint32_t fromPs3Tag(uint32_t ps3Tag) {
    const auto& m = tagMap().fromPs3;
    auto it = m.find(ps3Tag);
    return it == m.end() ? ps3Tag : it->second;
}

bool hasPs3Tag(uint32_t catalogTag) { return tagMap().toPs3.count(catalogTag) != 0; }

const TranslationStatus& translationStatus() {
    static const TranslationStatus s{
        "map: data/names/tagmap.tsv (package entry tables aligned; unlisted tags pass through)",
        "partial (x.catalog_translation)",
        "lab: the encounter catalogue matched live; inventory / loot-progress: some investment entries looked shifted +1/+2 in one "
        "comparison; PS3 package files are not on the PC",
        {{0x80A5F23F, 0x80A5F241}, {0x80A5E1E3, 0x80A5E1E4}, {0x80A633CB, 0x80A633CD}},
    };
    return s;
}

TranslationReport compareLiveTypes(const EntityCatalog& catalog, const std::vector<std::pair<uint32_t, uint8_t>>& live) {
    TranslationReport r;
    r.live = live.size();
    for (const auto& [tag, type] : live) {
        const Entity* e = catalog.find(fromPs3Tag(tag));
        if (!e) continue;
        ++r.inCatalog;
        if (e->type == type) {
            ++r.typeMatch;
        } else {
            ++r.typeMismatch;
            if (r.mismatches.size() < 20) r.mismatches.push_back({tag, {e->type, type}});
        }
    }
    char buf[256];
    if (r.inCatalog == 0) {
        std::snprintf(buf, sizeof buf, "no live tag is in the catalogue (%zu live): cannot judge", r.live);
    } else {
        const double pct = 100.0 * double(r.typeMatch) / double(r.inCatalog);
        std::snprintf(buf, sizeof buf, "%zu of %zu catalogued live tags have the catalogue's type (%.1f%%): %s", r.typeMatch, r.inCatalog, pct,
                      pct >= 99.0   ? "ids look identical"
                      : pct >= 90.0 ? "ids mostly identical; check the mismatches"
                                    : "ids look SHIFTED: do not trust catalogue tags on PS3");
    }
    r.verdict = buf;
    return r;
}

}   
