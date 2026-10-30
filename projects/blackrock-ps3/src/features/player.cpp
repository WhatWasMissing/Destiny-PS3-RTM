 
 
 
 
 
 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include <windows.h>
#include <shellapi.h>

#include "app/engine.h"
#include "app/registry.h"
#include "core/error.h"
#include "core/runtime.h"
#include "features/player_shared.h"
#include "game/names_catalog.h"
#include "game/player_inspect.h"
#include "game/world.h"
#include "ui/widgets.h"

namespace brps3 {
namespace {

using namespace ui;
using player::Feature;
using playerui::hex8;

constexpr float kRowH = 30;
constexpr const char* kTab = "Player";
constexpr const char* kSecSurvival = "Survival";
constexpr const char* kSecAbilities = "Ability recharge";
constexpr const char* kSecReach = "Reach";
constexpr const char* kSecLoadout = "Loadout";
constexpr const char* kSecBuffs = "Attached definitions";
constexpr const char* kSecStats = "Stats";
constexpr const char* kBuffsLoop = "player.buffs.auto";

double nowSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool matchesAll(const std::string& hay, const std::string& query) {
    std::string word;
    const std::string h = lower(hay);
    for (char c : lower(query) + " ") {
        if (c == ' ') {
            if (!word.empty() && h.find(word) == std::string::npos) return false;
            word.clear();
        } else {
            word += c;
        }
    }
    return true;
}

std::string ago(double at) {
    if (at <= 0) return {};
    const double s = std::max(0.0, nowSeconds() - at);
    char b[48];
    if (s < 60) snprintf(b, sizeof b, "updated %.0f s ago", s);
    else snprintf(b, sizeof b, "updated %.0f min ago", s / 60);
    return b;
}

 
bool refreshBar(Ui& ui, const char* job, const std::string& info, const char* tip) {
    const float bw = buttonWidth("Refresh", icon::Refresh, true);
    const ImVec2 ts = textSize(FontKind::Body, type::Small, info.c_str());
    ImGui::SameLine();
    alignRight(ts.x + px(12) + bw);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(5));
    caption(info.c_str(), pal::Faint);
    ImGui::SameLine(0, px(12));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(5));
    auto offline = requireConnection(ui);
    return button(("Refresh##" + std::string(job)).c_str(), {.icon = icon::Refresh, .small = true, .tooltip = tip, .busy = ui.busy(job)});
}

 
std::string nameOf(uint32_t tag) {
    if (!world::validHandle(tag)) return "(empty)";
    std::string n = names::tagLabel(tag);
    return n.empty() ? hex8(tag) : n;
}

 
struct NameEditor {
    uint32_t tag = 0;
    std::string text;
    bool open = false;
    void begin(uint32_t t) {
        tag = t;
        auto l = names::Labels::get().find(t);
        text = l ? l->name : names::tagName(t);
        open = true;
    }
    void draw(const char* id) {
        if (!open) return;
        ImGui::PushID(id);
        gap(sp::S);
        const float bw = buttonWidth("Save", icon::Check, true) + buttonWidth("Cancel", nullptr, true) + px(8);
        inputText("##name", &text, "Name for This Tag (Saved with Your Labels)", (availWidth() - bw - px(8)) / scale());
        ImGui::SameLine(0, px(8));
        if (button("Save", {.tone = Tone::Primary, .icon = icon::Check, .small = true})) {
            if (text.empty()) names::Labels::get().remove(tag);
            else names::Labels::get().set(tag, {text, "", false});
            notify(text.empty() ? "Label removed for " + hex8(tag) : "Saved \"" + text + "\" for " + hex8(tag), Level::Good);
            open = false;
        }
        ImGui::SameLine(0, px(8));
        if (button("Cancel", {.tone = Tone::Ghost, .small = true})) open = false;
        ImGui::PopID();
    }
};

 
 
 

struct LoadoutUi {
    std::mutex m;
    player::Loadout data;
    bool have = false;
    std::string error;
    double at = 0;
     
    std::string filter;
    int kind = 0;    
    int selected = -1;
    NameEditor names;
};
LoadoutUi& lo() {
    static LoadoutUi s;
    return s;
}

void refreshLoadout(Ctx& c) {
    c.require(world::kFingerprints, "Loadout");
    world::RuntimeReader mem(c.rt());
    LoadoutUi& s = lo();
    try {
        player::Loadout l = player::readLoadout(mem);
        c.log("[loadout] " + std::to_string(l.refs.size()) + " references on player " + hex8(l.player) + " and its weapons");
        std::lock_guard<std::mutex> lock(s.m);
        s.data = std::move(l);
        s.have = true;
        s.error.clear();
        s.at = nowSeconds();
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(s.m);
        s.error = e.what();
        s.at = nowSeconds();
        throw;
    }
}

