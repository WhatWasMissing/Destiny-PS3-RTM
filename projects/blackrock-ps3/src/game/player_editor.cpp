#include "game/player_editor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

#include "core/error.h"
#include "game/names_catalog.h"
#include "game/player_addr.h"
#include "game/player_records.h"
#include "game/world_addr.h"
#include "game/world_components.h"
#include "game/world_objects.h"
#include "game/world_player.h"

namespace brps3::player::editor {

namespace {

using world::hex8;

void put32(std::vector<uint8_t>& b, uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) b.push_back(uint8_t(v >> s));
}
uint32_t get32(const std::vector<uint8_t>& b, size_t o) {
    return uint32_t(b[o]) << 24 | uint32_t(b[o + 1]) << 16 | uint32_t(b[o + 2]) << 8 | b[o + 3];
}

std::vector<std::string> splitTabs(const std::string& line) {
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

uint32_t hexField(const std::string& s, const char* what) { return world::parseHex(s, what); }

bool atomicWrite(const std::filesystem::path& path, const std::string& text, std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const auto tmp = path.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << text;
        f.flush();
        if (!f) {
            error = "could not write " + tmp;
            return false;
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        std::filesystem::rename(tmp, path, ec);
    }
    if (ec) {
        error = "could not replace " + path.string() + ": " + ec.message();
        return false;
    }
    return true;
}

ComponentRef refOf(const world::SObject& o, const world::Accessor& a) {
    ComponentRef r;
    r.owner = o.handle;
    r.ownerEntry = o.addr;
    r.ownerHead = o.componentHead;
    r.accessor = a.addr;
    r.flags = a.flags;
    r.instance = a.instance;
    r.back = a.backOffset;
    r.cls = a.cls();
    r.data = a.data;
    r.size = a.size();
    return r;
}

}   

 

double Field::value() const { return integer ? double(raw) : double(world::bitsFloat(raw)); }

std::string Field::label() const { return known() ? name : "Record id " + std::to_string(id); }

std::vector<Field> decodeFields(const world::Bytes& data) {
    std::vector<Field> out;
    for (const Record& r : decodeRecords(data)) {
        Field f;
        f.offset = r.offset;
        f.id = r.id;
        f.tag = r.tag;
        f.raw = r.value;
        f.copy = r.copy;
        if (const FieldInfo* info = fieldInfo(r.id, r.tag)) {
            f.name = info->name;
            f.group = info->group;
            f.unit = info->unit;
            f.note = info->note;
        } else {
            f.group = "Other";
            f.unit = "value";
        }
        out.push_back(std::move(f));
    }
    if (data.has(wfc::AMMO_TAG_OFFSET, 0xC) && data.u32(wfc::AMMO_TAG_OFFSET) == wfc::AMMO_TAG) {
        for (uint32_t o : {wfc::AMMO_CLIP, wfc::AMMO_RESERVE}) {
            Field f;
            f.offset = o;
            f.tag = wfc::AMMO_TAG;
            f.raw = data.u32(o);
            f.copy = f.raw;
            f.integer = true;
            f.mirrored = false;
            f.name = o == wfc::AMMO_CLIP ? "Clip ammo" : "Reserve ammo";
            f.group = "Ammo";
            f.unit = "rounds";
            f.note = o == wfc::AMMO_CLIP ? "live clip count in rounds (a fusion charge is 7 rounds)" : "reserve in rounds; only moves on reload";
            out.push_back(std::move(f));
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const Field& a, const Field& b) {
        const int ga = groupOrder(a.group), gb = groupOrder(b.group);
        if (ga != gb) return ga < gb;
        if (a.known() != b.known()) return a.known();
        return a.offset < b.offset;
    });
    return out;
}

 

Scan scanWeapons(world::Reader& mem) {
    Scan s;
    const world::LocalPlayer p = world::requireLocalPlayer(mem);
    s.player = p.handle;
    const world::ObjectScan objects = world::readObjects(mem);
    const world::AccessorTable table = world::readAccessorTable(mem);
    for (const world::SObject& o : world::childrenOf(objects, p.handle)) {
        if (o.type != world::TYPE_WEAPON || !o.live()) continue;
        Weapon w;
        w.handle = o.handle;
        w.entry = o.addr;
        w.name = "Weapon " + hex8(o.handle);
        const auto arenas = world::arenaChain(table, o.componentHead);
        for (const world::Accessor& a : world::componentsIn(table, arenas)) w.components.push_back(refOf(o, a));
         
        for (const ComponentRef& c : w.components)
            if (c.cls == kWeaponArchetypeClass && c.size >= 0x80) {
                if (const auto b = mem.tryBytes(c.data, 0x80)) w.archetype = b->u32(0x7C);
                break;
            }
        if (w.archetype) {
            const std::string n = names::tagName(w.archetype);
            if (!n.empty()) w.name = n;
        }
         
        std::vector<ComponentRef> order;
        if (world::validHandle(o.component) && (o.component & 0x1FFF) < table.count()) {
            const world::Accessor a = table.at(o.component & 0x1FFF);
            if (a.identity == o.component && a.allocated() && a.size() >= 0x20 && world::heap(a.data, a.size())) order.push_back(refOf(o, a));
        }
        std::vector<ComponentRef> big;
        for (const ComponentRef& c : w.components)
            if (c.size >= 0x400 && c.size <= 0x10000 && (order.empty() || c.accessor != order.front().accessor)) big.push_back(c);
        std::sort(big.begin(), big.end(), [](const ComponentRef& a, const ComponentRef& b) { return a.size > b.size; });
        if (big.size() > 3) big.resize(3);
        order.insert(order.end(), big.begin(), big.end());
        for (const ComponentRef& c : order) {
            if (c.size > 0x10000) continue;
            const auto bytes = mem.tryBytes(c.data, c.size);
            if (!bytes) continue;
            auto fields = decodeFields(*bytes);
            if (fields.size() > w.fields.size()) {
                w.fire = c;
                w.fire.definition = bytes->u32(0);
                w.fields = std::move(fields);
            }
            if (w.fields.size() >= 8) break;
        }
        if (w.fields.empty()) w.note = "No self-validating records found on this weapon's components (see Port checks).";
        s.weapons.push_back(std::move(w));
    }
    if (s.weapons.empty()) s.notes.push_back("No equipped weapon objects found: equip a weapon and refresh.");
    return s;
}

bool validate(world::Reader& mem, const ComponentRef& r, std::string& error) {
    auto fail = [&](const char* why) {
        error = why;
        return false;
    };
    try {
        if (!world::heap(r.data, r.size) || r.size < 0x20 || r.size > 0x400000) return fail("Component address is not plausible.");
         
         
        const world::PoolHeader objects = world::readPool(mem, world::addr::SOBJECT_POOL);
        const auto expected = objects.entryOf(r.owner);
        if (!objects.plausible() || objects.stride != world::sobj::STRIDE || !expected || *expected != r.ownerEntry)
            return fail("The object pool moved (activity change?). Refresh.");
        const world::Bytes owner = mem.bytes(r.ownerEntry, 0x40);
        if (owner.u32(0xC) != r.owner || (owner.u32(4) & world::sobj::FLAG_DELETING)) return fail("The weapon is gone. Refresh; no write was made.");
        const world::AccessorPool pool = world::readAccessorPool(mem);
        if (!pool.plausible()) return fail("The component pool is not set up (activity change?).");
        if (r.accessor < pool.header.base || (r.accessor - pool.header.base) % world::kAccessorStride ||
            r.accessor >= pool.header.base + pool.header.capacity * world::kAccessorStride)
            return fail("The component pool moved (activity change?). Refresh.");
        const world::Bytes a = mem.bytes(r.accessor, world::kAccessorStride);
        if (a.u32(0) != r.flags || a.u32(4) != r.instance || a.u32(8) != r.back || a.u32(0x14) != r.cls ||
            r.accessor - (a.u32(8) & pool.mask) != r.data)
            return fail("The component changed. Refresh before editing; no write was made.");
        const world::Bytes head = mem.bytes(r.data, 0x20);
        if (head.u32(0x1C) != r.owner) return fail("The component no longer belongs to this weapon. Refresh.");
        if (r.definition && head.u32(0) != r.definition) return fail("The component's definition changed. Refresh.");
        const uint32_t curHead = owner.u32(0x3C);
        const auto arenas = world::readArenaChain(mem, pool, curHead);
        const bool inside = std::any_of(arenas.begin(), arenas.end(), [&](const world::Arena& ar) {
            return r.data >= ar.data && uint64_t(r.data) + r.size <= uint64_t(ar.data) + ar.size;
        });
        const bool isArena = std::any_of(arenas.begin(), arenas.end(), [&](const world::Arena& ar) { return pool.entry(ar.index) == r.accessor; });
        if (!inside || isArena) return fail("The component is no longer attached to this weapon. Refresh.");
        const world::Bytes again = mem.bytes(r.ownerEntry, 0x40);
        if (again.u32(0xC) != r.owner || again.u32(0x3C) != curHead) return fail("The weapon changed during validation. Refresh.");
        return true;
    } catch (const std::exception& e) {
        error = std::string("Could not validate the component: ") + e.what();
        return false;
    }
}

std::optional<world::Bytes> readComponent(world::Reader& mem, const ComponentRef& r, std::string& error) {
    if (!validate(mem, r, error)) return std::nullopt;
    auto b = mem.tryBytes(r.data, std::min<uint32_t>(r.size, 0x10000));
    if (!b) {
        error = "Component read failed.";
        return std::nullopt;
    }
    if (!validate(mem, r, error)) return std::nullopt;
    return b;
}

 

namespace {

bool sameField(const Field& a, const Field& b) {
    return a.offset == b.offset && a.id == b.id && a.tag == b.tag && a.integer == b.integer && a.mirrored == b.mirrored;
}

std::optional<std::string> checkValue(const Field& f, double v) {
    if (!std::isfinite(v)) return "Enter a finite value for " + f.label() + ".";
    if (f.integer && (v < 0 || v > 1000000 || std::floor(v) != v)) return f.label() + ": whole rounds from 0 to 1,000,000.";
    if (!f.integer && std::fabs(v) > 1.0e6) return f.label() + ": values above 1,000,000 are refused.";
    return std::nullopt;
}

}   

bool apply(world::Reader& mem, const ByteWriter& write, const ComponentRef& ref, uint32_t weapon, const std::vector<Change>& changes,
           std::vector<Edit>& journal, const Checkpoint& checkpoint, std::string& error, size_t& done) {
    done = 0;
    error.clear();
    if (changes.empty()) return true;
    std::vector<uint32_t> offsets;
    for (const Change& c : changes) {
        if (auto bad = checkValue(c.field, c.value)) {
            error = *bad;
            return false;
        }
        if (std::find(offsets.begin(), offsets.end(), c.field.offset) != offsets.end()) {
            error = "The same field appears twice.";
            return false;
        }
        offsets.push_back(c.field.offset);
    }
    auto bytes = readComponent(mem, ref, error);
    if (!bytes) return false;
    const auto live = decodeFields(*bytes);
    for (const Change& c : changes) {
        auto it = std::find_if(live.begin(), live.end(), [&](const Field& f) { return sameField(f, c.field); });
        if (it == live.end()) {
            error = c.field.label() + ": the field is no longer there. Refresh.";
            return false;
        }
        if (it->raw != c.field.raw || (c.field.mirrored && it->copy != c.field.copy)) {
            error = c.field.label() + " changed since it was read. Refresh before applying.";
            return false;
        }
    }
    for (const Change& c : changes) {
        const Field& f = c.field;
        const uint32_t raw = f.integer ? uint32_t(c.value) : world::floatBits(float(c.value));
        Edit e;
        e.component = ref;
        e.component.definition = bytes->u32(0);
        e.weapon = weapon;
        e.offset = f.offset;
        e.label = f.label();
        put32(e.original, f.raw);
        put32(e.applied, raw);
        if (f.mirrored) {
            put32(e.original, f.copy);
            put32(e.applied, raw);
        }
        if (e.original == e.applied) {
            ++done;
            continue;
        }
        if (!validate(mem, ref, error)) return false;
        journal.push_back(e);    
        if (checkpoint && !checkpoint(journal)) {
            journal.pop_back();
            error = "Could not save the recovery bytes; no write was made.";
            return false;
        }
        try {
            write(ref.data + f.offset, e.applied);
        } catch (const std::exception& ex) {
            error = f.label() + ": write not verified (" + ex.what() + "). The recovery entry is kept; refresh and inspect.";
            return false;
        }
        ++done;
    }
    return true;
}

bool restoreEdit(world::Reader& mem, const ByteWriter& write, const Edit& e, std::string& error) {
    if (!validate(mem, e.component, error)) return false;
    const size_t n = e.original.size();
    if ((n != 4 && n != 8) || e.applied.size() != n || e.offset % 4 || uint64_t(e.offset) + n > e.component.size) {
        error = "Invalid recovery entry.";
        return false;
    }
    const auto cur = mem.tryBytes(e.component.data + e.offset, uint32_t(n));
    if (!cur) {
        error = "Cannot read the value to restore.";
        return false;
    }
    if (cur->data() == e.original) return true;
    for (size_t i = 0; i < n; i += 4) {
        const uint32_t v = get32(cur->data(), i);
        if (v != get32(e.applied, i) && v != get32(e.original, i)) {
            error = e.label + ": the value changed after this edit; restoring would overwrite a newer change.";
            return false;
        }
    }
    try {
        write(e.component.data + e.offset, e.original);
    } catch (const std::exception& ex) {
        error = e.label + ": restore not verified (" + ex.what() + ").";
        return false;
    }
    return true;
}

bool restoreAll(world::Reader& mem, const ByteWriter& write, std::vector<Edit>& journal, const Checkpoint& checkpoint, std::string& error,
                size_t& restored) {
    restored = 0;
    error.clear();
    while (!journal.empty()) {
        const Edit e = journal.back();
        if (!restoreEdit(mem, write, e, error)) {
            error = e.label + ": " + error;
            return false;
        }
        journal.pop_back();
        if (checkpoint && !checkpoint(journal)) {
            journal.push_back(e);
            error = e.label + ": restored, but the recovery file could not be updated.";
            return false;
        }
        ++restored;
    }
    return true;
}

 

std::filesystem::path journalPath() { return state_dir() / "player" / "editor_journal.tsv"; }

std::string serializeJournal(const std::vector<Edit>& edits) {
    std::ostringstream o;
    o << "# Blackrock PS3 weapon editor recovery: original bytes of every edit (newest last)\n";
    for (const Edit& e : edits) {
        const ComponentRef& c = e.component;
        std::string label = e.label;
        std::replace(label.begin(), label.end(), '\t', ' ');
        o << "edit\t" << label << '\t' << hex8(e.weapon) << '\t' << hex8(c.owner) << '\t' << hex8(c.ownerEntry) << '\t' << hex8(c.ownerHead) << '\t'
          << hex8(c.accessor) << '\t' << hex8(c.flags) << '\t' << hex8(c.instance) << '\t' << hex8(c.back) << '\t' << hex8(c.cls) << '\t'
          << hex8(c.data) << '\t' << hex8(c.size) << '\t' << hex8(c.definition) << '\t' << hex8(e.offset) << '\t' << hexBytes(e.original) << '\t'
          << hexBytes(e.applied) << '\n';
    }
    return o.str();
}

std::vector<Edit> parseJournal(const std::string& text) {
    std::vector<Edit> out;
    std::istringstream in(text);
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        ++n;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const auto f = splitTabs(line);
        if (f.size() != 17 || f[0] != "edit") throw Error("editor journal line " + std::to_string(n) + " is malformed");
        Edit e;
        e.label = f[1];
        e.weapon = hexField(f[2], "weapon");
        ComponentRef& c = e.component;
        c.owner = hexField(f[3], "owner");
        c.ownerEntry = hexField(f[4], "owner entry");
        c.ownerHead = hexField(f[5], "owner head");
        c.accessor = hexField(f[6], "accessor");
        c.flags = hexField(f[7], "flags");
        c.instance = hexField(f[8], "instance");
        c.back = hexField(f[9], "back offset");
        c.cls = hexField(f[10], "class");
        c.data = hexField(f[11], "data");
        c.size = hexField(f[12], "size");
        c.definition = hexField(f[13], "definition");
        e.offset = hexField(f[14], "offset");
        e.original = parseHexBytes(f[15]);
        e.applied = parseHexBytes(f[16]);
        if ((e.original.size() != 4 && e.original.size() != 8) || e.applied.size() != e.original.size() || e.offset % 4 ||
            uint64_t(e.offset) + e.original.size() > c.size)
            throw Error("editor journal line " + std::to_string(n) + " has an invalid entry");
        out.push_back(std::move(e));
    }
    return out;
}

 

