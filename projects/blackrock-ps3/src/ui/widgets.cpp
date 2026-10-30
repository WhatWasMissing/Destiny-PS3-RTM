#define IMGUI_DEFINE_MATH_OPERATORS
#include "ui/widgets.h"

#include <imgui_internal.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "app/engine.h"
#include "app/registry.h"

namespace brps3::ui {
namespace {

bool g_redraw = false;
std::unordered_map<std::string, double> g_success;   
std::mutex g_activityMutex;
std::vector<detail::Activity> g_activities;    
const std::string* g_host = nullptr;
int g_searchFocusFrame = -1;                          
constexpr double kSuccessFlash = 1.2;
std::unordered_map<ImGuiID, float> g_anim;
std::unordered_map<ImGuiID, ImVec4> g_animColor;
std::vector<detail::Note> g_notes;
std::vector<const char*> g_reasons;                  
constexpr float kPi = 3.14159265f;

 
struct NumberEdit {
    std::string text;
    int startFrame = 0;
    bool focus = true;
};
std::unordered_map<ImGuiID, NumberEdit> g_numberEdits;

 
struct RowTrack {
    ImGuiWindow* window = nullptr;
    int frame = -1;
    float x = 0, endY = 0;
} g_lastRow;

 
struct PickerState {
    ImGuiID id = 0;
    float h = 0, s = 0, v = 0;
    std::string hex;
} g_picker;

struct CardFrame {
    ImDrawListSplitter splitter;
    ImDrawList* drawList = nullptr;
    ImVec2 min, headerMax;
    float width = 0, padX = 0, padV = 0, panelTop = 0, headerRight = 0, headerY = 0;
    ImRect savedWork, savedContent;
    bool raised = false, caption = false;
    ImVec4 accent;
};
CardFrame g_cards[8];
int g_cardDepth = 0;

struct ConfirmState {
    std::string title, message, label;
    std::function<void()> fn;
    bool danger = true, requestOpen = false;
    int openedFrame = -1;                
};
ConfirmState g_confirm;

float frameDt() { return std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.1f); }
ImDrawList* drawList() { return ImGui::GetWindowDrawList(); }
bool lastItemDisabled() { return (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0; }
bool scopeDisabled() { return (GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0; }

const char* visibleEnd(const char* label) { return ImGui::FindRenderedTextEnd(label); }

void vtext(FontKind kind, float em, const ImVec4& color, const char* fmt, va_list args) {
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, args);
    FontScope f(kind, em);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(buf);
    ImGui::PopStyleColor();
}

int stringResize(ImGuiInputTextCallbackData* d) {
    if (d->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* s = static_cast<std::string*>(d->UserData);
        s->resize(size_t(d->BufTextLen));
        d->Buf = s->data();
    }
    return 0;
}

 
 
struct FieldStyle {
    explicit FieldStyle(bool mono = false) {
        const FontKind k = mono ? FontKind::Mono : FontKind::Body;
        ImGui::PushFont(font(k), fontSize(k, mono ? type::Mono : type::Label));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, pal::Fill);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, pal::FillHover);
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(1, 1, 1, 0.13f));
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, pal::Faint);    
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(kFrameRounding));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(10), std::max(0.0f, (px(kControlH) - ImGui::GetFontSize()) * 0.5f)));
    }
    ~FieldStyle() {
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(4);
        ImGui::PopFont();
    }
};

 
void navRing(const ImVec2& a, const ImVec2& b, float rounding) {
    ImGuiContext& g = *GImGui;
    if (!g.NavCursorVisible || g.NavId == 0 || g.NavId != ImGui::GetItemID()) return;
    drawList()->AddRect(a - ImVec2(px(1), px(1)), b + ImVec2(px(1), px(1)), u32(pal::Accent, 0.9f), rounding + px(1), 0, 1.0f);
}

 
 
 
class Overlay {
public:
    Overlay() : g_(*GImGui), win_(g_.CurrentWindow), saved_(g_.LastItemData) {
        dc_ = {win_->DC.CursorPos, win_->DC.CursorPosPrevLine, win_->DC.CursorMaxPos, win_->DC.PrevLineSize, win_->DC.CurrLineSize,
               win_->DC.PrevLineTextBaseOffset, win_->DC.CurrLineTextBaseOffset, win_->DC.IsSameLine, win_->DC.IsSetPos};
        disabled_ = (g_.CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
        if (disabled_) ImGui::BeginDisabledOverrideReenable();
    }
    ~Overlay() {
        if (disabled_) ImGui::EndDisabledOverrideReenable();
        win_->DC.CursorPos = dc_.cursor;
        win_->DC.CursorPosPrevLine = dc_.prevLine;
        win_->DC.CursorMaxPos = dc_.maxPos;
        win_->DC.PrevLineSize = dc_.prevSize;
        win_->DC.CurrLineSize = dc_.currSize;
        win_->DC.PrevLineTextBaseOffset = dc_.prevBase;
        win_->DC.CurrLineTextBaseOffset = dc_.currBase;
        win_->DC.IsSameLine = dc_.sameLine;
        win_->DC.IsSetPos = dc_.setPos;
        g_.LastItemData = saved_;
    }
    Overlay(const Overlay&) = delete;
    Overlay& operator=(const Overlay&) = delete;
private:
    ImGuiContext& g_;
    ImGuiWindow* win_;
    ImGuiLastItemData saved_;
    struct {
        ImVec2 cursor, prevLine, maxPos, prevSize, currSize;
        float prevBase, currBase;
        bool sameLine, setPos;
    } dc_{};
    bool disabled_ = false;
};

 
void fieldRing() {}

void drawSpinnerAt(ImDrawList* d, ImVec2 c, float r, ImU32 col, float thickness) {
    const float t = float(ImGui::GetTime());
    const float start = t * 7.0f;
    const float sweep = kPi * (0.9f + 0.5f * std::sin(t * 2.2f));
    d->PathClear();
    d->PathArcTo(c, r, start, start + sweep, 20);
    d->PathStroke(col, 0, thickness);
    requestRedraw();
}

 
 
void rowSeparator(ImDrawList* d, ImVec2 pos, float w) {
    ImGuiWindow* win = ImGui::GetCurrentWindow();
    const float spacing = ImGui::GetStyle().ItemSpacing.y;
    if (g_lastRow.window == win && g_lastRow.frame == ImGui::GetFrameCount() && std::fabs(g_lastRow.x - pos.x) < 1.0f &&
        pos.y >= g_lastRow.endY - 0.5f && pos.y - g_lastRow.endY <= spacing + px(2)) {
        const float y = std::floor((g_lastRow.endY + pos.y) * 0.5f);
        d->AddRectFilled(ImVec2(pos.x, y), ImVec2(pos.x + w, y + 1.0f), u32(pal::Line));
    }
}
void rowEnded(ImVec2 pos, float h) {
    g_lastRow.window = ImGui::GetCurrentWindow();
    g_lastRow.frame = ImGui::GetFrameCount();
    g_lastRow.x = pos.x;
    g_lastRow.endY = pos.y + h;
}

 
void rowPlate(ImDrawList* d, ImVec2 pos, float w, float h, float hover, float flash) {
    const ImVec2 a(pos.x - px(6), pos.y + px(3)), b(pos.x + w + px(6), pos.y + h - px(3));
    if (hover > 0.01f) d->AddRectFilled(a, b, u32(ImVec4(1, 1, 1, 0.035f * hover)), px(7));
    if (flash > 0.01f) d->AddRectFilled(a, b, u32(pal::Accent, 0.14f * flash), px(7));
}

 
void rowLabel(ImDrawList* d, ImVec2 pos, float h, float maxW, const char* text, int depth, float alphaMul) {
    const float x = pos.x + px(kSubIndent) * float(std::max(0, depth));
    const float room = std::max(px(24), maxW - (x - pos.x));
    const std::string shown = ellipsize(text, room, FontKind::Body, type::Label);
    drawText(d, FontKind::Body, type::Label, ImVec2(x, textTop(FontKind::Body, type::Label, pos.y + h * 0.5f)),
             u32(depth > 0 ? pal::Muted : pal::Text, alphaMul), shown.c_str());
}

float labelWidth(const char* text, int depth) {
    return textSize(FontKind::Body, type::Label, text).x + px(kSubIndent) * float(std::max(0, depth));
}

 
std::string capsToTitle(const char* text) {
     
    if (!text) return {};
    static const char* const keep[] = {"TESTING", "INFO", "NULL", "JSON", "HTTP", "PS3MAPI", "UTF8", "VRAM", "XMB", "SPRX"};
    const std::string s = text;
    std::string out;
    size_t i = 0;
    bool changed = false;
    while (i < s.size()) {
        if (!std::isalpha(static_cast<unsigned char>(s[i]))) {
            out += s[i++];
            continue;
        }
        size_t j = i;
        while (j < s.size() && std::isalpha(static_cast<unsigned char>(s[j]))) ++j;
        std::string w = s.substr(i, j - i);
        bool allUpper = w.size() >= 4;
        for (char c : w)
            if (!std::isupper(static_cast<unsigned char>(c))) allUpper = false;
        const bool glued = (i > 0 && std::isdigit(static_cast<unsigned char>(s[i - 1]))) || (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j])));
        bool acr = false;
        for (const char* k : keep)
            if (w == k) acr = true;
        if (allUpper && !acr && !glued) {
            for (size_t n = 1; n < w.size(); ++n) w[n] = char(std::tolower(static_cast<unsigned char>(w[n])));
            changed = true;
        }
        out += w;
        i = j;
    }
    return changed ? titleCase(out) : out;
}

}   

 
 
 

void requestRedraw() { g_redraw = true; }

float animate(ImGuiID id, float target, float speed) {
    auto it = g_anim.find(id);
    if (it == g_anim.end() || reduceMotion()) {
        g_anim[id] = target;
        return target;
    }
    float& v = it->second;
    if (std::fabs(target - v) < 0.002f) {
        v = target;
        return v;
    }
    v += (target - v) * (1.0f - std::exp(-speed * frameDt()));
    requestRedraw();
    return v;
}

float animate(const char* strId, float target, float speed) { return animate(ImGui::GetID(strId), target, speed); }

ImVec4 animateColor(ImGuiID id, const ImVec4& target, float speed) {
    auto it = g_animColor.find(id);
    if (it == g_animColor.end() || reduceMotion()) {
        g_animColor[id] = target;
        return target;
    }
    ImVec4& v = it->second;
    const float d = std::fabs(v.x - target.x) + std::fabs(v.y - target.y) + std::fabs(v.z - target.z) + std::fabs(v.w - target.w);
    if (d < 0.004f) {
        v = target;
        return v;
    }
    v = mix(v, target, 1.0f - std::exp(-speed * frameDt()));
    requestRedraw();
    return v;
}

void notify(const std::string& text, Level level) { g_notes.push_back({text, level}); }

void flashSuccess(const std::string& key) {
    g_success[key] = ImGui::GetTime();
    requestRedraw();
}

void noteQueued(const std::string& label) {
    const EngineStatus st = Engine::get().status();
    if (st.state != EngineStatus::Connected || st.job.empty() || st.job == label) return;
    std::string running = st.job;
    if (running.size() > 40) running = running.substr(0, 38) + "...";
    notify("Queued behind " + running + ": Esc stops it", Level::Info);
}

std::string currentHost() { return g_host ? *g_host : std::string(); }

bool takeSearchFocus() {
    if (g_searchFocusFrame != ImGui::GetFrameCount()) return false;
    g_searchFocusFrame = -1;
    return true;
}

void trackActivity(const std::string& id, std::string labelText, Page page, std::function<bool()> running, std::function<void()> stop,
                   const ImVec4& color) {
    std::lock_guard<std::mutex> lock(g_activityMutex);
    for (detail::Activity& a : g_activities)
        if (a.id == id) {
            a.label = std::move(labelText);
            a.page = page;
            a.running = std::move(running);
            a.stop = std::move(stop);
            a.color = color;
            a.since = detail::activityClock();
            return;
        }
    g_activities.push_back({id, std::move(labelText), page, color, std::move(running), std::move(stop), detail::activityClock()});
}

void addPaletteProvider(PaletteProvider provider) { detail::paletteProviders().push_back(std::move(provider)); }

void copyToClipboard(const std::string& text, const char* what) {
    ImGui::SetClipboardText(text.c_str());
    notify(what ? std::string("Copied ") + what + "  " + (text.size() > 40 ? text.substr(0, 40) + "..." : text)
                : std::string("Copied to clipboard"),
           Level::Good);
}

 
 
 

