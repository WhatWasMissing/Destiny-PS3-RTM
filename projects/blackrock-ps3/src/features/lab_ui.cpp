 
#define IMGUI_DEFINE_MATH_OPERATORS
#include "features/lab_ui.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include "app/engine.h"
#include "core/error.h"
#include "game/world_objects.h"
#include "game/world_player.h"

namespace brps3::labui {

using namespace ui;

bool demo() {
    static const bool on = [] {
        char* v = nullptr;
        size_t n = 0;
        const bool d = _dupenv_s(&v, &n, "BRPS3_UI_DEMO") == 0 && v && v[0] == '1';
        free(v);
        return d;
    }();
    return on;
}

double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

std::string ago(double at) {
    if (at <= 0) return {};
    const double s = std::max(0.0, now() - at);
    if (s < 60) return fmt("%.1f s ago", s);
    if (s < 3600) return fmt("%.0f min ago", s / 60);
    return fmt("%.1f h ago", s / 3600);
}

std::string hx(uint32_t v) { return fmt("%08X", v); }

std::string thousands(size_t n) {
    std::string d = std::to_string(n), out;
    for (size_t i = 0; i < d.size(); ++i) {
        if (i && (d.size() - i) % 3 == 0) out += ',';
        out += d[i];
    }
    return out;
}

std::string fmt(const char* f, ...) {
    char buf[1024];
    va_list a;
    va_start(a, f);
    std::vsnprintf(buf, sizeof buf, f, a);
    va_end(a);
    return buf;
}

void post(Ui& ui, const char* label, std::function<void(Ctx&)> run, std::function<void(const std::string&)> onError) {
    noteQueued(label);
    ui.post(label, std::move(run), [onError](bool ok, const std::string& err) {
        if (!ok && onError) onError(err);
    });
}

 

bool kvBegin(const char* id, float keyWidth, float width) {
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(px(4), px(3)));
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit, ImVec2(width > 0 ? width : 0.0f, 0.0f))) {
        ImGui::PopStyleVar();
        return false;
    }
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, px(keyWidth));
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

void kv(const char* key, const std::string& value, const ImVec4& color, bool isMono, const char* tip) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    {
        FontScope f(FontKind::Body, type::Small);
        ImGui::PushStyleColor(ImGuiCol_Text, pal::Muted);
        ImGui::TextUnformatted(key);
        ImGui::PopStyleColor();
    }
    if (tip) tooltip(tip);
    ImGui::TableSetColumnIndex(1);
    if (isMono) mono(value.c_str(), color, type::MonoSmall);
    else {
        FontScope f(FontKind::Body, type::Small);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(value.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    if (tip) tooltip(tip);
}

void kvHex(const char* key, uint32_t value, const char* what, const ImVec4& color, const char* tip) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    {
        FontScope f(FontKind::Body, type::Small);
        ImGui::PushStyleColor(ImGuiCol_Text, pal::Muted);
        ImGui::TextUnformatted(key);
        ImGui::PopStyleColor();
    }
    if (tip) tooltip(tip);
    ImGui::TableSetColumnIndex(1);
    ImGui::PushID(key);
    copyText(hx(value), what, color);
    ImGui::PopID();
}

void copyText(const std::string& text, const char* what, const ImVec4& color) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 ts = textSize(FontKind::Mono, type::MonoSmall, text.c_str());
    ImGui::PushID(text.c_str());
    const bool clicked = ImGui::InvisibleButton("##copy", ImVec2(std::max(ts.x, 1.0f), std::max(ts.y, 1.0f)));
    const bool hov = ImGui::IsItemHovered();
    ImGui::PopID();
    if (hov) {
        ImGui::GetWindowDrawList()->AddRectFilled(p - ImVec2(px(4), px(1)), p + ts + ImVec2(px(4), px(1)), u32(pal::RaisedHi), px(4));
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("Click to copy");
    }
    drawText(ImGui::GetWindowDrawList(), FontKind::Mono, type::MonoSmall, p, u32(color), text.c_str());
    if (clicked) copyToClipboard(text, what);
}

