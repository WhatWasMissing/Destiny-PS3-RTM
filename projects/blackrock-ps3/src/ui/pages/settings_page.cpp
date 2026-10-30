 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

#include "core/error.h"
#include "ui/shell_internal.h"

namespace brps3::ui {
namespace {

struct Rows {
    Rows() { ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f)); }
    ~Rows() { ImGui::PopStyleVar(); }
    Rows(const Rows&) = delete;
    Rows& operator=(const Rows&) = delete;
};

 
void rowValue(const char* text, const ImVec4& color = pal::Muted) {
    const float w = availWidth();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const std::string shown = ellipsize(text, w, FontKind::Body, type::Label);
    const ImVec2 ts = textSize(FontKind::Body, type::Label, shown.c_str());
    ImGui::Dummy(ImVec2(1, px(kControlH)));
    drawText(ImGui::GetWindowDrawList(), FontKind::Body, type::Label, ImVec2(p.x + w - ts.x, textTop(FontKind::Body, type::Label, p.y + px(kControlH) * 0.5f)),
             u32(color), shown.c_str());
}

void connectionSettings() {
    ShellState& S = shell();
    Rows rows;
    const bool locked = S.status.state != EngineStatus::Disconnected;
    beginRow("set.host", "PS3 Address", 0, {.help = "webMAN MOD PS3MAPI on port 7887"});
    {
        DisabledScope off(locked, "Disconnect to change the host");
        inputMono("##sethost", &S.host, "192.168.0.254", kControlColumn, ImGuiInputTextFlags_CharsNoBlank);
        if (ImGui::IsItemDeactivatedAfterEdit() && !S.screenshot) {
            setString(key::Host, S.host);
            notify("Host saved", Level::Good);
        }
    }
    endRow();
    bool autoConnect = settings().flag(key::AutoConnect, true);
    if (toggleRow("set.autoconnect", "Connect Automatically on Launch", &autoConnect,
                  {.help = "Connects to this address when Blackrock starts, then re-applies sticky switches and values."}) &&
        !S.screenshot)
        settings().set(std::string(key::AutoConnect), autoConnect);
}

void appearanceSettings() {
    ShellState& S = shell();
    Rows rows;
    static const char* const labels[] = {"Auto", "90%", "100%", "110%", "125%", "150%", "175%"};
    static const float values[] = {0.0f, 0.9f, 1.0f, 1.1f, 1.25f, 1.5f, 1.75f};
    int sel = 0;
    for (int i = 0; i < int(IM_ARRAYSIZE(values)); ++i)
        if (std::fabs(values[i] - S.userScale) < 0.01f) sel = i;
    beginRow("set.scale", "Interface Scale", 0, {.help = "Auto follows the Windows display scale of the monitor the window is on."});
    if (combo("##scale", &sel, labels, IM_ARRAYSIZE(labels), kControlColumn) && !S.screenshot) setUserScale(values[sel]);
    endRow();
    bool rm = reduceMotion();
    if (toggleRow("set.motion", "Reduce Motion", &rm, {.help = "Turns off sliding, fading and pulsing animations."}) && !S.screenshot) {
        setReduceMotion(rm);
        settings().set(std::string(key::ReduceMotion), rm);
    }
}

void logSettings() {
    Rows rows;
    static const std::vector<std::string> levels = {"All", "Warnings", "Errors"};
    int level = std::clamp(settings().integer(key::LogLevel, 0), 0, 2);
    beginRow("set.level", "Show in the Log", 0, {.help = "Warnings includes errors."});
    if (segmented("##level", levels, &level, px(kControlColumn)) && !shell().screenshot) settings().set(std::string(key::LogLevel), level);
    endRow();
    if (actionRow("set.log", "Log", shell().logOpen ? "Hide" : "Show", {.help = "Ctrl+L or ` toggles it anywhere."})) toggleLogDrawer();
}

 
void itemRow(const Item& it, float rightW, const std::function<void()>& right) {
    const std::string label = titleCase(it.label);
    const std::string where = titleCase(std::string(pageLabel(int(it.page))) + (it.section.empty() || it.kind == ItemKind::Section ? "" : " / " + it.section));
    beginRow(it.id.c_str(), label.c_str(), rightW, {.help = where.c_str()});
    right();
    endRow();
}

void listCaption(const char* text, size_t count) {
    char buf[96];
    snprintf(buf, sizeof(buf), "%s  (%zu)", text, count);
    gap(sp::M);
    fieldLabel(buf);
    gap(sp::S);
}

