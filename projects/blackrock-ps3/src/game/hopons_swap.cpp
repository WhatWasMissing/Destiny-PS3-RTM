#include "game/hopons_swap.h"

#include <algorithm>
#include <ctime>
#include <sstream>

#include "core/error.h"
#include "core/runtime.h"
#include "game/hopons_addr.h"
#include "game/hopons_catalog.h"
#include "game/world_tags.h"

namespace brps3::hopons {

namespace {
bool entityTag(uint32_t v) { return v >= 0x80800000u && v <= 0x81FFFFFFu; }
}   

bool decodeSquadStructure(const std::vector<uint8_t>& b, std::vector<StructuredSite>& out, uint32_t& members, std::string& why) {
    out.clear();
    members = 0;
    const uint32_t extent = uint32_t(b.size());
    auto word = [&](int64_t off, uint32_t& v) {
        if (off < 0 || off % 4 || uint64_t(off) + 4 > extent) return false;
        v = be32At(b, size_t(off));
        return true;
    };
    uint32_t size = 0, memberRelative = 0;
    if (!word(0, size) || size < 0x34 || size > extent || size % 4) {
        why = "authored size is not plausible";
        return false;
    }
    if (!word(0x1C, members) || !word(0x20, memberRelative) || members > 8192) {
        why = "member count is not plausible";
        return false;
    }
    if (!members) return true;
    const int64_t header = 0x20 + int64_t(int32_t(memberRelative));
    if (header < 4 || header % 4 || header + 16 + int64_t(members) * 28 > int64_t(size)) {
        why = "member array exceeds the authored size";
        return false;
    }
    uint32_t w[5];
    for (int i = 0; i < 5; ++i)
        if (!word(header - 4 + i * 4, w[i])) return false;
    if (w[0] != cls::ARRAY || w[1] != members || w[2] != cls::SQUAD_MEMBER || w[3] || w[4]) {
        why = "member array has an invalid typed header";
        return false;
    }
    for (uint32_t m = 0; m < members; ++m) {
        const int64_t memberOffset = header + 16 + int64_t(m) * 28;
        for (uint32_t v = 0; v < 3; ++v) {
            const int64_t field = memberOffset + 4 + int64_t(v) * 8;
            uint32_t combatants = 0, variantRelative = 0;
            if (!word(field, combatants) || !word(field + 4, variantRelative) || combatants > 8192) {
                why = "member " + std::to_string(m) + " variant " + std::to_string(v) + " is not plausible";
                return false;
            }
            if (!combatants) continue;
            const int64_t h2 = field + 4 + int64_t(int32_t(variantRelative));
            if (h2 < 4 || h2 % 4 || h2 + 16 + int64_t(combatants) * 12 > int64_t(size)) {
                why = "combatant array exceeds the authored size";
                return false;
            }
            for (int i = 0; i < 5; ++i)
                if (!word(h2 - 4 + i * 4, w[i])) return false;
            if (w[0] != cls::ARRAY || w[1] != combatants || w[2] != cls::COMBATANT_ROW || w[3] || w[4]) {
                why = "combatant array has an invalid typed header";
                return false;
            }
            for (uint32_t c = 0; c < combatants; ++c) {
                const int64_t row = h2 + 16 + int64_t(c) * 12;
                uint32_t rel = 0, name = 0, weight = 0;
                if (!word(row, rel) || !word(row + 4, name) || !word(row + 8, weight)) return false;
                const int64_t entity = row + int64_t(int32_t(rel));
                uint32_t marker = 0, tag = 0;
                if (entity < 4 || entity % 4 || entity + 4 > int64_t(size) || !word(entity - 4, marker) || !word(entity, tag) || marker != cls::ENTITY_MARKER ||
                    !entityTag(tag)) {
                    why = "combatant " + std::to_string(m) + "." + std::to_string(v) + "." + std::to_string(c) + " has no entity reference";
                    return false;
                }
                out.push_back({m, v, c, uint32_t(entity), weight});
            }
        }
    }
    return true;
}

std::vector<uint32_t> markerSites(const std::vector<uint8_t>& b) {
    std::vector<uint32_t> out;
    for (size_t a = 0; a + 8 <= b.size(); a += 4)
        if (be32At(b, a) == cls::ENTITY_MARKER && entityTag(be32At(b, a + 4))) out.push_back(uint32_t(a + 4));
    return out;
}

std::vector<uint32_t> SquadDefinition::entities() const {
    std::vector<uint32_t> out;
    for (const LiveSite& s : sites)
        if (std::find(out.begin(), out.end(), s.entity) == out.end()) out.push_back(s.entity);
    return out;
}

std::optional<SquadDefinition> readSquadDefinition(Reader& mem, uint32_t tag, std::string& why) {
    TagReader tags(mem);
    const auto m = tags.map(tag, world::kPackageItemStride, &why);
    if (!m) return std::nullopt;
    if (m->word != cls::SQUAD_DEFINITION) {
        why = "item class " + hex8(m->word) + " is not a squad definition (808012D9)";
        return std::nullopt;
    }
    SquadDefinition d;
    d.tag = tag;
    d.body = m->body;
    d.size = mem.u32(m->body);
    if (d.size < 0x34 || d.size > 0x40000 || d.size % 4 || !storage(d.body, d.size)) {
        why = "authored size " + hexShort(d.size) + " is not plausible";
        return std::nullopt;
    }
    d.bytes = mem.bytes(d.body, d.size).data();
    std::vector<StructuredSite> structured;
    d.structured = decodeSquadStructure(d.bytes, structured, d.members, d.structureNote);
    const SquadDefRow* cat = squadCatalog().find(tag);
    for (uint32_t off : markerSites(d.bytes)) {
        LiveSite s;
        s.offset = off;
        s.address = d.body + off;
        s.entity = be32At(d.bytes, off);
        for (const StructuredSite& st : structured)
            if (st.offset == off) {
                s.member = int(st.member);
                s.variant = int(st.variant);
                s.combatant = int(st.combatant);
                s.weight = st.weight;
                if (cat)
                    for (const SquadSite& cs : cat->sites)
                        if (cs.member == st.member && cs.variant == st.variant && cs.combatant == st.combatant) s.catalogEntity = cs.entity;
            }
        d.sites.push_back(s);
    }
    if (d.structured && structured.size() != d.sites.size())
        d.structureNote = std::to_string(structured.size()) + " structured sites vs " + std::to_string(d.sites.size()) + " marker sites";
    if (d.sites.empty()) {
        why = "no entity sites (80800406 markers) in this squad definition";
        return std::nullopt;
    }
    return d;
}

std::optional<SwapJournal> planSwap(Reader& mem, const SquadDefinition& def, const std::vector<uint32_t>& offsets, uint32_t entity, std::string& why) {
    if (offsets.empty()) {
        why = "choose at least one site";
        return std::nullopt;
    }
    TagReader tags(mem);
    const auto m = tags.map(def.tag, world::kPackageItemStride, &why);
    if (!m) return std::nullopt;
    if (m->word != cls::SQUAD_DEFINITION || m->body != def.body) {
        why = "the squad definition was reloaded or moved: read it again";
        return std::nullopt;
    }
    const world::TagTable t = world::readTagTable(mem);
    const world::Residency r = world::checkResident(mem, t, entity);
    if (!r.resident) {
        why = "replacement " + hex8(entity) + " is not resident: " + r.why + " (pick an enemy loaded in this area)";
        return std::nullopt;
    }
    SwapJournal j;
    j.tag = def.tag;
    j.body = def.body;
    j.entity = entity;
    j.at = int64_t(std::time(nullptr));
    for (uint32_t off : offsets) {
        const auto it = std::find_if(def.sites.begin(), def.sites.end(), [&](const LiveSite& s) { return s.offset == off; });
        if (it == def.sites.end()) {
            why = "site +" + hexShort(off) + " is not an entity site";
            return std::nullopt;
        }
        const uint32_t now = mem.u32(it->address);
        if (now == entity) continue;    
        if (now != it->entity) {
            why = "site +" + hexShort(off) + " holds " + hex8(now) + ", not " + hex8(it->entity) + ": read the definition again";
            return std::nullopt;
        }
        j.words.push_back({it->address, now});
    }
    if (j.words.empty()) {
        why = "every chosen site already holds " + hex8(entity);
        return std::nullopt;
    }
    return j;
}

void applySwap(Runtime& rt, Reader& mem, const SwapJournal& j) {
    for (const auto& [addr, original] : j.words)
        if (mem.u32(addr) != original) throw Error("site " + hex8(addr) + " changed before the write: nothing written");
    const std::vector<uint8_t> v = {uint8_t(j.entity >> 24), uint8_t(j.entity >> 16), uint8_t(j.entity >> 8), uint8_t(j.entity)};
    for (const auto& w : j.words) rt.write(w.first, v.data(), v.size(), true);
}

RestoreResult restoreSwap(Runtime& rt, Reader& mem, const SwapJournal& j) {
    RestoreResult r;
    TagReader tags(mem);
    std::string why;
    const auto m = tags.map(j.tag, world::kPackageItemStride, &why);
    if (!m || m->word != cls::SQUAD_DEFINITION || m->body != j.body) {
        r.skipped = j.words.size();
        r.lines.push_back("squad definition " + hex8(j.tag) + " is no longer at " + hex8(j.body) + " (reloaded): nothing to restore");
        return r;
    }
    for (const auto& [addr, original] : j.words) {
        const uint32_t now = mem.u32(addr);
        if (now == original) {
            ++r.already;
            continue;
        }
        if (now != j.entity) {
            ++r.skipped;
            r.lines.push_back("site " + hex8(addr) + " holds " + hex8(now) + " (changed independently): left alone");
            continue;
        }
        const std::vector<uint8_t> v = {uint8_t(original >> 24), uint8_t(original >> 16), uint8_t(original >> 8), uint8_t(original)};
        rt.write(addr, v.data(), v.size(), true);
        ++r.restored;
    }
    return r;
}

std::string serialiseSwaps(const std::vector<SwapJournal>& swaps) {
    std::ostringstream s;
    s << "BR_PS3_SWAP_RECOVERY\t1\n";
    for (const SwapJournal& j : swaps) {
        std::string label = j.label;
        std::replace(label.begin(), label.end(), '\t', ' ');
        s << "S\t" << hex8(j.tag) << '\t' << hex8(j.body) << '\t' << hex8(j.entity) << '\t' << j.at << '\t' << label << '\t' << j.words.size() << '\n';
        for (const auto& w : j.words) s << "W\t" << hex8(w.first) << '\t' << hex8(w.second) << '\n';
    }
    return s.str();
}

std::vector<SwapJournal> parseSwaps(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line.rfind("BR_PS3_SWAP_RECOVERY\t1", 0) != 0) throw Error("not a swap recovery journal");
    auto fields = [](const std::string& l) {
        std::vector<std::string> f;
        std::string cur;
        for (char c : l) {
            if (c == '\t') {
                f.push_back(cur);
                cur.clear();
            } else if (c != '\r') {
                cur += c;
            }
        }
        f.push_back(cur);
        return f;
    };
    std::vector<SwapJournal> out;
    while (std::getline(in, line)) {
        if (line.empty() || line == "\r") continue;
        const auto f = fields(line);
        if (f.size() != 7 || f[0] != "S") throw Error("malformed swap entry");
        SwapJournal j;
        j.tag = uint32_t(std::stoul(f[1], nullptr, 16));
        j.body = uint32_t(std::stoul(f[2], nullptr, 16));
        j.entity = uint32_t(std::stoul(f[3], nullptr, 16));
        j.at = std::stoll(f[4]);
        j.label = f[5];
        const size_t n = size_t(std::stoul(f[6]));
        if (n > 4096) throw Error("malformed swap entry");
        for (size_t i = 0; i < n; ++i) {
            if (!std::getline(in, line)) throw Error("truncated swap journal");
            const auto w = fields(line);
            if (w.size() != 3 || w[0] != "W") throw Error("malformed swap word");
            j.words.push_back({uint32_t(std::stoul(w[1], nullptr, 16)), uint32_t(std::stoul(w[2], nullptr, 16))});
        }
        out.push_back(std::move(j));
    }
    return out;
}

}   
