#include "game/hopons_scripts.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <limits>
#include <map>
#include <sstream>

#include "core/error.h"
#include "core/runtime.h"
#include "game/hopons_addr.h"
#include "game/world_addr.h"
#include "game/world_objects.h"

namespace brps3::hopons {

using guard::Cmp;
using guard::Cond;
using guard::Width;

namespace {

uint32_t w32(const std::vector<uint8_t>& b, size_t o) { return be32At(b, o); }

std::vector<uint8_t> be(uint32_t v) { return {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)}; }

Cond eq32(uint32_t addr, uint32_t value, const char* what) { return Cond::abs(addr, Cmp::Eq, value, Width::U32, 0xFFFFFFFFu, what); }

struct BlobInfo {
    TagMapping map;
    Bytes header;
};

 
bool sourceBinding(Reader& mem, TagReader& tags, ScriptRef& ref, std::map<uint32_t, BlobInfo>* cache, std::string* why) {
    const EncounterDef& d = ref.def;
    auto no = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    BlobInfo blob;
    auto it = cache ? cache->find(d.blob) : decltype(cache->end()){};
    if (cache && it != cache->end()) {
        blob = it->second;
    } else {
        std::string w;
        const auto m = tags.map(d.blob, 0, &w);
        if (!m) return no("source blob " + hex8(d.blob) + ": " + w);
        blob.map = *m;
        const auto h = mem.tryBytes(m->body, 16);
        if (!h) return no("source blob header unreadable");
        blob.header = *h;
        if (cache) (*cache)[d.blob] = blob;
    }
    ref.guards.push_back({blob.map.desc, blob.map.descBytes.data()});
    ref.guards.push_back({blob.map.entry, blob.map.entryBytes.data()});
    ref.guards.push_back({blob.map.body, blob.header.data()});
    const uint32_t start = 8 + blob.header.u32(8), end = 12 + blob.header.u32(12);
    if (end <= start || end - start != d.size || start > 0x100000 || d.binding > 0x100000 || !storage(blob.map.body, d.binding + 64))
        return no("source blob range does not describe this controller");
    const auto root = mem.tryBytes(blob.map.body + start, 12);
    if (!root || root->u32(0) != d.blob || root->u32(4) != d.binding || root->u32(8) != d.bindingClass) return no("source root header differs");
    ref.guards.push_back({root->addr(), root->data()});
    const auto binding = mem.tryBytes(blob.map.body + d.binding, 64);
    if (!binding) return no("source binding unreadable");
    ref.guards.push_back({binding->addr(), binding->data()});
    if (binding->u32(0) != d.blob || binding->u32(4) != start || binding->u32(8) != d.cls || binding->u32(0x24) != d.slotHash ||
        binding->u32(0x28) != d.slotId || binding->u32(0x38) != d.outputSchema || binding->u32(0x3C) != d.inputSchema)
        return no("source binding fields differ from the catalogue");
    ref.blobBody = blob.map.body;
    ref.start = start;
    return true;
}

 
bool quickValidate(Reader& mem, const ScriptRef& ref) {
    if (ref.guards.size() < 5) return false;
    return guardsUnchanged(mem, {ref.guards[3], ref.guards[4]});
}

 
 
struct PoolCache {
    ContextPool pool;
    std::vector<uint8_t> header;
};
std::optional<PoolCache>& scanContextPool() {
    thread_local std::optional<PoolCache> p;
    return p;
}

std::vector<uint8_t> sub(const std::vector<uint8_t>& b, size_t off, size_t n) {
    return std::vector<uint8_t>(b.begin() + std::ptrdiff_t(off), b.begin() + std::ptrdiff_t(off + n));
}

}   

void instanceConds(guard::Request& q, const ScriptRef& r) {
    const auto& g = r.guards;
    if (g.size() >= 3 && g[0].addr == addr::TAG_TABLE_PTR && g[0].bytes.size() == 4 && g[2].addr == r.desc && g[2].bytes.size() == 0x40) {
        q.require(eq32(addr::TAG_TABLE_PTR, be32At(g[0].bytes, 0), "tag table manager unchanged"));
        q.require(eq32(r.desc + 0x10, be32At(g[2].bytes, 0x10), "instance page still registered"));
    }
    q.require(eq32(r.entry, r.entryWord, "instance entry word"));
    q.require(eq32(r.entry + 4, r.entryMeta, "instance identity"));
    q.require(eq32(r.entry + 8, r.entryBack, "instance back offset"));
    q.require(eq32(r.address, r.def.blob, "root blob"));
    q.require(eq32(r.address + 4, r.def.binding, "root binding"));
    q.require(eq32(r.address + 8, r.def.bindingClass, "root binding class"));
}

 

uint32_t expectedInputSize(const EncounterDef& d) {
    if (d.cls == cls::TIMER && d.inputSchema == cls::TIMER_INPUT) return 0x24;
    if (d.cls == cls::SEQUENCE && d.inputSchema == cls::SEQUENCE_INPUT) return 0x20;
    return 0;
}

bool joinsDuringScan(uint32_t c) { return c == cls::TIMER || c == cls::SPAWNER || c == cls::SQUAD; }

bool joinInput(Reader& mem, ScriptInstance& s, TagReader* tagsIn) {
    s.joined = s.inputStable = s.headerVerified = false;
    s.input.clear();
    s.recordBytes.clear();
    s.payload = s.record = s.context = s.upstream = 0;
    if (s.ref.guards.size() > kBaseGuards) s.ref.guards.resize(kBaseGuards);    
    const EncounterDef& d = s.ref.def;
    auto status = [&](std::string t) {
        s.inputStatus = std::move(t);
        return false;
    };
    if (d.size < 0x108 || s.bytes.size() < 0x108) return status("No upstream input (the root is smaller than 0x108 bytes)");
    s.upstream = w32(s.bytes, 0x104);
    if (!world::validHandle(s.upstream)) return status("No upstream input record");
    std::optional<TagReader> own;
    if (!tagsIn) own.emplace(mem);
    TagReader& tags = tagsIn ? *tagsIn : *own;
    std::string why;
    const auto rec = tags.map(s.upstream, 0, &why);
    if (!rec) return status("Upstream record " + hex8(s.upstream) + ": " + why);
    s.record = rec->body;
    const auto r = mem.tryBytes(s.record, 0x48);
    if (!r) return status("Upstream record unreadable");
    s.recordBytes = r->data();
    s.headerVerified = r->u32(0) == d.slotHash && r->u32(4) == d.slotId && r->u32(8) == d.outputSchema && r->u32(0xC) == d.inputSchema;
     
     
    uint32_t size = r->u32(0x24);
    const uint32_t offset = r->u32(0x28), index = r->u16(0x2C);
    const uint32_t knownSize = expectedInputSize(d);
    bool sizeFromClass = false;
    if (!size || size > 4096) {
        if (!knownSize) return status("Record size (+0x24) " + hexShort(size) + " is not plausible");
        sizeFromClass = true;
        size = knownSize;
    }
    auto& cache = scanContextPool();
    if (!cache) {
        const auto pool = readContextPool(mem, &why);
        if (!pool) return status(why);
        cache = PoolCache{*pool, mem.bytes(addr::CONTEXT_POOL, 14).data()};
    }
    const ContextPool& pool = cache->pool;
    if (!index || index == 0xFFFF || index >= pool.capacity) return status("Record context " + hexShort(index) + " is outside the context pool");
    s.context = pool.base + index * pool.stride;
    const auto c = mem.tryBytes(s.context, 0x44);
    if (!c) return status("Context record unreadable");
    const uint32_t base = c->u32(0x30), extent = c->u32(0x40);
    if (!extent || offset > extent || size > extent - offset || !storage(base, extent)) return status("Input payload is outside its context allocation");
    s.payload = base + offset;
    const auto in = mem.tryBytes(s.payload, size);
    if (!in) return status("Input payload unreadable");
    s.input = in->data();
    if (!quickValidate(mem, s.ref)) {
        s.input.clear();
        s.payload = 0;
        return status("The instance changed during the join");
    }
    const auto again = mem.tryBytes(s.payload, size);
    s.inputStable = again && again->data() == s.input;
     
     
    s.ref.guards.push_back({s.ref.address + 0x104, sub(s.bytes, 0x104, 4)});
    s.ref.guards.push_back({s.record, sub(s.recordBytes, 0, 0x10)});
    s.ref.guards.push_back({s.record + 0x24, sub(s.recordBytes, 0x24, 10)});    
    s.ref.guards.push_back({addr::CONTEXT_POOL, cache->header});
    s.ref.guards.push_back({s.context + 0x30, c->slice(0x30, 4).data()});
    s.ref.guards.push_back({s.context + 0x40, c->slice(0x40, 4).data()});
    s.joined = true;
    s.inputStatus = s.inputStable ? "Typed upstream input joined; stable on repeat" : "Typed upstream input joined; changed on repeat";
    if (!s.headerVerified) s.inputStatus += " (record header unconfirmed on PS3)";
    if (sizeFromClass) s.inputStatus += "; size taken from the class (record +0x24 = " + hexShort(r->u32(0x24)) + ")";
    else if (knownSize && size != knownSize) s.inputStatus += "; " + std::to_string(size) + " bytes, expected " + std::to_string(knownSize);
    return true;
}

