#include "game/hopons_catalog.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>
#include <sstream>

#include "core/error.h"
#include "game/hopons_mem.h"
#include "game/names_catalog.h"
#include "game/world_mem.h"

namespace brps3::hopons {

namespace {

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        const size_t tab = line.find('\t', start);
        out.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
        if (tab == std::string::npos) break;
        start = tab + 1;
    }
    if (!out.empty() && !out.back().empty() && out.back().back() == '\r') out.back().pop_back();
    return out;
}

 
uint32_t hexField(const std::string& s, const char* what, size_t line) {
    if (s == "-") return 0xFFFFFFFFu;
    if (s.empty() || s.size() > 8) throw Error(std::string("line ") + std::to_string(line) + ": bad " + what + " '" + s + "'");
    uint32_t v = 0;
    for (char c : s) {
        const int d = std::isdigit(static_cast<unsigned char>(c)) ? c - '0'
                      : (c >= 'A' && c <= 'F')                     ? c - 'A' + 10
                      : (c >= 'a' && c <= 'f')                     ? c - 'a' + 10
                                                                   : -1;
        if (d < 0) throw Error(std::string("line ") + std::to_string(line) + ": bad " + what + " '" + s + "'");
        v = v << 4 | uint32_t(d);
    }
    return v;
}

template <class F>
void eachRow(const std::string& tsv, size_t minFields, F&& fn) {
    std::istringstream in(tsv);
    std::string line;
    size_t n = 0;
    while (std::getline(in, line)) {
        ++n;
        if (n == 1 || line.empty() || line == "\r") continue;    
        auto f = splitTabs(line);
        if (f.size() < minFields) throw Error("line " + std::to_string(n) + ": " + std::to_string(f.size()) + " fields, expected " + std::to_string(minFields));
        fn(f, n);
    }
}

std::string readData(const std::string& rel) {
    const auto path = world::dataFile(rel);
    std::ifstream f(path, std::ios::binary);
    if (!f) throw Error("missing " + path.string());
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

}   

std::string lowerCopy(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> searchWords(const std::string& query) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : lowerCopy(query)) {
        if (c == ' ' || c == ',' || c == '\t') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    for (auto& w : out)
        if (w.size() > 2 && w[0] == '0' && w[1] == 'x') w = w.substr(2);
    return out;
}

bool matchesAll(const std::string& hay, const std::vector<std::string>& words) {
    for (const auto& w : words)
        if (hay.find(w) == std::string::npos) return false;
    return true;
}

 

const char* hopTypeName(uint8_t type) {
    switch (type) {
    case 20: return "Hop-on";
    case 21: return "Biped attachment";
    case 22: return "Weapon attachment";
    case 23: return "Ship attachment";
    default: return "Not a hop-on";
    }
}

const char* hopTypeShort(uint8_t type) {
    switch (type) {
    case 20: return "Hop-on";
    case 21: return "Biped";
    case 22: return "Weapon";
    case 23: return "Ship";
    default: return "?";
    }
}

uint8_t requiredTargetType(uint8_t defType) { return defType == 21 ? 9 : defType == 22 ? 11 : 0; }

std::string HopEntry::location() const {
    std::string loc, act;
    names::packageParts(package, loc, act);
    std::string where = names::locationName(loc);
    if (!where.empty() && where[0] >= 'a' && where[0] <= 'z') where[0] = char(where[0] - 'a' + 'A');    
    return where;
}

std::string HopEntry::activity() const {
    std::string loc, act;
    names::packageParts(package, loc, act);
    return act;
}

