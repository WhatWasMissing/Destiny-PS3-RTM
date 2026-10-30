#include "app/engine.h"
#include "app/heartbeat_health.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <map>
#include <set>
#include <thread>
#include <unordered_map>

#include "core/profile.h"
#include "core/runtime.h"
#include "core/session.h"

namespace brps3 {

namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

constexpr size_t kLogLines = 5000;

 
 
thread_local const std::atomic<bool>* tlCancel = nullptr;

std::string pyHex(uint32_t v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%x", v);
    return buf;
}

 
 
void appendLogFile(LogLine::Level level, const std::string& text) {
    static FILE* f = nullptr;
    static bool failed = false;
    if (failed) return;
    try {
        auto path = state_dir() / "blackrock.log";
        if (!f) {
            std::error_code ec;
            if (std::filesystem::file_size(path, ec) > (4u << 20) && !ec) {
                std::filesystem::rename(path, state_dir() / "blackrock.log.old", ec);
            }
            f = _wfopen(path.c_str(), L"ab");
            if (!f) { failed = true; return; }
        }
        SYSTEMTIME st;
        GetLocalTime(&st);
        static const char* lv[] = {"info", "good", "warn", "ERROR"};
        std::fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d.%03d %-5s %s\r\n", st.wYear, st.wMonth, st.wDay, st.wHour,
                     st.wMinute, st.wSecond, st.wMilliseconds, lv[int(level) & 3], text.c_str());
        std::fflush(f);
    } catch (...) {
        failed = true;
    }
}

}   

struct Engine::Impl {
     
    struct Conn;

    class CtxImpl final : public Ctx {
    public:
        CtxImpl(Impl& e, Conn& c) : e_(e), c_(c) {}

        Runtime& rt() override { return *c_.rt; }
        Settings& settings() override { return e_.settings; }
        void log(const std::string& text) override { e_.log(LogLine::Info, text); }
        void toast(const std::string& text) override { e_.toast(LogLine::Info, text); }
        bool cancelled() const override { return tlCancel && tlCancel->load(); }

        void require(const std::vector<std::pair<uint32_t, const char*>>& fingerprints, const char* what) override {
            std::string key = what ? what : "";
            for (const auto& [addr, hex] : fingerprints) key += "|" + hex32(addr) + ":" + hex;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (verified_.count(key)) return;
            }
            std::string bad;
            for (const auto& [addr, hex] : fingerprints) {
                auto want = parseHexBytes(hex);
                auto got = c_.rt->read(addr, uint32_t(want.size()));
                if (got != want)
                    bad += (bad.empty() ? "" : "; ") + pyHex(addr) + ": " + hexBytes(got) + " (expected " + hexBytes(want) + ")";
            }
            if (!bad.empty())
                throw Error(std::string(what ? what : "feature") + ": game bytes differ from build " + profile::BUILD +
                            " or are patched by another tool: " + bad);
            std::lock_guard<std::mutex> lock(mutex_);
            verified_.insert(key);
        }

        void startLoop(const std::string& id, int intervalMs, std::function<void(Ctx&)> fn) override {
            e_.startLoop(id, intervalMs, std::move(fn));
        }
        void stopLoop(const std::string& id) override { e_.stopLoop(id); }
        bool loopRunning(const std::string& id) const override { return e_.loopRunning(id); }

        void onDisconnect(std::function<void(Ctx&)> fn) override {
            std::lock_guard<std::mutex> lock(mutex_);
            hooks_.push_back(std::move(fn));
        }