ScriptScan scanScripts(Reader& mem, const EncounterCatalog& cat, const std::function<bool()>& cancelled, const ScanProgress& progress) {
    ScriptScan out;
    const uint64_t r0 = mem.reads();
    scanContextPool().reset();
    TagReader tags(mem);
    tags.loadAllPages();
    const uint32_t mgr = tags.table().manager;
    const std::vector<uint8_t> ptrBytes = mem.bytes(addr::TAG_TABLE_PTR, 4).data();
    const std::vector<uint8_t> mgrBytes = mem.bytes(mgr, 0x14).data();
    std::vector<world::TagPage> pages;
    for (uint32_t p = 0; p < tags.table().pageCount; ++p) {
        const world::TagPage page = tags.page(p);
        if (!page.items || !page.reg || page.itemStride != 12 || page.itemsEnd <= page.items) continue;
        const uint32_t len = page.itemsEnd - page.items;
        if (len % 12 || len / 12 > 8192 || !storage(page.items, len)) continue;
        pages.push_back(page);
    }
    std::map<uint32_t, BlobInfo> blobs;
    struct Cand {
        uint32_t entry, word, meta, back, addr, size;
        std::vector<uint8_t> entryBytes;
    };
    for (size_t i = 0; i < pages.size(); ++i) {
        if (cancelled && cancelled()) {
            out.cancelled = true;
            break;
        }
        const world::TagPage& page = pages[i];
        const auto arr = mem.tryBytes(page.items, page.itemsEnd - page.items);
        if (progress) progress(uint32_t(i), uint32_t(pages.size()), out.instances.size());
        if (!arr) continue;
        ++out.pages;
        std::vector<Cand> cs;
        for (uint32_t off = 0; off + 12 <= arr->size(); off += 12) {
            const uint32_t word = arr->u32(off), size = word & 0x3FFFFFFFu;
            if ((word & 0xC0000000u) != 0xC0000000u || size < 12 || size > 4096) continue;
            ++out.candidates;
            Cand c{page.items + off, word, arr->u32(off + 4), arr->u32(off + 8), 0, size, arr->slice(off, 12).data()};
            c.addr = c.entry - (c.back & page.mask);
            if (!storage(c.addr, size)) continue;
            cs.push_back(std::move(c));
        }
        std::sort(cs.begin(), cs.end(), [](const Cand& a, const Cand& b) { return a.addr < b.addr; });
         
        size_t k = 0;
        while (k < cs.size()) {
            if (cancelled && cancelled()) break;
            const uint32_t start = cs[k].addr;
            uint32_t end = start + cs[k].size;
            size_t j = k + 1;
            while (j < cs.size() && cs[j].addr <= end + 0x1000 && uint64_t(cs[j].addr) + cs[j].size - start <= 0x10000) {
                end = std::max(end, cs[j].addr + cs[j].size);
                ++j;
            }
            const auto chunk = mem.tryBytes(start, end - start);
            for (size_t c = k; c < j; ++c) {
                const Cand& cand = cs[c];
                std::optional<Bytes> root;
                if (chunk) root = chunk->slice(cand.addr - start, cand.size);
                else root = mem.tryBytes(cand.addr, cand.size);
                if (!root) continue;
                const EncounterDef* def = cat.match(root->u32(0), root->u32(4), root->u32(8));
                if (!def || def->size != cand.size) {
                    ++out.unmatched;
                    continue;
                }
                ScriptInstance s;
                ScriptRef& ref = s.ref;
                ref.def = *def;
                ref.address = cand.addr;
                ref.page = page.page;
                ref.index = (cand.entry - page.items) / 12;
                ref.desc = tags.table().base + page.page * tags.table().pageStride;
                ref.registration = page.reg;
                ref.entry = cand.entry;
                ref.entryWord = cand.word;
                ref.entryMeta = cand.meta;
                ref.entryBack = cand.back;
                ref.guards = {{addr::TAG_TABLE_PTR, ptrBytes},
                              {mgr, mgrBytes},
                              {ref.desc, tags.pageBytes(page.page).data()},
                              {cand.entry, cand.entryBytes},
                              {cand.addr, root->slice(0, 12).data()}};
                if (!sourceBinding(mem, tags, ref, &blobs, nullptr)) {
                    ++out.sourceMismatch;
                    continue;
                }
                s.bytes = root->data();
                if (joinsDuringScan(def->cls)) joinInput(mem, s, &tags);
                else s.inputStatus = "Input not joined yet: refresh this instance to read it";
                out.instances.push_back(std::move(s));
            }
            k = j;
        }
    }
    if (progress) progress(uint32_t(pages.size()), uint32_t(pages.size()), out.instances.size());
    out.reads = mem.reads() - r0;
    return out;
}

bool validateScript(Reader& mem, const ScriptRef& ref, std::string* why) {
    auto no = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    if (ref.def.size < 12 || ref.def.size > 4096 || !storage(ref.address, ref.def.size) || ref.guards.size() < 9 || ref.guards.size() > 48)
        return no("not a verified script instance (rediscover live instances)");
    const auto& g = ref.guards;
    if (g[0].addr != addr::TAG_TABLE_PTR || g[3].addr != ref.entry || g[4].addr != ref.address || g[3].bytes.size() != 12 || g[4].bytes.size() != 12 ||
        w32(g[3].bytes, 0) != (0xC0000000u | ref.def.size) || w32(g[4].bytes, 0) != ref.def.blob || w32(g[4].bytes, 4) != ref.def.binding ||
        w32(g[4].bytes, 8) != ref.def.bindingClass)
        return no("the identity snapshot does not describe this instance");
    std::string w;
    if (!guardsUnchanged(mem, g, &w)) return no("registration, instance or source binding changed (" + w + "): rediscover live instances");
    return true;
}

