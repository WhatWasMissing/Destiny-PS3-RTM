#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <vector>

namespace brps3 {
class Runtime;
}

namespace brps3::player::aura {

inline constexpr uint32_t kBlockSize = 0x10;
inline constexpr uint32_t kStubSize = 0x30;

std::vector<uint8_t> stub(uint32_t base, uint32_t block, uint32_t site, uint32_t original);

class KillAura {
public:
    void enable(Runtime& rt, float value);    
    void setValue(Runtime& rt, float value);  
    void disable(Runtime& rt);                
    bool active(const Runtime& rt) const;
    uint32_t block() const { return block_; }
private:
    uint32_t block_ = 0;
};

}   