        void onPanic(const std::string& id, std::function<void(Ctx&)> fn) override {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& [key, hook] : panicHooks_)
                if (key == id) {
                    hook = std::move(fn);
                    return;
                }
            panicHooks_.emplace_back(id, std::move(fn));
        }

         
        int runPanicHooks() {
            std::vector<std::pair<std::string, std::function<void(Ctx&)>>> hooks;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                hooks = panicHooks_;
            }
            int failed = 0;
            for (auto it = hooks.rbegin(); it != hooks.rend(); ++it) {
                if (!it->second) continue;
                try {
                    it->second(*this);
                } catch (const std::exception& ex) {
                    ++failed;
                    e_.log(LogLine::Warn, "Panic: " + it->first + ": " + ex.what());
                } catch (...) {
                    ++failed;
                    e_.log(LogLine::Warn, "Panic: " + it->first + ": unexpected failure");
                }
            }
            return failed;
        }

        void runDisconnectHooks() {
            for (;;) {
                std::function<void(Ctx&)> fn;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (hooks_.empty()) return;
                    fn = std::move(hooks_.back());
                    hooks_.pop_back();
                }
                 
                 
                try {
                    fn(*this);
                } catch (const std::exception& ex) {
                    e_.log(LogLine::Warn, std::string("cleanup failed: ") + ex.what());
                } catch (...) {
                    e_.log(LogLine::Warn, "cleanup failed: unexpected failure");
                }
            }
        }

    protected:
        std::shared_ptr<void>& stateSlot(const std::string& key) override {
            std::lock_guard<std::mutex> lock(mutex_);
            return slots_[key];    
        }

    private:
        Impl& e_;
        Conn& c_;
        mutable std::mutex mutex_;
        std::set<std::string> verified_;
        std::vector<std::function<void(Ctx&)>> hooks_;
        std::vector<std::pair<std::string, std::function<void(Ctx&)>>> panicHooks_;
        std::unordered_map<std::string, std::shared_ptr<void>> slots_;
    };

    struct Conn {
        std::string host;
        std::unique_ptr<Session> session;    
        std::unique_ptr<Runtime> rt;
        std::unique_ptr<CtxImpl> ctx;        
    };

    struct Job {
        std::string label;
        std::function<void(Ctx&)> fn;
        std::function<void(bool, const std::string&)> done;
    };

    struct Loop {
        int intervalMs;
        std::function<void(Ctx&)> fn;
        Clock::time_point next;
        uint64_t gen;
    };

    explicit Impl(Engine& e) : self(e) {
        settings.load();
        status.journalPending = Session::journalPending();
        setCoreLog([this](const std::string& text) { log(LogLine::Warn, text); });
        lifeThread = std::thread([this] { lifeMain(); });
        workerThread = std::thread([this] { workerMain(); });
        loopThread = std::thread([this] { loopMain(); });
        refreshThread = std::thread([this] { refreshMain(); });
    }

    ~Impl() { shutdown(); }

     
    Engine& self;
    Settings settings;
    const Clock::time_point start = Clock::now();

    mutable std::mutex logMutex;
    std::deque<LogLine> lines;
    size_t base = 0;                     
    std::vector<Toast> toasts;

    mutable std::mutex statusMutex;
    EngineStatus status;                 

    mutable std::mutex connMutex;
    std::shared_ptr<Conn> conn;
    std::atomic<int> state{EngineStatus::Disconnected};
    std::atomic<bool> disconnecting{false};

    mutable std::mutex lifeMutex;
    std::condition_variable lifeCv;
    std::deque<std::function<void()>> lifeQueue;
    bool lifeBusy = false;
    std::string lifeJob;                 

    mutable std::mutex jobMutex;
    std::condition_variable jobCv;
    std::deque<Job> jobs;
    std::string running;
    bool jobActive = false;
    std::atomic<bool> jobCancel{false};

    mutable std::mutex loopMutex;
    std::condition_variable loopCv;
    std::map<std::string, Loop> loops;
    std::string runningLoop;
    uint64_t genCounter = 0;
    std::atomic<bool> loopStop{false};
    std::thread::id loopThreadId;

    std::mutex uiMutex;
    std::deque<std::function<void()>> uiQueue;

    std::atomic<bool> quit{false};
    std::mutex quitMutex;
    std::condition_variable quitCv;
    std::thread lifeThread, workerThread, loopThread, refreshThread;
    std::once_flag shutdownOnce;

    double now() const { return std::chrono::duration<double>(Clock::now() - start).count(); }

     
    void log(LogLine::Level level, const std::string& text) {
        std::lock_guard<std::mutex> lock(logMutex);
        appendLogFile(level, text);
        lines.push_back({now(), level, text});
        while (lines.size() > kLogLines) {
            lines.pop_front();
            ++base;
        }
    }

    void toast(LogLine::Level level, const std::string& text) {
        log(level, text);
        std::lock_guard<std::mutex> lock(logMutex);
        toasts.push_back({now(), level, text});
        if (toasts.size() > 50) toasts.erase(toasts.begin());
    }

    void postUi(std::function<void()> fn) {
        std::lock_guard<std::mutex> lock(uiMutex);
        uiQueue.push_back(std::move(fn));
    }

    void pump() {
        std::deque<std::function<void()>> todo;
        {
            std::lock_guard<std::mutex> lock(uiMutex);
            todo.swap(uiQueue);
        }
        for (auto& fn : todo) {
            try {
                fn();
            } catch (const std::exception& e) {
                log(LogLine::Error, std::string("UI callback failed: ") + e.what());
            }
        }
    }

    void setState(EngineStatus::State s) {
        state = s;
        std::lock_guard<std::mutex> lock(statusMutex);
        status.state = s;
    }

    std::shared_ptr<Conn> liveConn() const {
        std::lock_guard<std::mutex> lock(connMutex);
        if (state != EngineStatus::Connected || disconnecting) return nullptr;
        return conn;
    }

     
    void enqueueLife(std::function<void()> fn) {
        {
            std::lock_guard<std::mutex> lock(lifeMutex);
            lifeQueue.push_back(std::move(fn));
        }
        lifeCv.notify_all();
    }

    void lifeMain() {
        for (;;) {
            std::function<void()> fn;
            {
                std::unique_lock<std::mutex> lock(lifeMutex);
                lifeCv.wait(lock, [&] { return quit || !lifeQueue.empty(); });
                if (lifeQueue.empty()) return;
                fn = std::move(lifeQueue.front());
                lifeQueue.pop_front();
                lifeBusy = true;
            }
            try {
                fn();
            } catch (const std::exception& e) {
                log(LogLine::Error, e.what());
            } catch (...) {
                log(LogLine::Error, "connection step failed unexpectedly");
            }
            {
                std::lock_guard<std::mutex> lock(lifeMutex);
                lifeBusy = false;
            }
            lifeCv.notify_all();
        }
    }

    void doConnect(const std::string& host) {
        {
            std::lock_guard<std::mutex> lock(statusMutex);
            status = EngineStatus{};
            status.state = EngineStatus::Connecting;
            status.host = host;
        }
        log(LogLine::Info, "Connecting to " + host + "...");
        auto c = std::make_shared<Conn>();
        c->host = host;
        try {
            c->session = std::make_unique<Session>(host);
            log(LogLine::Info, "Found " + c->session->process() + " (pid " + pyHex(c->session->pid()) + ")");
            c->rt = std::make_unique<Runtime>(*c->session);
            c->rt->install();
            c->ctx = std::make_unique<CtxImpl>(*this, *c);
        } catch (const std::exception& e) {
            std::string msg = e.what();
            c.reset();    
            {
                std::lock_guard<std::mutex> lock(statusMutex);
                status.error = msg;
                status.journalPending = Session::journalPending();
            }
            setState(EngineStatus::Disconnected);
            toast(LogLine::Error, "Connect failed: " + msg);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(connMutex);
            conn = c;
        }
        {
            std::lock_guard<std::mutex> lock(statusMutex);
            status.process = c->session->process();
            status.build = profile::BUILD;
            status.error.clear();
            status.journalPending = true;    
        }
        jobCancel = false;
        setState(EngineStatus::Connected);
        postUi([this] {
            if (self.onConnected) self.onConnected();
        });
        bool ticking = c->rt->waitFrames(1, 1500);
        toast(LogLine::Good, "Connected to " + c->session->process() + (ticking ? "" : " (waiting for the game: load into a session)"));
    }

    void doDisconnect(const std::string& reason) {
        std::shared_ptr<Conn> c;
        {
            std::lock_guard<std::mutex> lock(connMutex);
            c = conn;
        }
        if (!c) {
            disconnecting = false;
            return;
        }
        disconnecting = true;
        setState(EngineStatus::Disconnecting);
        log(LogLine::Info, "Disconnecting" + (reason.empty() ? std::string() : " (" + reason + ")") + "...");

         
        std::deque<Job> dropped;
        {
            std::unique_lock<std::mutex> lock(jobMutex);
            dropped.swap(jobs);
            jobCancel = true;
            auto t0 = Clock::now();
            bool told = false;
            while (jobActive) {
                jobCv.wait_for(lock, 250ms);
                if (!told && jobActive && Clock::now() - t0 > 3s) {
                    told = true;
                    std::string label = running;
                    lock.unlock();
                    log(LogLine::Warn, "Waiting for \"" + label + "\" to stop...");
                    lock.lock();
                }
            }
        }
        for (auto& j : dropped)
            if (j.done) postUi([d = std::move(j.done)] { d(false, "Disconnected"); });

         
        stopAllLoops();
        c->ctx->runDisconnectHooks();
        try {
            c->rt->close();
        } catch (const std::exception& e) {
            log(LogLine::Warn, std::string("Executor stop failed: ") + e.what());
        }
        bool restored = false;
        try {
            size_t n = c->session->patchCount();
            c->session->restoreAll();
            restored = true;
            log(LogLine::Good, "Restored " + std::to_string(n) + " patch" + (n == 1 ? "" : "es") + " and cleared scratch memory");
        } catch (const std::exception& e) {
            toast(LogLine::Error, std::string("Restore failed: ") + e.what() + ". The journal is kept: use Recover.");
        }

         
        {
            std::lock_guard<std::mutex> lock(connMutex);
            conn.reset();
        }
        auto deadline = Clock::now() + 15s;
        while (c.use_count() > 1 && Clock::now() < deadline) std::this_thread::sleep_for(5ms);
        c.reset();    

        {
            std::lock_guard<std::mutex> lock(statusMutex);
            std::string host = status.host;
            status = EngineStatus{};
            status.host = host;
            status.journalPending = Session::journalPending();
        }
        disconnecting = false;
        jobCancel = false;
        setState(EngineStatus::Disconnected);
        if (restored) toast(LogLine::Good, "Disconnected: game code restored");
        postUi([this] {
            for (const auto& s : Registry::get().sections()) {
                if (!s.onDisconnect) continue;
                try {
                    s.onDisconnect();
                } catch (const std::exception& e) {
                    log(LogLine::Warn, s.id + ": " + e.what());
                }
            }
            if (self.onDisconnected) self.onDisconnected();
        });
    }

    void doRecover(const std::string& host) {
        if (!Session::journalPending()) {
            toast(LogLine::Info, "Nothing to recover");
            std::lock_guard<std::mutex> lock(statusMutex);
            status.journalPending = false;
            return;
        }
        {
            std::lock_guard<std::mutex> lock(lifeMutex);
            lifeJob = "Recover";
        }
        log(LogLine::Info, "Recovering from the journal on " + host + "...");
        try {
            auto done = Session::recover(host);
            std::string list;
            for (const auto& r : done) list += (list.empty() ? "" : ", ") + r;
            if (!list.empty()) log(LogLine::Info, "Restored " + list);
            toast(LogLine::Good, "Recovery done: " + std::to_string(done.size()) + " region" + (done.size() == 1 ? "" : "s") + " restored");
        } catch (const std::exception& e) {
            toast(LogLine::Error, std::string("Recovery failed: ") + e.what());
        }
        {
            std::lock_guard<std::mutex> lock(lifeMutex);
            lifeJob.clear();
        }
        std::lock_guard<std::mutex> lock(statusMutex);
        status.journalPending = Session::journalPending();
    }

     
    void post(const std::string& label, std::function<void(Ctx&)> job, std::function<void(bool, const std::string&)> done) {
        {
            std::lock_guard<std::mutex> lock(jobMutex);
            jobs.push_back({label, std::move(job), std::move(done)});
        }
        jobCv.notify_all();
    }

    void workerMain() {
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(jobMutex);
                jobCv.wait(lock, [&] { return quit || !jobs.empty(); });
                if (jobs.empty()) return;
                job = std::move(jobs.front());
                jobs.pop_front();
                running = job.label;
                jobActive = true;
                jobCancel = false;
            }
            bool ok = false;
            std::string err;
            auto c = liveConn();
            if (!c) {
                err = disconnecting ? "Disconnected" : "Not connected";
            } else {
                tlCancel = &jobCancel;
                try {
                    job.fn(*c->ctx);
                    ok = true;
                } catch (const std::exception& e) {
                    err = e.what();
                } catch (...) {
                    err = "unexpected failure";
                }
                tlCancel = nullptr;
            }
            c.reset();
            {
                std::lock_guard<std::mutex> lock(jobMutex);
                running.clear();
                jobActive = false;
            }
            jobCv.notify_all();
            if (!ok) {
                if (err == "Not connected") toast(LogLine::Warn, job.label + ": connect to the console first");
                else if (err != "Disconnected") toast(LogLine::Error, job.label + ": " + err);
            }
            if (job.done) postUi([d = std::move(job.done), ok, err] { d(ok, err); });
        }
    }

    bool busy(const std::string& label) const {
        std::lock_guard<std::mutex> lock(jobMutex);
        if (jobActive && running == label) return true;
        return std::any_of(jobs.begin(), jobs.end(), [&](const Job& j) { return j.label == label; });
    }

     
    bool panic(const std::vector<std::string>& toggles, const std::vector<std::string>& values) {
        if (!liveConn()) return false;
        std::deque<Job> dropped;
        bool already = false;
        {
            std::lock_guard<std::mutex> lock(jobMutex);
            already = (jobActive && running == kPanicJob) ||
                      std::any_of(jobs.begin(), jobs.end(), [](const Job& j) { return j.label == kPanicJob; });
            if (!already) {
                dropped.swap(jobs);
                if (jobActive) jobCancel = true;    
                jobs.push_front({kPanicJob, [this, toggles, values](Ctx& c) { runPanic(c, toggles, values); },
                                 [this](bool, const std::string&) {
                                     for (const auto& s : Registry::get().sections()) {
                                         const auto& reset = s.onPanic ? s.onPanic : s.onDisconnect;
                                         if (!reset) continue;
                                         try {
                                             reset();
                                         } catch (const std::exception& e) {
                                             log(LogLine::Warn, s.id + ": " + e.what());
                                         }
                                     }
                                     if (self.onPanicked) self.onPanicked();
                                 }});
            }
        }
        if (already) return true;
        jobCv.notify_all();
        for (auto& j : dropped)
            if (j.done) postUi([d = std::move(j.done)] { d(false, "Cancelled by Panic"); });
        log(LogLine::Warn, "Panic: turning everything off" +
                               (dropped.empty() ? std::string() : " (" + std::to_string(dropped.size()) + " queued jobs dropped)"));
        return true;
    }

    void runPanic(Ctx& c, const std::vector<std::string>& toggles, const std::vector<std::string>& values) {
        stopAllLoops();
        const Registry& reg = Registry::get();
        int off = 0, reset = 0, failed = 0;
        for (const std::string& id : toggles) {
            const ToggleDef* t = reg.findToggle(id);
            if (!t || !t->set) continue;
            try {
                t->set(c, false);
                ++off;
            } catch (const std::exception& e) {
                ++failed;
                log(LogLine::Warn, "Panic: " + t->label + " off failed: " + e.what());
            }
        }
        for (const std::string& id : values) {
            const ValueDef* v = reg.findValue(id);
            if (!v) continue;
            try {
                if (v->reset) v->reset(c);
                else if (v->set) v->set(c, v->def);
                else continue;
                ++reset;
            } catch (const std::exception& e) {
                ++failed;
                log(LogLine::Warn, "Panic: " + v->label + " reset failed: " + e.what());
            }
        }
        failed += static_cast<CtxImpl&>(c).runPanicHooks();
        Runtime::RevertReport rep = c.rt().revertAll();
        for (const std::string& e : rep.errors) log(LogLine::Warn, "Panic: " + e);
        failed += int(rep.errors.size());
        std::string text = "Panic: all off (" + std::to_string(off) + (off == 1 ? " switch, " : " switches, ") + std::to_string(reset) +
                           (reset == 1 ? " value reset" : " values reset");
        const size_t left = rep.patches + rep.hooks + rep.tasks;
        if (left) text += "; reverted " + std::to_string(left) + " leftover patch/hook/task" + (left == 1 ? "" : "s");
        text += ")";
        if (failed) text += ". " + std::to_string(failed) + " step" + (failed == 1 ? "" : "s") + " failed: see the log";
        toast(failed ? LogLine::Warn : LogLine::Good, text);
    }

     
    void startLoop(const std::string& id, int intervalMs, std::function<void(Ctx&)> fn) {
        stopLoop(id);
        {
            std::lock_guard<std::mutex> lock(loopMutex);
            loops[id] = Loop{std::max(intervalMs, 1), std::move(fn), Clock::now(), ++genCounter};
        }
        loopCv.notify_all();
    }

    void stopLoop(const std::string& id) {
        std::unique_lock<std::mutex> lock(loopMutex);
        loops.erase(id);
        if (runningLoop == id) {
            loopStop = true;
            if (std::this_thread::get_id() != loopThreadId) loopCv.wait(lock, [&] { return runningLoop != id; });
        }
    }

    void stopAllLoops() {
        std::unique_lock<std::mutex> lock(loopMutex);
        loops.clear();
        if (!runningLoop.empty()) {
            loopStop = true;
            if (std::this_thread::get_id() != loopThreadId) loopCv.wait(lock, [&] { return runningLoop.empty(); });
        }
    }

    bool loopRunning(const std::string& id) const {
        std::lock_guard<std::mutex> lock(loopMutex);
        return loops.count(id) != 0;
    }

    void loopMain() {
        {
            std::lock_guard<std::mutex> lock(loopMutex);
            loopThreadId = std::this_thread::get_id();
        }
        for (;;) {
            std::string id;
            std::function<void(Ctx&)> fn;
            uint64_t gen = 0;
            {
                std::unique_lock<std::mutex> lock(loopMutex);
                for (;;) {
                    if (quit) return;
                    if (loops.empty()) {
                        loopCv.wait(lock);
                        continue;
                    }
                    auto due = std::min_element(loops.begin(), loops.end(),
                                                [](const auto& a, const auto& b) { return a.second.next < b.second.next; });
                    if (due->second.next <= Clock::now()) {
                        id = due->first;
                        fn = due->second.fn;
                        gen = due->second.gen;
                        runningLoop = id;
                        loopStop = false;
                        break;
                    }
                    loopCv.wait_until(lock, due->second.next);
                }
            }
            std::string err;
            auto c = liveConn();
            const bool live = c != nullptr;
            if (live) {
                tlCancel = &loopStop;
                try {
                    fn(*c->ctx);
                } catch (const std::exception& e) {
                    err = e.what();
                } catch (...) {
                    err = "unexpected failure";
                }
                tlCancel = nullptr;
            }
            c.reset();
            bool stopped = false;
            {
                std::lock_guard<std::mutex> lock(loopMutex);
                runningLoop.clear();
                auto it = loops.find(id);
                if (it != loops.end() && it->second.gen == gen) {
                    if (!err.empty() || !live) {
                        loops.erase(it);
                        stopped = !err.empty();
                    } else {
                        it->second.next = Clock::now() + std::chrono::milliseconds(it->second.intervalMs);
                    }
                }
            }
            loopCv.notify_all();
            if (stopped) toast(LogLine::Error, "[" + id + "] stopped: " + err);
        }
    }

     
    void refreshMain() {
        HeartbeatHealth heartbeatHealth;
        const Conn* seen = nullptr;
        uint32_t lastBeat = 0, windowBeat = 0;
        double lastChange = -1e9, windowStart = 0, fps = 0, lastJournalCheck = -1e9;
        for (;;) {
            {
                std::unique_lock<std::mutex> lock(quitMutex);
                quitCv.wait_for(lock, 250ms, [&] { return quit.load(); });
            }
            if (quit) return;
            settings.saveIfDirty();
            double t = now();
            auto c = liveConn();
            if (!c) {
                seen = nullptr;
                heartbeatHealth.reset();
                if (state == EngineStatus::Disconnected && t - lastJournalCheck >= 1.0) {
                    lastJournalCheck = t;
                    bool pending = Session::journalPending();
                    std::lock_guard<std::mutex> lock(statusMutex);
                    status.journalPending = pending;
                }
                continue;
            }
            if (!heartbeatHealth.shouldSample(Clock::now())) continue;
            try {
                auto t0 = Clock::now();
                uint32_t beat = c->rt->heartbeat();
                double latency = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
                if (c.get() != seen) {
                    seen = c.get();
                    lastBeat = windowBeat = beat;
                    windowStart = t;
                    lastChange = -1e9;
                    fps = 0;
                }
                if (beat != lastBeat) {
                    lastBeat = beat;
                    lastChange = t;
                }
                if (t - windowStart >= 1.0) {
                    fps = double(uint32_t(beat - windowBeat)) / (t - windowStart);
                    windowStart = t;
                    windowBeat = beat;
                }
                bool ticking = heartbeatHealth.ticking(t, lastChange);
                std::lock_guard<std::mutex> lock(statusMutex);
                status.heartbeat = beat;
                status.ticking = ticking;
                status.fps = ticking ? float(fps) : 0.0f;
                status.latencyMs = latency;
                status.patches = c->session->patchCount();
                status.hooks = c->rt->hookCount();
                status.tasks = c->rt->taskCount();
                status.journalPending = true;
                if (heartbeatHealth.succeeded()) {
                    log(LogLine::Good, "Executor heartbeat polling recovered");
                    status.error.clear();
                }
            } catch (const std::exception& e) {
                const bool prolongedFailure = heartbeatHealth.failed(t);
                if (heartbeatHealth.failureCount() == 3)
                    log(LogLine::Warn, std::string("Executor heartbeat read is slow, retrying: ") + e.what());
                if (prolongedFailure) {
                    const std::string msg = std::string("Executor heartbeat unavailable: ") + e.what();
                    log(LogLine::Warn, msg + " (PS3MAPI session retained; disconnect manually if necessary)");
                    // A failed PASV transfer does not prove the control session
                    // disconnected. Do not tear down the session and attempt
                    // patch restoration automatically on this ambiguous signal.
                    std::lock_guard<std::mutex> lock(statusMutex);
                    status.error = msg;
                    status.ticking = false;
                    status.fps = 0;
                }
            }
        }
    }

    EngineStatus snapshot() const {
        EngineStatus s;
        {
            std::lock_guard<std::mutex> lock(statusMutex);
            s = status;
        }
        s.state = EngineStatus::State(state.load());
        {
            std::lock_guard<std::mutex> lock(jobMutex);
            s.queued = jobs.size();
            s.job = jobActive ? running : std::string();
        }
        {
            std::lock_guard<std::mutex> lock(loopMutex);
            s.loops = loops.size();
        }
        if (s.job.empty()) {
            std::lock_guard<std::mutex> lock(lifeMutex);
            s.job = lifeJob;
        }
        if (s.state != EngineStatus::Connected) {
            s.ticking = false;
            s.fps = 0;
        }
        return s;
    }

     
    void shutdown() {
        std::call_once(shutdownOnce, [this] {
            enqueueLife([this] { doDisconnect("closing"); });
            {
                std::unique_lock<std::mutex> lock(lifeMutex);
                lifeCv.wait(lock, [&] { return lifeQueue.empty() && !lifeBusy; });
            }
             
             
             
            quit = true;
            {
                std::lock_guard<std::mutex> lock(lifeMutex);
            }
            lifeCv.notify_all();
            {
                std::lock_guard<std::mutex> lock(jobMutex);
            }
            jobCv.notify_all();
            {
                std::lock_guard<std::mutex> lock(loopMutex);
            }
            loopCv.notify_all();
            {
                std::lock_guard<std::mutex> lock(quitMutex);
            }
            quitCv.notify_all();
            for (std::thread* t : {&lifeThread, &workerThread, &loopThread, &refreshThread})
                if (t->joinable()) t->join();
            settings.save();
            setCoreLog(nullptr);
        });
    }
};

 