void drawLoadout(Ui& ui) {
    LoadoutUi& s = lo();
    player::Loadout data;
    bool have;
    std::string error;
    double at;
    {
        std::lock_guard<std::mutex> lock(s.m);
        data = s.data;
        have = s.have;
        error = s.error;
        at = s.at;
    }
     
    searchField("##loadoutsearch", &s.filter, "Search Names, Tags, Slots", std::min(320.0f, availWidth() / scale() * 0.38f));
    ImGui::SameLine(0, px(12));
    pillTabs("##loadoutkind", {"All", "Abilities", "Weapons", "Talents"}, &s.kind);
    const std::string info = have ? std::to_string(data.refs.size()) + " references  \xC2\xB7  " + ago(at) : std::string();
    if (refreshBar(ui, "Refresh Loadout", info, "Read the ability, archetype and talent references of your character and weapons")) {
        noteQueued("Refresh Loadout");
        ui.post("Refresh Loadout", [](Ctx& c) { refreshLoadout(c); });
    }
    gap(sp::M);
    if (!error.empty()) {
        pill(error.c_str(), pal::Warning, icon::Warning);
        gap(sp::S);
    }
    if (have && data.unreadable) {
        const std::string note = std::to_string(data.unreadable) +
                                 (data.unreadable == 1 ? " loadout component was" : " loadout components were") +
                                 " unreadable or changed while reading (not shown): refresh";
        pill(note.c_str(), pal::Warning, icon::Info);
        gap(sp::S);
    }
    if (!have) {
        emptyState(icon::Tag, "Read Your Loadout", "Ability Slots, Weapon Archetypes and Perk References", 150);
        return;
    }
    std::vector<int> view;
    for (int i = 0; i < int(data.refs.size()); ++i) {
        const player::LoadoutRef& r = data.refs[size_t(i)];
        if (s.kind == 1 && r.kind != "Ability reference") continue;
        if (s.kind == 2 && r.kind != "Weapon archetype") continue;
        if (s.kind == 3 && r.kind != "Talent reference") continue;
        const std::string hay = nameOf(r.tag) + " " + hex8(r.tag) + " " + r.kind + " " + r.slotName + " " + hex8(r.owner);
        if (!s.filter.empty() && !matchesAll(hay, s.filter)) continue;
        view.push_back(i);
    }
    const float tableH = std::clamp(34.0f + kRowH * float(std::max<size_t>(view.size(), 3)), 130.0f, 330.0f);
    if (beginTable("##loadout", 5, tableH, ImGuiTableFlags_BordersOuter)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, px(130));
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, px(140));
        ImGui::TableSetupColumn("Owner", ImGuiTableColumnFlags_WidthFixed, px(120));
        ImGui::TableSetupColumn("Tag", ImGuiTableColumnFlags_WidthFixed, px(84));
        tableHeaders();
        ImGui::PushFont(font(FontKind::Body), fontSize(FontKind::Body, type::Label));
        ImGuiListClipper clip;
        clip.Begin(int(view.size()), px(kRowH));
        while (clip.Step()) {
            for (int row = clip.DisplayStart; row < clip.DisplayEnd; ++row) {
                const int idx = view[size_t(row)];
                const player::LoadoutRef& r = data.refs[size_t(idx)];
                ImGui::TableNextRow(0, px(kRowH));
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(idx);
                const float y = ImGui::GetCursorPosY();
                if (tableRowSelectable("##row", s.selected == idx, nullptr, kRowH - 2 * ImGui::GetStyle().CellPadding.y / scale())) {
                    s.selected = s.selected == idx ? -1 : idx;
                    s.names.open = false;
                }
                ImGui::PopID();
                ImGui::SetCursorPosY(y);
                cellText(r.slotName.c_str(), pal::Muted, FontKind::Body, type::Label, kRowH);
                ImGui::TableSetColumnIndex(1);
                const bool empty = !world::validHandle(r.tag);
                const std::string nm = nameOf(r.tag);
                const bool unnamed = !empty && nm == hex8(r.tag);
                cellText(unnamed ? "Unnamed tag" : nm.c_str(), empty || unnamed ? pal::Faint : pal::Text, FontKind::Body, type::Label, kRowH);
                ImGui::TableSetColumnIndex(2);
                cellText(r.kind.c_str(), pal::Muted, FontKind::Body, type::Label, kRowH);
                ImGui::TableSetColumnIndex(3);
                cellText(r.weapon ? ("weapon " + hex8(r.owner)).c_str() : "you", r.weapon ? pal::Muted : pal::Accent, FontKind::Body, type::Label, kRowH);
                ImGui::TableSetColumnIndex(4);
                cellText(hex8(r.tag).c_str(), empty ? pal::Faint : pal::Muted, FontKind::Mono, type::MonoSmall, kRowH);
            }
        }
        ImGui::PopFont();
        if (view.empty()) {
            ImGui::TableNextRow(0, px(60));
            ImGui::TableSetColumnIndex(1);
            gap(sp::M);
            caption(data.refs.empty() ? "No supported references found (class tags need the live check: see Port checks)." : "Nothing matches.",
                    pal::Muted);
        }
        endTable();
    }
    if (s.selected >= 0 && s.selected < int(data.refs.size())) {
        const player::LoadoutRef& r = data.refs[size_t(s.selected)];
        gap(sp::S);
        beginCard("##loadoutdetail", nullptr, {.raised = true});
        heading(nameOf(r.tag).c_str());
        gap(sp::XS);
        hashLabel(r.tag, "tag", pal::Accent);
        ImGui::SameLine(0, px(10));
        pill(r.kind.c_str(), pal::Info);
        tooltip("A definition link: its presence alone does not prove a perk is active. Swapping an ability or talent needs the game's "
                "unbind / bind lifecycle; the Xbox tool did not expose a swap either.");
        ImGui::SameLine(0, px(6));
        pill(r.slotName.c_str(), pal::Muted);
        gap(sp::XS);
        char line[200];
        snprintf(line, sizeof line, "Owner %s  \xC2\xB7  component class %08X at %08X  \xC2\xB7  tag at +%X  \xC2\xB7  associated %08X  \xC2\xB7  flags %08X",
                 hex8(r.owner).c_str(), r.cls, r.component, r.offset, r.associated, r.flags);
        caption(line, pal::Faint);
        gap(sp::S);
        {
            DisabledScope none(!world::validHandle(r.tag), "Empty slot");
            if (button("Name This Tag", {.tone = Tone::Ghost, .icon = icon::Tag, .small = true})) s.names.begin(r.tag);
            ImGui::SameLine(0, px(6));
            if (button("Copy Tag", {.tone = Tone::Ghost, .icon = icon::Copy, .small = true})) copyToClipboard(hex8(r.tag), "tag");
        }
        s.names.draw("loadoutname");
        endCard();
    }
}

 
 
 