void copyCell(const std::string& text, const char* what, const ImVec4& color, float rowHeight) {
    const float padY = ImGui::GetStyle().CellPadding.y;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float mid = p.y - padY + px(rowHeight) * 0.5f;
    const float baseline = std::floor(textTop(FontKind::Body, type::Label, mid) + ascent(FontKind::Body, type::Label) + 0.5f);
    ImGui::SetCursorScreenPos(ImVec2(p.x, baselineTop(FontKind::Mono, type::MonoSmall, baseline)));
    copyText(text, what, color);
}

void kvEnd() {
    ImGui::EndTable();
    ImGui::PopStyleVar();
}

void check(bool ok, const std::string& text, const char* tip) {
    const char* glyph = ok ? icon::Check : icon::Close;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = px(20);
    const ImVec2 is = textSize(FontKind::Icons, 11, glyph);
    drawText(ImGui::GetWindowDrawList(), FontKind::Icons, 11, ImVec2(p.x, p.y + (h - is.y) * 0.5f), u32(ok ? pal::Success : pal::Danger), glyph);
    const ImVec2 ts = textSize(FontKind::Body, type::Small, text.c_str());
    drawText(ImGui::GetWindowDrawList(), FontKind::Body, type::Small, ImVec2(p.x + px(18), textTop(FontKind::Body, type::Small, p.y + h * 0.5f)),
             u32(ok ? pal::Text : mix(pal::Danger, pal::Text, 0.45f)), text.c_str());
    ImGui::Dummy(ImVec2(px(18) + ts.x, h));
    if (tip) tooltip(tip);
}

void warnLine(const std::string& text) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = px(20);
    const ImVec2 is = textSize(FontKind::Icons, 11, icon::Info);
    drawText(ImGui::GetWindowDrawList(), FontKind::Icons, 11, ImVec2(p.x, p.y + (h - is.y) * 0.5f), u32(pal::Warning), icon::Info);
    ImGui::SetCursorScreenPos(ImVec2(p.x + px(18), p.y + px(2)));
    ImGui::PushTextWrapPos(0);
    {
        FontScope f(FontKind::Body, type::Small);
        ImGui::PushStyleColor(ImGuiCol_Text, mix(pal::Warning, pal::Text, 0.45f));
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::PopTextWrapPos();
    ImGui::SetCursorScreenPos(ImVec2(p.x, ImGui::GetCursorScreenPos().y));
}

void hint(const char* text) { wrapped(text, pal::Muted, type::Small); }

void errorBox(const std::string& text) {
    if (text.empty()) return;
    gap(sp::XS);
    wrapped(text.c_str(), mix(pal::Danger, pal::Text, 0.3f), type::Small);
}

void readStamp(double at, const char* what) {
    if (at <= 0) return;
    const std::string s = std::string(what) + " " + ago(at);
    const ImVec2 ts = textSize(FontKind::Body, type::Small, s.c_str());
    ImGui::SameLine();
    alignRight(ts.x);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(8));
    caption(s.c_str(), pal::Faint);
}

Flow::Flow() : avail_(availWidth()) {}

void Flow::next(float width, float spacing) {
    if (!first_) {
        if (x_ + px(spacing) + width <= avail_) {
            ImGui::SameLine(0, px(spacing));
            x_ += px(spacing);
        } else {
            x_ = 0;
            gap(sp::XS);
        }
    }
    first_ = false;
    x_ += width;
}

bool jobButton(Ui& ui, const char* label, const char* glyph, const char* tip, Tone tone, const char* text) {
    auto off = requireConnection(ui);
    return button(text ? text : label, {.tone = tone, .icon = glyph, .tooltip = tip, .busy = ui.busy(label), .successKey = label});
}

 

namespace {

struct PickerState {
    std::mutex m;
    uint32_t handle = 0;
    uint8_t type = 0;
    std::vector<ObjRow> rows;
    double rowsAt = 0;
    std::string listError, playerError;
    std::optional<std::pair<uint32_t, uint8_t>> pending;    
     
