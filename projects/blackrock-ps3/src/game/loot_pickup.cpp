#include "game/loot_pickup.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>

#include "core/error.h"
#include "core/ppc.h"
#include "core/sha256.h"

namespace brps3::loot::pickup {

using world::Bytes;

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            out.push_back(trim(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(trim(cur));
    return out;
}

std::vector<std::string> words(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : lower(text)) {
        if (c == ' ' || c == ',') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

struct BucketName {
    uint8_t id;
    const char* name;
};
 
constexpr BucketName kBuckets[] = {
    {1, "Consumables"}, {2, "Primary Weapons"}, {3, "Special Weapons"}, {4, "Heavy Weapons"}, {5, "Materials"},
    {6, "Subclass"}, {7, "Helmet"}, {8, "Gauntlets"}, {9, "Chest Armor"}, {10, "Leg Armor"},
    {11, "Class Armor"}, {12, "Ships"}, {13, "Vehicle"}, {14, "Ghost"}, {15, "Mission"},
    {18, "Cryptarch"}, {19, "Rewards"}, {25, "Emblems"}, {26, "Bounties"}, {27, "Shaders"},
    {28, "Emotes"}, {29, "Sparrow Horn"}, {30, "Glimmer"}, {31, "Vanguard Marks"}, {32, "Crucible Marks"},
    {33, "Silver"}, {34, "Legendary Marks"}, {35, "Artifacts"}, {36, "Quests"}, {255, "None"},
};

}   

const char* className(ItemClass c) {
    switch (c) {
    case ItemClass::Weapon: return "weapon";
    case ItemClass::Armor: return "armor";
    case ItemClass::Ghost: return "ghost";
    case ItemClass::Engram: return "engram";
    default: return "other";
    }
}

const char* classLabel(ItemClass c) {
    switch (c) {
    case ItemClass::Weapon: return "Weapons";
    case ItemClass::Armor: return "Armor";
    case ItemClass::Ghost: return "Ghost shells";
    case ItemClass::Engram: return "Engrams";
    default: return "Other items";
    }
}

std::optional<ItemClass> parseClass(const std::string& s) {
    const std::string l = lower(trim(s));
    for (int i = 0; i < int(ItemClass::Count); ++i)
        if (l == className(ItemClass(i))) return ItemClass(i);
    return std::nullopt;
}

const char* tierName(uint8_t tier) {
    switch (tier) {
    case 1: return "Common";
    case 2: return "Uncommon";
    case 3: return "Rare";
    case 4: return "Legendary";
    case 5: return "Exotic";
    case 6: return "Tier 6";
    default: return "No tier";
    }
}

std::string bucketName(uint8_t bucket) {
    for (const BucketName& b : kBuckets)
        if (b.id == bucket) return b.name;
    return "Bucket " + std::to_string(bucket);
}

const std::vector<uint8_t>& knownBuckets() {
    static const std::vector<uint8_t> v = [] {
        std::vector<uint8_t> out;
        for (const BucketName& b : kBuckets) out.push_back(b.id);
        return out;
    }();
    return v;
}

std::string Definition::label() const { return name.empty() ? "definition " + std::to_string(index) : name; }

 

DefinitionCatalog DefinitionCatalog::parse(const std::string& tsv) {
    DefinitionCatalog c;
    std::istringstream in(tsv);
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#' || lineNo == 1) continue;    
        std::vector<std::string> f;
        std::string cur;
        for (char ch : line) {
            if (ch == '\t') {
                f.push_back(cur);
                cur.clear();
            } else {
                cur += ch;
            }
        }
        f.push_back(cur);
        if (f.size() < 8) throw Error("definitions.tsv line " + std::to_string(lineNo) + ": expected 8 columns");
        Definition d;
        try {
            const unsigned long idx = std::stoul(f[0]);
            if (idx != c.rows_.size()) throw Error("definitions.tsv line " + std::to_string(lineNo) + ": index out of order");
            d.index = uint16_t(idx);
            d.hash = uint32_t(std::stoul(f[1], nullptr, 16));
            d.tier = uint8_t(std::stoul(f[2]));
            d.bucket = uint8_t(std::stoul(f[3]));
        } catch (const Error&) {
            throw;
        } catch (const std::exception&) {
            throw Error("definitions.tsv line " + std::to_string(lineNo) + ": bad number");
        }
        d.cls = parseClass(f[4]).value_or(ItemClass::Other);
        d.slot = f[5];
        d.type = f[6];
        d.name = f[7];
        c.hay_.push_back(lower(d.name + " " + d.type + " " + bucketName(d.bucket) + " " + tierName(d.tier) + " " + className(d.cls) + " " +
                               world::hex8(d.hash) + " #" + std::to_string(d.index)));
        c.rows_.push_back(std::move(d));
    }
    if (c.rows_.size() != ITEM_DEFINITIONS)
        throw Error("definitions.tsv has " + std::to_string(c.rows_.size()) + " rows, expected " + std::to_string(ITEM_DEFINITIONS));
    return c;
}

const Definition* DefinitionCatalog::byHash(uint32_t hash) const {
    for (const Definition& d : rows_)
        if (d.hash == hash) return &d;
    return nullptr;
}

std::vector<const Definition*> DefinitionCatalog::search(const std::string& query, size_t limit) const {
    std::vector<const Definition*> out;
    const std::vector<std::string> w = words(query);
    for (size_t i = 0; i < rows_.size(); ++i) {
        bool ok = true;
        for (const std::string& word : w) {
            std::string needle = word;
            if (needle.size() > 2 && needle[0] == '0' && needle[1] == 'x') needle = needle.substr(2);
            if (hay_[i].find(needle) == std::string::npos) {
                ok = false;
                break;
            }
        }
        if (!ok) continue;
        out.push_back(&rows_[i]);
        if (limit && out.size() >= limit) break;
    }
    return out;
}

const DefinitionCatalog& definitions() {
    static const DefinitionCatalog cat = [] {
        try {
            std::ifstream f(world::dataFile("loot/definitions.tsv"), std::ios::binary);
            if (!f) throw Error("data/loot/definitions.tsv is missing (rebuild, or run tools/loot_build.py)");
            std::ostringstream ss;
            ss << f.rdbuf();
            return DefinitionCatalog::parse(ss.str());
        } catch (const std::exception& e) {
            DefinitionCatalog empty;
            empty.error = e.what();
            return empty;
        }
    }();
    return cat;
}

 

bool Rule::matches(const Definition& d) const {
    if (classes && !(classes & (1u << unsigned(d.cls)))) return false;
    if (tiers && !(tiers & tierBit(d.tier))) return false;
    if (!buckets.empty() && std::find(buckets.begin(), buckets.end(), d.bucket) == buckets.end()) return false;
    if (!items.empty() && std::find(items.begin(), items.end(), d.hash) == items.end()) return false;
    if (!name.empty() && lower(d.name).find(lower(name)) == std::string::npos) return false;
    return true;
}

std::string Rule::describe() const {
    std::string s = keep ? "Keep " : "Drop ";
    std::vector<std::string> tierWords;
    for (uint8_t t : {uint8_t(1), uint8_t(2), uint8_t(3), uint8_t(4), uint8_t(5), uint8_t(6), uint8_t(0), uint8_t(255)})
        if (tiers & tierBit(t)) tierWords.push_back(lower(tierName(t)));
    for (size_t i = 0; i < tierWords.size(); ++i) s += (i ? (i + 1 == tierWords.size() ? " or " : ", ") : "") + tierWords[i];
    if (!tierWords.empty()) s += " ";
    std::vector<std::string> cls;
    for (int i = 0; i < int(ItemClass::Count); ++i)
        if (classes & (1u << i)) cls.push_back(lower(classLabel(ItemClass(i))));
    if (cls.empty()) s += items.empty() ? "items" : (items.size() == 1 ? "1 chosen item" : std::to_string(items.size()) + " chosen items");
    for (size_t i = 0; i < cls.size(); ++i) s += (i ? (i + 1 == cls.size() ? " and " : ", ") : "") + cls[i];
    if (!cls.empty() && !items.empty()) s += items.size() == 1 ? " (1 chosen item)" : " (" + std::to_string(items.size()) + " chosen items)";
    if (!buckets.empty()) {
        s += " in ";
        for (size_t i = 0; i < buckets.size(); ++i) s += (i ? (i + 1 == buckets.size() ? " or " : ", ") : "") + bucketName(buckets[i]);
    }
    if (!name.empty()) s += " named \"" + name + "\"";
    return s;
}

Bitmap compile(const Preset& p, const DefinitionCatalog& cat) {
    Bitmap b{};
    for (const Definition& d : cat.all()) {
        const int r = decidingRule(p, d);
        const bool keep = r < 0 ? p.keepOthers : p.rules[size_t(r)].keep;
        if (keep && d.index < ITEM_DEFINITIONS) b[d.index / 8] |= uint8_t(1u << (d.index % 8));
    }
    return b;
}

int decidingRule(const Preset& p, const Definition& d) {
    for (size_t i = 0; i < p.rules.size(); ++i)
        if (p.rules[i].matches(d)) return int(i);
    return -1;
}

uint32_t countAllowed(const Bitmap& b) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < ITEM_DEFINITIONS; ++i) n += allowed(b, i) ? 1 : 0;
    return n;
}

 