struct BuffsUi {
    std::mutex m;
    player::Attachments snap;
    bool have = false;
    std::string error;
    double at = 0;
    std::atomic<uint32_t> target{0};      
    std::atomic<bool> autoRefresh{false};
     
    std::string filter;
    bool hideDisabled = false, hopOnsOnly = false, savedOnly = false;
    NameEditor names;
};
BuffsUi& bf() {
    static BuffsUi s;
    return s;
}

void refreshBuffs(Ctx& c) {
    c.require(world::kFingerprints, "Attached definitions");
    world::RuntimeReader mem(c.rt());
    BuffsUi& s = bf();
    try {
        uint32_t owner = s.target.load();
        if (!owner) owner = world::requireLocalPlayer(mem).handle;
        player::Attachments a = player::readAttachments(mem, owner);
        std::lock_guard<std::mutex> lock(s.m);
        s.snap = std::move(a);
        s.have = true;
        s.error.clear();
        s.at = nowSeconds();
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(s.m);
        s.error = e.what();
        s.at = nowSeconds();
        throw;
    }
}

void setBuffsAuto(Ui& ui, bool on) {
    BuffsUi& s = bf();
    s.autoRefresh = on;
    if (on)
        ui.post("Auto-refresh attached definitions", [](Ctx& c) {
            c.startLoop(kBuffsLoop, 2000, [](Ctx& lc) {
                if (!bf().autoRefresh.load()) {
                    lc.stopLoop(kBuffsLoop);
                    return;
                }
                try {
                    refreshBuffs(lc);
                } catch (const std::exception&) {    
                }
            });
        });
    else
        ui.post("Stop auto-refresh", [](Ctx& c) { c.stopLoop(kBuffsLoop); });
}

std::string csvQuote(const std::string& v) {
    std::string out = "\"";
    for (char ch : v) {
        if (ch == '"') out += '"';
        out += ch;
    }
    return out + "\"";
}

void exportBuffs(const player::Attachments& a) {
    const auto dir = state_dir() / "player";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    char name[96];
    snprintf(name, sizeof name, "attachments-%08X-%lld.csv", a.owner, static_cast<long long>(std::time(nullptr)));
    const auto path = dir / name;
    std::ofstream f(path, std::ios::binary);
    f << "owner,definition,name,arena,address,size,flags\n";
    for (const player::Attached& d : a.defs)
        f << hex8(a.owner) << ',' << hex8(d.tag) << ',' << csvQuote(nameOf(d.tag)) << ',' << hex8(d.arenaHandle) << ',' << hex8(d.address) << ','
          << d.size << ',' << hex8(d.flags) << '\n';
    f.flush();
    if (f) notify("Saved " + path.filename().string() + " in the state folder", Level::Good);
    else notify("Could not save " + path.string(), Level::Error);
}