    bool browse = false;
    std::string filter;
    int typeFilter = 0;
    uint32_t manual = 0;
    std::vector<std::pair<uint32_t, uint8_t>> recent;
    bool seeded = false;
};

PickerState& ps() {
    static PickerState s;
    return s;
}

const char* const kTypeFilters[] = {"All", "Bipeds", "Weapons", "Vehicles", "Props", "Hop-Ons", "Other"};

bool typeMatches(int f, uint8_t t) {
    switch (f) {
    case 1: return t == world::TYPE_BIPED || t == world::TYPE_CREATURE;
    case 2: return t == world::TYPE_WEAPON;
    case 3: return t == world::TYPE_VEHICLE || t == world::TYPE_TURRET;
    case 4: return t >= 1 && t <= 8;
    case 5: return t >= 20 && t <= 23;
    case 6: return t == 0 || (t >= 14 && t <= 19) || t >= 24;
    default: return true;
    }
}

void seedDemo() {
    PickerState& s = ps();
    if (s.seeded) return;
    s.seeded = true;
    if (!demo()) return;
    std::lock_guard<std::mutex> l(s.m);    
    const struct {
        uint32_t h;
        uint8_t t;
        float x, y, z;
        uint32_t parent;
    } sample[] = {{0x51FB8003, 9, 0.2f, -18.6f, -7.0f, 0xFFFFFFFF},  {0x6EFB8001, 11, 0.4f, -18.2f, -6.1f, 0x51FB8003},
                  {0x52038005, 11, 0.4f, -18.3f, -6.0f, 0x51FB8003}, {0x5A0B8011, 9, 31.5f, 4.2f, -6.8f, 0xFFFFFFFF},
                  {0x5A138012, 9, 33.0f, 6.9f, -6.8f, 0xFFFFFFFF},   {0x60178014, 12, -12.0f, 40.1f, -5.0f, 0xFFFFFFFF},
                  {0x611B8017, 2, 8.0f, 12.5f, -7.2f, 0xFFFFFFFF},   {0x63238019, 21, 0.2f, -18.6f, -7.0f, 0x51FB8003},
                  {0x6427801C, 14, 5.0f, 2.0f, 1.0f, 0xFFFFFFFF},    {0x652B8020, 6, 9.0f, 3.5f, -7.1f, 0xFFFFFFFF}};
    for (const auto& o : sample) {
        ObjRow r;
        r.handle = o.h;
        r.index = o.h & 0x1FFF;
        r.type = o.t;
        r.pos = {o.x, o.y, o.z};
        r.parent = o.parent;
        r.head = 0x2C410000u | r.index;
        s.rows.push_back(r);
    }
    s.rowsAt = now() - 3;
    s.handle = 0x51FB8003;
    s.type = 9;
    s.recent = {{0x51FB8003, 9}, {0x5A0B8011, 9}, {0x60178014, 12}};
}

void remember(uint32_t h, uint8_t t) {
    PickerState& s = ps();
    s.recent.erase(std::remove_if(s.recent.begin(), s.recent.end(), [&](const auto& r) { return r.first == h; }), s.recent.end());
    s.recent.insert(s.recent.begin(), {h, t});
    if (s.recent.size() > 6) s.recent.resize(6);
}

}   

uint32_t pickedHandle() {
     
    std::lock_guard<std::mutex> l(ps().m);
    return ps().handle;
}

std::string pickedText() {
    std::lock_guard<std::mutex> l(ps().m);
    if (!ps().handle) return "no object";
    return std::string(world::typeName(ps().type)) + " " + hx(ps().handle);
}

void pick(uint32_t handle, uint8_t type, bool) {
    {
        std::lock_guard<std::mutex> l(ps().m);
        ps().handle = handle;
        ps().type = type;
    }
    remember(handle, type);
}

void runFindPlayer(Ctx& c) {
    c.require(world::kFingerprints, "World readers");
    world::RuntimeReader mem(c.rt());
    const world::LocalPlayer p = world::requireLocalPlayer(mem);
    std::lock_guard<std::mutex> l(ps().m);
    ps().pending = std::make_pair(p.handle, p.object.type);
    ps().playerError.clear();
    c.log("[lab] local player " + hx(p.handle) + " (" + world::typeName(p.object.type) + ", sObject " + hx(p.entry) + ")");
}

void runListObjects(Ctx& c) {
    c.require(world::kFingerprints, "World readers");
    world::RuntimeReader mem(c.rt());
    const world::ObjectScan scan = world::readObjects(mem);
    std::vector<ObjRow> rows;
    for (const world::SObject& o : scan.objects) {
        ObjRow r;
        r.handle = o.handle;
        r.index = o.index;
        r.type = o.type;
        r.parent = o.parent;
        r.head = o.componentHead;
        r.deleting = o.deleting();
        r.pos = o.position;
        rows.push_back(r);
    }
    std::lock_guard<std::mutex> l(ps().m);
    ps().rows = std::move(rows);
    ps().rowsAt = now();
    ps().listError.clear();
    c.log("[lab] " + std::to_string(ps().rows.size()) + " live objects of " + std::to_string(scan.pool.capacity));
}