std::string titleCase(const std::string& s) {
    static const char* const small[] = {"a", "an", "and", "as", "at", "by", "for", "from", "in", "into", "of", "on", "or", "per", "the", "to", "via", "vs", "with"};
    static const char* const units[] = {"px", "ms", "s", "m", "x"};
    std::string out = s;
    bool seenLetter = false;
    size_t i = 0;
    auto alpha = [](char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; };
    while (i < out.size()) {
        if (!alpha(out[i])) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < out.size() && (alpha(out[j]) || out[j] == '\'')) ++j;
        const char prev = i > 0 ? out[i - 1] : 0;
        const char next = j < out.size() ? out[j] : 0;
        const bool first = !seenLetter;
        seenLetter = true;
        const std::string w = out.substr(i, j - i);
        bool innerUpper = false, allUpper = true;
        for (size_t k = 0; k < w.size(); ++k) {
            const unsigned char c = static_cast<unsigned char>(w[k]);
            if (k > 0 && std::isupper(c)) innerUpper = true;
            if (std::islower(c)) allUpper = false;
        }
        const bool glued = std::isdigit(static_cast<unsigned char>(prev)) || std::isdigit(static_cast<unsigned char>(next)) || prev == '.' ||
                           prev == '_' || prev == '\\' || prev == '/' || next == '_' || (next == '.' && j + 1 < out.size() && alpha(out[j + 1]));
        if (glued || innerUpper || allUpper) {
            i = j;
            continue;
        }
        std::string lw = w;
        for (char& c : lw) c = char(std::tolower(static_cast<unsigned char>(c)));
        bool isUnit = false, isSmall = false;
        for (const char* u : units)
            if (lw == u) isUnit = true;
        for (const char* m : small)
            if (lw == m) isSmall = true;
        if (isUnit && prev == '(') {
            out.replace(i, w.size(), lw);
        } else if (prev == '-' && lw == "ons") {    
            out.replace(i, w.size(), lw);
        } else if (!first && isSmall && prev != '-') {
            out.replace(i, w.size(), lw);
        } else {
            out[i] = char(std::toupper(static_cast<unsigned char>(out[i])));
        }
        i = j;
    }
    return out;
}

std::string trimZeros(const std::string& s) {
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '.' && i > 0 && std::isdigit(static_cast<unsigned char>(s[i - 1])) && i + 1 < s.size() &&
            std::isdigit(static_cast<unsigned char>(s[i + 1]))) {
            size_t j = i + 1;
            while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) ++j;
            size_t end = j;
            while (end > i + 1 && s[end - 1] == '0') --end;
            if (end > i + 1) out.append(s, i, end - i);
            i = j;
            continue;
        }
        out += s[i++];
    }
    return out;
}

void title(const char* text) {
    FontScope f(FontKind::Semibold, type::H1);
    ImGui::TextUnformatted(text);
}

void heading(const char* text, const ImVec4& color) {
    FontScope f(FontKind::Semibold, type::H2);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

void label(const char* text, const ImVec4& color) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

void muted(const char* text) { label(text, pal::Muted); }

void caption(const char* text, const ImVec4& color) {
    FontScope f(FontKind::Body, type::Small);
    label(text, color);
}

void wrapped(const char* text, const ImVec4& color, float size) {
    FontScope f(FontKind::Body, size);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void labelf(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    vtext(FontKind::Body, type::Body, pal::Text, fmt, a);
    va_end(a);
}

void mutedf(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    vtext(FontKind::Body, type::Body, pal::Muted, fmt, a);
    va_end(a);
}

void captionf(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    vtext(FontKind::Body, type::Small, pal::Muted, fmt, a);
    va_end(a);
}

void mono(const char* text, const ImVec4& color, float size) {
    FontScope f(FontKind::Mono, size);
    label(text, color);
}

void monof(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    vtext(FontKind::Mono, type::Mono, pal::Text, fmt, a);
    va_end(a);
}

void fieldLabel(const char* text) {
     
     
    const std::string shown = capsToTitle(text);
    const ImVec2 ts = textSize(FontKind::Body, type::Caption, shown.c_str());
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(ts.x, ts.y));
    drawText(drawList(), FontKind::Body, type::Caption, p, u32(pal::Muted), shown.c_str());
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(3));
}

std::string ellipsize(const char* text, float maxWidth, FontKind kind, float em, bool middle) {
    if (textSize(kind, em, text).x <= maxWidth) return text;
    const std::string s(text);
    if (!middle) {
        const float dots = textSize(kind, em, "...").x;
        size_t n = s.size();
        while (n > 0 && textSize(kind, em, s.c_str(), s.c_str() + n).x + dots > maxWidth) --n;
        while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;    
        return s.substr(0, n) + "...";
    }
    size_t head = s.size() / 2, tail = s.size() - head;
    while (head + tail > 0) {
        const std::string t = s.substr(0, head) + "..." + s.substr(s.size() - tail);
        if (textSize(kind, em, t.c_str()).x <= maxWidth) return t;
        if (head >= tail && head > 0) --head;
        else if (tail > 0) --tail;
    }
    return "...";
}

void cellText(const char* text, const ImVec4& color, FontKind kind, float em, float rowHeight) {
    const float padY = ImGui::GetStyle().CellPadding.y;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float avail = ImGui::GetContentRegionAvail().x;
    const std::string shown = ellipsize(text, avail, kind, em);
    const ImVec2 ts = textSize(kind, em, shown.c_str());
     
    const float mid = p.y - padY + px(rowHeight) * 0.5f;
    const float baseline = std::floor(textTop(FontKind::Body, type::Label, mid) + ascent(FontKind::Body, type::Label) + 0.5f);
    drawText(drawList(), kind, em, ImVec2(p.x, baselineTop(kind, em, baseline)), u32(color), shown.c_str());
    ImGui::Dummy(ImVec2(std::min(ts.x, avail), 1));
}

bool copyable(const char* fullText, const char* what, const ImVec4& color) {
    ImGui::PushID(fullText);
    const float iconW = px(20);
    const std::string shownText = ellipsize(fullText, availWidth() - iconW - px(8), FontKind::Mono, type::Mono, true);
    const char* text = shownText.c_str();
    const ImVec2 ts = textSize(FontKind::Mono, type::Mono, text);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = std::max(ts.y, ImGui::GetFrameHeight() - px(8));
    const bool clicked = ImGui::InvisibleButton("##copy", ImVec2(ts.x + iconW + px(6), h));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* d = drawList();
    const float t = animate(ImGui::GetItemID(), hovered ? 1.0f : 0.0f, 20.0f);
    if (t > 0.01f)
        d->AddRectFilled(pos - ImVec2(px(5), px(1)), pos + ImVec2(ts.x + iconW + px(6), h + px(1)), u32(pal::Fill, t), px(5));
    drawText(d, FontKind::Mono, type::Mono, ImVec2(pos.x, pos.y + (h - ts.y) * 0.5f), u32(color), text);
    if (t > 0.01f) {
        const ImVec2 is = textSize(FontKind::Icons, 13, icon::Copy);
        drawText(d, FontKind::Icons, 13, ImVec2(pos.x + ts.x + px(7), pos.y + (h - is.y) * 0.5f), u32(pal::Muted, t), icon::Copy);
    }
    if (clicked) copyToClipboard(fullText, what);
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (shownText.size() != strlen(fullText)) tooltip(fullText);
    ImGui::PopID();
    return clicked;
}

bool hashLabel(uint32_t value, const char* what, const ImVec4& color) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%08X", value);
    return copyable(buf, what, color);
}

void keys(const char* chord, bool dim) {
    if (!chord || !*chord) return;
    ImDrawList* d = drawList();
    const char* p = chord;
    bool first = true;
    while (*p) {
        const char* e = strchr(p, '+');
        if (e == p) e = p + 1;             
        if (!e) e = p + strlen(p);
        if (!first) ImGui::SameLine(0, px(3));
        first = false;
        const ImVec2 ts = textSize(FontKind::Body, type::Caption, p, e);
        const float h = px(20);
        const ImVec2 size(std::max(h, ts.x + px(12)), h);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::Dummy(size);
        d->AddRectFilled(pos, pos + size, u32(ImVec4(1, 1, 1, dim ? 0.05f : 0.07f)), px(5));
        drawText(d, FontKind::Body, type::Caption, ImVec2(pos.x + (size.x - ts.x) * 0.5f, textTop(FontKind::Body, type::Caption, pos.y + size.y * 0.5f)),
                 u32(dim ? pal::Muted : pal::Text), p, e);
        p = *e ? e + 1 : e;
    }
}

 
 
 

void gap(float logical) {
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    w->DC.CursorPos.y += px(logical) - ImGui::GetStyle().ItemSpacing.y;
    w->DC.CursorMaxPos.y = std::max(w->DC.CursorMaxPos.y, w->DC.CursorPos.y - ImGui::GetStyle().ItemSpacing.y);
}

void hgap(float logical) { ImGui::SameLine(0, px(logical)); }

float availWidth() { return ImGui::GetContentRegionAvail().x; }

void alignRight(float width) {
    ImGui::SameLine();
    const float avail = ImGui::GetContentRegionAvail().x;
    if (avail > width) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - width);
}