void drawBuffs(Ui& ui) {
    BuffsUi& s = bf();
    player::Attachments snap;
    bool have;
    std::string error;
    double at;
    {
        std::lock_guard<std::mutex> lock(s.m);
        snap = s.snap;
        have = s.have;
        error = s.error;
        at = s.at;
    }
     
    {
        const uint32_t t = s.target.load();
        pill(t ? ("Actor " + hex8(t)).c_str() : "You (follows respawns)", t ? pal::Info : pal::Accent, t ? icon::View : icon::Home);
        tooltip(t ? "Picked in Weapons > Nearby" : "Your own character; pick another actor in Weapons > Nearby");
        if (t) {
            ImGui::SameLine(0, px(6));
            if (button("Me", {.tone = Tone::Ghost, .small = true, .tooltip = "Inspect your own character again"})) {
                s.target = 0;
                if (ui.connected()) ui.post("Refresh Attached Definitions", [](Ctx& c) { refreshBuffs(c); });
            }
        }
        const std::string info = have ? std::to_string(snap.defs.size()) + " attached  ·  " + ago(at) : std::string();
        if (refreshBar(ui, "Refresh Attached Definitions", info, "Read the arena chain of the target (sObject +0x3C)")) {
            noteQueued("Refresh Attached Definitions");
            ui.post("Refresh Attached Definitions", [](Ctx& c) { refreshBuffs(c); });
        }
    }
    gap(sp::S);
    searchField("##buffsearch", &s.filter, "Filter Names, Tags, Notes", std::max(140.0f, std::min(240.0f, availWidth() / scale() - 330)));
    ImGui::SameLine(0, px(8));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(3));
    if (button("Hop-Ons##bf", {.tone = Tone::Ghost, .small = true, .tooltip = "Only hop-on definitions (catalogue types 20-23)", .selected = s.hopOnsOnly}))
        s.hopOnsOnly = !s.hopOnsOnly;
    ImGui::SameLine(0, px(6));
    if (button("Labelled##bf", {.tone = Tone::Ghost, .small = true, .tooltip = "Only tags you have named", .selected = s.savedOnly}))
        s.savedOnly = !s.savedOnly;
    ImGui::SameLine(0, px(6));
    if (button("Hide Disabled##bf", {.tone = Tone::Ghost, .small = true, .tooltip = "Hide arenas whose lookup is disabled (+0 flag 2) and empty tags",
                                     .selected = s.hideDisabled}))
        s.hideDisabled = !s.hideDisabled;
    gap(sp::XS);
    {
        auto offline = requireConnection(ui);
        bool autoOn = s.autoRefresh.load();
        if (toggleRow("buffsauto", "Auto Refresh", &autoOn, {.help = "Re-read every 2 seconds while this is on."})) setBuffsAuto(ui, autoOn);
    }
    if (!error.empty()) {
        gap(sp::XS);
        pill(error.c_str(), pal::Warning, icon::Warning);
    }
    gap(sp::S);
    if (!have) {
        emptyState(icon::Shield, "See What Is Attached", "Every Definition on the Actor's Arena Chain", 140);
        return;
    }
    std::vector<int> view;
    for (int i = 0; i < int(snap.defs.size()); ++i) {
        const player::Attached& d = snap.defs[size_t(i)];
        if (s.hideDisabled && ((d.flags & 2) || !world::validHandle(d.tag))) continue;    
        if (s.hopOnsOnly) {
            const names::Entity* e = names::entities().find(names::fromPs3Tag(d.tag));
            if (!e || e->type < 20 || e->type > 23) continue;
        }
        if (s.savedOnly && !names::Labels::get().find(d.tag)) continue;
        if (!s.filter.empty()) {
            auto l = names::Labels::get().find(d.tag);
            if (!matchesAll(nameOf(d.tag) + " " + hex8(d.tag) + (l ? " " + l->notes : ""), s.filter)) continue;
        }
        view.push_back(i);
    }
    const float tableH = std::clamp(34.0f + kRowH * float(std::max<size_t>(view.size(), 3)), 130.0f, 300.0f);
    int clicked = -1;
    if (beginTable("##buffs", 3, tableH, ImGuiTableFlags_BordersOuter)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Definition", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Tag", ImGuiTableColumnFlags_WidthFixed, px(84));
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, px(110));
        tableHeaders();
        ImGui::PushFont(font(FontKind::Body), fontSize(FontKind::Body, type::Label));
        for (int row : view) {
            const player::Attached& d = snap.defs[size_t(row)];
            ImGui::TableNextRow(0, px(kRowH));
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(row);
            const float y = ImGui::GetCursorPosY();
            if (tableRowSelectable("##row", s.names.open && s.names.tag == d.tag, nullptr, kRowH - 2 * ImGui::GetStyle().CellPadding.y / scale()))
                clicked = row;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) {
                const std::string tip = "Arena " + hex8(d.arenaHandle) + " at " + hex8(d.address) + ", " + std::to_string(d.size) +
                                        " bytes. Click to name this definition.";
                ImGui::SetTooltip("%s", tip.c_str());
            }
            ImGui::PopID();
            ImGui::SetCursorPosY(y);
            const std::string nm = nameOf(d.tag);
            const bool unnamed = nm == hex8(d.tag);
            cellText(unnamed ? "Unnamed definition" : nm.c_str(), unnamed ? pal::Faint : pal::Text, FontKind::Body, type::Label, kRowH);
            ImGui::TableSetColumnIndex(1);
            cellText(hex8(d.tag).c_str(), pal::Muted, FontKind::Mono, type::MonoSmall, kRowH);
            ImGui::TableSetColumnIndex(2);
            cellText((d.flags & 2) ? "Lookup disabled" : "Attached", (d.flags & 2) ? pal::Faint : pal::Success, FontKind::Body, type::Label, kRowH);
        }
        ImGui::PopFont();
        if (view.empty()) {
            ImGui::TableNextRow(0, px(60));
            ImGui::TableSetColumnIndex(0);
            gap(sp::M);
            caption(snap.defs.empty() ? "Nothing attached." : "Nothing matches the filters.", pal::Muted);
        }
        endTable();
    }
    if (clicked >= 0) s.names.begin(snap.defs[size_t(clicked)].tag);
    s.names.draw("buffname");
    gap(sp::S);
    if (button("Save CSV", {.tone = Tone::Ghost, .icon = icon::Log, .small = true, .tooltip = "Save this snapshot to the state folder"}))
        exportBuffs(snap);
    ImGui::SameLine(0, px(6));
    if (button("Copy Tags", {.tone = Tone::Ghost, .icon = icon::Copy, .small = true})) {
        std::string text;
        for (int row : view) text += hex8(snap.defs[size_t(row)].tag) + "\t" + nameOf(snap.defs[size_t(row)].tag) + "\n";
        copyToClipboard(text, "attached definitions");
    }
}

 
 
 

