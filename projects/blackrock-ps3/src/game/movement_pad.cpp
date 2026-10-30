#include "game/movement_pad.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "game/movement_addr.h"

namespace brps3::movement {

namespace {

constexpr const char* kNames[int(Btn::Count)] = {"Up",     "Down",   "Left",   "Right",    "Start", "Select", "L3", "R3",
                                                  "Cross", "Circle", "Square", "Triangle", "L1",    "R1",     "L2", "R2"};

 
constexpr Btn kDisplay[] = {Btn::L2,    Btn::R2,   Btn::L1, Btn::R1,   Btn::Cross, Btn::Circle, Btn::Square, Btn::Triangle,
                            Btn::Up,    Btn::Down, Btn::Left, Btn::Right, Btn::L3,  Btn::R3,     Btn::Select, Btn::Start};

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}   

const char* buttonName(Btn b) { return int(b) >= 0 && b < Btn::Count ? kNames[int(b)] : "?"; }

std::optional<Btn> parseButton(const std::string& name) {
    const std::string n = lower(name);
    for (int i = 0; i < int(Btn::Count); ++i)
        if (n == lower(kNames[i])) return Btn(i);
    struct Alias {
        const char* name;
        Btn b;
    };
    static const Alias aliases[] = {{"a", Btn::Cross},      {"b", Btn::Circle}, {"x", Btn::Square},  {"y", Btn::Triangle},
                                    {"lb", Btn::L1},        {"rb", Btn::R1},    {"lt", Btn::L2},     {"rt", Btn::R2},
                                    {"ls", Btn::L3},        {"rs", Btn::R3},    {"back", Btn::Select}, {"dpad up", Btn::Up},
                                    {"dpad down", Btn::Down}, {"dpad left", Btn::Left}, {"dpad right", Btn::Right},
                                    {"triangle", Btn::Triangle}, {"cross", Btn::Cross}};
    for (const Alias& a : aliases)
        if (n == a.name) return a.b;
    return std::nullopt;
}

std::vector<Btn> chordButtons(uint32_t chord) {
    std::vector<Btn> out;
    for (Btn b : kDisplay)
        if (chord & maskOf(b)) out.push_back(b);
    return out;
}

std::string chordText(uint32_t chord) {
    std::string s;
    for (Btn b : chordButtons(chord)) {
        if (!s.empty()) s += "+";
        s += buttonName(b);
    }
    return s;
}

std::optional<uint32_t> parseChord(const std::string& text) {
    uint32_t chord = 0;
    std::string cur;
    auto flush = [&]() -> bool {
        std::string t;
        for (char c : cur)
            if (c != ' ' && c != '\t') t += c;
        cur.clear();
        if (t.empty()) return true;
        auto b = parseButton(t);
        if (!b) return false;
        chord |= maskOf(*b);
        return true;
    };
    for (char c : text) {
        if (c == '+') {
            if (!flush()) return std::nullopt;
        } else {
            cur += c;
        }
    }
    if (!flush()) return std::nullopt;
    return chord;
}

bool bindValid(uint32_t chord, std::string* reason) {
    auto why = [&](const char* r) {
        if (reason) *reason = r;
        return false;
    };
    if (!chord) return why("No buttons.");
    if (chord & ~kAllButtons) return why("Unknown buttons in the chord.");
    if (!(chord & kModifiers)) return why("Hold L1, R1, L2 or R2 with it, so normal play never triggers it.");
    if (!(chord & ~kModifiers)) return why("Add a button that is not L1, R1, L2 or R2.");
    if (reason) reason->clear();
    return true;
}

uint32_t maskFromState(const uint8_t* state, size_t size) {
    if (!state || size < pad::READ) return 0;
    uint32_t mask = 0;
    for (int i = 0; i < pad::BUTTONS; ++i)
        if (state[pad::BUTTON_COUNTER + i]) mask |= kMaskOf[i];
    if (state[pad::TRIGGER_COUNTER + 0]) mask |= maskOf(Btn::L2);
    if (state[pad::TRIGGER_COUNTER + 1]) mask |= maskOf(Btn::R2);
    return mask;
}

PadState parsePortState(const uint8_t* state, size_t size, bool connected) {
    PadState p;
    p.connected = connected;
    if (!state || size < pad::READ) return p;
    p.held = maskFromState(state, size);
    auto s16 = [&](uint32_t off) { return int16_t(uint16_t(state[off] << 8 | state[off + 1])); };
    p.lx = s16(pad::STICK_LX);
    p.ly = s16(pad::STICK_LY);
    p.rx = s16(pad::STICK_RX);
    p.ry = s16(pad::STICK_RY);
    p.l2 = state[pad::TRIGGER_L2];
    p.r2 = state[pad::TRIGGER_R2];
    return p;
}

 

std::vector<uint32_t> RingReader::take(uint32_t seq, const std::array<uint32_t, kSize>& ring) {
    std::vector<uint32_t> out;
    if (!synced_) {
        synced_ = true;
        seq_ = seq;
        return out;
    }
    uint32_t pending = seq - seq_;               
    if (pending > 0x40000000u) {                 
        seq_ = seq;
        return out;
    }
    uint32_t first = seq_;
    if (pending > kSize) {
        lost_ += pending - kSize;
        first = seq - kSize;
        out.push_back(0xFFFFFFFFu);              
    }
    for (uint32_t k = first; k != seq; ++k) out.push_back(ring[k % kSize]);
    seq_ = seq;
    return out;
}

 

std::string encodeBind(const Bind& b) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%X", b.chord);
    return b.item + "=" + buf;
}

std::optional<Bind> decodeBind(const std::string& text) {
    const size_t eq = text.rfind('=');
    if (eq == std::string::npos || eq == 0 || eq + 1 >= text.size()) return std::nullopt;
    Bind b;
    b.item = text.substr(0, eq);
    try {
        size_t used = 0;
        const unsigned long v = std::stoul(text.substr(eq + 1), &used, 16);
        if (used != text.size() - eq - 1) return std::nullopt;
        b.chord = uint32_t(v);
    } catch (...) {
        return std::nullopt;
    }
    if (!bindValid(b.chord)) return std::nullopt;
    return b;
}

std::vector<std::string> BindMatcher::feed(const std::vector<uint32_t>& masks, const std::vector<Bind>& binds) {
    std::vector<std::string> fired;
    for (uint32_t now : masks) {
        for (const Bind& b : binds)
            if (bindEdge(b.chord, now, prev_)) fired.push_back(b.item);
        prev_ = now;
    }
    return fired;
}

 

void ChordCapture::feed(uint32_t mask) {
    if (mask == 0xFFFFFFFFu) return;             
    mask &= kAllButtons;
    switch (state_) {
    case State::WaitRelease:
        if (!mask) state_ = State::Collect;
        break;
    case State::Collect:
        if (mask) {
             
            if ((mask & chord_) == chord_) chord_ = mask;
            break;
        }
        if (!chord_) break;
        if (chord_ == maskOf(Btn::Circle)) state_ = State::Cancelled;
        else if (chord_ == maskOf(Btn::Square)) state_ = State::Cleared;
        else state_ = State::Done;
        break;
    default: break;
    }
}

}   
