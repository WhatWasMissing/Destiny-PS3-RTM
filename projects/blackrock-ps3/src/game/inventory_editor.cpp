#include "game/inventory_editor.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>

#include "core/error.h"
#include "game/inventory_addr.h"
#include "game/names_catalog.h"

namespace brps3::inventory {

namespace {

using Bytes = std::vector<uint8_t>;

void need(bool ok, const std::string& m) {
    if (!ok) throw Error(m);
}

uint32_t num(const std::string& s, const char* what, int base = 10) {
    try {
        size_t used = 0;
        const unsigned long v = std::stoul(s, &used, base);
        if (used != s.size()) throw Error("");
        return uint32_t(v);
    } catch (...) {
        throw Error(std::string("editor data: bad ") + what + " '" + s + "'");
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

template <class T>
std::vector<T> numbers(const std::string& csv, const char* what, int base = 10) {
    std::vector<T> out;
    if (csv.empty()) return out;
    for (const std::string& p : splitChar(csv, ',')) out.push_back(T(p == "-" ? 0 : num(p, what, base)));
    return out;
}

uint16_t be16(const Bytes& b, size_t at) { return uint16_t(b.at(at) << 8 | b.at(at + 1)); }
uint32_t be32(const Bytes& b, size_t at) { return uint32_t(be16(b, at)) << 16 | be16(b, at + 2); }
uint64_t be64(const Bytes& b, size_t at) { return uint64_t(be32(b, at)) << 32 | be32(b, at + 4); }
void put32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24);
    p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);
    p[3] = uint8_t(v);
}
Bytes part(const Bytes& b, size_t o, size_t n) {
    need(o <= b.size() && n <= b.size() - o, "editor range outside the record");
    return Bytes(b.begin() + std::ptrdiff_t(o), b.begin() + std::ptrdiff_t(o + n));
}
void index(int v, size_t n, const char* what) { need(v >= 0 && size_t(v) < n, std::string(what) + " index " + std::to_string(v) + " is outside the package catalogue"); }

Bytes words(const std::vector<uint16_t>& v, size_t capacity, size_t perkCount) {
    need(v.size() <= capacity, "perk capacity exceeded (" + std::to_string(v.size()) + " of " + std::to_string(capacity) + ")");
    Bytes b(capacity * 2, 0xFF);
    for (size_t i = 0; i < v.size(); ++i) {
        need(v[i] < perkCount || v[i] == 0xFFFF, "expected a perk ordinal, not a hash or tag (" + std::to_string(v[i]) + ")");
        b[i * 2] = uint8_t(v[i] >> 8);
        b[i * 2 + 1] = uint8_t(v[i]);
    }
    return b;
}
std::vector<uint16_t> wordlist(const Bytes& b, size_t o, size_t n) {
    std::vector<uint16_t> v;
    for (size_t i = 0; i < n; ++i) v.push_back(be16(b, o + i * 2));
    return v;
}

bool matchesEquipped(const Bytes& p, const std::array<uint8_t, 160>& eq) {
    bool occupied = false;
    for (size_t i = 0; i < 20; ++i) {
        uint64_t s = 0;
        for (size_t k = 0; k < 8; ++k) s = s << 8 | eq[i * 8 + k];
        occupied |= s != 0;
        if (be64(p, off::P_EQUIP + i * 48) != s) return false;
    }
    return occupied;
}

std::string readText(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw Error("missing " + p.string());
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

uint32_t tagOf(uint32_t t) {
    need(t != 0, "art key has no verified package tag");
    return names::toPs3Tag(t);
}

std::vector<uint16_t> contribution(const EditorSnapshot& s, uint64_t soid) {
    auto it = s.contributions.find(soid);
    if (it != s.contributions.end()) return it->second;
    const EditorItem* i = s.item(soid);
    need(i != nullptr, "equipped item detail missing");
    return i->perks;
}

std::string nowText() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    char b[32];
    std::strftime(b, sizeof b, "%H:%M:%S", &tm);
    return b;
}

}   

 

