#include "game/spawn_place.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/error.h"
#include "game/world_objects.h"

namespace brps3::spawn {

namespace {

bool finite(const Vec3& v) { return v.finite(); }

Vec3 normalized(const Vec3& v) {
    const float l = v.length();
    return l > 1e-6f ? v * (1.0f / l) : Vec3{};
}

void put32(std::vector<uint8_t>& b, uint32_t off, uint32_t v) {
    b[off] = uint8_t(v >> 24);
    b[off + 1] = uint8_t(v >> 16);
    b[off + 2] = uint8_t(v >> 8);
    b[off + 3] = uint8_t(v);
}

 
void basis(const std::optional<CameraPose>& cam, Vec3& fwd, Vec3& right) {
    fwd = {1, 0, 0};
    if (cam) {
        const Vec3 h = normalized({cam->forward.x, cam->forward.y, 0});
        if (h.length() > 0.5f) fwd = h;
    }
    right = {fwd.y, -fwd.x, 0};
}

}   

 

CameraMatrices parseMatrices(const world::Bytes& b) {
    CameraMatrices m;
    for (uint32_t i = 0; i < 16; ++i) {
        m.view[i] = b.f32(i * 4);
        m.projection[i] = b.f32(0x40 + i * 4);
    }
    return m;
}

bool cameraFromView(const float* v, CameraPose& out) {
    for (int i = 0; i < 16; ++i)
        if (!std::isfinite(v[i])) return false;
    if (std::fabs(v[3]) > 0.001f || std::fabs(v[7]) > 0.001f || std::fabs(v[11]) > 0.001f || std::fabs(v[15] - 1) > 0.001f) return false;
    const Vec3 right{v[0], v[4], v[8]}, up{v[1], v[5], v[9]}, back{v[2], v[6], v[10]};
    for (const Vec3& axis : {right, up, back})
        if (std::fabs(dot(axis, axis) - 1) > 0.01f) return false;
    if (std::fabs(dot(right, up)) > 0.01f || std::fabs(dot(right, back)) > 0.01f || std::fabs(dot(up, back)) > 0.01f) return false;
    CameraPose pose;
    pose.position = {-v[0] * v[12] - v[1] * v[13] - v[2] * v[14], -v[4] * v[12] - v[5] * v[13] - v[6] * v[14],
                     -v[8] * v[12] - v[9] * v[13] - v[10] * v[14]};
    const float len = std::sqrt(dot(back, back));
    pose.forward = {-back.x / len, -back.y / len, -back.z / len};
    if (!finite(pose.position)) return false;
    out = pose;
    return true;
}

bool cameraFromMatrices(const CameraMatrices& m, float sx, float sy, CameraPose& out) {
    if (!std::isfinite(sx) || !std::isfinite(sy) || sx < 0 || sx > 1 || sy < 0 || sy > 1) return false;
    CameraPose pose;
    if (!cameraFromView(m.view.data(), pose)) return false;
    const auto& p = m.projection;
    for (float v : p)
        if (!std::isfinite(v)) return false;
     
    if (std::fabs(p[15]) > 1e-6f || std::fabs(p[3]) + std::fabs(p[7]) > 1e-6f || std::fabs(p[11]) < 1e-6f) return false;
    double a[4][8] = {};
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) a[row][col] = p[size_t(row * 4 + col)];
        a[row][row + 4] = 1;
    }
    for (int col = 0; col < 4; ++col) {
        int pivot = col;
        for (int row = col + 1; row < 4; ++row)
            if (std::fabs(a[row][col]) > std::fabs(a[pivot][col])) pivot = row;
        if (std::fabs(a[pivot][col]) < 1e-12) return false;
        if (pivot != col)
            for (int k = 0; k < 8; ++k) std::swap(a[col][k], a[pivot][k]);
        const double d = a[col][col];
        for (double& v : a[col]) v /= d;
        for (int row = 0; row < 4; ++row)
            if (row != col) {
                const double f = a[row][col];
                for (int k = 0; k < 8; ++k) a[row][k] -= f * a[col][k];
            }
    }
    const double clip[4] = {2.0 * sx - 1, 1 - 2.0 * sy, 0.5, 1};
    double point[4] = {};
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row) point[col] += clip[row] * a[row][col + 4];
    if (!std::isfinite(point[3]) || std::fabs(point[3]) < 1e-12) return false;
    for (int i = 0; i < 3; ++i) point[i] /= point[3];
    if (point[2] * p[11] <= 0) return false;
    const auto& v = m.view;
    double dir[3] = {};
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) dir[row] += v[size_t(row * 4 + col)] * point[col];
    const double len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (!std::isfinite(len) || len < 1e-12) return false;
    pose.forward = {float(dir[0] / len), float(dir[1] / len), float(dir[2] / len)};
    if (!finite(pose.forward)) return false;
    out = pose;
    return true;
}

