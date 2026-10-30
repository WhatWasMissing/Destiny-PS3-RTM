#include "game/activity_catalog.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>
#include <sstream>

#include "core/error.h"
#include "game/world_mem.h"

namespace brps3::activity {

namespace {

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        const size_t tab = line.find('\t', start);
        out.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
        if (tab == std::string::npos) break;
        start = tab + 1;
    }
    return out;
}

 
template <class F>
void eachRow(const std::string& text, size_t columns, const char* what, F&& fn) {
    std::istringstream in(text);
    std::string line;
    int n = 0;
    bool header = false;
    while (std::getline(in, line)) {
        ++n;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (!header) {
            header = true;    
            continue;
        }
        auto cols = splitTabs(line);
        if (cols.size() != columns)
            throw Error(std::string(what) + " line " + std::to_string(n) + ": expected " + std::to_string(columns) + " columns, got " +
                        std::to_string(cols.size()));
        try {
            fn(cols);
        } catch (const Error&) {
            throw;
        } catch (const std::exception& e) {
            throw Error(std::string(what) + " line " + std::to_string(n) + ": " + e.what());
        }
    }
}

std::string readText(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw Error("cannot read " + p.string());
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

}   

std::vector<DeployEntry> parseDeployTsv(const std::string& text) {
    std::vector<DeployEntry> out;
    eachRow(text, 3, "catalog.tsv", [&](const std::vector<std::string>& c) {
        DeployEntry e;
        e.id = uint32_t(std::stoul(c[0]));
        if (e.id > 0xFFFF) throw Error("activity id " + c[0] + " does not fit the u16 descriptor field");
        e.category = c[1];
        e.name = c[2];
        out.push_back(std::move(e));
    });
    return out;
}

std::vector<NamedActivity> parseNamedTsv(const std::string& text) {
    std::vector<NamedActivity> out;
    eachRow(text, 5, "named.tsv", [&](const std::vector<std::string>& c) {
        NamedActivity a;
        a.name = c[0];
        if (!validActivityName(a.name)) throw Error("not an activity name: " + a.name);
        a.category = c[1];
        a.display = c[2];
        a.phases = std::stoi(c[3]);
        std::string tags = c[4];
        size_t start = 0;
        while (start <= tags.size()) {
            const size_t comma = tags.find(',', start);
            const std::string t = tags.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            if (!t.empty()) a.scenarios.push_back(world::parseHex(t, "scenario tag"));
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        out.push_back(std::move(a));
    });
    return out;
}

std::vector<Phase> parsePhasesTsv(const std::string& text) {
    std::vector<Phase> out;
    eachRow(text, 5, "phases.tsv", [&](const std::vector<std::string>& c) {
        Phase p;
        p.activity = c[0];
        p.index = std::stoi(c[1]);
        p.name = c[2];
        p.hash = world::parseHex(c[3], "phase hash");
        p.kind = std::stoi(c[4]);
        out.push_back(std::move(p));
    });
    return out;
}

const DeployEntry* Catalog::byId(uint32_t id) const {
    for (const DeployEntry& e : deploy)
        if (e.id == id) return &e;
    return nullptr;
}

const NamedActivity* Catalog::byName(const std::string& name) const {
    for (const NamedActivity& a : named)
        if (a.name == name) return &a;
    return nullptr;
}

const NamedActivity* Catalog::byScenario(uint32_t tag) const {
    for (const NamedActivity& a : named)
        for (uint32_t s : a.scenarios)
            if (s == tag) return &a;
    return nullptr;
}

std::vector<const Phase*> Catalog::phasesOf(const std::string& activity) const {
    std::vector<const Phase*> out;
    for (const Phase& p : phases)
        if (p.activity == activity) out.push_back(&p);
    return out;
}

const Catalog& catalog() {
    static Catalog c;
    static std::once_flag once;
    std::call_once(once, [] {
        try {
            c.deploy = parseDeployTsv(readText(world::dataFile("activity/catalog.tsv")));
            c.named = parseNamedTsv(readText(world::dataFile("activity/named.tsv")));
            c.phases = parsePhasesTsv(readText(world::dataFile("activity/phases.tsv")));
        } catch (const std::exception& e) {
            c.error = e.what();
        }
        for (const DeployEntry& e : c.deploy)
            if (std::find(c.deployCategories.begin(), c.deployCategories.end(), e.category) == c.deployCategories.end())
                c.deployCategories.push_back(e.category);
        for (const NamedActivity& a : c.named)
            if (std::find(c.namedCategories.begin(), c.namedCategories.end(), a.category) == c.namedCategories.end())
                c.namedCategories.push_back(a.category);
    });
    return c;
}

std::string lower(std::string s) {
    for (char& ch : s) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

std::vector<std::string> queryWords(const std::string& query) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : lower(query)) {
        if (ch == ' ' || ch == ',' || ch == '\t') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool matchesWords(const std::string& hay, const std::vector<std::string>& words) {
    for (const std::string& w : words)
        if (hay.find(w) == std::string::npos) return false;
    return true;
}

bool isCrotaPhase(uint32_t hash) { return std::find(std::begin(kCrotaPhases), std::end(kCrotaPhases), hash) != std::end(kCrotaPhases); }

bool validActivityName(const std::string& name) {
    if (name.empty() || name.size() > 39) return false;
    for (char ch : name)
        if (!(std::islower(static_cast<unsigned char>(ch)) || std::isdigit(static_cast<unsigned char>(ch)) || ch == '_')) return false;
    return true;
}

}   
