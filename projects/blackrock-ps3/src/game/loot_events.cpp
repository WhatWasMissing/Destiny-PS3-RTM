#include "game/loot_events.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>

#include "core/error.h"
#include "core/ppc.h"
#include "core/profile.h"
#include "core/runtime.h"
#include "core/sha256.h"
#include "game/cmd_handler.h"

namespace brps3::loot_events {

namespace {

namespace PF = profile;
using ppc::Asm;

 

std::string pyHex(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%x", v);
    return buf;
}

std::vector<uint8_t> packBE(std::initializer_list<uint32_t> words) {
    std::vector<uint8_t> out;
    for (uint32_t w : words) ppc::putBE32(out, w);
    return out;
}

uint32_t get32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

double nowSec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

void report(const Progress& p, const std::string& text) {
    if (p) p(text);
}

bool cancelled(const Cancel& c) { return c && c(); }

void ret(Asm& a, uint32_t tail) {    
    if (tail) a.b(tail);
    else a.blr();
}

 
 
 
 
 
bool attempt(const char* what, const std::function<void()>& fn, std::string& error) {
    for (int i = 0;; ++i) {
        try {
            fn();
            return true;
        } catch (const std::exception& e) {
            if (i == 1) {
                if (error.empty()) error = std::string(what) + ": " + e.what();
                return false;
            }
        }
    }
}

 
 
void finish(const char* flow, const std::string& cleanupError, bool failing) {
    if (cleanupError.empty()) return;
    std::string text = std::string(flow) + " cleanup failed (still journalled; Disconnect restores it): " + cleanupError;
    if (failing) coreLog(text);
    else throw Error(text);
}

 

struct CatalogState {
    std::mutex mutex;
    std::filesystem::path path;
    bool loaded = false;
    std::vector<Entry> entries;
    std::unordered_map<uint32_t, size_t> index;
};

CatalogState& state() {
    static CatalogState s;
    return s;
}

std::filesystem::path defaultCatalogPath() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, DWORD(std::size(buf)));
    std::filesystem::path exe(std::wstring(buf, n));
    return exe.parent_path() / "data" / "loot" / "loot_events.tsv";
}

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        size_t end = line.find('\t', start);
        if (end == std::string::npos) {
            out.push_back(line.substr(start));
            return out;
        }
        out.push_back(line.substr(start, end - start));
        start = end + 1;
    }
}

uint32_t parseHexU32(const std::string& s, const char* what) {
    std::string t = s;
    if (t.rfind("0x", 0) == 0 || t.rfind("0X", 0) == 0) t = t.substr(2);
    if (t.empty() || t.size() > 8 || t.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        throw Error(std::string("bad ") + what + ": " + s);
    return uint32_t(std::stoul(t, nullptr, 16));
}

}   

 

std::vector<Entry> loadCatalog(const std::filesystem::path& tsv) {
    std::ifstream f(tsv, std::ios::binary);
    if (!f) throw Error("loot_event catalog not found: " + tsv.string());
    std::string line;
    if (!std::getline(f, line)) throw Error("loot_event catalog is empty: " + tsv.string());
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.rfind("\xEF\xBB\xBF", 0) == 0) line = line.substr(3);
    auto header = splitTabs(line);
    auto col = [&](const char* name) {
        auto it = std::find(header.begin(), header.end(), name);
        if (it == header.end()) throw Error(std::string("loot_event catalog has no column ") + name);
        return size_t(it - header.begin());
    };
    const size_t cHash = col("hash"), cType = col("type"), cSchema = col("schema"), cName = col("name"), cLabel = col("label"),
                 cSize = col("payload_size"), cPresets = col("call_presets");
    std::vector<Entry> out;
    std::unordered_map<uint32_t, size_t> seen;
    int row = 1;
    while (std::getline(f, line)) {
        ++row;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        auto cells = splitTabs(line);
        cells.resize(std::max(cells.size(), header.size()));
        try {
            Entry e;
            e.hash = parseHexU32(cells[cHash], "hash");
            e.type = std::stoi(cells[cType]);
            e.schema = cells[cSchema].empty() ? 0 : parseHexU32(cells[cSchema], "schema");
            e.size = cells[cSize].empty() ? 0 : uint32_t(std::stoul(cells[cSize]));
            e.label = !cells[cName].empty() ? cells[cName] : cells[cLabel];
            e.presets = cells[cPresets];
            auto it = seen.find(e.hash);           
            if (it != seen.end()) {
                out[it->second] = std::move(e);
            } else {
                seen[e.hash] = out.size();
                out.push_back(std::move(e));
            }
        } catch (const Error&) {
            throw;
        } catch (const std::exception&) {
            throw Error("loot_event catalog row " + std::to_string(row) + " is malformed");
        }
    }
    return out;
}