void divider(float spacing) {
    gap(spacing * 0.5f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = availWidth();
    drawList()->AddRectFilled(ImVec2(p.x, p.y), ImVec2(p.x + w, p.y + 1.0f), u32(pal::Line));
    ImGui::Dummy(ImVec2(w, 1));
    gap(spacing * 0.5f);
}

float buttonWidth(const char* labelText, const char* iconGlyph, bool small) {
    const float em = small ? type::Caption : type::Label;
    const char* end = visibleEnd(labelText);
    float w = textSize(FontKind::Semibold, em, labelText, end).x;
    if (iconGlyph) w += textSize(FontKind::Icons, em * 1.08f, iconGlyph).x + (end > labelText ? px(7) : 0);
    return w + px(small ? 18 : 24);
}

 
 
 

void beginCard(const char* id, const char* titleText, const CardOptions& o) {
    IM_ASSERT(g_cardDepth < 8 && "cards nest at most 8 deep");
    CardFrame& f = g_cards[g_cardDepth++];
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    f.drawList = w->DrawList;
    f.min = ImGui::GetCursorScreenPos();
    f.width = o.width > 0 ? o.width : ImGui::GetContentRegionAvail().x;
    f.raised = o.raised;
    f.accent = o.accent;
    f.padX = px(o.raised ? 12 : sp::Card);
    f.padV = px(o.pad >= 0 ? o.pad : 12);
    f.caption = titleText && *titleText && !o.raised;
    const float capH = f.caption ? px(22) : 0.0f;
    f.panelTop = f.min.y + capH;
    ImGui::PushID(id);
    f.splitter.Split(f.drawList, 2);
    f.splitter.SetCurrentChannel(f.drawList, 1);
    f.savedWork = w->WorkRect;
    f.savedContent = w->ContentRegionRect;
    const float right = f.min.x + f.width - f.padX;
    w->WorkRect.Max.x = right;
    w->ContentRegionRect.Max.x = right;
    f.headerMax = ImVec2(f.min.x + f.width, f.panelTop);
    if (f.caption) {
         
        ImDrawList* d = f.drawList;
        const std::string cap = titleCase(titleText);
        const ImVec2 ts = textSize(FontKind::Body, type::Caption, cap.c_str());
        const float ty = f.panelTop - px(6) - ts.y;
        drawText(d, FontKind::Body, type::Caption, ImVec2(f.min.x + px(14), ty), u32(pal::Muted), cap.c_str());
        if (o.badge && *o.badge) drawBadgePill(d, ImVec2(f.min.x + px(14) + ts.x + px(8), ty + (ts.y - px(18)) * 0.5f), o.badge);
        if (o.subtitle && *o.subtitle && ImGui::IsWindowHovered() &&
            ImGui::IsMouseHoveringRect(ImVec2(f.min.x, f.min.y), ImVec2(f.min.x + px(14) + ts.x + px(4), f.panelTop)))
            ImGui::SetTooltip("%s", o.subtitle);
        f.headerRight = f.min.x + f.width - px(4);
        f.headerY = f.min.y;
    } else {
        f.headerRight = right;
        f.headerY = f.panelTop + std::max(f.padV, px(8));
    }
    ImGui::SetCursorScreenPos(ImVec2(f.min.x + f.padX, f.panelTop + f.padV));
    ImGui::BeginGroup();
    ImGui::PushItemWidth(-FLT_MIN);
    if (o.raised && titleText && *titleText) {
        ImDrawList* d = f.drawList;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const std::string t = titleCase(titleText);
        const ImVec2 ts = textSize(FontKind::Semibold, type::Label, t.c_str());
        drawText(d, FontKind::Semibold, type::Label, p, u32(pal::Text), t.c_str());
        if (o.badge && *o.badge) drawBadgePill(d, ImVec2(p.x + ts.x + px(8), p.y + (ts.y - px(18)) * 0.5f), o.badge);
        f.headerMax.y = p.y + ts.y;
        ImGui::Dummy(ImVec2(0, ts.y));
        gap(sp::S);
    }
}

void endCard() {
    IM_ASSERT(g_cardDepth > 0);
    {
         
         
        ImGuiWindow* cw = ImGui::GetCurrentWindow();
        if (cw->DC.IsSetPos && (cw->DC.CursorPos.y > cw->DC.CursorMaxPos.y || cw->DC.CursorPos.x > cw->DC.CursorMaxPos.x)) ImGui::Dummy(ImVec2(0, 0));
    }
    ImGui::PopItemWidth();
    ImGui::EndGroup();
    CardFrame& f = g_cards[--g_cardDepth];
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    const float bottom = std::max(ImGui::GetItemRectMax().y, f.panelTop + f.padV);
    const float h = std::max(bottom + f.padV - f.panelTop, px(8));
    w->WorkRect = f.savedWork;
    w->ContentRegionRect = f.savedContent;
    f.splitter.SetCurrentChannel(f.drawList, 0);
    const ImVec2 pmin(f.min.x, f.panelTop), pmax(f.min.x + f.width, f.panelTop + h);
    const float rounding = px(f.raised ? kRowRounding : kCardRounding);
    ImDrawList* d = f.drawList;
    d->AddRectFilled(pmin, pmax, u32(f.raised ? ImVec4(1, 1, 1, 0.035f) : pal::Surface), rounding);
    if (f.accent.w > 0) d->AddRectFilled(pmin, ImVec2(pmax.x, pmin.y + px(2)), u32(f.accent), rounding, ImDrawFlags_RoundCornersTop);
    f.splitter.Merge(d);
    ImGui::SetCursorScreenPos(f.min);
    ImGui::Dummy(ImVec2(f.width, pmax.y - f.min.y));
    ImGui::PopID();
}

bool cardHeaderHovered() {
    IM_ASSERT(g_cardDepth > 0);
    const CardFrame& f = g_cards[g_cardDepth - 1];
    return ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseHoveringRect(f.min, f.headerMax);
}

bool cardHeaderButton(const char* glyph, const char* tip, bool active) {
    IM_ASSERT(g_cardDepth > 0);
    CardFrame& f = g_cards[g_cardDepth - 1];
    const ImVec2 saved = ImGui::GetCursorScreenPos();
    const float size = f.caption ? 20.0f : 26.0f;
    f.headerRight -= px(size);
    ImGui::SetCursorScreenPos(ImVec2(f.headerRight, f.caption ? f.headerY : f.headerY - px(4)));
    const bool r = iconButton(glyph, glyph, tip, active, size);
    f.headerRight -= px(4);
    ImGui::SetCursorScreenPos(saved);
    return r;
}

 
 
 

bool button(const char* labelText, const ButtonOptions& o) {
    const float em = o.small ? type::Caption : type::Label;
    const char* end = visibleEnd(labelText);
    const bool hasText = end > labelText;
    const bool spin = o.busy || o.spinner;
    const float h = px(o.small ? kSmallControlH : kControlH);
    float w = o.width > 0 ? px(o.width) : o.width < 0 ? availWidth() : buttonWidth(labelText, o.icon || spin ? (o.icon ? o.icon : icon::Play) : nullptr, o.small);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(labelText, ImVec2(w, h), ImGuiButtonFlags_EnableNav);
    if (!ImGui::IsItemVisible()) return pressed && !o.busy;
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive() && hovered;
    const ImGuiID id = ImGui::GetItemID();
     
    bool flash = false;
    if (o.icon && !spin) {
        auto it = g_success.find(o.successKey ? std::string(o.successKey) : std::string(labelText));
        if (it != g_success.end() && ImGui::GetTime() - it->second < kSuccessFlash) {
            flash = true;
            requestRedraw();
        }
    }

    ImVec4 bg, fg;
    bool bold = false;
    const bool disabled = lastItemDisabled();
    const Tone tone = disabled && o.tone != Tone::Ghost ? Tone::Secondary : o.tone;
    auto white = [](float a) { return ImVec4(1, 1, 1, a); };
    if (o.selected && !disabled && (tone == Tone::Secondary || tone == Tone::Ghost)) {
        bg = held ? alpha(pal::Accent, 0.12f) : hovered ? alpha(pal::Accent, 0.20f) : pal::Selection;
        fg = pal::Accent;
        bold = true;
    } else switch (tone) {
    case Tone::Primary:
        bg = held ? pal::AccentActive : hovered ? pal::AccentHover : pal::Accent;
        fg = pal::Window;
        bold = true;
        break;
    case Tone::Ghost:
        bg = held ? white(0.05f) : hovered ? white(0.07f) : white(0.0f);
        fg = hovered ? pal::Text : pal::Muted;
        break;
    case Tone::Danger:
        bg = alpha(pal::DangerFill, held ? 0.30f : hovered ? 0.26f : 0.16f);
        fg = hovered ? rgb(0xFFB3AE) : pal::Red;
        break;
    case Tone::Success:
        bg = alpha(pal::Green, held ? 0.24f : hovered ? 0.20f : 0.12f);
        fg = pal::Green;
        break;
    default:
        bg = held ? white(0.05f) : hovered ? white(0.11f) : white(0.07f);
        fg = pal::Text;
        break;
    }
    bg = animateColor(id, bg, 22.0f);
    ImDrawList* d = drawList();
    const float r = px(kFrameRounding);
    const ImVec2 max = pos + ImVec2(w, h);
    if (bg.w > 0.002f) d->AddRectFilled(pos, max, u32(bg), r);
    navRing(pos, max, r);

    const FontKind fk = bold ? FontKind::Semibold : FontKind::Body;
    const ImVec2 ts = textSize(fk, em, labelText, end);
    const float iconW = o.icon || spin ? textSize(FontKind::Icons, em * 1.08f, o.icon ? o.icon : icon::Play).x : 0.0f;
    const float gapW = (iconW > 0 && hasText) ? px(7) : 0.0f;
    const float contentW = iconW + gapW + ts.x;
    float x = pos.x + std::max(px(6), (w - contentW) * 0.5f);
    const float cy = pos.y + h * 0.5f;
    if (spin) {
        drawSpinnerAt(d, ImVec2(x + iconW * 0.5f, cy), iconW * 0.42f, u32(fg), px(1.6f));
        x += iconW + gapW;
    } else if (o.icon) {
        const char* glyph = flash ? icon::Check : o.icon;
        const ImVec4 ic = flash && tone != Tone::Primary ? pal::Success : fg;
        const ImVec2 is = textSize(FontKind::Icons, em * 1.08f, glyph);
        drawText(d, FontKind::Icons, em * 1.08f, ImVec2(x + (iconW - is.x) * 0.5f, cy - is.y * 0.5f), u32(ic), glyph);
        x += iconW + gapW;
    }
    if (hasText) {
        ImGui::PushClipRect(pos, max, true);
        drawText(d, fk, em, ImVec2(x, textTop(fk, em, cy)), u32(fg), labelText, end);
        ImGui::PopClipRect();
    }
    if (hovered && !o.busy) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    tooltip(o.tooltip);
    return pressed && !o.busy;
}

bool primaryButton(const char* labelText, const char* glyph, float width) {
    return button(labelText, {.tone = Tone::Primary, .icon = glyph, .width = width});
}

bool dangerButton(const char* labelText, const char* glyph, float width) {
    return button(labelText, {.tone = Tone::Danger, .icon = glyph, .width = width});
}

bool iconButton(const char* id, const char* glyph, const char* tip, bool active, float size, const ImVec4& color) {
    const float s = px(size > 0 ? size : 30);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(7));
    const bool pressed = ImGui::InvisibleButton("##icon", ImVec2(s, s), ImGuiButtonFlags_EnableNav);
    ImGui::PopStyleVar();
    ImGui::PopID();
    if (!ImGui::IsItemVisible()) return pressed;
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive() && hovered;
    const ImVec4 bg = animateColor(ImGui::GetItemID(), held ? ImVec4(1, 1, 1, 0.05f) : hovered || active ? ImVec4(1, 1, 1, 0.07f) : ImVec4(1, 1, 1, 0.0f), 24.0f);
    ImDrawList* d = drawList();
    if (bg.w > 0.002f) d->AddRectFilled(pos, pos + ImVec2(s, s), u32(bg), px(7));
    const float em = std::min(15.0f, (size > 0 ? size : 30) * 0.5f);
    const ImVec2 is = textSize(FontKind::Icons, em, glyph);
    const ImVec4 fg = active || hovered ? pal::Text : color;
    drawText(d, FontKind::Icons, em, pos + ImVec2((s - is.x) * 0.5f, (s - is.y) * 0.5f), u32(fg), glyph);
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    tooltip(tip);
    return pressed;
}

 
 
 

void drawLight(ImDrawList* d, ImVec2 c, bool on, bool hovered, bool pending) {
    const float r = px(6.5f);
    if (on) {
         
        const float steps[4][2] = {{px(7.5f), 0.05f}, {px(5.5f), 0.07f}, {px(3.5f), 0.10f}, {px(1.8f), 0.16f}};
        for (const auto& s : steps) d->AddCircleFilled(c, r + s[0], u32(alpha(pal::Accent, s[1])), 32);
        d->AddCircleFilled(c, r, u32(pal::Accent), 32);
    } else {
        d->AddCircle(c, r - px(0.75f), u32(ImVec4(1, 1, 1, hovered ? 0.55f : 0.30f)), 32, px(1.5f));
    }
    if (pending) drawSpinnerAt(d, c, r + px(3.5f), u32(ImVec4(1, 1, 1, 0.6f)), px(1.3f));
}

bool toggleSwitch(const char* id, bool* on, bool pending) {
    const ImVec2 size(px(22), px(22));
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, size, ImGuiButtonFlags_EnableNav);
    if (clicked && !pending) *on = !*on;
    else clicked = false;
    if (ImGui::IsItemVisible()) {
        const bool hovered = ImGui::IsItemHovered();
        drawLight(drawList(), pos + size * 0.5f, *on, hovered, pending);
        navRing(pos, pos + size, size.x * 0.5f);
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    tooltip(nullptr);
    return clicked;
}

float hotkeyChipWidth(const char* binding) {
    float w = std::max(textSize(FontKind::Body, type::Caption, "Set Key").x, textSize(FontKind::Body, type::Caption, "Press Key").x);
    if (binding && *binding) w = std::max(w, textSize(FontKind::Body, type::Caption, binding).x);
    return w + px(14);
}

bool hotkeyChip(const char* id, ImVec2 pos, const char* binding, bool listening, bool visible) {
    const bool bound = binding && *binding;
    const float w = hotkeyChipWidth(binding), h = px(22);
    bool clicked = false;
    {
        Overlay overlay;
        ImGui::SetCursorScreenPos(pos);
        ImGui::PushID(id);
        clicked = ImGui::InvisibleButton("##chip", ImVec2(w, h));
        const bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        if (visible || hov || listening || bound) {
            ImDrawList* d = drawList();
            const char* text = listening ? "Press Key" : bound ? binding : "Set Key";
            const ImVec2 a = pos, b = pos + ImVec2(w, h);
            ImVec4 fg = pal::Faint;
            if (listening) {
                d->AddRectFilled(a, b, u32(alpha(pal::Accent, 0.12f)), px(5));
                d->AddRect(a, b, u32(pal::Accent), px(5), 0, 1.0f);
                fg = pal::Accent;
            } else if (bound) {
                d->AddRectFilled(a, b, u32(ImVec4(1, 1, 1, 0.07f)), px(5));
                if (hov) d->AddRect(a, b, u32(ImVec4(1, 1, 1, 0.28f)), px(5), 0, 1.0f);
                fg = pal::Muted;
            } else {
                d->AddRect(a, b, u32(ImVec4(1, 1, 1, hov ? 0.28f : 0.16f)), px(5), 0, 1.0f);
            }
            const ImVec2 ts = textSize(FontKind::Body, type::Caption, text);
            drawText(d, FontKind::Body, type::Caption, ImVec2(a.x + (w - ts.x) * 0.5f, textTop(FontKind::Body, type::Caption, a.y + h * 0.5f)), u32(fg), text);
        }
        if (hov) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("%s", listening ? "Press a key: Esc cancels, Backspace removes it" : bound ? "Click to change the hotkey" : "Click, then press a key");
        }
    }
    return clicked;
}

namespace {
 
float rowExtras(ImDrawList* d, float x, float cy, bool pending, bool sticky) {
    if (sticky) {
        x -= px(20);
        const ImVec2 is = textSize(FontKind::Icons, 12, icon::Repeat);
        drawText(d, FontKind::Icons, 12, ImVec2(x + (px(20) - is.x) * 0.5f, cy - is.y * 0.5f), u32(pal::Muted), icon::Repeat);
    }
    (void)pending;
    return x;
}
}   