std::vector<Preset> parsePresets(const std::string& text, bool builtIn) {
    std::vector<Preset> out;
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    auto bad = [&](const std::string& why) { return Error("presets line " + std::to_string(lineNo) + ": " + why); };
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        if (t.rfind("[preset]", 0) == 0) {
            Preset p;
            p.name = trim(t.substr(8));
            p.builtIn = builtIn;
            if (p.name.empty()) throw bad("a preset needs a name");
            out.push_back(std::move(p));
            continue;
        }
        if (out.empty()) throw bad("text before the first [preset]");
        Preset& p = out.back();
        const size_t eq = t.find('=');
        if (eq == std::string::npos) throw bad("expected key = value");
        const std::string key = lower(trim(t.substr(0, eq)));
        const std::string value = trim(t.substr(eq + 1));
        if (key == "note") {
            p.note = value;
        } else if (key == "others") {
            const std::string v = lower(value);
            if (v != "keep" && v != "drop") throw bad("others must be keep or drop");
            p.keepOthers = v == "keep";
        } else if (key == "rule") {
            const std::vector<std::string> parts = split(value, ';');
            Rule r;
            const std::string action = lower(parts[0]);
            if (action != "keep" && action != "drop") throw bad("a rule starts with keep or drop");
            r.keep = action == "keep";
            for (size_t i = 1; i < parts.size(); ++i) {
                if (parts[i].empty()) continue;
                const size_t e = parts[i].find('=');
                if (e == std::string::npos) throw bad("rule field without '='");
                const std::string k = lower(trim(parts[i].substr(0, e)));
                const std::string v = trim(parts[i].substr(e + 1));
                if (k == "name") {
                    r.name = v;
                    continue;
                }
                for (const std::string& item : split(v, ',')) {
                    if (item.empty()) continue;
                    try {
                        if (k == "class") {
                            const auto c = parseClass(item);
                            if (!c) throw bad("unknown class " + item);
                            r.classes |= 1u << unsigned(*c);
                        } else if (k == "tier") {
                            const unsigned long tv = std::stoul(item);
                            if (tv > 255) throw bad("tier " + item);
                            r.tiers |= Rule::tierBit(uint8_t(tv));
                        } else if (k == "bucket") {
                            const unsigned long bv = std::stoul(item);
                            if (bv > 255) throw bad("bucket " + item);
                            r.buckets.push_back(uint8_t(bv));
                        } else if (k == "items") {
                            r.items.push_back(world::parseHex(item, "item hash"));
                        } else {
                            throw bad("unknown rule field " + k);
                        }
                    } catch (const Error&) {
                        throw;
                    } catch (const std::exception&) {
                        throw bad("bad value " + item);
                    }
                }
            }
            p.rules.push_back(std::move(r));
        } else {
            throw bad("unknown key " + key);
        }
    }
    return out;
}