void setCatalogPath(const std::filesystem::path& tsv) {
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (s.path == tsv) return;           
    s.path = tsv;
    s.loaded = false;
    s.entries.clear();
    s.index.clear();
}

const std::vector<Entry>& catalog() {
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.loaded) {
        auto entries = loadCatalog(s.path.empty() ? defaultCatalogPath() : s.path);
        s.entries = std::move(entries);
        s.index.clear();
        for (size_t i = 0; i < s.entries.size(); ++i) s.index[s.entries[i].hash] = i;
        s.loaded = true;
    }
    return s.entries;
}

const Entry* find(uint32_t hash) {
    const auto& entries = catalog();
    auto& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    auto it = s.index.find(hash);
    return it == s.index.end() ? nullptr : &entries[it->second];
}

std::vector<Field> parseFields(const std::string& text) {
     
    std::string t = text;
    std::replace(t.begin(), t.end(), ' ', ';');
    std::vector<Field> out;
    std::stringstream parts(t);
    std::string part;
    while (std::getline(parts, part, ';')) {
        if (part.empty()) continue;
        std::vector<std::string> p;
        std::stringstream ps(part);
        std::string x;
        while (std::getline(ps, x, ':')) p.push_back(x);
        if (part.back() == ':') p.push_back("");
        if (p.size() != 3) throw Error("field '" + part + "' is not offset:width:value (hex)");
        uint32_t off = parseHexU32(p[0], "field offset");
        uint32_t width = parseHexU32(p[1], "field width");
        uint32_t value = parseHexU32(p[2], "field value");
        out.push_back({off, int(width), value});
    }
    return out;
}

std::string formatFields(const std::vector<Field>& fields) {
    std::string out;
    char buf[48];
    for (const auto& f : fields) {
        std::snprintf(buf, sizeof buf, "%X:%X:%X", f.offset, unsigned(f.width), f.value);
        if (!out.empty()) out += ";";
        out += buf;
    }
    return out;
}

 
 

void validateCall(const Entry& e, const std::vector<Field>& fields, int count) {
    const bool typed = e.type != 0;
    if (typed && !(24 <= e.size && e.size <= 0x340)) throw Error("unsupported payload size");
    for (const auto& f : fields) {
        uint64_t w = uint64_t(f.width);
        if (!typed || (f.width != 1 && f.width != 2 && f.width != 4) || f.offset < 0x18 || uint64_t(f.offset) + w > e.size ||
            f.offset % w || (uint64_t(f.value) >> (8 * w)))
            throw Error("invalid field " + pyHex(f.offset) + ":" + std::to_string(f.width) + ":" + pyHex(f.value));
    }
    if (count < 1) throw Error("count must be at least 1");
}

std::pair<std::vector<uint8_t>, std::vector<uint8_t>> callProgram(const Entry& e, const std::vector<Field>& fields, int count,
                                                                  uint32_t cave, uint32_t ctrl, uint32_t tail) {
    validateCall(e, fields, count);
    const bool typed = e.type != 0;
    const int64_t frame = 0x480, payload = 0x100;
    const std::vector<int> save = {3, 2, 31};
    Asm a(cave);
    ppc::prologue(a, frame, save);
    a.addr(31, ctrl);
    a.lwz(0, 0, 31).cmpwi(0, 1).bne("exit");
    a.lwz(0, 0xC, 31).cmpwi(0, 0).beq("finish");
    a.load32(2, PF::TOC);
    if (typed) {
        a.lwz(3, 8, 31).addi(4, 1, payload).bl(PF::PAYLOAD_INIT);
        for (const auto& f : fields) {
            a.load32(10, f.value);
            if (f.width == 1) a.stb(10, payload + f.offset, 1);
            else if (f.width == 2) a.sth(10, payload + f.offset, 1);
            else a.stw(10, payload + f.offset, 1);
        }
        a.addi(3, 31, 4).lwz(4, 8, 31).addi(5, 1, payload).bl(PF::EMIT);
    } else {
        a.addi(3, 31, 4).bl(PF::EMIT_BASIC);
    }
    a.lwz(12, 0xC, 31).addi(12, 12, -1).stw(12, 0xC, 31);
    a.lwz(12, 0x10, 31).addi(12, 12, 1).stw(12, 0x10, 31);
    a.b("exit");
    a.label("finish").li(0, 3).stw(0, 0, 31);
    a.label("exit");
    ppc::epilogue(a, frame, save);
    ret(a, tail);
    return {a.link(), packBE({0, e.hash, e.schema, uint32_t(count), 0})};
}

