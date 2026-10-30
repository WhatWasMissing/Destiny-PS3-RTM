#define IMGUI_DEFINE_MATH_OPERATORS
#include "features/movement_widgets.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "features/movement_shared.h"
#include "ui/widgets.h"

namespace brps3::mvui {

using namespace ui;
using movement::Btn;

namespace {

 
ImVec4 faceColor(Btn b) {
    switch (b) {
    case Btn::Cross: return rgb(0x7DA7F0);
    case Btn::Circle: return rgb(0xEF6F6C);
    case Btn::Square: return rgb(0xE48ACB);
    case Btn::Triangle: return rgb(0x5FD3A6);
    default: return pal::Text;
    }
}

bool isFace(Btn b) { return b == Btn::Cross || b == Btn::Circle || b == Btn::Square || b == Btn::Triangle; }

const char* capText(Btn b) {
    switch (b) {
    case Btn::Up: return "Up";
    case Btn::Down: return "Down";
    case Btn::Left: return "Left";
    case Btn::Right: return "Right";
    default: return movement::buttonName(b);
    }
}

 
void drawShape(ImDrawList* d, Btn b, ImVec2 c, float r, ImU32 col, float thick) {
    switch (b) {
    case Btn::Cross:
        d->AddLine(c + ImVec2(-r, -r), c + ImVec2(r, r), col, thick);
        d->AddLine(c + ImVec2(-r, r), c + ImVec2(r, -r), col, thick);
        break;
    case Btn::Circle: d->AddCircle(c, r * 1.05f, col, 20, thick); break;
    case Btn::Square: d->AddRect(c - ImVec2(r, r) * 0.92f, c + ImVec2(r, r) * 0.92f, col, 0, 0, thick); break;
    case Btn::Triangle:
        d->AddTriangle(c + ImVec2(0, -r * 1.08f), c + ImVec2(r * 1.05f, r * 0.75f), c + ImVec2(-r * 1.05f, r * 0.75f), col, thick);
        break;
    default: break;
    }
}

}   

float padCapWidth(Btn b, float size) {
    if (isFace(b)) return px(size);
    const ImVec2 ts = textSize(FontKind::Semibold, type::Tiny, capText(b));
    return std::max(px(size), ts.x + px(12));
}

void padCap(Btn b, bool lit, float size) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float h = px(size), w = padCapWidth(b, size);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h));
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", movement::buttonName(b));
    const float round = isFace(b) ? h * 0.5f : px(5);
    const ImVec4 fill = lit ? alpha(pal::Accent, 0.22f) : pal::Raised;
    d->AddRectFilled(pos, pos + ImVec2(w, h), u32(fill), round);
    d->AddRect(pos, pos + ImVec2(w, h), u32(lit ? alpha(pal::Accent, 0.85f) : pal::InputBorder), round, 0, 1.0f);
    if (isFace(b)) {
        const ImVec4 col = lit ? faceColor(b) : mix(faceColor(b), pal::Muted, 0.35f);
        drawShape(d, b, pos + ImVec2(w, h) * 0.5f, h * 0.24f, u32(col), std::max(1.0f, px(1.6f)));
        return;
    }
    const char* t = capText(b);
    const ImVec2 ts = textSize(FontKind::Semibold, type::Tiny, t);
    drawText(d, FontKind::Semibold, type::Tiny, ImVec2(pos.x + (w - ts.x) * 0.5f, textTop(FontKind::Semibold, type::Tiny, pos.y + h * 0.5f)),
             u32(lit ? pal::Accent : pal::Text), t);
}

float padChordWidth(uint32_t chord, float size) {
    const auto bs = movement::chordButtons(chord);
    if (bs.empty()) return textSize(FontKind::Body, type::Small, "unbound").x;
    float w = 0;
    for (size_t i = 0; i < bs.size(); ++i) w += padCapWidth(bs[i], size) + (i ? px(14) : 0.0f);
    return w;
}

void padChord(uint32_t chord, bool lit, float size) {
    const auto bs = movement::chordButtons(chord);
    if (bs.empty()) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (px(size) - textSize(FontKind::Body, type::Small, "u").y) * 0.5f);
        caption("unbound", pal::Faint);
        return;
    }
    ImGui::BeginGroup();
    for (size_t i = 0; i < bs.size(); ++i) {
        if (i) {
            ImGui::SameLine(0, 0);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(px(14), px(size)));
            const ImVec2 ps = textSize(FontKind::Body, type::Tiny, "+");
            drawText(ImGui::GetWindowDrawList(), FontKind::Body, type::Tiny, ImVec2(p.x + (px(14) - ps.x) * 0.5f, textTop(FontKind::Body, type::Tiny, p.y + px(size) * 0.5f)),
                     u32(pal::Faint), "+");
            ImGui::SameLine(0, 0);
        }
        padCap(bs[i], lit, size);
    }
    ImGui::EndGroup();
}

