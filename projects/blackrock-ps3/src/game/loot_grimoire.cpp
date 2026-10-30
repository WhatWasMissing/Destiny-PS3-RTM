#include "game/loot_grimoire.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include "core/error.h"
#include "core/ppc.h"
#include "core/sha256.h"

namespace brps3::loot::grim {

using world::Bytes;

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}   

std::string Flag::group() const {
    const size_t colon = name.find(':');
    if (colon == std::string::npos || colon == 0 || colon > 28) return {};
    return name.substr(0, colon);
}

Catalog Catalog::parse(const std::string& tsv) {
    Catalog c;
    std::istringstream in(tsv);
    std::string line;
    int lineNo = 0;
    bool header = false;
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        auto fields = [&] {
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
            return f;
        };
        if (line[0] == '#') {
            const auto f = fields();
            if (f.size() < 2) continue;
            const std::string key = f[0].substr(f[0].find_first_not_of("# "));
            try {
                if (key == "core_first") c.coreFirst = uint32_t(std::stoul(f[1]));
                else if (key == "core_size") c.coreSize = uint32_t(std::stoul(f[1]));
                else if (key == "flags_size") c.flagsSize = uint32_t(std::stoul(f[1]));
                else if (key == "core_sha256") c.coreSha256 = f[1];
                else if (key == "flags_sha256") c.flagsSha256 = f[1];
            } catch (const std::exception&) {
                throw Error("grimoire.tsv line " + std::to_string(lineNo) + ": bad header value");
            }
            continue;
        }
        if (!header) {
            header = true;
            continue;
        }
        const auto f = fields();
        if (f.size() < 3) throw Error("grimoire.tsv line " + std::to_string(lineNo) + ": expected 3 columns");
        Flag fl;
        try {
            fl.ordinal = uint16_t(std::stoul(f[0]));
            fl.dense = uint16_t(std::stoul(f[1]));
        } catch (const std::exception&) {
            throw Error("grimoire.tsv line " + std::to_string(lineNo) + ": bad number");
        }
        if (fl.dense >= GRIM_FLAGS_SPAN) throw Error("grimoire.tsv line " + std::to_string(lineNo) + ": dense index out of range");
        fl.name = f[2];
        c.flags.push_back(std::move(fl));
    }
    if (c.flags.empty() || !c.coreSize || !c.flagsSize || c.coreSha256.size() != 64 || c.flagsSha256.size() != 64)
        throw Error("grimoire.tsv is incomplete (flags or table identity missing)");
    return c;
}

const Flag* Catalog::byOrdinal(uint16_t ordinal) const {
    for (const Flag& f : flags)
        if (f.ordinal == ordinal) return &f;
    return nullptr;
}

std::vector<size_t> Catalog::search(const std::string& query) const {
    std::vector<std::string> w;
    {
        std::string cur;
        for (char ch : lower(query)) {
            if (ch == ' ') {
                if (!cur.empty()) w.push_back(cur);
                cur.clear();
            } else {
                cur += ch;
            }
        }
        if (!cur.empty()) w.push_back(cur);
    }
    std::vector<size_t> out;
    for (size_t i = 0; i < flags.size(); ++i) {
        const std::string hay = lower(flags[i].name) + " #" + std::to_string(flags[i].ordinal);
        bool ok = true;
        for (const std::string& word : w)
            if (hay.find(word) == std::string::npos) {
                ok = false;
                break;
            }
        if (ok) out.push_back(i);
    }
    return out;
}

const Catalog& catalog() {
    static const Catalog cat = [] {
        try {
            std::ifstream f(world::dataFile("loot/grimoire.tsv"), std::ios::binary);
            if (!f) throw Error("data/loot/grimoire.tsv is missing (rebuild, or run tools/loot_build.py)");
            std::ostringstream ss;
            ss << f.rdbuf();
            return Catalog::parse(ss.str());
        } catch (const std::exception& e) {
            Catalog empty;
            empty.error = e.what();
            return empty;
        }
    }();
    return cat;
}

Tables verifyTables(world::Reader& mem, Resolver& res, const Catalog& cat) {
    Tables t;
    std::string why;
    const auto core = res.body(PS3_GRIM_CORE_TAG, true, &why);
    if (!core) throw Error("The Grimoire core table " + hex(PS3_GRIM_CORE_TAG) + " does not resolve (" + why + "): load a character first");
    const auto flags = res.body(PS3_GRIM_FLAGS_TAG, true, &why);
    if (!flags) throw Error("The Grimoire flag table " + hex(PS3_GRIM_FLAGS_TAG) + " does not resolve (" + why + ")");
    t.core = *core;
    t.flags = *flags;
    const std::vector<uint8_t> a = mem.read(t.core, cat.coreSize);
    if (sha256Hex(a.data(), a.size()) != cat.coreSha256) throw Error("The live Grimoire table differs from the mapped build");
    const std::vector<uint8_t> b = mem.read(t.flags, cat.flagsSize);
    if (sha256Hex(b.data(), b.size()) != cat.flagsSha256) throw Error("The live Grimoire unlock-flag table differs from the mapped build");
    return t;
}

void verifyContextClass(world::Reader& mem) {
    const uint32_t vt = mem.u32(GRIM_CONTEXT);
    if (vt != GRIM_CONTEXT_VTABLE) throw Error("Unexpected investment context class " + hex(vt) + " (expected " + hex(GRIM_CONTEXT_VTABLE) + ")");
}

