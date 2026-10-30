#define IMGUI_DEFINE_MATH_OPERATORS
#include "ui/widgets.h"    
#include "ui/background.h"

#include <windows.h>
#include <d3d11.h>

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>


namespace brps3::ui {
namespace {

ID3D11Device* g_device = nullptr;
ID3D11ShaderResourceView* g_srv = nullptr;
int g_w = 0, g_h = 0;           
float g_dpr = 0;                
int g_wantW = 0, g_wantH = 0;   
double g_wantSince = 0;

float srgbToLinear(float v) {
    v /= 255.0f;
    return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}

uint32_t hash2(uint32_t x, uint32_t y) {
    uint32_t h = (x * 0x9E3779B1u) ^ ((y + 0x632BE5ABu) * 0x85EBCA77u);
    h = (h ^ (h >> 16)) * 0x7FEB352Du;
    h = (h ^ (h >> 15)) * 0x846CA68Bu;
    return h ^ (h >> 16);
}

 
 
std::vector<uint32_t> bake(int PW, int PH, float dpr) {
    std::vector<uint32_t> pixels(static_cast<size_t>(PW) * static_cast<size_t>(PH));
    const float W = float(PW) / dpr, H = float(PH) / dpr;
    constexpr int LN = 8192;
    constexpr float LMAX = 0.06f;
    static std::vector<float> lut;
    if (lut.empty()) {
        lut.resize(LN + 1);
        for (int k = 0; k <= LN; ++k) {
            const float v = float(k) / LN * LMAX;
            lut[size_t(k)] = 255.0f * (v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f);
        }
    }
    auto enc = [&](float v) { return lut[size_t(std::clamp(int(v * (LN / LMAX) + 0.5f), 0, LN))]; };
    const float top[3] = {srgbToLinear(15), srgbToLinear(16), srgbToLinear(20)};
    const float mid[3] = {srgbToLinear(17), srgbToLinear(17), srgbToLinear(20)};
    const float bot[3] = {srgbToLinear(17), srgbToLinear(17), srgbToLinear(18)};
    const float tint[3] = {0.84f, 0.92f, 1.0f};
    const float lx = W * 0.6f, La = W * 1.05f, Lb = W * 0.6f, dd = 60, s = 46, y0 = 10, tail = 0.08f;
    auto seg = [](float L, float dx, float r2) {
        const float u1 = dx + L / 2, u0 = dx - L / 2;
        return u1 / std::sqrt(r2 + u1 * u1) - u0 / std::sqrt(r2 + u0 * u0);
    };
    auto strip = [&](float dx, float r2) { return seg(La, dx, r2) + seg(Lb, dx, r2); };
    auto V = [&](float y) {
        const float t = (y + y0) / s;
        return (1 - tail) * std::exp(-t * t) + tail * dd * dd / (dd * dd + (y + y0) * (y + y0));
    };
    const float A = 0.0086f / V(30);
    const float fx = W * 0.84f, fy = H * 0.08f, h2 = (H * 0.5f) * (H * 0.5f);
    std::vector<float> fl(static_cast<size_t>(PW));
    for (int i = 0; i < PW; ++i) {
        const float dx = (float(i) + 0.5f) / dpr - fx;
        fl[size_t(i)] = dx * dx;
    }
     
    const float lightX = W * 0.28f, lightY = H * 0.12f, lightR = std::max(W, H) * 0.9f;
    const float vr0 = std::min(W, H) * 0.35f, vr1 = std::max(W, H) * 0.75f;
    const float shadeW = W * 0.38f;
    for (int j = 0; j < PH; ++j) {
        const float y = (float(j) + 0.5f) / dpr, v = y / H, a = (1 - v) * (1 - v), b = 2 * v * (1 - v), e = v * v;
        const float r2 = (y + y0) * (y + y0) + dd * dd, vy = A * V(y) / strip(0, r2), fdy = (y - fy) * (y - fy);
        const float R = top[0] * a + mid[0] * b + bot[0] * e, G = top[1] * a + mid[1] * b + bot[1] * e, B = top[2] * a + mid[2] * b + bot[2] * e;
        uint32_t* row = pixels.data() + size_t(j) * size_t(PW);
        for (int i = 0; i < PW; ++i) {
            const float x = (float(i) + 0.5f) / dpr;
            const float q = h2 / (h2 + fl[size_t(i)] + fdy);
            const float E = vy * strip(x - lx, r2) + 0.0037f * q * std::sqrt(q);
            float c[3] = {enc(R + E * tint[0]), enc(G + E * tint[1]), enc(B + E * tint[2])};
             
            const float dl = std::sqrt((x - lightX) * (x - lightX) + (y - lightY) * (y - lightY));
            const float al = 0.035f * std::max(0.0f, 1.0f - dl / lightR);
             
            const float dv = std::sqrt((x - W * 0.5f) * (x - W * 0.5f) + (y - H * 0.5f) * (y - H * 0.5f));
            const float av = 0.42f * std::clamp((dv - vr0) / (vr1 - vr0), 0.0f, 1.0f);
            const float as = x < shadeW ? 0.28f * (1.0f - x / shadeW) : 0.0f;
            const uint32_t hsh = hash2(uint32_t(i), uint32_t(j));
            const float n = (float((hsh & 0xFFFF) + (hsh >> 16)) / 65535.0f - 1.0f) * 2.2f;    
            uint32_t out = 0xFF000000u;
            for (int k = 0; k < 3; ++k) {
                float ch = c[k] + (255.0f - c[k]) * al;
                ch *= (1.0f - av) * (1.0f - as);
                const int iv = std::clamp(int(std::lround(ch + n)), 0, 255);
                out |= uint32_t(iv) << (8 * k);    
            }
            row[i] = out;
        }
    }
    return pixels;
}

void rebake(int w, int h, float dpr) {
    if (!g_device || w <= 0 || h <= 0) return;
    const std::vector<uint32_t> pixels = bake(w, h, dpr);
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = UINT(w);
    desc.Height = UINT(h);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem = pixels.data();
    init.SysMemPitch = UINT(w) * 4;
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(g_device->CreateTexture2D(&desc, &init, &tex)) || !tex) return;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = desc.Format;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = 1;
    ID3D11ShaderResourceView* srv = nullptr;
    const HRESULT hr = g_device->CreateShaderResourceView(tex, &sd, &srv);
    tex->Release();
    if (FAILED(hr) || !srv) return;
    if (g_srv) g_srv->Release();
    g_srv = srv;
    g_w = w;
    g_h = h;
    g_dpr = dpr;
}

}   