bool toggleRow(const char* id, const char* labelText, bool* on, const RowOptions& o) {
    ImGui::PushID(id);
    const float w = availWidth();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = px(kRowHeight);
    ImDrawList* d = drawList();
    const bool hasChip = o.chip || (o.hotkey && *o.hotkey);
    const float chipW = hasChip ? hotkeyChipWidth(o.hotkey) : 0.0f;
    float rightW = px(19) + (hasChip ? chipW + px(10) : 0.0f) + (o.sticky ? px(20) : 0.0f);
    const float badgeW = o.badge && *o.badge ? badgePillWidth(o.badge) + px(8) : 0.0f;
    const float labelRoom = std::max(px(40), w - rightW - px(12) - badgeW);

    rowSeparator(d, pos, w);
    if (hasChip) ImGui::SetNextItemAllowOverlap();
    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
    bool clicked = ImGui::InvisibleButton("##row", ImVec2(w, h), ImGuiButtonFlags_EnableNav);
    ImGui::PopStyleColor();
    const ImGuiID rowId = ImGui::GetItemID();
    const bool rowHover = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                          ImGui::IsMouseHoveringRect(pos, pos + ImVec2(w, h));
    const float cy = pos.y + h * 0.5f;
    if (ImGui::IsItemVisible()) {
        const float hv = animate(rowId + 1, rowHover ? 1.0f : 0.0f, 22.0f);
        const float hl = animate(rowId + 2, o.highlight ? 1.0f : 0.0f, o.highlight ? 40.0f : 2.6f);
        rowPlate(d, pos, w, h, hv, hl);
        navRing(pos - ImVec2(px(6), -px(3)), pos + ImVec2(w + px(6), h - px(3)), px(7));
        const float textAlpha = lastItemDisabled() ? 0.85f / ImGui::GetStyle().DisabledAlpha : 1.0f;    
        rowLabel(d, pos, h, labelRoom, labelText, o.depth, textAlpha);
        if (badgeW > 0) {
            const float lx = std::min(pos.x + labelWidth(labelText, o.depth), pos.x + labelRoom);
            drawBadgePill(d, ImVec2(lx + px(8), cy - px(9)), o.badge);
        }
        const float lightX = pos.x + w - px(19) * 0.5f;
        const float t = animate(rowId + 3, *on ? 1.0f : 0.0f, 24.0f);
        drawLight(d, ImVec2(lightX, cy), t > 0.5f, rowHover, o.pending);
        rowExtras(d, pos.x + w - px(19) - (hasChip ? chipW + px(10) : 0.0f), cy, o.pending, o.sticky);
        if (ImGui::IsItemHovered() && !o.pending) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    if (clicked && !o.pending) *on = !*on;
    else clicked = false;
    tooltip(o.help);
    if (hasChip) {
        const bool chipClick = hotkeyChip("##hk", ImVec2(pos.x + w - px(19) - px(10) - chipW, cy - px(11)), o.hotkey, o.listening,
                                          o.chipAlways || rowHover);
        if (chipClick && o.chipClicked) *o.chipClicked = true;
    }
    rowEnded(pos, h);
    ImGui::PopID();
    return clicked;
}

bool actionRow(const char* id, const char* labelText, const char* buttonLabel, const RowOptions& o) {
    ImGui::PushID(id);
    const float w = availWidth();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = px(kRowHeight);
    const float cy = pos.y + h * 0.5f;
    ImDrawList* d = drawList();
    rowSeparator(d, pos, w);
    const std::string bl = std::string(buttonLabel && *buttonLabel ? buttonLabel : "Run") + "##act";
    const float bw = buttonWidth(bl.c_str(), nullptr, true);
    const bool hasChip = o.chip || (o.hotkey && *o.hotkey);
    const float chipW = hasChip ? hotkeyChipWidth(o.hotkey) : 0.0f;
    const float rightW = bw + (hasChip ? chipW + px(8) : 0.0f) + (o.sticky ? px(20) : 0.0f);
    const bool rowHover = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                          ImGui::IsMouseHoveringRect(pos, pos + ImVec2(w, h));
    const ImGuiID rowId = ImGui::GetID("##row");
    rowPlate(d, pos, w, h, 0.0f, animate(rowId + 2, o.highlight ? 1.0f : 0.0f, o.highlight ? 40.0f : 2.6f));
    const float textAlpha = scopeDisabled() ? 0.85f / ImGui::GetStyle().DisabledAlpha : 1.0f;
    const float badgeW = o.badge && *o.badge ? badgePillWidth(o.badge) + px(8) : 0.0f;
    const float labelRoom = std::max(px(40), w - rightW - px(12) - badgeW);
    rowLabel(d, pos, h, labelRoom, labelText, o.depth, textAlpha);
    if (badgeW > 0) drawBadgePill(d, ImVec2(std::min(pos.x + labelWidth(labelText, o.depth), pos.x + labelRoom) + px(8), cy - px(9)), o.badge);
    ImGui::SetCursorScreenPos(ImVec2(pos.x + w - bw, cy - px(kSmallControlH) * 0.5f));
    const bool pressed = button(bl.c_str(), {.small = true, .busy = o.pending, .successKey = id});
    if (hasChip) {
        const bool chipClick = hotkeyChip("##hk", ImVec2(pos.x + w - bw - px(8) - chipW, cy - px(11)), o.hotkey, o.listening, true);
        if (chipClick && o.chipClicked) *o.chipClicked = true;
    }
    rowExtras(d, pos.x + w - rightW + (o.sticky ? px(20) : 0.0f), cy, false, o.sticky);
     
    ImGui::SetCursorScreenPos(pos);
    const ImRect row(pos, pos + ImVec2(w, h));
    ImGui::ItemSize(row.GetSize());
    ImGui::ItemAdd(row, rowId, nullptr, ImGuiItemFlags_NoNav);
    (void)rowHover;
    tooltip(o.help);
    rowEnded(pos, h);
    ImGui::PopID();
    return pressed;
}

namespace {
struct OpenRow {
    ImVec2 pos;
    float w, h;
    ImGuiID id;
    const char* help;
};
std::vector<OpenRow> g_openRows;
}   

void beginRow(const char* id, const char* labelText, float controlWidth, const RowOptions& o) {
    ImGui::PushID(id);
    const float w = availWidth();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = px(kRowHeight);
    const float cy = pos.y + h * 0.5f;
    ImDrawList* d = drawList();
    rowSeparator(d, pos, w);
    const ImGuiID rowId = ImGui::GetID("##row");
    rowPlate(d, pos, w, h, 0.0f, animate(rowId + 2, o.highlight ? 1.0f : 0.0f, o.highlight ? 40.0f : 2.6f));
    const float cw = controlWidth > 0 ? controlWidth : std::min(px(kControlColumn), w * 0.6f);
    const float textAlpha = scopeDisabled() ? 0.85f / ImGui::GetStyle().DisabledAlpha : 1.0f;
    const float badgeW = o.badge && *o.badge ? badgePillWidth(o.badge) + px(8) : 0.0f;
    const float labelRoom = std::max(px(40), w - cw - px(12) - badgeW);
    rowLabel(d, pos, h, labelRoom, labelText, o.depth, textAlpha);
    if (badgeW > 0) drawBadgePill(d, ImVec2(std::min(pos.x + labelWidth(labelText, o.depth), pos.x + labelRoom) + px(8), cy - px(9)), o.badge);
    g_openRows.push_back({pos, w, h, rowId, o.help});
    ImGui::SetCursorScreenPos(ImVec2(pos.x + w - cw, cy - px(kControlH) * 0.5f));
    ImGui::BeginGroup();
}

void endRow() {
    IM_ASSERT(!g_openRows.empty());
    ImGui::EndGroup();
    const OpenRow r = g_openRows.back();
    g_openRows.pop_back();
    ImGui::SetCursorScreenPos(r.pos);
    const ImRect row(r.pos, r.pos + ImVec2(r.w, r.h));
    ImGui::ItemSize(row.GetSize());
    ImGui::ItemAdd(row, r.id, nullptr, ImGuiItemFlags_NoNav);
    tooltip(r.help);
    rowEnded(r.pos, r.h);
    ImGui::PopID();
}

Edit slider(const char* id, float* v, float minV, float maxV, float step, float width, bool logScale) {
    const float w = width > 0 ? px(width) : availWidth();
    const float h = px(22);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(w, h), ImGuiButtonFlags_EnableNav);
    const ImGuiID itemId = ImGui::GetItemID();
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const float inset = px(7);
    const float range = maxV - minV;
    const bool logs = logScale && minV > 0 && maxV > minV;
    const float logRange = logs ? std::log(maxV / minV) : 0.0f;
     
    auto toValue = [&](float t) { return logs ? minV * std::exp(t * logRange) : minV + t * range; };
    auto toTrack = [&](float val) {
        if (range <= 0) return 0.0f;
        if (logs) return std::clamp(std::log(std::max(val, minV) / minV) / logRange, 0.0f, 1.0f);
        return std::clamp((val - minV) / range, 0.0f, 1.0f);
    };
    Edit result = Edit::None;
    auto quantize = [&](float nv) {
        nv = std::clamp(nv, minV, maxV);
        if (step > 0) nv = std::clamp(minV + std::round((nv - minV) / step) * step, minV, maxV);
        return nv;
    };
    if (active && range > 0) {
        const float t = std::clamp((ImGui::GetIO().MousePos.x - pos.x - inset) / std::max(1.0f, w - inset * 2), 0.0f, 1.0f);
        const float nv = quantize(toValue(t));
        if (nv != *v) {
            *v = nv;
            ImGui::MarkItemEdited(itemId);
            result = Edit::Changing;
        }
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) result = Edit::Committed;
    if (ImGui::IsItemFocused() && !active && range > 0) {
        float nv = *v;
        const bool down = ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow);
        const bool up = ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow);
        if (logs && step <= 0) {    
            if (down) nv = toValue(toTrack(nv) - 0.01f);
            if (up) nv = toValue(toTrack(nv) + 0.01f);
        } else {
            const float inc = step > 0 ? step : range / 100.0f;
            if (down) nv -= inc;
            if (up) nv += inc;
        }
        nv = quantize(nv);
        if (nv != *v) {
            *v = nv;
            result = Edit::Committed;
        }
    }
    if (ImGui::IsItemVisible()) {
        ImDrawList* d = drawList();
        const float t = toTrack(*v);
        const float shown = animate(itemId + 7, t, active ? 60.0f : 18.0f);
        const float cy = pos.y + h * 0.5f;
        const float x0 = pos.x + inset, x1 = pos.x + w - inset;
        const float th = px(4);
        d->AddRectFilled(ImVec2(x0, cy - th * 0.5f), ImVec2(x1, cy + th * 0.5f), u32(pal::FillActive), px(2));
        const float kx = x0 + (x1 - x0) * shown;
        if (kx > x0) d->AddRectFilled(ImVec2(x0, cy - th * 0.5f), ImVec2(kx, cy + th * 0.5f), u32(pal::Accent), px(2));
        const float grow = animate(itemId + 8, active || hovered ? 1.0f : 0.0f, 22.0f);
        const float kr = px(7) + px(1) * grow;
        d->AddCircleFilled(ImVec2(kx, cy + px(0.5f)), kr + px(1), u32(ImVec4(0, 0, 0, 0.28f)), 24);
        d->AddCircleFilled(ImVec2(kx, cy), kr, u32(ImVec4(1, 1, 1, 1)), 24);
        navRing(pos, pos + ImVec2(w, h), h * 0.5f);
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    tooltip(nullptr);
    return result;
}

namespace {
std::string numberText(float v, const char* format) {
    char buf[64];
    snprintf(buf, sizeof(buf), format && *format ? format : "%.2f", double(v));
    return trimZeros(buf);
}
}   