struct StatsUi {
    std::mutex m;
    player::StatRegistry registry;
    player::StatusAggregate status;
    bool haveRegistry = false, haveStatus = false;
    std::string registryError, statusError;
    double at = 0;
    std::string filter;    
};
StatsUi& stt() {
    static StatsUi s;
    return s;
}

void refreshStats(Ctx& c) {
    c.require(world::kFingerprints, "Stats");
    c.require(player::kStatsFingerprints, "Stats");
    world::RuntimeReader mem(c.rt());
    StatsUi& s = stt();
    std::string regErr, stErr;
    player::StatRegistry reg;
    player::StatusAggregate st;
    bool okReg = false, okSt = false;
    try {
        reg = player::readStatRegistry(mem);
        okReg = true;
    } catch (const std::exception& e) {
        regErr = e.what();
    }
    try {
        st = player::readStatus(mem);
        okSt = true;
    } catch (const std::exception& e) {
        stErr = e.what();
    }
    {
        std::lock_guard<std::mutex> lock(s.m);
        if (okReg) s.registry = std::move(reg);
        if (okSt) s.status = std::move(st);
        s.haveRegistry = okReg;
        s.haveStatus = okSt;
        s.registryError = regErr;
        s.statusError = stErr;
        s.at = nowSeconds();
    }
    c.log("[stats] registry: " + (okReg ? std::to_string(s.registry.refs.size()) + " references" : regErr) +
          "; status: " + (okSt ? std::to_string(s.status.records.size()) + " records" : stErr));
    if (!okReg && !okSt) throw Error(stErr);
}

