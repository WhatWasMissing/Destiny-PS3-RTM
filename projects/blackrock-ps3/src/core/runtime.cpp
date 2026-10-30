#include "core/runtime.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#include "core/ppc.h"

namespace brps3 {

namespace {

namespace PF = profile;
using ppc::Asm;

constexpr uint32_t MAGIC = 0x42525831;      
constexpr uint32_t EXEC_SIZE = 0x400;
constexpr uint32_t TASKS = 0x10, NTASKS = 8;
constexpr uint32_t SLOTS = 0x100, NSLOTS = 16, SLOT = 0x80;
constexpr uint32_t ARENA = 0x1000;
constexpr int64_t FRAME = 0x100;
const std::vector<int> SAVE = {2, 29, 30, 31};

uint64_t nowMs() {
    using namespace std::chrono;
    return uint64_t(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

void sleepMs(int ms) {
    if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

std::string pyHex(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%x", v);
    return buf;
}

void put32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v >> 24);
    p[1] = uint8_t(v >> 16);
    p[2] = uint8_t(v >> 8);
    p[3] = uint8_t(v);
}

void put64(uint8_t* p, uint64_t v) {
    put32(p, uint32_t(v >> 32));
    put32(p + 4, uint32_t(v));
}

uint32_t get32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

uint64_t get64(const uint8_t* p) { return uint64_t(get32(p)) << 32 | get32(p + 4); }

std::vector<uint8_t> be32(uint32_t v) {
    std::vector<uint8_t> b(4);
    put32(b.data(), v);
    return b;
}

}   

 

std::vector<uint8_t> executorProgram(uint32_t cave, uint32_t ctrl, uint32_t tail) {
    Asm a(cave);
    ppc::prologue(a, FRAME, SAVE);
    a.addr(31, ctrl);
    a.lwz(0, 4, 31).cmpwi(0, 1).bne("out");
    a.lwz(12, 8, 31).addi(12, 12, 1).stw(12, 8, 31);
    a.addi(30, 31, TASKS).li(29, NTASKS);
    a.label("task").lwz(12, 0, 30).cmpwi(12, 0).beq("task_next");
    a.load32(2, PF::TOC).lwz(3, 4, 30).mtctr(12).bctrl();
    a.label("task_next").addi(30, 30, 8).addi(29, 29, -1).cmpwi(29, 0).bne("task");
    a.addi(30, 31, SLOTS).li(29, NSLOTS);
    a.label("slot").lwz(0, 0, 30).cmpwi(0, 1).bne("slot_next");
    a.li(0, 2).stw(0, 0, 30);
    a.lwz(2, 8, 30);
    for (int i = 0; i < 4; ++i) a.lfd(1 + i, 0x50 + 8 * i, 30);
    for (int i = 0; i < 8; ++i) a.ld(3 + i, 0x10 + 8 * i, 30);
    a.lwz(12, 4, 30).mtctr(12).bctrl();
    a.std_(3, 0x70, 30).stfd(1, 0x78, 30);
    a.sync().li(0, 3).stw(0, 0, 30);
    a.label("slot_next").addi(30, 30, SLOT).addi(29, 29, -1).cmpwi(29, 0).bne("slot");
    a.label("out");
    ppc::epilogue(a, FRAME, SAVE);
    a.b(tail);
    auto code = a.link();
    if (code.size() > EXEC_SIZE) throw Error("executor larger than its allocation");
    return code;
}

 

Heap::Heap(uint32_t base, uint32_t end) { free_.push_back({base, end - base}); }

uint32_t Heap::alloc(uint32_t size, uint32_t align) {
    uint64_t sz = (uint64_t(size) + 15) & ~uint64_t(15);
    for (size_t i = 0; i < free_.size(); ++i) {
        uint64_t a = free_[i].first, n = free_[i].second;
        uint64_t start = (a + align - 1) & ~uint64_t(align - 1);
        if (start + sz <= a + n) {
            std::vector<std::pair<uint32_t, uint32_t>> rest;
            if (start > a) rest.push_back({uint32_t(a), uint32_t(start - a)});
            if (start + sz < a + n) rest.push_back({uint32_t(start + sz), uint32_t(a + n - start - sz)});
            free_.erase(free_.begin() + std::ptrdiff_t(i));
            free_.insert(free_.begin() + std::ptrdiff_t(i), rest.begin(), rest.end());
            used_[uint32_t(start)] = uint32_t(sz);
            return uint32_t(start);
        }
    }
    throw Error("out of scratch memory for " + pyHex(uint32_t(sz)) + " bytes");
}

void Heap::release(uint32_t addr) {
    auto it = used_.find(addr);
    if (it == used_.end()) throw Error("no allocation at " + pyHex(addr));
    free_.push_back({addr, it->second});
    used_.erase(it);
    std::sort(free_.begin(), free_.end());
    std::vector<std::pair<uint32_t, uint32_t>> merged;
    for (const auto& [a, n] : free_) {
        if (!merged.empty() && merged.back().first + merged.back().second == a) merged.back().second += n;
        else merged.push_back({a, n});
    }
    free_ = std::move(merged);
}

uint32_t Heap::sizeOf(uint32_t addr) const {
    auto it = used_.find(addr);
    if (it == used_.end()) throw Error("no allocation at " + pyHex(addr));
    return it->second;
}

 

Runtime::Runtime(Session& s)
    : s_(s), cave_(PF::CAVE + EXEC_SIZE, PF::CAVE + PF::CAVE_SIZE), arena_(PF::CONTROL + ARENA, PF::CONTROL + PF::CONTROL_SIZE),
      caveLow_(PF::CAVE_LOW, PF::CAVE_LOW + PF::CAVE_LOW_SIZE), tasks_(NTASKS) {}

Runtime::~Runtime() {
    try {
        close();
    } catch (const std::exception& e) {
        coreLog(std::string("runtime close failed: ") + e.what());
    }
}

void Runtime::install() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    s_.verifyBuild();
    s_.claimScratch(PF::CAVE, PF::CAVE_SIZE);
    s_.claimScratch(PF::CONTROL, PF::CONTROL_SIZE);
     
     
    try {
        s_.claimScratch(PF::CAVE_LOW, PF::CAVE_LOW_SIZE);
        caveLowOk_ = true;
    } catch (const std::exception& e) {
        caveLowOk_ = false;
        coreLog(std::string("overflow cave not used: ") + e.what());
    }
    auto exec = executorProgram();
    s_.write(PF::CAVE, exec.data(), exec.size(), true);
    uint8_t header[12];
    put32(header, MAGIC);
    put32(header + 4, 1);
    put32(header + 8, 0);
    s_.write(PF::CONTROL, header, sizeof header, true);
    s_.patch(PF::TICK_SITE, be32(ppc::branchWord(PF::TICK_SITE, PF::CAVE, true)));
    installed_ = true;
}

void Runtime::close() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!installed_) return;
    try {
        uint8_t zero[4] = {};
        s_.write(PF::CONTROL + 4, zero, 4, true);
    } catch (...) {
        installed_ = false;
        throw;
    }
    installed_ = false;
}

