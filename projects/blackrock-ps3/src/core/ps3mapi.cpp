#include "core/ps3mapi.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <regex>

namespace brps3 {

namespace {

void wsaInit() {
    static std::once_flag once;
    std::call_once(once, [] {
        WSADATA d;
        if (WSAStartup(MAKEWORD(2, 2), &d) != 0) throw Error("Winsock is unavailable");
    });
}

std::string wsaText(const char* what) {
    int code = WSAGetLastError();
    if (code == WSAETIMEDOUT) return std::string("PS3MAPI ") + what + " timed out";
    if (code == WSAECONNRESET || code == WSAECONNABORTED) return std::string("PS3MAPI ") + what + ": connection lost";
    return std::string("PS3MAPI ") + what + " failed (WSA " + std::to_string(code) + ")";
}

SOCKET connectTo(const std::string& host, int port, int timeoutMs) {
    wsaInit();
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res)
        throw Error("cannot resolve " + host);
    std::string failure = "cannot connect to " + host + ":" + std::to_string(port);
    SOCKET s = INVALID_SOCKET;
    for (addrinfo* ai = res; ai && s == INVALID_SOCKET; ai = ai->ai_next) {
        s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == INVALID_SOCKET) continue;
        u_long nb = 1;
        ioctlsocket(s, FIONBIO, &nb);
        bool ok = false;
        if (connect(s, ai->ai_addr, int(ai->ai_addrlen)) == 0) {
            ok = true;
        } else if (WSAGetLastError() == WSAEWOULDBLOCK) {
            fd_set wr, ex;
            FD_ZERO(&wr);
            FD_ZERO(&ex);
            FD_SET(s, &wr);
            FD_SET(s, &ex);
            timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
            int n = select(0, nullptr, &wr, &ex, &tv);
            if (n > 0 && FD_ISSET(s, &wr)) {
                int err = 0;
                int len = sizeof err;
                getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
                ok = err == 0;
                if (!ok) failure += " (error " + std::to_string(err) + ")";
            } else if (n == 0) {
                failure += " (timed out)";
            } else {
                failure += " (refused)";
            }
        }
        if (!ok) {
            closesocket(s);
            s = INVALID_SOCKET;
            continue;
        }
        nb = 0;
        ioctlsocket(s, FIONBIO, &nb);
        DWORD tmo = DWORD(timeoutMs);
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tmo), sizeof tmo);
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tmo), sizeof tmo);
    }
    freeaddrinfo(res);
    if (s == INVALID_SOCKET) throw Error(failure);
    return s;
}

void sendAll(SOCKET s, const void* data, size_t size) {
    const char* p = static_cast<const char*>(data);
    while (size) {
        int n = send(s, p, int(std::min<size_t>(size, 0x10000)), 0);
        if (n == SOCKET_ERROR) throw Error(wsaText("send"));
        p += n;
        size -= size_t(n);
    }
}

bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

 
 
bool waitReadable(SOCKET s, int ms) {
    fd_set rd;
    FD_ZERO(&rd);
    FD_SET(s, &rd);
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    return select(0, &rd, nullptr, nullptr, &tv) > 0;
}

 
struct LateReply : Error {
    using Error::Error;
};

std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

 
 
class OpLock {
public:
    explicit OpLock(void* handle, int timeoutMs = 60000) : h_(static_cast<HANDLE>(handle)) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        for (;;) {
            OVERLAPPED ov{};
            if (LockFileEx(h_, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov)) return;
            if (std::chrono::steady_clock::now() > deadline) throw Error("PS3MAPI operation lock busy");
            Sleep(5);
        }
    }
    ~OpLock() {
        OVERLAPPED ov{};
        UnlockFileEx(h_, 0, 1, 0, &ov);
    }
    OpLock(const OpLock&) = delete;
    OpLock& operator=(const OpLock&) = delete;

private:
    HANDLE h_;
};