int call(Runtime& rt, const Entry& e, const std::vector<Field>& fields, int count, Progress progress, Cancel cancel) {
    auto ctrlBytes = callProgram(e, fields, count, PF::CAVE, PF::CONTROL).second;    
    uint32_t ctrl = rt.alloc(0x40);
    int task = -1;
    try {
        rt.write(ctrl, ctrlBytes.data(), ctrlBytes.size());
        const uint32_t size = uint32_t((callProgram(e, fields, count, PF::CAVE + 0x400, ctrl).first.size() + 15) & ~size_t(15));
        task = rt.addTask([&](uint32_t base) { return callProgram(e, fields, count, base, ctrl).first; }, 0, size);
    } catch (...) {
        try {
            rt.free(ctrl);
        } catch (...) {
        }
        throw;
    }
    uint32_t done = 0;
    auto cleanup = [&] {
        std::string error;
        attempt("stop", [&] { rt.writeU32(ctrl, 0, false); }, error);    
        if (attempt("remove task", [&] { rt.removeTask(task); }, error)) attempt("free", [&] { rt.free(ctrl); }, error);
        return error;
    };
    try {
        rt.writeU32(ctrl, 1, false);
        double deadline = nowSec() + std::max(10.0, count / 25.0 + 10);
        uint32_t last = 0;
        while (nowSec() < deadline) {
            auto b = rt.read(ctrl, 0x14);
            uint32_t st = get32(b.data());
            done = get32(b.data() + 0x10);
            if (done != last) {
                report(progress, std::to_string(done) + "/" + std::to_string(count) + " emitted");
                last = done;
            }
            if (st == 3 || cancelled(cancel)) break;
            sleepMs(50);
        }
    } catch (...) {
        finish("loot_event call", cleanup(), true);
        throw;
    }
    finish("loot_event call", cleanup(), false);
    return int(done);
}

int callInGame(Runtime& rt, cmd::Handler& h, const Entry& e, const std::vector<Field>& fields, int count, Progress progress,
               Cancel cancel) {
    validateCall(e, fields, count);
    if (count > int(cmd::kMaxLootEventCount)) throw Error("count must be at most " + std::to_string(cmd::kMaxLootEventCount));
    if (fields.size() > cmd::kMaxFields) throw Error("at most " + std::to_string(cmd::kMaxFields) + " payload fields");
    cmd::LootEventArgs a;
    a.hash = e.hash;
    a.schema = e.schema;
    a.type = e.type != 0 ? uint32_t(e.type) : 0;
    a.size = e.size;
    a.count = uint32_t(count);
    for (const Field& f : fields) a.fields.push_back({f.offset, uint32_t(f.width), f.value});
    const auto body = cmd::encodeLootEvent(a, 0);
    const cmd::Outcome o = h.run(
        rt, body, [&cancel] { return cancelled(cancel); },
        [&](uint32_t done) { report(progress, std::to_string(done) + "/" + std::to_string(count) + " emitted"); });
    if (o.header.state == cmd::kStateFailed)
        throw Error(std::string("the command handler refused the call (") + cmd::resultName(o.header.code()) + ")");
    if (o.panicked) report(progress, std::to_string(o.header.done) + "/" + std::to_string(count) + " emitted (stopped by Panic)");
    return int(o.header.done);
}

 
 
 
 

