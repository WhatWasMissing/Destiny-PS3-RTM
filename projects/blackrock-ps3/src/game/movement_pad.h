#pragma once
 
 
 
 
 
 
 
 
 
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace brps3::movement {

 
enum class Btn : int { Up, Down, Left, Right, Start, Select, L3, R3, Cross, Circle, Square, Triangle, L1, R1, L2, R2, Count };

inline constexpr uint32_t kMaskOf[int(Btn::Count)] = {0x1,    0x2,    0x4,    0x8,    0x10,   0x20,   0x40,    0x80,
                                                      0x1000, 0x2000, 0x4000, 0x8000, 0x100, 0x200, 0x10000, 0x20000};
inline constexpr uint32_t kModifiers = 0x100 | 0x200 | 0x10000 | 0x20000;    
inline constexpr uint32_t kAllButtons = 0x3F3FF;
inline uint32_t maskOf(Btn b) { return kMaskOf[int(b)]; }

const char* buttonName(Btn b);                   
 
std::optional<Btn> parseButton(const std::string& name);

 
std::string chordText(uint32_t chord);
 
std::optional<uint32_t> parseChord(const std::string& text);
 
std::vector<Btn> chordButtons(uint32_t chord);

 
bool bindValid(uint32_t chord, std::string* reason = nullptr);
 
inline bool bindEdge(uint32_t chord, uint32_t now, uint32_t before) { return chord && now == chord && (before & chord) != chord; }

 
uint32_t maskFromState(const uint8_t* state, size_t size);    

 
struct PadState {
    bool connected = false;
    uint32_t held = 0;                   
    int16_t lx = 0, ly = 0, rx = 0, ry = 0;    
    uint8_t l2 = 0, r2 = 0;              
};
PadState parsePortState(const uint8_t* state, size_t size, bool connected);

 
 
class RingReader {
public:
    static constexpr uint32_t kSize = 16;
     
     
     
    std::vector<uint32_t> take(uint32_t seq, const std::array<uint32_t, kSize>& ring);
    void reset() { synced_ = false; }
    bool synced() const { return synced_; }
    uint32_t lost() const { return lost_; }
private:
    bool synced_ = false;
    uint32_t seq_ = 0;
    uint32_t lost_ = 0;
};

 
struct Bind {
    std::string item;                    
    uint32_t chord = 0;
    bool operator==(const Bind&) const = default;
};
 
std::string encodeBind(const Bind& b);
std::optional<Bind> decodeBind(const std::string& text);

 
class BindMatcher {
public:
    std::vector<std::string> feed(const std::vector<uint32_t>& masks, const std::vector<Bind>& binds);
    void reset(uint32_t held = 0xFFFFFFFFu) { prev_ = held; }    
    uint32_t previous() const { return prev_; }
private:
    uint32_t prev_ = 0xFFFFFFFFu;
};

 
 
 
class ChordCapture {
public:
    enum class State { Idle, WaitRelease, Collect, Done, Cancelled, Cleared };
    void start() { state_ = State::WaitRelease, chord_ = 0; }
    void stop() { state_ = State::Idle; }
    State state() const { return state_; }
    bool active() const { return state_ == State::WaitRelease || state_ == State::Collect; }
    uint32_t chord() const { return chord_; }
    uint32_t partial() const { return chord_; }    
    void feed(uint32_t mask);
    void feed(const std::vector<uint32_t>& masks) {
        for (uint32_t m : masks) feed(m);
    }
private:
    State state_ = State::Idle;
    uint32_t chord_ = 0;
};

}   