HopCatalog HopCatalog::parse(const std::string& tsv) {
    HopCatalog c;
    eachRow(tsv, 8, [&](const std::vector<std::string>& f, size_t line) {
        HopEntry e;
        e.tag = hexField(f[0], "tag", line);
        const int t = std::atoi(f[1].c_str());
        if (t < 20 || t > 23) throw Error("line " + std::to_string(line) + ": type " + f[1] + " is not a hop-on type");
        e.type = uint8_t(t);
        e.package = f[2];
        e.name = f[3];
        e.evidence = f[4];
        e.identifier = f[5];
        e.archetype = f[6];
        e.hints = f[7];
        c.rows_.push_back(std::move(e));
    });
    std::sort(c.rows_.begin(), c.rows_.end(), [](const HopEntry& a, const HopEntry& b) { return a.tag < b.tag; });
    for (size_t i = 0; i < c.rows_.size(); ++i) {
        const HopEntry& e = c.rows_[i];
        if (!c.byTag_.emplace(e.tag, i).second) throw Error("duplicate hop-on tag " + hex8(e.tag));
        if (!e.name.empty()) ++c.named_;
        c.hay_.push_back(lowerCopy(hex8(e.tag) + " " + e.name + " " + e.identifier + " " + e.archetype + " " + e.hints + " " + e.package + " " +
                                   e.location() + " " + hopTypeName(e.type)));
    }
    return c;
}

const HopEntry* HopCatalog::find(uint32_t tag) const {
    auto it = byTag_.find(tag);
    return it == byTag_.end() ? nullptr : &rows_[it->second];
}

size_t HopCatalog::countOfType(uint8_t type) const {
    return size_t(std::count_if(rows_.begin(), rows_.end(), [type](const HopEntry& e) { return e.type == type; }));
}

const HopCatalog& hopCatalog() {
    static HopCatalog c;
    static std::once_flag once;
    std::call_once(once, [] {
        try {
            c = HopCatalog::parse(readData("hopons/hopons.tsv"));
        } catch (const std::exception& e) {
            c = HopCatalog();
            c.error = e.what();
        }
    });
    return c;
}

 

namespace {
uint64_t keyOf(uint32_t blob, uint32_t binding, uint32_t cls) {
    uint64_t h = 1469598103934665603ull;
    for (uint32_t v : {blob, binding, cls}) {
        h ^= v;
        h *= 1099511628211ull;
    }
    return h;
}
}   

EncounterCatalog EncounterCatalog::parse(const std::string& rowsTsv, const std::string& classesTsv) {
    EncounterCatalog c;
    eachRow(classesTsv, 3, [&](const std::vector<std::string>& f, size_t line) {
        const uint32_t cls = hexField(f[0], "class", line);
        c.classes_.push_back({cls, f[1]});
        c.kinds_[cls] = f[1];
    });
    eachRow(rowsTsv, 13, [&](const std::vector<std::string>& f, size_t line) {
        EncounterDef d;
        d.root = hexField(f[0], "root", line);
        d.blob = hexField(f[1], "blob", line);
        d.cls = hexField(f[2], "class", line);
        d.binding = hexField(f[3], "binding", line);
        d.bindingClass = hexField(f[4], "binding class", line);
        d.size = hexField(f[5], "size", line);
        d.slotHash = hexField(f[6], "slot hash", line);
        d.slotId = hexField(f[7], "slot id", line);
        d.outputSchema = hexField(f[8], "output schema", line);
        d.inputSchema = hexField(f[9], "input schema", line);
        d.effect = hexField(f[10], "effect", line);
        d.name = f[11];
        d.evidence = f[12].empty() ? 'g' : f[12][0];
        if (d.size < 12 || d.size > 4096 || d.binding > 0x100000) throw Error("line " + std::to_string(line) + ": implausible size or binding");
        c.rows_.push_back(std::move(d));
    });
    for (size_t i = 0; i < c.rows_.size(); ++i) {
        const EncounterDef& d = c.rows_[i];
        if (!c.byRoot_.emplace(d.root, i).second) throw Error("duplicate encounter root " + hex8(d.root));
        c.byKey_[keyOf(d.blob, d.binding, d.bindingClass)].push_back(i);
        c.hay_.push_back(lowerCopy(d.name + " " + c.kind(d.cls) + " " + hex8(d.root) + " " + hex8(d.blob) + " " + hex8(d.cls)));
    }
    return c;
}

const EncounterDef* EncounterCatalog::match(uint32_t blob, uint32_t binding, uint32_t bindingClass) const {
    auto it = byKey_.find(keyOf(blob, binding, bindingClass));
    if (it == byKey_.end()) return nullptr;
    for (size_t i : it->second) {
        const EncounterDef& d = rows_[i];
        if (d.blob == blob && d.binding == binding && d.bindingClass == bindingClass) return &d;
    }
    return nullptr;
}