Edit valueControl(const char* id, float* v, const ValueOptions& o, float width) {
    ImGui::PushID(id);
    const float w = width > 0 ? width : std::min(px(kControlColumn), availWidth());
    const float h = px(kControlH);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float cy = pos.y + h * 0.5f;
    ImDrawList* d = drawList();
    Edit result = Edit::None;
    const bool hasUnit = o.unit && *o.unit;
    const float unitW = hasUnit ? textSize(FontKind::Body, type::Label, o.unit).x + px(5) : 0.0f;
    const std::string text = numberText(*v, o.format);
    const float boxW = std::max({px(46), textSize(FontKind::Body, type::Label, numberText(o.max, o.format).c_str()).x + px(4),
                                 textSize(FontKind::Body, type::Label, numberText(o.min, o.format).c_str()).x + px(4)});
    const float trackW = std::max(px(20), w - boxW - unitW - px(4));
    const float textAlpha = scopeDisabled() ? 0.85f / ImGui::GetStyle().DisabledAlpha : 1.0f;

     
    ImGui::SetCursorScreenPos(ImVec2(pos.x, cy - px(11)));
    const Edit s = slider("##track", v, o.min, o.max, o.integer ? std::max(1.0f, o.step) : o.step, trackW / scale(), o.logScale);
    if (s != Edit::None) result = s;

     
    const ImVec2 boxMin(pos.x + w - unitW - boxW, cy - px(12));
    const ImVec2 boxMax(boxMin.x + boxW, cy + px(12));
    const ImGuiID editId = ImGui::GetID("##num");
    auto edit = g_numberEdits.find(editId);
    if (edit == g_numberEdits.end()) {
        ImGui::SetCursorScreenPos(boxMin);
        if (ImGui::InvisibleButton("##numbtn", boxMax - boxMin)) {
            g_numberEdits[editId] = {text, ImGui::GetFrameCount(), true};
        }
        const bool hov = ImGui::IsItemHovered();
        if (hov) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
            d->AddRectFilled(boxMin - ImVec2(px(4), 0), boxMax + ImVec2(px(2), 0), u32(pal::Fill), px(5));
        }
        tooltip(nullptr);
        const ImVec2 ts = textSize(FontKind::Body, type::Label, text.c_str());
        drawText(d, FontKind::Body, type::Label, ImVec2(boxMax.x - ts.x, textTop(FontKind::Body, type::Label, cy)), u32(pal::Text, textAlpha), text.c_str());
    } else {
        NumberEdit& e = edit->second;
        FontScope f(FontKind::Body, type::Label);
        const float tw = std::max(boxW, textSize(FontKind::Body, type::Label, e.text.c_str()).x + px(8));
        ImGui::SetCursorScreenPos(ImVec2(boxMax.x - tw, boxMin.y));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1, 1, 1, 0.08f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1, 1, 1, 0.08f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(1, 1, 1, 0.08f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(5));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(3), std::max(0.0f, (px(24) - ImGui::GetFontSize()) * 0.5f)));
        ImGui::SetNextItemWidth(tw + px(2));
        if (e.focus) {
            ImGui::SetKeyboardFocusHere();
            e.focus = false;
        }
        ImGui::InputText("##numedit", e.text.data(), e.text.capacity() + 1, ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_CharsScientific | ImGuiInputTextFlags_AutoSelectAll,
                         stringResize, &e.text);
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(3);
        const bool active = ImGui::IsItemActive();
        if (ImGui::IsItemDeactivated() || (!active && ImGui::GetFrameCount() > e.startFrame + 2)) {
            const bool cancel = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
            if (!cancel) {
                char* endp = nullptr;
                const float parsed = std::strtof(e.text.c_str(), &endp);
                if (endp && endp != e.text.c_str() && std::isfinite(parsed)) {
                    float nv = std::clamp(parsed, o.min, o.max);
                    if (o.step > 0) nv = std::clamp(o.min + std::round((nv - o.min) / o.step) * o.step, o.min, o.max);
                    if (o.integer && std::ceil(o.min) <= std::floor(o.max)) nv = std::clamp(std::round(nv), std::ceil(o.min), std::floor(o.max));
                    *v = nv;
                    result = Edit::Committed;
                }
            }
            g_numberEdits.erase(editId);
        }
    }
    if (hasUnit) drawText(d, FontKind::Body, type::Label, ImVec2(pos.x + w - unitW + px(5), textTop(FontKind::Body, type::Label, cy)), u32(pal::Muted, textAlpha), o.unit);
    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy(ImVec2(w, h));
    ImGui::PopID();
    return result;
}

Edit valueRow(const char* id, const char* labelText, float* v, const ValueOptions& o) {
    ImGui::PushID(id);
    const float w = availWidth();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = px(kRowHeight);
    const float cy = pos.y + h * 0.5f;
    ImDrawList* d = drawList();
    rowSeparator(d, pos, w);
    const ImGuiID rowId = ImGui::GetID("##row");
    rowPlate(d, pos, w, h, 0.0f, animate(rowId + 2, o.highlight ? 1.0f : 0.0f, o.highlight ? 40.0f : 2.6f));
    const float ctlW = std::min(px(kControlColumn), std::max(px(120), w * 0.55f));
    const bool hasCap = o.hotkey && *o.hotkey;
    const float capW = hasCap ? hotkeyChipWidth(o.hotkey) + px(10) : 0.0f;
    const float extrasW = (o.sticky ? px(20) : 0.0f) + (o.pending ? px(20) : 0.0f) + capW;
    const float badgeW = o.badge && *o.badge ? badgePillWidth(o.badge) + px(8) : 0.0f;
    const float labelRoom = std::max(px(40), w - ctlW - extrasW - px(12) - badgeW);
    const float textAlpha = scopeDisabled() ? 0.85f / ImGui::GetStyle().DisabledAlpha : 1.0f;
    rowLabel(d, pos, h, labelRoom, labelText, o.depth, textAlpha);
    if (badgeW > 0) drawBadgePill(d, ImVec2(std::min(pos.x + labelWidth(labelText, o.depth), pos.x + labelRoom) + px(8), cy - px(9)), o.badge);

    ImGui::SetCursorScreenPos(ImVec2(pos.x + w - ctlW, cy - px(kControlH) * 0.5f));
    const Edit result = valueControl("##ctl", v, o, ctlW);
    float x = pos.x + w - ctlW - px(8);
    if (o.pending) {
        x -= px(20);
        drawSpinnerAt(d, ImVec2(x + px(10), cy), px(5.5f), u32(pal::Muted), px(1.5f));
    }
    x = rowExtras(d, x, cy, false, o.sticky);
    if (hasCap) {
        x -= capW;
        hotkeyChip("##hk", ImVec2(x, cy - px(11)), o.hotkey, false, true);
    }

     
    ImGui::SetCursorScreenPos(pos);
    const ImRect row(pos, pos + ImVec2(w, h));
    ImGui::ItemSize(row.GetSize());
    ImGui::ItemAdd(row, rowId, nullptr, ImGuiItemFlags_NoNav);
    tooltip(o.help);
    rowEnded(pos, h);
    ImGui::PopID();
    return result;
}

bool inputText(const char* id, std::string* s, const char* hint, float width, ImGuiInputTextFlags flags) {
    FieldStyle fs(false);
    ImGui::SetNextItemWidth(width > 0 ? px(width) : width < 0 ? availWidth() : -FLT_MIN);
    const bool r = ImGui::InputTextWithHint(id, hint ? hint : "", s->data(), s->capacity() + 1,
                                            flags | ImGuiInputTextFlags_CallbackResize, stringResize, s);
    fieldRing();
    tooltip(nullptr);
    return r;
}

bool inputMono(const char* id, std::string* s, const char* hint, float width, ImGuiInputTextFlags flags) {
    FieldStyle fs(true);
    ImGui::SetNextItemWidth(width > 0 ? px(width) : width < 0 ? availWidth() : -FLT_MIN);
    const bool r = ImGui::InputTextWithHint(id, hint ? hint : "", s->data(), s->capacity() + 1,
                                            flags | ImGuiInputTextFlags_CallbackResize, stringResize, s);
    fieldRing();
    tooltip(nullptr);
    return r;
}

bool searchField(const char* id, std::string* s, const char* hint, float width, bool browseKeys) {
    const float w = width > 0 ? px(width) : availWidth();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = px(kControlH);
    const float iconPad = px(30);
    const float clearW = s->empty() ? 0.0f : px(24);
    ImDrawList* d = drawList();
    ImGui::PushID(id);
    const ImGuiID frameId = ImGui::GetID("##frame");
    ImDrawListSplitter split;
    split.Split(d, 2);
    split.SetCurrentChannel(d, 1);
    ImGui::SetCursorScreenPos(ImVec2(pos.x + iconPad, pos.y));
    FontScope fsc(FontKind::Body, type::Label);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, pal::Faint);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, (h - ImGui::GetFontSize()) * 0.5f));
    ImGui::SetNextItemWidth(w - iconPad - clearW - px(6));
    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
    if (ImGui::GetFrameCount() == g_searchFocusFrame) {
        ImGui::SetKeyboardFocusHere();
        g_searchFocusFrame = -1;
    }
    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize | (browseKeys ? ImGuiInputTextFlags_CallbackHistory : 0);
    const bool changed = ImGui::InputTextWithHint("##input", hint ? hint : "", s->data(), s->capacity() + 1, flags, stringResize, s);
    ImGui::PopStyleColor();
    const bool active = ImGui::IsItemActive();
    const bool hovered = ImGui::IsItemHovered();
    bool cleared = false;
    if (active && ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !s->empty()) {
        s->clear();
        cleared = true;
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    if (!s->empty()) {
        ImGui::SetCursorScreenPos(ImVec2(pos.x + w - clearW - px(3), pos.y + (h - px(22)) * 0.5f));
        if (iconButton("##clear", icon::Close, "Clear", false, 22)) {
            s->clear();
            cleared = true;
        }
    }
    split.SetCurrentChannel(d, 0);
    const float t = animate(frameId, active ? 1.0f : hovered ? 0.5f : 0.0f, 20.0f);
    d->AddRectFilled(pos, pos + ImVec2(w, h), u32(ImVec4(1, 1, 1, 0.06f + 0.07f * t)), px(kFrameRounding));
    const ImVec2 is = textSize(FontKind::Icons, 13, icon::Search);
    drawText(d, FontKind::Icons, 13, ImVec2(pos.x + px(10), pos.y + (h - is.y) * 0.5f), u32(active ? pal::Text : pal::Muted), icon::Search);
    split.Merge(d);
    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy(ImVec2(w, h));
    ImGui::PopID();
    return changed || cleared;
}

bool inputHex(const char* id, uint32_t* v, float width, int digits) {
    FieldStyle fs(true);
    ImGui::SetNextItemWidth(width > 0 ? px(width) : width < 0 ? availWidth() : px(110));
    char fmt[8];
    snprintf(fmt, sizeof(fmt), "%%0%dX", std::clamp(digits, 1, 8));
    const bool r = ImGui::InputScalar(id, ImGuiDataType_U32, v, nullptr, nullptr, fmt,
                                      ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_CharsUppercase);
    fieldRing();
    tooltip(nullptr);
    return r;
}

namespace {
 
bool squareButton(const char* id, const char* glyph, float size) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(size, size), ImGuiButtonFlags_EnableNav);
    const bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive() && hovered;
    ImDrawList* d = drawList();
    d->AddRectFilled(pos, pos + ImVec2(size, size), u32(ImVec4(1, 1, 1, held ? 0.05f : hovered ? 0.11f : 0.07f)), px(kFrameRounding));
    const ImVec2 is = textSize(FontKind::Icons, 11, glyph);
    drawText(d, FontKind::Icons, 11, pos + ImVec2((size - is.x) * 0.5f, (size - is.y) * 0.5f), u32(pal::Text), glyph);
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return pressed;
}
}   

bool inputInt(const char* id, int* v, int minV, int maxV, float width, int step) {
    ImGui::PushID(id);
    const float h = px(kControlH);
    const float total = width > 0 ? px(width) : px(130);
    const float bw = h;
    bool changed = false;
    {
        FieldStyle fs(true);
        ImGui::SetNextItemWidth(std::max(px(30), total - bw * 2 - px(8)));
        int tmp = *v;
        if (ImGui::InputInt("##value", &tmp, 0, 0)) {
            *v = std::clamp(tmp, minV, maxV);
            changed = true;
        }
        if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0 && !lastItemDisabled()) {
            *v = std::clamp(*v + (ImGui::GetIO().MouseWheel > 0 ? step : -step), minV, maxV);
            changed = true;
            ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        }
    }
    ImGui::SameLine(0, px(4));
    if (squareButton("##dec", icon::Remove, bw)) {
        *v = std::max(minV, *v - step);
        changed = true;
    }
    ImGui::SameLine(0, px(4));
    if (squareButton("##inc", icon::Add, bw)) {
        *v = std::min(maxV, *v + step);
        changed = true;
    }
    ImGui::PopID();
    return changed;
}

bool inputFloat(const char* id, float* v, const char* format, float width) {
    FieldStyle fs(true);
    ImGui::SetNextItemWidth(width > 0 ? px(width) : width < 0 ? availWidth() : px(110));
    const bool r = ImGui::InputScalar(id, ImGuiDataType_Float, v, nullptr, nullptr, format ? format : "%.2f");
    fieldRing();
    tooltip(nullptr);
    return r;
}

bool checkbox(const char* labelText, bool* v) {
    const float box = px(20);
    const char* end = visibleEnd(labelText);
    const ImVec2 ts = textSize(FontKind::Body, type::Label, labelText, end);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = std::max(box, ts.y);
    const bool clicked = ImGui::InvisibleButton(labelText, ImVec2(box + (end > labelText ? px(8) + ts.x : 0), h), ImGuiButtonFlags_EnableNav);
    if (clicked) *v = !*v;
    if (ImGui::IsItemVisible()) {
        const bool hovered = ImGui::IsItemHovered();
        ImDrawList* d = drawList();
        const ImVec2 bp(pos.x, pos.y + (h - box) * 0.5f);
        d->AddRectFilled(bp, bp + ImVec2(box, box), u32(hovered ? pal::FillHover : pal::Fill), px(5));
        if (*v) {
             
            const ImVec2 a = bp + ImVec2(box * 0.27f, box * 0.52f), b = bp + ImVec2(box * 0.44f, box * 0.68f), c = bp + ImVec2(box * 0.74f, box * 0.34f);
            d->AddLine(a, b, u32(pal::Text), px(1.6f));
            d->AddLine(b, c, u32(pal::Text), px(1.6f));
        }
        navRing(bp, bp + ImVec2(box, box), px(5));
        if (end > labelText) drawText(d, FontKind::Body, type::Label, ImVec2(bp.x + box + px(8), pos.y + (h - ts.y) * 0.5f), u32(pal::Text), labelText, end);
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    tooltip(nullptr);
    return clicked;
}

namespace {
void popupListStyleBegin() {
    ImGui::PushStyleColor(ImGuiCol_PopupBg, pal::RaisedFill);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 0.08f));
    ImGui::PushStyleColor(ImGuiCol_Header, pal::Selection);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, pal::Fill);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, pal::Selection);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, px(10));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(4), px(4)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(px(4), px(0)));
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableRounding, px(5));
}
void popupListStyleEnd() {
    ImGui::PopStyleVar(5);
    ImGui::PopStyleColor(5);
}
}   