struct DataSocket {
    SOCKET s;
    ~DataSocket() {
        if (s != INVALID_SOCKET) closesocket(s);
    }
    void close() {
        if (s != INVALID_SOCKET) closesocket(s);
        s = INVALID_SOCKET;
    }
};

 
 
 
 
 
 

constexpr uintptr_t kClosed = ~uintptr_t(0);

void dropControl(uintptr_t& ctrl, std::string& rbuf) {
    if (ctrl != kClosed) closesocket(SOCKET(ctrl));
    else return;
    ctrl = kClosed;
    rbuf.clear();
}

std::string readLine(uintptr_t& ctrl, std::string& rbuf, int timeoutMs) {
    if (ctrl == kClosed) throw Error("PS3MAPI is not connected");
    for (;;) {
        auto pos = rbuf.find('\n');
        if (pos != std::string::npos) {
            std::string l = rbuf.substr(0, pos);
            rbuf.erase(0, pos + 1);
            while (!l.empty() && (l.back() == '\r' || l.back() == '\n')) l.pop_back();
            return l;
        }
        char buf[2048];
        if (!waitReadable(SOCKET(ctrl), timeoutMs)) throw LateReply("PS3MAPI receive timed out");
        int n = recv(SOCKET(ctrl), buf, int(sizeof buf), 0);
        if (n == 0) {
            if (rbuf.empty()) {
                dropControl(ctrl, rbuf);
                throw Error("PS3MAPI closed the control connection");
            }
            std::string l;
            l.swap(rbuf);
            while (!l.empty() && (l.back() == '\r' || l.back() == '\n')) l.pop_back();
            dropControl(ctrl, rbuf);    
            return l;
        }
        if (n < 0) {
            std::string why = wsaText("receive");    
            dropControl(ctrl, rbuf);
            throw Error(why);
        }
        rbuf.append(buf, size_t(n));
    }
}

 
void openControl(const std::string& host, int port, int timeoutMs, void* oplock, uintptr_t& ctrl, std::string& rbuf) {
    dropControl(ctrl, rbuf);
    ctrl = uintptr_t(connectTo(host, port, timeoutMs));
    try {
        BOOL nodelay = TRUE;
        setsockopt(SOCKET(ctrl), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nodelay), sizeof nodelay);
        OpLock op(oplock);
        for (const char* expected : {"220", "230"}) {
            std::string l = readLine(ctrl, rbuf, timeoutMs);
            if (!startsWith(l, expected)) throw Error("Unexpected PS3MAPI greeting: " + l);
        }
    } catch (...) {
        dropControl(ctrl, rbuf);    
        throw;
    }
}

 
 
 
 
struct Backoff {
    std::chrono::steady_clock::time_point next{};
    int failures = 0;
};
Backoff& backoffFor(const void* key) {
    static std::mutex m;
    static std::map<const void*, Backoff> all;
    std::lock_guard<std::mutex> lock(m);
    return all[key];
}

void reopen(const std::string& host, int port, int timeoutMs, void* oplock, uintptr_t& ctrl, std::string& rbuf) {
    if (ctrl != kClosed) return;
    Backoff& b = backoffFor(&ctrl);
    auto now = std::chrono::steady_clock::now();
    if (now < b.next) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(b.next - now).count();
        throw Error("PS3MAPI is reconnecting (next attempt in " + std::to_string(ms) + " ms)");
    }
    // Back off up to 30 seconds while transfers keep failing; do not spam
    // the console with new control connections every few seconds.
    int delayMs = std::min(30000, 1000 << std::min(b.failures, 5));
    b.next = now + std::chrono::milliseconds(delayMs);
    ++b.failures;
    openControl(host, port, timeoutMs, oplock, ctrl, rbuf);
    coreLog("PS3MAPI: control socket reopened to " + host + ":" + std::to_string(port) +
            " (attempt " + std::to_string(b.failures) + "; awaiting completed transaction)");
}

