 
 
 
 
 
 
 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <map>

#include "app/engine.h"
#include "app/registry.h"
#include "core/error.h"
#include "features/movement_bridge.h"
#include "features/movement_shared.h"
#include "features/movement_widgets.h"
#include "game/movement_pad.h"
#include "ui/widgets.h"

namespace brps3::mvui {

using namespace ui;
using movement::Btn;

namespace {

constexpr const char* kBindsId = "movement.binds";
constexpr const char* kPanelId = "movement.binds.panel";
constexpr float kRowH = 40;

struct BindsUi {
    std::string filter;
    std::string pickFilter;
    std::string manualFor;
    uint32_t manualChord = 0;
    bool openPicker = false, openManual = false;
    std::atomic<bool> stopBinds{false}, stopCapture{false};
};
BindsUi& B() {
    static BindsUi b;
    return b;
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string labelOf(const std::string& id) {
    if (auto it = mvbridge::item(id)) return it->label;
    return id;
}

void upsert(std::vector<movement::Bind>& list, const std::string& item, uint32_t chord) {
    for (auto& b : list)
        if (b.item == item) {
            b.chord = chord;
            return;
        }
    list.push_back({item, chord});
}

 
std::optional<std::string> chordOwner(const std::vector<movement::Bind>& list, uint32_t chord, const std::string& item) {
    for (const auto& b : list)
        if (b.chord == chord && b.item != item) return b.item;
    return std::nullopt;
}

 
void finishCapture() {
    const Capture cap = capture();
    if (!cap.active) return;
    if (now() - cap.started > 20.0) {
        stopCapture();
        notify("No chord pressed within 20 s: nothing changed", Level::Warn);
        return;
    }
    using S = movement::ChordCapture::State;
    if (cap.state == S::Done) {
        std::string why;
        const std::string text = movement::chordText(cap.chord);
        if (!movement::bindValid(cap.chord, &why)) {
            notify(text + ": " + why, Level::Warn);
            startCapture(cap.item);                    
            return;
        }
        auto list = binds();
        if (auto owner = chordOwner(list, cap.chord, cap.item)) {
            notify(text + " is already bound to " + labelOf(*owner) + ": press another chord", Level::Warn);
            startCapture(cap.item);
            return;
        }
        upsert(list, cap.item, cap.chord);
        setBinds(list);
        notify("Bound " + text + " to " + labelOf(cap.item), Level::Good);
        stopCapture();
    } else if (cap.state == S::Cancelled) {
        notify("Bind cancelled (Circle)", Level::Info);
        stopCapture();
    } else if (cap.state == S::Cleared) {
        auto list = binds();
        list.erase(std::remove_if(list.begin(), list.end(), [&](const movement::Bind& b) { return b.item == cap.item; }), list.end());
        setBinds(list);
        notify("Cleared the bind of " + labelOf(cap.item) + " (Square)", Level::Info);
        stopCapture();
    }
}

void beginCapture(const std::string& item) {
    if (!Engine::get().connected()) {
        notify(std::string(kConnectFirst) + " to record a chord: or choose the buttons by hand", Level::Warn);
        BindsUi& b = B();
        b.manualFor = item;
        b.manualChord = 0;
        for (const auto& x : binds())
            if (x.item == item) b.manualChord = x.chord;
        b.openManual = true;
        return;
    }
    startCapture(item);
    B().stopCapture = false;
    trackActivity(
        "movement.capture", "Listening for a controller chord", Page::Console,
        [] {
            if (B().stopCapture.exchange(false)) stopCapture();
            finishCapture();
            pump();
            return captureWanted();
        },
        [] { B().stopCapture = true; }, pal::Accent);
}

void setBindsOn(Ctx& c, bool on) {
    armBinds(on);
    try {
        configure(c, [](movement::Config&) {});
    } catch (...) {
        armBinds(false);
        throw;
    }
    if (!on) return;
    B().stopBinds = false;
    trackActivity(
        "movement.binds", "Controller binds on", Page::Console,
        [] {
            if (B().stopBinds.exchange(false) && mvbridge::toggleOn(kBindsId)) mvbridge::activate(kBindsId);
            finishCapture();
            return pump();
        },
        [] { B().stopBinds = true; }, pal::Info);
    const size_t n = binds().size();
    c.log("[controller] binds on: " + std::to_string(n) + (n == 1 ? " chord" : " chords") + " armed");
}

struct Suggested {
    const char* item;
    uint32_t chord;
};
const std::vector<Suggested>& suggested() {
    static const std::vector<Suggested> s = {
        {"movement.fly", movement::maskOf(Btn::L1) | movement::maskOf(Btn::Up)},
        {"movement.fly.brake", movement::maskOf(Btn::L1) | movement::maskOf(Btn::Down)},
        {"movement.tp.save", movement::maskOf(Btn::L1) | movement::maskOf(Btn::Left)},
        {"movement.tp.goto", movement::maskOf(Btn::L1) | movement::maskOf(Btn::Right)},
        {"movement.tp.return", movement::maskOf(Btn::L1) | movement::maskOf(Btn::Triangle)},
        {"movement.fly.faster", movement::maskOf(Btn::R1) | movement::maskOf(Btn::Up)},
        {"movement.fly.slower", movement::maskOf(Btn::R1) | movement::maskOf(Btn::Down)},
        {"app.panic", movement::maskOf(Btn::L2) | movement::maskOf(Btn::R2) | movement::maskOf(Btn::Select)},
    };
    return s;
}

void addSuggested() {
    auto list = binds();
    int added = 0;
    for (const Suggested& s : suggested()) {
        if (!mvbridge::item(s.item)) continue;
        if (std::any_of(list.begin(), list.end(), [&](const movement::Bind& b) { return b.item == s.item; })) continue;
        if (chordOwner(list, s.chord, s.item)) continue;
        list.push_back({s.item, s.chord});
        ++added;
    }
    setBinds(list);
    notify(added ? "Added " + std::to_string(added) + " suggested binds (aim + d-pad: in Destiny, emotes need the d-pad without L1)"
                 : std::string("The suggested binds are already there"),
           added ? Level::Good : Level::Info);
}

 

void drawPicker() {
    BindsUi& b = B();
    if (b.openPicker) {
        ImGui::OpenPopup("##movement.bindpicker");
        b.openPicker = false;
        b.pickFilter.clear();
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(px(520), px(480)), ImGuiCond_Always);
    if (!ImGui::BeginPopup("##movement.bindpicker")) return;
    heading("Bind a Switch or Action");
    gap(sp::S);
    searchField("##pick", &b.pickFilter, "Search Switches and Actions", 0, false);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere(-1);
    gap(sp::S);
    const auto list = binds();
    std::map<std::string, uint32_t> bound;
    for (const auto& x : list) bound[x.item] = x.chord;
    const std::string f = lower(b.pickFilter);
    ImGui::BeginChild("##picklist", ImVec2(0, 0), ImGuiChildFlags_None);
    int shown = 0;
    std::string lastWhere;
    for (const mvbridge::ItemInfo& it : mvbridge::bindableItems()) {
        if (!f.empty() && lower(it.label + " " + it.where + " " + it.id).find(f) == std::string::npos) continue;
        ++shown;
        ImGui::PushID(it.id.c_str());
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = availWidth(), h = px(44);
        const bool clicked = ImGui::InvisibleButton("##pickrow", ImVec2(w, h));
        const bool hov = ImGui::IsItemHovered();
        ImDrawList* d = ImGui::GetWindowDrawList();
        if (hov) d->AddRectFilled(p, p + ImVec2(w, h), u32(pal::Raised), px(kRowRounding));
        const char* glyph = it.icon ? it.icon : icon::Play;
        const ImVec2 gs = textSize(FontKind::Icons, 13, glyph);
        drawText(d, FontKind::Icons, 13, ImVec2(p.x + px(10), p.y + (h - gs.y) * 0.5f), u32(pal::Accent), glyph);
        drawText(d, FontKind::Semibold, type::Label, ImVec2(p.x + px(34), p.y + px(5)), u32(pal::Text), it.label.c_str());
        drawText(d, FontKind::Body, type::Tiny, ImVec2(p.x + px(34), p.y + px(25)), u32(pal::Faint), it.where.c_str());
        if (auto bc = bound.find(it.id); bc != bound.end()) {
            const std::string t = movement::chordText(bc->second);
            const ImVec2 ts = textSize(FontKind::Semibold, type::Tiny, t.c_str());
            drawText(d, FontKind::Semibold, type::Tiny, ImVec2(p.x + w - ts.x - px(10), p.y + (h - ts.y) * 0.5f), u32(pal::Accent), t.c_str());
        }
        if (clicked) {
            ImGui::CloseCurrentPopup();
            beginCapture(it.id);
        }
        ImGui::PopID();
    }
    if (!shown) {
        gap(sp::L);
        caption("Nothing matches.", pal::Muted);
    }
    ImGui::EndChild();
    ImGui::EndPopup();
}

void drawManual() {
    BindsUi& b = B();
    if (b.openManual) {
        ImGui::OpenPopup("##movement.bindmanual");
        b.openManual = false;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(px(440), 0), ImGuiCond_Always);
    if (!ImGui::BeginPopup("##movement.bindmanual")) return;
    heading(("Buttons for " + labelOf(b.manualFor)).c_str());
    gap(sp::M);
    const Btn order[] = {Btn::L2, Btn::L1, Btn::R1, Btn::R2, Btn::Up, Btn::Down, Btn::Left, Btn::Right,
                         Btn::Triangle, Btn::Cross, Btn::Square, Btn::Circle, Btn::L3, Btn::R3, Btn::Select, Btn::Start};
    for (int i = 0; i < 16; ++i) {
        if (i % 4) ImGui::SameLine(0, px(8));
        const Btn k = order[i];
        const bool on = (b.manualChord & movement::maskOf(k)) != 0;
        ImGui::PushID(i);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = px(92), h = px(34);
        if (ImGui::InvisibleButton("##cap", ImVec2(w, h))) b.manualChord ^= movement::maskOf(k);
        const bool hov = ImGui::IsItemHovered();
        ImDrawList* d = ImGui::GetWindowDrawList();
        d->AddRectFilled(p, p + ImVec2(w, h), u32(on ? alpha(pal::Accent, 0.16f) : hov ? pal::Raised : pal::Input), px(kFrameRounding));
        d->AddRect(p, p + ImVec2(w, h), u32(on ? pal::Accent : pal::InputBorder), px(kFrameRounding), 0, 1.0f);
        ImGui::SetCursorScreenPos(p + ImVec2((w - padCapWidth(k, 20)) * 0.5f, (h - px(20)) * 0.5f));
        padCap(k, on, 20);
        ImGui::SetCursorScreenPos(p + ImVec2(0, h));
        ImGui::Dummy(ImVec2(w, 0));
        ImGui::PopID();
    }
    gap(sp::M);
    std::string why;
    const bool valid = movement::bindValid(b.manualChord, &why);
    const auto owner = chordOwner(binds(), b.manualChord, b.manualFor);
    if (b.manualChord) {
        padChord(b.manualChord, true, 22);
        gap(sp::XS);
    }
    if (!valid) caption(why.c_str(), pal::Warning);
    else if (owner) caption(("Already bound to " + labelOf(*owner)).c_str(), pal::Warning);
    else caption("Ready.", pal::Success);
    gap(sp::M);
    {
        DisabledScope bad(!valid || owner.has_value(), "Pick a valid, unused chord first");
        if (button("Save", {.tone = Tone::Primary, .icon = icon::Check})) {
            auto list = binds();
            upsert(list, b.manualFor, b.manualChord);
            setBinds(list);
            notify("Bound " + movement::chordText(b.manualChord) + " to " + labelOf(b.manualFor), Level::Good);
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine(0, px(8));
    if (button("Cancel", {.tone = Tone::Ghost})) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

 

void drawController(Ui& ui, const Snapshot& s, float width) {
    const bool reading = ui.connected() && s.installed && s.c.padOn && s.at > 0 && now() - s.at < 2.0;
    movement::PadState pad;
    pad.connected = !reading || s.c.connected != 0;
    pad.held = reading ? s.c.held : 0;
    pad.lx = reading ? s.c.sticks[0] : 0;
    pad.ly = reading ? s.c.sticks[1] : 0;
    pad.rx = reading ? s.c.sticks[2] : 0;
    pad.ry = reading ? s.c.sticks[3] : 0;
    padDiagram(pad, width, width * 0.6f);
    gap(sp::S);
    if (!ui.connected()) statusLine(pal::Decor, "Offline", "Connect to read controller 1.", false, width);
    else if (!reading) statusLine(pal::Decor, "Not reading", "Turn on binds or the live view.", false, width);
    else if (!s.c.connected) statusLine(pal::Warning, "No controller 1", "Connect the pad to port 1.", false, width);
    else statusLine(pal::Success, "Live", s.c.held ? movement::chordText(s.c.held) : std::string("no buttons held"), true, width);
    gap(sp::XS);
    const bool live = padViewOn();
    {
        auto offline = requireConnection(ui);
        if (button(live ? "Stop Live View" : "Live View", {.tone = Tone::Ghost, .icon = live ? icon::Stop : icon::View, .small = true,
                                                           .tooltip = "Read the controller ~10 times a second without arming the binds",
                                                           .selected = live}))
            togglePadView("binds-card");
    }
}

void drawBindRows(Ui& ui, float width) {
    BindsUi& b = B();
    const auto list = binds();
    const Capture cap = capture();
    const std::string f = lower(b.filter);
    std::vector<movement::Bind> view;
    for (const auto& x : list) {
        const std::string hay = lower(labelOf(x.item) + " " + x.item + " " + movement::chordText(x.chord));
        if (f.empty() || hay.find(f) != std::string::npos) view.push_back(x);
    }
    if (list.empty()) {
        emptyState(icon::Console, "No Controller Binds Yet", "Add a Bind or Start from the Suggested Set", 190);
        return;
    }
    if (beginTable("##binds", 3, 0, ImGuiTableFlags_BordersOuter, width / scale())) {
        ImGui::TableSetupColumn("Does", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Chord", ImGuiTableColumnFlags_WidthFixed, px(200));
        ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, px(3 * kSmallControlH + 12));
        tableHeaders();
        std::string remove;
        for (const auto& x : view) {
            ImGui::TableNextRow(0, px(kRowH));
            ImGui::PushID(x.item.c_str());
            const auto info = mvbridge::item(x.item);
            const bool recording = cap.active && cap.item == x.item;
            ImGui::TableSetColumnIndex(0);
            {
                const ImVec2 p = ImGui::GetCursorScreenPos();
                ImDrawList* d = ImGui::GetWindowDrawList();
                const float cw = ImGui::GetContentRegionAvail().x;
                const std::string l = ellipsize(info ? info->label.c_str() : x.item.c_str(), cw, FontKind::Semibold, type::Label);
                drawText(d, FontKind::Semibold, type::Label, ImVec2(p.x, p.y + px(3)), u32(info ? pal::Text : pal::Warning), l.c_str());
                const std::string w = ellipsize(info ? info->where.c_str() : "not in this build (kept)", cw, FontKind::Body, type::Tiny);
                drawText(d, FontKind::Body, type::Tiny, ImVec2(p.x, p.y + px(21)), u32(pal::Faint), w.c_str());
                ImGui::Dummy(ImVec2(cw, px(kRowH) - 2 * ImGui::GetStyle().CellPadding.y));
                if (info && !info->help.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", info->help.c_str());
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (px(kRowH) - px(22)) * 0.5f - ImGui::GetStyle().CellPadding.y);
            if (recording) {
                if (cap.partial) padChord(cap.partial, true, 22);
                else {
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(3));
                    caption(cap.state == movement::ChordCapture::State::WaitRelease ? "release all buttons..." : "press the chord...", pal::Accent);
                }
                requestRedraw();
            } else {
                padChord(x.chord, false, 22);
            }
            ImGui::TableSetColumnIndex(2);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (px(kRowH) - px(kSmallControlH)) * 0.5f - ImGui::GetStyle().CellPadding.y);
            if (iconButton("##rec", icon::Record, recording ? "Listening: press the chord now" : "Record a new chord on the controller", recording,
                           kSmallControlH, recording ? pal::Danger : pal::Muted))
                recording ? stopCapture() : beginCapture(x.item);
            ImGui::SameLine(0, px(4));
            if (iconButton("##edit", icon::Keyboard, "Choose the buttons by hand", false, kSmallControlH)) {
                b.manualFor = x.item;
                b.manualChord = x.chord;
                b.openManual = true;
            }
            ImGui::SameLine(0, px(4));
            if (iconButton("##del", icon::Delete, "Remove this bind", false, kSmallControlH)) remove = x.item;
            ImGui::PopID();
        }
        if (view.empty()) {
            ImGui::TableNextRow(0, px(60));
            ImGui::TableSetColumnIndex(0);
            gap(sp::M);
            caption("No binds match.", pal::Muted);
        }
        endTable();
        if (!remove.empty()) {
            auto all = binds();
            all.erase(std::remove_if(all.begin(), all.end(), [&](const movement::Bind& y) { return y.item == remove; }), all.end());
            setBinds(all);
            notify("Removed the bind of " + labelOf(remove), Level::Info);
        }
    }
    (void)ui;
}

void drawBinds(Ui& ui) {
    seedDemo();
    finishCapture();
    BindsUi& b = B();
    const Snapshot s = snapshot();
    const Capture cap = capture();
    const float avail = availWidth();
    const bool split = avail >= px(760);
    const float leftW = split ? std::clamp(avail * 0.30f, px(280), px(340)) : std::min(avail, px(360));

    ImGui::BeginGroup();
    drawController(ui, s, leftW);
    ImGui::EndGroup();
    if (split) ImGui::SameLine(0, px(24));
    else gap(sp::L);
    ImGui::BeginGroup();
    const float rightW = split ? avail - leftW - px(24) : avail;
     
    {
        const float searchW = std::min(px(280), rightW * 0.45f);
        searchField("##bindfilter", &b.filter, "Filter Binds", searchW / scale());
        const float addW = buttonWidth("Add Bind", icon::Add), sugW = buttonWidth("Suggested", icon::Lightbulb);
        ImGui::SameLine(0, 0);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(px(8), rightW - searchW - addW - sugW - px(8)));
        if (button("Suggested", {.tone = Tone::Ghost, .icon = icon::Lightbulb,
                                 .tooltip = "Add a ready set: L1+Up fly, L1+Down hover, L1+Left / Right save / go to the quick slot, "
                                            "L1+Triangle return, R1+Up / Down fly faster / slower, L2+R2+Select Panic"}))
            addSuggested();
        ImGui::SameLine(0, px(8));
        if (button("Add Bind", {.tone = Tone::Primary, .icon = icon::Add, .tooltip = "Pick a switch or action, then press its chord"})) b.openPicker = true;
    }
    gap(sp::S);
     
    if (cap.active) {
        beginCard("##capturebanner", nullptr, {.raised = true, .accent = pal::Accent});
        statusLine(pal::Accent, ("Recording a chord for " + labelOf(cap.item)).c_str(),
                   "Hold L1 / R1 / L2 / R2 plus another button together, then release. Circle alone cancels, Square alone clears.", true);
        if (!ui.connected() || !s.installed) caption("Waiting for the movement task to read controller 1...", pal::Faint);
        ImGui::SameLine();
        alignRight(buttonWidth("Cancel", icon::Close, true));
        if (button("Cancel##cap", {.tone = Tone::Ghost, .icon = icon::Close, .small = true})) stopCapture();
        endCard();
        gap(sp::S);
    }
    drawBindRows(ui, rightW);
    gap(sp::S);
    char line[160];
    const auto list = binds();
    std::snprintf(line, sizeof line, "%zu %s  \xC2\xB7  %s", list.size(), list.size() == 1 ? "bind" : "binds",
                  bindsArmed() ? "armed: chords run while you play" : "switch on Controller binds above to arm them");
    caption(line, bindsArmed() ? pal::Success : pal::Faint);
    ImGui::EndGroup();
    drawPicker();
    drawManual();
}

void resetBindsUi() { B().stopBinds = false; }

void paletteEntries(const std::string& query, std::vector<PaletteEntry>& out) {
    (void)query;
    {
        PaletteEntry e;
        e.label = "Add a Controller Bind";
        e.where = "Console  \xE2\x80\xBA  Movement  \xE2\x80\xBA  Controller Binds";
        e.icon = icon::Console;
        e.run = [] {
            mvbridge::reveal(kPanelId);
            B().openPicker = true;
        };
        out.push_back(std::move(e));
    }
    for (const auto& b : binds()) {
        PaletteEntry e;
        e.label = "Re-Record the Chord of " + labelOf(b.item);
        e.where = "Controller Bind  \xE2\x80\xBA  " + movement::chordText(b.chord);
        e.icon = icon::Record;
        const std::string item = b.item;
        e.run = [item] {
            mvbridge::reveal(kPanelId);
            beginCapture(item);
        };
        out.push_back(std::move(e));
    }
}

}   

BR_FEATURES(movement_binds) {
    r.toggle({.id = kBindsId,
              .page = Page::Console,
              .tab = kTab,
              .section = kBinds,
              .label = "Controller Binds",
              .help = "Chords on controller 1 flip switches and run actions while you play (the Xbox binds).",
              .set = setBindsOn,
              .badge = kBadgeNeedsCheck});
    r.section({.id = kPanelId, .page = Page::Console, .tab = kTab, .section = kBinds, .order = 40, .draw = drawBinds, .onDisconnect = resetBindsUi});
    ui::styleSection(kBinds, {.fullWidth = true, .icon = ui::icon::Console});
    ui::addPaletteProvider(paletteEntries);
}

}   