EditorCatalog EditorCatalog::parse(const std::string& grids, const std::string& patterns, const std::string& art, const std::string& abilities,
                                   const std::string& modifiers, const std::string& meta) {
    EditorCatalog c;
    for (const std::string& l : lines(grids)) {
        if (l.empty() || l[0] == '#') continue;
        const auto f = splitTabs(l);
        if (f[0] == "G") {
            need(f.size() >= 3, "editor data: grid row");
            Grid g;
            g.index = uint16_t(num(f[1], "grid"));
            need(g.index == c.grids.size(), "editor data: grids out of order");
            g.label = f[2];
            c.grids.push_back(std::move(g));
        } else if (f[0] == "N") {
            need(f.size() == 5 && !c.grids.empty(), "editor data: node row");
            GridNode n;
            n.index = int(num(f[1], "node"));
            n.roll = int(num(f[2], "roll selector"));
            n.exclusive = f[3] == "1";
            n.groups.resize(num(f[4], "group count"));
            c.grids.back().nodes.push_back(std::move(n));
        } else if (f[0] == "S") {
            need(f.size() == 6 && !c.grids.empty() && !c.grids.back().nodes.empty(), "editor data: step row");
            GridNode& n = c.grids.back().nodes.back();
            const uint32_t g = num(f[1], "group"), s = num(f[2], "step");
            need(g < n.groups.size() && s == n.groups[g].steps.size(), "editor data: step out of order");
            GridStep st;
            st.perks = numbers<uint16_t>(f[3], "step perk");
            st.abilities = numbers<int>(f[4], "step ability");
            st.modifiers = numbers<uint32_t>(f[5], "step modifier", 16);
            n.groups[g].steps.push_back(std::move(st));
        }
    }
    for (const std::string& l : lines(patterns)) {
        if (l.empty() || l[0] == '#') continue;
        const auto f = splitTabs(l);
        if (f[0] == "P") {
            need(f.size() >= 3, "editor data: pattern row");
            Pattern p;
            p.index = uint16_t(num(f[1], "pattern"));
            need(p.index == c.patterns.size(), "editor data: patterns out of order");
            p.label = f[2];
            p.search = f.size() > 3 ? f[3] : "";
            c.patterns.push_back(std::move(p));
        } else if (f[0] == "C") {
            need(f.size() >= 3 && !c.patterns.empty(), "editor data: pattern category row");
            ArtCategory a;
            a.category = int(num(f[1], "art category"));
            a.statSelector = int(num(f[2], "stat selector"));
            if (f.size() > 3 && !f[3].empty())
                for (const std::string& t : splitChar(f[3], ',')) {
                    const auto mv = splitChar(t, ':');
                    need(mv.size() == 2, "editor data: threshold");
                    a.thresholds.push_back({int(std::stol(mv[0])), int(num(mv[1], "variant"))});
                }
            c.patterns.back().categories.push_back(std::move(a));
        }
    }
    for (const std::string& l : lines(art)) {
        if (l.empty() || l[0] == '#') continue;
        const auto f = splitTabs(l);
        if (f[0] == "A") {
            need(f.size() >= 5, "editor data: art row");
            Art a;
            a.index = uint16_t(num(f[1], "art"));
            need(a.index == c.art.size(), "editor data: art out of order");
            a.baseTag = f[2] == "-" ? 0 : num(f[2], "art tag", 16);
            a.altTag = f[3] == "-" ? 0 : num(f[3], "art tag", 16);
            a.label = f[4];
            a.search = f.size() > 5 ? f[5] : "";
            c.art.push_back(std::move(a));
        } else if (f[0] == "T") {
            need(f.size() == 3 && !c.art.empty(), "editor data: art part row");
            ArtPart p;
            p.category = int(num(f[1], "part category"));
            p.tags = numbers<uint32_t>(f[2], "part tag", 16);
            c.art.back().parts.push_back(std::move(p));
        }
    }
    for (const std::string& l : lines(abilities)) {
        if (l.empty() || l.rfind("index\t", 0) == 0) continue;
        const auto f = splitTabs(l);
        need(f.size() == 2, "editor data: ability row");
        c.abilities.push_back({num(f[0], "ability"), f[1]});
    }
    for (const std::string& l : lines(modifiers)) {
        if (l.empty() || l.rfind("key\t", 0) == 0) continue;
        const auto f = splitTabs(l);
        need(f.size() == 2, "editor data: modifier row");
        c.modifiers.push_back({num(f[0], "modifier key", 16), f[1]});
    }
    for (const std::string& l : lines(meta)) {
        const auto f = splitTabs(l);
        if (f.size() != 2) continue;
        if (f[0] == "fingerprint") c.fingerprint = f[1];
        if (f[0] == "art_constants") c.artConstants = numbers<int>(f[1], "art constant");
    }
    need(c.artConstants.size() >= 22, "editor data: art constants missing");
    return c;
}

const EditorCatalog& editorCatalog() {
    static std::once_flag once;
    static EditorCatalog c;
    std::call_once(once, [] {
        try {
            auto f = [](const char* n) { return readText(world::dataFile(std::string("inventory/") + n)); };
            c = EditorCatalog::parse(f("grids.tsv"), f("patterns.tsv"), f("art.tsv"), f("abilities.tsv"), f("modifiers.tsv"), f("editor.tsv"));
        } catch (const std::exception& e) {
            c = EditorCatalog();
            c.error = e.what();
        }
    });
    return c;
}

 