Engine& Engine::get() {
    static Engine engine;
    return engine;
}

Engine::Engine() : d_(std::make_unique<Impl>(*this)) {}

void Engine::connect(const std::string& host) {
    int expected = EngineStatus::Disconnected;
    if (!d_->state.compare_exchange_strong(expected, EngineStatus::Connecting)) {
        d_->toast(LogLine::Warn, expected == EngineStatus::Connected ? "Already connected" : "Busy: wait for the current connection step");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(d_->statusMutex);
        d_->status.state = EngineStatus::Connecting;
        d_->status.host = host;
        d_->status.error.clear();
    }
    d_->enqueueLife([this, host] { d_->doConnect(host); });
}

void Engine::disconnect() {
    if (d_->state == EngineStatus::Disconnected) return;
    d_->disconnecting = true;    
    d_->jobCancel = true;
    int expected = EngineStatus::Connected;
    if (d_->state.compare_exchange_strong(expected, EngineStatus::Disconnecting)) {
        std::lock_guard<std::mutex> lock(d_->statusMutex);
        d_->status.state = EngineStatus::Disconnecting;
    }
    d_->enqueueLife([this] { d_->doDisconnect(""); });
}

void Engine::recover(const std::string& host) {
    if (d_->state != EngineStatus::Disconnected) {
        d_->toast(LogLine::Warn, "Disconnect before running recovery");
        return;
    }
    d_->enqueueLife([this, host] { d_->doRecover(host); });
}