std::string serialisePresets(const std::vector<Preset>& presets) {
    std::ostringstream o;
    o << "# Blackrock PS3 pickup filter presets (Loot > Pickup filter). One [preset] per block;\n"
         "# rules are checked in order, the first match decides; 'others' covers the rest.\n";
    auto join = [](const std::vector<std::string>& v) {
        std::string s;
        for (size_t i = 0; i < v.size(); ++i) s += (i ? "," : "") + v[i];
        return s;
    };
    for (const Preset& p : presets) {
        o << "\n[preset] " << p.name << "\n";
        if (!p.note.empty()) o << "note = " << p.note << "\n";
        o << "others = " << (p.keepOthers ? "keep" : "drop") << "\n";
        for (const Rule& r : p.rules) {
            o << "rule = " << (r.keep ? "keep" : "drop");
            std::vector<std::string> v;
            for (int i = 0; i < int(ItemClass::Count); ++i)
                if (r.classes & (1u << i)) v.push_back(className(ItemClass(i)));
            if (!v.empty()) o << "; class = " << join(v);
            v.clear();
            for (int t = 0; t < 8; ++t)
                if (r.tiers & (1u << t)) v.push_back(std::to_string(t));
            if (r.tiers & (1u << 8)) v.push_back("255");
            if (!v.empty()) o << "; tier = " << join(v);
            v.clear();
            for (uint8_t b : r.buckets) v.push_back(std::to_string(b));
            if (!v.empty()) o << "; bucket = " << join(v);
            v.clear();
            for (uint32_t h : r.items) v.push_back(world::hex8(h));
            if (!v.empty()) o << "; items = " << join(v);
            if (!r.name.empty()) {
                std::string n = r.name;
                n.erase(std::remove(n.begin(), n.end(), ';'), n.end());
                o << "; name = " << n;
            }
            o << "\n";
        }
    }
    return o.str();
}