uint32_t Runtime::heartbeat() { return s_.u32(PF::CONTROL + 8); }

bool Runtime::waitFrames(uint32_t n, int timeoutMs) {
    uint32_t start = heartbeat();
    uint64_t deadline = nowMs() + uint64_t(std::max(timeoutMs, 0));
    while (nowMs() < deadline) {
        uint32_t beat = heartbeat();
        if (uint32_t(beat - start) >= n) {
            lastBeat_ = beat;
            lastBeatMs_ = nowMs();
            return true;
        }
        sleepMs(30);
    }
    return false;
}

bool Runtime::alive() {
    if (nowMs() - lastBeatMs_.load() < 1000) return true;
    return waitFrames(1, 1500);
}

float Runtime::f32(uint32_t addr) {
    uint32_t v = u32(addr);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

void Runtime::write(uint32_t addr, const void* data, size_t size, bool verify) {
     
    s_.write(addr, data, size, verify);
}

void Runtime::writeU32(uint32_t addr, uint32_t v, bool verify) {
    uint8_t b[4];
    put32(b, v);
    write(addr, b, 4, verify);
}

void Runtime::writeF32(uint32_t addr, float v, bool verify) {
    uint32_t u;
    std::memcpy(&u, &v, 4);
    writeU32(addr, u, verify);
}

uint32_t Runtime::alloc(uint32_t size, const void* data, size_t n, uint32_t align) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    reclaimArena();
    uint32_t addr = arena_.alloc(std::max<uint32_t>(size, uint32_t(n)), align);
    if (data && n) {
        try {
            s_.write(addr, data, n, true);
        } catch (...) {
            arena_.release(addr);
            throw;
        }
    }
    return addr;
}