bool combo(const char* id, int* current, const char* const items[], int count, float width) {
    FieldStyle fs(false);
    popupListStyleBegin();
    ImGui::SetNextItemWidth(width > 0 ? px(width) : width < 0 ? availWidth() : px(180));
    bool changed = false;
    const char* preview = (*current >= 0 && *current < count) ? items[*current] : "";
    const bool open = ImGui::BeginCombo(id, preview, ImGuiComboFlags_HeightLarge | ImGuiComboFlags_NoArrowButton);
    const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
    const bool hovered = ImGui::IsItemHovered();
    if (open) {
        for (int i = 0; i < count; ++i) {
            const bool sel = i == *current;
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(8), px(4)));
            if (ImGui::Selectable(items[i], sel, 0, ImVec2(0, px(26)))) {
                *current = i;
                changed = true;
            }
            ImGui::PopStyleVar();
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    {
         
        const float aw = mx.y - mn.y;
        ImDrawList* d = drawList();
        d->AddRectFilled(ImVec2(mx.x - aw, mn.y), mx, u32(ImVec4(1, 1, 1, open || hovered ? 0.05f : 0.02f)), px(kFrameRounding), ImDrawFlags_RoundCornersRight);
        const ImVec2 cs = textSize(FontKind::Icons, 9, icon::ChevronDown);
        drawText(d, FontKind::Icons, 9, ImVec2(mx.x - aw + (aw - cs.x) * 0.5f, (mn.y + mx.y - cs.y) * 0.5f), u32(pal::Muted), icon::ChevronDown);
    }
    popupListStyleEnd();
    tooltip(nullptr);
    return changed;
}

namespace {
 
 
bool segImpl(const char* id, const std::vector<std::string>& tabs, int* current, bool equal, float total, float padX) {
    if (tabs.empty()) return false;
    ImGui::PushID(id);
    const float h = px(kControlH), inner = px(2), rowGap = px(6);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float avail = std::max(px(80), total > 0 ? total : availWidth());
    const size_t n = tabs.size();
    std::vector<std::string> shown(n);
    std::vector<float> textW(n), widths(n), xs(n);
    std::vector<int> rowOf(n);
    float textTotal = 0;
    for (size_t i = 0; i < n; ++i) {
        shown[i] = titleCase(tabs[i]);
        textW[i] = textSize(FontKind::Semibold, type::Label, shown[i].c_str()).x;
        textTotal += textW[i];
    }
    std::vector<float> rowW{inner};
    if (equal) {
         
        const float segW = (avail - inner * 2) / float(n);
        bool fits = true;
        for (size_t i = 0; i < n; ++i)
            if (textW[i] + px(14) > segW) fits = false;
        const float extra = std::max(0.0f, (avail - inner * 2 - textTotal) / float(n));
        float x = inner;
        for (size_t i = 0; i < n; ++i) {
            widths[i] = fits ? segW : textW[i] + extra;
            xs[i] = x;
            x += widths[i];
            rowOf[i] = 0;
        }
        rowW[0] = avail - inner;
    } else {
        float pad = px(padX);
        if (inner * 2 + textTotal + pad * 2 * float(n) > avail) pad = std::max(px(8), (avail - inner * 2 - textTotal) / (2.0f * float(n)));
        for (size_t i = 0; i < n; ++i) {
            widths[i] = textW[i] + pad * 2;
            float& x = rowW.back();
            if (x > inner && x + widths[i] + inner > avail) rowW.push_back(inner);
            rowOf[i] = int(rowW.size()) - 1;
            xs[i] = rowW.back();
            rowW.back() += widths[i];
        }
    }
    float maxW = 0;
    for (float& w : rowW) {
        w += inner;
        maxW = std::max(maxW, w);
    }
    const int rows = int(rowW.size());
    auto rowY = [&](int r) { return pos.y + float(r) * (h + rowGap); };
    ImDrawList* d = drawList();
    for (int r = 0; r < rows; ++r) {
        const ImVec2 p0(pos.x, rowY(r));
        d->AddRectFilled(p0, p0 + ImVec2(rowW[size_t(r)], h), u32(pal::Fill), px(8));
    }
    bool changed = false;
    float selX = xs[0], selY = 0, selW = widths[0];
    bool any = false;
    ImDrawListSplitter split;
    split.Split(d, 2);
    split.SetCurrentChannel(d, 1);
    for (size_t i = 0; i < n; ++i) {
        const float x = pos.x + xs[i], y = rowY(rowOf[i]);
        ImGui::SetCursorScreenPos(ImVec2(x, y + inner));
        ImGui::PushID(int(i));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(6));
        if (ImGui::InvisibleButton("##tab", ImVec2(std::max(1.0f, widths[i]), h - inner * 2), ImGuiButtonFlags_EnableNav) && *current != int(i)) {
            *current = int(i);
            changed = true;
        }
        ImGui::PopStyleVar();
        const bool hovered = ImGui::IsItemHovered();
        const bool sel = *current == int(i);
        if (sel) {
            selX = xs[i];
            selY = y - pos.y;
            selW = widths[i];
            any = true;
        }
        const FontKind fk = sel ? FontKind::Semibold : FontKind::Body;
        const ImVec4 fg = sel || hovered ? pal::Text : pal::Muted;
        const std::string t = ellipsize(shown[i].c_str(), widths[i] - px(8), fk, type::Label);
        const float tw = textSize(fk, type::Label, t.c_str()).x;
        drawText(d, fk, type::Label, ImVec2(x + (widths[i] - tw) * 0.5f, textTop(fk, type::Label, y + h * 0.5f)), u32(fg), t.c_str());
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::PopID();
    }
    split.SetCurrentChannel(d, 0);
    if (any) {
        const ImGuiID ind = ImGui::GetID("##indicator");
        const float ax = animate(ind, selX, 20.0f);
        const float aw = animate(ind + 1, selW, 20.0f);
        const float ay = animate(ind + 2, selY, 20.0f);
        const ImVec2 a0(pos.x + ax, pos.y + ay + inner), a1(pos.x + ax + aw, pos.y + ay + h - inner);
        d->AddRectFilled(a0, a1, u32(pal::FillActive), px(6));
    }
    split.Merge(d);
    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy(ImVec2(maxW, float(rows) * h + float(rows - 1) * rowGap));
    ImGui::PopID();
    return changed;
}
}   

bool pillTabs(const char* id, const std::vector<std::string>& tabs, int* current) { return segImpl(id, tabs, current, false, 0, 14); }

bool segmented(const char* id, const std::vector<std::string>& options, int* current, float width, bool tabs) {
    if (tabs) return segImpl(id, options, current, false, width > 0 ? width : availWidth(), 16);
    return segImpl(id, options, current, true, width > 0 ? width : availWidth(), 8);
}

float segmentedWidth(const std::vector<std::string>& options, float padX) {
    float w = px(4);
    for (const std::string& o : options) w += textSize(FontKind::Semibold, type::Label, titleCase(o).c_str()).x + px(padX) * 2;
    return w;
}

 
 
 

namespace {
ImU32 hsvU32(float h, float s, float v, float a = 1.0f) {
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(h, s, v, r, g, b);
    return ImGui::GetColorU32(ImVec4(r, g, b, a));
}
std::string hexOf(const ImVec4& c) {
    char buf[16];
    snprintf(buf, sizeof(buf), "#%02X%02X%02X", int(std::lround(std::clamp(c.x, 0.0f, 1.0f) * 255)), int(std::lround(std::clamp(c.y, 0.0f, 1.0f) * 255)),
             int(std::lround(std::clamp(c.z, 0.0f, 1.0f) * 255)));
    return buf;
}
bool parseHex(std::string t, ImVec4* out) {
    if (!t.empty() && t[0] == '#') t.erase(0, 1);
    for (char c : t)
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    if (t.size() == 3) t = {t[0], t[0], t[1], t[1], t[2], t[2]};
    if (t.size() != 6) return false;
    const unsigned long v = std::strtoul(t.c_str(), nullptr, 16);
    *out = ImVec4(float((v >> 16) & 0xFF) / 255.0f, float((v >> 8) & 0xFF) / 255.0f, float(v & 0xFF) / 255.0f, out->w);
    return true;
}
}   

bool colorSwatch(const char* id, ImVec4* color) {
    ImGui::PushID(id);
    const ImVec2 size(px(22), px(22));
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImGuiID popup = ImGui::GetID("##picker");
    const bool clicked = ImGui::InvisibleButton("##swatch", size, ImGuiButtonFlags_EnableNav);
    const bool hovered = ImGui::IsItemHovered();
    const bool open = ImGui::IsPopupOpen(popup, ImGuiPopupFlags_None);
    ImDrawList* d = drawList();
    const ImVec2 c = pos + size * 0.5f;
    const float r = px(6.5f);
    d->AddCircleFilled(c, r, ImGui::GetColorU32(ImVec4(color->x, color->y, color->z, 1.0f)), 32);
    d->AddCircle(c, r - px(0.5f), u32(ImVec4(1, 1, 1, 0.14f)), 32, 1.0f);
    d->AddCircle(c, r + px(0.5f), u32(ImVec4(0, 0, 0, 0.35f)), 32, 1.0f);
    if (hovered || open) d->AddCircle(c, px(9.5f), u32(open ? pal::Accent : ImVec4(1, 1, 1, 0.40f)), 32, 1.0f);
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (clicked) {
        ImGui::OpenPopup(popup);
        g_picker.id = popup;
        ImGui::ColorConvertRGBtoHSV(color->x, color->y, color->z, g_picker.h, g_picker.s, g_picker.v);
        g_picker.hex = hexOf(*color);
    }
    bool changed = false;
    const float svW = px(168), svH = px(140), barW = px(14), gapW = px(8), pad = px(10);
    const float popW = svW + gapW + barW + pad * 2;
    ImGui::SetNextWindowPos(ImVec2(std::max(px(4), pos.x + size.x - popW), pos.y + size.y + px(6)), ImGuiCond_Appearing);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, pal::RaisedFill);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 0.08f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, px(10));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gapW, gapW));
    if (ImGui::BeginPopupEx(popup, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove)) {
        if (g_picker.id != popup) {    
            g_picker.id = popup;
            ImGui::ColorConvertRGBtoHSV(color->x, color->y, color->z, g_picker.h, g_picker.s, g_picker.v);
            g_picker.hex = hexOf(*color);
        }
        PickerState& ps = g_picker;
        ImDrawList* pd = ImGui::GetWindowDrawList();
        const ImVec2 sv = ImGui::GetCursorScreenPos();
         
        ImGui::InvisibleButton("##sv", ImVec2(svW, svH));
        if (ImGui::IsItemActive()) {
            const ImVec2 m = ImGui::GetIO().MousePos;
            ps.s = std::clamp((m.x - sv.x) / svW, 0.0f, 1.0f);
            ps.v = 1.0f - std::clamp((m.y - sv.y) / svH, 0.0f, 1.0f);
            changed = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
        const ImU32 hue = hsvU32(ps.h, 1, 1);
        pd->AddRectFilledMultiColor(sv, sv + ImVec2(svW, svH), IM_COL32_WHITE, hue, hue, IM_COL32_WHITE);
        pd->AddRectFilledMultiColor(sv, sv + ImVec2(svW, svH), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32_BLACK, IM_COL32_BLACK);
        const ImVec2 cur(sv.x + ps.s * svW, sv.y + (1.0f - ps.v) * svH);
        pd->AddCircleFilled(cur, px(5), hsvU32(ps.h, ps.s, ps.v), 20);
        pd->AddCircle(cur, px(5.75f), IM_COL32_WHITE, 20, px(1.5f));
        pd->AddCircle(cur, px(7.0f), IM_COL32(0, 0, 0, 115), 20, 1.0f);
         
        ImGui::SameLine();
        const ImVec2 hb = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##hue", ImVec2(barW, svH));
        if (ImGui::IsItemActive()) {
            ps.h = std::clamp((ImGui::GetIO().MousePos.y - hb.y) / svH, 0.0f, 0.9999f);
            changed = true;
        }
        for (int i = 0; i < 6; ++i) {
            const float y0 = hb.y + svH * float(i) / 6.0f, y1 = hb.y + svH * float(i + 1) / 6.0f;
            const ImU32 a = hsvU32(float(i) / 6.0f, 1, 1), b = hsvU32(float(i + 1) / 6.0f, 1, 1);
            pd->AddRectFilledMultiColor(ImVec2(hb.x, y0), ImVec2(hb.x + barW, y1), a, a, b, b);
        }
        const float hy = hb.y + ps.h * svH;
        pd->AddRectFilled(ImVec2(hb.x - px(2), hy - px(2)), ImVec2(hb.x + barW + px(2), hy + px(2)), IM_COL32_WHITE, px(2));
        pd->AddRect(ImVec2(hb.x - px(2), hy - px(2)), ImVec2(hb.x + barW + px(2), hy + px(2)), IM_COL32(0, 0, 0, 115), px(2), 0, 1.0f);
        if (changed) {
            float r2, g2, b2;
            ImGui::ColorConvertHSVtoRGB(ps.h, ps.s, ps.v, r2, g2, b2);
            *color = ImVec4(r2, g2, b2, color->w);
            ps.hex = hexOf(*color);
        }
         
        {
            FieldStyle fs(true);
            ImGui::SetNextItemWidth(svW + gapW + barW - px(28));
            if (ImGui::InputText("##hex", ps.hex.data(), ps.hex.capacity() + 1, ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_CharsUppercase, stringResize, &ps.hex)) {
                ImVec4 parsed = *color;
                if (parseHex(ps.hex, &parsed)) {
                    *color = parsed;
                    ImGui::ColorConvertRGBtoHSV(color->x, color->y, color->z, ps.h, ps.s, ps.v);
                    changed = true;
                }
            }
        }
        ImGui::SameLine();
        const ImVec2 sp2 = ImGui::GetCursorScreenPos() + ImVec2(0, (px(kControlH) - px(20)) * 0.5f);
        ImGui::Dummy(ImVec2(px(20), px(kControlH)));
        pd->AddCircleFilled(sp2 + ImVec2(px(10), px(10)), px(10), ImGui::GetColorU32(ImVec4(color->x, color->y, color->z, 1)), 32);
        pd->AddCircle(sp2 + ImVec2(px(10), px(10)), px(9.5f), u32(ImVec4(1, 1, 1, 0.14f)), 32, 1.0f);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    } else if (g_picker.id == popup) {
        g_picker.id = 0;
    }
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    ImGui::PopID();
    return changed;
}