PresetStore& PresetStore::get() {
    static PresetStore s;
    return s;
}

void PresetStore::ensureLoaded() const {
    if (!loaded_) reloadLocked(world::dataFile("loot/pickup_presets.txt"), state_dir() / "loot_pickup_presets.txt");
}

std::vector<Preset> PresetStore::builtIns() const {
    std::lock_guard<std::mutex> lock(m_);
    ensureLoaded();
    return builtIns_;
}

std::vector<Preset> PresetStore::user() const {
    std::lock_guard<std::mutex> lock(m_);
    ensureLoaded();
    return user_;
}

std::vector<Preset> PresetStore::all() const {
    std::lock_guard<std::mutex> lock(m_);
    ensureLoaded();
    std::vector<Preset> out = builtIns_;
    out.insert(out.end(), user_.begin(), user_.end());
    return out;
}

std::string PresetStore::error() const {
    std::lock_guard<std::mutex> lock(m_);
    ensureLoaded();
    return error_;
}

uint64_t PresetStore::revision() const {
    std::lock_guard<std::mutex> lock(m_);
    ensureLoaded();
    return revision_;
}

void PresetStore::reload(const std::filesystem::path& builtInFile, const std::filesystem::path& userFile) {
    std::lock_guard<std::mutex> lock(m_);
    reloadLocked(builtInFile, userFile);
}