Talents talentsOf(const std::array<uint8_t, 168>& d) {
    Talents t;
    t.grid = uint16_t(d[off::D_GRID] << 8 | d[off::D_GRID + 1]);
    for (size_t i = 0; i < 36; ++i) t.nodes[i] = d[off::D_NODES + i];
    for (size_t i = 0; i < 8; ++i) t.rolls[i] = d[off::D_ROLLS + i];
    for (size_t i = 0; i < 5; ++i)
        for (size_t k = 0; k < 3; ++k) t.abilities[i][k] = d[off::D_ABILITIES + 3 * i + k];
    return t;
}

void applyTalents(std::array<uint8_t, 168>& d, const Talents& t) {
    d[off::D_GRID] = uint8_t(t.grid >> 8);
    d[off::D_GRID + 1] = uint8_t(t.grid);
    for (size_t i = 0; i < 36; ++i) d[off::D_NODES + i] = t.nodes[i];
    for (size_t i = 0; i < 8; ++i) d[off::D_ROLLS + i] = t.rolls[i];
    for (size_t i = 0; i < 5; ++i)
        for (size_t k = 0; k < 3; ++k) d[off::D_ABILITIES + 3 * i + k] = t.abilities[i][k];
}

int nodeVariant(const GridNode& n, const Talents& t) {
    need(!n.groups.empty() && !n.groups[0].steps.empty(), "empty talent group");
    const int count = int(n.groups[0].steps.size());
    if (n.groups.size() == 1 && n.roll != 255) {
        index(n.roll, 8, "roll selector");
        return t.rolls[size_t(n.roll)] % count;
    }
    return count - 1;
}

TalentPerks decodeTalents(const Talents& t, const EditorCatalog& ec, const Catalog& cat) {
    TalentPerks r;
    for (const auto& triple : t.abilities) {
        const bool empty = triple[0] == 255 || triple[1] == 255 || triple[2] == 255;
        need(!empty || (triple[0] == 255 && triple[1] == 255 && triple[2] == 255), "an empty ability triple must be FF/FF/FF");
    }
    if (t.grid == 0xFFFF) return r;
    index(t.grid, ec.grids.size(), "talent grid");
    const Grid& g = ec.grids[t.grid];
    for (const GridNode& n : g.nodes) {
        index(n.index, 36, "talent node");
        const int rank = t.nodes[size_t(n.index)];
        need(rank <= int(n.groups.size()), "node " + std::to_string(n.index) + " state is outside the native group count");
        if (!rank) continue;
        const int vi = nodeVariant(n, t);
        const int start = n.exclusive ? rank - 1 : 0;
        for (int gi = start; gi < rank; ++gi) {
            const auto& steps = n.groups[size_t(gi)].steps;
            index(vi, steps.size(), "talent step");
            const GridStep& st = steps[size_t(vi)];
            for (uint16_t p : st.perks)
                if (const Perk* pk = cat.perk(p); pk && pk->enabled) r.perks.push_back(p);
            for (uint32_t m : st.modifiers) r.modifiers.push_back(m);
        }
    }
    for (const auto& triple : t.abilities) {
        if (triple[0] == 255) {
            r.abilities.push_back(255);
            continue;
        }
        index(triple[0], g.nodes.size(), "ability node");
        const GridNode& n = g.nodes[triple[0]];
        index(triple[1], n.groups.size(), "ability group");
        const auto& steps = n.groups[triple[1]].steps;
        const int vi = nodeVariant(n, t);
        index(vi, steps.size(), "ability step");
        const auto& abil = steps[size_t(vi)].abilities;
        index(triple[2], abil.size(), "ability record");
        r.abilities.push_back(abil[triple[2]]);
    }
    return r;
}

 

const EditorItem* EditorSnapshot::item(uint64_t soid) const {
    for (const EditorItem& i : items)
        if (i.soid == soid) return &i;
    return nullptr;
}

std::array<uint8_t, 64> EditorSnapshot::aggregateKey() const {
    std::array<uint8_t, 64> k{};
    if (!profiles.empty() && profiles[0].raw.size() >= off::P_CHAR_PERKS + 64)
        std::copy_n(profiles[0].raw.begin() + off::P_CHAR_PERKS, 64, k.begin());
    return k;
}

