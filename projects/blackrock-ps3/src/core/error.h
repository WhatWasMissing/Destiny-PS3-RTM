#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace brps3 {

 
 
struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

 
 
 
std::filesystem::path state_dir();

std::string hex32(uint32_t v);                  
std::string hexBytes(const void* p, size_t n);  
inline std::string hexBytes(const std::vector<uint8_t>& v) { return hexBytes(v.data(), v.size()); }
std::vector<uint8_t> parseHexBytes(const std::string& s);    

 
 
void setCoreLog(std::function<void(const std::string&)> sink);
void coreLog(const std::string& text);

}   