bool refreshScript(Reader& mem, const ScriptRef& ref, ScriptInstance& out, std::string* why) {
     
     
    ScriptRef base = ref;
    if (base.guards.size() > kBaseGuards) base.guards.resize(kBaseGuards);
    if (!validateScript(mem, base, why)) return false;
    ScriptInstance fresh;
    fresh.ref = base;
    const auto b = mem.tryBytes(ref.address, ref.def.size);
    if (!b) {
        if (why) *why = "cannot read the instance";
        return false;
    }
    fresh.bytes = b->data();
    if (!validateScript(mem, base, why)) return false;
    scanContextPool().reset();
    joinInput(mem, fresh);
    out = std::move(fresh);
    return true;
}

 

std::string timerFieldsUnavailable(const ScriptInstance& s) {
    const EncounterDef& d = s.ref.def;
    if (d.cls != cls::TIMER || d.inputSchema != cls::TIMER_INPUT) return "not a timer controller";
    if (d.size < 0x12C || s.bytes.size() < 0x12C) return "the timer root is smaller than 0x12C bytes";
    if (!s.joined) return "no verified upstream input join (" + s.inputStatus + ")";
    if (s.input.size() != 36) return "the joined input is " + std::to_string(s.input.size()) + " bytes, not 36 (record size field needs a check)";
    if (!s.inputStable) return "the upstream input changed on repeat: refresh the instance";
    return {};
}

std::vector<ScriptField> timerFields(const ScriptInstance& s) {
    std::vector<ScriptField> out;
    if (!timerFieldsUnavailable(s).empty()) return out;
    ScriptField b;
    b.offset = 0x108;
    b.name = "Elapsed-time subtraction";
    b.unit = "on/off";
    b.help = "On: the timer subtracts elapsed time from its remaining value (it runs). Off: it holds.";
    b.value = double(s.bytes[0x108]);
    b.kind = ScriptField::Boolean;
    out.push_back(b);
    static const char* names[] = {"Lower clamp", "Upper clamp", "Base progress", "Initial remaining", "Reference clock"};
    static const char* helps[] = {"Smallest value the result may take.", "Largest value the result may take.",
                                  "Progress the timer starts from.", "Remaining time when the timer started.",
                                  "Clock value the elapsed time is measured from."};
    for (uint32_t i = 0; i < 5; ++i) {
        ScriptField f;
        f.offset = 0x10C + i * 4;
        f.name = names[i];
        f.unit = "clock units";
        f.help = helps[i];
        f.value = double(int32_t(w32(s.bytes, f.offset)));
        f.kind = ScriptField::Integer;
        out.push_back(f);
    }
    ScriptField m;
    m.offset = 0x120;
    m.name = "Clock-delta multiplier";
    m.unit = "x";
    m.help = "Scales the whole elapsed delta: 0 freezes the timer, 2 runs it twice as fast (the result can jump).";
    m.value = double(world::bitsFloat(w32(s.bytes, 0x120)));
    m.kind = ScriptField::Float;
    out.push_back(m);
    return out;
}

std::optional<ScriptEdit> prepareTimerEdit(Reader& mem, const ScriptInstance& s, const ScriptField& field, double value, std::string& why) {
    const auto fields = timerFields(s);
    const auto found = std::find_if(fields.begin(), fields.end(), [&](const ScriptField& f) { return f.offset == field.offset && f.kind == field.kind; });
    if (found == fields.end()) {
        why = fields.empty() ? timerFieldsUnavailable(s) : "unsupported field";
        return std::nullopt;
    }
    if (!std::isfinite(value) || (field.kind == ScriptField::Boolean && value != 0 && value != 1) ||
        (field.kind == ScriptField::Integer && (value < -2147483648.0 || value > 2147483647.0 || std::floor(value) != value)) ||
        (field.kind == ScriptField::Float && (value < 0 || value > 10000))) {
        why = field.kind == ScriptField::Float ? "the multiplier must be between 0 and 10000"
              : field.kind == ScriptField::Boolean ? "the value must be 0 or 1"
                                                    : "the value must be a whole 32-bit number";
        return std::nullopt;
    }
    if (!validateScript(mem, s.ref, &why)) return std::nullopt;
    const auto current = mem.tryBytes(s.ref.address + 0x108, 36);
    if (!current || !std::equal(current->data().begin(), current->data().end(), s.bytes.begin() + 0x108)) {
        why = "the timer changed since it was read: refresh before applying";
        return std::nullopt;
    }
    int32_t lower = int32_t(current->u32(4)), upper = int32_t(current->u32(8));
    if (field.offset == 0x10C) lower = int32_t(value);
    if (field.offset == 0x110) upper = int32_t(value);
    if (lower > upper) {
        why = "the lower clamp must not exceed the upper clamp";
        return std::nullopt;
    }
    ScriptEdit e;
    e.ref = s.ref;
    e.offset = field.offset;
    e.label = field.name;
    e.at = int64_t(std::time(nullptr));
    e.before = current->data();
    const uint32_t n = field.kind == ScriptField::Boolean ? 1 : 4;
    e.original.assign(s.bytes.begin() + field.offset, s.bytes.begin() + field.offset + n);
    if (n == 1) {
        e.applied = {uint8_t(value)};
    } else {
        const uint32_t raw = field.kind == ScriptField::Float ? world::floatBits(float(value)) : uint32_t(int32_t(value));
        e.applied = be(raw);
    }
    if (e.original == e.applied) {
        why = "the field already holds that value";
        return std::nullopt;
    }
    return e;
}

void writeTimerEdit(Runtime& rt, Reader& mem, const ScriptEdit& e) {
    std::string why;
    if (!validateScript(mem, e.ref, &why)) throw Error("timer edit refused: " + why);
    const auto latest = mem.tryBytes(e.ref.address + 0x108, 36);
    if (!latest || latest->data() != e.before) throw Error("timer edit refused: the timer changed while saving the recovery journal");
    rt.write(e.ref.address + e.offset, e.applied.data(), e.applied.size(), true);
}

bool restoreTimerEdit(Runtime& rt, Reader& mem, const ScriptEdit& e, std::string& why) {
    const bool shape = e.ref.def.cls == cls::TIMER && e.ref.def.size >= 0x12C && e.original.size() == e.applied.size() &&
                       ((e.offset == 0x108 && e.original.size() == 1) || (e.offset >= 0x10C && e.offset <= 0x120 && e.offset % 4 == 0 && e.original.size() == 4));
    if (!shape) {
        why = "not a timer field edit";
        return false;
    }
    if (!validateScript(mem, e.ref, &why)) return false;
    const auto current = mem.tryBytes(e.ref.address + e.offset, uint32_t(e.applied.size()));
    if (!current) {
        why = "cannot read the field";
        return false;
    }
    for (size_t i = 0; i < current->size(); ++i)
        if (current->data()[i] != e.original[i] && current->data()[i] != e.applied[i]) {
            why = "the field changed independently: restore refused";
            return false;
        }
    if (current->data() == e.original) return true;    
    if (e.offset == 0x10C || e.offset == 0x110) {
        auto clamps = mem.bytes(e.ref.address + 0x10C, 8).data();
        std::copy(e.original.begin(), e.original.end(), clamps.begin() + (e.offset - 0x10C));
        if (int32_t(w32(clamps, 0)) > int32_t(w32(clamps, 4))) {
            why = "restoring this clamp would invert the range: restore the newer edits first";
            return false;
        }
    }
    rt.write(e.ref.address + e.offset, e.original.data(), e.original.size(), true);
    return true;
}

namespace {
std::vector<uint8_t> parseHexField(const std::string& s) {
    if (s.size() % 2 || s.size() > 256) throw Error("bad hex field");
    std::vector<uint8_t> out;
    for (size_t i = 0; i < s.size(); i += 2) out.push_back(uint8_t(std::stoul(s.substr(i, 2), nullptr, 16)));
    return out;
}
uint32_t h32(const std::string& s) { return uint32_t(std::stoul(s, nullptr, 16)); }
std::vector<std::string> tabs(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == '\t') {
            out.push_back(cur);
            cur.clear();
        } else if (c != '\r') {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}
}   

