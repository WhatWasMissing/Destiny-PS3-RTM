#include "ui/theme.h"

#include <windows.h>

#include <algorithm>
#include <cfloat>
#include <filesystem>
#include <string>

namespace brps3::ui {
namespace {

Fonts g_fonts;
float g_scale = 1.0f;
bool g_reduceMotion = false;

std::string fontPath(const char* file) {
    wchar_t dir[MAX_PATH] = {};
    UINT n = GetWindowsDirectoryW(dir, MAX_PATH);
    std::filesystem::path p = n ? std::filesystem::path(dir) / L"Fonts" / file : std::filesystem::path(file);
    std::error_code ec;
    return std::filesystem::exists(p, ec) ? p.string() : std::string();
}

 
constexpr float kSegoeLine = 2724.0f / 2048.0f;
constexpr float kCascadiaLine = 2380.0f / 2048.0f;
 
 
constexpr float kIconRatio = 0.865f;
constexpr float kIconShift = kIconRatio / 2.0f - 0.281f;    

 
 
const ImWchar kIconRanges[] = {0xE000, 0xF8FF, 0};

void mergeIcons(ImFontAtlas* atlas, float textSize, const std::string& icons) {
    if (icons.empty()) return;
    ImFontConfig cfg;
    cfg.MergeMode = true;
    cfg.GlyphRanges = kIconRanges;
    cfg.GlyphOffset = ImVec2(0, textSize * kIconShift);
    atlas->AddFontFromFileTTF(icons.c_str(), textSize * kIconRatio, &cfg);
}

ImFont* addFont(ImFontAtlas* atlas, const std::string& path, float size, float brighten) {
    if (path.empty()) return nullptr;
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.RasterizerMultiply = brighten;
    return atlas->AddFontFromFileTTF(path.c_str(), size, &cfg);
}

void buildStyle(ImGuiStyle& s) {
    ImGui::StyleColorsDark(&s);
    s.WindowPadding = ImVec2(sp::L, sp::L);
    s.FramePadding = ImVec2(10, 7);
    s.ItemSpacing = ImVec2(sp::S, sp::S);
    s.ItemInnerSpacing = ImVec2(sp::S, 6);
    s.CellPadding = ImVec2(10, 6);
    s.IndentSpacing = 18;
    s.ScrollbarSize = 6;
    s.ScrollbarPadding = 2;
    s.GrabMinSize = 12;
    s.WindowRounding = kCardRounding;
    s.ChildRounding = kCardRounding;
    s.FrameRounding = kFrameRounding;
    s.PopupRounding = kCardRounding;
    s.ScrollbarRounding = 4;
    s.GrabRounding = 6;
    s.TabRounding = kFrameRounding;
    s.SelectableRounding = 6;
    s.WindowBorderSize = 0;
    s.ChildBorderSize = 1;
    s.PopupBorderSize = 1;
    s.FrameBorderSize = 0;
    s.TabBorderSize = 0;
    s.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    s.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    s.SelectableTextAlign = ImVec2(0, 0.5f);
    s.SeparatorTextBorderSize = 1;
    s.DisabledAlpha = 0.55f;
    s.AntiAliasedLines = true;
    s.AntiAliasedFill = true;
    s.HoverDelayShort = 0.25f;
    s.HoverDelayNormal = 0.45f;
    s.HoverStationaryDelay = 0.15f;

    ImVec4* c = s.Colors;
    const ImVec4 clear(0, 0, 0, 0);
    c[ImGuiCol_Text] = pal::Text;
    c[ImGuiCol_TextDisabled] = pal::Faint;
    c[ImGuiCol_WindowBg] = pal::Window;
    c[ImGuiCol_ChildBg] = clear;
    c[ImGuiCol_PopupBg] = pal::RaisedFill;
    c[ImGuiCol_Border] = ImVec4(1, 1, 1, 0.08f);
    c[ImGuiCol_BorderShadow] = clear;
    c[ImGuiCol_FrameBg] = pal::Fill;
    c[ImGuiCol_FrameBgHovered] = pal::FillHover;
    c[ImGuiCol_FrameBgActive] = ImVec4(1, 1, 1, 0.13f);
    c[ImGuiCol_TitleBg] = pal::Window;
    c[ImGuiCol_TitleBgActive] = pal::Window;
    c[ImGuiCol_TitleBgCollapsed] = pal::Window;
    c[ImGuiCol_MenuBarBg] = pal::Window;
    c[ImGuiCol_ScrollbarBg] = clear;
    c[ImGuiCol_ScrollbarGrab] = ImVec4(1, 1, 1, 0.14f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(1, 1, 1, 0.22f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(1, 1, 1, 0.28f);
    c[ImGuiCol_CheckMark] = pal::Text;
    c[ImGuiCol_CheckboxSelectedBg] = pal::FillActive;
    c[ImGuiCol_SliderGrab] = pal::Accent;
    c[ImGuiCol_SliderGrabActive] = pal::AccentHover;
    c[ImGuiCol_Button] = ImVec4(1, 1, 1, 0.07f);
    c[ImGuiCol_ButtonHovered] = ImVec4(1, 1, 1, 0.11f);
    c[ImGuiCol_ButtonActive] = ImVec4(1, 1, 1, 0.05f);
    c[ImGuiCol_Header] = pal::Selection;
    c[ImGuiCol_HeaderHovered] = ImVec4(1, 1, 1, 0.05f);
    c[ImGuiCol_HeaderActive] = pal::Selection;
    c[ImGuiCol_Separator] = pal::Line;
    c[ImGuiCol_SeparatorHovered] = pal::FillActive;
    c[ImGuiCol_SeparatorActive] = pal::Accent;
    c[ImGuiCol_ResizeGrip] = clear;
    c[ImGuiCol_ResizeGripHovered] = pal::FillActive;
    c[ImGuiCol_ResizeGripActive] = pal::Accent;
    c[ImGuiCol_InputTextCursor] = pal::Text;
    c[ImGuiCol_Tab] = clear;
    c[ImGuiCol_TabHovered] = pal::Fill;
    c[ImGuiCol_TabSelected] = pal::FillActive;
    c[ImGuiCol_TabSelectedOverline] = clear;
    c[ImGuiCol_TabDimmed] = clear;
    c[ImGuiCol_TabDimmedSelected] = pal::Fill;
    c[ImGuiCol_TabDimmedSelectedOverline] = clear;
    c[ImGuiCol_PlotLines] = pal::Accent;
    c[ImGuiCol_PlotLinesHovered] = pal::AccentHover;
    c[ImGuiCol_PlotHistogram] = pal::Accent;
    c[ImGuiCol_PlotHistogramHovered] = pal::AccentHover;
    c[ImGuiCol_TableHeaderBg] = pal::RaisedFill;
    c[ImGuiCol_TableBorderStrong] = pal::Line;
    c[ImGuiCol_TableBorderLight] = pal::Line;
    c[ImGuiCol_TableRowBg] = clear;
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1, 1, 1, 0.015f);
    c[ImGuiCol_TextLink] = pal::Accent;
    c[ImGuiCol_TextSelectedBg] = ImVec4(1, 1, 1, 0.20f);
    c[ImGuiCol_DragDropTarget] = pal::Accent;
    c[ImGuiCol_NavCursor] = pal::Accent;
    c[ImGuiCol_NavWindowingHighlight] = alpha(pal::Accent, 0.7f);
    c[ImGuiCol_NavWindowingDimBg] = ImVec4(0, 0, 0, 0.5f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.55f);
}

}   

const Fonts& fonts() { return g_fonts; }

ImFont* font(FontKind kind) {
    switch (kind) {
    case FontKind::Semibold: return g_fonts.semibold;
    case FontKind::Mono: return g_fonts.mono;
    case FontKind::Icons: return g_fonts.icons;
    default: return g_fonts.body;
    }
}

float fontSize(FontKind kind, float em) {
    switch (kind) {
    case FontKind::Mono: return em * kCascadiaLine;
    case FontKind::Icons: return em;
    default: return em * kSegoeLine;
    }
}

float textTop(FontKind kind, float em, float midY) {
     
    const float k = kind == FontKind::Mono ? 0.4983f : kind == FontKind::Icons ? 0.5f : 0.5481f;
    return midY - k * px(fontSize(kind, em));
}

float ascent(FontKind kind, float em) {
    ImFont* f = font(kind);
    const float size = px(fontSize(kind, em));
    if (!f) return size * 0.75f;
    ImFontBaked* b = f->GetFontBaked(size);
    return b ? b->Ascent : size * 0.75f;
}

FontScope::FontScope(FontKind kind, float em) { ImGui::PushFont(font(kind), fontSize(kind, em)); }
FontScope::~FontScope() { ImGui::PopFont(); }

ImVec2 textSize(FontKind kind, float em, const char* text, const char* end, float wrap) {
    ImFont* f = font(kind);
    const float size = px(fontSize(kind, em));
    ImVec2 s = f->CalcTextSizeA(size, FLT_MAX, wrap, text, end);
    s.x = float(int(s.x + 0.99999f));
    return s;
}

void drawText(ImDrawList* dl, FontKind kind, float em, ImVec2 pos, ImU32 col, const char* text, const char* end, float wrap) {
    pos.x = float(int(pos.x + 0.5f));
    pos.y = float(int(pos.y + 0.5f));
    dl->AddText(font(kind), px(fontSize(kind, em)), pos, col, text, end, wrap);
}
float scale() { return g_scale; }
bool reduceMotion() { return g_reduceMotion; }
void setReduceMotion(bool on) { g_reduceMotion = on; }

void initTheme() {
    ImGuiIO& io = ImGui::GetIO();
    ImFontAtlas* atlas = io.Fonts;
    atlas->Clear();

    const std::string body = fontPath("segoeui.ttf");
    const std::string semi = fontPath("seguisb.ttf");
    std::string mono = fontPath("CascadiaMono.ttf");
    if (mono.empty()) mono = fontPath("consola.ttf");
    std::string icons = fontPath("SegoeIcons.ttf");
    if (icons.empty()) icons = fontPath("segmdl2.ttf");

    const float ref = type::Body * kSegoeLine;
    g_fonts.body = addFont(atlas, body, ref, 1.10f);
    if (g_fonts.body) mergeIcons(atlas, ref, icons);
    else g_fonts.body = atlas->AddFontDefault();

    g_fonts.semibold = addFont(atlas, semi.empty() ? body : semi, ref, 1.0f);
    if (g_fonts.semibold) mergeIcons(atlas, ref, icons);
    else g_fonts.semibold = g_fonts.body;

    g_fonts.mono = addFont(atlas, mono, type::Mono * kCascadiaLine, 1.08f);
    if (!g_fonts.mono) g_fonts.mono = g_fonts.body;

    if (!icons.empty()) {
        ImFontConfig cfg;
        cfg.GlyphRanges = kIconRanges;
        g_fonts.icons = atlas->AddFontFromFileTTF(icons.c_str(), type::Icon, &cfg);
    }
    if (!g_fonts.icons) g_fonts.icons = g_fonts.body;
    io.FontDefault = g_fonts.body;
}

void applyScale(float dpiScale, float userScale) {
    const float s = std::clamp(dpiScale * (userScale > 0 ? userScale : 1.0f), 0.5f, 4.0f);
    g_scale = s;
    ImGuiStyle& style = ImGui::GetStyle();
    ImGuiStyle fresh;
    buildStyle(fresh);
    fresh.ScaleAllSizes(s);
    fresh.FontSizeBase = fontSize(FontKind::Body, type::Body);
    fresh.FontScaleMain = 1.0f;
    fresh.FontScaleDpi = s;
     
    fresh.ChildBorderSize = fresh.PopupBorderSize = 1.0f;
    fresh.FrameBorderSize = 0.0f;
    style = fresh;
    style._NextFrameFontSizeBase = style.FontSizeBase;
}

}   