void PresetStore::reloadLocked(const std::filesystem::path& builtInFile, const std::filesystem::path& userFile) const {
    loaded_ = true;
    ++revision_;
    error_.clear();
    builtIns_.clear();
    user_.clear();
    userPath_ = userFile;
    auto readAll = [](const std::filesystem::path& p, std::string& out) {
        std::ifstream f(p, std::ios::binary);
        if (!f) return false;
        std::ostringstream ss;
        ss << f.rdbuf();
        out = ss.str();
        return true;
    };
    std::string text;
    try {
        if (!readAll(builtInFile, text)) throw Error("data/loot/pickup_presets.txt is missing");
        builtIns_ = parsePresets(text, true);
    } catch (const std::exception& e) {
        error_ = e.what();
    }
    try {
        if (readAll(userFile, text)) user_ = parsePresets(text, false);
    } catch (const std::exception& e) {
        error_ = std::string("Your presets file could not be read (") + e.what() + "); it is left untouched until you save";
    }
}

const Preset* PresetStore::findLocked(const std::string& name) const {
    for (const Preset& p : builtIns_)
        if (p.name == name) return &p;
    for (const Preset& p : user_)
        if (p.name == name) return &p;
    return nullptr;
}

std::optional<Preset> PresetStore::find(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_);
    ensureLoaded();
    if (const Preset* p = findLocked(name)) return *p;
    return std::nullopt;
}

std::string PresetStore::uniqueName(const std::string& base) const {
    std::lock_guard<std::mutex> lock(m_);
    ensureLoaded();
    if (!findLocked(base)) return base;
    for (int i = 2;; ++i) {
        const std::string n = base + " " + std::to_string(i);
        if (!findLocked(n)) return n;
    }
}

void PresetStore::setUser(std::vector<Preset> presets) {
    std::lock_guard<std::mutex> lock(m_);
    ensureLoaded();
    std::set<std::string> names;
    for (Preset& p : presets) {
        p.builtIn = false;
        if (p.name.empty()) throw Error("A preset needs a name");
        if (!names.insert(p.name).second) throw Error("Two presets are called \"" + p.name + "\"");
        for (const Preset& b : builtIns_)
            if (b.name == p.name) throw Error("\"" + p.name + "\" is a built-in preset name: pick another");
    }
    const std::string text = serialisePresets(presets);
    std::error_code ec;
    std::filesystem::create_directories(userPath_.parent_path(), ec);
    const std::filesystem::path tmp = userPath_.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) throw Error("Cannot write " + tmp.string());
        f << text;
        if (!f) throw Error("Cannot write " + tmp.string());
    }
    std::filesystem::rename(tmp, userPath_, ec);
    if (ec) throw Error("Cannot save " + userPath_.string() + ": " + ec.message());
    user_ = std::move(presets);
    error_.clear();
    ++revision_;
}

 

std::vector<uint8_t> stubProgram(uint32_t base, uint32_t data) {
    ppc::Asm a(base);
     
     
    a.addr(11, int64_t(data)).lwz(12, kDataMode, 11);                
    a.cmpwi(12, int64_t(Mode::Allow)).beq("orig");
    a.cmpwi(12, int64_t(Mode::Bitmap)).bne("reject");                
    a.lhz(10, 0, 4);                                                  
    a.cmplwi(10, ITEM_DEFINITIONS).bge("reject");
    a.srwi(9, 10, 3).addi(9, 9, kDataBitmap).lbzx(8, 11, 9);         
    a.andi_(9, 10, 7).addi(9, 9, kDataMasks).lbzx(9, 11, 9);         
    a.andDot(9, 9, 8).beq("reject");
    a.label("orig").b(PICKUP_ELIGIBILITY);                           
    a.label("reject").li(0, 1).stw(0, 0, 7).li(3, 0).blr();          
    auto code = a.link();
    if (code.size() > kStubSize) throw Error("pickup stub larger than its allocation");
    return code;
}