void Runtime::free(uint32_t addr) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    uint32_t size = arena_.sizeOf(addr);
    std::vector<uint8_t> zeros(size);
    s_.write(addr, zeros.data(), zeros.size(), true);
    arena_.release(addr);
}

void Runtime::freeLater(uint32_t addr) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        (void)arena_.sizeOf(addr);                              
    } catch (const std::exception&) {
        return;
    }
    for (const auto& p : arenaLater_)
        if (p.first == addr) return;
    uint32_t beat = lastBeat_.load();
    if (installed_) {
        try {
            beat = heartbeat();
        } catch (const std::exception&) {
        }
    }
    arenaLater_.push_back({addr, beat});
}

size_t Runtime::pendingFrees() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return arenaLater_.size();
}

void Runtime::reclaimArena() {
    if (arenaLater_.empty()) return;
    uint32_t beat = 0;
    if (installed_) {
        try {
            beat = heartbeat();
        } catch (const std::exception&) {
            return;                                             
        }
    }
    for (auto it = arenaLater_.begin(); it != arenaLater_.end();) {
        try {
            (void)arena_.sizeOf(it->first);
        } catch (const std::exception&) {
            it = arenaLater_.erase(it);                         
            continue;
        }
        if (!installed_ || uint32_t(beat - it->second) >= 2) {
            try {
                free(it->first);
            } catch (const std::exception& e) {
                coreLog("deferred arena free failed: " + std::string(e.what()));
                ++it;
                continue;
            }
            it = arenaLater_.erase(it);
        } else {
            ++it;
        }
    }
}

CallResult Runtime::call(uint32_t code, std::vector<uint64_t> args, std::vector<double> floats, int timeoutMs) {
    return callMany({NativeCall{code, std::move(args), std::move(floats), PF::TOC}}, timeoutMs)[0];
}