void markHealthy(const uintptr_t& ctrl) {
    Backoff& b = backoffFor(&ctrl);
    b.failures = 0;
    b.next = {};
}

}   

Ps3Mapi::Ps3Mapi(const std::string& host, int timeoutMs) : host_(host), port_(PORT), timeoutMs_(timeoutMs) {
    auto colon = host.rfind(':');
    if (colon != std::string::npos) {
        host_ = host.substr(0, colon);
        port_ = std::atoi(host.c_str() + colon + 1);
        if (port_ <= 0 || port_ > 0xFFFF) throw Error("bad port in " + host);
    }
    auto lockPath = state_dir() / "ps3mapi.oplock";
    HANDLE h = CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) throw Error("cannot open " + lockPath.string());
    oplock_ = h;
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        openControl(host_, port_, timeoutMs_, oplock_, ctrl_, rbuf_);
    } catch (...) {
        CloseHandle(static_cast<HANDLE>(oplock_));
        oplock_ = nullptr;
        throw;
    }
}

Ps3Mapi::~Ps3Mapi() {
    if (ctrl_ != kClosed) {
        static const char bye[] = "DISCONNECT\r\n";
        send(SOCKET(ctrl_), bye, int(sizeof bye - 1), 0);
        closesocket(SOCKET(ctrl_));
    }
    if (oplock_) CloseHandle(static_cast<HANDLE>(oplock_));
}

std::string Ps3Mapi::line() {
    try {
        return readLine(ctrl_, rbuf_, timeoutMs_);
    } catch (const LateReply&) {
        desync_ = true;    
        throw;
    }
}

void Ps3Mapi::ready() {
     
     
     
     
    if (desync_) {
        dropControl(ctrl_, rbuf_);
        desync_ = false;
    }
    reopen(host_, port_, timeoutMs_, oplock_, ctrl_, rbuf_);
}

std::string Ps3Mapi::command(const std::string& text) {
    if (ctrl_ == kClosed) throw Error("PS3MAPI is not connected");
    std::string out = text + "\r\n";
    try {
        sendAll(SOCKET(ctrl_), out.data(), out.size());
    } catch (...) {
        dropControl(ctrl_, rbuf_);
        throw;
    }
    std::string l = line();
    if (!l.empty() && l[0] != '1' && l[0] != '2') throw Error("'" + text + "' -> " + l);
    return l;
}

std::string Ps3Mapi::value(const std::string& text) {
    std::string l;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ready();
        OpLock op(oplock_);
        l = command(text);
        if (!startsWith(l, "200 ")) throw Error("'" + text + "' -> " + l);
        // A complete control-only transaction is a valid reconnect health signal.
        markHealthy(ctrl_);
    }
    return l.substr(4);
}

std::vector<uint32_t> Ps3Mapi::pids() {
    std::string v = value("PROCESS GETALLPID");
    std::vector<uint32_t> out;
    size_t start = 0;
    while (start <= v.size()) {
        size_t end = v.find('|', start);
        if (end == std::string::npos) end = v.size();
        std::string p = v.substr(start, end - start);
        if (!p.empty()) {
            uint32_t pid;
            try {
                if (startsWith(p, "0x") || startsWith(p, "0X")) pid = uint32_t(std::stoul(p.substr(2), nullptr, 16));
                else pid = uint32_t(std::stoul(p, nullptr, 10));
            } catch (const std::exception&) {
                throw Error("bad PROCESS GETALLPID reply: " + v);
            }
            if (pid) out.push_back(pid);
        }
        start = end + 1;
    }
    return out;
}

std::string Ps3Mapi::processName(uint32_t pid) { return value("PROCESS GETNAME " + std::to_string(pid)); }

