#include "game/inventory_data.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sstream>

#include "core/error.h"
#include "game/names_catalog.h"
#include "game/world_mem.h"

namespace brps3::inventory {

namespace {

uint32_t num(const std::string& s, const char* what, int base = 10) {
    try {
        size_t used = 0;
        const unsigned long v = std::stoul(s, &used, base);
        if (used != s.size()) throw Error("");
        return uint32_t(v);
    } catch (...) {
        throw Error(std::string("inventory data: bad ") + what + " '" + s + "'");
    }
}

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : text) {
        if (c == '\n') {
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

std::string readText(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw Error("missing " + p.string());
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

}   

std::vector<std::string> splitTabs(const std::string& line) { return splitChar(line, '\t'); }

std::vector<std::string> splitChar(const std::string& text, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : text) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

std::string lowerAscii(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string hex16(uint64_t v) {
    char b[20];
    std::snprintf(b, sizeof b, "%016llX", static_cast<unsigned long long>(v));
    return b;
}

std::string bucketName(int key) {
    switch (key) {
    case 1: return "Consumables";
    case 2: return "Primary weapons";
    case 3: return "Special weapons";
    case 4: return "Heavy weapons";
    case 5: return "Materials";
    case 6: return "Subclass";
    case 7: return "Helmet";
    case 8: return "Gauntlets";
    case 9: return "Chest armor";
    case 10: return "Leg armor";
    case 11: return "Class item";
    case 12: return "Ships";
    case 13: return "Sparrows";
    case 14: return "Ghost shells";
    case 15: return "Mission items";
    case 18: return "Relics";
    case 19: return "Packages";
    case 21: return "Account";
    case 22: return "Vault armor";
    case 23: return "Vault general";
    case 24: return "Vault weapons";
    case 25: return "Emblems";
    case 26: return "Bounties";
    case 27: return "Shaders";
    case 28: return "Emotes";
    case 29: return "Horns";
    case 30: case 31: case 32: case 33: case 34: return "Currency";
    case 35: return "Artifacts";
    case 36: return "Quest steps";
    default: return "Bucket " + std::to_string(key);
    }
}

int bucketGroup(int key) {
    if (isWeaponBucket(key) || key == 24) return 1;
    if (isArmorBucket(key) || key == 22 || key == 35) return 2;
    return 0;
}

std::string ItemDef::label() const {
    if (!name.empty()) return name;
    if (!type.empty()) return type;
    return "Definition " + std::to_string(def);
}

std::string Perk::label() const { return name.empty() ? "Perk " + std::to_string(ordinal) : name; }

Catalog Catalog::parse(const std::string& items, const std::string& buckets, const std::string& routes, const std::string& perks,
                       const std::string& rolls, const std::string& tables) {
    Catalog c;
     
     
    for (const std::string& l : lines(items)) {
        if (l.empty() || l.rfind("def\t", 0) == 0) continue;
        const auto f = splitTabs(l);
        if (f.size() != 19) throw Error("inventory data: items.tsv row with " + std::to_string(f.size()) + " fields");
        ItemDef d;
        d.def = uint16_t(num(f[0], "definition"));
        if (d.def != c.items_.size()) throw Error("inventory data: items.tsv is not in definition order at " + f[0]);
        d.hash = num(f[1], "hash", 16);
        d.tag = num(f[2], "tag", 16);
        d.rarity = uint8_t(num(f[3], "rarity"));
        d.home = uint8_t(num(f[4], "home bucket"));
        d.fallback = uint8_t(num(f[5], "fallback bucket"));
        d.perInstance = f[6] == "1";
        d.compat = f[7] == "1";
        d.maxStack = num(f[8], "max stack");
        d.uniq = num(f[9], "uniqueness group", 16);
        d.related = num(f[10], "related reference", 16);
        d.category = f[11];
        d.grid = uint16_t(num(f[12], "grid"));
        d.pattern = uint16_t(num(f[13], "pattern"));
        d.eslot = uint8_t(num(f[14], "equipment slot"));
        if (!f[15].empty())
            for (const std::string& p : splitChar(f[15], ',')) d.intrinsic.push_back(uint16_t(num(p, "intrinsic perk")));
        d.name = f[16];
        d.type = f[17];
        d.alias = f[18];
        c.byHash_.emplace(d.hash, d.def);
        char hx[48];
        std::snprintf(hx, sizeof hx, " %08x def %u", d.hash, d.def);
        c.hay_.push_back(lowerAscii(d.name + " " + d.type + " " + d.alias + " " + names::rarityName(d.rarity) + " " + bucketName(d.home) + hx));
        c.items_.push_back(std::move(d));
    }
    for (const std::string& l : lines(buckets)) {
        if (l.empty() || l.rfind("key\t", 0) == 0) continue;
        const auto f = splitTabs(l);
        if (f.size() < 5) throw Error("inventory data: buckets.tsv row too short");
        Bucket b;
        b.key = int(num(f[0], "bucket key"));
        b.account = f[1] == "account";
        b.first = int(num(f[2], "bucket first"));
        b.count = int(num(f[3], "bucket count"));
        b.eviction = f[4] == "1";
        if (b.count <= 0 || b.first + b.count > (b.account ? 326 : 320)) throw Error("inventory data: bucket " + f[0] + " exceeds its holder");
        for (const Bucket& o : c.buckets_)
            if (o.key == b.key || (o.account == b.account && b.first < o.first + o.count && o.first < b.first + b.count))
                throw Error("inventory data: bucket " + f[0] + " overlaps another");
        c.buckets_.push_back(b);
    }
    for (const std::string& l : lines(routes)) {
        if (l.empty() || l.rfind("source\t", 0) == 0) continue;
        const auto f = splitTabs(l);
        if (f.size() != 2) throw Error("inventory data: routes.tsv row");
        const int src = int(num(f[0], "route source")), dst = int(num(f[1], "route destination"));
        if (!c.routes_.emplace(src, dst).second) throw Error("inventory data: ambiguous route from bucket " + f[0]);
    }
    for (const auto& [src, dst] : c.routes_)
        if (!c.bucket(src) || (dst != 255 && !c.bucket(dst))) throw Error("inventory data: route uses an unknown bucket");
    for (const std::string& l : lines(perks)) {
        if (l.empty() || l.rfind("ordinal\t", 0) == 0) continue;
        const auto f = splitTabs(l);
        if (f.size() != 5) throw Error("inventory data: perks.tsv row");
        Perk p;
        p.ordinal = uint16_t(num(f[0], "perk ordinal"));
        if (p.ordinal != c.perks_.size()) throw Error("inventory data: perks.tsv is not in ordinal order");
        p.enabled = f[1] == "1";
        p.displayHash = num(f[2], "perk hash", 16);
        p.name = f[3];
        p.description = f[4];
        c.perks_.push_back(std::move(p));
    }
    for (const std::string& l : lines(rolls)) {
        if (l.empty() || l[0] == '#') continue;
        const auto f = splitTabs(l);
        if (f[0] == "G" && f.size() == 3) {
            c.rollNodes_[num(f[1], "roll grid")] = num(f[2], "roll node count");
        } else if (f[0] == "N" && f.size() >= 7) {
            RollNode& n = c.rolls_[num(f[1], "roll grid")][num(f[2], "roll node")];
            n.groups = num(f[3], "roll groups");
            n.selector = std::stoi(f[4]);
            n.count = num(f[5], "roll count");
            n.variants[num(f[6], "roll variant")] = f.size() > 7 ? f[7] : "";
        }
    }
    for (const std::string& l : lines(tables)) {
        if (l.empty() || l.rfind("xbox_tag\t", 0) == 0) continue;
        const auto f = splitTabs(l);
        if (f.size() != 3) continue;
        c.tables_[num(f[0], "table tag", 16)] = parseHexBytes(f[2]);
    }
    return c;
}

const ItemDef* Catalog::byHash(uint32_t hash) const {
    auto it = byHash_.find(hash);
    return it == byHash_.end() ? nullptr : &items_[it->second];
}

const Bucket* Catalog::bucket(int key) const {
    for (const Bucket& b : buckets_)
        if (b.key == key) return &b;
    return nullptr;
}

const Bucket* Catalog::bucketAt(bool account, int slot) const {
    for (const Bucket& b : buckets_)
        if (b.account == account && b.contains(slot)) return &b;
    return nullptr;
}

std::optional<int> Catalog::route(int sourceKey) const {
    auto it = routes_.find(sourceKey);
    if (it == routes_.end()) return std::nullopt;
    return it->second;
}

const std::map<unsigned, RollNode>* Catalog::rollGrid(uint32_t grid) const {
    auto it = rolls_.find(grid);
    return it == rolls_.end() ? nullptr : &it->second;
}

unsigned Catalog::rollNodes(uint32_t grid) const {
    auto it = rollNodes_.find(grid);
    return it == rollNodes_.end() ? 0 : it->second;
}

const std::vector<uint8_t>* Catalog::table(uint32_t xboxTag) const {
    auto it = tables_.find(xboxTag);
    return it == tables_.end() ? nullptr : &it->second;
}

const Catalog& catalog() {
    static std::once_flag once;
    static Catalog c;
    std::call_once(once, [] {
        try {
            auto f = [](const char* n) { return readText(world::dataFile(std::string("inventory/") + n)); };
            c = Catalog::parse(f("items.tsv"), f("buckets.tsv"), f("routes.tsv"), f("perks.tsv"), f("rolls.tsv"), f("tables.tsv"));
        } catch (const std::exception& e) {
            c = Catalog();
            c.error = e.what();
        }
    });
    return c;
}

}   