std::string serialiseEdits(const std::vector<ScriptEdit>& edits) {
    std::ostringstream s;
    s << "BR_PS3_SCRIPT_RECOVERY\t1\n";
    for (const ScriptEdit& e : edits) {
        const EncounterDef& d = e.ref.def;
        const ScriptRef& r = e.ref;
        std::string label = e.label, name = d.name;
        std::replace(label.begin(), label.end(), '\t', ' ');
        std::replace(name.begin(), name.end(), '\t', ' ');
        s << "E";
        for (uint32_t v : {d.root, d.blob, d.cls, d.binding, d.bindingClass, d.size, d.slotHash, d.slotId, d.outputSchema, d.inputSchema, d.effect}) s << '\t' << hex8(v);
        s << '\t' << name;
        for (uint32_t v : {r.address, r.page, r.index, r.desc, r.registration, r.entry, r.entryWord, r.entryMeta, r.entryBack, r.blobBody, r.start, e.offset})
            s << '\t' << hex8(v);
        s << '\t' << hexBytes(e.original) << '\t' << hexBytes(e.applied) << '\t' << label << '\t' << e.at << '\t' << r.guards.size() << '\n';
        for (const Guard& g : r.guards) s << "G\t" << hex8(g.addr) << '\t' << hexBytes(g.bytes) << '\n';
    }
    return s.str();
}

std::vector<ScriptEdit> parseEdits(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line.rfind("BR_PS3_SCRIPT_RECOVERY\t1", 0) != 0) throw Error("not a script recovery journal");
    std::vector<ScriptEdit> out;
    while (std::getline(in, line)) {
        if (line.empty() || line == "\r") continue;
        const auto f = tabs(line);
        if (f[0] != "E" || f.size() != 30) throw Error("malformed recovery entry");
        ScriptEdit e;
        EncounterDef& d = e.ref.def;
        uint32_t* defs[] = {&d.root, &d.blob, &d.cls, &d.binding, &d.bindingClass, &d.size, &d.slotHash, &d.slotId, &d.outputSchema, &d.inputSchema, &d.effect};
        for (int i = 0; i < 11; ++i) *defs[i] = h32(f[size_t(1 + i)]);
        d.name = f[12];
        ScriptRef& r = e.ref;
        uint32_t* refs[] = {&r.address, &r.page, &r.index, &r.desc, &r.registration, &r.entry, &r.entryWord, &r.entryMeta, &r.entryBack, &r.blobBody, &r.start, &e.offset};
        for (int i = 0; i < 12; ++i) *refs[i] = h32(f[size_t(13 + i)]);
        e.original = parseHexField(f[25]);
        e.applied = parseHexField(f[26]);
        e.label = f[27];
        e.at = std::stoll(f[28]);
        const size_t guards = size_t(std::stoul(f[29]));
        if (guards > 48) throw Error("malformed recovery guards");
        for (size_t g = 0; g < guards; ++g) {
            if (!std::getline(in, line)) throw Error("truncated recovery journal");
            const auto gf = tabs(line);
            if (gf.size() != 3 || gf[0] != "G") throw Error("malformed recovery guard");
            r.guards.push_back({h32(gf[1]), parseHexField(gf[2])});
        }
        if (e.original.size() != e.applied.size() || e.original.empty()) throw Error("malformed recovery bytes");
        out.push_back(std::move(e));
    }
    return out;
}

 

const char* sequenceStateText(uint8_t state) {
    switch (state) {
    case 0: return "cleared";
    case 1: return "deferred";
    case 2: return "created";
    default: return "unknown";
    }
}

std::optional<SequenceState> decodeSequence(const ScriptInstance& s) {
    if (s.ref.def.cls != cls::SEQUENCE || s.ref.def.size < 0x13C || s.bytes.size() < 0x13C) return std::nullopt;
    SequenceState q;
    for (uint32_t i = 0; i < 8; ++i) q.input[i] = w32(s.bytes, 0x108 + i * 4);
    q.selector = s.bytes[0x10C];
    q.pairIdentity = w32(s.bytes, 0x128);
    q.pairAccessor = w32(s.bytes, 0x12C);
    q.state = s.bytes[0x130];
    q.completed = s.bytes[0x131];
    return q;
}

std::optional<CleanupPlan> prepareCleanup(Reader& mem, const ScriptInstance& s, std::string& why) {
    const EncounterDef& d = s.ref.def;
    if (d.cls != cls::SEQUENCE || d.inputSchema != cls::SEQUENCE_INPUT || d.size < 0x13C) {
        why = "not a supported constructed sequence instance";
        return std::nullopt;
    }
    if (!validateScript(mem, s.ref, &why)) return std::nullopt;
    const auto first = mem.tryBytes(s.ref.address + 0x108, 0x2C);
    if (!validateScript(mem, s.ref, &why)) return std::nullopt;
    const auto again = mem.tryBytes(s.ref.address + 0x108, 0x2C);
    if (!first || !again || first->data() != again->data()) {
        why = "the sequence input / state is changing: try again";
        return std::nullopt;
    }
    CleanupPlan p;
    p.ref = s.ref;
    for (uint32_t i = 0; i < 8; ++i) p.state.input[i] = first->u32(i * 4);
    p.state.selector = first->u8(4);
    p.state.pairIdentity = first->u32(0x20);
    p.state.pairAccessor = first->u32(0x24);
    p.state.state = first->u8(0x28);
    p.state.completed = first->u8(0x29);
    if (p.state.state > 2) {
        why = "sequence state " + std::to_string(p.state.state) + " is not 0..2";
        return std::nullopt;
    }
    if (p.state.hasPair()) {
        const auto acc = readAccessor(mem, p.state.pairAccessor, &why);
        if (!acc) {
            why = "the tracked effect does not resolve: " + why;
            return std::nullopt;
        }
        if (acc->canonical != p.state.pairAccessor || acc->identity != p.state.pairIdentity || !world::readSObject(mem, acc->target)) {
            why = "the tracked effect pair does not describe a live effect";
            return std::nullopt;
        }
        p.tracked = acc;
    } else if (p.state.state == 2) {
        why = "state 2 (created) without a tracked effect";
        return std::nullopt;
    }
    return p;
}

guard::Request cleanupRequest(const CleanupPlan& p) {
    const ScriptRef& r = p.ref;
    guard::Request q(addr::SEQUENCE_CLEANUP);
    q.arg(r.address);
    if (p.tracked) q.sobject(p.tracked->target);
    instanceConds(q, r);
    q.require(eq32(r.address + 0x128, p.state.pairIdentity, "tracked identity"));
    q.require(eq32(r.address + 0x12C, p.state.pairAccessor, "tracked accessor"));
    q.require(Cond::abs(r.address + 0x130, Cmp::Eq, p.state.state, Width::U8, 0xFF, "sequence state"));
    if (p.tracked) {
        q.require(eq32(p.tracked->entry, p.tracked->word, "effect accessor allocation"));
        q.require(eq32(p.tracked->entry + 0xC, p.tracked->canonical, "effect owner"));
        q.require(eq32(p.tracked->body + 0x14, p.tracked->accessor, "effect holds its accessor"));
    }
    return q;
}