std::pair<uint32_t, std::string> Ps3Mapi::findProcess(const std::string& needle) {
    std::vector<std::pair<uint32_t, std::string>> hits, match;
    for (uint32_t p : pids()) hits.emplace_back(p, processName(p));
    for (const auto& h : hits)
        if (lower(h.second).find(lower(needle)) != std::string::npos) match.push_back(h);
    if (match.size() != 1) {
        std::string found;
        for (const auto& h : hits) found += (found.empty() ? "" : ", ") + std::to_string(h.first) + " '" + h.second + "'";
        throw Error("Expected one process matching '" + needle + "'; found [" + found + "]");
    }
    return match[0];
}

uintptr_t Ps3Mapi::pasv() {
    std::string l = command("PASV");
    static const std::regex re(R"(\((\d+),(\d+),(\d+),(\d+),(\d+),(\d+)\))");
    std::smatch m;
    if (!std::regex_search(l, m, re)) {
        desync_ = true;
        throw Error("Bad PASV reply: " + l);
    }
    int port = std::stoi(m[5].str()) * 256 + std::stoi(m[6].str());
    return connectTo(host_, port, timeoutMs_);    
}

std::vector<uint8_t> Ps3Mapi::read(uint32_t pid, uint32_t addr, uint32_t size) {
    std::vector<uint8_t> out;
    out.reserve(size);
    char cmd[96];
    while (size) {
        uint32_t n = std::min(size, CHUNK);
        size_t before = out.size();
        std::string done;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ready();
            OpLock op(oplock_);
            DataSocket data{SOCKET(pasv())};
            std::snprintf(cmd, sizeof cmd, "MEMORY GET %u %X %u", pid, addr, n);
            std::string l = command(cmd);
            if (!startsWith(l, "150")) {
                if (startsWith(l, "2")) desync_ = true;
                throw Error("MEMORY GET " + hex32(addr) + ": " + l);
            }
            out.resize(before + n);
            size_t got = 0;
            while (got < n) {
                if (!waitReadable(data.s, timeoutMs_)) {
                    desync_ = true;    
                    throw Error("PS3MAPI data receive timed out");
                }
                int r = recv(data.s, reinterpret_cast<char*>(out.data() + before + got), int(n - got), 0);
                if (r == 0) break;
                if (r < 0) {
                    std::string why = wsaText("data receive");
                    desync_ = true;
                    throw Error(why);
                }
                got += size_t(r);
            }
            out.resize(before + got);
            data.close();
            done = line();
            // 150/PASV acknowledge only the start of a transfer, not its success.
            // Don't reset reconnect backoff until the binary data and final 226 arrive.
            if (startsWith(done, "226") && got == n) markHealthy(ctrl_);
        }
        size_t got = out.size() - before;
        if (!startsWith(done, "226") || got != n) {
            if (!startsWith(done, "226") && !startsWith(done, "4") && !startsWith(done, "5")) desync_ = true;
            std::snprintf(cmd, sizeof cmd, "MEMORY GET %X/%u: ", addr, n);
            throw Error(cmd + done + " (" + std::to_string(got) + " bytes)");
        }
        addr += n;
        size -= n;
    }
    return out;
}

void Ps3Mapi::write(uint32_t pid, uint32_t addr, const void* data, size_t size) {
    if (!size || size > CHUNK) throw Error("write size must be 1..CHUNK bytes");
    char cmd[96];
    std::string done;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ready();
        OpLock op(oplock_);
        DataSocket sock{SOCKET(pasv())};
        std::snprintf(cmd, sizeof cmd, "MEMORY SET %u %X", pid, addr);
        std::string l = command(cmd);
        if (!startsWith(l, "150")) {
            if (startsWith(l, "2")) desync_ = true;
            throw Error("MEMORY SET " + hex32(addr) + ": " + l);
        }
        try {
            sendAll(sock.s, data, size);
        } catch (...) {
            desync_ = true;    
            throw;
        }
        sock.close();
        done = line();
        if (startsWith(done, "226")) markHealthy(ctrl_);
    }
    if (!startsWith(done, "226")) {
        std::snprintf(cmd, sizeof cmd, "MEMORY SET %X/%zu: ", addr, size);
        throw Error(cmd + done);
    }
}

}   