void padDiagram(const movement::PadState& p, float width, float height) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(width, height));
    const ImVec2 sz(width, height);
    auto held = [&](Btn b) { return (p.held & movement::maskOf(b)) != 0; };
    const ImU32 idle = u32(pal::FillActive), lit = u32(alpha(pal::Accent, 0.9f));
     
    const float sw = width * 0.19f, sh = height * 0.085f;
    struct Sh {
        Btn b;
        float x, y;
    };
    const Sh shoulders[] = {{Btn::L2, 0.07f, 0.0f}, {Btn::L1, 0.07f, 0.09f}, {Btn::R2, 0.93f, 0.0f}, {Btn::R1, 0.93f, 0.09f}};
    for (const Sh& s : shoulders) {
        const float x = s.x < 0.5f ? o.x + width * s.x : o.x + width * s.x - sw;
        const ImVec2 a(x, o.y + height * s.y), b(x + sw, o.y + height * s.y + sh);
        d->AddRectFilled(a, b, held(s.b) ? lit : idle, px(5));
        const char* t = movement::buttonName(s.b);
        const ImVec2 ts = textSize(FontKind::Semibold, type::Micro, t);
        drawText(d, FontKind::Semibold, type::Micro, ImVec2((a.x + b.x - ts.x) * 0.5f, textTop(FontKind::Semibold, type::Micro, (a.y + b.y) * 0.5f)),
                 u32(held(s.b) ? pal::OnAccent : pal::Muted), t);
    }
     
    const float bodyTop = o.y + height * 0.19f, bodyBottom = o.y + height * 0.77f;
    const ImU32 bodyCol = u32(pal::Input), gripCol = u32(ImVec4(0, 0, 0, 0.25f)), edge = u32(pal::Border);
    const float gripR = width * 0.10f;
    auto grip = [&](float x0, float x1) {
        const ImVec2 a(o.x + width * x0, o.y + height * 0.52f), b(o.x + width * x1, o.y + height - gripR);
        d->AddLine(a, b, gripCol, gripR * 2.0f);
        d->AddCircleFilled(a, gripR, gripCol, 32);
        d->AddCircleFilled(b, gripR, gripCol, 32);
    };
    grip(0.20f, 0.13f);
    grip(0.80f, 0.87f);
    d->AddRectFilled(ImVec2(o.x, bodyTop), ImVec2(o.x + width, bodyBottom), bodyCol, px(22));
    d->AddRect(ImVec2(o.x, bodyTop), ImVec2(o.x + width, bodyBottom), edge, px(22), 0, 1.0f);
     
    const ImVec2 dc(o.x + width * 0.20f, bodyTop + height * 0.17f);
    const float a = height * 0.07f;
    struct Dp {
        Btn b;
        ImVec2 off;
    };
    const Dp dpad[] = {{Btn::Up, ImVec2(0, -1)}, {Btn::Down, ImVec2(0, 1)}, {Btn::Left, ImVec2(-1, 0)}, {Btn::Right, ImVec2(1, 0)}};
    for (const Dp& k : dpad) {
        const ImVec2 c = dc + k.off * (a * 1.15f);
        d->AddRectFilled(c - ImVec2(a, a) * 0.55f, c + ImVec2(a, a) * 0.55f, held(k.b) ? lit : idle, px(3));
    }
     
    const ImVec2 fc(o.x + width * 0.80f, dc.y);
    const Dp face[] = {{Btn::Triangle, ImVec2(0, -1)}, {Btn::Cross, ImVec2(0, 1)}, {Btn::Square, ImVec2(-1, 0)}, {Btn::Circle, ImVec2(1, 0)}};
    for (const Dp& k : face) {
        const ImVec2 c = fc + k.off * (a * 1.2f);
        d->AddCircleFilled(c, a * 0.62f, held(k.b) ? u32(alpha(faceColor(k.b), 0.30f)) : idle, 20);
        drawShape(d, k.b, c, a * 0.28f, u32(held(k.b) ? faceColor(k.b) : mix(faceColor(k.b), pal::Muted, 0.45f)), std::max(1.0f, px(1.4f)));
    }
     
    const float mid = o.x + width * 0.5f;
    d->AddRectFilled(ImVec2(mid - a * 1.6f, dc.y - a * 0.25f), ImVec2(mid - a * 0.6f, dc.y + a * 0.25f), held(Btn::Select) ? lit : idle, px(3));
    d->AddTriangleFilled(ImVec2(mid + a * 0.6f, dc.y - a * 0.3f), ImVec2(mid + a * 0.6f, dc.y + a * 0.3f), ImVec2(mid + a * 1.4f, dc.y),
                         held(Btn::Start) ? lit : idle);
     
    auto stick = [&](ImVec2 c, int16_t x, int16_t y, bool press) {
        const float r = height * 0.13f;
        d->AddCircleFilled(c, r, u32(ImVec4(0, 0, 0, 0.25f)), 28);
        d->AddCircle(c, r, press ? lit : u32(pal::Border), 28, press ? 2.0f : 1.0f);
        d->AddCircle(c, r * (8000.0f / 32768.0f), u32(alpha(pal::Decor, 0.6f)), 16);    
        const ImVec2 k = c + ImVec2(float(x) / 32768.0f, -float(y) / 32768.0f) * (r * 0.78f);
        d->AddCircleFilled(k, r * 0.28f, press ? lit : u32(alpha(pal::Text, 0.85f)), 20);
    };
    const float sy = o.y + height * 0.60f;
    stick(ImVec2(o.x + width * 0.35f, sy), p.lx, p.ly, held(Btn::L3));
    stick(ImVec2(o.x + width * 0.65f, sy), p.rx, p.ry, held(Btn::R3));
    if (!p.connected) {
        d->AddRectFilled(o, o + sz, u32(alpha(pal::Card, 0.72f)), px(18));
        const char* t = "Controller 1 not connected";
        const ImVec2 ts = textSize(FontKind::Semibold, type::Small, t);
        drawText(d, FontKind::Semibold, type::Small, o + (sz - ts) * 0.5f, u32(pal::Muted), t);
    }
}