void shortcutSettings() {
    std::vector<std::pair<const Item*, ImGuiKeyChord>> bound;
    for (const Item& it : items())
        if (ImGuiKeyChord c = hotkey(it.id)) bound.push_back({&it, c});
    std::vector<const Item*> pins, stickies;
    for (const std::string& id : settings().list(key::Pins))
        if (const Item* it = findItem(id)) pins.push_back(it);
    for (const std::string& id : settings().list(key::Sticky))
        if (const Item* it = findItem(id)) stickies.push_back(it);

    if (bound.empty() && pins.empty() && stickies.empty()) {
        emptyState(icon::Keyboard, "Nothing Bound or Pinned Yet", nullptr, 110);
        return;
    }
    if (!bound.empty()) {
        listCaption("Hotkeys", bound.size());
        Rows rows;
        for (auto& [it, chord] : bound) {
            const std::string name = chordName(chord);
            const Item* item = it;
            const float rightW = hotkeyChipWidth(name.c_str()) + px(6) + px(24);
            itemRow(*item, rightW, [item, name] {
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (px(kControlH) - px(22)) * 0.5f);
                const ImVec2 cp = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(hotkeyChipWidth(name.c_str()), px(22)));
                if (hotkeyChip("##chip", cp, name.c_str(), listening(item->id), true)) startListening(item->id);
                ImGui::SameLine(0, px(6));
                if (iconButton("##rm", icon::Close, "Remove this hotkey", false, 22)) setHotkey(item->id, 0);
            });
        }
    }
    if (!pins.empty()) {
        listCaption("Pinned to Home", pins.size());
        Rows rows;
        for (const Item* it : pins)
            itemRow(*it, px(24), [it] {
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (px(kControlH) - px(22)) * 0.5f);
                if (iconButton("##unpin", icon::Close, "Unpin from Home", false, 22)) {
                    settings().toggleInList(key::Pins, it->id);
                    reloadUiLists();
                    notify("Unpinned " + it->label, Level::Good);
                }
            });
    }
    if (!stickies.empty()) {
        listCaption("Re-Applied on Connect", stickies.size());
        Rows rows;
        for (const Item* it : stickies)
            itemRow(*it, px(24), [it] {
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (px(kControlH) - px(22)) * 0.5f);
                if (iconButton("##unstick", icon::Close, "Stop re-applying after connecting", false, 22)) {
                    setSticky(*it, false);
                    notify(it->label + " is no longer re-applied", Level::Good);
                }
            });
    }
     
    const bool any = bound.size() > 1 || pins.size() > 1 || stickies.size() > 1;
    if (!any) return;
    gap(sp::M);
    char buf[64];
    bool first = true;
    auto sameLine = [&first] {
        if (!first) ImGui::SameLine(0, px(8));
        first = false;
    };
    if (bound.size() > 1) {
        sameLine();
        snprintf(buf, sizeof(buf), "Clear %zu Hotkeys", bound.size());
        if (button(buf, {.tone = Tone::Ghost}))
            confirm("Clear Every Hotkey?", "Your shortcut bindings are removed. Items and pins are kept.", "Clear Hotkeys", [] {
                for (const Item& it : items())
                    if (hotkey(it.id)) setHotkey(it.id, 0);
            });
    }
    if (pins.size() > 1) {
        sameLine();
        snprintf(buf, sizeof(buf), "Unpin All %zu", pins.size());
        if (button(buf, {.tone = Tone::Ghost}))
            confirm("Unpin Everything?", "Home goes back to an empty Pinned card.", "Unpin All", [] {
                settings().setList(key::Pins, {});
                reloadUiLists();
            });
    }
    if (stickies.size() > 1) {
        sameLine();
        snprintf(buf, sizeof(buf), "Stop Re-Applying %zu Items", stickies.size());
        if (button(buf, {.tone = Tone::Ghost}))
            confirm("Stop Re-Applying Items?", "Sticky switches and values will no longer be re-applied after connecting.", "Stop Re-Applying", [] {
                settings().setList(key::Sticky, {});
                reloadUiLists();
            });
    }
    ImGui::Dummy(ImVec2(1, px(4)));
}

void storageSettings() {
    std::string dir;
    try {
        dir = state_dir().string();
    } catch (...) {
        dir = "%LOCALAPPDATA%\\BlackrockPS3";
    }
    Rows rows;
    beginRow("set.folder", "State Folder", px(320), {.help = "settings.ini, the recovery journal, the session lock and loot_event recordings live here."});
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(4));
    copyable(dir.c_str(), "path", pal::Muted);
    endRow();
    if (actionRow("set.openfolder", "Open State Folder", "Open")) openStateFolder();
}

void aboutSettings() {
    Rows rows;
    beginRow("about.app", "Blackrock PS3", px(320));
    rowValue("Destiny NPEB02021 (EU digital, 01.24)");
    endRow();
    beginRow("about.build", "Game Build", px(320));
    rowValue("51539.16.07.26.1808.v230");
    endRow();
    char buf[96];
    snprintf(buf, sizeof(buf), "Dear ImGui %s  \xC2\xB7  Direct3D 11", ImGui::GetVersion());
    beginRow("about.ui", "Interface", px(320));
    rowValue(buf);
    endRow();
    snprintf(buf, sizeof(buf), "%zu", items().size());
    beginRow("about.items", "Registered Items", px(320));
    rowValue(buf);
    endRow();
}

}   

void drawSettingsPage() {
    std::vector<CardSpec> cards;
    auto add = [&](const char* key, const char* title, void (*body)(), float pad) {
        CardSpec c{key, title, "", nullptr, false, body};
        c.pad = pad;
        cards.push_back(std::move(c));
    };
    add("set.connection", "Connection", connectionSettings, 0);
    add("set.appearance", "Appearance", appearanceSettings, 0);
    add("set.shortcuts", "Hotkeys, Pins and Sticky Items", shortcutSettings, 4);
    add("set.log", "Log", logSettings, 0);
    add("set.storage", "Storage", storageSettings, 0);
    add("set.about", "About", aboutSettings, 0);
    masonry(cards, px(sp::L));
}

}   