void drawStats(Ui& ui) {
    StatsUi& s = stt();
    player::StatRegistry reg;
    player::StatusAggregate st;
    bool haveReg, haveSt;
    std::string regErr, stErr;
    double at;
    {
        std::lock_guard<std::mutex> lock(s.m);
        reg = s.registry;
        st = s.status;
        haveReg = s.haveRegistry;
        haveSt = s.haveStatus;
        regErr = s.registryError;
        stErr = s.statusError;
        at = s.at;
    }
    fieldLabel("Local Status Aggregate");
    tooltip("The player's status component (descriptor +0x25BC): up to four merged records (max value / priority, OR of flags, sum of "
            "contributions).");
    if (refreshBar(ui, "Refresh Stats", at > 0 ? ago(at) : std::string(), "Read the status aggregate and the registered stat table")) {
        noteQueued("Refresh Stats");
        ui.post("Refresh Stats", [](Ctx& c) { refreshStats(c); });
    }
    gap(sp::XS + 2);
    if (!haveSt) {
        if (!stErr.empty()) caption(stErr.c_str(), pal::Warning);
    } else {
        char line[160];
        snprintf(line, sizeof line, "Component %08X (class %08X) at %08X  \xC2\xB7  %zu / 4 records", st.handle, st.cls, st.data, st.records.size());
        caption(line, pal::Faint);
        gap(sp::XS);
        if (st.records.empty()) caption("The aggregate is empty right now.", pal::Muted);
        else if (beginTable("##status", 5, 0, ImGuiTableFlags_BordersOuter)) {
            ImGui::TableSetupColumn("Identifier", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, px(90));
            ImGui::TableSetupColumn("Priority", ImGuiTableColumnFlags_WidthFixed, px(70));
            ImGui::TableSetupColumn("Flags", ImGuiTableColumnFlags_WidthFixed, px(50));
            ImGui::TableSetupColumn("Contrib.", ImGuiTableColumnFlags_WidthFixed, px(70));
            tableHeaders();
            for (const player::StatusRecord& r : st.records) {
                char b[32];
                ImGui::TableNextRow(0, px(kRowH));
                ImGui::TableSetColumnIndex(0);
                cellText(hex8(r.id).c_str(), pal::Text, FontKind::Mono, type::MonoSmall, kRowH);
                ImGui::TableSetColumnIndex(1);
                snprintf(b, sizeof b, "%.6g", double(r.value));
                cellText(b, pal::Accent, FontKind::Mono, type::MonoSmall, kRowH);
                ImGui::TableSetColumnIndex(2);
                cellText(std::to_string(r.priority).c_str(), pal::Muted, FontKind::Body, type::Label, kRowH);
                ImGui::TableSetColumnIndex(3);
                snprintf(b, sizeof b, "%02X", unsigned(r.flags));
                cellText(b, pal::Muted, FontKind::Mono, type::MonoSmall, kRowH);
                ImGui::TableSetColumnIndex(4);
                cellText(std::to_string(r.contributions).c_str(), pal::Muted, FontKind::Body, type::Label, kRowH);
            }
            endTable();
        }
        gap(sp::XS);
        if (button("Copy Status", {.tone = Tone::Ghost, .icon = icon::Copy, .small = true})) {
            std::string text = "identifier\tvalue\tpriority\tflags\tcontributions\n";
            for (const player::StatusRecord& r : st.records) {
                char b[160];
                snprintf(b, sizeof b, "%08X\t%.9g\t%d\t%02X\t%d\n", r.id, double(r.value), r.priority, unsigned(r.flags), r.contributions);
                text += b;
            }
            copyToClipboard(text, "status aggregate");
        }
    }
    divider(sp::M);
    fieldLabel("Registered Stat References");
    tooltip("The \"known stats all handles\" table (0x1AB9820) with its 15 category counts. Reference ids use separate biped / weapon "
            "investment offset namespaces: they are not entity tags or writable stat values.");
    gap(sp::XS + 2);
    if (!haveReg) {
        if (!regErr.empty()) caption(regErr.c_str(), pal::Warning);
        return;
    }
    searchField("##statsearch", &s.filter, "Filter ID, Namespace, Offset, Category", std::min(300.0f, availWidth() / scale() * 0.6f));
    std::vector<const player::StatRef*> view;
    for (const player::StatRef& r : reg.refs) {
        char details[96];
        snprintf(details, sizeof details, " %08X %u %u %08X", r.id, unsigned(r.category), unsigned(r.index), r.handle);
        if (s.filter.empty() || matchesAll(player::statLabel(r.id) + details, s.filter)) view.push_back(&r);
    }
    ImGui::SameLine();
    const std::string count = std::to_string(view.size()) + " of " + std::to_string(reg.refs.size());
    alignRight(textSize(FontKind::Body, type::Small, count.c_str()).x);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(8));
    caption(count.c_str(), pal::Faint);
    gap(sp::S);
    if (beginTable("##statreg", 4, 240, ImGuiTableFlags_BordersOuter)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Reference", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, px(84));
        ImGui::TableSetupColumn("Cat / Index", ImGuiTableColumnFlags_WidthFixed, px(84));
        ImGui::TableSetupColumn("Handle", ImGuiTableColumnFlags_WidthFixed, px(84));
        tableHeaders();
        ImGui::PushFont(font(FontKind::Body), fontSize(FontKind::Body, type::Label));
        ImGuiListClipper clip;
        clip.Begin(int(view.size()), px(kRowH));
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                const player::StatRef& r = *view[size_t(i)];
                ImGui::TableNextRow(0, px(kRowH));
                ImGui::TableSetColumnIndex(0);
                cellText(player::statLabel(r.id).c_str(), pal::Text, FontKind::Body, type::Label, kRowH);
                ImGui::TableSetColumnIndex(1);
                cellText(hex8(r.id).c_str(), pal::Muted, FontKind::Mono, type::MonoSmall, kRowH);
                ImGui::TableSetColumnIndex(2);
                cellText((std::to_string(r.category) + " / " + std::to_string(r.index)).c_str(), pal::Muted, FontKind::Body, type::Label, kRowH);
                ImGui::TableSetColumnIndex(3);
                cellText(hex8(r.handle).c_str(), pal::Faint, FontKind::Mono, type::MonoSmall, kRowH);
            }
        ImGui::PopFont();
        endTable();
    }
}

 
 
 

