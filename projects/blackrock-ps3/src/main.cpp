 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include "ui/widgets.h"    

#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi1_2.h>
#include <shellapi.h>
#include <wincodec.h>

#include <crtdbg.h>
#include <stdlib.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <imgui_internal.h>

#include "app/engine.h"
#include "app/settings.h"
#include "ui/background.h"
#include "ui/shell.h"
#include "ui/theme.h"

#pragma comment(lib, "windowscodecs.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

using namespace brps3;

struct Options {
    ui::LaunchOptions launch;
    std::wstring screenshot;
    int frames = 30;
    int delayMs = 0;                     
    int width = 0, height = 0;
    float scale = 0;
    std::string selfcheck, items;
    bool selfcheckMode = false;
};

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain1* g_swap = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
HWND g_hwnd = nullptr;
ui::Shell* g_shell = nullptr;
Options g_opts;
bool g_inFrame = false;
bool g_minimized = false;
bool g_occluded = false;
bool g_scalePending = true;
bool g_cloaked = false;
UINT g_dpi = 96;
double g_lastActivity = 0;
RECT g_normalRect{};
LARGE_INTEGER g_freq{}, g_start{};

double now() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return double(t.QuadPart - g_start.QuadPart) / double(g_freq.QuadPart);
}

void activity() { g_lastActivity = now(); }

 

void createTarget() {
    ID3D11Texture2D* back = nullptr;
    if (SUCCEEDED(g_swap->GetBuffer(0, IID_PPV_ARGS(&back))) && back) {
        g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
        back->Release();
    }
}

void releaseTarget() {
    if (g_rtv) {
        g_rtv->Release();
        g_rtv = nullptr;
    }
}

bool createDevice(HWND hwnd) {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL got{};
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 3, D3D11_SDK_VERSION, &g_device, &got, &g_context);
    if (FAILED(hr))
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, levels, 3, D3D11_SDK_VERSION, &g_device, &got, &g_context);
    if (FAILED(hr)) return false;

    IDXGIDevice1* dxgiDevice = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory2* factory = nullptr;
    g_device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    if (dxgiDevice) {
        dxgiDevice->SetMaximumFrameLatency(1);
        dxgiDevice->GetAdapter(&adapter);
    }
    if (adapter) adapter->GetParent(IID_PPV_ARGS(&factory));
    if (!factory) {
        if (adapter) adapter->Release();
        if (dxgiDevice) dxgiDevice->Release();
        return false;
    }
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Scaling = DXGI_SCALING_NONE;
    sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    hr = factory->CreateSwapChainForHwnd(g_device, hwnd, &sd, nullptr, nullptr, &g_swap);
    if (FAILED(hr)) {
        sd.Scaling = DXGI_SCALING_STRETCH;
        hr = factory->CreateSwapChainForHwnd(g_device, hwnd, &sd, nullptr, nullptr, &g_swap);
    }
    if (FAILED(hr)) {
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        sd.BufferCount = 1;
        hr = factory->CreateSwapChainForHwnd(g_device, hwnd, &sd, nullptr, nullptr, &g_swap);
    }
    if (SUCCEEDED(hr)) factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    factory->Release();
    adapter->Release();
    dxgiDevice->Release();
    if (FAILED(hr)) return false;
    createTarget();
    return true;
}

void destroyDevice() {
    releaseTarget();
    if (g_swap) g_swap->Release();
    if (g_context) g_context->Release();
    if (g_device) g_device->Release();
    g_swap = nullptr;
    g_context = nullptr;
    g_device = nullptr;
}

void resizeTarget(UINT w, UINT h) {
    if (!g_swap || w == 0 || h == 0) return;
    releaseTarget();
    g_swap->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
    createTarget();
}

 

