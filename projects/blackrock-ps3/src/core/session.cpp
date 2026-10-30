#include "core/session.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>
#include <variant>

#include "core/profile.h"

namespace brps3 {

namespace {

std::string pyHex(uint32_t v) {    
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%x", v);
    return buf;
}

std::string lowerHex(const std::vector<uint8_t>& b) {
    std::string s = hexBytes(b);
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

void sleepMs(int ms) {
    if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

 
struct Json;
using JsonArray = std::vector<Json>;
using JsonObject = std::vector<std::pair<std::string, Json>>;
struct Json {
    std::variant<std::nullptr_t, bool, int64_t, double, std::string, JsonArray, JsonObject> v;
    const Json& at(const std::string& key) const {
        const auto* o = std::get_if<JsonObject>(&v);
        if (!o) throw Error("journal: expected an object");
        for (const auto& [k, val] : *o)
            if (k == key) return val;
        throw Error("journal: missing \"" + key + "\"");
    }
    bool has(const std::string& key) const {
        const auto* o = std::get_if<JsonObject>(&v);
        if (!o) return false;
        for (const auto& kv : *o)
            if (kv.first == key) return true;
        return false;
    }
    const std::string& str() const {
        if (const auto* s = std::get_if<std::string>(&v)) return *s;
        throw Error("journal: expected a string");
    }
    int64_t integer() const {
        if (const auto* i = std::get_if<int64_t>(&v)) return *i;
        throw Error("journal: expected an integer");
    }
    const JsonArray& array() const {
        if (const auto* a = std::get_if<JsonArray>(&v)) return *a;
        throw Error("journal: expected an array");
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& s) : s_(s) {}
    Json document() {
        Json j = value();
        ws();
        if (i_ != s_.size()) fail("trailing data");
        return j;
    }

private:
    [[noreturn]] void fail(const char* what) { throw Error(std::string("journal: ") + what + " at offset " + std::to_string(i_)); }
    void ws() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\r' || s_[i_] == '\n')) ++i_;
    }
    bool eat(char c) {
        ws();
        if (i_ < s_.size() && s_[i_] == c) {
            ++i_;
            return true;
        }
        return false;
    }
    void expect(char c) {
        if (!eat(c)) fail("unexpected character");
    }
    Json value() {
        ws();
        if (i_ >= s_.size()) fail("unexpected end");
        char c = s_[i_];
        if (c == '{') {
            ++i_;
            JsonObject o;
            if (eat('}')) return {o};
            do {
                ws();
                std::string k = string();
                expect(':');
                o.emplace_back(std::move(k), value());
            } while (eat(','));
            expect('}');
            return {o};
        }
        if (c == '[') {
            ++i_;
            JsonArray a;
            if (eat(']')) return {a};
            do a.push_back(value());
            while (eat(','));
            expect(']');
            return {a};
        }
        if (c == '"') return {string()};
        if (s_.compare(i_, 4, "true") == 0) { i_ += 4; return {true}; }
        if (s_.compare(i_, 5, "false") == 0) { i_ += 5; return {false}; }
        if (s_.compare(i_, 4, "null") == 0) { i_ += 4; return {nullptr}; }
        size_t start = i_;
        bool real = false;
        while (i_ < s_.size() && std::strchr("+-0123456789.eE", s_[i_])) {
            if (std::strchr(".eE", s_[i_])) real = true;
            ++i_;
        }
        if (start == i_) fail("unexpected character");
        std::string num = s_.substr(start, i_ - start);
        try {
            if (real) return {std::stod(num)};
            return {int64_t(std::stoll(num))};
        } catch (const std::exception&) {
            fail("bad number");
        }
    }
    std::string string() {
        if (i_ >= s_.size() || s_[i_] != '"') fail("expected a string");
        ++i_;
        std::string out;
        while (i_ < s_.size() && s_[i_] != '"') {
            char c = s_[i_++];
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (i_ >= s_.size()) fail("bad escape");
            char e = s_[i_++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (i_ + 4 > s_.size()) fail("bad escape");
                    unsigned cp = unsigned(std::stoul(s_.substr(i_, 4), nullptr, 16));
                    i_ += 4;
                    if (cp < 0x80) {
                        out.push_back(char(cp));
                    } else if (cp < 0x800) {
                        out.push_back(char(0xC0 | (cp >> 6)));
                        out.push_back(char(0x80 | (cp & 0x3F)));
                    } else {
                        out.push_back(char(0xE0 | (cp >> 12)));
                        out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
                        out.push_back(char(0x80 | (cp & 0x3F)));
                    }
                    break;
                }
                default: fail("bad escape");
            }
        }
        if (i_ >= s_.size()) fail("unterminated string");
        ++i_;
        return out;
    }
    const std::string& s_;
    size_t i_ = 0;
};

std::string jsonString(const std::string& s) {    
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out + "\"";
}

std::string readFile(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw Error("cannot read " + p.string());
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void writeFileAtomic(const std::filesystem::path& p, const std::string& bytes) {
    auto tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write(bytes.data(), std::streamsize(bytes.size()));
        f.flush();
        if (!f) throw Error("cannot write " + tmp.string());
    }
    if (!MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw Error("cannot replace " + p.string());
}

}   

 

std::filesystem::path Journal::path() { return state_dir() / "pending.json"; }

std::string Journal::text() const {
     
    const char* nl = "\r\n";
    std::string o = "{";
    o += nl;
    o += " \"pid\": " + std::to_string(pid) + "," + nl;
    o += " \"build\": " + jsonString(build) + "," + nl;
    o += " \"patches\": [";
    if (patches.empty()) {
        o += "],";
    } else {
        o += nl;
        for (size_t i = 0; i < patches.size(); ++i) {
            const auto& p = patches[i];
            o += "  {" + std::string(nl);
            o += "   \"address\": " + jsonString(hex32(p.address)) + "," + nl;
            o += "   \"original\": " + jsonString(lowerHex(p.original)) + "," + nl;
            o += "   \"written\": " + jsonString(lowerHex(p.written)) + nl;
            o += std::string("  }") + (i + 1 < patches.size() ? "," : "") + nl;
        }
        o += " ],";
    }
    o += nl;
    o += " \"scratch\": [";
    if (scratch.empty()) {
        o += "]";
    } else {
        o += nl;
        for (size_t i = 0; i < scratch.size(); ++i) {
            const auto& r = scratch[i];
            o += "  {" + std::string(nl);
            o += "   \"address\": " + jsonString(hex32(r.address)) + "," + nl;
            o += "   \"size\": " + std::to_string(r.size) + nl;
            o += std::string("  }") + (i + 1 < scratch.size() ? "," : "") + nl;
        }
        o += " ]";
    }
    o += nl;
    o += "}";
    o += nl;
    return o;
}

Journal Journal::parse(const std::string& text) {
    Json doc = JsonParser(text).document();
    auto addr = [](const Json& j) {
        const std::string& s = j.str();
        try {
            size_t used = 0;
            unsigned long v = std::stoul(s, &used, 16);
            if (used != s.size() || v > 0xFFFFFFFFul) throw Error("");
            return uint32_t(v);
        } catch (const std::exception&) {
            throw Error("journal: bad address " + s);
        }
    };
    Journal j;
    j.pid = uint32_t(doc.at("pid").integer());
    if (doc.has("build")) j.build = doc.at("build").str();
    for (const auto& p : doc.at("patches").array())
        j.patches.push_back({addr(p.at("address")), parseHexBytes(p.at("original").str()), parseHexBytes(p.at("written").str())});
    if (doc.has("scratch"))
        for (const auto& r : doc.at("scratch").array()) j.scratch.push_back({addr(r.at("address")), uint32_t(r.at("size").integer())});
    return j;
}

 

namespace {

HANDLE acquireLock() {
    auto path = state_dir() / "session.lock";
    for (int attempt = 0; attempt < 5; ++attempt) {
         
         
         
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE | DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_NEW,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (h != INVALID_HANDLE_VALUE) return h;
        DWORD err = GetLastError();
        if (err == ERROR_FILE_EXISTS || err == ERROR_ALREADY_EXISTS) {
            if (DeleteFileW(path.c_str())) continue;            
            if (GetLastError() == ERROR_FILE_NOT_FOUND) continue;
            throw Error("Another Blackrock tool holds " + path.string() + " (close it first)");
        }
        if (err == ERROR_ACCESS_DENIED) {                      
            Sleep(50);
            continue;
        }
        throw Error("cannot create " + path.string() + " (error " + std::to_string(err) + ")");
    }
    throw Error("cannot take " + path.string());
}

}   

Session::Session(const std::string& host) : Session(host, false) {}

Session::Session(const std::string& host, bool allowJournal) {
    lock_ = acquireLock();
    try {
        if (!allowJournal && journalPending())
            throw Error("Pending recovery journal " + Journal::path().string() + "; run Recover first");
        mapi_ = std::make_unique<Ps3Mapi>(host);
        auto [pid, name] = mapi_->findProcess(profile::PROCESS);
        pid_ = pid;
        process_ = name;
    } catch (...) {
        mapi_.reset();
        releaseLock();
        throw;
    }
}

Session::~Session() {
    try {
        bool owned;
        {
            std::lock_guard<std::recursive_mutex> lock(mutex_);
            owned = !patches_.empty() || !scratch_.empty();
        }
        if (owned) restoreAll();
    } catch (const std::exception& e) {
        coreLog(std::string("restore on close failed (journal kept; use Recover): ") + e.what());
    }
    mapi_.reset();
    releaseLock();
}

void Session::releaseLock() {
    if (!lock_) return;
    CloseHandle(static_cast<HANDLE>(lock_));
    lock_ = nullptr;
    DeleteFileW((state_dir() / "session.lock").c_str());    
}

std::vector<uint8_t> Session::read(uint32_t addr, uint32_t size) { return mapi_->read(pid_, addr, size); }

uint32_t Session::u32(uint32_t addr) {
    auto b = read(addr, 4);
    return uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 | b[3];
}

void Session::write(uint32_t addr, const void* data, size_t size, bool verify) {
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t off = 0; off < size; off += Ps3Mapi::CHUNK)
        mapi_->write(pid_, addr + uint32_t(off), p + off, std::min<size_t>(Ps3Mapi::CHUNK, size - off));
    if (verify) {
        auto back = read(addr, uint32_t(size));
        if (back.size() != size || std::memcmp(back.data(), p, size) != 0)
            throw Error("write verification failed at " + pyHex(addr));
    }
}

void Session::writeU32(uint32_t addr, uint32_t value, bool verify) {
    uint8_t b[4] = {uint8_t(value >> 24), uint8_t(value >> 16), uint8_t(value >> 8), uint8_t(value)};
    write(addr, b, 4, verify);
}

void Session::verifyBuild() {
    std::string bad;
    for (const auto& [addr, want] : profile::FINGERPRINTS) {
        auto expected = parseHexBytes(want);
        auto got = read(addr, uint32_t(expected.size()));
        if (got != expected) bad += (bad.empty() ? "" : ", ") + std::string("'") + hex32(addr) + "': '" + hexBytes(got) + "'";
    }
    if (!bad.empty())
        throw Error(std::string("Game code differs from build ") + profile::BUILD + " (or another tool owns a site): {" + bad + "}");
}

void Session::requireFree(uint32_t addr, uint32_t size) {
    auto b = read(addr, size);
    if (std::any_of(b.begin(), b.end(), [](uint8_t v) { return v != 0; }))
        throw Error(pyHex(addr) + "+" + pyHex(size) + " is not empty; run recovery or close other tools");
}

void Session::saveJournal() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    patchCount_ = patches_.size();
    auto path = Journal::path();
    if (patches_.empty() && scratch_.empty()) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        return;
    }
    Journal j;
    j.pid = pid_;
    j.build = profile::BUILD;
    for (const auto& p : patches_) j.patches.push_back({p.addr, p.original, p.written});
    for (const auto& s : scratch_) j.scratch.push_back({s.addr, s.size});
    writeFileAtomic(path, j.text());
}