CleanupOutcome cleanupSequence(Runtime& rt, Reader& mem, const ScriptInstance& s, const std::function<void(const std::string&)>& log) {
    CleanupOutcome out;
    auto say = [&](const std::string& t) {
        if (log) log(t);
    };
    std::string why;
    const auto plan = prepareCleanup(mem, s, why);
    if (!plan) {
        out.message = "Nothing changed: " + why;
        say(out.message);
        return out;
    }
    say("sequence " + hex8(s.ref.address) + ": state " + std::to_string(plan->state.state) + ", pair " + hex8(plan->state.pairIdentity) + "/" +
        hex8(plan->state.pairAccessor));
    const guard::Result res = guard::call(rt, cleanupRequest(*plan), 3000, false);
    if (!res.called) {
        out.message = "Refused on the game thread: " + res.reason;
        say(out.message);
        return out;
    }
    out.called = true;
    const auto after = mem.tryBytes(s.ref.address + 0x128, 12);
    ScriptRef identity = s.ref;    
    if (identity.guards.size() > kBaseGuards) identity.guards.resize(kBaseGuards);
    const bool ok = after && after->u32(0) == 0xFFFFFFFFu && after->u32(4) == 0xFFFFFFFFu && after->u8(8) == 0 && validateScript(mem, identity, &why);
    out.cleared = ok;
    out.message = ok ? "Effect deleted: the sequence is cleared (upstream input may create it again later)"
                     : "Cleanup ran but the cleared state was not verified: inspect before retrying";
    say(out.message);
    return out;
}

 

std::optional<SpawnerState> decodeSpawner(uint32_t c, const std::vector<uint8_t>& b) {
    if (c != cls::SPAWNER || b.size() < 0x178) return std::nullopt;
    SpawnerState s;
    s.actor = w32(b, 0x174);
    s.gate = b[0x14C];
    s.initialized = b[0x14D];
    s.inputKey = w32(b, 0x108);
    if (b.size() >= 0xA00) s.linkedEffect = w32(b, 0x9FC);
    return s;
}

std::optional<SpawnerDecision> inspectRefresh(uint32_t c, const std::vector<uint8_t>& state, const std::vector<uint8_t>& input, bool stable) {
    const auto s = decodeSpawner(c, state);
    if (!s || !stable || input.size() < 7) return std::nullopt;
    SpawnerDecision r;
    r.mode = input[5];
    r.enabled = input[6];
    r.hasActor = s->actor != 0xFFFFFFFFu;
    r.spawnGate = s->gate != 0;
    if (w32(input, 0) != s->inputKey) r.action = RefreshAction::ReconcileInputState;
    else if (r.hasActor && (!r.enabled || r.spawnGate)) r.action = RefreshAction::RemoveTrackedActor;
    else if (r.hasActor) r.action = RefreshAction::KeepTrackedActor;
    else if (!r.enabled || r.spawnGate || r.mode == 1 || r.mode == 3 || r.mode == 4) r.action = RefreshAction::SpawnBlocked;
    else r.action = RefreshAction::SpawnEligible;
    return r;
}

const char* refreshActionText(RefreshAction a) {
    switch (a) {
    case RefreshAction::ReconcileInputState: return "Input key changed: the native refresh resets cached state and can remove the tracked actor.";
    case RefreshAction::RemoveTrackedActor: return "Spawn-phase gates ask for the tracked actor's removal.";
    case RefreshAction::KeepTrackedActor: return "Spawn-phase gates keep the tracked actor.";
    case RefreshAction::SpawnEligible: return "Basic spawn gates allow an attempt (dependencies and capacity still apply).";
    case RefreshAction::SpawnBlocked: return "Basic spawn gates currently block creation.";
    }
    return "";
}

std::optional<SquadRoster> decodeRoster(uint32_t c, const std::vector<uint8_t>& b) {
    if (c != cls::SQUAD || b.size() < 0x31C) return std::nullopt;
    SquadRoster r;
    r.count = w32(b, 0x1D8);
    if (r.count > 40) return std::nullopt;
    for (uint32_t i = 0; i < r.count; ++i) r.entries.push_back({w32(b, 0x1DC + i * 8), w32(b, 0x1E0 + i * 8)});
    return r;
}

std::optional<SquadSpawnInput> decodeSquadSpawnInput(uint32_t c, const std::vector<uint8_t>& input, bool stable) {
    if (c != cls::SQUAD || input.size() < 0x44 || !stable) return std::nullopt;
    SquadSpawnInput r;
    r.variant = input[0x40];
    for (int i = 0; i < 3; ++i) r.contextFlags[size_t(i)] = input[size_t(0x41 + i)];
    return r;
}

std::optional<ActorObject> readActorObject(Reader& mem, uint32_t actor, std::string* why) {
    auto no = [&](std::string w) -> std::optional<ActorObject> {
        if (why) *why = std::move(w);
        return std::nullopt;
    };
    if (!world::validHandle(actor)) return no("No AI actor is tracked.");
    std::string w;
    const auto slot = poolSlot(mem, addr::ACTOR_POOL, actor & 0x1FFF, &w);
    if (!slot) return no("Actor slot: " + w);
    if (slot->pool.stride != 0x28E0) return no("Actor pool stride " + hexShort(slot->pool.stride) + " is not 0x28E0");
    if (slot->handle != actor) return no("Actor handle was recycled (now " + hex8(slot->handle) + ").");
    ActorObject a;
    a.actor = actor;
    a.slot = *slot;
    a.generation = slot->identity;
    a.actorAddress = slot->pool.base + (actor & 0x1FFF) * slot->pool.stride;
    const Bytes rec = mem.bytes(a.actorAddress, 0x48);
    a.squadTag = rec.u32(0x24);
    a.squadOffset = rec.u32(0x28);
    a.object = rec.u32(0x30);
    a.ownerTag = rec.u32(0x40);
    a.ownerOffset = rec.u32(0x44);
    const auto obj = world::readSObject(mem, a.object);
    if (!obj) return no("Actor's object " + hex8(a.object) + " was recycled or removed.");
    a.objectAddress = obj->addr;
    a.objectType = obj->type;
    const auto again = poolSlot(mem, addr::ACTOR_POOL, actor & 0x1FFF, &w);
    if (!again || again->identity != slot->identity || mem.u32(a.actorAddress + 0x30) != a.object) return no("Actor identity changed during the lookup.");
    return a;
}

std::optional<SpawnerBinding> readSpawnerBinding(Reader& mem, const ScriptRef& ref, std::string* why) {
    if (ref.def.cls != cls::SPAWNER) {
        if (why) *why = "select an AI spawner instance";
        return std::nullopt;
    }
    if (!validateScript(mem, ref, why)) return std::nullopt;
    if (ref.def.binding > 0x100000 || !storage(ref.blobBody, ref.def.binding + 0x54)) {
        if (why) *why = "the spawner's authored squad reference is unavailable";
        return std::nullopt;
    }
    SpawnerBinding b;
    b.address = ref.blobBody + ref.def.binding + 0x44;
    const Bytes f = mem.bytes(b.address, 16);
    for (int i = 0; i < 8; ++i) b.squadReference[size_t(i)] = f.u8(uint32_t(i));
    b.member = f.u32(12);
    if (!validateScript(mem, ref, why)) return std::nullopt;
    return b;
}

