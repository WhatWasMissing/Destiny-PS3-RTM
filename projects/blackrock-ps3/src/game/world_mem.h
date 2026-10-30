#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/error.h"

namespace brps3 {
class Runtime;
}

namespace brps3::world {

struct Vec3 {
    float x = 0, y = 0, z = 0;
    bool finite() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
    float length() const { return std::sqrt(x * x + y * y + z * z); }
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    bool operator==(const Vec3&) const = default;
};
struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
    bool operator==(const Quat&) const = default;
};
inline float distance(const Vec3& a, const Vec3& b) { return (a - b).length(); }

 
inline constexpr uint32_t kEbootDataBegin = 0x1840000, kEbootDataEnd = 0x1C80000;    
inline constexpr uint32_t kHeapBegin = 0x30000000, kHeapEnd = 0x80000000;
inline constexpr uint32_t kTextBegin = 0x10000, kTextEnd = 0x183D178;                
bool heap(uint32_t addr, uint32_t size = 1);         
bool ebootData(uint32_t addr, uint32_t size = 1);    
bool mapped(uint32_t addr, uint32_t size = 1);       
inline bool validHandle(uint32_t h) { return h != 0 && h != 0xFFFFFFFFu; }

 
class Bytes {
public:
    Bytes() = default;
    Bytes(uint32_t addr, std::vector<uint8_t> data) : addr_(addr), data_(std::move(data)) {}
    uint32_t addr() const { return addr_; }
    size_t size() const { return data_.size(); }
    bool has(uint32_t off, uint32_t n = 1) const { return uint64_t(off) + n <= data_.size(); }
    const std::vector<uint8_t>& data() const { return data_; }
    uint8_t u8(uint32_t off) const;
    uint16_t u16(uint32_t off) const;
    uint32_t u32(uint32_t off) const;
    uint64_t u64(uint32_t off) const;
    int8_t s8(uint32_t off) const { return int8_t(u8(off)); }
    float f32(uint32_t off) const;
    Vec3 vec3(uint32_t off) const;
    Quat quat(uint32_t off) const;
    Bytes slice(uint32_t off, uint32_t n) const;
    std::string cstr(uint32_t off, uint32_t max) const;    
private:
    void need(uint32_t off, uint32_t n) const;
    uint32_t addr_ = 0;
    std::vector<uint8_t> data_;
};

 
class Reader {
public:
    virtual ~Reader() = default;
    virtual std::vector<uint8_t> read(uint32_t addr, uint32_t size) = 0;    
    Bytes bytes(uint32_t addr, uint32_t size) { return Bytes(addr, read(addr, size)); }
    std::optional<Bytes> tryBytes(uint32_t addr, uint32_t size);            
    uint32_t u32(uint32_t addr) { return bytes(addr, 4).u32(0); }
    float f32(uint32_t addr) { return bytes(addr, 4).f32(0); }
     
    std::optional<uint32_t> pointer(uint32_t slot, uint32_t minSize = 4);
    uint64_t reads() const { return reads_; }                               
protected:
    uint64_t reads_ = 0;
};

class RuntimeReader final : public Reader {
public:
    explicit RuntimeReader(Runtime& rt) : rt_(rt) {}
    std::vector<uint8_t> read(uint32_t addr, uint32_t size) override;
private:
    Runtime& rt_;
};

 
std::vector<uint8_t> be32(uint32_t v);
std::vector<uint8_t> beF32(float v);
std::vector<uint8_t> beVec3(const Vec3& v);
uint32_t floatBits(float f);
float bitsFloat(uint32_t v);

 
 
std::filesystem::path dataDir();
void setDataDir(const std::filesystem::path& dir);
inline std::filesystem::path dataFile(const std::string& relative) { return dataDir() / std::filesystem::path(relative); }

 
uint32_t parseHex(const std::string& text, const char* what = "hex value");
std::string hex8(uint32_t v);                        

}   