EditorSnapshot editorRead(world::Reader& mem, DefinitionCache& cache, const EditorCatalog& ec, const Catalog& cat) {
    need(ec.error.empty(), "editor catalogue unavailable: " + ec.error);
    const uint64_t reads0 = mem.reads();
    ReadOptions ro;
    ro.sessionOnly = true;
    const Snapshot inv = readSnapshot(mem, cache, ro);
    EditorSnapshot s;
    s.manager = inv.manager;
    s.characterAddr = inv.characterAddr;
    s.descriptorAddr = inv.descriptorAddr;
    s.descriptor = inv.descriptor;
    s.accountSoid = inv.accountSoid;
    s.characterSoid = inv.characterSoid;
    Observation obs(mem);
    const world::Bytes meta = obs.bytes(s.manager + off::META, 96);
    const uint32_t offset = meta.u32(48), count = meta.u32(52), stride = meta.u32(56);
    need(stride == off::DETAIL_SIZE && meta.u32(60) == off::DETAIL_SCHEMA && count <= off::DETAIL_MAX &&
             uint64_t(offset) + uint64_t(count) * off::DETAIL_SIZE <= off::STORAGE_LIMIT,
         "unrecognised item detail table");
    const uint32_t detailBase = s.manager + off::STORAGE + offset;
    const world::Bytes eq = obs.bytes(s.characterAddr + off::EQUIPPED, 160);
    for (size_t i = 0; i < 160; ++i) s.equipped[i] = eq.data()[i];
    if (count) {
        const world::Bytes details = obs.bytes(detailBase, count * off::DETAIL_SIZE);
        for (uint32_t i = 0; i < count; ++i) {
            const uint64_t id = details.u64(i * off::DETAIL_SIZE);
            if (!id) continue;
            EditorItem it;
            it.soid = id;
            it.def = details.u16(i * off::DETAIL_SIZE + off::D_DEF);
            index(it.def, cat.size(), "item definition");
            it.addr = detailBase + i * off::DETAIL_SIZE;
            for (uint32_t k = 0; k < off::DETAIL_SIZE; ++k) it.raw[k] = details.u8(i * off::DETAIL_SIZE + k);
            for (int j = 0; j < 20; ++j)
                if (eq.u64(uint32_t(j) * 8) == id) it.slot = j;
            it.talents = talentsOf(it.raw);
            try {
                it.perks = decodeTalents(it.talents, ec, cat).perks;
                if (const ItemDef* d = cat.item(it.def))
                    for (uint16_t p : d->intrinsic)
                        if (const Perk* pk = cat.perk(p); pk && pk->enabled) it.perks.push_back(p);
            } catch (const std::exception& e) {
                it.perkError = e.what();
            }
            s.items.push_back(std::move(it));
        }
    }
     
    const uint32_t cacheAddr = obs.u32(addr::ORBIT_PROFILE_CACHE);
    need(world::heap(cacheAddr, off::ORBIT_RECORDS * off::ORBIT_RECORD) && cacheAddr % 4 == 0, "the profile cache pointer " + world::hex8(cacheAddr) + " is not plausible");
    const world::Bytes records = obs.bytes(cacheAddr, off::ORBIT_RECORDS * off::ORBIT_RECORD);
    for (uint32_t i = 0; i < off::ORBIT_RECORDS; ++i) {
        const uint32_t o = i * off::ORBIT_RECORD;
        if (records.u8(o) < 5 || records.u64(o + 0x18) != s.accountSoid || records.u64(o + 0x20) != s.characterSoid) continue;
        Profile p;
        p.kind = Profile::Cache;
        p.addr = cacheAddr + o + off::ORBIT_PROFILE;
        p.raw = records.slice(o + off::ORBIT_PROFILE, off::PROFILE_SIZE).data();
        need(matchesEquipped(p.raw, s.equipped), "the cached equipment differs from the inventory; refresh");
        s.profiles.push_back(std::move(p));
    }
    need(s.profiles.size() == 1, "expected one owned cached profile, found " + std::to_string(s.profiles.size()) + " (load your character)");
     
     
    const uint32_t hp = obs.u32(addr::PLAYER_DESCRIPTOR);
    need(world::heap(hp, 16), "the player descriptor pointer " + world::hex8(hp) + " is not plausible");
    const world::Bytes header = obs.bytes(hp, 16);
    const uint32_t base = header.u32(4);
    need(world::heap(base, 4) && header.u32(8) == off::GAMEPLAY_STRIDE, "unknown runtime profile layout (stride " + world::hex8(header.u32(8)) + ")");
     
     
    const uint32_t first = base + off::GAMEPLAY_PROFILE;
    const uint32_t span = (off::GAMEPLAY_MAX - 1) * off::GAMEPLAY_STRIDE + off::PROFILE_SIZE;
    const auto bulk = world::heap(first, span) ? mem.tryBytes(first, span) : std::nullopt;
    for (uint32_t i = 0; i < off::GAMEPLAY_MAX; ++i) {
        const uint32_t a = first + i * off::GAMEPLAY_STRIDE;
        const auto b = bulk ? std::optional<world::Bytes>(bulk->slice(i * off::GAMEPLAY_STRIDE, off::PROFILE_SIZE)) : mem.tryBytes(a, off::PROFILE_SIZE);
        if (b && matchesEquipped(b->data(), s.equipped)) s.profiles.push_back({Profile::Gameplay, a, b->data()});
    }
    need(s.profiles.size() <= 2, "ambiguous gameplay profile ownership (" + std::to_string(s.profiles.size() - 1) + " gameplay copies match the equipped list)");
    const Bytes& p = s.profiles[0].raw;
    for (size_t i = 0; i < 20; ++i) {
        SlotState& st = s.slots[i];
        st.soid = eq.u64(uint32_t(i) * 8);
        st.def = be16(p, off::P_EQUIP + i * 48 + 8);
        st.pattern = be16(p, off::P_EQUIP + i * 48 + 0xA);
        st.art = be16(p, off::P_EQUIP + i * 48 + 0xC);
        std::copy_n(p.begin() + std::ptrdiff_t(off::P_PARTS + i * 32), 32, st.parts.begin());
        if (weaponSlot(int(i))) st.perks = wordlist(p, off::P_WEAPON_PERKS + (i - 6) * 32, 16);
    }
    s.characterPerks = wordlist(p, off::P_CHAR_PERKS, 32);
    for (size_t i = 0; i < 5; ++i) std::copy_n(p.begin() + std::ptrdiff_t(off::P_ABILITY + i * 68), 68, s.abilities[i].begin());
    obs.verify();
    s.stamp = uint64_t(std::chrono::steady_clock::now().time_since_epoch().count());
    s.reads = mem.reads() - reads0;
    return s;
}

 