bool saveBackBuffer(const std::wstring& path) {
    ID3D11Texture2D* back = nullptr;
    if (FAILED(g_swap->GetBuffer(0, IID_PPV_ARGS(&back))) || !back) return false;
    D3D11_TEXTURE2D_DESC desc;
    back->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    HRESULT hr = g_device->CreateTexture2D(&desc, nullptr, &staging);
    if (FAILED(hr)) {
        back->Release();
        return false;
    }
    g_context->CopyResource(staging, back);
    back->Release();
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(g_context->Map(staging, 0, D3D11_MAP_READ, 0, &m))) {
        staging->Release();
        return false;
    }
    const UINT w = desc.Width, h = desc.Height;
    std::vector<BYTE> pixels(size_t(w) * h * 4);
    for (UINT y = 0; y < h; ++y) {
        const BYTE* src = static_cast<const BYTE*>(m.pData) + size_t(y) * m.RowPitch;
        BYTE* dst = pixels.data() + size_t(y) * w * 4;
        for (UINT x = 0; x < w; ++x) {
            dst[x * 4 + 0] = src[x * 4 + 2];
            dst[x * 4 + 1] = src[x * 4 + 1];
            dst[x * 4 + 2] = src[x * 4 + 0];
            dst[x * 4 + 3] = 255;
        }
    }
    g_context->Unmap(staging, 0);
    staging->Release();

    IWICImagingFactory* wic = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    bool ok = false;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
        SUCCEEDED(wic->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
        SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache)) && SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) &&
        SUCCEEDED(frame->Initialize(nullptr)) && SUCCEEDED(frame->SetSize(w, h))) {
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
        if (SUCCEEDED(frame->SetPixelFormat(&fmt)) &&
            SUCCEEDED(frame->WritePixels(h, w * 4, UINT(pixels.size()), pixels.data())) && SUCCEEDED(frame->Commit()) &&
            SUCCEEDED(encoder->Commit()))
            ok = true;
    }
    if (frame) frame->Release();
    if (encoder) encoder->Release();
    if (stream) stream->Release();
    if (wic) wic->Release();
    return ok;
}

 

struct P2 {
    float x, y;
};

bool inTriangle(P2 p, P2 a, P2 b, P2 c) {
    const float d1 = (p.x - b.x) * (a.y - b.y) - (a.x - b.x) * (p.y - b.y);
    const float d2 = (p.x - c.x) * (b.y - c.y) - (b.x - c.x) * (p.y - c.y);
    const float d3 = (p.x - a.x) * (c.y - a.y) - (c.x - a.x) * (p.y - a.y);
    const bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
    return !(neg && pos);
}

void inset(P2& a, P2& b, P2& c, float d) {
    auto len = [](P2 u, P2 v) { return std::sqrt((u.x - v.x) * (u.x - v.x) + (u.y - v.y) * (u.y - v.y)); };
    const float la = len(b, c), lb = len(c, a), lc = len(a, b), per = la + lb + lc;
    const P2 in{(a.x * la + b.x * lb + c.x * lc) / per, (a.y * la + b.y * lb + c.y * lc) / per};
    const float area = std::fabs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) * 0.5f;
    const float r = 2 * area / per, k = r > d ? (r - d) / r : 0.0f;
    for (P2* v : {&a, &b, &c}) *v = {in.x + (v->x - in.x) * k, in.y + (v->y - in.y) * k};
}

 
float segDist(P2 p, P2 a, P2 b) {
    const float vx = b.x - a.x, vy = b.y - a.y;
    const float t = std::clamp(((p.x - a.x) * vx + (p.y - a.y) * vy) / std::max(1e-6f, vx * vx + vy * vy), 0.0f, 1.0f);
    const float dx = a.x + vx * t - p.x, dy = a.y + vy * t - p.y;
    return std::sqrt(dx * dx + dy * dy);
}

 
bool inOutline(P2 p, P2 a, P2 b, P2 c, float stroke) {
    return inTriangle(p, a, b, c) && std::min({segDist(p, a, b), segDist(p, b, c), segDist(p, c, a)}) <= stroke;
}