std::vector<CallResult> Runtime::callMany(const std::vector<NativeCall>& calls, int timeoutMs) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (calls.empty() || calls.size() > NSLOTS) throw Error("1..16 calls per batch");
    std::vector<uint8_t> body(calls.size() * SLOT);
    for (size_t i = 0; i < calls.size(); ++i) {
        const auto& c = calls[i];
        if (!(c.code >= 0x10000 && c.code < PF::TEXT_END && c.code % 4 == 0) || c.args.size() > 8 || c.floats.size() > 4)
            throw Error("bad call " + pyHex(c.code) + " with " + std::to_string(c.args.size()) + " args / " +
                        std::to_string(c.floats.size()) + " floats");
        uint8_t* p = body.data() + i * SLOT;
        put32(p + 0, 0);
        put32(p + 4, c.code);
        put32(p + 8, c.toc);
        put32(p + 0xC, uint32_t(i));
        for (size_t k = 0; k < c.args.size(); ++k) put64(p + 0x10 + 8 * k, c.args[k]);
        for (size_t k = 0; k < c.floats.size(); ++k) {
            uint64_t bits;
            std::memcpy(&bits, &c.floats[k], 8);
            put64(p + 0x50 + 8 * k, bits);
        }
    }
    if (!alive()) throw Error("the game thread is not running the executor (not in a game session?)");
    const uint32_t base = PF::CONTROL + SLOTS;
    s_.write(base, body.data(), body.size(), true);            
    for (size_t i = 0; i < calls.size(); ++i) put32(body.data() + i * SLOT, 1);
    s_.write(base, body.data(), body.size(), false);           
    uint64_t deadline = nowMs() + uint64_t(std::max(timeoutMs, 0));
    std::vector<uint8_t> got;
    for (;;) {
        got = s_.read(base, uint32_t(body.size()));
        std::vector<uint32_t> states;
        for (size_t i = 0; i < calls.size(); ++i) states.push_back(get32(got.data() + i * SLOT));
        if (std::all_of(states.begin(), states.end(), [](uint32_t st) { return st == 3; })) break;
        if (nowMs() > deadline) {
             
             
             
             
             
             
             
            lastBeatMs_ = 0;
            if (std::all_of(states.begin(), states.end(), [](uint32_t st) { return st == 1; })) {
                std::vector<uint8_t> zeros(body.size());
                s_.write(base, zeros.data(), zeros.size(), false);    
                throw Error("calls not picked up by the game thread; cancelled");
            }
            std::string list;
            for (uint32_t st : states) list += (list.empty() ? "" : ", ") + std::to_string(st);
            char secs[32];
            std::snprintf(secs, sizeof secs, "%.1f", timeoutMs / 1000.0);
            throw Error(std::string("calls did not finish in ") + secs + "s (states [" + list + "]); not cancelled");
        }
        sleepMs(20);
    }
    lastBeatMs_ = nowMs();
    std::vector<CallResult> out;
    for (size_t i = 0; i < calls.size(); ++i) {
        CallResult r;
        r.r3 = get64(got.data() + i * SLOT + 0x70);
        uint64_t bits = get64(got.data() + i * SLOT + 0x78);
        std::memcpy(&r.f1, &bits, 8);
        out.push_back(r);
    }
    return out;
}

void Runtime::updateTaskCount() {
    taskCount_ = size_t(std::count_if(tasks_.begin(), tasks_.end(), [](const auto& t) { return t.has_value(); }));
}

void Runtime::reclaimRetired(uint64_t now) {
    if (retired_.empty()) return;
    const bool aged = std::any_of(retired_.begin(), retired_.end(),
                                  [&](const Retired& r) { return now - r.at >= uint64_t(std::max(retireMs_, 0)); });
    if (!aged) return;
    uint32_t beat = 0;
    if (installed_) {
        try {
            beat = heartbeat();
        } catch (const std::exception&) {
            return;                                             
        }
    }
    for (auto it = retired_.begin(); it != retired_.end();) {
         
        if (now - it->at >= uint64_t(std::max(retireMs_, 0)) && (!installed_ || uint32_t(beat - it->beat) >= 2)) {
            std::vector<uint8_t> zeros(caveHeap(it->base).sizeOf(it->base));
            s_.write(it->base, zeros.data(), zeros.size(), true);
            caveHeap(it->base).release(it->base);
            it = retired_.erase(it);
        } else {
            ++it;
        }
    }
}

Heap& Runtime::caveHeap(uint32_t addr) {
    return addr >= PF::CAVE_LOW && addr < PF::CAVE_LOW + PF::CAVE_LOW_SIZE ? caveLow_ : cave_;
}

 
bool Runtime::tryCaveAlloc(uint32_t size, uint32_t* out) {
    try {
        *out = cave_.alloc(size);
        return true;
    } catch (const Error&) {
    }
    if (!caveLowOk_) return false;
    try {
        *out = caveLow_.alloc(size);
        return true;
    } catch (const Error&) {
        return false;
    }
}

uint32_t Runtime::caveAlloc(uint32_t size) {
    reclaimRetired(nowMs());
    uint32_t base = 0;
    if (tryCaveAlloc(size, &base)) return base;
    const std::string full = "the code cave is full (" + std::to_string(size) + " more bytes needed; every feature's stubs, tasks and hooks share " +
                             std::to_string((PF::CAVE_SIZE - EXEC_SIZE + (caveLowOk_ ? PF::CAVE_LOW_SIZE : 0)) / 1024) +
                             " KiB): turn off a feature you are not using, or reconnect to start with an empty cave";
    if (retired_.empty()) throw Error(full);
     
    const uint64_t deadline = nowMs() + uint64_t(std::max(retireMs_, 0)) + 3000;
    for (;;) {
        reclaimRetired(nowMs());
        if (tryCaveAlloc(size, &base)) return base;
        if (retired_.empty()) throw Error(full);
        if (nowMs() > deadline)
            throw Error("the code cave is full (" + std::to_string(retired_.size()) +
                        " unhooked stubs are still waiting for the game thread to move on)");
        sleepMs(100);
    }
}