const char* editModeName(EditMode m) {
    switch (m) {
    case EditMode::Equipment: return "equipment";
    case EditMode::Ability: return "ability";
    case EditMode::Character: return "character";
    case EditMode::Talents: return "talents";
    }
    return "?";
}

std::vector<uint16_t> nonEmpty(const std::vector<uint16_t>& v) {
    std::vector<uint16_t> r;
    for (uint16_t x : v)
        if (x != 0xFFFF) r.push_back(x);
    return r;
}

std::array<uint8_t, 32> resolveArt(int pattern, int art, int slot, const Bytes& profile, const EditorCatalog& c) {
    index(slot, 20, "equipment slot");
    need(profile.size() == off::PROFILE_SIZE, "wrong profile size");
    std::array<uint8_t, 32> out;
    out.fill(0xFF);
    if (art == 0xFFFF) return out;
    index(art, c.art.size(), "art");
    const Art& a = c.art[size_t(art)];
    const bool weapon = weaponSlot(slot);
    int sequential = 0;
    if (!weapon) {
        bool alternate = false;
        const int old = be16(profile, off::P_EQUIP + size_t(slot) * 48 + 12);
        if (old != 0xFFFF) {
            index(old, c.art.size(), "current art");
            const Art& oa = c.art[size_t(old)];
            if (oa.altTag && oa.altTag != oa.baseTag) alternate = be32(profile, off::P_PARTS + size_t(slot) * 32) == tagOf(oa.altTag);
        }
        const uint32_t base = alternate && a.altTag ? a.altTag : a.baseTag;
        if (base) {
            put32(out.data(), tagOf(base));
            sequential = 1;
        }
    }
    if (pattern == 0xFFFF) return out;
    index(pattern, c.patterns.size(), "pattern");
    const auto& k = c.artConstants;
    for (const ArtCategory& category : c.patterns[size_t(pattern)].categories) {
        for (const ArtPart& p : a.parts) {
            if (p.category != category.category) continue;
            if (p.tags.empty()) continue;
            int value = 0, variant = 0;
            if (weapon) {
                const size_t stat = off::P_WEAPON_STATS + size_t(slot - 6) * 128;
                for (size_t i = 0; i < 16; ++i)
                    if (profile[stat + i * 8] == category.statSelector) value = int32_t(be32(profile, stat + i * 8 + 4));
            }
            for (const auto& [minimum, v] : category.thresholds)
                if (value >= minimum) variant = v;
            variant = std::clamp(variant, 0, int(p.tags.size()) - 1);
            int dest = sequential;
            const int cat = category.category;
            if (weapon) {
                dest = -1;
                for (auto [ki, d] : {std::pair{14, 2}, {17, 2}, {15, 0}, {16, 1}, {18, 3}, {19, 4}, {20, 5}, {21, 6}})
                    if (size_t(ki) < k.size() && cat == k[size_t(ki)]) dest = d;
                if (dest < 0) dest = 7 + sequential++;
            } else {
                ++sequential;
            }
            if (dest < 8) put32(out.data() + dest * 4, tagOf(p.tags[size_t(variant)]));
            break;
        }
    }
    return out;
}

