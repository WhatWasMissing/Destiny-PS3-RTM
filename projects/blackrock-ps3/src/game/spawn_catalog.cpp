#include "game/spawn_catalog.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sstream>

#include "core/error.h"
#include "game/names_catalog.h"
#include "game/world_addr.h"
#include "game/world_mem.h"

namespace brps3::spawn {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
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

uint32_t decField(const std::string& s) {
    if (s.empty()) return 0;
    size_t used = 0;
    const unsigned long v = std::stoul(s, &used, 10);
    if (used != s.size()) throw Error("not a number: " + s);
    return uint32_t(v);
}

uint32_t tagField(const std::string& s) {
    uint32_t t = 0;
    if (!parseTag(s, t)) throw Error("not a tag: " + s);
    return t;
}

}   

 

Catalog Catalog::parse(const std::string& tsv) {
    Catalog c;
    forEachRow(tsv, "spawnable.tsv", {"tag", "type", "squads", "squad_names", "family", "refused", "candidates"},
               [&](std::vector<std::string>& f) {
                   CatalogRow r;
                   r.tag = tagField(f[0]);
                   const uint32_t type = decField(f[1]);
                   if (type > 0xFF) throw Error("type out of range");
                   r.type = uint8_t(type);
                   r.squads = uint16_t(std::min<uint32_t>(decField(f[2]), 0xFFFF));
                   r.squadNames = std::move(f[3]);
                   r.family = std::move(f[4]);
                   r.refused = f[5] == "1";
                   r.candidates = std::move(f[6]);
                   c.rows_.push_back(std::move(r));
               });
    std::sort(c.rows_.begin(), c.rows_.end(), [](const CatalogRow& a, const CatalogRow& b) { return a.tag < b.tag; });
    c.rows_.erase(std::unique(c.rows_.begin(), c.rows_.end(), [](const CatalogRow& a, const CatalogRow& b) { return a.tag == b.tag; }),
                  c.rows_.end());
    return c;
}

const CatalogRow* Catalog::find(uint32_t tag) const {
    auto it = std::lower_bound(rows_.begin(), rows_.end(), tag, [](const CatalogRow& r, uint32_t t) { return r.tag < t; });
    return it != rows_.end() && it->tag == tag ? &*it : nullptr;
}

const Catalog& catalog() {
    static Catalog c;
    static std::once_flag once;
    std::call_once(once, [] {
        try {
            c = Catalog::parse(readText(world::dataFile("spawn/spawnable.tsv")));
        } catch (const std::exception& e) {
            c = Catalog();
            c.error = e.what();
        }
    });
    return c;
}

 

std::vector<Opener> parseOpeners(const std::string& tsv) {
    std::vector<Opener> out;
    forEachRow(tsv, "openers.tsv", {"group", "id", "name", "tags", "where", "opens", "verdict", "note"}, [&](std::vector<std::string>& f) {
        Opener o;
        o.group = std::move(f[0]);
        o.id = std::move(f[1]);
        o.name = std::move(f[2]);
        std::string piece;
        std::istringstream in(f[3]);
        while (std::getline(in, piece, ','))
            if (!piece.empty()) o.tags.push_back(tagField(piece));
        if (o.tags.empty()) throw Error("an opener without a tag");
        o.where = std::move(f[4]);
        o.opens = std::move(f[5]);
        o.verdict = std::move(f[6]);
        o.note = std::move(f[7]);
        out.push_back(std::move(o));
    });
    return out;
}

namespace {
struct OpenerData {
    std::vector<Opener> rows;
    std::string error;
};
const OpenerData& openerData() {
    static OpenerData d;
    static std::once_flag once;
    std::call_once(once, [] {
        try {
            d.rows = parseOpeners(readText(world::dataFile("spawn/openers.tsv")));
        } catch (const std::exception& e) {
            d.rows.clear();
            d.error = e.what();
        }
    });
    return d;
}
}   

const std::vector<Opener>& openers() { return openerData().rows; }
std::string openersError() { return openerData().error; }

