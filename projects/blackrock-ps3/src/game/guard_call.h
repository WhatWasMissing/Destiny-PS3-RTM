#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace brps3 {
class Runtime;
}

namespace brps3::guard {

inline constexpr uint64_t kMinus1 = 0xFFFFFFFFFFFFFFFFull;
inline constexpr uint32_t kMagic = 0x42524731;            
inline constexpr const char* kStubName = "guard.v1";        
inline constexpr uint32_t kStubSize = 0x300;                
inline constexpr size_t kMaxConds = 32;
inline constexpr uint32_t kMaxScratch = 0x400;
inline constexpr size_t kMaxDerefs = 3;

enum class Width : uint8_t { U8 = 1, U16 = 2, U32 = 4 };
enum class Cmp : uint8_t { Eq = 0, Ne = 1, LtU = 2, GeU = 3, LeU = 4, GtU = 5 };
const char* cmpText(Cmp c);                               
uint32_t acceptBits(Cmp c);                               

 
 
 
struct Cond {
    bool entryBased = false;
    uint32_t addr = 0;
    std::vector<uint32_t> derefs;
    Width width = Width::U32;
    uint32_t mask = 0xFFFFFFFFu;
    Cmp cmp = Cmp::Eq;
    uint32_t value = 0;
    std::string what;                    

     
    static Cond abs(uint32_t address, Cmp c, uint32_t value, Width w = Width::U32, uint32_t mask = 0xFFFFFFFFu, std::string what = {});
     
    static Cond entry(uint32_t offset, Cmp c, uint32_t value, Width w = Width::U32, uint32_t mask = 0xFFFFFFFFu, std::string what = {});
     
     
    static Cond ptr(uint32_t slot, std::vector<uint32_t> offsets, Cmp c, uint32_t value, std::string what = {}, Width w = Width::U32,
                    uint32_t mask = 0xFFFFFFFFu);
    std::string describe() const;        
};

 
 
struct HandleCheck {
    uint32_t pool = 0;                   
    uint32_t handle = 0;
    uint32_t identityOffset = 0xC;       
    uint32_t indexMask = 0x1FFF;         
};

struct Request {
    uint32_t native = 0;                 
    std::vector<uint64_t> args;          
    std::vector<double> floats;          
    std::vector<int> scratchArgs;        
    std::optional<HandleCheck> handle;
    std::vector<Cond> conds;             
    std::vector<uint8_t> scratch;        

    Request() = default;
    explicit Request(uint32_t nativeAddr) : native(nativeAddr) {}
    Request& arg(uint64_t v) {
        args.push_back(v);
        return *this;
    }
    Request& scratchArg(uint32_t offset) {    
        scratchArgs.push_back(int(args.size()));
        args.push_back(offset);
        return *this;
    }
    Request& farg(double v) {
        floats.push_back(v);
        return *this;
    }
    Request& require(Cond c) {
        conds.push_back(std::move(c));
        return *this;
    }
    Request& check(HandleCheck h) {
        handle = h;
        return *this;
    }
     
    Request& sobject(uint32_t h);
};

enum class Status : uint32_t { Pending = 0, Called = 1, Refused = 2, BadBlock = 3, Disarmed = 4 };
const char* statusName(Status s);        
 
inline constexpr uint32_t kFailPool = 0x100, kFailIndex = 0x101, kFailEntry = 0x102, kFailIdentity = 0x103;
inline constexpr uint32_t kNoFailure = 0xFFFFFFFFu;

struct Result {
    Status status = Status::Pending;
    bool called = false;
    uint32_t failed = kNoFailure;        
    std::string reason;                  
    uint64_t r3 = 0;                     
    double f1 = 0;
    uint32_t entry = 0;                  
    std::vector<uint8_t> scratch;        
    uint32_t scratchU32(uint32_t off) const;    
};

 
 
 
 
Result call(Runtime& rt, const Request& r, int timeoutMs = 3000, bool readBack = false);
 
std::vector<Result> callMany(Runtime& rt, const std::vector<Request>& rs, int timeoutMs = 3000, bool readBack = false);
 
uint32_t installStub(Runtime& rt);

 
 
class GuardTask {
public:
     
     
    void start(Runtime& rt, const std::string& name, const Request& r, bool once = false);
     
     
    void update(Runtime& rt, const Request& r);
    void stop(Runtime& rt);                         
    bool running(Runtime& rt) const;
    struct Stats {
        Status status = Status::Pending;            
        uint32_t failed = kNoFailure, calls = 0, entry = 0;
        bool armed = false;
        std::string reason;                         
    };
    Stats stats(Runtime& rt) const;                 
    const std::string& name() const { return name_; }
    uint32_t block() const { return block_; }
private:
    std::string name_;
    uint32_t block_ = 0, size_ = 0;
    bool once_ = false;
    Request req_;
};

 
std::vector<uint8_t> stubProgram(uint32_t base);    
uint32_t stubAllocSize();                           
inline constexpr uint32_t kHeaderSize = 0xA0, kCondOffset = 0xA0, kCondSize = 0x20;
inline constexpr uint32_t kOffFlags = 0x04, kOffStatus = 0x08, kOffFailed = 0x0C, kOffNative = 0x10, kOffToc = 0x14,
                          kOffCondCount = 0x18, kOffCalls = 0x1C, kOffPool = 0x20, kOffHandle = 0x24, kOffIdentity = 0x28,
                          kOffMask = 0x2A, kOffEntry = 0x2C, kOffR3 = 0x30, kOffF1 = 0x38, kOffArgs = 0x40, kOffFloats = 0x80;
inline constexpr uint32_t kFlagHandle = 1, kFlagArmed = 2, kFlagOnce = 4;
uint32_t blockSize(const Request& r);               
uint32_t scratchOffset(const Request& r);           
std::vector<uint8_t> encodeBlock(const Request& r, uint32_t blockAddr, bool armed = true, bool once = false);
void validate(const Request& r);                    
Result decodeR3(uint64_t r3, const Request& r);      
void decodeBlock(const std::vector<uint8_t>& block, const Request& r, Result& out);
std::string describeFailure(const Request& r, uint32_t failed);

}   