std::optional<SquadBinding> readSquadBinding(Reader& mem, const ScriptRef& ref, std::string* why) {
    auto no = [&](std::string w) -> std::optional<SquadBinding> {
        if (why) *why = std::move(w);
        return std::nullopt;
    };
    if (ref.def.cls != cls::SQUAD) return no("select an AI squad controller");
    if (!validateScript(mem, ref, why)) return std::nullopt;
    if (ref.def.binding > 0x100000 || !storage(ref.blobBody, ref.def.binding + 0x4C)) return no("the squad definition reference is unavailable");
    const Bytes pair = mem.bytes(ref.blobBody + ref.def.binding + 0x44, 8);
    SquadBinding r;
    r.tag = pair.u32(0);
    r.offset = pair.u32(4);
    TagReader tags(mem);
    std::string w;
    const auto m = r.offset % 4 || r.offset > 0x100000 ? std::nullopt : tags.map(r.tag, 0, &w);
    if (!m) return no("the referenced squad definition " + hex8(r.tag) + " is unavailable: " + w);
    r.address = m->body + r.offset;
    const Bytes header = mem.bytes(r.address, 0x34);
    r.members = header.u32(0x1C);
    const uint32_t size = header.u32(0), relative = header.u32(0x20);
    const int64_t arrayOffset = 0x20ll + int32_t(relative);
    if (size < 0x34 || size > 0x100000 || size % 4 || !storage(r.address, size) || r.members > 8192 || (!r.members && relative) ||
        (r.members && (arrayOffset < 4 || arrayOffset % 4 || arrayOffset + 16 + int64_t(r.members) * 28 > int64_t(size))))
        return no("the squad member array exceeds the authored definition");
    if (r.members) {
        const Bytes arr = mem.bytes(r.address + uint32_t(arrayOffset) - 4, 20);
        if (arr.u32(0) != cls::ARRAY || arr.u32(4) != r.members || arr.u32(8) != cls::SQUAD_MEMBER || arr.u32(12) || arr.u32(16))
            return no("the squad member array has an invalid typed header");
    }
    if (!validateScript(mem, ref, why)) return std::nullopt;
    return r;
}

uint32_t instanceHandle(Reader& mem, const ScriptRef& ref, std::string* why) {
    const auto slot = poolSlot(mem, ref.registration, ref.index, why);
    if (!slot) return 0;
    if (slot->identity != ref.entryMeta) {
        if (why) *why = "the pool identity " + hex8(slot->identity) + " differs from the entry +4 " + hex8(ref.entryMeta);
        return 0;
    }
    return slot->handle;
}

 

namespace {
std::optional<SquadRosterTarget> squadRosterPass(Reader& mem, uint32_t tag, uint32_t offset, uint32_t actor, uint32_t generation, std::string& why) {
    auto no = [&](std::string w) -> std::optional<SquadRosterTarget> {
        why = std::move(w);
        return std::nullopt;
    };
    if (offset) return no("the actor's squad controller is embedded (+0x28 = " + hexShort(offset) + "): only standalone controllers are supported");
    SquadRosterTarget r;
    r.tag = tag;
    TagReader tags(mem);
    std::string w;
    const auto m = tags.map(tag, 12, &w);
    if (!m) return no("squad controller " + hex8(tag) + ": " + w);
    const auto slot = poolSlot(mem, m->registration, m->index, &w);
    if (!slot || slot->handle != tag || slot->identity != m->metadata)
        return no("squad controller " + hex8(tag) + " is stale (" + (slot ? "now " + hex8(slot->handle) : w) + ")");
    r.entry = m->entry;
    r.entryWord = m->word;
    r.entryMeta = m->metadata;
    r.entryBack = m->backOffset;
    r.root = m->body;
    r.size = m->word & 0x3FFFFFFFu;
    if ((m->word & 0xC0000000u) != 0xC0000000u || r.size < kSquadRosterEnd || r.size > 4096 || !storage(r.root, r.size))
        return no("squad controller " + hex8(tag) + " allocation (" + hex8(m->word) + ") cannot hold a roster");
    const Bytes root = mem.bytes(r.root, kSquadRosterEnd);
    for (uint32_t i = 0; i < 3; ++i) r.header[i] = root.u32(i * 4);
     
    const auto blob = tags.map(r.header[0], 0, &w);
    if (!blob || blob->word != cls::BLOB)
        return no("the squad controller's source blob " + hex8(r.header[0]) + " is not resident" + (w.empty() ? std::string() : ": " + w));
    const Bytes bh = mem.bytes(blob->body, 16);
    const uint64_t start = 8ull + bh.u32(8), finish = 12ull + bh.u32(12);
    const uint32_t binding = r.header[1];
    if (start < 4 || start % 4 || finish <= start || finish - start != r.size || binding % 4 || binding < finish || binding > 0x100000 ||
        !storage(blob->body, binding + 0x10))
        return no("the squad controller's source range does not describe it");
    const Bytes source = mem.bytes(blob->body + uint32_t(start) - 4, 16), bind = mem.bytes(blob->body + binding - 4, 16);
    if (source.u32(0) != cls::SQUAD || source.u32(4) != r.header[0] || source.u32(8) != r.header[1] || source.u32(12) != r.header[2] ||
        bind.u32(0) != r.header[2] || bind.u32(4) != r.header[0] || bind.u32(8) != uint32_t(start) || bind.u32(12) != cls::SQUAD)
        return no("the actor's squad controller is not a squad (808007BC) by its source binding");
    r.count = root.u32(0x1D8);
    if (r.count > 40) return no("squad roster count " + std::to_string(r.count) + " is above 40");
    const world::PoolHeader actors = world::readPool(mem, addr::ACTOR_POOL);
    for (uint32_t i = 0; i < r.count; ++i) {
        const uint32_t gen = root.u32(0x1DC + i * 8), member = root.u32(0x1E0 + i * 8);
        r.words.push_back(gen);
        r.words.push_back(member);
        if (member == 0xFFFFFFFFu) continue;
         
         
        if (registrationOf(mem, member) != addr::ACTOR_POOL || !actors.capacity || (member & 0x1FFF) >= actors.capacity)
            return no("roster entry " + std::to_string(i) + " (" + hex8(member) + ") is not an actor pool handle");
        if (member == actor) {
            if (r.selected != 0xFFFFFFFFu || gen != generation) return no("the roster lists the actor twice or with another generation");
            r.selected = i;
        }
    }
    if (r.selected == 0xFFFFFFFFu) return no("the squad roster does not list the actor " + hex8(actor));
    const auto again = mem.tryBytes(r.entry, 12);
    const Bytes rootAgain = mem.bytes(r.root, kSquadRosterEnd);
    if (!again || again->data() != m->entryBytes.data() || rootAgain.data() != root.data()) return no("the squad controller changed during the capture");
    return r;
}

std::optional<SpawnerTarget> capturePass(Reader& mem, const ScriptInstance& s, std::string& why) {
    SpawnerTarget t;
    const ScriptRef& r = s.ref;
    if (r.def.cls != cls::SPAWNER || r.def.size != kSpawnerRootSize) {
        why = "select an AI spawner instance";
        return std::nullopt;
    }
    if (!validateScript(mem, r, &why)) return std::nullopt;
    t.root = r.address;
    const Bytes st = mem.bytes(r.address + 0x174, 4);
    t.actor = st.u32(0);
    t.linkedEffect = mem.u32(r.address + 0x9FC);
    if (!world::validHandle(t.actor)) {
        why = "the spawner tracks no actor";
        return std::nullopt;
    }
    const auto a = readActorObject(mem, t.actor, &why);
    if (!a) return std::nullopt;
    t.actorObj = *a;
    t.selfHandle = instanceHandle(mem, r, &why);
    if (!t.selfHandle) {
        why = "the instance handle is unknown (" + why + ")";
        return std::nullopt;
    }
    if (a->ownerTag != t.selfHandle || a->ownerOffset != 0) {
        why = "the actor's spawner owner (" + hex8(a->ownerTag) + " +" + hexShort(a->ownerOffset) + ") is not this spawner (" + hex8(t.selfHandle) + ")";
        return std::nullopt;
    }
    if (a->squadTag != 0xFFFFFFFFu) {
        t.squad = squadRosterPass(mem, a->squadTag, a->squadOffset, t.actor, a->generation, why);
        if (!t.squad) return std::nullopt;
    } else if (a->squadOffset) {
        why = "the actor names no squad controller but a squad offset " + hexShort(a->squadOffset);
        return std::nullopt;
    }
    t.bitmapAddr = addr::SOBJECT_ALLOC_BITMAP + ((a->object & 0x1FFF) >> 5) * 4;
    t.bitmapBit = 1u << (a->object & 31);
    if (!(mem.u32(t.bitmapAddr) & t.bitmapBit)) {
        why = "the actor's object is not allocated in the sObject bitmap";
        return std::nullopt;
    }
    if (mem.u32(r.address + 0x9FC) != t.linkedEffect || mem.u32(r.address + 0x174) != t.actor) {
        why = "the spawner changed during the capture";
        return std::nullopt;
    }
    t.lines = {"spawner " + hex8(t.root) + " (" + r.def.name + "), instance handle " + hex8(t.selfHandle),
               "tracked actor " + hex8(t.actor) + " at " + hex8(a->actorAddress) + ", generation " + hex8(a->generation),
               "object " + hex8(a->object) + " (" + world::typeName(a->objectType) + ") at " + hex8(a->objectAddress) + ", bitmap " + hex8(t.bitmapAddr),
               "squad owner " + hex8(a->squadTag) + " +" + hexShort(a->squadOffset) + ", linked effect " + hex8(t.linkedEffect)};
    if (t.squad)
        t.lines.push_back("squad controller " + hex8(t.squad->root) + ": roster " + std::to_string(t.squad->count) + ", the actor is entry " +
                          std::to_string(t.squad->selected));
    return t;
}
}   