void Engine::shutdown() { d_->shutdown(); }

void Engine::post(const std::string& label, std::function<void(Ctx&)> job, std::function<void(bool ok, const std::string& error)> done) {
    d_->post(label, std::move(job), std::move(done));
}

void Engine::cancel() {
    bool active;
    std::string label;
    {
        std::lock_guard<std::mutex> lock(d_->jobMutex);
        active = d_->jobActive;
        label = d_->running;
    }
    if (!active) return;
    d_->jobCancel = true;
    d_->log(LogLine::Info, "Stopping \"" + label + "\"...");
}

bool Engine::busy(const std::string& label) const { return d_->busy(label); }

bool Engine::panic(const std::vector<std::string>& toggles, const std::vector<std::string>& values) { return d_->panic(toggles, values); }

void Engine::pump() { d_->pump(); }

EngineStatus Engine::status() const { return d_->snapshot(); }

bool Engine::connected() const { return d_->state == EngineStatus::Connected; }

void Engine::log(LogLine::Level level, const std::string& text) { d_->log(level, text); }

void Engine::toast(LogLine::Level level, const std::string& text) { d_->toast(level, text); }

std::vector<LogLine> Engine::logSince(size_t& cursor) const {
    std::lock_guard<std::mutex> lock(d_->logMutex);
    size_t first = std::max(cursor, d_->base);
    size_t end = d_->base + d_->lines.size();
    std::vector<LogLine> out;
    if (first < end) out.assign(d_->lines.begin() + std::ptrdiff_t(first - d_->base), d_->lines.end());
    cursor = end;
    return out;
}

std::vector<Toast> Engine::takeToasts() {
    std::lock_guard<std::mutex> lock(d_->logMutex);
    std::vector<Toast> out;
    out.swap(d_->toasts);
    return out;
}

Settings& Engine::settings() { return d_->settings; }

}   