int Runtime::addTaskEntry(const std::string& name, uint32_t code, bool owned, uint32_t arg) {
    auto slot = std::find(tasks_.begin(), tasks_.end(), std::nullopt);
    if (slot == tasks_.end()) throw Error("all 8 per-frame task slots are in use");
    int idx = int(slot - tasks_.begin());
    uint8_t entry[8];
    put32(entry, code);
    put32(entry + 4, arg);
    tasks_[size_t(idx)] = Task{code, owned, name};              
    try {                                                       
        s_.write(PF::CONTROL + TASKS + 8 * uint32_t(idx), entry, 8, true);
    } catch (...) {
        updateTaskCount();
        throw;
    }
    updateTaskCount();
    return idx;
}

int Runtime::addTask(const Build& build, uint32_t arg, uint32_t size) { return addTask(std::string(), build, arg, size); }

int Runtime::addTask(const std::string& name, const Build& build, uint32_t arg, uint32_t size) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!name.empty() && hasTask(name)) throw Error("task " + name + " is already running");
    if (std::find(tasks_.begin(), tasks_.end(), std::nullopt) == tasks_.end()) throw Error("all 8 per-frame task slots are in use");
    uint32_t base = caveAlloc(size);
    try {
        auto code = build(base);
        if (code.size() > size) throw Error("task program larger than its allocation");
        s_.write(base, code.data(), code.size(), true);
    } catch (...) {
        caveHeap(base).release(base);                           
        throw;
    }
    return addTaskEntry(name, base, true, arg);
}

int Runtime::addTaskAt(const std::string& name, uint32_t code, uint32_t arg) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (name.empty()) throw Error("addTaskAt needs a task name");
    if (hasTask(name)) throw Error("task " + name + " is already running");
    if (!(code >= 0x10000 && code < PF::TEXT_END && code % 4 == 0)) throw Error("bad task code address " + pyHex(code));
    return addTaskEntry(name, code, false, arg);
}

void Runtime::removeTask(int index) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (index < 0 || size_t(index) >= tasks_.size() || !tasks_[size_t(index)]) throw Error("no task " + std::to_string(index));
    const Task task = *tasks_[size_t(index)];
    uint8_t zero8[8] = {};
    s_.write(PF::CONTROL + TASKS + 8 * uint32_t(index), zero8, 8, true);
    tasks_[size_t(index)].reset();                              
    updateTaskCount();
    if (!task.owned) return;                                    
    waitFrames(2, 1000);                                        
    std::vector<uint8_t> zeros(caveHeap(task.code).sizeOf(task.code));
    s_.write(task.code, zeros.data(), zeros.size(), true);
    caveHeap(task.code).release(task.code);
}

void Runtime::removeTask(const std::string& name) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (name.empty()) return;
    for (size_t i = 0; i < tasks_.size(); ++i)
        if (tasks_[i] && tasks_[i]->name == name) {
            removeTask(int(i));
            return;
        }
}

bool Runtime::hasTask(const std::string& name) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return !name.empty() && std::any_of(tasks_.begin(), tasks_.end(), [&](const auto& t) { return t && t->name == name; });
}

std::vector<std::string> Runtime::taskNames() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<std::string> out;
    for (size_t i = 0; i < tasks_.size(); ++i)
        if (tasks_[i]) out.push_back(tasks_[i]->name.empty() ? "#" + std::to_string(i) : tasks_[i]->name);
    return out;
}