namespace {
constexpr uint32_t GATHER_ROOT = 0x7A0FD954;

struct Bound {
    uint32_t record, handle, actor, sobj;
};

bool bindPlayer(Runtime& rt, Bound& out, std::string& why) {
    auto hdr = rt.read(PF::PLAYER_POOL + 8, 12);
    uint32_t base = get32(hdr.data()), stride = get32(hdr.data() + 4), capacity = get32(hdr.data() + 8);
    if (!(0x10000 <= base && base < 0x40000000u + 0x10000000u && stride && capacity && capacity <= 64)) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "player pool looks wrong: base 0x%x stride 0x%x capacity %u", base, stride, capacity);
        why = buf;
        return false;
    }
    std::vector<Bound> found;
    for (uint32_t slot = 0; slot < capacity; ++slot) {
        uint32_t at = base + slot * stride;
        auto rec = rt.read(at, 0x58);
        if ((rec[6] == 0xFE && rec[7] == 0xFE) || !(rec[0x40] & 1)) continue;
        uint32_t h = get32(&rec[0x48]), actor = get32(&rec[0x4C]), sobj = get32(&rec[0x54]);
        if (h != 0xFFFFFFFF && (h & 0x1FFF) == slot && actor != 0xFFFFFFFF && sobj != 0 && sobj != 0xFFFFFFFF)
            found.push_back({at, h, actor, sobj});
    }
    if (found.size() != 1) {
        why = "native gather needs exactly one bound local player, found " + std::to_string(found.size());
        return false;
    }
    out = found[0];
    return true;
}
}   

std::vector<uint8_t> gatherProgram(uint32_t cave, uint32_t ctrl, uint32_t tail) {
    const int64_t frame = 0x200, pos = 0x100, sel = 0x120;
    const std::vector<int> save = {3, 2, 31};
    Asm a(cave);
    ppc::prologue(a, frame, save);
    a.addr(31, ctrl);
    a.lwarx(0, 0, 31).cmpwi(0, 1).bne("exit").li(0, 2).stwcx(0, 0, 31).bne("exit");
     
    a.lwz(11, 8, 31);
    const int64_t pairs[3][2] = {{0x48, 0xC}, {0x4C, 0x10}, {0x54, 0x14}};
    for (const auto& p : pairs) a.lwz(0, p[0], 11).lwz(12, p[1], 31).cmplw(0, 12).bne("refuse");
    a.load32(2, PF::TOC);
    a.lwz(3, 0x14, 31).bl(PF::POSITION_OF);                  
    a.li(0, pos).stvx(2, 1, 0);
    a.lwz(0, 4, 31).stw(0, sel, 1);
    a.lwz(3, 0x10, 31).extsw(3, 3).addi(4, 1, pos).addi(5, 1, sel).bl(PF::GATHER_EMIT);
    a.li(0, 3).sync().stw(0, 0, 31).b("exit");
    a.label("refuse").li(0, 4).stw(0, 0, 31);
    a.label("exit");
    ppc::epilogue(a, frame, save);
    ret(a, tail);
    return a.link();
}

bool gather(Runtime& rt, uint32_t selector, std::string& message) {
    for (const auto& [addr, want] : PF::GATHER_FINGERPRINTS) {
        if (hexBytes(rt.read(addr, 16)) != want) {
            message = "gather code at " + pyHex(addr) + " differs from build " + PF::BUILD;
            return false;
        }
    }
    if (rt.u32(PF::LOOT_EVENT_HASHES + 0x58) != GATHER_ROOT) {    
        message = "gather_material root hash not where expected";
        return false;
    }
    Bound b{};
    if (!bindPlayer(rt, b, message)) return false;
    auto init = packBE({0, selector, b.record, b.handle, b.actor, b.sobj});
    uint32_t ctrl = rt.alloc(0x40, init.data(), init.size());
    int task;
    try {
        const uint32_t size = uint32_t((gatherProgram(PF::CAVE + 0x400, ctrl).size() + 15) & ~size_t(15));
        task = rt.addTask([&](uint32_t base) { return gatherProgram(base, ctrl); }, 0, size);
    } catch (...) {
        try {
            rt.free(ctrl);
        } catch (...) {
        }
        throw;
    }
    uint32_t st = 1;
    auto cleanup = [&] {
        std::string error;
        attempt("disarm", [&] { if (rt.u32(ctrl) == 1) rt.writeU32(ctrl, 0, false); }, error);
        if (attempt("remove task", [&] { rt.removeTask(task); }, error)) attempt("free", [&] { rt.free(ctrl); }, error);
        return error;
    };
    try {
        rt.writeU32(ctrl, 1, false);
        double deadline = nowSec() + 10;
        while (nowSec() < deadline && (st == 1 || st == 2)) {
            st = rt.u32(ctrl);
            sleepMs(50);
        }
        switch (st) {
            case 3: message = "gather emitted"; break;
            case 4: message = "refused: player binding changed"; break;
            case 1: message = "not run (not in game?)"; break;
            default: message = "state " + std::to_string(st); break;
        }
    } catch (...) {
        finish("gather", cleanup(), true);
        throw;
    }
    finish("gather", cleanup(), false);
    return st == 3;
}

 
 