const EncounterDef* EncounterCatalog::byRoot(uint32_t root) const {
    auto it = byRoot_.find(root);
    return it == byRoot_.end() ? nullptr : &rows_[it->second];
}

std::string EncounterCatalog::kind(uint32_t cls) const {
    auto it = kinds_.find(cls);
    return it == kinds_.end() ? "Script class " + hex8(cls) : it->second;
}

std::vector<std::pair<uint32_t, std::string>> EncounterCatalog::classes() const { return classes_; }

const EncounterCatalog& encounterCatalog() {
    static EncounterCatalog c;
    static std::once_flag once;
    std::call_once(once, [] {
        try {
            c = EncounterCatalog::parse(readData("hopons/encounters.tsv"), readData("hopons/classes.tsv"));
        } catch (const std::exception& e) {
            c = EncounterCatalog();
            c.error = e.what();
        }
    });
    return c;
}

 

std::vector<uint32_t> SquadDefRow::entities() const {
    std::vector<uint32_t> out;
    for (const SquadSite& s : sites)
        if (std::find(out.begin(), out.end(), s.entity) == out.end()) out.push_back(s.entity);
    return out;
}

SquadCatalog SquadCatalog::parse(const std::string& tsv) {
    SquadCatalog c;
    eachRow(tsv, 4, [&](const std::vector<std::string>& f, size_t line) {
        SquadDefRow r;
        r.tag = hexField(f[0], "tag", line);
        r.name = f[1];
        r.size = hexField(f[2], "size", line);
        {
            size_t start = 0;
            if (!f[3].empty())
                for (;;) {
                    const size_t bar = f[3].find('|', start);
                    r.members.push_back(f[3].substr(start, bar == std::string::npos ? std::string::npos : bar - start));
                    if (bar == std::string::npos) break;
                    start = bar + 1;
                }
        }
        if (f.size() > 4 && !f[4].empty()) {
            std::istringstream in(f[4]);
            std::string item;
            while (std::getline(in, item, ',')) {
                unsigned m = 0, v = 0, k = 0;
                char off[16] = {}, ent[16] = {};
                if (std::sscanf(item.c_str(), "%u.%u.%u:%15[0-9A-Fa-f]:%15[0-9A-Fa-f]", &m, &v, &k, off, ent) != 5)
                    throw Error("line " + std::to_string(line) + ": bad site '" + item + "'");
                SquadSite s;
                s.member = uint8_t(m);
                s.variant = uint8_t(v);
                s.combatant = uint8_t(k);
                s.offset = hexField(off, "site offset", line);
                s.entity = hexField(ent, "site entity", line);
                r.sites.push_back(s);
            }
        }
        c.rows_.push_back(std::move(r));
    });
    std::sort(c.rows_.begin(), c.rows_.end(), [](const SquadDefRow& a, const SquadDefRow& b) { return a.tag < b.tag; });
    for (size_t i = 0; i < c.rows_.size(); ++i) {
        const SquadDefRow& r = c.rows_[i];
        c.byTag_.emplace(r.tag, i);
        std::string h = hex8(r.tag) + " " + r.name;
        for (const auto& m : r.members) h += " " + m;
        for (uint32_t e : r.entities()) h += " " + hex8(e) + " " + names::tagName(e);
        c.hay_.push_back(lowerCopy(h));
    }
    return c;
}

const SquadDefRow* SquadCatalog::find(uint32_t tag) const {
    auto it = byTag_.find(tag);
    return it == byTag_.end() ? nullptr : &rows_[it->second];
}

const SquadCatalog& squadCatalog() {
    static SquadCatalog c;
    static std::once_flag once;
    std::call_once(once, [] {
        try {
            c = SquadCatalog::parse(readData("hopons/squads.tsv"));
        } catch (const std::exception& e) {
            c = SquadCatalog();
            c.error = e.what();
        }
    });
    return c;
}

}   