std::filesystem::path presetsPath() { return state_dir() / "player" / "weapon_presets.tsv"; }

std::string serializePresets(const std::vector<Preset>& presets) {
    std::ostringstream o;
    o << "# Blackrock PS3 weapon presets: preset<TAB>name<TAB>archetype<TAB>component class, then its fields\n";
    o << "# field<TAB>id<TAB>tag<TAB>integer<TAB>offset<TAB>value\n";
    char buf[64];
    for (const Preset& p : presets) {
        std::string name = p.name;
        std::replace(name.begin(), name.end(), '\t', ' ');
        o << "preset\t" << name << '\t' << hex8(p.archetype) << '\t' << hex8(p.componentClass) << '\n';
        for (const PresetField& f : p.fields) {
            std::snprintf(buf, sizeof buf, "%.9g", f.value);
            o << "field\t" << f.id << '\t' << hex8(f.tag) << '\t' << (f.integer ? 1 : 0) << '\t' << hex8(f.offset) << '\t' << buf << '\n';
        }
    }
    return o.str();
}

std::vector<Preset> parsePresets(const std::string& text) {
    std::vector<Preset> out;
    std::istringstream in(text);
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        ++n;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const auto f = splitTabs(line);
        const std::string where = "weapon presets line " + std::to_string(n);
        if (f[0] == "preset") {
            if (f.size() != 4 || f[1].empty()) throw Error(where + ": malformed preset");
            for (const Preset& p : out)
                if (p.name == f[1]) throw Error(where + ": duplicate preset name " + f[1]);
            Preset p;
            p.name = f[1];
            p.archetype = hexField(f[2], "archetype");
            p.componentClass = hexField(f[3], "component class");
            out.push_back(std::move(p));
        } else if (f[0] == "field") {
            if (out.empty() || f.size() != 6) throw Error(where + ": malformed field");
            PresetField pf;
            try {
                pf.id = uint32_t(std::stoul(f[1]));
                pf.value = std::stod(f[5]);
            } catch (const std::exception&) {
                throw Error(where + ": not a number");
            }
            pf.tag = hexField(f[2], "tag");
            pf.integer = f[3] == "1";
            pf.offset = hexField(f[4], "offset");
            if (!std::isfinite(pf.value)) throw Error(where + ": value is not finite");
            out.back().fields.push_back(pf);
        } else {
            throw Error(where + ": unknown line");
        }
    }
    return out;
}