std::pair<std::vector<uint8_t>, std::vector<uint8_t>> replayProgram(const Entry& e, int count, uint32_t cave, uint32_t ctrl) {
    if (count < 1) throw Error("count must be at least 1");
    Asm a(cave);
    a.addr(11, ctrl);
    a.lwz(12, 0, 11).cmpwi(12, 1).bne("pass");
    a.lwz(12, 8, 11).cmplw(4, 12).bne("pass");
    a.cmpwi(5, 0).beq("pass");
    a.lwarx(12, 0, 11).cmpwi(12, 1).bne("pass").li(12, 2).stwcx(12, 0, 11).bne("pass");
    const int64_t frame = 0x100;
    const std::vector<int> save = {3, 4, 5, 2, 31};
    ppc::prologue(a, frame, save);
    a.mr(31, 11).load32(2, PF::TOC);
    a.addi(3, 31, 4).lwz(4, 8, 31).bl(PF::EMIT);            
    a.lwz(12, 0x10, 31).addi(12, 12, 1).stw(12, 0x10, 31);
    a.lwz(12, 0xC, 31).addi(12, 12, -1).stw(12, 0xC, 31);
    a.cmpwi(12, 0).li(0, 1).bne("rearm").li(0, 3);
    a.label("rearm").sync().stw(0, 0, 31);
    ppc::epilogue(a, frame, save);
    a.label("pass").mflr(0).b(PF::EMIT + 4);                 
    return {a.link(), packBE({0, e.hash, e.schema, uint32_t(count), 0})};
}

int replay(Runtime& rt, const Entry& e, int count, double timeoutSec, Progress progress, Cancel cancel) {
    auto ctrlBytes = replayProgram(e, count, PF::CAVE, PF::CONTROL).second;
    uint32_t ctrl = rt.alloc(0x40);
    try {
        rt.write(ctrl, ctrlBytes.data(), ctrlBytes.size());
         
        const uint32_t size = uint32_t((replayProgram(e, count, PF::CAVE + 0x400, ctrl).first.size() + 15) & ~size_t(15));
        rt.hook(PF::EMIT, [&](uint32_t base, uint32_t) { return replayProgram(e, count, base, ctrl).first; }, 0x7C0802A6u, size);
    } catch (...) {
        try {
            rt.free(ctrl);
        } catch (...) {
        }
        throw;
    }
    uint32_t last = 0;
    auto cleanup = [&] {
        std::string error;
        attempt("disarm", [&] { if (rt.u32(ctrl) == 1) rt.writeU32(ctrl, 0, false); }, error);
         
        if (attempt("unhook", [&] { if (rt.hooked(PF::EMIT)) rt.unhook(PF::EMIT); }, error))
            attempt("free", [&] { rt.free(ctrl); }, error);
        return error;
    };
    try {
        rt.writeU32(ctrl, 1, false);
        report(progress, "armed: waiting for a natural schema " + hex32(e.schema) + " event");
        double deadline = nowSec() + timeoutSec;
        while (nowSec() < deadline && !cancelled(cancel)) {
            uint32_t st = rt.u32(ctrl), emitted = rt.u32(ctrl + 0x10);
            if (emitted != last) {
                report(progress, std::to_string(emitted) + "/" + std::to_string(count) + " replayed");
                last = emitted;
            }
            if (st == 3) break;
            sleepMs(50);
        }
    } catch (...) {
        finish("replay", cleanup(), true);
        throw;
    }
    finish("replay", cleanup(), false);
    return int(last);
}

}   