HICON makeEmblemIcon(int size) {
    const float w = float(size) * 0.94f, h = w * 0.86f;
    const float ox = (float(size) - w) * 0.5f, oy = (float(size) - h) * 0.5f;
    auto P = [&](float x, float y) { return P2{ox + x * w, oy + y * h}; };
    const P2 A = P(0.504f, 0), L = P(0, 1), R = P(1, 1), C = P(0.482f, 0.63f);
    const float gap = std::max(0.75f, w * 0.04f), stroke = std::max(1.1f, w * 0.06f);
    P2 l1 = A, l2 = L, l3 = C;
    inset(l1, l2, l3, gap);
    P2 r1 = A, r2 = R, r3 = C;
    inset(r1, r2, r3, gap);
    P2 b1 = L, b2 = R, b3 = C;
    inset(b1, b2, b3, gap);

    BITMAPV5HEADER bh{};
    bh.bV5Size = sizeof(bh);
    bh.bV5Width = size;
    bh.bV5Height = -size;
    bh.bV5Planes = 1;
    bh.bV5BitCount = 32;
    bh.bV5Compression = BI_BITFIELDS;
    bh.bV5RedMask = 0x00FF0000;
    bh.bV5GreenMask = 0x0000FF00;
    bh.bV5BlueMask = 0x000000FF;
    bh.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&bh), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color || !bits) return nullptr;
    auto* px = static_cast<uint32_t*>(bits);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            int hits = 0;
            for (int sy = 0; sy < 4; ++sy)
                for (int sx = 0; sx < 4; ++sx) {
                    const P2 p{float(x) + (float(sx) + 0.5f) / 4, float(y) + (float(sy) + 0.5f) / 4};
                    const bool in = inOutline(p, l1, l2, l3, stroke) || inTriangle(p, r1, r2, r3) || inOutline(p, b1, b2, b3, stroke);
                    hits += in ? 1 : 0;
                }
            const uint32_t a = uint32_t(hits * 255 / 16);
            px[y * size + x] = (a << 24) | (0xE8u << 16) | (0xE8u << 8) | 0xEAu;    
        }
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO ii{TRUE, 0, 0, mask, color};
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(color);
    return icon;
}

 

struct ScriptEvent {
    int frame;
    std::string what;
};
std::vector<ScriptEvent> g_script;

void loadScript() {
    char* v = nullptr;
    size_t n = 0;
    if (_dupenv_s(&v, &n, "BRPS3_UI_INPUT") != 0 || !v) return;
    const std::string text = v;
    free(v);
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find(';', start);
        if (end == std::string::npos) end = text.size();
        const std::string item = text.substr(start, end - start);
        const size_t colon = item.find(':');
        if (colon != std::string::npos) g_script.push_back({std::atoi(item.substr(0, colon).c_str()), item.substr(colon + 1)});
        start = end + 1;
    }
}

ImGuiKey keyFromName(const std::string& name) {
    auto lower = [](std::string s) {
        for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    const std::string want = lower(name);
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k)
        if (lower(ImGui::GetKeyName(ImGuiKey(k))) == want) return ImGuiKey(k);
    return ImGuiKey_None;
}

void injectInput(int frame) {
    ImGuiIO& io = ImGui::GetIO();
    for (const ScriptEvent& ev : g_script) {
        if (ev.frame != frame) continue;
        if (ev.what.rfind("wheel=", 0) == 0) {
            io.AddMouseWheelEvent(0.0f, float(std::atof(ev.what.c_str() + 6)));
        } else if (ev.what.rfind("text=", 0) == 0) {
            for (char c : ev.what.substr(5)) io.AddInputCharacter(static_cast<unsigned int>(static_cast<unsigned char>(c)));
        } else if (ev.what.rfind("click=", 0) == 0 || ev.what.rfind("rclick=", 0) == 0 || ev.what.rfind("move=", 0) == 0) {
            const size_t eq = ev.what.find('=');
            float x = 0, y = 0;
            sscanf_s(ev.what.c_str() + eq + 1, "%fx%f", &x, &y);
            io.AddMousePosEvent(x, y);
            if (ev.what[0] != 'm') {
                const int button = ev.what[0] == 'r' ? 1 : 0;
                io.AddMouseButtonEvent(button, true);
                io.AddMouseButtonEvent(button, false);
            }
        } else {
            std::string rest = ev.what;
            std::vector<ImGuiKey> mods;
            for (const char* m : {"Ctrl+", "Shift+", "Alt+"}) {
                if (rest.rfind(m, 0) == 0) {
                    mods.push_back(m[0] == 'C' ? ImGuiMod_Ctrl : m[0] == 'S' ? ImGuiMod_Shift : ImGuiMod_Alt);
                    rest = rest.substr(strlen(m));
                }
            }
            const ImGuiKey key = keyFromName(rest);
            if (key == ImGuiKey_None) continue;
            for (ImGuiKey m : mods) io.AddKeyEvent(m, true);
            io.AddKeyEvent(key, true);
            io.AddKeyEvent(key, false);
            for (ImGuiKey m : mods) io.AddKeyEvent(m, false);
        }
    }
}

 