std::optional<SquadRosterTarget> captureSquadRoster(Reader& mem, uint32_t tag, uint32_t offset, uint32_t actor, uint32_t generation, std::string& why) {
    const auto a = squadRosterPass(mem, tag, offset, actor, generation, why);
    if (!a) return std::nullopt;
    const auto b = squadRosterPass(mem, tag, offset, actor, generation, why);
    if (!b) return std::nullopt;
    if (!(*a == *b)) {
        why = "the squad controller changed between two captures";
        return std::nullopt;
    }
    return b;
}

bool squadRosterRemoved(Reader& mem, const SquadRosterTarget& before, std::string* why) {
    auto no = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    if (!before.count || before.selected >= before.count || before.words.size() != size_t(before.count) * 2) return no("no roster snapshot");
    TagReader tags(mem);
    std::string w;
    const auto m = tags.map(before.tag, 12, &w);
    if (!m || m->entry != before.entry || m->word != before.entryWord || m->metadata != before.entryMeta || m->body != before.root)
        return no("the squad controller allocation changed" + (w.empty() ? std::string() : ": " + w));
    const Bytes root = mem.bytes(before.root, kSquadRosterEnd);
    for (uint32_t i = 0; i < 3; ++i)
        if (root.u32(i * 4) != before.header[i]) return no("the squad controller header changed");
    const uint32_t last = before.count - 1;
    if (root.u32(0x1D8) != last) return no("roster count is " + std::to_string(root.u32(0x1D8)) + ", expected " + std::to_string(last));
    for (uint32_t i = 0; i < last; ++i) {
        const uint32_t old = i == before.selected ? last : i;
        if (root.u32(0x1DC + i * 8) != before.words[size_t(old) * 2] || root.u32(0x1E0 + i * 8) != before.words[size_t(old) * 2 + 1])
            return no("roster entry " + std::to_string(i) + " is not the expected compaction");
    }
    if (root.u32(0x1E0 + last * 8) != 0xFFFFFFFFu) return no("the vacated roster slot still names an actor");
    return true;
}

std::optional<SpawnerTarget> captureSpawnerTarget(Reader& mem, const ScriptInstance& s, std::string& why) {
    const auto a = capturePass(mem, s, why);
    if (!a) return std::nullopt;
    const auto b = capturePass(mem, s, why);
    if (!b) return std::nullopt;
    if (a->actor != b->actor || a->linkedEffect != b->linkedEffect || a->actorObj.generation != b->actorObj.generation ||
        a->actorObj.object != b->actorObj.object || a->selfHandle != b->selfHandle || a->actorObj.squadTag != b->actorObj.squadTag ||
        a->actorObj.slot.pool.base != b->actorObj.slot.pool.base || a->squad != b->squad) {
        why = "the spawner changed between two captures: try again";
        return std::nullopt;
    }
    return b;
}

guard::Request spawnerRemoveRequest(const ScriptRef& r, const SpawnerTarget& t) {
    guard::Request q(addr::SPAWNER_REMOVE_ACTOR);
    q.arg(t.root);
    q.sobject(t.actorObj.object);
    instanceConds(q, r);
    q.require(eq32(r.address + 0x174, t.actor, "tracked actor unchanged"));
    q.require(eq32(r.address + 0x9FC, t.linkedEffect, "linked effect unchanged"));
    q.require(eq32(addr::ACTOR_POOL + 8, t.actorObj.slot.pool.base, "actor pool unchanged"));
    q.require(eq32(t.actorObj.slot.identityAddr, t.actorObj.slot.identity, "actor generation"));
    q.require(eq32(t.actorObj.actorAddress + 0x30, t.actorObj.object, "actor object"));
    q.require(eq32(t.actorObj.actorAddress + 0x40, t.selfHandle, "actor's spawner owner"));
    q.require(eq32(t.actorObj.actorAddress + 0x44, 0, "actor's spawner offset"));
    q.require(eq32(t.actorObj.actorAddress + 0x24, t.actorObj.squadTag, "actor's squad controller"));
    q.require(eq32(t.actorObj.actorAddress + 0x28, t.actorObj.squadOffset, "actor's squad offset"));
    q.require(Cond::abs(t.bitmapAddr, Cmp::Eq, t.bitmapBit, Width::U32, t.bitmapBit, "object allocated"));
    if (t.squad) {
         
        const SquadRosterTarget& g = *t.squad;
        q.require(eq32(g.entry, g.entryWord, "squad controller allocation"));
        q.require(eq32(g.entry + 4, g.entryMeta, "squad controller identity"));
        q.require(eq32(g.root + 0x1D8, g.count, "squad roster count"));
        q.require(eq32(g.root + 0x1DC + g.selected * 8, g.words[size_t(g.selected) * 2], "roster entry generation"));
        q.require(eq32(g.root + 0x1E0 + g.selected * 8, t.actor, "roster entry actor"));
    }
    return q;
}