void seedDemo() {
    static bool done = false;
    if (done || !playerui::demoMode()) return;
    done = true;
    playerui::seedDemoBoard();
    {
        LoadoutUi& s = lo();
        std::lock_guard<std::mutex> lock(s.m);
        const uint32_t me = 0x5A5A0007, gun = 0x5A5B0123;
        auto add = [&](uint32_t owner, bool weapon, const char* kind, const char* slot, uint32_t tag, uint32_t off) {
            player::LoadoutRef r;
            r.owner = owner;
            r.weapon = weapon;
            r.kind = kind;
            r.slotName = slot;
            r.tag = tag;
            r.offset = off;
            r.cls = weapon ? 0x8080271E : 0x80AA28D4;
            r.component = 0x6A0C4200;
            s.data.refs.push_back(r);
        };
        add(me, false, "Ability reference", "Grenade", 0x80A723CC, 0x240);
        add(me, false, "Ability reference", "Melee", 0x80A6A01B, 0x248);
        add(me, false, "Ability reference", "Movement", 0x80A7248A, 0x24C);
        add(me, false, "Ability reference", "Super", 0x80A6A016, 0x250);
        add(gun, true, "Weapon archetype", "Archetype", 0x80AA2851, 0x7C);
        add(gun, true, "Talent reference", "Perk 1", 0x80A6F2A1, 0x124);
        add(gun, true, "Talent reference", "Perk 2", 0x80A6F2B7, 0x164);
        s.data.player = me;
        s.have = true;
        s.at = nowSeconds() - 3;
    }
    {
        BuffsUi& s = bf();
        std::lock_guard<std::mutex> lock(s.m);
        s.snap.owner = 0x5A5A0007;
        s.snap.type = world::TYPE_BIPED;
        const uint32_t tags[] = {0x80A6A07E, 0x80A6BC21, 0x80A70114, 0x80A6D3F0, 0x80A5E211};
        for (uint32_t i = 0; i < 5; ++i) s.snap.defs.push_back({tags[i], 0x6D0A0010 + i, 0x6A1E0000 + 0x400 * i, 0x180, i == 3 ? 2u : 0u});
        s.have = true;
        s.at = nowSeconds() - 1;
    }
    {
        StatsUi& s = stt();
        std::lock_guard<std::mutex> lock(s.m);
        s.status.handle = 0xE3D2001C;
        s.status.cls = 0x80802555;
        s.status.data = 0x6A2F3C40;
        s.status.records = {{0x80000014, world::floatBits(1.25f), 1.25f, 2, 1, 0x01}, {0x4000002C, world::floatBits(0.8f), 0.8f, 0, 2, 0x00}};
        s.registry.base = 0x33D9DA80;
        s.registry.stride = 0x14;
        s.registry.capacity = 0xAD;
        for (uint32_t i = 0; i < 24; ++i)
            s.registry.refs.push_back({0xE0000000u | i, (i % 3 == 0 ? 0x80000000u : i % 3 == 1 ? 0x40000000u : 0x00120000u) | (i * 0x14),
                                       0x33D9DA80 + i * 0x14, uint16_t(i % 7), uint8_t(i % 15)});
        s.haveStatus = s.haveRegistry = true;
        s.at = nowSeconds() - 2;
    }
}

void drawLoadoutSeeded(Ui& ui) {
    seedDemo();
    drawLoadout(ui);
}

void drawLive(Ui& ui) {
    seedDemo();
    playerui::drawLiveWrites(ui, false);
}

void resetOnDisconnect() {
    BuffsUi& s = bf();
    s.autoRefresh = false;
    s.target = 0;
}

void setOn(Feature f, Ctx& c, bool on) { playerui::setFeature(c, f, on); }

}   

void playerui::setAttachmentsTarget(uint32_t handle) { bf().target = handle; }

void playerui::openAttachments(Ui& ui, uint32_t handle) {
    bf().target = handle;
    if (ui.connected()) {
        ui::noteQueued("Refresh Attached Definitions");
        ui.post("Refresh Attached Definitions", [](Ctx& c) { refreshBuffs(c); });
    }
    ui::openPage(Page::Console, kTab);
}