uint32_t Runtime::sharedCode(const std::string& name, const Build& build, uint32_t size) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (name.empty()) throw Error("shared code needs a name");
    auto it = shared_.find(name);
    if (it != shared_.end()) return it->second;
    uint32_t base = caveAlloc(size);
    try {
        auto code = build(base);
        if (code.empty() || code.size() > size) throw Error("shared code " + name + " does not fit its allocation");
        s_.write(base, code.data(), code.size(), true);
    } catch (...) {
        caveHeap(base).release(base);
        throw;
    }
    shared_[name] = base;
    return base;
}

std::optional<uint32_t> Runtime::sharedCodeAt(const std::string& name) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = shared_.find(name);
    if (it == shared_.end()) return std::nullopt;
    return it->second;
}

void Runtime::hook(uint32_t site, const std::function<std::vector<uint8_t>(uint32_t base, uint32_t original)>& build,
                   std::optional<uint32_t> expect, uint32_t size) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (hooks_.count(site)) throw Error(pyHex(site) + " is already hooked");
    uint32_t original = s_.u32(site);
    if (expect && original != *expect) throw Error(pyHex(site) + " holds " + hex32(original) + ", expected " + hex32(*expect));
    uint32_t base = caveAlloc(size);
    try {
        auto code = build(base, original);
        if (code.size() > size) throw Error("hook stub larger than its allocation");
        s_.write(base, code.data(), code.size(), true);
    } catch (...) {
        caveHeap(base).release(base);                           
        throw;
    }
    s_.patch(site, be32(ppc::branchWord(site, base)));         
    hooks_[site] = base;
    hookCount_ = hooks_.size();
}

void Runtime::unhook(uint32_t site) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = hooks_.find(site);
    if (it == hooks_.end()) throw Error(pyHex(site) + " is not hooked");
    uint32_t base = it->second;
    s_.restore(site);                                           
    hooks_.erase(it);
    hookCount_ = hooks_.size();
     
     
    uint32_t beat = lastBeat_.load();
    if (installed_) {
        try {
            beat = heartbeat();
        } catch (const std::exception&) {
        }
    }
    retired_.push_back({base, nowMs(), beat});
}

bool Runtime::hooked(uint32_t site) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return hooks_.count(site) != 0;
}

size_t Runtime::retiredCount() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return retired_.size();
}

void Runtime::setRetireDelay(int ms) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    retireMs_ = std::max(ms, 0);
}

Runtime::RevertReport Runtime::revertAll() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    RevertReport rep;
     
    for (int i = int(tasks_.size()) - 1; i >= 0; --i) {
        if (!tasks_[size_t(i)]) continue;
        const std::string what = tasks_[size_t(i)]->name.empty() ? "task #" + std::to_string(i) : "task " + tasks_[size_t(i)]->name;
        try {
            removeTask(i);
            ++rep.tasks;
        } catch (const std::exception& e) {
            rep.errors.push_back(what + ": " + e.what());
        }
    }
     
    std::vector<uint32_t> sites;
    for (const auto& kv : hooks_) sites.push_back(kv.first);
    for (auto it = sites.rbegin(); it != sites.rend(); ++it) {
        try {
            unhook(*it);
            ++rep.hooks;
        } catch (const std::exception& e) {
            rep.errors.push_back("hook " + pyHex(*it) + ": " + e.what());
        }
    }
     
    const std::vector<std::string> names = namedOrder_;
    for (auto it = names.rbegin(); it != names.rend(); ++it) {
        try {
            revert(*it);
            ++rep.patches;
        } catch (const std::exception& e) {
            rep.errors.push_back(*it + ": " + e.what());
        }
    }
    return rep;
}

