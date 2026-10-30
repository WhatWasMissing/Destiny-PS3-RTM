 
 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <string>

#include "app/engine.h"
#include "app/registry.h"
#include "app/settings.h"
#include "core/error.h"
#include "core/runtime.h"
#include "features/loot_common.h"
#include "game/loot_events.h"
#include "game/world_mem.h"
#include "ui/widgets.h"

namespace brps3::lootui {

using namespace ui;

 

AfkShared& afkShared() {
    static AfkShared s;
    return s;
}

void AfkShared::publish(const std::string& lastEvent) {
    AfkView v;
    v.held = keeper.held();
    v.armed = keeper.armed();
    v.noLimit = keeper.binding().noLimit;
    v.method = keeper.method();
    v.resets = keeper.resets();
    v.status = keeper.status();
    std::lock_guard<std::mutex> lock(viewM_);
    v.lastEvent = lastEvent.empty() ? view_.lastEvent : lastEvent;
    if (!v.held) v.lastEvent.clear();
    view_ = std::move(v);
}

void AfkShared::clearEvent() {
    std::lock_guard<std::mutex> lock(viewM_);
    view_.lastEvent.clear();
}

AfkView AfkShared::view() const {
    std::lock_guard<std::mutex> lock(viewM_);
    return view_;
}

loot::afk::Method afkMethod(Settings& s) {
    return s.integer("loot.afk.method", 0) == 1 ? loot::afk::Method::ZeroLimit : loot::afk::Method::ResetIdle;
}

double now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool demo() {
    static const bool on = [] {
        char* v = nullptr;
        size_t n = 0;
        const bool r = _dupenv_s(&v, &n, "BRPS3_UI_DEMO") == 0 && v && v[0] == '1';
        free(v);
        return r;
    }();
    return on;
}

std::string duration(double s) {
    if (!std::isfinite(s) || s < 0) s = 0;
    char b[48];
    if (s < 60) std::snprintf(b, sizeof b, "%.0f s", s);
    else if (s < 3600) std::snprintf(b, sizeof b, "%d min %02d s", int(s) / 60, int(s) % 60);
    else std::snprintf(b, sizeof b, "%d h %02d min", int(s) / 3600, (int(s) / 60) % 60);
    return b;
}

std::string grouped(uint64_t n) {
    std::string s = std::to_string(n);
    for (int i = int(s.size()) - 3; i > 0; i -= 3) s.insert(size_t(i), ",");
    return s;
}

void checkRow(const char* text, Mark mark, const char* detail) {
    const float h = px(27);
    const float w = availWidth();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(w, h));
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float cy = p.y + h * 0.5f;
    const float r = px(7.5f);
    const ImVec2 c(p.x + r + px(1), cy);
    switch (mark) {
    case Mark::Ok:
        d->AddCircleFilled(c, r, u32(alpha(pal::Success, 0.18f)));
        d->AddCircle(c, r, u32(alpha(pal::Success, 0.55f)), 0, px(1));
        d->PathLineTo(c + ImVec2(-r * 0.42f, 0));
        d->PathLineTo(c + ImVec2(-r * 0.1f, r * 0.32f));
        d->PathLineTo(c + ImVec2(r * 0.45f, -r * 0.35f));
        d->PathStroke(u32(pal::Success), 0, px(1.6f));
        break;
    case Mark::Bad:
        d->AddCircleFilled(c, r, u32(alpha(pal::Danger, 0.18f)));
        d->AddCircle(c, r, u32(alpha(pal::Danger, 0.55f)), 0, px(1));
        d->AddLine(c + ImVec2(-r * 0.32f, -r * 0.32f), c + ImVec2(r * 0.32f, r * 0.32f), u32(pal::Danger), px(1.6f));
        d->AddLine(c + ImVec2(r * 0.32f, -r * 0.32f), c + ImVec2(-r * 0.32f, r * 0.32f), u32(pal::Danger), px(1.6f));
        break;
    case Mark::Waiting: {
        d->AddCircle(c, r, u32(alpha(pal::Warning, 0.35f)), 0, px(1.5f));
        const float t = float(ImGui::GetTime() * 3.0);
        d->PathArcTo(c, r, t, t + 1.9f, 12);
        d->PathStroke(u32(pal::Warning), 0, px(1.6f));
        requestRedraw();
        break;
    }
    case Mark::Skip:
        d->AddLine(c + ImVec2(-r * 0.45f, 0), c + ImVec2(r * 0.45f, 0), u32(pal::Decor), px(1.5f));
        break;
    default:
        d->AddCircle(c, r - px(1), u32(pal::Decor), 0, px(1.2f));
        break;
    }
    const float tx = p.x + r * 2 + px(10);
    const bool dim = mark == Mark::Skip || mark == Mark::Unknown;
    float detailW = 0;
    if (detail && *detail) {
        const std::string s = ellipsize(detail, w * 0.45f, FontKind::Body, type::Small);
        const ImVec2 ds = textSize(FontKind::Body, type::Small, s.c_str());
        detailW = ds.x + px(8);
        drawText(d, FontKind::Body, type::Small, ImVec2(p.x + w - ds.x, textTop(FontKind::Body, type::Small, cy)),
                 u32(mark == Mark::Bad ? pal::Danger : pal::Faint), s.c_str());
    }
    const std::string t = ellipsize(text, std::max(px(40), w - (tx - p.x) - detailW), FontKind::Body, type::Label);
    drawText(d, FontKind::Body, type::Label, ImVec2(tx, textTop(FontKind::Body, type::Label, cy)), u32(dim ? pal::Muted : pal::Text), t.c_str());
    if (detail && *detail && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", detail);
}

void meter(const char* labelText, float fraction, const char* value, const ImVec4& color, float width) {
    const float w = width > 0 ? px(width) : availWidth();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = px(30);
    ImGui::Dummy(ImVec2(w, h));
    ImDrawList* d = ImGui::GetWindowDrawList();
    drawText(d, FontKind::Body, type::Small, ImVec2(p.x, p.y), u32(pal::Muted), labelText);
    if (value) {
        const ImVec2 vs = textSize(FontKind::Semibold, type::Small, value);
        drawText(d, FontKind::Semibold, type::Small, ImVec2(p.x + w - vs.x, p.y), u32(pal::Text), value);
    }
    const float by = p.y + px(21), bh = px(5);
    d->AddRectFilled(ImVec2(p.x, by), ImVec2(p.x + w, by + bh), u32(pal::FillActive), bh);
    const float f = std::clamp(fraction, 0.0f, 1.0f);
    if (f > 0) d->AddRectFilled(ImVec2(p.x, by), ImVec2(p.x + std::max(bh, w * f), by + bh), u32(color), bh);
}

void kicker(const char* text) { fieldLabel(text); }

void wrapText(const char* text, float widthPx, const ImVec4& color, float size) {
    FontScope f(FontKind::Body, size);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::max(px(60), widthPx));
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

namespace {
struct Feed {
    std::mutex m;
    std::deque<FeedItem> items;
};
Feed& feedStore() {
    static Feed f;
    return f;
}
}   

void feed(const char* source, const std::string& text, bool ok) {
    Feed& f = feedStore();
    std::lock_guard<std::mutex> lock(f.m);
    f.items.push_front({now(), source, text, ok});
    while (f.items.size() > 60) f.items.pop_back();
}

std::vector<FeedItem> feedItems() {
    Feed& f = feedStore();
    std::lock_guard<std::mutex> lock(f.m);
    return std::vector<FeedItem>(f.items.begin(), f.items.end());
}

void beginPanel(const char* id, float width, float minHeight) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, pal::Raised);
    ImGui::PushStyleColor(ImGuiCol_Border, pal::Border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(kRowRounding));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(16), px(14)));
    ImGui::SetNextWindowSizeConstraints(ImVec2(width, minHeight), ImVec2(width, FLT_MAX));
    ImGui::BeginChild(id, ImVec2(width, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AutoResizeY);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

void endPanel() { ImGui::EndChild(); }

namespace {

 

struct Tile {
    const char* title;
    const char* icon;
    const char* tab;
    Summary s;
};

void drawTile(const Tile& t, float w) {
    const float h = px(104);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(t.title);
    const bool clicked = ImGui::InvisibleButton("##tile", ImVec2(w, h));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(p, p + ImVec2(w, h), u32(hovered ? pal::RaisedHi : pal::Raised), px(kRowRounding));
    d->AddRect(p, p + ImVec2(w, h), u32(t.s.active ? alpha(t.s.color, 0.45f) : pal::Border), px(kRowRounding), 0, 1.0f);
    const float x = p.x + px(14);
    const ImVec2 is = textSize(FontKind::Icons, 14, t.icon);
    drawText(d, FontKind::Icons, 14, ImVec2(x, p.y + px(14)), u32(t.s.active ? t.s.color : pal::Muted), t.icon);
    drawText(d, FontKind::Semibold, type::Small, ImVec2(x + is.x + px(8), p.y + px(13)), u32(pal::Muted), t.title);
    if (t.s.active) {
        const ImVec2 dc(p.x + w - px(16), p.y + px(21));
        d->AddCircleFilled(dc, px(4), u32(t.s.color));
        d->AddCircle(dc, px(4) + px(3) * float(0.5 + 0.5 * std::sin(ImGui::GetTime() * 3)), u32(t.s.color, 0.35f), 0, px(1));
        requestRedraw();
    }
    ImGui::PushClipRect(p, p + ImVec2(w - px(10), h), true);
    drawText(d, FontKind::Semibold, type::Stat, ImVec2(x, p.y + px(38)), u32(t.s.active ? pal::Text : t.s.color), t.s.value.c_str());
    const std::string sub = ellipsize(t.s.sub.c_str(), w - px(28), FontKind::Body, type::Small);
    drawText(d, FontKind::Body, type::Small, ImVec2(x, p.y + px(70)), u32(pal::Muted), sub.c_str());
    ImGui::PopClipRect();
    if (hovered) ImGui::SetTooltip("%s\nClick to open the \"%s\" tab.", t.s.sub.c_str(), t.tab);
    if (clicked) openPage(Page::Loot, t.tab);
    ImGui::PopID();
}

void drawOverview(Ui&) {
    const Tile tiles[] = {
        {"PoE Chest Loop", icon::Repeat, kTabPoe, poeSummary()},
        {"AFK Keepalive", icon::Shield, kTabPoe, afkSummary()},
        {"Pickup Filter", icon::Filter, kTabPickup, pickupSummary()},
        {"Grimoire", icon::Star, kTabGrimoire, grimoireSummary()},
        {"Walkable Lighthouse", icon::Lightbulb, kTabLighthouse, lighthouseSummary()},
    };
    const float avail = availWidth();
    const int cols = avail > px(980) ? 5 : avail > px(600) ? 3 : 2;
    const float gapPx = px(10);
    const float w = std::floor((avail - gapPx * float(cols - 1)) / float(cols));
    for (int i = 0; i < 5; ++i) {
        if (i % cols) ImGui::SameLine(0, gapPx);
        else if (i) gap(gapPx / scale());
        drawTile(tiles[i], w);
    }
}

 

struct GatherState {
    std::mutex m;
    uint32_t selector = 0xC541A0C3;
    std::string message;
    bool ok = false;
};
GatherState& gs() {
    static GatherState s;
    return s;
}

struct GatherPreset {
    const char* name;
    uint32_t selector;
    const char* help;
};
const GatherPreset kGather[] = {
    {"Epiphany Chest 9", 0xC541A0C3, "The chest the LootEvents page offers (selector C541A0C3)"},
};

void runGather(Ctx& c, uint32_t sel) {
    std::string msg;
    const bool ok = loot_events::gather(c.rt(), sel, msg);
    c.log("[gather] " + world::hex8(sel) + ": " + msg);
    GatherState& s = gs();
    std::lock_guard<std::mutex> lock(s.m);
    s.ok = ok;
    s.message = msg.empty() ? (ok ? std::string("emitted") : std::string("not emitted")) : msg;
    feed("Gather", world::hex8(sel) + ": " + s.message, ok);
    if (!ok) throw Error("Native gather: " + s.message);
}

void drawGather(Ui& ui) {
    GatherState& s = gs();
    fieldLabel("CHEST SELECTOR");
    gap(sp::XS + 2);
    uint32_t sel;
    {
        std::lock_guard<std::mutex> lock(s.m);
        sel = s.selector;
    }
    if (inputHex("##sel", &sel, 120)) {
        std::lock_guard<std::mutex> lock(s.m);
        s.selector = sel;
    }
    tooltip("Gather selector hash (which chest's reward)");
    for (const GatherPreset& p : kGather) {
        ImGui::SameLine(0, px(8));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(3));
        if (button(p.name, {.tone = Tone::Ghost, .small = true, .tooltip = p.help, .selected = sel == p.selector})) {
            std::lock_guard<std::mutex> lock(s.m);
            s.selector = p.selector;
        }
    }
    gap(sp::M);
    const bool busy = ui.busy("Loot: native gather");
    {
        auto offline = requireConnection(ui);
        if (button("Gather", {.tone = Tone::Primary, .icon = icon::Play, .tooltip = "Emit the chest reward now", .busy = busy,
                              .successKey = "loot.gather"})) {
            noteQueued("Loot: native gather");
            ui.post("Loot: native gather", [sel](Ctx& c) { runGather(c, sel); },
                    [](bool ok, const std::string&) {
                        if (ok) flashSuccess("loot.gather");
                    });
        }
    }
    ImGui::SameLine(0, px(8));
    if (button("Open Loot events", {.tone = Tone::Ghost, .icon = icon::LootEvents, .tooltip = "Loot events > Native gather, with the recorder"}))
        openPage(Page::LootEvents, "Loot events");
    std::string msg;
    bool ok;
    {
        std::lock_guard<std::mutex> lock(s.m);
        msg = s.message;
        ok = s.ok;
    }
    if (!msg.empty()) {
        gap(sp::S);
        pill(msg.c_str(), ok ? pal::Success : pal::Warning, ok ? icon::Check : icon::Info);
    }
}

void drawActivity(Ui&) {
    static bool seeded = false;
    if (!seeded && demo()) {
        seeded = true;
        feed("Lighthouse", "Slice 8 requested through the teleport mailbox");
        feed("Pickup filter", "On: Legendary loot only (1,704 of 6,557 kept)");
        feed("Grimoire", "Unlocked 48 cards: 368 / 862 read back as unlocked", true);
        feed("PoE", "Chest timer refilled (9 so far)");
    }
    const std::vector<FeedItem> items = feedItems();
    if (items.empty()) {
        emptyState(icon::History, "Nothing Yet", "Starts, stops, refills, presets and unlocks of this session show up here.", 150);
        return;
    }
    const double t = now();
    int n = 0;
    for (const FeedItem& it : items) {
        if (++n > 8) break;
        ImGui::PushID(n);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = availWidth();
        const float h = px(30);
        ImGui::Dummy(ImVec2(w, h));
        ImDrawList* d = ImGui::GetWindowDrawList();
        const float cy = p.y + h * 0.5f;
        d->AddCircleFilled(ImVec2(p.x + px(4), cy), px(3), u32(it.ok ? pal::Success : pal::Danger));
        const std::string ago = duration(t - it.time);
        const ImVec2 as = textSize(FontKind::Body, type::Small, ago.c_str());
        drawText(d, FontKind::Body, type::Small, ImVec2(p.x + w - as.x, textTop(FontKind::Body, type::Small, cy)), u32(pal::Faint), ago.c_str());
        drawText(d, FontKind::Semibold, type::Small, ImVec2(p.x + px(16), textTop(FontKind::Semibold, type::Small, cy)), u32(pal::Muted), it.source.c_str());
        const float tx = p.x + px(16) + px(104);
        const std::string text = ellipsize(it.text.c_str(), std::max(px(40), w - (tx - p.x) - as.x - px(12)), FontKind::Body, type::Small);
        drawText(d, FontKind::Body, type::Small, ImVec2(tx, textTop(FontKind::Body, type::Small, cy)), u32(it.ok ? pal::Text : pal::Danger), text.c_str());
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", it.text.c_str());
        ImGui::PopID();
    }
}

void gatherAction(Ctx& c) {
    uint32_t sel;
    {
        std::lock_guard<std::mutex> lock(gs().m);
        sel = gs().selector;
    }
    runGather(c, sel);
}

}   

}   