std::string Profile::identityHex() const { return hexBytes(identity.data(), identity.size()); }

Profile readProfile(world::Reader& mem, const Catalog& cat) {
    Profile p;
    p.manager = mem.u32(GRIM_PROFILE_MGR);
    if (!heapPtr(p.manager, GRIM_PROFILE_META + 16)) throw Error("The profile manager is not ready: load a character first");
    const Bytes meta = mem.bytes(p.manager + GRIM_PROFILE_META, 16);
    p.offset = meta.u32(0);
    if (meta.u32(4) != 1 || meta.u32(8) != 0x7050 || meta.u32(12) != 0x80801803 || uint64_t(p.offset) + 0x7050 > GRIM_PROFILE_LIMIT)
        throw Error("The account profile schema differs from the mapped build");
    p.account = p.manager + GRIM_PROFILE_ACCOUNT + p.offset;
    const Bytes id = mem.bytes(p.account, 8);
    for (int i = 0; i < 8; ++i) p.identity[size_t(i)] = id.u8(uint32_t(i));
    if (p.identity == std::array<uint8_t, 8>{}) throw Error("No account identity: load a character first");
     
     
     
    try {
        const Bytes desc = mem.bytes(p.manager + GRIM_PROFILE_DESCRIPTORS, 257 * 64);
        std::vector<uint32_t> matches;
        for (uint32_t i = 0; i < 257; ++i) {
            const uint32_t o = i * 64;
            if (desc.u8(o) == 4 && desc.u32(o + 36) == id.u32(4)) matches.push_back(o);
        }
        if (matches.size() == 1) {
            if (desc.u8(matches[0] + 40) != 3) throw Error("The profile is updating: try again in a moment");
            p.descriptorOk = true;
            p.descriptor = p.manager + GRIM_PROFILE_DESCRIPTORS + matches[0];
            p.version = desc.u32(matches[0] + 60);
        } else {
            p.note = fmt("profile descriptor table: %zu matches (expected 1); using the account identity check only", matches.size());
        }
    } catch (const Error&) {
        throw;
    } catch (const std::exception& e) {
        p.note = std::string("profile descriptor table unreadable (") + e.what() + "); using the account identity check only";
    }
    p.values = readValues(mem, p, cat);
    for (size_t i = 0; i < p.values.size(); ++i) p.unlocked += p.isUnlocked(i) ? 1 : 0;
    if (mem.bytes(p.account, 8).data() != std::vector<uint8_t>(p.identity.begin(), p.identity.end()))
        throw Error("The account identity changed during the readback: try again");
    return p;
}

void recheck(world::Reader& mem, const Profile& before) {
    if (mem.u32(GRIM_PROFILE_MGR) != before.manager) throw Error("The profile manager changed; batch stopped");
    if (mem.bytes(before.account, 8).data() != std::vector<uint8_t>(before.identity.begin(), before.identity.end()))
        throw Error("The account changed; batch stopped");
    if (before.descriptorOk) {
        const Bytes d = mem.bytes(before.descriptor + 40, 24);
        if (d.u8(0) != 3) throw Error("The profile is updating; batch stopped (try again)");
        if (d.u32(20) != before.version) throw Error("The profile was reloaded; batch stopped");
    }
}

std::vector<uint8_t> readValues(world::Reader& mem, const Profile& p, const Catalog& cat) {
    const Bytes b = mem.bytes(p.account + GRIM_ACCOUNT_FLAGS, GRIM_FLAGS_SPAN);
    std::vector<uint8_t> out;
    out.reserve(cat.flags.size());
    for (const Flag& f : cat.flags) out.push_back(b.u8(f.dense));
    return out;
}

std::vector<uint8_t> stubProgram(uint32_t base) {
    ppc::Asm a(base);
    const std::vector<int> save = {26, 27, 28, 29, 30, 31};
    constexpr int64_t frame = 0xB0;    
    ppc::prologue(a, frame, save);
    a.mr(31, 3).mr(30, 4).mr(29, 5).mr(26, 6);         
    a.li(0, 0).stw(0, 4, 29).stw(0, 8, 29);
    a.li(3, 4).bl(GRIM_CONTEXT_GET);                   
    a.cmpwi(3, 0).bne("have");
    a.li(3, kOutNoContext).stw(3, 0, 29).b("out");
    a.label("have").mr(28, 3);
    a.lwz(0, 8, 28).stw(0, 8, 29).cmplw(0, 26).beq("start");    
    a.li(3, kOutAccount).stw(3, 0, 29).b("out");       
    a.label("start").li(27, 0);
    a.label("loop").cmplw(27, 30).bge("done");
    a.slwi(9, 27, 2).lwzx(4, 31, 9);                   
    a.mr(3, 28).li(5, 2).bl(GRIM_SETTER);              
    a.addi(27, 27, 1).stw(27, 4, 29).b("loop");
    a.label("done").lwz(0, 8, 28).stw(0, 8, 29);       
    a.li(3, kOutDone).stw(3, 0, 29);
    a.label("out");
    ppc::epilogue(a, frame, save);
    a.blr();
    auto code = a.link();
    if (code.size() > kStubSize) throw Error("grimoire stub larger than its allocation");
    return code;
}

}   