EditPlan editorPlan(const EditorSnapshot& s, const EditRequest& r, const EditorCatalog& ec, const Catalog& cat) {
    EditPlan plan;
    plan.request = r;
    plan.character = s.characterSoid;
    plan.equipped = s.equipped;
    plan.previousContributions = s.contributions;
    auto add = [&](std::string name, uint32_t a, const Bytes& before, const Bytes& after) {
        if (before != after) plan.changes.push_back({std::move(name), a, before, after});
    };
    const size_t perkCount = cat.perks().size();
    if (r.mode == EditMode::Talents) {
        const EditorItem* item = nullptr;
        for (const EditorItem& i : s.items)
            if (i.soid == r.soid) {
                need(item == nullptr, "duplicate SOID in the detail rows");
                item = &i;
            }
        need(item != nullptr, "the item is no longer present");
        decodeTalents(r.talents, ec, cat);
        if (r.talents.grid != 0xFFFF) index(r.talents.grid, ec.grids.size(), "talent grid");
        std::array<uint8_t, 168> after = item->raw;
        applyTalents(after, r.talents);
        const Bytes b(item->raw.begin(), item->raw.end()), a(after.begin(), after.end());
        for (auto [o, n] : {std::pair{0x38u, 15u}, {0x48u, 2u}, {0x4Au, 36u}, {0x6Eu, 8u}})
            add("item talent state", item->addr + o, part(b, o, n), part(a, o, n));
        return plan;
    }
    for (const Profile& profile : s.profiles) {
        const Bytes& b = profile.raw;
        const uint32_t a = profile.addr;
        if (r.mode == EditMode::Equipment) {
            index(r.slot, 20, "equipment slot");
            const size_t slot = size_t(r.slot);
            need(be64(b, off::P_EQUIP + slot * 48) != 0, "that equipment slot is empty");
            need(r.soid == s.slots[slot].soid, "the selected equipment changed; read again");
            if (r.pattern != 0xFFFF) index(r.pattern, ec.patterns.size(), "pattern");
            if (r.art != 0xFFFF) index(r.art, ec.art.size(), "art");
            const Bytes pair{uint8_t(r.pattern >> 8), uint8_t(r.pattern), uint8_t(r.art >> 8), uint8_t(r.art)};
            const size_t o = off::P_EQUIP + slot * 48 + 0xA;
            add("pattern / art", a + uint32_t(o), part(b, o, 4), pair);
            if (r.pattern != be16(b, o) || r.art != be16(b, o + 2)) {
                const auto parts = resolveArt(r.pattern, r.art, r.slot, b, ec);
                add("resolved model parts", a + off::P_PARTS + uint32_t(slot) * 32, part(b, off::P_PARTS + slot * 32, 32), Bytes(parts.begin(), parts.end()));
            }
            if (weaponSlot(r.slot)) {
                add("weapon perks", a + off::P_WEAPON_PERKS + uint32_t(slot - 6) * 32, part(b, off::P_WEAPON_PERKS + (slot - 6) * 32, 32),
                    words(r.perks, 16, perkCount));
            } else if (aggregateSlot(r.slot)) {
                const auto old = nonEmpty(contribution(s, r.soid));
                const auto replacement = nonEmpty(r.perks);
                words(replacement, 32, perkCount);
                if (old != replacement) {
                    auto current = nonEmpty(wordlist(b, off::P_CHAR_PERKS, 32));
                    size_t insertion = current.size();
                    for (uint16_t v : old) {
                        auto it = std::find(current.begin(), current.end(), v);
                        need(it != current.end(),
                             "this item's perk contribution is absent from the current character perks; read again or edit CHARACTER PERKS directly");
                        insertion = std::min(insertion, size_t(it - current.begin()));
                        current.erase(it);
                    }
                    for (uint16_t v : replacement) {
                        current.insert(current.begin() + std::ptrdiff_t(std::min(insertion, current.size())), v);
                        ++insertion;
                    }
                    add("equipment perk contribution", a + off::P_CHAR_PERKS, part(b, off::P_CHAR_PERKS, 64), words(current, 32, perkCount));
                }
            }
        } else if (r.mode == EditMode::Character) {
            add("character perks", a + off::P_CHAR_PERKS, part(b, off::P_CHAR_PERKS, 64), words(r.perks, 32, perkCount));
        } else if (r.mode == EditMode::Ability) {
            index(r.slot, 5, "ability slot");
            const size_t slot = size_t(r.slot);
            if (r.selector != 255) index(r.selector, ec.abilities.size(), "ability");
            add("ability selector", a + off::P_ABILITY + uint32_t(slot) * 68, part(b, off::P_ABILITY + slot * 68, 1), Bytes{r.selector});
            if (r.haveModifiers) {
                Bytes after(64);
                for (size_t i = 0; i < 16; ++i) {
                    const uint32_t key = r.modifiers[i];
                    bool known = key == kDefaultUniq || key == be32(b, off::P_ABILITY + 4 + slot * 68 + i * 4);
                    for (const Choice& m : ec.modifiers) known |= m.key == key;
                    need(known, "modifier key " + world::hex8(key) + " is not in the verified catalogue");
                    put32(after.data() + i * 4, key);
                }
                add("ability modifiers", a + off::P_ABILITY + 4 + uint32_t(slot) * 68, part(b, off::P_ABILITY + 4 + slot * 68, 64), after);
            }
        }
    }
    return plan;
}