bool aimRotation(Vec3 f, Quat& q) {
    if (!finite(f) || dot(f, f) < 1e-6f) return false;
    const float yaw = std::atan2(f.y, f.x) * 0.5f;
    const float pitch = std::atan2(f.z, std::hypot(f.x, f.y)) * 0.5f;
    q.x = std::sin(yaw) * std::sin(pitch);
    q.y = -std::cos(yaw) * std::sin(pitch);
    q.z = std::sin(yaw) * std::cos(pitch);
    q.w = std::cos(yaw) * std::cos(pitch);
    return true;
}

 

const char* modeName(Mode m) {
    switch (m) {
    case Mode::Crosshair: return "Crosshair";
    case Mode::Player: return "Player";
    case Mode::Coordinates: return "Coordinates";
    }
    return "?";
}

const char* facingName(Facing f) {
    switch (f) {
    case Facing::TowardMe: return "Face me";
    case Facing::AlongAim: return "Face along aim";
    case Facing::World: return "World axes";
    }
    return "?";
}

const char* playerSourceName(PlayerSource s) {
    return s == PlayerSource::Engine ? "engine position (sObject +0xB0)" : "player element +0x2810";
}

Placement clamped(Placement p) {
    auto fix = [](float v, float lo, float hi, float def) { return std::isfinite(v) ? std::clamp(v, lo, hi) : def; };
    p.distance = fix(p.distance, 0.5f, 200.0f, 6.0f);
    p.screenX = fix(p.screenX, 0.0f, 1.0f, 0.5f);
    p.screenY = fix(p.screenY, 0.0f, 1.0f, 0.5f);
    p.count = std::clamp(p.count, 1, kMaxCount);
    p.spacing = fix(p.spacing, 0.1f, 50.0f, 2.0f);
    p.scale = fix(p.scale, 0.05f, 20.0f, 1.0f);
    for (float* f : {&p.offset.x, &p.offset.y, &p.offset.z}) *f = fix(*f, -1000.0f, 1000.0f, 0.0f);
    for (float* f : {&p.coords.x, &p.coords.y, &p.coords.z}) *f = fix(*f, -1e7f, 1e7f, 0.0f);
    if (int(p.mode) < 0 || int(p.mode) > 2) p.mode = Mode::Crosshair;
    if (int(p.facing) < 0 || int(p.facing) > 2) p.facing = Facing::TowardMe;
    if (int(p.source) < 0 || int(p.source) > 1) p.source = PlayerSource::Engine;
    return p;
}

std::vector<std::pair<float, float>> gridOffsets(int count, float spacing) {
    count = std::clamp(count, 1, kMaxCount);
    const int perRow = int(std::ceil(std::sqrt(float(count))));
    const int rows = (count + perRow - 1) / perRow;
    std::vector<std::pair<float, float>> out;
    for (int i = 0; i < count; ++i) {
        const int col = i % perRow, row = i / perRow;
        const int inRow = row == rows - 1 ? count - row * perRow : perRow;    
        out.push_back({(float(col) - float(inRow - 1) * 0.5f) * spacing, (float(row) - float(rows - 1) * 0.5f) * spacing});
    }
    return out;
}