bool Runtime::apply(const std::string& name, const std::vector<Word>& words) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (named_.count(name)) return false;
     
     
     
    enum class How : char { Write, Share, Stack };
    std::vector<How> how(words.size(), How::Write);
    for (size_t i = 0; i < words.size(); ++i) {
        const Word& w = words[i];
        const uint32_t size = uint32_t(w.original.size());
        if (size == 0 || w.replacement.size() != size)
            throw Error(name + ": the word at " + pyHex(w.addr) + " has original and replacement of different sizes");
        auto held = sites_.find(w.addr);
        if (held != sites_.end() && !held->second.empty()) {
            const Layer& top = held->second.back();
            auto cur = s_.read(w.addr, size);
            if (top.size == size && top.original == w.original && top.replacement == w.replacement) {
                if (cur != w.replacement)
                    throw Error(name + ": shared site " + pyHex(w.addr) + " holds " + hexBytes(cur) + ", expected " +
                                hexBytes(w.replacement) + " (patched by " + top.holders.front() + ")");
                how[i] = How::Share;
                continue;
            }
            if (top.size == size && top.replacement == w.original) {
                if (cur != w.original)
                    throw Error(name + ": " + pyHex(w.addr) + " holds " + hexBytes(cur) + ", expected " + hexBytes(w.original));
                how[i] = How::Stack;
                continue;
            }
            throw Error(name + ": " + pyHex(w.addr) + " is already patched differently by " + top.holders.front());
        }
        for (const auto& [addr, layers] : sites_) {
            if (layers.empty()) continue;
            const uint32_t len = layers.back().size;
            if (w.addr < addr + len && addr < w.addr + size)
                throw Error(name + ": " + pyHex(w.addr) + " overlaps the word at " + pyHex(addr) + " patched by " +
                            layers.back().holders.front());
        }
        auto cur = s_.read(w.addr, size);
        if (cur != w.original)
            throw Error(name + ": " + pyHex(w.addr) + " holds " + hexBytes(cur) + ", expected " + hexBytes(w.original));
    }
     
    std::vector<uint32_t> done;
    try {
        for (size_t i = 0; i < words.size(); ++i) {
            if (how[i] == How::Share) continue;
            s_.patch(words[i].addr, words[i].replacement);
            done.push_back(words[i].addr);
        }
    } catch (...) {
        for (auto it = done.rbegin(); it != done.rend(); ++it) {
            try {
                s_.restore(*it);
            } catch (const std::exception& e) {
                coreLog(name + ": undo failed: " + e.what());
            }
        }
        throw;
    }
     
    std::vector<uint32_t> addrs;
    for (size_t i = 0; i < words.size(); ++i) {
        const Word& w = words[i];
        auto& layers = sites_[w.addr];
        if (how[i] == How::Share) layers.back().holders.push_back(name);
        else layers.push_back(Layer{uint32_t(w.original.size()), w.original, w.replacement, {name}});
        addrs.push_back(w.addr);
    }
    named_[name] = std::move(addrs);
    namedOrder_.push_back(name);
    return true;
}

void Runtime::revert(const std::string& name) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = named_.find(name);
    if (it == named_.end()) return;
     
     
     
    auto& addrs = it->second;
    while (!addrs.empty()) {
        const uint32_t a = addrs.back();
        auto site = sites_.find(a);
        size_t at = 0;                                   
        if (site != sites_.end()) {
            const auto& layers = site->second;
            for (size_t k = layers.size(); k > 0 && !at; --k)
                if (std::find(layers[k - 1].holders.begin(), layers[k - 1].holders.end(), name) != layers[k - 1].holders.end()) at = k;
        }
        if (at && site->second[at - 1].holders.size() > 1) {
            auto& h = site->second[at - 1].holders;
            h.erase(std::remove(h.begin(), h.end(), name), h.end());
        } else {
            s_.restore(a);
            if (at) {
                site->second.erase(site->second.begin() + std::ptrdiff_t(at - 1));
                if (site->second.empty()) sites_.erase(site);
            }
        }
        addrs.pop_back();
    }
    named_.erase(it);
    namedOrder_.erase(std::remove(namedOrder_.begin(), namedOrder_.end(), name), namedOrder_.end());
}

std::vector<std::string> Runtime::siteHolders(uint32_t addr) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = sites_.find(addr);
    return it == sites_.end() || it->second.empty() ? std::vector<std::string>{} : it->second.back().holders;
}

bool Runtime::applied(const std::string& name) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return named_.count(name) != 0;
}

std::vector<std::string> Runtime::appliedNames() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return namedOrder_;
}

}   