float dpiScale() { return g_opts.scale > 0 ? g_opts.scale : float(g_dpi) / 96.0f; }

void rememberNormalRect() {
    if (g_hwnd && !IsIconic(g_hwnd) && !IsZoomed(g_hwnd)) GetWindowRect(g_hwnd, &g_normalRect);
}

void saveWindowState() {
    if (!g_hwnd || !g_opts.screenshot.empty()) return;
    rememberNormalRect();
    char buf[96];
    snprintf(buf, sizeof(buf), "%ld,%ld,%ld,%ld,%d", g_normalRect.left, g_normalRect.top, g_normalRect.right - g_normalRect.left,
             g_normalRect.bottom - g_normalRect.top, IsZoomed(g_hwnd) ? 1 : 0);
    Engine::get().settings().set(std::string("ui.window"), std::string(buf));
}

void renderFrame() {
    if (g_inFrame || !g_shell || !g_rtv) return;
    g_inFrame = true;
    float userScale = 0;
    if (g_shell->takeScaleChange(&userScale)) g_scalePending = true;
    if (g_scalePending) {
        ui::applyScale(dpiScale(), g_shell->userScale());
        g_scalePending = false;
    }
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    if (!g_opts.screenshot.empty()) ImGui::GetIO().DeltaTime = 1.0f / 60.0f;    
    ImGui::NewFrame();
    g_shell->frame();
    ImGui::Render();
    const ImVec4 bg = ui::pal::Bg;
    const float clear[4] = {bg.x, bg.y, bg.z, 1.0f};
    g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
    g_context->ClearRenderTargetView(g_rtv, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_inFrame = false;
}

void present() {
    const HRESULT hr = g_swap->Present(g_opts.screenshot.empty() ? 1 : 0, 0);
    g_occluded = hr == DXGI_STATUS_OCCLUDED;
    if (g_cloaked) {
        BOOL off = FALSE;
        DwmSetWindowAttribute(g_hwnd, DWMWA_CLOAK, &off, sizeof(off));
        g_cloaked = false;
    }
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE:
        g_minimized = wParam == SIZE_MINIMIZED;
        if (!g_minimized && g_device) {
            resizeTarget(LOWORD(lParam), HIWORD(lParam));
            rememberNormalRect();
            activity();
            if (!g_inFrame && g_shell && g_opts.screenshot.empty()) {
                renderFrame();
                present();
            }
        }
        return 0;
    case WM_MOVE:
        rememberNormalRect();
        return 0;
    case WM_DPICHANGED: {
        g_dpi = HIWORD(wParam);
        g_scalePending = true;
        const RECT* r = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        activity();
        return 0;
    }
    case WM_GETMINMAXINFO: {
        auto* mm = reinterpret_cast<MINMAXINFO*>(lParam);
        const float s = float(g_dpi) / 96.0f;
        mm->ptMinTrackSize.x = LONG(760 * s);
        mm->ptMinTrackSize.y = LONG(520 * s);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        if (!g_inFrame && g_shell && g_opts.screenshot.empty() && !g_minimized) {
            renderFrame();
            present();
        }
        return 0;
    }
    case WM_ACTIVATE:
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        activity();
        break;
    case WM_EXITSIZEMOVE:
        saveWindowState();
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_KEYMENU) return 0;    
        break;
    case WM_CLOSE:
        saveWindowState();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool parseArgs(Options& o) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return true;
    auto narrow = [](const wchar_t* w) {
        std::string s;
        for (; *w; ++w) s += char(*w < 128 ? *w : '?');
        return s;
    };
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        auto next = [&]() -> const wchar_t* { return i + 1 < argc ? argv[++i] : L""; };
        if (a == L"--screenshot") o.screenshot = next();
        else if (a == L"--page") o.launch.page = narrow(next());
        else if (a == L"--tab") o.launch.tab = narrow(next());
        else if (a == L"--frames") o.frames = std::max(1, _wtoi(next()));
        else if (a == L"--delay") o.delayMs = std::max(0, _wtoi(next()));
        else if (a == L"--size") swscanf_s(next(), L"%dx%d", &o.width, &o.height);
        else if (a == L"--scale") o.scale = float(_wtof(next()));
        else if (a == L"--log") o.launch.openLog = true;
        else if (a == L"--palette") o.launch.openPalette = true;
        else if (a == L"--collapsed") o.launch.collapsed = true;
        else if (a == L"--demo") {
            o.launch.demo = true;
            _putenv_s("BRPS3_UI_DEMO", "1");    
        }
        else if (a == L"--selfcheck") {
            o.selfcheckMode = true;
            o.selfcheck = narrow(next());
        } else if (a == L"--items") o.items = narrow(next());
        else if (a == L"--icons") {
             
            const unsigned start = i + 1 < argc ? unsigned(wcstoul(argv[i + 1], nullptr, 16)) : 0u;
            if (start) ++i;
            o.launch.iconSheet = start ? start : 0xE700u;
        }
    }
    LocalFree(argv);
    o.launch.screenshot = !o.screenshot.empty();
    if (o.launch.screenshot && o.scale <= 0) o.scale = 1.0f;
    return true;
}