std::vector<SpawnPoint> resolvePoints(const Placement& in, const PlacementInputs& inputs) {
    const Placement p = clamped(in);
    std::optional<CameraPose> cam;
    if (inputs.camera) {
        CameraPose pose;
        if (cameraFromMatrices(*inputs.camera, p.screenX, p.screenY, pose)) cam = pose;
    }
    Vec3 centre;
    switch (p.mode) {
    case Mode::Crosshair:
        if (!inputs.camera) throw Error("Crosshair placement needs the rendered camera: load into an activity first");
        if (!cam) throw Error("The rendered camera is not usable right now (menu, cutscene or loading?): use Player or Coordinates");
        centre = cam->position + cam->forward * p.distance + p.offset;
        break;
    case Mode::Player:
        if (!inputs.player || !inputs.player->finite() || *inputs.player == Vec3{})
            throw Error("No player position: load into an activity first (or use Coordinates)");
        centre = *inputs.player + p.offset;
        break;
    case Mode::Coordinates: centre = p.coords; break;
    }
    if (!centre.finite() || std::fabs(centre.x) > 1e7f || std::fabs(centre.y) > 1e7f || std::fabs(centre.z) > 1e7f)
        throw Error("The spawn point is not a finite world position");
    Vec3 fwd, right;
    basis(cam, fwd, right);
    std::vector<SpawnPoint> out;
    for (const auto& [r, f] : gridOffsets(p.count, p.spacing)) {
        SpawnPoint s;
        s.position = centre + right * r + fwd * f;
        Vec3 dir{};
        if (p.facing == Facing::AlongAim && cam) dir = cam->forward;
        else if (p.facing == Facing::TowardMe) {
            if (cam) dir = cam->position - s.position;
            else if (inputs.player) dir = *inputs.player - s.position;
            dir.z = 0;    
        }
        if (!aimRotation(dir, s.rotation)) s.rotation = Quat{};
        out.push_back(s);
    }
    return out;
}

std::string summary(const Placement& in) {
    const Placement p = clamped(in);
    char buf[160];
    const char* dot = " \xC2\xB7 ";
    switch (p.mode) {
    case Mode::Crosshair: std::snprintf(buf, sizeof buf, "Crosshair%s%.1f m", dot, double(p.distance)); break;
    case Mode::Player: std::snprintf(buf, sizeof buf, "At player"); break;
    case Mode::Coordinates:
        std::snprintf(buf, sizeof buf, "At %.1f, %.1f, %.1f", double(p.coords.x), double(p.coords.y), double(p.coords.z));
        break;
    }
    std::string s = buf;
    if (p.count > 1) {
        std::snprintf(buf, sizeof buf, "%sx%d, %.1f m apart", dot, p.count, double(p.spacing));
        s += buf;
    }
    if (std::fabs(p.scale - 1.0f) > 0.001f) {
        std::snprintf(buf, sizeof buf, "%sscale %.2f", dot, double(p.scale));
        s += buf;
    }
    return s;
}

 

const std::array<uint32_t, kDescSize / 4>& descTemplate() {
     
     
    return world::spawnDescTemplate();
}

std::vector<uint8_t> buildDescriptor(uint32_t tag, const SpawnPoint& at, float scale) {
    std::vector<uint8_t> d(kDescSize, 0);
    const auto& t = descTemplate();
    for (uint32_t i = 0; i < kDescSize / 4; ++i) put32(d, i * 4, t[i]);
    put32(d, kDescTag, tag);
    put32(d, kDescTagOffset, kTagRefOffset);
    put32(d, kDescClass, kPlacementClass);
    put32(d, kDescRot + 0, world::floatBits(at.rotation.x));
    put32(d, kDescRot + 4, world::floatBits(at.rotation.y));
    put32(d, kDescRot + 8, world::floatBits(at.rotation.z));
    put32(d, kDescRot + 12, world::floatBits(at.rotation.w));
    put32(d, kDescPos + 0, world::floatBits(at.position.x));
    put32(d, kDescPos + 4, world::floatBits(at.position.y));
    put32(d, kDescPos + 8, world::floatBits(at.position.z));
    put32(d, kDescScale, world::floatBits(scale > 0 && std::isfinite(scale) ? scale : 1.0f));
    put32(d, kDescOwner, 0xFFFFFFFFu);    
    return d;
}

}   