EditRequest draftFor(const EditorSnapshot& s, EditMode m, int slot, uint64_t itemSoid) {
    EditRequest r;
    r.mode = m;
    if (m == EditMode::Equipment) {
        slot = std::clamp(slot, 0, 19);
        const SlotState& st = s.slots[size_t(slot)];
        r.slot = slot;
        r.soid = st.soid;
        r.pattern = st.pattern;
        r.art = st.art;
        if (weaponSlot(slot)) r.perks = st.perks;
        else if (aggregateSlot(slot)) {
            if (auto it = s.contributions.find(st.soid); it != s.contributions.end()) r.perks = it->second;
            else if (const EditorItem* i = s.item(st.soid)) r.perks = i->perks;
        }
    } else if (m == EditMode::Ability) {
        slot = std::clamp(slot, 0, 4);
        r.slot = slot;
        r.selector = s.abilities[size_t(slot)][0];
        r.haveModifiers = true;
        for (size_t i = 0; i < 16; ++i) {
            const auto& a = s.abilities[size_t(slot)];
            r.modifiers[i] = uint32_t(a[4 + i * 4]) << 24 | uint32_t(a[5 + i * 4]) << 16 | uint32_t(a[6 + i * 4]) << 8 | a[7 + i * 4];
        }
    } else if (m == EditMode::Character) {
        r.perks = s.characterPerks;
    } else {
        const EditorItem* it = s.item(itemSoid);
        if (!it && !s.items.empty()) it = &s.items.front();
        if (it) {
            r.soid = it->soid;
            r.talents = it->talents;
        }
    }
    return r;
}

 

Applied editorCommit(world::Reader& mem, const WriteFn& write, DefinitionCache& cache, const EditorSnapshot& snapshot, const EditPlan& plan,
                     const EditorCatalog& ec, const Catalog& cat, Applied* partial) {
     
    const EditPlan again = editorPlan(snapshot, plan.request, ec, cat);
    need(again.changes == plan.changes, "the patch plan differs from its typed request");
    need(!plan.changes.empty(), "no changes to apply");
    EditorSnapshot fresh = editorRead(mem, cache, ec, cat);
    uint32_t talentAddr = 0;
    Bytes talentIdentity;
    if (plan.request.mode == EditMode::Talents) {
        const EditorItem* it = snapshot.item(plan.request.soid);
        need(it != nullptr, "the item is no longer present");
        talentAddr = it->addr;
        talentIdentity.assign(it->raw.begin(), it->raw.begin() + 0x28);
        need(mem.bytes(talentAddr, 0x28).data() == talentIdentity, "the item identity changed; read again before applying");
    }
    need(fresh.characterSoid == plan.character && fresh.equipped == plan.equipped, "the character or equipment changed; read again before applying");
    if (!snapshot.contributions.empty()) {
        need(fresh.aggregateKey() == snapshot.aggregateKey(), "the character perks changed; read again");
        fresh.contributions = snapshot.contributions;
    }
    const EditPlan next = editorPlan(fresh, plan.request, ec, cat);
    need(next.changes == plan.changes, "target values or addresses changed; read again before applying");
    for (const Change& c : plan.changes) need(mem.bytes(c.addr, uint32_t(c.before.size())).data() == c.before, "a target changed before the write; read again");
    Applied out;
    out.plan = plan;
    out.manager = fresh.manager;
    out.descriptorAddr = fresh.descriptorAddr;
    out.characterAddr = fresh.characterAddr;
    out.equipped = fresh.equipped;
    out.character = fresh.characterSoid;
    out.when = nowText();
    try {
        for (const Change& c : plan.changes) {
            need(mem.u32(addr::PROFILE_MANAGER) == fresh.manager, "the inventory owner changed during apply");
            const auto d = mem.bytes(fresh.descriptorAddr, 64).data();
            need(std::equal(d.begin(), d.end(), fresh.descriptor.begin()), "the inventory revision changed during apply");
            const auto e = mem.bytes(fresh.characterAddr + off::EQUIPPED, 160).data();
            need(std::equal(e.begin(), e.end(), plan.equipped.begin()), "the equipment changed during apply");
            need(mem.bytes(c.addr, uint32_t(c.before.size())).data() == c.before, "a target changed during apply");
            if (talentAddr) need(mem.bytes(talentAddr, 0x28).data() == talentIdentity, "the item identity changed during apply");
            write(c.addr, c.after);
            ++out.written;
            need(mem.bytes(c.addr, uint32_t(c.after.size())).data() == c.after, "the write did not keep the requested bytes");
        }
    } catch (...) {
        if (partial) *partial = out;
        throw;
    }
    return out;
}