std::vector<uint8_t> encodeData(Mode mode, const Bitmap& bitmap) {
    std::vector<uint8_t> b(kDataBitmap + kBitmapBytes, 0);
    const uint32_t m = uint32_t(mode);
    b[0] = uint8_t(m >> 24);
    b[1] = uint8_t(m >> 16);
    b[2] = uint8_t(m >> 8);
    b[3] = uint8_t(m);
    for (int i = 0; i < 8; ++i) b[kDataMasks + uint32_t(i)] = uint8_t(1u << i);
    std::copy(bitmap.begin(), bitmap.end(), b.begin() + kDataBitmap);
    return b;
}

uint32_t hookWord(uint32_t stub) { return ppc::branchWord(PICKUP_SITE, stub, true); }

TableCheck verifyItemTable(world::Reader& mem, Resolver& res) {
    TableCheck c;
    c.provider = mem.u32(PICKUP_PROVIDER);
    if (!heapPtr(c.provider, 12)) throw Error("Pickup definitions are not ready (no investment provider): load a character first");
    const Bytes prov = mem.bytes(c.provider, 12);
    if (prov.u32(0) != PICKUP_PROVIDER_VTABLE || prov.u32(4) < 2 || prov.u32(4) > 4 || prov.u32(8) != INVESTMENT_ROOT_TAG)
        throw Error(fmt("The investment provider is not the verified class ({%s, %u, %s})", hex(prov.u32(0)).c_str(), prov.u32(4),
                        hex(prov.u32(8)).c_str()));
    std::string why;
    const auto root = res.body(INVESTMENT_ROOT_TAG, true, &why);
    if (!root) throw Error("The investment root " + hex(INVESTMENT_ROOT_TAG) + " does not resolve: " + why);
    c.root = *root;
    c.tableTag = mem.u32(c.root + ITEM_TABLE_SLOT);
    const auto table = res.body(c.tableTag, true, &why);
    if (!table) throw Error("The item table " + hex(c.tableTag) + " does not resolve: " + why);
    c.table = *table;
    const Bytes head = mem.bytes(c.table + 4, 8);
    c.count = head.u32(0);
    const uint32_t headerSize = head.u32(4);
    if (c.count != ITEM_DEFINITIONS || headerSize < 8 || headerSize > 8192)
        throw Error(fmt("The item table holds %u definitions (expected %u)", c.count, ITEM_DEFINITIONS));
    const uint32_t at = c.table + 8 + headerSize;
    const Bytes schema = mem.bytes(at - 4, 20);
    if (schema.u32(0) != ITEM_SCHEMA_A || schema.u32(4) != ITEM_DEFINITIONS || schema.u32(8) != ITEM_SCHEMA_B)
        throw Error("The item table schema changed");
    const std::vector<uint8_t> rows = mem.read(at + 16, ITEM_DEFINITIONS * 12);
    std::vector<uint8_t> hashes;
    hashes.reserve(ITEM_DEFINITIONS * 4);
    for (uint32_t i = 0; i < ITEM_DEFINITIONS; ++i) {
        hashes.insert(hashes.end(), rows.begin() + i * 12, rows.begin() + i * 12 + 4);
        c.hashes.push_back(uint32_t(rows[i * 12]) << 24 | uint32_t(rows[i * 12 + 1]) << 16 | uint32_t(rows[i * 12 + 2]) << 8 | rows[i * 12 + 3]);
    }
    c.sha256 = sha256Hex(hashes.data(), hashes.size());
    if (c.sha256 != ITEM_TABLE_SHA256) throw Error("Live item identities differ from the pickup filter catalogue (SHA-256 " + c.sha256 + ")");
    if (mem.read(at + 16, 4096) != std::vector<uint8_t>(rows.begin(), rows.begin() + 4096))
        throw Error("The item table changed during verification: try again");
    return c;
}

}   