std::vector<Preset> loadPresets(std::string& error) {
    error.clear();
    std::error_code ec;
    if (!std::filesystem::exists(presetsPath(), ec)) return {};
    try {
        std::ifstream f(presetsPath(), std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        return parsePresets(ss.str());
    } catch (const std::exception& e) {
        error = e.what();
        return {};
    }
}

bool savePresets(const std::vector<Preset>& presets, std::string& error) { return atomicWrite(presetsPath(), serializePresets(presets), error); }

PresetMatch matchPreset(const Preset& p, const Weapon& w) {
    PresetMatch m;
    m.sameWeapon = p.archetype == w.archetype && p.componentClass == w.fire.cls;
    for (const PresetField& pf : p.fields) {
         
         
        size_t found = w.fields.size();
        for (size_t i = 0; i < w.fields.size(); ++i) {
            const Field& f = w.fields[i];
            if (f.integer != pf.integer || f.tag != pf.tag) continue;
            if (f.integer) {
                if (f.offset == pf.offset) {
                    found = i;
                    break;
                }
                continue;
            }
            if (f.id != pf.id) continue;
            if (f.offset == pf.offset) {
                found = i;
                break;
            }
            if (found == w.fields.size()) found = i;
        }
        if (found == w.fields.size()) ++m.missing;
        else m.values.push_back({found, pf.value});
    }
    return m;
}

Preset presetFrom(const std::string& name, const Weapon& w, const std::vector<double>& values) {
    Preset p;
    p.name = name;
    p.archetype = w.archetype;
    p.componentClass = w.fire.cls;
    for (size_t i = 0; i < w.fields.size(); ++i) {
        const Field& f = w.fields[i];
        p.fields.push_back({f.id, f.tag, f.offset, f.integer, i < values.size() ? values[i] : f.value()});
    }
    return p;
}

const std::vector<QuickPreset>& quickPresets() {
    static const std::vector<QuickPreset> q = {
        {"Zero recoil", "Every Recoil field (vertical, horizontal, kick, bounce) to 0."},
        {"Zero spread", "Crosshair bloom and spread cone to 0."},
        {"Burst fire", "The four shots-per-trigger-pull fields to the burst count (Xbox test: 7 made an auto rifle fire seven per pull)."},
        {"Full auto", "The four shots-per-trigger-pull fields to 0 (what an auto rifle reads)."},
        {"Max ammo", "Clip and reserve to 999 rounds."},
    };
    return q;
}

std::vector<std::pair<size_t, double>> quickValues(size_t index, const Weapon& w, int burst) {
    std::vector<std::pair<size_t, double>> out;
    for (size_t i = 0; i < w.fields.size(); ++i) {
        const Field& f = w.fields[i];
        switch (index) {
        case 0:
            if (f.group == "Recoil" && f.known()) out.push_back({i, 0.0});
            break;
        case 1:
            if (f.group == "Spread" && f.known()) out.push_back({i, 0.0});
            break;
        case 2:
        case 3:
            if (f.id == 14240 || f.id == 12208 || f.id == 12240 || f.id == 12224) out.push_back({i, index == 2 ? double(std::clamp(burst, 1, 32)) : 0.0});
            break;
        case 4:
            if (f.integer && f.tag == wfc::AMMO_TAG) out.push_back({i, 999.0});
            break;
        default: break;
        }
    }
    return out;
}

}   
