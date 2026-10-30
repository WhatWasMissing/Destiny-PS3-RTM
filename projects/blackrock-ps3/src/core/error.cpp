#include "core/error.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <mutex>

namespace brps3 {

namespace {

std::filesystem::path envPath(const wchar_t* name) {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetEnvironmentVariableW(name, buf, DWORD(std::size(buf)));
    if (n == 0 || n >= std::size(buf)) return {};
    return std::filesystem::path(std::wstring(buf, n));
}

std::mutex& logMutex() {
    static std::mutex m;
    return m;
}

std::function<void(const std::string&)>& logSink() {
    static std::function<void(const std::string&)> sink;
    return sink;
}

}   

std::filesystem::path state_dir() {
     
    std::filesystem::path dir = envPath(L"BRPS3_STATE_DIR");
    if (dir.empty()) {
        std::filesystem::path base = envPath(L"LOCALAPPDATA");
        if (base.empty()) base = envPath(L"USERPROFILE") / "AppData" / "Local";
        dir = base / "BlackrockPS3";
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::string hex32(uint32_t v) {
    char buf[12];
    std::snprintf(buf, sizeof buf, "%08X", v);
    return buf;
}

std::string hexBytes(const void* p, size_t n) {
    static const char digits[] = "0123456789ABCDEF";
    const auto* b = static_cast<const uint8_t*>(p);
    std::string out;
    out.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        out.push_back(digits[b[i] >> 4]);
        out.push_back(digits[b[i] & 15]);
    }
    return out;
}

std::vector<uint8_t> parseHexBytes(const std::string& s) {
    std::vector<uint8_t> out;
    int high = -1;
    for (char c : s) {
        int v;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        else if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
        else throw Error("not a hex byte string: " + s);
        if (high < 0) {
            high = v;
        } else {
            out.push_back(uint8_t(high << 4 | v));
            high = -1;
        }
    }
    if (high >= 0) throw Error("odd number of hex digits: " + s);
    return out;
}

void setCoreLog(std::function<void(const std::string&)> sink) {
    std::lock_guard<std::mutex> lock(logMutex());
    logSink() = std::move(sink);
}

void coreLog(const std::string& text) {
    std::function<void(const std::string&)> sink;
    {
        std::lock_guard<std::mutex> lock(logMutex());
        sink = logSink();
    }
    if (sink) {
        sink(text);
    } else {
        OutputDebugStringA(("[brps3] " + text + "\n").c_str());
    }
}

}   
