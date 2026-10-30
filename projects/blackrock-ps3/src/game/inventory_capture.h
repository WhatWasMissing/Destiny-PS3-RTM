#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace brps3::inventory {

struct CaptureSite {
    int op = 0;
    uint32_t site = 0, word = 0, builder = 0;
    int requestReg = 0;                  
    int soidReg = -1;                    
    const char* name = "";
};
const std::vector<CaptureSite>& captureSites();

inline constexpr uint32_t kCaptureMagic = 0x42524350;    
inline constexpr uint32_t kCaptureRecords = 8, kCaptureRecord = 0x80, kCaptureHeader = 0x10;
inline constexpr uint32_t kCaptureBuffer = kCaptureHeader + kCaptureRecords * kCaptureRecord;

 
std::vector<uint8_t> captureStub(uint32_t base, const CaptureSite& s, uint32_t buffer);
uint32_t branchTarget(uint32_t site, uint32_t word);     

struct CaptureRecord {
    int op = 0;
    uint32_t sequence = 0;
    std::array<uint64_t, 6> r{};         
    uint64_t callback = 0;               
    uint64_t soid = 0;                   
    uint32_t requestPtr = 0;
    std::array<uint8_t, 32> request{};
    std::string describe() const;        
};
 
std::vector<CaptureRecord> parseCaptures(const std::vector<uint8_t>& buffer, uint32_t& seen);
std::vector<uint8_t> emptyCaptureBuffer();

}   