size_t editorRevert(world::Reader& mem, const WriteFn& write, const Applied& a, std::vector<std::string>* skipped) {
    size_t restored = 0;
    auto skip = [&](const Change& c, const std::string& why) {
        if (skipped) skipped->push_back(c.name + " at " + world::hex8(c.addr) + ": " + why);
    };
    bool owner = false;
    try {
        owner = mem.u32(addr::PROFILE_MANAGER) == a.manager;
        const auto e = mem.bytes(a.characterAddr + off::EQUIPPED, 160).data();
        owner = owner && std::equal(e.begin(), e.end(), a.equipped.begin()) && mem.bytes(a.characterAddr, 8).u64(0) == a.character;
    } catch (const std::exception&) {
        owner = false;
    }
    for (size_t i = std::min(a.written, a.plan.changes.size()); i-- > 0;) {
        const Change& c = a.plan.changes[i];
        if (!owner) {
            skip(c, "the character or equipment changed");
            continue;
        }
        try {
            if (mem.bytes(c.addr, uint32_t(c.after.size())).data() != c.after) {
                skip(c, "the bytes are no longer the edited ones (the game rebuilt them)");
                continue;
            }
            write(c.addr, c.before);
            if (mem.bytes(c.addr, uint32_t(c.before.size())).data() == c.before) ++restored;
            else skip(c, "read back differs");
        } catch (const std::exception& ex) {
            skip(c, ex.what());
        }
    }
    return restored;
}

 

std::string serialiseRequest(const EditRequest& r) {
    std::ostringstream o;
    auto list = [](const auto& v) {
        std::string s;
        for (const auto& x : v) s += (s.empty() ? "" : ",") + std::to_string(unsigned(x));
        return s;
    };
    o << "mode=" << editModeName(r.mode) << "\n";
    o << "slot=" << r.slot << "\n";
    if (r.mode == EditMode::Equipment) o << "pattern=" << r.pattern << "\nart=" << r.art << "\nperks=" << list(r.perks) << "\n";
    if (r.mode == EditMode::Character) o << "perks=" << list(r.perks) << "\n";
    if (r.mode == EditMode::Ability) {
        o << "selector=" << unsigned(r.selector) << "\n";
        if (r.haveModifiers) {
            std::string s;
            for (uint32_t k : r.modifiers) s += (s.empty() ? "" : ",") + world::hex8(k);
            o << "modifiers=" << s << "\n";
        }
    }
    if (r.mode == EditMode::Talents) {
        o << "grid=" << r.talents.grid << "\nnodes=" << list(r.talents.nodes) << "\nrolls=" << list(r.talents.rolls) << "\n";
        std::string s;
        for (const auto& t : r.talents.abilities) s += (s.empty() ? "" : ",") + std::to_string(t[0]) + "." + std::to_string(t[1]) + "." + std::to_string(t[2]);
        o << "abilities=" << s << "\n";
    }
    return o.str();
}

EditRequest parseRequest(const std::string& text) {
    EditRequest r;
    bool haveMode = false;
    for (const std::string& l : lines(text)) {
        const size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = l.substr(0, eq), v = l.substr(eq + 1);
        if (k == "mode") {
            haveMode = true;
            if (v == "equipment") r.mode = EditMode::Equipment;
            else if (v == "ability") r.mode = EditMode::Ability;
            else if (v == "character") r.mode = EditMode::Character;
            else if (v == "talents") r.mode = EditMode::Talents;
            else throw Error("preset: unknown mode " + v);
        } else if (k == "slot") r.slot = int(num(v, "slot"));
        else if (k == "pattern") r.pattern = uint16_t(num(v, "pattern"));
        else if (k == "art") r.art = uint16_t(num(v, "art"));
        else if (k == "perks") r.perks = numbers<uint16_t>(v, "perk");
        else if (k == "selector") r.selector = uint8_t(num(v, "selector"));
        else if (k == "modifiers") {
            const auto keys = numbers<uint32_t>(v, "modifier", 16);
            need(keys.size() == 16, "preset: 16 modifier keys expected");
            std::copy(keys.begin(), keys.end(), r.modifiers.begin());
            r.haveModifiers = true;
        } else if (k == "grid") r.talents.grid = uint16_t(num(v, "grid"));
        else if (k == "nodes") {
            const auto n = numbers<uint8_t>(v, "node");
            need(n.size() == 36, "preset: 36 node states expected");
            std::copy(n.begin(), n.end(), r.talents.nodes.begin());
        } else if (k == "rolls") {
            const auto n = numbers<uint8_t>(v, "roll");
            need(n.size() == 8, "preset: 8 roll bytes expected");
            std::copy(n.begin(), n.end(), r.talents.rolls.begin());
        } else if (k == "abilities") {
            const auto t = splitChar(v, ',');
            need(t.size() == 5, "preset: 5 ability triples expected");
            for (size_t i = 0; i < 5; ++i) {
                const auto p = splitChar(t[i], '.');
                need(p.size() == 3, "preset: ability triple");
                for (size_t j = 0; j < 3; ++j) r.talents.abilities[i][j] = uint8_t(num(p[j], "ability byte"));
            }
        }
    }
    need(haveMode, "preset: no mode");
    return r;
}

}   