void setBackgroundDevice(void* device) { g_device = static_cast<ID3D11Device*>(device); }

void releaseBackground() {
    if (g_srv) g_srv->Release();
    g_srv = nullptr;
    g_w = g_h = 0;
    g_device = nullptr;
}

void drawBackground(ImDrawList* d, ImVec2 min, ImVec2 max) {
    const int w = int(std::lround(max.x - min.x)), h = int(std::lround(max.y - min.y));
    const float dpr = scale();
    const double now = ImGui::GetTime();
    if (w != g_wantW || h != g_wantH) {
        g_wantW = w;
        g_wantH = h;
        g_wantSince = now;
    }
    const bool stale = w != g_w || h != g_h || std::fabs(dpr - g_dpr) > 1e-3f;
     
    if (stale && (!g_srv || now - g_wantSince > 0.15)) rebake(w, h, dpr);
    else if (stale) requestRedraw();    
    d->AddRectFilled(min, max, ImGui::GetColorU32(pal::Window));
    if (g_srv) d->AddImage(ImTextureRef(ImTextureID(reinterpret_cast<intptr_t>(g_srv))), min, max);
     
    const float x0 = min.x + px(12), x1 = max.x - px(12);
    if (x1 > x0) {
        const float stops[5] = {0.0f, 0.30f, 0.56f, 0.82f, 1.0f};
        const float alphas[5] = {0.02f, 0.10f, 0.16f, 0.10f, 0.02f};
        for (int k = 0; k < 4; ++k) {
            const float a = x0 + (x1 - x0) * stops[k], b = x0 + (x1 - x0) * stops[k + 1];
            const ImU32 ca = ImGui::GetColorU32(ImVec4(1, 1, 1, alphas[k])), cb = ImGui::GetColorU32(ImVec4(1, 1, 1, alphas[k + 1]));
            d->AddRectFilledMultiColor(ImVec2(a, min.y), ImVec2(b, min.y + 1.0f), ca, cb, cb, ca);
        }
        d->AddRectFilled(ImVec2(x0, min.y + 1.0f), ImVec2(x1, min.y + 2.0f), ImGui::GetColorU32(ImVec4(0, 0, 0, 0.18f)));
    }
}

}   