bool colorSwatch(const char* id, uint32_t* rgbValue) {
    ImVec4 c(float((*rgbValue >> 16) & 0xFF) / 255.0f, float((*rgbValue >> 8) & 0xFF) / 255.0f, float(*rgbValue & 0xFF) / 255.0f, 1.0f);
    if (!colorSwatch(id, &c)) return false;
    *rgbValue = (uint32_t(std::lround(c.x * 255)) << 16) | (uint32_t(std::lround(c.y * 255)) << 8) | uint32_t(std::lround(c.z * 255));
    return true;
}

 
 
 

void pill(const char* text, const ImVec4& color, const char* glyph) {
    const ImVec2 ts = textSize(FontKind::Body, type::Caption, text);
    const float iw = glyph ? textSize(FontKind::Icons, 11, glyph).x + px(6) : 0.0f;
    const float h = px(22);
    const ImVec2 size(ts.x + iw + px(16), h);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    ImDrawList* d = drawList();
    d->AddRectFilled(pos, pos + size, u32(alpha(color, 0.12f)), px(6));
    float x = pos.x + px(8);
    if (glyph) {
        const ImVec2 is = textSize(FontKind::Icons, 11, glyph);
        drawText(d, FontKind::Icons, 11, ImVec2(x, pos.y + (h - is.y) * 0.5f), u32(color), glyph);
        x += iw;
    }
    drawText(d, FontKind::Body, type::Caption, ImVec2(x, textTop(FontKind::Body, type::Caption, pos.y + h * 0.5f)), u32(mix(color, pal::Text, 0.35f)), text);
}

ImVec4 badgeTone(const char* text) {
    const std::string t = text ? text : "";
    if (t == "Needs check" || t == "Partial" || t == "Unverified") return pal::Warning;
    if (t == "Blocked") return pal::Danger;
    if (t == "Dev" || t == "Research") return pal::Info;
    if (t == "New" || t == "Verified") return pal::Success;
    return pal::Muted;
}

float badgePillWidth(const char* text) {
    if (!text || !*text) return 0;
    return textSize(FontKind::Body, type::Micro, titleCase(text).c_str()).x + px(12);
}

void drawBadgePill(ImDrawList* d, ImVec2 topLeft, const char* text) {
    if (!text || !*text) return;
    const ImVec4 tone = badgeTone(text);
    const std::string t = titleCase(text);
    const ImVec2 ts = textSize(FontKind::Body, type::Micro, t.c_str());
    const ImVec2 size(ts.x + px(12), px(18));
    d->AddRectFilled(topLeft, topLeft + size, u32(alpha(tone, 0.14f)), px(5));
    drawText(d, FontKind::Body, type::Micro, ImVec2(topLeft.x + px(6), textTop(FontKind::Body, type::Micro, topLeft.y + size.y * 0.5f)),
             u32(mix(tone, pal::Text, 0.30f)), t.c_str());
}

void badgePill(const char* text) {
    if (!text || !*text) return;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = std::max(px(18), ImGui::GetTextLineHeight());
    ImGui::Dummy(ImVec2(badgePillWidth(text), h));
    drawBadgePill(drawList(), ImVec2(pos.x, pos.y + (h - px(18)) * 0.5f), text);
    const std::string t = text;
    if (t == "Needs check") tooltip("Ported from the Xbox tool but not yet confirmed in game on PS3. The TESTING page, Port checks tab, has a read-only probe for it.");
    else if (t == "Blocked") tooltip("Cannot work on PS3 yet; its section says what is missing.");
    else if (t == "Dev") tooltip("Development / research tool.");
}

void badge(const char* text, const ImVec4& color) {
    const ImVec2 ts = textSize(FontKind::Semibold, type::Tiny, text);
    const float h = px(18);
    const ImVec2 size(std::max(h, ts.x + px(10)), h);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    ImDrawList* d = drawList();
    d->AddRectFilled(pos, pos + size, u32(alpha(color, 0.16f)), h * 0.5f);
    drawText(d, FontKind::Semibold, type::Tiny, pos + ImVec2((size.x - ts.x) * 0.5f, (h - ts.y) * 0.5f), u32(mix(color, pal::Text, 0.3f)), text);
}

void statusDot(const ImVec4& color, bool pulse) {
    const float r = px(3);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetTextLineHeight();
    ImGui::Dummy(ImVec2(r * 2 + px(2), h));
    const ImVec2 c(pos.x + r + px(1), pos.y + h * 0.5f);
    ImDrawList* d = drawList();
    if (pulse && !reduceMotion()) {
        const float t = float(std::fmod(ImGui::GetTime(), 1.6) / 1.6);
        d->AddCircleFilled(c, r + px(5) * t, u32(color, 0.35f * (1.0f - t)), 20);
        requestRedraw();
    }
    d->AddCircleFilled(c, r, u32(color), 16);
}

void statTile(const char* id, const char* captionText, const std::string& value, const std::string& sub, float width, const ImVec4& color) {
    ImGui::PushID(id);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool hasSub = !sub.empty();
    const float h = px(hasSub ? 74 : 56);
    ImGui::Dummy(ImVec2(width, h));
    ImDrawList* d = drawList();
    d->AddRectFilled(pos, pos + ImVec2(width, h), u32(ImVec4(1, 1, 1, 0.035f)), px(kRowRounding));
    const float x = pos.x + px(12);
    ImGui::PushClipRect(pos, pos + ImVec2(width - px(8), h), true);
    const std::string cap = capsToTitle(captionText);
    drawText(d, FontKind::Body, type::Caption, ImVec2(x, pos.y + px(9)), u32(pal::Muted), cap.c_str());
    drawText(d, FontKind::Semibold, type::Stat, ImVec2(x, pos.y + px(25)), u32(color), value.c_str());
    if (hasSub) drawText(d, FontKind::Body, type::Caption, ImVec2(x, pos.y + px(52)), u32(pal::Faint), sub.c_str());
    ImGui::PopClipRect();
    ImGui::PopID();
}

 
 
 

void spinner(float radius, const ImVec4& color, float thickness) {
    const float r = px(radius);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(r * 2 + px(2), r * 2 + px(2)));
    drawSpinnerAt(drawList(), pos + ImVec2(r + px(1), r + px(1)), r, u32(color), px(thickness));
}

void progressBar(float fraction, float width, float height) {
    const float w = width > 0 ? px(width) : availWidth();
    const float h = px(height);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h));
    ImDrawList* d = drawList();
    d->AddRectFilled(pos, pos + ImVec2(w, h), u32(pal::FillActive), h);
    if (fraction < 0) {
        const float t = float(std::fmod(ImGui::GetTime(), 1.4) / 1.4);
        const float seg = w * 0.3f;
        const float x0 = pos.x - seg + (w + seg) * t;
        const float a = std::max(pos.x, x0), b = std::min(pos.x + w, x0 + seg);
        if (b > a) d->AddRectFilled(ImVec2(a, pos.y), ImVec2(b, pos.y + h), u32(pal::Accent), h);
        requestRedraw();
    } else {
        const float f = std::clamp(fraction, 0.0f, 1.0f);
        if (f > 0) d->AddRectFilled(pos, pos + ImVec2(std::max(h, w * f), h), u32(pal::Accent), h);
    }
}

 
 
 

void tooltip(const char* text) {
    const bool disabled = lastItemDisabled();
    const char* reason = disabled ? disabledReason() : nullptr;
    if (!reason && (!text || !*text)) return;
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) return;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(10), px(8)));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, px(8));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, pal::RaisedFill);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 0.08f));
     
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f);
    if (ImGui::BeginTooltip()) {
        ImGui::PushTextWrapPos(px(320));
        if (reason) {
            FontScope f(FontKind::Semibold, type::Small);
            ImGui::PushStyleColor(ImGuiCol_Text, pal::Warning);
            ImGui::TextUnformatted(reason);
            ImGui::PopStyleColor();
        }
        if (text && *text) {
            FontScope f(FontKind::Body, type::Small);
            ImGui::PushStyleColor(ImGuiCol_Text, reason ? pal::Muted : pal::Text);
            ImGui::TextUnformatted(text);
            ImGui::PopStyleColor();
        }
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

DisabledScope::DisabledScope(bool disabled, const char* reason) : active_(disabled) {
    if (active_) {
        ImGui::BeginDisabled(true);
        g_reasons.push_back(reason ? reason : kConnectFirst);
    }
}

DisabledScope::~DisabledScope() {
    if (active_) {
        g_reasons.pop_back();
        ImGui::EndDisabled();
    }
}

DisabledScope requireConnection(const Ui& ui) { return DisabledScope(!ui.connected(), kConnectFirst); }

const char* disabledReason() { return g_reasons.empty() ? nullptr : g_reasons.back(); }

 
 
 

void emptyState(const char* glyph, const char* titleText, const char* text, float height) {
    const float w = availWidth();
    const float h = height > 0 ? px(height) : px(160);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h));
    ImDrawList* d = drawList();
    const float circle = px(h < px(140) ? 36 : 44);
    const std::string t = titleCase(titleText ? titleText : "");
    const ImVec2 ts = textSize(FontKind::Semibold, type::Label, t.c_str());
    const float wrap = std::min(w - px(40), px(420));
    const ImVec2 bs = text ? textSize(FontKind::Body, type::Small, text, nullptr, wrap) : ImVec2(0, 0);
    const float total = circle + px(12) + ts.y + (text ? px(4) + bs.y : 0);
    float y = pos.y + (h - total) * 0.5f;
    const ImVec2 c(pos.x + w * 0.5f, y + circle * 0.5f);
    d->AddCircleFilled(c, circle * 0.5f, u32(ImVec4(1, 1, 1, 0.05f)), 40);
    const float iconEm = circle > px(40) ? 18.0f : 15.0f;
    const ImVec2 is = textSize(FontKind::Icons, iconEm, glyph);
    drawText(d, FontKind::Icons, iconEm, c - is * 0.5f, u32(pal::Muted), glyph);
    y += circle + px(12);
    drawText(d, FontKind::Semibold, type::Label, ImVec2(pos.x + (w - ts.x) * 0.5f, y), u32(pal::Text), t.c_str());
    y += ts.y + px(4);
    if (text) {
         
        ImFont* f = font(FontKind::Body);
        const float size = px(fontSize(FontKind::Body, type::Small));
        const char* p = text;
        const char* end = text + strlen(text);
        while (p < end) {
            const char* lineEnd = f->CalcWordWrapPosition(size, p, end, wrap);
            if (lineEnd == p) lineEnd = p + 1;
            const ImVec2 ls = f->CalcTextSizeA(size, FLT_MAX, 0, p, lineEnd);
            drawText(d, FontKind::Body, type::Small, ImVec2(pos.x + (w - ls.x) * 0.5f, y), u32(pal::Muted), p, lineEnd);
            y += ls.y;
            p = lineEnd;
            while (p < end && (*p == ' ' || *p == '\n')) ++p;
        }
    }
}