const char* openerGroupLabel(const std::string& group) {
    if (group == "main") return "Hidden";
    if (group == "normal") return "Normally available";
    if (group == "broken") return "Chain broken";
    if (group == "nonui") return "No UI";
    return "Other";
}

 

const char* categoryName(uint8_t type) {
    switch (type) {
    case 1: return "Static mesh";
    case 2: return "Simple prop";
    case 3: return "Cinematic prop";
    case 4: return "Simulated prop";
    case 5: return "Expensive prop";
    case 6: return "Garbage prop";
    case 8: return "Interactive";
    case 9: return "Biped";
    case 10: return "Creature";
    case 11: return "Weapon";
    case 12: return "Vehicle";
    case 13: return "Turret";
    case 14: return "Emitter";
    case 15: return "Projectile";
    case 17: return "Ammo item";
    case 18: return "Loot item";
    case 19: return "Gear";
    case 20: return "Hop-on";
    case 21: return "Biped attachment";
    case 22: return "Weapon attachment";
    case 23: return "Ship attachment";
    case 24: return "System";
    default: return world::typeName(type);
    }
}

const char* usageNote(const CatalogRow& r) {
    if (r.refused || r.type == world::TYPE_PROP_GARBAGE) return "The spawn factory refuses garbage props.";
    if (r.type >= 20 && r.type <= 23)
        return "Attachment definition: it needs a compatible live target (Hop-ons page); spawned alone it has no body.";
    if (r.type == world::TYPE_BIPED)
        return r.squads ? "Raw biped body. Authored squads give it AI and loadout; spawned alone it may stand idle or unarmed."
                        : "Raw biped body. Spawned alone it may stand idle or unarmed.";
    return "Entity definition. What it does once spawned is not established by the catalogue.";
}

bool noiseType(uint8_t type) {
     
    return type == 14 || type == 15 || type == 1 || type == 7 || type == world::TYPE_PROP_GARBAGE || type == 24 || type == 0;
}

bool riskyType(uint8_t type) { return world::typeIsRisky(type); }

 

std::string searchText(const CatalogRow& r) {
    std::string s = tagText(r.tag);
    s += ' ';
    s += categoryName(r.type);
    s += ' ';
    s += world::typeName(r.type);
    if (const names::Entity* e = names::entities().find(r.tag)) {
        s += ' ' + e->name + ' ' + e->aliases + ' ' + e->package + ' ' + e->location() + ' ' + e->activity() + ' ' +
             names::locationName(e->location());
    }
    const std::string own = names::tagName(r.tag);
    if (!own.empty()) s += ' ' + own;
    if (!r.squadNames.empty()) s += ' ' + r.squadNames;
    if (!r.family.empty()) s += ' ' + r.family;
    if (!r.candidates.empty()) s += ' ' + r.candidates;
    return lower(s);
}

std::vector<std::string> queryWords(const std::string& query) {
    std::vector<std::string> out;
    std::istringstream in(lower(query));
    std::string w;
    while (in >> w) {
        if (w.size() > 2 && w[0] == '0' && w[1] == 'x') w = w.substr(2);
        if (!w.empty()) out.push_back(w);
    }
    return out;
}

bool matches(const std::string& hay, const std::vector<std::string>& words) {
    for (const auto& w : words)
        if (hay.find(w) == std::string::npos) return false;
    return true;
}

bool parseTag(const std::string& text, uint32_t& tag) {
    std::string s;
    for (char c : text)
        if (!std::isspace(static_cast<unsigned char>(c))) s += c;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s = s.substr(2);
    if (s.empty() || s.size() > 8) return false;
    for (char c : s)
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    tag = uint32_t(std::stoul(s, nullptr, 16));
    return tag != 0 && tag != 0xFFFFFFFFu;
}

std::string tagText(uint32_t tag) {
    char b[12];
    std::snprintf(b, sizeof b, "%08X", tag);
    return b;
}

}   