void stickCap(float size, bool lit) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float h = px(size);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(h, h));
    const ImVec2 c = pos + ImVec2(h, h) * 0.5f;
    d->AddCircleFilled(c, h * 0.5f, u32(lit ? alpha(pal::Accent, 0.22f) : pal::Raised), 24);
    d->AddCircle(c, h * 0.5f, u32(lit ? pal::Accent : pal::InputBorder), 24, 1.0f);
    d->AddCircleFilled(c, h * 0.2f, u32(lit ? pal::Accent : pal::Text), 16);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("Left stick");
}

void statusLine(const ImVec4& color, const char* label, const std::string& detail, bool pulse, float width) {
    const float h = px(22), w = std::max(px(40), width > 0 ? width : availWidth());
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h));
    ImDrawList* d = ImGui::GetWindowDrawList();
    const ImVec2 dot(p.x + px(4), p.y + h * 0.5f);
    if (pulse && !reduceMotion()) {
        const float t = float(std::fmod(ImGui::GetTime(), 1.6) / 1.6);
        d->AddCircleFilled(dot, px(4) + px(5) * t, u32(alpha(color, 0.35f * (1.0f - t))), 20);
        requestRedraw();
    }
    d->AddCircleFilled(dot, px(4), u32(color), 16);
    const float lx = p.x + px(16);
    drawText(d, FontKind::Semibold, type::Label, ImVec2(lx, textTop(FontKind::Semibold, type::Label, dot.y)), u32(pal::Text), label);
    if (detail.empty()) return;
    const float dx = lx + textSize(FontKind::Semibold, type::Label, label).x + px(10);
    const float room = p.x + w - dx;
    if (room < px(30)) return;
    const std::string cut = ellipsize(detail.c_str(), room, FontKind::Body, type::Small);
    drawText(d, FontKind::Body, type::Small, ImVec2(dx, textTop(FontKind::Body, type::Small, dot.y)), u32(pal::Muted), cut.c_str());
    if (cut != detail && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", detail.c_str());
}

std::string ago(double at) {
    if (at <= 0) return "never";
    const double d = now() - at;
    char b[32];
    if (d < 1.5) return "just now";
    if (d < 90) std::snprintf(b, sizeof b, "%.0f s ago", d);
    else if (d < 5400) std::snprintf(b, sizeof b, "%.0f min ago", d / 60);
    else std::snprintf(b, sizeof b, "%.0f h ago", d / 3600);
    return b;
}

}   
