#include "game/movement_w2s.h"

#include <cmath>

#include "game/movement_addr.h"

namespace brps3::movement {

namespace {

 
void rows(const float* V, float m[3][3]) {
    for (int r = 0; r < 3; ++r) {
        m[r][0] = V[r];           
        m[r][1] = V[8 + r];       
        m[r][2] = V[4 + r];       
    }
}

}   

bool View::valid() const {
    bool any = false;
    for (int i = 0; i < 16; ++i) {
        if (!std::isfinite(V[i]) || !std::isfinite(P[i])) return false;
        any = any || V[i] != 0.0f;
    }
    return any;
}

std::optional<ScreenPoint> View::project(const world::Vec3& p) const {
    const float x = p.x, y = p.y, z = p.z;
    const float vx = V[0] * x + V[8] * y + V[4] * z + V[12];
    const float vy = V[1] * x + V[9] * y + V[5] * z + V[13];
    const float vz = V[2] * x + V[10] * y + V[6] * z + V[14];
    const float w = P[3] * vx + P[7] * vy + P[11] * vz + P[15];
    if (!(w >= 0.0001f)) return std::nullopt;             
    const float nx = (P[0] * vx + P[4] * vy + P[8] * vz + P[12]) / w;
    const float ny = (P[1] * vx + P[5] * vy + P[9] * vz + P[13]) / w;
    const float sw = float(width ? width : 1024), sh = float(height ? height : 624);
    ScreenPoint s;
    s.x = (nx + 1.0f) * sw * 0.5f;
    s.y = (1.0f - ny) * sh * 0.5f;
    s.depth = w;
    s.onScreen = s.x > 0.0f && s.x < sw && s.y > 0.0f && s.y < sh;
    return s;
}

world::Vec3 View::cameraPosition() const {
    float m[3][3];
    rows(V, m);
    const float t[3] = {V[12], V[13], V[14]};
    world::Vec3 c;
    c.x = -(m[0][0] * t[0] + m[1][0] * t[1] + m[2][0] * t[2]);
    c.y = -(m[0][1] * t[0] + m[1][1] * t[1] + m[2][1] * t[2]);
    c.z = -(m[0][2] * t[0] + m[1][2] * t[1] + m[2][2] * t[2]);
    return c;
}

world::Vec3 View::cameraForward() const {
    float m[3][3];
    rows(V, m);
     
    const float f[3] = {P[3], P[7], P[11]};
    world::Vec3 d;
    d.x = m[0][0] * f[0] + m[1][0] * f[1] + m[2][0] * f[2];
    d.y = m[0][1] * f[0] + m[1][1] * f[1] + m[2][1] * f[2];
    d.z = m[0][2] * f[0] + m[1][2] * f[1] + m[2][2] * f[2];
    const float len = d.length();
    return len > 1e-6f ? d * (1.0f / len) : world::Vec3{};
}

View parseView(const world::Bytes& b) {
    View v;
    for (int i = 0; i < 16; ++i) {
        v.V[i] = b.f32(uint32_t(4 * i));
        v.P[i] = b.f32(addr::PROJ_MATRIX - addr::VIEW_MATRIX + uint32_t(4 * i));
    }
    const uint32_t s = addr::SCREEN_SIZE - addr::VIEW_MATRIX;
    v.width = b.u16(s);
    v.height = b.u16(s + 2);
    if (!v.width) v.width = 1024;
    if (!v.height) v.height = 624;
    return v;
}

View readView(world::Reader& mem) { return parseView(mem.bytes(addr::VIEW_MATRIX, addr::VIEW_READ)); }

}   