void initialPlacement(int& x, int& y, int& w, int& h, bool& maximized) {
    const float s = float(GetDpiForSystem()) / 96.0f;
    w = int(1440 * s);
    h = int(900 * s);
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    w = std::min(w, int(work.right - work.left));
    h = std::min(h, int(work.bottom - work.top));
    x = work.left + (work.right - work.left - w) / 2;
    y = work.top + (work.bottom - work.top - h) / 2;
    maximized = false;
    if (!g_opts.screenshot.empty()) {
        if (g_opts.width > 0 && g_opts.height > 0) {
            RECT r{0, 0, g_opts.width, g_opts.height};
            AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForSystem());
            w = r.right - r.left;
            h = r.bottom - r.top;
        }
        return;
    }
    const std::string saved = Engine::get().settings().str("ui.window", "");
    int sx, sy, sw, sh, sm = 0;
    if (sscanf_s(saved.c_str(), "%d,%d,%d,%d,%d", &sx, &sy, &sw, &sh, &sm) >= 4 && sw >= 400 && sh >= 300) {
        RECT r{sx, sy, sx + sw, sy + sh};
        if (MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) {
            x = sx;
            y = sy;
            w = sw;
            h = sh;
            maximized = sm != 0;
        }
    }
}

}   

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCmd) {
#ifdef _DEBUG
     
    _set_error_mode(_OUT_TO_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    QueryPerformanceFrequency(&g_freq);
    QueryPerformanceCounter(&g_start);
    ImGui_ImplWin32_EnableDpiAwareness();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    parseArgs(g_opts);
    if (g_opts.selfcheckMode) {
        ImGui::CreateContext();    
        const int rc = ui::runSelfCheck(g_opts.selfcheck, g_opts.items);
        ImGui::DestroyContext();
        CoUninitialize();
        return rc;
    }
    const bool shot = !g_opts.screenshot.empty();

    Engine& engine = Engine::get();
    engine.settings().load();

    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = L"BlackrockPS3";
    RegisterClassExW(&wc);

    int x, y, w, h;
    bool maximized;
    initialPlacement(x, y, w, h, maximized);
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"Blackrock PS3", WS_OVERLAPPEDWINDOW, x, y, w, h, nullptr, nullptr, instance, nullptr);
    if (!g_hwnd) return 1;
    g_dpi = GetDpiForWindow(g_hwnd);
    HICON bigIcon = makeEmblemIcon(GetSystemMetricsForDpi(SM_CXICON, g_dpi));
    HICON smallIcon = makeEmblemIcon(GetSystemMetricsForDpi(SM_CXSMICON, g_dpi));
    if (bigIcon) SendMessageW(g_hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(bigIcon));
    if (smallIcon) SendMessageW(g_hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(smallIcon));
    if (shot && g_opts.width > 0 && g_opts.height > 0) {
         
        RECT r{0, 0, g_opts.width, g_opts.height};
        AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, g_dpi);
        SetWindowPos(g_hwnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    GetWindowRect(g_hwnd, &g_normalRect);

     
    BOOL dark = TRUE;
    DwmSetWindowAttribute(g_hwnd, 20  , &dark, sizeof(dark));
    const COLORREF caption = RGB(0x13, 0x14, 0x17), text = RGB(0xED, 0xEE, 0xF0), border = RGB(0x26, 0x27, 0x2C);
    DwmSetWindowAttribute(g_hwnd, 35  , &caption, sizeof(caption));
    DwmSetWindowAttribute(g_hwnd, 36  , &text, sizeof(text));
    DwmSetWindowAttribute(g_hwnd, 34  , &border, sizeof(border));

    if (!createDevice(g_hwnd)) {
        MessageBoxW(g_hwnd, L"Direct3D 11 could not be initialised.", L"Blackrock PS3", MB_ICONERROR);
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;             
    io.LogFilename = nullptr;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.ConfigInputTextCursorBlink = true;
     
    ImGui::GetCurrentContext()->ConfigNavWindowingKeyNext = 0;
    ImGui::GetCurrentContext()->ConfigNavWindowingKeyPrev = 0;
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);
    ui::setBackgroundDevice(g_device);
    ui::initTheme();

     
    ui::detail::setWakeHandler([] {
        if (g_hwnd) PostMessageW(g_hwnd, WM_NULL, 0, 0);
    });
    {
        ui::Shell shell(g_opts.launch);
        g_shell = &shell;
        if (shot) {
            loadScript();
            for (int i = 0; i < g_opts.frames; ++i) {
                MSG msg;
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
                injectInput(i);
                if (g_opts.delayMs > 0 && i == std::max(0, g_opts.frames - 10)) Sleep(DWORD(g_opts.delayMs));
                renderFrame();
                if (i + 1 == g_opts.frames) {
                    const bool ok = saveBackBuffer(g_opts.screenshot);
                    if (!ok) MessageBoxW(nullptr, (L"Could not write " + g_opts.screenshot).c_str(), L"Blackrock PS3", MB_ICONERROR);
                }
                g_swap->Present(0, 0);
            }
        } else {
             
            BOOL cloak = TRUE;
            g_cloaked = SUCCEEDED(DwmSetWindowAttribute(g_hwnd, DWMWA_CLOAK, &cloak, sizeof(cloak)));
            ShowWindow(g_hwnd, maximized ? SW_SHOWMAXIMIZED : (showCmd == SW_SHOWMINIMIZED ? SW_SHOWMINIMIZED : SW_SHOWNORMAL));
            UpdateWindow(g_hwnd);
            renderFrame();
            present();
            activity();
            bool done = false;
            while (!done) {
                const bool idle = g_minimized || g_occluded || (!g_shell->wantsFullRate() && now() - g_lastActivity > 2.0);
                if (idle) MsgWaitForMultipleObjectsEx(0, nullptr, g_shell->idleWaitMs(), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                bool gotMessage = false;
                MSG msg;
                while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                    gotMessage = true;
                    if ((msg.message >= WM_KEYFIRST && msg.message <= WM_KEYLAST) ||
                        (msg.message >= WM_MOUSEFIRST && msg.message <= WM_MOUSELAST) || msg.message == WM_NCMOUSEMOVE ||
                        msg.message == WM_MOUSELEAVE)
                        activity();
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                    if (msg.message == WM_QUIT) done = true;
                }
                if (done) break;
                if (g_minimized) {
                    engine.pump();
                    continue;
                }
                if (ui::detail::takeWake()) gotMessage = true;                   
                if (idle && !gotMessage && !g_shell->needsFrame()) continue;    
                renderFrame();
                present();
            }
        }
        g_shell = nullptr;
    }
    ui::detail::setWakeHandler(nullptr);

    engine.shutdown();
    if (!shot) engine.settings().save();
    ui::releaseBackground();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    destroyDevice();
    if (g_hwnd) DestroyWindow(g_hwnd);
    if (bigIcon) DestroyIcon(bigIcon);
    if (smallIcon) DestroyIcon(smallIcon);
    UnregisterClassW(wc.lpszClassName, instance);
    CoUninitialize();
    return 0;
}