void resetPicker() {
    std::lock_guard<std::mutex> l(ps().m);
    ps().handle = 0;
    ps().rows.clear();
    ps().rowsAt = 0;
    ps().pending.reset();
    ps().recent.clear();
}

void drawPicker(Ui& ui) {
    seedDemo();
    PickerState& s = ps();
    {
        std::lock_guard<std::mutex> l(s.m);
        if (s.pending) {
            const auto [h, t] = *s.pending;
            s.pending.reset();
            s.handle = h;
            s.type = t;
            remember(h, t);
        }
    }
    uint32_t handle;
    uint8_t type;
    std::vector<ObjRow> rows;
    double rowsAt;
    std::string listError, playerError;
    {
        std::lock_guard<std::mutex> l(s.m);
        handle = s.handle;
        type = s.type;
        rows = s.rows;
        rowsAt = s.rowsAt;
        listError = s.listError;
        playerError = s.playerError;
    }
     
    if (handle) {
        const ObjRow* row = nullptr;
        for (const ObjRow& r : rows)
            if (r.handle == handle) row = &r;
        hashLabel(handle, "handle", pal::Accent);
        ImGui::SameLine(0, px(10));
        pill(world::typeName(type), pal::Info);
        ImGui::SameLine(0, px(6));
        pill(fmt("index %u", handle & 0x1FFF).c_str(), pal::Muted);
        if (row) {
            ImGui::SameLine(0, px(12));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(4));
            caption(fmt("at (%.1f, %.1f, %.1f)  \xC2\xB7  +0x3C %08X", double(row->pos.x), double(row->pos.y), double(row->pos.z), row->head).c_str(),
                    pal::Muted);
        }
    } else {
        caption("No object selected: inspect your player, pick one from the live list, or type a handle.", pal::Faint);
    }
    gap(sp::S);
    if (jobButton(ui, kFindPlayerLabel, icon::Follow, "Use your own player's sObject (resolves the local player chain)", Tone::Primary, "Local Player"))
        post(ui, kFindPlayerLabel, runFindPlayer, [](const std::string& e) {
            std::lock_guard<std::mutex> l(ps().m);
            ps().playerError = e;
        });
    ImGui::SameLine(0, px(8));
    if (button(s.browse ? "Hide Objects" : "Live Objects", {.icon = s.browse ? icon::ChevronUp : icon::List,
                                                            .tooltip = "Browse every live sObject (one read of the whole pool)"})) {
        s.browse = !s.browse;
        if (s.browse && rows.empty() && ui.connected() && !ui.busy(kListObjectsLabel))
            post(ui, kListObjectsLabel, runListObjects, [](const std::string& e) {
                std::lock_guard<std::mutex> l(ps().m);
                ps().listError = e;
            });
    }
    ImGui::SameLine(0, px(16));
    inputHex("##manual", &s.manual, 120);
    tooltip("Any sObject handle (hex); the readers check that it is still live");
    ImGui::SameLine(0, px(6));
    {
        DisabledScope none(s.manual == 0, "Type a handle first");
        if (button("Inspect", {.tone = Tone::Ghost, .icon = icon::View, .tooltip = "Inspect this handle"})) pick(s.manual, 0, false);
    }
    if (!s.recent.empty()) {
        ImGui::SameLine(0, px(16));
        {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const ImVec2 ts = textSize(FontKind::Body, type::Small, "Recent");
            drawText(ImGui::GetWindowDrawList(), FontKind::Body, type::Small, ImVec2(p.x, textTop(FontKind::Body, type::Small, p.y + px(kControlH) * 0.5f)),
                     u32(pal::Faint), "Recent");
            ImGui::Dummy(ImVec2(ts.x, px(kControlH)));
        }
        for (const auto& [h, t] : std::vector<std::pair<uint32_t, uint8_t>>(s.recent)) {
            ImGui::SameLine(0, px(6));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px((kControlH - kSmallControlH) * 0.5f));
            const std::string label = std::string(world::typeName(t)) + " " + fmt("%04X", h & 0xFFFF) + "##r" + hx(h);
            if (button(label.c_str(), {.tone = Tone::Ghost, .small = true, .tooltip = hx(h).c_str(), .selected = h == handle})) pick(h, t, false);
        }
    }
    errorBox(playerError);
    if (!s.browse) return;
    gap(sp::M);
    searchField("##objfilter", &s.filter, "Filter by Type, Handle or Index", 260);
    ImGui::SameLine(0, px(10));
    std::vector<std::string> tf(std::begin(kTypeFilters), std::end(kTypeFilters));
    pillTabs("##objtype", tf, &s.typeFilter);
    ImGui::SameLine(0, px(8));
    if (jobButton(ui, kListObjectsLabel, icon::Refresh, "Read the sObject pool again", Tone::Ghost, "Refresh"))
        post(ui, kListObjectsLabel, runListObjects, [](const std::string& e) {
            std::lock_guard<std::mutex> l(ps().m);
            ps().listError = e;
        });
    readStamp(rowsAt);
    errorBox(listError);
    gap(sp::S);
    std::vector<const ObjRow*> view;
    std::string f;
    for (char ch : s.filter) f += char(std::tolower(static_cast<unsigned char>(ch)));
    for (const ObjRow& r : rows) {
        if (!typeMatches(s.typeFilter, r.type)) continue;
        if (!f.empty()) {
            std::string hay = std::string(world::typeName(r.type)) + " " + fmt("%08x %u", r.handle, r.index);
            if (hay.find(f) == std::string::npos) continue;
        }
        view.push_back(&r);
    }
    if (rows.empty()) {
        caption(ui.connected() ? (ui.busy(kListObjectsLabel) ? "Reading the sObject pool..." : "No objects read yet: press Refresh.")
                               : "Connect to list the live objects.",
                pal::Faint);
        return;
    }
    if (beginTable("##objects", 6, 250, ImGuiTableFlags_BordersOuter)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Handle", ImGuiTableColumnFlags_WidthFixed, px(92));
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, px(130));
        ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, px(56));
        ImGui::TableSetupColumn("Position", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Parent", ImGuiTableColumnFlags_WidthFixed, px(92));
        ImGui::TableSetupColumn("+0x3C", ImGuiTableColumnFlags_WidthFixed, px(92));
        tableHeaders();
        ImGuiListClipper clip;
        clip.Begin(int(view.size()), px(28));
        while (clip.Step()) {
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                const ObjRow& r = *view[size_t(i)];
                ImGui::TableNextRow(0, px(28));
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(int(r.handle));
                const float y = ImGui::GetCursorPosY();
                if (tableRowSelectable("##o", r.handle == handle, nullptr, 28 - 2 * ImGui::GetStyle().CellPadding.y / scale())) pick(r.handle, r.type, true);
                ImGui::PopID();
                ImGui::SetCursorPosY(y);
                cellText(hx(r.handle).c_str(), r.handle == handle ? pal::Accent : pal::Text, FontKind::Mono, type::MonoSmall, 28);
                ImGui::TableSetColumnIndex(1);
                cellText((std::string(world::typeName(r.type)) + (r.deleting ? " (deleting)" : "")).c_str(), r.deleting ? pal::Warning : pal::Text,
                         FontKind::Body, type::Label, 28);
                ImGui::TableSetColumnIndex(2);
                cellText(std::to_string(r.index).c_str(), pal::Muted, FontKind::Body, type::Label, 28);
                ImGui::TableSetColumnIndex(3);
                cellText(fmt("%.1f, %.1f, %.1f", double(r.pos.x), double(r.pos.y), double(r.pos.z)).c_str(), pal::Muted, FontKind::Mono, type::MonoSmall, 28);
                ImGui::TableSetColumnIndex(4);
                cellText(world::validHandle(r.parent) ? hx(r.parent).c_str() : "-", pal::Faint, FontKind::Mono, type::MonoSmall, 28);
                ImGui::TableSetColumnIndex(5);
                cellText(world::validHandle(r.head) ? hx(r.head).c_str() : "-", pal::Faint, FontKind::Mono, type::MonoSmall, 28);
            }
        }
        endTable();
    }
    gap(sp::XS);
    caption(fmt("%zu of %zu live objects", view.size(), rows.size()).c_str(), pal::Faint);
}

}   