RemovalOutcome removeTrackedActor(Runtime& rt, Reader& mem, const ScriptInstance& s, const std::function<void(const std::string&)>& log) {
    RemovalOutcome out;
    auto say = [&](const std::string& t) {
        out.lines.push_back(t);
        if (log) log(t);
    };
    std::string why;
    const auto t = captureSpawnerTarget(mem, s, why);
    if (!t) {
        out.message = "Nothing changed: " + why;
        say(out.message);
        return out;
    }
    for (const auto& l : t->lines) say(l);
    const guard::Result res = guard::call(rt, spawnerRemoveRequest(s.ref, *t), 3000, false);
    if (!res.called) {
        out.message = "Refused on the game thread: " + res.reason;
        say(out.message);
        return out;
    }
    out.called = true;
     
     
    const uint32_t actorAfter = mem.u32(t->root + 0x174), effectAfter = mem.u32(t->root + 0x9FC);
    const uint32_t bitmapAfter = mem.u32(t->bitmapAddr);
    const Bytes rec = mem.bytes(t->actorObj.actorAddress, 0x48);
    const bool spawnerCleared = actorAfter == 0xFFFFFFFFu && effectAfter == 0xFFFFFFFFu;
    const bool objectGone = !(bitmapAfter & t->bitmapBit);
    const bool ownerCleared = rec.u32(0x40) == 0xFFFFFFFFu && rec.u32(0x44) == 0;
    const bool squadCleared = rec.u32(0x24) == 0xFFFFFFFFu && rec.u32(0x28) == 0;
    std::string rosterWhy;
    const bool rosterRemoved = !t->squad || squadRosterRemoved(mem, *t->squad, &rosterWhy);
    std::string identityWhy;
    ScriptRef spawner = s.ref;    
    if (spawner.guards.size() > kBaseGuards) spawner.guards.resize(kBaseGuards);
    const bool identityKept = validateScript(mem, spawner, &identityWhy) && mem.u32(addr::ACTOR_POOL + 8) == t->actorObj.slot.pool.base;
    say(std::string("spawner +0x174 / +0x9FC: ") + hex8(actorAfter) + " / " + hex8(effectAfter) + (spawnerCleared ? " (cleared)" : " (NOT cleared)"));
    say(std::string("object allocation bit: ") + (objectGone ? "clear" : "still set"));
    say(std::string("actor owner +0x40/+0x44: ") + hex8(rec.u32(0x40)) + " / " + hexShort(rec.u32(0x44)) + (ownerCleared ? " (cleared)" : " (NOT cleared)"));
    say(std::string("actor squad +0x24/+0x28: ") + hex8(rec.u32(0x24)) + " / " + hexShort(rec.u32(0x28)) + (squadCleared ? " (cleared)" : " (NOT cleared)"));
    if (t->squad) say(rosterRemoved ? std::string("squad roster: the actor's entry was removed") : "squad roster: NOT verified (" + rosterWhy + ")");
    if (!identityKept) say("spawner identity after the call: NOT verified" + (identityWhy.empty() ? std::string() : " (" + identityWhy + ")"));
    out.cleared = spawnerCleared && objectGone && ownerCleared && squadCleared && rosterRemoved && identityKept;
    out.message = out.cleared ? "Tracked actor removed (spawner, actor, object and roster cleared)"
                              : "Removal ran but not everything is verified cleared: inspect before retrying";
    say(out.message);
    return out;
}

 

std::vector<std::string> inspectTrackedActor(Reader& mem, const ScriptInstance& s) {
    std::vector<std::string> out;
    std::string why;
    if (s.ref.def.cls != cls::SPAWNER || s.ref.def.size < 0x178) return {"Select an AI spawner instance."};
    if (!validateScript(mem, s.ref, &why)) return {why};
    const uint32_t before = mem.u32(s.ref.address + 0x174);
    const auto a = readActorObject(mem, before, &why);
    if (!a) return {why};
    const uint32_t after = mem.u32(s.ref.address + 0x174);
    if (after != before || !validateScript(mem, s.ref, &why)) return {why.empty() ? "The spawner changed its tracked actor: refresh." : why};
    out.push_back("AI actor " + hex8(a->actor) + " at " + hex8(a->actorAddress) + " (generation " + hex8(a->generation) + ")");
    out.push_back("Object " + hex8(a->object) + " (" + world::typeName(a->objectType) + ") at " + hex8(a->objectAddress));
    out.push_back("Spawner owner " + hex8(a->ownerTag) + " +" + hexShort(a->ownerOffset) + ", squad owner " + hex8(a->squadTag) + " +" + hexShort(a->squadOffset));
    std::string w;
    const uint32_t self = instanceHandle(mem, s.ref, &w);
    if (self) out.push_back(std::string("This spawner's handle ") + hex8(self) + (a->ownerTag == self ? ": the actor names it as owner." : ": the actor names another owner."));
    else out.push_back("This spawner's own handle could not be reconstructed: " + w);
    out.push_back("Allocation bit, actor generation and object handle matched during this snapshot (not a lifetime guarantee).");
    return out;
}

std::vector<std::string> inspectSquadReference(Reader& mem, const ScriptInstance& s, const ScriptScan& scan, uint32_t* squadRoot) {
    if (squadRoot) *squadRoot = 0;
    std::string why;
    const auto b = readSpawnerBinding(mem, s.ref, &why);
    if (!b) return {why};
    ScriptReference ref;
    if (!resolveReference(mem, b->squadReference, 0, ref, why)) return {why};
    if (!ref.present) return {"No authored squad reference (native absent sentinel)."};
    if (ref.target[2] != cls::SQUAD) return {"The registered reference is not an AI squad controller (class " + hex8(ref.target[2]) + ")."};
    TagReader tags(mem);
    const auto m = tags.map(ref.target[0], 0, &why);
    if (!m) return {"Reference target allocation is unavailable: " + why};
    const uint32_t address = m->body + ref.target[1];
    const ScriptInstance* target = nullptr;
    for (const auto& i : scan.instances)
        if (i.ref.address == address && i.ref.def.cls == ref.target[2]) {
            if (target) return {"The reference target " + hex8(address) + " is ambiguous in this scan: rediscover."};
            target = &i;
        }
    if (!target) return {"The reference target " + hex8(address) + " is not a verified live controller in this scan: rediscover."};
     
    ScriptReference again;
    std::string w;
    const auto m2 = tags.map(ref.target[0], 0, &w);
    if (!resolveReference(mem, b->squadReference, 0, again, w) || again.target != ref.target || !m2 || m2->entryBytes.data() != m->entryBytes.data())
        return {"The squad reference changed while it was matched: try again."};
    const auto squad = readSquadBinding(mem, target->ref, &why);
    if (!squad) return {why};
    if (squadRoot) *squadRoot = target->ref.def.root;
    return {"Authored member index " + std::to_string(b->member) + " of " + std::to_string(squad->members) + " members (zero based)" +
                (b->member < squad->members ? ": in range." : ": OUT OF RANGE (native spawning must be refused)."),
            "Squad definition " + hex8(squad->tag) + " +" + hexShort(squad->offset),
            "Squad controller " + target->ref.def.name + ", root " + hex8(target->ref.def.root) + ", live instance " + hex8(target->ref.address),
            "Resolved through reference bank 0 (snapshot membership; actor identity and spawn eligibility need separate checks)."};
}

std::vector<std::string> inspectSpatialReference(Reader& mem, const ScriptInstance& s) {
    std::vector<std::string> out;
    std::string why;
    if (s.ref.def.cls != cls::SEQUENCE || s.ref.def.size < 0x13C) return {"Select a sequence instance."};
    if (!validateScript(mem, s.ref, &why)) return {why};
    const Bytes input = mem.bytes(s.ref.address + 0x108, 32);
    std::array<uint8_t, 8> bytes{};
    for (int i = 0; i < 8; ++i) bytes[size_t(i)] = input.u8(uint32_t(0x18 + i));
    for (unsigned bank = 0; bank < 2; ++bank) {
        ScriptReference ref;
        std::string line = "Bank " + std::to_string(bank) + (bank == nativeReferenceBank(bytes[4]) ? " (native selection): " : " (alternate): ");
        if (!resolveReference(mem, bytes, bank, ref, why)) {
            line += why;
        } else if (!ref.present) {
            out.push_back(line + "no spatial reference (native absent sentinel)");
            break;
        } else {
            line += "kind " + std::to_string(ref.kind) + ", slot " + std::to_string(ref.slot) + ", target " + hex8(ref.target[0]) + " +" + hexShort(ref.target[1]) +
                    ", class " + hex8(ref.target[2]);
            if (ref.target[2] == cls::SPATIAL_OBJECT) line += " (spatial object; its controller offsets are not verified on PS3)";
        }
        out.push_back(line);
    }
    const Bytes again = mem.bytes(s.ref.address + 0x108, 32);
    if (again.data() != input.data() || !validateScript(mem, s.ref, &why)) return {"The sequence changed while inspecting: refresh."};
    out.push_back("Read-only snapshot: playback would have to revalidate the target on the game thread.");
    return out;
}

}   