namespace brps3 {

BR_FEATURES(loot_page) {
    using namespace lootui;
    r.section({.id = "loot.overview", .page = Page::Loot, .tab = kTabLoot, .section = "At a glance", .order = 10, .draw = drawOverview});
    r.section({.id = "loot.gather", .page = Page::Loot, .tab = kTabLoot, .section = "Native gather shortcut", .order = 20, .draw = drawGather,
               .onDisconnect = [] {
                   std::lock_guard<std::mutex> lock(gs().m);
                   gs().message.clear();
               }});
    r.section({.id = "loot.activity", .page = Page::Loot, .tab = kTabLoot, .section = "Recent activity", .order = 30, .draw = drawActivity});
    r.action({.id = "loot.gather.run", .page = Page::Loot, .tab = kTabLoot, .section = "Native gather shortcut", .label = "Native Gather (Chest Reward)",
              .help = "Spawn the selected chest's reward for your character with the game's gather call (Loot > Native gather).",
              .run = gatherAction});
    ui::styleSection("At a glance", {.fullWidth = true, .icon = ui::icon::Loot});
    ui::styleSection("Recent activity", {.icon = ui::icon::History});
    ui::styleSection("Native gather shortcut", {.icon = ui::icon::Loot, .ownsActions = true});
}

}   