void Session::patch(uint32_t addr, const std::vector<uint8_t>& bytes) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto original = read(addr, uint32_t(bytes.size()));
    patches_.push_back({addr, original, bytes});
    saveJournal();
    write(addr, bytes.data(), bytes.size(), true);
}

void Session::claimScratch(uint32_t addr, uint32_t size) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    requireFree(addr, size);
    scratch_.push_back({addr, size});
    saveJournal();
}

void Session::zero(uint32_t addr, uint32_t size) {
    std::vector<uint8_t> zeros(0x8000);
    for (uint32_t off = 0; off < size; off += 0x8000) mapi_->write(pid_, addr + off, zeros.data(), std::min<uint32_t>(0x8000, size - off));
}

void Session::restore(uint32_t addr) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (size_t i = patches_.size(); i-- > 0;) {
        const auto& p = patches_[i];
        if (p.addr != addr) continue;
        auto current = read(p.addr, uint32_t(p.original.size()));
        if (current != p.written && current != p.original) throw Error(pyHex(p.addr) + " changed by someone else; journal kept");
        write(p.addr, p.original.data(), p.original.size(), true);
        patches_.erase(patches_.begin() + std::ptrdiff_t(i));
        saveJournal();
        return;
    }
    throw Error("no patch of ours at " + pyHex(addr));
}

