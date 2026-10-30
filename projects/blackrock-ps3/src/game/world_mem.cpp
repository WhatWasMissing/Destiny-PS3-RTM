#include "game/world_mem.h"

#include <windows.h>

#include <cctype>
#include <cstdio>
#include <mutex>

#include "core/runtime.h"

namespace brps3::world {

bool heap(uint32_t addr, uint32_t size) { return addr >= kHeapBegin && uint64_t(addr) + size <= kHeapEnd; }

bool ebootData(uint32_t addr, uint32_t size) { return addr >= kEbootDataBegin && uint64_t(addr) + size <= kEbootDataEnd; }

bool mapped(uint32_t addr, uint32_t size) {
    return heap(addr, size) || (addr >= kTextBegin && uint64_t(addr) + size <= kEbootDataEnd);
}

 

void Bytes::need(uint32_t off, uint32_t n) const {
    if (!has(off, n))
        throw Error("read past the record at " + hex8(addr_) + " (+" + hex8(off) + ", " + std::to_string(n) + " bytes of " +
                    std::to_string(data_.size()) + ")");
}

uint8_t Bytes::u8(uint32_t off) const {
    need(off, 1);
    return data_[off];
}

uint16_t Bytes::u16(uint32_t off) const {
    need(off, 2);
    return uint16_t(data_[off] << 8 | data_[off + 1]);
}

uint32_t Bytes::u32(uint32_t off) const {
    need(off, 4);
    return uint32_t(data_[off]) << 24 | uint32_t(data_[off + 1]) << 16 | uint32_t(data_[off + 2]) << 8 | data_[off + 3];
}

uint64_t Bytes::u64(uint32_t off) const { return uint64_t(u32(off)) << 32 | u32(off + 4); }

float Bytes::f32(uint32_t off) const { return bitsFloat(u32(off)); }

Vec3 Bytes::vec3(uint32_t off) const { return {f32(off), f32(off + 4), f32(off + 8)}; }

Quat Bytes::quat(uint32_t off) const { return {f32(off), f32(off + 4), f32(off + 8), f32(off + 12)}; }

Bytes Bytes::slice(uint32_t off, uint32_t n) const {
    need(off, n);
    return Bytes(addr_ + off, std::vector<uint8_t>(data_.begin() + off, data_.begin() + off + n));
}

std::string Bytes::cstr(uint32_t off, uint32_t max) const {
    std::string s;
    for (uint32_t i = 0; i < max && has(off + i); ++i) {
        const char c = char(data_[off + i]);
        if (!c) break;
        s += std::isprint(static_cast<unsigned char>(c)) ? c : '?';
    }
    return s;
}

 

std::optional<Bytes> Reader::tryBytes(uint32_t addr, uint32_t size) {
    try {
        return bytes(addr, size);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<uint32_t> Reader::pointer(uint32_t slot, uint32_t minSize) {
    const uint32_t p = u32(slot);
    if (!validHandle(p) || !mapped(p, minSize)) return std::nullopt;
    return p;
}

std::vector<uint8_t> RuntimeReader::read(uint32_t addr, uint32_t size) {
    ++reads_;
    auto b = rt_.read(addr, size);
    if (b.size() != size) throw Error("short read at " + hex8(addr));
    return b;
}

 

std::vector<uint8_t> be32(uint32_t v) { return {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)}; }

uint32_t floatBits(float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    return v;
}

float bitsFloat(uint32_t v) {
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

std::vector<uint8_t> beF32(float v) { return be32(floatBits(v)); }

std::vector<uint8_t> beVec3(const Vec3& v) {
    std::vector<uint8_t> out;
    for (float f : {v.x, v.y, v.z}) {
        auto b = beF32(f);
        out.insert(out.end(), b.begin(), b.end());
    }
    return out;
}

 

namespace {
std::mutex g_dataMutex;
std::filesystem::path g_dataDir;
}   

std::filesystem::path dataDir() {
    std::lock_guard<std::mutex> lock(g_dataMutex);
    if (!g_dataDir.empty()) return g_dataDir;
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, buf, DWORD(std::size(buf)));
    return std::filesystem::path(std::wstring(buf, n)).parent_path() / "data";
}

void setDataDir(const std::filesystem::path& dir) {
    std::lock_guard<std::mutex> lock(g_dataMutex);
    g_dataDir = dir;
}

uint32_t parseHex(const std::string& text, const char* what) {
    std::string t;
    for (char c : text)
        if (!std::isspace(static_cast<unsigned char>(c))) t += c;
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) t = t.substr(2);
    if (t.empty() || t.size() > 8 || t.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        throw Error(std::string("not a ") + what + ": " + text);
    return uint32_t(std::stoul(t, nullptr, 16));
}

std::string hex8(uint32_t v) {
    char b[12];
    std::snprintf(b, sizeof b, "%08X", v);
    return b;
}

}   