void hexView(const char* id, const uint8_t* data, size_t size, const HexViewOptions& o) {
    ImGui::PushID(id);
    const float charW = textSize(FontKind::Mono, type::Mono, "0").x;
    const float avail = availWidth();
    int per = o.bytesPerRow;
    if (per <= 0) {
        const float need16 = charW * (8 + 3 + 16 * 3 + 3 + 3 + 16) + px(8);
        per = avail >= need16 ? 16 : 8;
    }
    const size_t rows = (size + size_t(per) - 1) / size_t(per);
    const float lineH = textSize(FontKind::Mono, type::Mono, "0").y + px(3);
    const float contentH = float(rows) * lineH + px(2);
    const float height = o.height > 0 ? std::min(px(o.height), contentH) : contentH;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0.22f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(12), px(8)));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(8));
    ImGui::PushStyleColor(ImGuiCol_Border, pal::Border);
    ImGui::BeginChild("##hex", ImVec2(avail, height + px(16)), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float xAddr = 0, xHex = charW * 10;
    const float groupGap = charW;
    const float xAscii = xHex + float(per) * charW * 3 + float(per / 4) * groupGap + charW * 1.5f;
    ImGuiListClipper clip;
    clip.Begin(int(rows), lineH);
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    int hoverIndex = -1;
    while (clip.Step()) {
        for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(xAscii + charW * float(per), lineH));
            char buf[16];
            snprintf(buf, sizeof(buf), "%08X", unsigned(o.base + uint32_t(r * per)));
            drawText(d, FontKind::Mono, type::Mono, ImVec2(p.x + xAddr, p.y), u32(pal::Faint), buf);
            for (int c = 0; c < per; ++c) {
                const size_t i = size_t(r) * size_t(per) + size_t(c);
                if (i >= size) break;
                const uint8_t b = data[i];
                const bool changed = o.previous && i < o.previousSize && o.previous[i] != b;
                const float x = p.x + xHex + float(c) * charW * 3 + float(c / 4) * groupGap;
                const ImRect cell(ImVec2(x - charW * 0.25f, p.y - px(1)), ImVec2(x + charW * 2.25f, p.y + lineH - px(2)));
                const bool hov = ImGui::IsWindowHovered() && cell.Contains(mouse);
                const float ax = p.x + xAscii + float(c) * charW;
                const ImRect acell(ImVec2(ax, p.y - px(1)), ImVec2(ax + charW, p.y + lineH - px(2)));
                const bool ahov = ImGui::IsWindowHovered() && acell.Contains(mouse);
                if (hov || ahov) {
                    hoverIndex = int(i);
                    d->AddRectFilled(cell.Min, cell.Max, u32(alpha(pal::Accent, 0.22f)), px(3));
                    d->AddRectFilled(acell.Min, acell.Max, u32(alpha(pal::Accent, 0.22f)), px(2));
                } else if (changed) {
                    d->AddRectFilled(cell.Min, cell.Max, u32(alpha(pal::Accent, 0.12f)), px(3));
                }
                snprintf(buf, sizeof(buf), "%02X", b);
                const ImVec4 col = changed ? pal::Accent : b == 0 ? pal::Decor : pal::Text;
                drawText(d, FontKind::Mono, type::Mono, ImVec2(x, p.y), u32(col), buf);
                const char ch = (b >= 0x20 && b < 0x7F) ? char(b) : '.';
                buf[0] = ch;
                buf[1] = 0;
                drawText(d, FontKind::Mono, type::Mono, ImVec2(ax, p.y), u32(ch == '.' ? pal::Decor : pal::Muted), buf);
            }
        }
    }
    clip.End();
    if (hoverIndex >= 0) {
        const uint32_t addr = o.base + uint32_t(hoverIndex);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            char a[16];
            snprintf(a, sizeof(a), "%08X", addr);
            copyToClipboard(a, "address");
        }
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::BeginTooltip()) {
            FontScope f(FontKind::Mono, type::Mono);
            ImGui::Text("%08X  +0x%X", addr, unsigned(hoverIndex));
            const uint8_t b = data[hoverIndex];
            ImGui::TextColored(pal::Muted, "u8 %u  i8 %d", unsigned(b), int(int8_t(b)));
            if (size_t(hoverIndex) + 4 <= size) {
                const uint32_t be = (uint32_t(data[hoverIndex]) << 24) | (uint32_t(data[hoverIndex + 1]) << 16) |
                                    (uint32_t(data[hoverIndex + 2]) << 8) | uint32_t(data[hoverIndex + 3]);
                float fv;
                memcpy(&fv, &be, 4);
                ImGui::TextColored(pal::Muted, "u32 %08X  f32 %g", be, double(fv));
            }
            ImGui::EndTooltip();
        }
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    ImGui::PopID();
}

bool beginTable(const char* id, int columns, float height, ImGuiTableFlags extra, float width) {
    ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, pal::RaisedFill);
    ImGui::PushStyleColor(ImGuiCol_TableBorderLight, pal::Line);
    ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, pal::Line);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(1, 1, 1, 0.05f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, pal::Selection);
    ImGui::PushStyleColor(ImGuiCol_Header, pal::Selection);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(px(8), px(6)));
    ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_PadOuterX | ImGuiTableFlags_SizingStretchProp | extra;
    ImVec2 size(width > 0 ? px(width) : availWidth(), height > 0 ? px(height) : 0.0f);
    if (height > 0) {
        flags |= ImGuiTableFlags_ScrollY;
        flags &= ~ImGuiTableFlags_BordersOuter;
    }
    const bool open = ImGui::BeginTable(id, columns, flags, size);
    if (!open) {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(6);
    }
    return open;
}

void endTable() {
    ImGui::EndTable();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(6);
}

void tableHeaders() {
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers, px(26));
    const int n = ImGui::TableGetColumnCount();
    FontScope f(FontKind::Body, type::Caption);
    ImGui::PushStyleColor(ImGuiCol_Text, pal::Muted);
    for (int c = 0; c < n; ++c) {
        if (!ImGui::TableSetColumnIndex(c)) continue;
        const std::string name = titleCase(ImGui::TableGetColumnName(c));
        ImGui::TableHeader(name.c_str());
    }
    ImGui::PopStyleColor();
}

bool tableRowSelectable(const char* id, bool selected, bool* doubleClicked, float height) {
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableRounding, px(6));
    const bool r = ImGui::Selectable(id, selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap |
                                                       ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, height > 0 ? px(height) : 0));
    ImGui::PopStyleVar();
    if (doubleClicked) *doubleClicked = r && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    return r;
}

 
 
 

void confirm(std::string titleText, std::string message, std::string confirmLabel, std::function<void()> onConfirm, bool danger) {
    g_confirm.title = std::move(titleText);
    g_confirm.message = std::move(message);
    g_confirm.label = std::move(confirmLabel);
    g_confirm.fn = std::move(onConfirm);
    g_confirm.danger = danger;
    g_confirm.requestOpen = true;
}

namespace {
std::unordered_map<std::string, SectionStyle>& sectionStyles() {
    static std::unordered_map<std::string, SectionStyle> styles;    
    return styles;
}
}   

std::vector<std::string>& detail::sectionStyleConflicts() {
    static std::vector<std::string> conflicts;    
    return conflicts;
}

void styleSection(const std::string& sectionTitle, SectionStyle style) {
    auto& m = sectionStyles();
    if (auto it = m.find(sectionTitle); it != m.end()) {
        const SectionStyle& o = it->second;
        if (o.fullWidth != style.fullWidth || o.subtitle != style.subtitle || o.icon != style.icon || o.ownsActions != style.ownsActions)
            detail::sectionStyleConflicts().push_back(sectionTitle);
    }
    m[sectionTitle] = std::move(style);
}

const SectionStyle* findSectionStyle(const std::string& sectionTitle) {
    auto& m = sectionStyles();
    auto it = m.find(sectionTitle);
    return it == m.end() ? nullptr : &it->second;
}

namespace {
std::vector<std::function<void()>>& frameHooks() {
    static std::vector<std::function<void()>> hooks;    
    return hooks;
}
std::atomic<void (*)()> g_wakeHandler{nullptr};
std::atomic<bool> g_woken{false};
bool g_demo = false;
}   

void addFrameHook(std::function<void()> fn) {
    if (fn) frameHooks().push_back(std::move(fn));
}

void wake() {
    g_woken = true;
    if (void (*fn)() = g_wakeHandler.load()) fn();
}

bool isDemo() { return g_demo; }

namespace detail {

void runFrameHooks() {
    for (auto& fn : frameHooks()) {
        try {
            fn();
        } catch (const std::exception& e) {
            notify(std::string("Frame hook failed: ") + e.what(), Level::Error);
        }
    }
}

void setWakeHandler(void (*fn)()) { g_wakeHandler = fn; }

bool takeWake() { return g_woken.exchange(false); }

void setDemo(bool on) { g_demo = on; }

std::vector<Activity> activities() {
    std::lock_guard<std::mutex> lock(g_activityMutex);
    return g_activities;
}

void pruneActivities() {
    std::vector<Activity> current = activities();
    std::vector<std::string> ended;
    for (const Activity& a : current)
        if (!a.running || !a.running()) ended.push_back(a.id);    
    if (ended.empty()) return;
    std::lock_guard<std::mutex> lock(g_activityMutex);
    g_activities.erase(std::remove_if(g_activities.begin(), g_activities.end(),
                                      [&](const Activity& a) { return std::find(ended.begin(), ended.end(), a.id) != ended.end(); }),
                       g_activities.end());
}

double activityClock() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

std::vector<PaletteProvider>& paletteProviders() {
    static std::vector<PaletteProvider> providers;    
    return providers;
}

void requestSearchFocus() { g_searchFocusFrame = ImGui::GetFrameCount(); }

bool takeSearchFocusRequest() {
    const bool pending = g_searchFocusFrame == ImGui::GetFrameCount();
    if (pending) g_searchFocusFrame = -1;
    return pending;
}

void setHost(const std::string* host) { g_host = host; }

bool takeRedrawRequest() {
    const bool r = g_redraw;
    g_redraw = false;
    return r;
}

std::vector<Note> takeNotes() {
    std::vector<Note> out;
    out.swap(g_notes);
    return out;
}

void beginFrame() {
    g_reasons.clear();
    g_lastRow = {};
    g_cardDepth = 0;
    if (g_searchFocusFrame >= 0 && g_searchFocusFrame < ImGui::GetFrameCount()) g_searchFocusFrame = -1;
}

bool modalOpen() { return ImGui::IsPopupOpen(ImHashStr("##brps3-confirm"), ImGuiPopupFlags_None) || g_confirm.requestOpen; }

void drawModals() {
    const ImGuiID popupId = ImHashStr("##brps3-confirm");
    if (g_confirm.requestOpen) {
        ImGui::OpenPopup(popupId);
        g_confirm.requestOpen = false;
        g_confirm.openedFrame = ImGui::GetFrameCount();
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(16), px(16)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(10));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, pal::RaisedFill);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, pal::RaisedFill);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 0.08f));
    if (ImGui::BeginPopupEx(popupId, ImGuiWindowFlags_Modal | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
        const float innerW = px(380);
        ImGui::Dummy(ImVec2(innerW, 0));
        {
            FontScope f(FontKind::Semibold, type::Label);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + innerW);
            ImGui::TextUnformatted(titleCase(g_confirm.title).c_str());
            ImGui::PopTextWrapPos();
        }
        gap(sp::S);
        {
            FontScope f(FontKind::Body, type::Label);
            ImGui::PushStyleColor(ImGuiCol_Text, pal::Muted);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + innerW);
            ImGui::TextUnformatted(g_confirm.message.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
        gap(sp::M);
        const std::string okLabel = titleCase(g_confirm.label);
        const float bw1 = buttonWidth("Cancel"), bw2 = buttonWidth(okLabel.c_str());
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, innerW - bw1 - bw2 - px(8)));
        bool close = false, run = false;
        const bool keys = ImGui::GetFrameCount() > g_confirm.openedFrame;
         
        const bool cancelClicked = button("Cancel");
        const bool cancelFocused = ImGui::IsItemFocused();
        if (cancelClicked || (keys && ImGui::IsKeyPressed(ImGuiKey_Escape, false))) close = true;
        ImGui::SameLine(0, px(8));
        const bool confirmClicked = button(okLabel.c_str(), {.tone = g_confirm.danger ? Tone::Danger : Tone::Primary});
        ImGui::SetItemDefaultFocus();    
        const bool enter = keys && !close && !cancelFocused &&
                           (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
        if (confirmClicked || enter) {
            run = true;
            close = true;
        }
        if (close) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        if (run && g_confirm.fn) {
            auto fn = std::move(g_confirm.fn);
            g_confirm.fn = nullptr;
            fn();
        }
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(3);
}

}   

}   