namespace {

 
 
 
template <class P>
std::vector<size_t> restoreOrder(const std::vector<P>& patches) {
    std::vector<size_t> order(patches.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        bool la = patches[a].original.size() != 4, lb = patches[b].original.size() != 4;
        if (la != lb) return !la;
        return a > b;
    });
    return order;
}

 
 
 
template <class P, class Addr>
bool unwoundBelow(const std::vector<P>& patches, size_t i, const std::vector<uint8_t>& current, Addr addrOf) {
    for (size_t k = 0; k < i; ++k)
        if (addrOf(patches[k]) == addrOf(patches[i]) && patches[k].original.size() == patches[i].original.size() &&
            patches[k].original == current)
            return true;
    return false;
}

}   

void Session::restoreAll(int settleMs) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto addrOf = [](const Patch& p) { return p.addr; };
    bool settled = false;
    for (size_t i : restoreOrder(patches_)) {
        const auto& p = patches_[i];
        if (p.original.size() != 4 && !settled) {
            sleepMs(settleMs);
            settled = true;
        }
        auto current = read(p.addr, uint32_t(p.original.size()));
        if (current != p.written && current != p.original) {
            if (unwoundBelow(patches_, i, current, addrOf)) continue;
            throw Error(pyHex(p.addr) + " changed by someone else; journal kept");
        }
        write(p.addr, p.original.data(), p.original.size(), true);
    }
    patches_.clear();
    patchCount_ = 0;
    if (!scratch_.empty() && !settled) sleepMs(settleMs);
    for (const auto& s : scratch_) zero(s.addr, s.size);
    scratch_.clear();
    std::error_code ec;
    std::filesystem::remove(Journal::path(), ec);
}

