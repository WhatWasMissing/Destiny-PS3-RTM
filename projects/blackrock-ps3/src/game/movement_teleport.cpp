#include "game/movement_teleport.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <sstream>

#include "core/error.h"
#include "game/movement_addr.h"
#include "game/world_addr.h"

namespace brps3::movement {

namespace {

using world::Vec3;

std::vector<std::string> splitOn(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        const size_t e = s.find(sep, start);
        out.push_back(s.substr(start, e == std::string::npos ? std::string::npos : e - start));
        if (e == std::string::npos) break;
        start = e + 1;
    }
    return out;
}

bool parseFloat(const std::string& t, float& out) {
    if (t.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (end == t.c_str() || *end != 0) return false;
    out = float(v);
    return std::isfinite(out);
}

std::string fmtF(float v) {
    char b[48];
    std::snprintf(b, sizeof b, "%.4f", double(v));
    return b;
}

 
std::pair<world::LocalPlayer, Vec3> current(world::Reader& mem) {
    const world::LocalPlayer p = world::requireLocalPlayer(mem);
    const Vec3 pos = world::readPosition(mem, p);
    if (!pos.finite() || !validTarget(pos))
        throw Error("The position at element +0x2810 (" + formatVec(pos) +
                    ") is not plausible: run TESTING > Port checks > Movement > Teleport target first.");
    return {p, pos};
}

TeleportResult write(world::Reader& mem, const WriteFn& writeFn, const world::LocalPlayer& p, const Vec3& before, const Vec3& target) {
    std::string why;
    if (!validTarget(target, &why)) throw Error(why);
    if (!world::stillValid(mem, p)) throw Error("The player changed (respawn or load) before the teleport: nothing was written.");
    const std::vector<uint8_t> bytes = world::beVec3(target);
    const uint32_t at = p.element + world::player::ELEM_POSITION;
    writeFn(at, bytes);
    TeleportResult r;
    r.before = before;
    r.target = target;
    const auto back = mem.tryBytes(at, 12);
    r.verified = back && back->data() == bytes;
    r.message = r.verified ? "Moved to " + formatVec(target) + "."
                           : "Written, but the read-back differs: the engine already moved or corrected the player (check in game).";
    return r;
}

}   

bool validTarget(const Vec3& v, std::string* why) {
    if (!v.finite()) {
        if (why) *why = "Coordinates must be finite numbers.";
        return false;
    }
    if (std::fabs(v.x) > d1::TP_LIMIT || std::fabs(v.y) > d1::TP_LIMIT || std::fabs(v.z) > d1::TP_LIMIT) {
        if (why) *why = "Coordinates must be within +-10,000,000.";
        return false;
    }
    return true;
}

TeleportResult teleportTo(world::Reader& mem, const WriteFn& w, const Vec3& target) {
    std::string why;
    if (!validTarget(target, &why)) throw Error(why);
    auto [p, before] = current(mem);
    return write(mem, w, p, before, target);
}

TeleportResult teleportBy(world::Reader& mem, const WriteFn& w, const Vec3& offset) {
    std::string why;
    if (!validTarget(offset, &why)) throw Error(why);
    auto [p, before] = current(mem);
    const Vec3 target = before + offset;
    if (!validTarget(target)) throw Error("That offset would move outside the supported coordinate range.");
    return write(mem, w, p, before, target);
}

TeleportResult teleportEdge(world::Reader& mem, const WriteFn& w, float up) {
    auto [p, before] = current(mem);
    return write(mem, w, p, before, before + Vec3{0, 0, up});
}

std::optional<Vec3> horizontal(const Vec3& f) {
    const float len = std::sqrt(f.x * f.x + f.y * f.y);
    if (!std::isfinite(len) || len < d1::OPK_MIN_LEN) return std::nullopt;
    return Vec3{f.x / len, f.y / len, 0};
}

TeleportResult teleportForward(world::Reader& mem, const WriteFn& w, float distance) {
    if (!std::isfinite(distance) || distance <= 0 || distance > 10000) throw Error("Blink distance must be between 0 and 10,000.");
    auto [p, before] = current(mem);
    if (!p.playerEntry) throw Error("The player array row (camera forward +0x110) was not found.");
    const Vec3 fwd = mem.bytes(p.playerEntry + world::player::ENTRY_FORWARD, 12).vec3(0);
    const auto dir = horizontal(fwd);
    if (!dir) throw Error("The camera is looking straight up or down (forward " + formatVec(fwd) + "): turn and try again.");
    return write(mem, w, p, before, before + *dir * distance);
}

 

std::string cleanName(const std::string& name) {
    std::string s;
    for (char c : name) s += (c == '\t' || c == '\n' || c == '\r' || c == '|') ? ' ' : c;
    const size_t a = s.find_first_not_of(' ');
    if (a == std::string::npos) return {};
    s = s.substr(a, s.find_last_not_of(' ') - a + 1);
    if (s.size() > 48) s.resize(48);
    return s;
}

std::string uniqueName(const std::vector<Slot>& slots, const std::string& base) {
    const std::string b = cleanName(base).empty() ? std::string("Spot") : cleanName(base);
    auto taken = [&](const std::string& n) { return std::any_of(slots.begin(), slots.end(), [&](const Slot& s) { return s.name == n; }); };
    if (!taken(b)) return b;
    for (int i = 2;; ++i) {
        const std::string n = b + " " + std::to_string(i);
        if (!taken(n)) return n;
    }
}

std::string encodeSlots(const std::vector<Slot>& slots) {
    std::string out;
    for (const Slot& s : slots) {
        if (!out.empty()) out += '\n';
        out += cleanName(s.name) + '\t' + fmtF(s.pos.x) + '\t' + fmtF(s.pos.y) + '\t' + fmtF(s.pos.z) + '\t' + (s.favourite ? "1" : "0") + '\t' +
               std::to_string(s.used) + '\t' + std::to_string(s.created) + '\t' + cleanName(s.note);
    }
    return out;
}

std::vector<Slot> decodeSlots(const std::string& text) {
    std::vector<Slot> out;
    for (const std::string& line : splitOn(text, '\n')) {
        const auto f = splitOn(line, '\t');
        if (f.size() < 4) continue;
        Slot s;
        s.name = cleanName(f[0]);
        if (s.name.empty() || !parseFloat(f[1], s.pos.x) || !parseFloat(f[2], s.pos.y) || !parseFloat(f[3], s.pos.z)) continue;
        if (f.size() > 4) s.favourite = f[4] == "1";
        try {
            if (f.size() > 5) s.used = std::stoll(f[5]);
            if (f.size() > 6) s.created = std::stoll(f[6]);
        } catch (...) {
        }
        if (f.size() > 7) s.note = cleanName(f[7]);
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<size_t> slotOrder(const std::vector<Slot>& slots) {
    std::vector<size_t> idx(slots.size());
    std::iota(idx.begin(), idx.end(), size_t(0));
    std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
        const Slot& x = slots[a];
        const Slot& y = slots[b];
        if (x.favourite != y.favourite) return x.favourite;
        if (x.used != y.used) return x.used > y.used;
        return x.created > y.created;
    });
    return idx;
}

std::string formatVec(const Vec3& v, int decimals) {
    char b[128];
    std::snprintf(b, sizeof b, "%.*f, %.*f, %.*f", decimals, double(v.x), decimals, double(v.y), decimals, double(v.z));
    return b;
}

std::optional<Vec3> parseVec(const std::string& text) {
    std::string t;
    for (char c : text) t += (c == ',' || c == '(' || c == ')' || c == ';' || c == '\t') ? ' ' : c;
    std::istringstream ss(t);
    std::vector<std::string> parts;
    std::string p;
    while (ss >> p) parts.push_back(p);
    if (parts.size() != 3) return std::nullopt;
    Vec3 v;
    if (!parseFloat(parts[0], v.x) || !parseFloat(parts[1], v.y) || !parseFloat(parts[2], v.z)) return std::nullopt;
    return v;
}

 

void History::push(const Vec3& p) {
    if (!p.finite()) return;
    points.insert(points.begin(), p);
    if (points.size() > kMax) points.resize(kMax);
}

std::optional<Vec3> History::pop() {
    if (points.empty()) return std::nullopt;
    const Vec3 p = points.front();
    points.erase(points.begin());
    return p;
}

std::string History::encode() const {
    std::string out;
    for (const Vec3& p : points) {
        if (!out.empty()) out += '\n';
        out += fmtF(p.x) + '\t' + fmtF(p.y) + '\t' + fmtF(p.z);
    }
    return out;
}

History History::decode(const std::string& text) {
    History h;
    for (const std::string& line : splitOn(text, '\n')) {
        const auto f = splitOn(line, '\t');
        Vec3 v;
        if (f.size() == 3 && parseFloat(f[0], v.x) && parseFloat(f[1], v.y) && parseFloat(f[2], v.z)) h.points.push_back(v);
        if (h.points.size() >= kMax) break;
    }
    return h;
}

}   