BR_FEATURES(player_tab) {
    const Page P = Page::Console;
    auto toggle = [&](Feature f, const char* section, const char* label, const char* help, const char* hotkey = "") {
        r.toggle({.id = std::string("player.") + player::featureKey(f),
                  .page = P,
                  .tab = kTab,
                  .section = section,
                  .label = label,
                  .help = help,
                  .set = [f](Ctx& c, bool on) { setOn(f, c, on); },
                  .badge = kBadgeNeedsCheck,
                  .hotkey = hotkey});
    };
    r.section({.id = "player.live", .page = P, .tab = kTab, .section = playerui::kLiveWrites, .order = 5, .draw = drawLive});
    toggle(Feature::God, kSecSurvival, "God Mode",
           "Writes 2147483648.0 to the player component's god-mode record every frame (Xbox +0x25A4). Lands only where the live "
           "record signature confirms the field; the original comes back when you switch it off.",
           "Ctrl+Shift+G");
    toggle(Feature::Super, kSecAbilities, "Super Recharge",
           "Writes the recharge value to the super record every frame (Xbox +0x31A4, Super Charge).");
    toggle(Feature::Grenade, kSecAbilities, "Grenade Recharge",
           "Writes the recharge value to the grenade record every frame (Xbox +0x31CC, Grenade Charge).");
    toggle(Feature::Melee, kSecAbilities, "Melee Recharge",
           "Writes the recharge value to the melee record every frame (Xbox +0x31F4, Melee Charge).");
    r.value({.id = "player.recharge_value", .page = P, .tab = kTab, .section = kSecAbilities, .label = "Recharge Value",
             .help = "What the three recharge switches write (Xbox constant: 10000, instant). Applies live while a switch is on.",
             .min = 1, .max = 10000, .def = player::kAbilityValue, .step = 1, .format = "%.0f",
             .set = [](Ctx& c, float v) { playerui::editMagnitudes(c, [v](player::Magnitudes& m) { m.ability = v; }); }});
    toggle(Feature::Interact, kSecReach, "Interact Range",
           "Writes the range value to the interact-range record every frame (Xbox +0x2D14): use and pick up from far away.");
    toggle(Feature::Pickup, kSecReach, "Pickup Range",
           "Writes the range value to the seven pickup-range records every frame (Xbox +0x2D3C, stride 0x28): ammo and engrams fly to you.");
    r.value({.id = "player.range_value", .page = P, .tab = kTab, .section = kSecReach, .label = "Range",
             .help = "What the range switches write (Xbox constant 100000). Applies live while a switch is on.",
             .min = 10, .max = 100000, .def = player::kRangeValue, .step = 10, .format = "%.0f",
             .set = [](Ctx& c, float v) { playerui::editMagnitudes(c, [v](player::Magnitudes& m) { m.range = v; }); }});

    r.section({.id = "player.loadout", .page = P, .tab = kTab, .section = kSecLoadout, .order = 60, .draw = drawLoadoutSeeded,
               .badge = kBadgeNeedsCheck});
    r.section({.id = "player.buffs", .page = P, .tab = kTab, .section = kSecBuffs, .order = 70, .draw = drawBuffs, .onDisconnect = resetOnDisconnect,
               .badge = kBadgeNeedsCheck});
    r.section({.id = "player.stats", .page = P, .tab = kTab, .section = kSecStats, .order = 80, .draw = drawStats, .badge = kBadgeNeedsCheck});
    r.action({.id = "player.loadout.refresh", .page = P, .tab = kTab, .section = kSecLoadout, .label = "Refresh Loadout",
              .help = "Read the ability, archetype and talent references of your character and weapons.", .run = refreshLoadout});
    r.action({.id = "player.buffs.refresh", .page = P, .tab = kTab, .section = kSecBuffs, .label = "Refresh Attached Definitions",
              .help = "Read the definitions attached to your character (or the actor picked in Nearby).", .run = refreshBuffs});
    r.action({.id = "player.stats.refresh", .page = P, .tab = kTab, .section = kSecStats, .label = "Refresh Stats",
              .help = "Read the local status aggregate and the registered stat table.", .run = refreshStats});

    styleSection(playerui::kLiveWrites, {.fullWidth = true, .icon = icon::Shield});
    styleSection(kSecSurvival, {.icon = icon::Shield});
    styleSection(kSecAbilities, {.icon = icon::Lightbulb});
    styleSection(kSecReach, {.icon = icon::Link});
    styleSection(kSecLoadout, {.fullWidth = true, .icon = icon::Tag, .ownsActions = true});
    styleSection(kSecBuffs, {.icon = icon::List, .ownsActions = true});
    styleSection(kSecStats, {.icon = icon::Sliders, .ownsActions = true});
}

}   