bool Session::journalPending() {
    std::error_code ec;
    return std::filesystem::exists(Journal::path(), ec);
}

std::vector<std::string> Session::recover(const std::string& host) {
     
     
    Session s(host, true);
    Journal j = Journal::parse(readFile(Journal::path()));
    std::vector<std::string> restored;
     
     
    auto addrOf = [](const Journal::Patch& p) { return p.address; };
    for (size_t i : restoreOrder(j.patches)) {
        const auto& p = j.patches[i];
        auto cur = s.read(p.address, uint32_t(p.original.size()));
        if (cur == p.original) continue;
        if (cur != p.written) {
            if (unwoundBelow(j.patches, i, cur, addrOf)) continue;
            throw Error(pyHex(p.address) + " holds unknown bytes; not touching it");
        }
        if (p.original.size() != 4) sleepMs(500);
        s.write(p.address, p.original.data(), p.original.size(), true);
        restored.push_back(hex32(p.address));
    }
    if (!j.scratch.empty()) sleepMs(500);
    for (const auto& r : j.scratch) {
        s.zero(r.address, r.size);
        restored.push_back(hex32(r.address) + "+" + pyHex(r.size) + " zeroed");
    }
    std::error_code ec;
    std::filesystem::remove(Journal::path(), ec);
    return restored;
}

}   
