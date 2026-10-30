 
 
 
 
 
 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "app/engine.h"
#include "app/registry.h"
#include "core/error.h"
#include "core/runtime.h"
#include "features/player_shared.h"
#include "game/names_catalog.h"
#include "game/player_aura.h"
#include "game/player_editor.h"
#include "game/player_inspect.h"
#include "game/world.h"
#include "ui/widgets.h"

namespace brps3 {
namespace {

using namespace ui;
using player::Feature;
using playerui::hex8;
namespace ed = player::editor;

constexpr float kRowH = 30;
constexpr const char* kTab = "Weapons";
constexpr const char* kSecFiring = "Firing";
constexpr const char* kSecAccuracy = "Accuracy and handling";
constexpr const char* kSecDamage = "Damage";
constexpr const char* kSecEditor = "Weapon editor";
constexpr const char* kSecNearby = "Nearby";
constexpr const char* kEspLoop = "player.esp";

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

std::string nameOf(uint32_t tag) {
    if (!world::validHandle(tag)) return "(unknown)";
    return names::tagLabel(tag);
}

std::string fmtValue(double v, bool integer) {
    char b[48];
    if (integer) snprintf(b, sizeof b, "%.0f", v);
    else if (v != 0 && (std::fabs(v) < 0.001 || std::fabs(v) >= 1e6)) snprintf(b, sizeof b, "%.4g", v);
    else snprintf(b, sizeof b, "%.4f", v);
    std::string s = b;
    if (!integer && s.find('.') != std::string::npos && s.find('e') == std::string::npos) {
        while (s.size() > 1 && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    return s;
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

 
 
 

struct AuraState {
    player::aura::KillAura aura;
    float value = player::kKillAuraValue;
};

void setKillAura(Ctx& c, bool on) {
    AuraState& s = c.state<AuraState>("player.aura");
    if (on) {
        c.require(player::kKillAuraFingerprints, "Kill aura");
        s.aura.enable(c.rt(), s.value);
        c.log("[kill aura] hook at " + hex8(player::addr::KILL_AURA_SITE) + " armed, value " + fmtValue(s.value, false));
    } else {
        s.aura.disable(c.rt());
    }
}

void setKillAuraValue(Ctx& c, float v) {
    AuraState& s = c.state<AuraState>("player.aura");
    s.value = v;
    s.aura.setValue(c.rt(), v);
}

 
 
 

struct EditorUi {
    std::mutex m;
    ed::Scan scan;
    bool have = false;
    std::string error;
    double at = 0;
    std::vector<ed::Edit> journal;       
    bool journalLoaded = false;
    size_t recovered = 0;
    std::string status;
    bool statusOk = true;
     
    uint32_t weapon = 0;
    std::map<uint64_t, double> drafts;   
    std::string filter;
    int group = 0;
    bool unnamed = false;
    std::vector<ed::Preset> presets;
    bool presetsLoaded = false;
    std::string presetError, presetName, presetFilter;
    int selectedPreset = -1;
    int burst = 3;
};
EditorUi& edu() {
    static EditorUi s;
    return s;
}

const char* const kGroups[] = {"All", "Starred", "Firing", "Ammo", "Reload", "Recoil", "Spread", "Aiming", "Movement", "Other"};
constexpr const char* kStarKey = "player.editor.starred";

std::string starId(const ed::Field& f) { return f.integer ? "o:" + hex8(f.offset) : std::to_string(f.id) + ":" + hex8(f.tag); }
bool starred(const ed::Field& f) { return Engine::get().settings().inList(kStarKey, starId(f)); }

uint64_t draftKey(uint32_t weapon, uint32_t offset) { return uint64_t(weapon) << 32 | offset; }

void setStatus(const std::string& text, bool ok) {
    EditorUi& s = edu();
    std::lock_guard<std::mutex> lock(s.m);
    s.status = text;
    s.statusOk = ok;
}

bool saveJournal(const std::vector<ed::Edit>& journal) {
    std::error_code ec;
    std::filesystem::create_directories(ed::journalPath().parent_path(), ec);
    if (journal.empty()) {
        std::filesystem::remove(ed::journalPath(), ec);
        return true;
    }
    const auto tmp = ed::journalPath().string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << ed::serializeJournal(journal);
        f.flush();
        if (!f) return false;
    }
    std::filesystem::rename(tmp, ed::journalPath(), ec);
    if (ec) {
        std::filesystem::remove(ed::journalPath(), ec);
        std::filesystem::rename(tmp, ed::journalPath(), ec);
    }
    return !ec;
}

void loadJournalOnce() {
    EditorUi& s = edu();
    std::lock_guard<std::mutex> lock(s.m);
    if (s.journalLoaded) return;
    s.journalLoaded = true;
    std::error_code ec;
    if (!std::filesystem::exists(ed::journalPath(), ec)) return;
    try {
        std::ifstream f(ed::journalPath(), std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        s.journal = ed::parseJournal(ss.str());
        s.recovered = s.journal.size();
    } catch (const std::exception& e) {
        s.status = std::string("The editor recovery file is unreadable (kept as is): ") + e.what();
        s.statusOk = false;
    }
}

void refreshEditor(Ctx& c) {
    c.require(world::kFingerprints, "Weapon editor");
    loadJournalOnce();
    world::RuntimeReader mem(c.rt());
    EditorUi& s = edu();
    try {
        ed::Scan scan = ed::scanWeapons(mem);
        size_t fields = 0;
        for (const ed::Weapon& w : scan.weapons) fields += w.fields.size();
        c.log("[editor] " + std::to_string(scan.weapons.size()) + " weapon(s), " + std::to_string(fields) + " fields");
        std::lock_guard<std::mutex> lock(s.m);
        s.scan = std::move(scan);
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

ed::ByteWriter writerFor(Ctx& c) {
    return [&c](uint32_t addr, const std::vector<uint8_t>& b) { c.rt().write(addr, b.data(), b.size(), true); };
}

void postApply(Ui& ui, const ed::Weapon& w, std::vector<ed::Change> changes) {
    if (changes.empty()) return;
    const std::string job = "Apply weapon fields";
    noteQueued(job);
    const ed::ComponentRef ref = w.fire;
    const uint32_t weapon = w.handle;
    ui.post(job, [ref, weapon, changes](Ctx& c) {
        c.require(world::kFingerprints, "Weapon editor");
        loadJournalOnce();
        world::RuntimeReader mem(c.rt());
        EditorUi& s = edu();
        std::vector<ed::Edit> journal;
        {
            std::lock_guard<std::mutex> lock(s.m);
            journal = s.journal;
        }
        std::string error;
        size_t done = 0;
        const bool ok = ed::apply(mem, writerFor(c), ref, weapon, changes, journal, saveJournal, error, done);
        {
            std::lock_guard<std::mutex> lock(s.m);
            s.journal = journal;
        }
        c.log("[editor] " + std::to_string(done) + " of " + std::to_string(changes.size()) + " field(s) written" + (ok ? "" : ": " + error));
        setStatus(ok ? std::to_string(done) + (done == 1 ? " field written and verified." : " fields written and verified.") : error, ok);
        try {
            refreshEditor(c);
        } catch (const std::exception&) {
        }
        if (!ok) throw Error(error);
    }, [weapon, n = changes](bool ok, const std::string&) {
        if (!ok) return;
        EditorUi& s = edu();
        for (const ed::Change& ch : n) s.drafts.erase(draftKey(weapon, ch.field.offset));
        flashSuccess("player.editor.apply");
    });
}

 
void restoreAllEdits(Ctx& c) {
    c.require(world::kFingerprints, "Weapon editor");
    loadJournalOnce();
    world::RuntimeReader mem(c.rt());
    EditorUi& s = edu();
    std::vector<ed::Edit> journal;
    {
        std::lock_guard<std::mutex> lock(s.m);
        journal = s.journal;
    }
    if (journal.empty()) {
        c.log("[editor] restore: nothing to restore");
        return;
    }
    std::string error;
    size_t restored = 0;
    const bool ok = ed::restoreAll(mem, writerFor(c), journal, saveJournal, error, restored);
    {
        std::lock_guard<std::mutex> lock(s.m);
        s.journal = journal;
        s.recovered = std::min(s.recovered, journal.size());
    }
    setStatus(ok ? "Original values restored (" + std::to_string(restored) + ")." : error, ok);
    c.log("[editor] restore: " + std::to_string(restored) + " edit(s) restored" + (ok ? "" : "; " + error));
    try {
        refreshEditor(c);
    } catch (const std::exception&) {
    }
    if (!ok) throw Error(error);
}

void postRestoreAll(Ui& ui) {
    noteQueued("Restore weapon fields");
    ui.post("Restore weapon fields", [](Ctx& c) { restoreAllEdits(c); });
}

bool sameEdit(const ed::Edit& a, const ed::Edit& b) {
    return a.component.sameAs(b.component) && a.offset == b.offset && a.original == b.original && a.applied == b.applied;
}

 
 
void postRestoreOne(Ui& ui, const ed::Edit& which) {
    noteQueued("Restore weapon fields");
    ui.post("Restore weapon fields", [which](Ctx& c) {
        c.require(world::kFingerprints, "Weapon editor");
        loadJournalOnce();
        world::RuntimeReader mem(c.rt());
        EditorUi& s = edu();
        std::string error;
        bool ok = ed::restoreEdit(mem, writerFor(c), which, error);
        if (ok) {
            std::lock_guard<std::mutex> lock(s.m);
            auto remaining = s.journal;
            auto it = std::find_if(remaining.begin(), remaining.end(), [&](const ed::Edit& e) { return sameEdit(e, which); });
            if (it != remaining.end()) {
                const size_t index = size_t(it - remaining.begin());
                remaining.erase(it);
                if (saveJournal(remaining)) {
                    s.journal = std::move(remaining);
                    if (index < s.recovered) --s.recovered;
                } else {
                    ok = false;
                    error = which.label + ": restored, but the recovery file could not be updated (the entry is kept).";
                }
            }
        } else {
            error = which.label + ": " + error;
        }
        setStatus(ok ? which.label + " restored." : error, ok);
        c.log("[editor] restore " + which.label + (ok ? ": done" : ": " + error));
        try {
            refreshEditor(c);
        } catch (const std::exception&) {
        }
        if (!ok) throw Error(error);
    });
}

void ensurePresets() {
    EditorUi& s = edu();
    if (s.presetsLoaded) return;
    s.presetsLoaded = true;
    s.presets = ed::loadPresets(s.presetError);
}

void drawFieldTable(Ui& ui, const ed::Weapon& w, float width, float height) {
    EditorUi& s = edu();
    std::vector<int> view;
    for (int i = 0; i < int(w.fields.size()); ++i) {
        const ed::Field& f = w.fields[size_t(i)];
        const bool star = starred(f);
        if (!s.unnamed && !f.known() && !star) continue;
        if (s.group == 1 && !star) continue;
        if (s.group > 1 && f.group != kGroups[s.group]) continue;
        if (!s.filter.empty() && !matchesAll(f.label() + " " + f.group + " " + f.unit + " " + std::to_string(f.id) + " " + hex8(f.offset), s.filter))
            continue;
        view.push_back(i);
    }
    std::stable_sort(view.begin(), view.end(), [&](int a, int b) { return starred(w.fields[size_t(a)]) > starred(w.fields[size_t(b)]); });
    if (beginTable("##fields", 5, height / scale(), ImGuiTableFlags_BordersOuter, width / scale())) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Current", ImGuiTableColumnFlags_WidthFixed, px(92));
        ImGui::TableSetupColumn("New Value", ImGuiTableColumnFlags_WidthFixed, px(132));
        ImGui::TableSetupColumn("Unit", ImGuiTableColumnFlags_WidthFixed, px(80));
        ImGui::TableSetupColumn("##act", ImGuiTableColumnFlags_WidthFixed, px(36));
        tableHeaders();
        const float rowH = 46;
        const float padY = ImGui::GetStyle().CellPadding.y;
        const float inner = px(rowH) - 2 * padY;           
        auto centre = [&](float h) { ImGui::SetCursorPosY(ImGui::GetCursorPosY() + std::max(0.0f, (inner - h) * 0.5f)); };
        for (int idx : view) {
            const ed::Field& f = w.fields[size_t(idx)];
            const uint64_t key = draftKey(w.handle, f.offset);
            auto it = s.drafts.find(key);
            const bool edited = it != s.drafts.end();
            ImGui::TableNextRow(0, px(rowH));
            ImGui::PushID(idx);
            ImGui::TableSetColumnIndex(0);
            {
                const ImVec2 cp = ImGui::GetCursorScreenPos();
                ImDrawList* d = ImGui::GetWindowDrawList();
                const float nameH = textSize(FontKind::Body, type::Label, "Ag").y, subH = textSize(FontKind::Body, type::Tiny, "Ag").y;
                const float top = cp.y + std::max(0.0f, (inner - nameH - subH) * 0.5f);
                 
                const bool star = starred(f);
                ImGui::SetCursorScreenPos(ImVec2(cp.x - px(4), cp.y + (inner - px(24)) * 0.5f));
                if (ImGui::InvisibleButton("##star", ImVec2(px(24), px(24)))) Engine::get().settings().toggleInList(kStarKey, starId(f));
                const bool sh = ImGui::IsItemHovered();
                if (sh) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip(star ? "Unstar" : "Star: show this field first, on every weapon");
                }
                const char* g = star ? icon::StarFill : icon::Star;
                const ImVec2 is = textSize(FontKind::Icons, 12, g);
                drawText(d, FontKind::Icons, 12, ImVec2(cp.x - px(4) + (px(24) - is.x) * 0.5f, cp.y + (inner - is.y) * 0.5f),
                         u32(star ? pal::Accent : sh ? pal::Text : pal::Decor), g);
                ImGui::SetCursorScreenPos(cp);
                const float x = cp.x + px(24);
                const float room = ImGui::GetContentRegionAvail().x - px(30);
                const std::string nm = ellipsize(f.label().c_str(), room, FontKind::Body, type::Label);
                drawText(d, FontKind::Body, type::Label, ImVec2(x, top), u32(f.known() ? pal::Text : pal::Muted), nm.c_str());
                if (edited) {
                    const float nx = x + textSize(FontKind::Body, type::Label, nm.c_str()).x + px(7);
                    d->AddCircleFilled(ImVec2(nx, top + nameH * 0.55f), px(3), u32(pal::Accent));
                }
                char sub[96];
                snprintf(sub, sizeof sub, "%s  ·  +%X%s", f.group.c_str(), f.offset, f.id ? ("  ·  id " + std::to_string(f.id)).c_str() : "");
                drawText(d, FontKind::Body, type::Tiny, ImVec2(x, top + nameH), u32(pal::Faint), sub);
                ImGui::Dummy(ImVec2(1, inner));
                if (!f.note.empty() && ImGui::IsMouseHoveringRect(cp + ImVec2(px(22), 0), cp + ImVec2(ImGui::GetContentRegionAvail().x, inner)) &&
                    ImGui::IsWindowHovered())
                    ImGui::SetTooltip("%s", f.note.c_str());
            }
            ImGui::TableSetColumnIndex(1);
            cellText(fmtValue(f.value(), f.integer).c_str(), edited ? pal::Faint : pal::Muted, FontKind::Mono, type::MonoSmall, rowH);
            ImGui::TableSetColumnIndex(2);
            {
                auto offline = requireConnection(ui);
                centre(ImGui::GetFrameHeight());
                if (f.integer) {
                    int v = int(edited ? it->second : f.value());
                    if (inputInt("##v", &v, 0, 1000000, 122)) s.drafts[key] = double(v);
                } else {
                    float v = float(edited ? it->second : f.value());
                    if (inputFloat("##v", &v, "%.5g", 122) && std::isfinite(v)) s.drafts[key] = double(v);
                }
            }
            ImGui::TableSetColumnIndex(3);
            cellText(f.unit.c_str(), pal::Faint, FontKind::Body, type::Small, rowH);
            ImGui::TableSetColumnIndex(4);
            if (edited) {
                centre(px(28));
                if (iconButton("##undo", icon::Undo, "Discard this draft", false, 28)) s.drafts.erase(key);
            }
            ImGui::PopID();
        }
        if (view.empty()) {
            ImGui::TableNextRow(0, px(60));
            ImGui::TableSetColumnIndex(0);
            gap(sp::M);
            caption(w.fields.empty() ? "No fields on this weapon." : "No fields match.", pal::Muted);
        }
        endTable();
    }
}

void drawEditorSide(Ui& ui, const ed::Weapon& w) {
    EditorUi& s = edu();
    ensurePresets();
     
    fieldLabel("Quick Drafts");
    gap(sp::XS + 2);
    const auto& quick = ed::quickPresets();
    for (size_t i = 0; i < quick.size(); ++i) {
        if (i) ImGui::SameLine(0, px(6));
        const std::string label = std::string(quick[i].name) + (i == 2 ? " x" + std::to_string(s.burst) : "");
        if (ImGui::GetCursorPosX() + buttonWidth(label.c_str(), nullptr, true) > ImGui::GetContentRegionMax().x && i) ImGui::NewLine();
        if (button((label + "##q" + std::to_string(i)).c_str(), {.tone = Tone::Ghost, .small = true, .tooltip = quick[i].help})) {
            const auto values = ed::quickValues(i, w, s.burst);
            for (const auto& [fi, v] : values) s.drafts[draftKey(w.handle, w.fields[fi].offset)] = v;
            notify(values.empty() ? std::string(quick[i].name) + ": this weapon has none of those fields"
                                  : std::string(quick[i].name) + ": " + std::to_string(values.size()) + " drafts (review, then Apply)",
                   values.empty() ? Level::Warn : Level::Info);
        }
    }
    gap(sp::XS);
    {
        FontScope f(FontKind::Body, type::Small);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(8));
        label("Burst Count", pal::Muted);
    }
    ImGui::SameLine(0, px(8));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(8));
    inputInt("##burst", &s.burst, 1, 32, 120);
    divider(sp::M);

     
    fieldLabel("Saved Presets");
    gap(sp::XS + 2);
    if (!s.presetError.empty()) {
        wrapped(("Preset file unreadable: " + s.presetError).c_str(), pal::Warning, type::Small);
        gap(sp::XS);
    }
    if (s.presets.empty()) {
        caption("No presets yet.", pal::Faint);
    } else {
        if (s.presets.size() > 5) {
            searchField("##presetsearch", &s.presetFilter, "Find Preset", -1);
            gap(sp::XS);
        }
        const float listH = std::min(px(34) * float(s.presets.size()) + px(4), px(170));
        ImGui::BeginChild("##presets", ImVec2(0, listH), ImGuiChildFlags_None);
        for (int i = 0; i < int(s.presets.size()); ++i) {
            const ed::Preset& p = s.presets[size_t(i)];
            if (!s.presetFilter.empty() && !matchesAll(p.name, s.presetFilter)) continue;
            const ed::PresetMatch m = ed::matchPreset(p, w);
            ImGui::PushID(i);
            const ImVec2 rp = ImGui::GetCursorScreenPos();
            const float rw = availWidth();
            if (ImGui::Selectable("##p", s.selectedPreset == i, 0, ImVec2(rw, px(30)))) s.selectedPreset = s.selectedPreset == i ? -1 : i;
            ImDrawList* d = ImGui::GetWindowDrawList();
            drawText(d, FontKind::Body, type::Label, ImVec2(rp.x + px(8), textTop(FontKind::Body, type::Label, rp.y + px(15))), u32(pal::Text),
                     ellipsize(p.name.c_str(), rw - px(110), FontKind::Body, type::Label).c_str());
            const char* tag = m.sameWeapon ? "this weapon" : m.values.empty() ? "no match" : "other weapon";
            const ImVec4 tone = m.sameWeapon ? pal::Success : m.values.empty() ? pal::Faint : pal::Warning;
            const ImVec2 ts = textSize(FontKind::Body, type::Tiny, tag);
            drawText(d, FontKind::Body, type::Tiny, ImVec2(rp.x + rw - ts.x - px(8), textTop(FontKind::Body, type::Tiny, rp.y + px(15))), u32(tone), tag);
            ImGui::PopID();
        }
        ImGui::EndChild();
        if (s.selectedPreset >= 0 && s.selectedPreset < int(s.presets.size())) {
            const ed::Preset& p = s.presets[size_t(s.selectedPreset)];
            const ed::PresetMatch m = ed::matchPreset(p, w);
            gap(sp::XS);
            char line[160];
            const std::string missing = m.missing ? "  \xC2\xB7  " + std::to_string(m.missing) + " not on this weapon" : std::string();
            snprintf(line, sizeof line, "%zu %s  \xC2\xB7  archetype %08X%s", p.fields.size(), p.fields.size() == 1 ? "field" : "fields", p.archetype,
                     missing.c_str());
            caption(line, pal::Faint);
            gap(sp::XS);
            {
                DisabledScope none(m.values.empty(), "This weapon has none of the preset's fields");
                if (button("Load as Drafts", {.icon = icon::Undo, .small = true,
                                              .tooltip = m.sameWeapon ? "Replace this weapon's drafts with the preset" : "Different archetype: only matching fields load"})) {
                    for (auto it = s.drafts.begin(); it != s.drafts.end();)
                        it = uint32_t(it->first >> 32) == w.handle ? s.drafts.erase(it) : std::next(it);
                    for (const auto& [fi, v] : m.values)
                        if (std::fabs(v - w.fields[fi].value()) > 1e-9) s.drafts[draftKey(w.handle, w.fields[fi].offset)] = v;
                    notify("Loaded \"" + p.name + "\": review the drafts, then Apply", Level::Info);
                }
            }
            ImGui::SameLine(0, px(6));
            if (button("Delete", {.tone = Tone::Ghost, .icon = icon::Delete, .small = true})) {
                const std::string name = p.name;
                confirm("Delete Preset?", "\"" + name + "\" will be removed from your saved presets.", "Delete", [name] {
                    EditorUi& t = edu();
                    auto updated = t.presets;
                    updated.erase(std::remove_if(updated.begin(), updated.end(), [&](const ed::Preset& x) { return x.name == name; }), updated.end());
                    std::string err;
                    if (ed::savePresets(updated, err)) {
                        t.presets = std::move(updated);
                        t.selectedPreset = -1;
                        notify("Deleted preset " + name, Level::Good);
                    } else {
                        notify(err, Level::Error);
                    }
                });
            }
             
            ImGui::SameLine(0, px(6));
            {
                const bool clash = std::any_of(s.presets.begin(), s.presets.end(), [&](const ed::Preset& x) { return x.name == s.presetName; });
                DisabledScope none(s.presetName.empty() || clash, s.presetName.empty() ? "Type the new name below first" : "A preset already has that name");
                if (button("Rename", {.tone = Tone::Ghost, .icon = icon::Tag, .small = true, .tooltip = "Give the selected preset the name typed below"})) {
                    auto updated = s.presets;
                    const std::string from = updated[size_t(s.selectedPreset)].name;
                    updated[size_t(s.selectedPreset)].name = s.presetName;
                    std::string err;
                    if (ed::savePresets(updated, err)) {
                        s.presets = std::move(updated);
                        notify("Renamed \"" + from + "\" to \"" + s.presetName + "\"", Level::Good);
                        s.presetName.clear();
                    } else {
                        notify(err, Level::Error);
                    }
                }
            }
        }
    }
    gap(sp::S);
    inputText("##presetname", &s.presetName, "Preset Name", (availWidth() - buttonWidth("Save", icon::Add, true) - px(8)) / scale());
    ImGui::SameLine(0, px(8));
    {
        DisabledScope none(s.presetName.empty() || w.fields.empty(), s.presetName.empty() ? "Type a name first" : "No fields on this weapon");
        if (button("Save", {.icon = icon::Add, .small = true, .tooltip = "Save the current values (with your drafts) as a preset"})) {
            std::vector<double> values;
            for (const ed::Field& f : w.fields) {
                auto it = s.drafts.find(draftKey(w.handle, f.offset));
                values.push_back(it != s.drafts.end() ? it->second : f.value());
            }
            ed::Preset p = ed::presetFrom(s.presetName, w, values);
            auto updated = s.presets;
            auto found = std::find_if(updated.begin(), updated.end(), [&](const ed::Preset& x) { return x.name == p.name; });
            if (found != updated.end()) *found = p;
            else updated.push_back(p);
            std::string err;
            if (ed::savePresets(updated, err)) {
                s.presets = std::move(updated);
                notify("Saved preset \"" + p.name + "\"", Level::Good);
                s.presetName.clear();
            } else {
                notify(err, Level::Error);
            }
        }
    }
    divider(sp::M);

     
    std::vector<ed::Edit> journal;
    size_t recovered;
    {
        std::lock_guard<std::mutex> lock(s.m);
        journal = s.journal;
        recovered = s.recovered;
    }
    const size_t journalSize = journal.size();
    fieldLabel("Original Values");
    gap(sp::XS + 2);
    if (!journalSize) {
        caption("Nothing to restore.", pal::Faint);
    } else {
        std::string line = std::to_string(journalSize) + (journalSize == 1 ? " edit can be restored" : " edits can be restored");
        if (recovered) line += " (" + std::to_string(recovered) + " from an earlier session)";
        caption(line.c_str(), pal::Text);
        gap(sp::XS);
         
        const bool busy = ui.busy("Restore weapon fields");
        const float rowH = px(38);
        ImGui::BeginChild("##edits", ImVec2(0, std::min(rowH * float(journalSize) + px(2), px(190))), ImGuiChildFlags_None);
        for (size_t k = journalSize; k-- > 0;) {
            const ed::Edit& e = journal[k];
            ImGui::PushID(int(k));
            const ImVec2 rp = ImGui::GetCursorScreenPos();
            const float rw = availWidth();
            ImGui::Dummy(ImVec2(rw, rowH));
            ImDrawList* d = ImGui::GetWindowDrawList();
            const float bw = px(26);
            const std::string name = e.label + (k < recovered ? "  (earlier session)" : "");
            const float nameH = textSize(FontKind::Body, type::Small, "Ag").y, subH = textSize(FontKind::Body, type::Tiny, "Ag").y;
            const float top = rp.y + std::max(0.0f, (rowH - nameH - subH - px(2)) * 0.5f);
            drawText(d, FontKind::Body, type::Small, ImVec2(rp.x + px(2), top), u32(pal::Text),
                     ellipsize(name.c_str(), rw - bw - px(10), FontKind::Body, type::Small).c_str());
            char sub[64];
            snprintf(sub, sizeof sub, "weapon %08X  \xC2\xB7  +%X", e.weapon, e.offset);
            drawText(d, FontKind::Body, type::Tiny, ImVec2(rp.x + px(2), top + nameH + px(2)), u32(pal::Faint), sub);
            ImGui::SetCursorScreenPos(ImVec2(rp.x + rw - bw, rp.y + (rowH - bw) * 0.5f));
            {
                auto offline = requireConnection(ui);
                DisabledScope wait(busy, "A restore is running");
                if (iconButton("##restoreone", icon::Restore, "Restore this edit's original value", false, 26)) postRestoreOne(ui, e);
            }
            ImGui::SetCursorScreenPos(ImVec2(rp.x, rp.y + rowH));
            ImGui::PopID();
        }
        ImGui::EndChild();
        gap(sp::XS);
        auto offline = requireConnection(ui);
        if (button("Restore All", {.tone = Tone::Danger, .icon = icon::Restore, .small = true, .busy = busy}))
            confirm("Restore Original Weapon Values?",
                    "Every edit is undone newest first, only where the weapon and component are still the ones that were edited and the value "
                    "is still the one written (a newer change is never overwritten). Stops at the first edit that cannot be restored.",
                    "Restore", [&ui] { postRestoreAll(ui); }, false);
    }
}

void drawEditor(Ui& ui) {
    EditorUi& s = edu();
    ed::Scan scan;
    bool have;
    std::string error, status;
    bool statusOk;
    double at;
    {
        std::lock_guard<std::mutex> lock(s.m);
        scan = s.scan;
        have = s.have;
        error = s.error;
        status = s.status;
        statusOk = s.statusOk;
        at = s.at;
    }
     
    if (have && !scan.weapons.empty()) {
        std::vector<std::string> names;
        int current = 0;
        for (size_t i = 0; i < scan.weapons.size(); ++i) {
            const ed::Weapon& w = scan.weapons[i];
            int edits = 0;
            for (const auto& [k, v] : s.drafts)
                if (uint32_t(k >> 32) == w.handle) ++edits;
            std::string n = w.name.size() > 22 ? w.name.substr(0, 20) + ".." : w.name;
            if (edits) n += "  \xE2\x80\xA2" + std::to_string(edits);
            names.push_back(n);
            if (w.handle == s.weapon) current = int(i);
        }
        if (pillTabs("##weapons", names, &current)) s.weapon = scan.weapons[size_t(current)].handle;
        s.weapon = scan.weapons[size_t(std::clamp(current, 0, int(scan.weapons.size()) - 1))].handle;
    } else {
        fieldLabel("Equipped Weapons");
    }
    const std::string info = have ? std::to_string(scan.weapons.size()) + (scan.weapons.size() == 1 ? " weapon  \xC2\xB7  " : " weapons  \xC2\xB7  ") + ago(at)
                                  : std::string();
    if (refreshBar(ui, "Refresh weapons", info, "Read your equipped weapons and their fire components")) {
        noteQueued("Refresh weapons");
        ui.post("Refresh weapons", [](Ctx& c) { refreshEditor(c); });
    }
    gap(sp::M);
    if (!error.empty()) {
        pill(error.c_str(), pal::Warning, icon::Warning);
        gap(sp::S);
    }
    if (!have || scan.weapons.empty()) {
        emptyState(icon::Sliders, have ? "No Weapon Objects Found" : "Edit Your Weapons",
                   have ? "Equip a Weapon and Refresh" : "Read Your Equipped Weapons to Edit Their Fields", 170);
        if (!have && ui.connected()) {
            const float bw = buttonWidth("Read Weapons", icon::Refresh);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (availWidth() - bw) * 0.5f));
            if (button("Read Weapons", {.tone = Tone::Primary, .icon = icon::Refresh, .busy = ui.busy("Refresh weapons")}))
                ui.post("Refresh weapons", [](Ctx& c) { refreshEditor(c); });
            gap(sp::M);
        }
        return;
    }
    const ed::Weapon* wp = nullptr;
    for (const ed::Weapon& w : scan.weapons)
        if (w.handle == s.weapon) wp = &w;
    if (!wp) wp = &scan.weapons.front();
    const ed::Weapon& w = *wp;

     
    heading(w.name.c_str());
    ImGui::SameLine(0, px(10));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(1));
    hashLabel(w.handle, "weapon handle", pal::Muted);
    if (w.archetype) {
        ImGui::SameLine(0, px(8));
        caption("Archetype", pal::Faint);
        ImGui::SameLine(0, px(4));
        hashLabel(w.archetype, "archetype tag", pal::Muted);
    }
    if (w.fire.accessor) {
        ImGui::SameLine(0, px(8));
        char b[96];
        snprintf(b, sizeof b, "fire component %08X  \xC2\xB7  %u bytes  \xC2\xB7  %zu fields", w.fire.cls, w.fire.size, w.fields.size());
        caption(b, pal::Faint);
    }
    gap(sp::S);
    if (!w.note.empty()) {
        pill(w.note.c_str(), pal::Warning, icon::Info);
        gap(sp::S);
    }
     
    searchField("##fieldsearch", &s.filter, "Filter Fields", std::min(240.0f, availWidth() / scale() * 0.28f));
    ImGui::SameLine(0, px(10));
    {
        std::vector<std::string> groups(std::begin(kGroups), std::end(kGroups));
        pillTabs("##groups", groups, &s.group);
    }
    ImGui::SameLine(0, px(10));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(8));
    checkbox("Unnamed", &s.unnamed);
    tooltip("Also list records nobody has named yet (the Xbox research found 93 per weapon; ~70 are unnamed).");
    gap(sp::S);

     
    std::vector<ed::Change> changes;
    for (const ed::Field& f : w.fields) {
        auto it = s.drafts.find(draftKey(w.handle, f.offset));
        if (it != s.drafts.end() && std::fabs(it->second - f.value()) > 0) changes.push_back({f, it->second});
    }
    const float avail = availWidth();
    const bool split = avail >= px(900);
    const float sideW = split ? std::clamp(avail * 0.32f, px(280), px(360)) : avail;
    const float tableW = split ? avail - sideW - px(16) : avail;
    const float tableH = std::clamp(ImGui::GetWindowHeight() - px(300), px(320), px(620));
    ImGui::BeginGroup();
    drawFieldTable(ui, w, tableW, tableH);
    gap(sp::S);
    {
        auto offline = requireConnection(ui);
        const std::string applyLabel = changes.empty() ? std::string("Apply") : "Apply " + std::to_string(changes.size()) + (changes.size() == 1 ? " Edit" : " Edits");
        DisabledScope none(changes.empty() && ui.connected(), "Change a value first");
        if (button(applyLabel.c_str(), {.tone = Tone::Primary, .icon = icon::Check, .busy = ui.busy("Apply weapon fields"), .successKey = "player.editor.apply"}))
            postApply(ui, w, changes);
    }
    ImGui::SameLine(0, px(8));
    {
        DisabledScope none(changes.empty(), "No drafts on this weapon");
        if (button("Discard Drafts", {.tone = Tone::Ghost, .icon = icon::Delete}))
            for (auto it = s.drafts.begin(); it != s.drafts.end();) it = uint32_t(it->first >> 32) == w.handle ? s.drafts.erase(it) : std::next(it);
    }
    if (!status.empty()) {
        ImGui::SameLine(0, px(12));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(5));
        pill(status.c_str(), statusOk ? pal::Success : pal::Danger, statusOk ? icon::Check : icon::Warning);
    }
    ImGui::EndGroup();
    if (split) {
        ImGui::SameLine(0, px(16));
        beginCard("##editorside", nullptr, {.width = sideW, .raised = true});
        drawEditorSide(ui, w);
        endCard();
    } else {
        gap(sp::M);
        beginCard("##editorside", nullptr, {.raised = true});
        drawEditorSide(ui, w);
        endCard();
    }
}

 
 
 

struct EspUi {
    std::mutex m;
    player::EspSnapshot snap;
    bool have = false;
    std::string error;
    double at = 0;
    player::EspOptions opts;             
     
    uint32_t selected = 0;
    std::string filter;
     
    player::Attachments attached;
    bool haveAttached = false;
    std::string attachedError;
};
EspUi& esp() {
    static EspUi s;
    return s;
}

struct EspSession {
    std::mutex m;                        
    player::EspScanner scanner;
};

void refreshEsp(Ctx& c) {
    c.require(world::kFingerprints, "Nearby");
    world::RuntimeReader mem(c.rt());
    EspUi& s = esp();
    player::EspOptions o;
    {
        std::lock_guard<std::mutex> lock(s.m);
        o = s.opts;
    }
    try {
        EspSession& session = c.state<EspSession>("player.esp");
        player::EspSnapshot snap;
        {
            std::lock_guard<std::mutex> lock(session.m);
            snap = session.scanner.scan(mem, o);
        }
        std::lock_guard<std::mutex> lock(s.m);
        s.snap = std::move(snap);
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

void setLiveEsp(Ctx& c, bool on) {
    if (!on) {
        c.stopLoop(kEspLoop);
        return;
    }
    c.require(world::kFingerprints, "Nearby");
    try {
        refreshEsp(c);                                           
    } catch (const std::exception& e) {
        c.log(std::string("[nearby] ") + e.what());
    }
    c.startLoop(kEspLoop, 1500, [](Ctx& lc) {
        try {
            refreshEsp(lc);
        } catch (const std::exception&) {    
        }
    });
}

ImVec4 hostilityColor(player::Hostility h) {
    switch (h) {
    case player::Hostility::Hostile: return pal::Danger;
    case player::Hostility::Friendly: return pal::Success;
    case player::Hostility::Unknown: break;
    }
    return pal::Muted;
}

void chipToggle(const char* label, bool* v, const ImVec4& tone, const char* tip) {
    if (button(label, {.tone = Tone::Ghost, .small = true, .tooltip = tip, .selected = *v})) *v = !*v;
    if (*v) {
        const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(mn.x + px(9), (mn.y + mx.y) * 0.5f), px(3), u32(tone));
    }
}

void drawNearby(Ui& ui) {
    EspUi& s = esp();
    player::EspSnapshot snap;
    bool have;
    std::string error;
    double at;
    player::EspOptions opts;
    {
        std::lock_guard<std::mutex> lock(s.m);
        snap = s.snap;
        have = s.have;
        error = s.error;
        at = s.at;
        opts = s.opts;
    }
     
    bool changed = false;
    {
        fieldLabel("Radius");
        ImGui::SameLine(0, px(8));
        char b[32];
        snprintf(b, sizeof b, "%.0f m", double(opts.radius));
        caption(b, pal::Text);
        gap(sp::XS);
        float r = opts.radius;
        const Edit e = slider("##radius", &r, 10, 500, 5, std::min(320.0f, availWidth() / scale() * 0.45f));
        if (e != Edit::None) {
            opts.radius = r;
            changed = true;
        }
        ImGui::SameLine(0, px(16));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(6));
        bool bip = opts.bipeds, veh = opts.vehicles, oth = opts.others;
        chipToggle("   Bipeds", &bip, pal::Accent, "Living actors (type 9): enemies, allies, NPCs");
        ImGui::SameLine(0, px(6));
        chipToggle("   Vehicles", &veh, pal::Accent, "Vehicles and turrets (types 12, 13)");
        ImGui::SameLine(0, px(6));
        chipToggle("   Creatures", &oth, pal::Accent, "Creatures (type 10)");
        ImGui::SameLine(0, px(16));
        bool showHostile = !opts.hideHostile, showFriendly = !opts.hideFriendly, showUnknown = !opts.hideUnknown;
        chipToggle("   Hostile", &showHostile, pal::Danger, "Show actors whose faction is not allied with yours");
        ImGui::SameLine(0, px(6));
        chipToggle("   Friendly", &showFriendly, pal::Success, "Show your faction and allied factions");
        ImGui::SameLine(0, px(6));
        chipToggle("   Unknown", &showUnknown, pal::Muted, "Show actors without a faction entry");
        if (bip != opts.bipeds || veh != opts.vehicles || oth != opts.others || showHostile == opts.hideHostile || showFriendly == opts.hideFriendly ||
            showUnknown == opts.hideUnknown) {
            opts.bipeds = bip;
            opts.vehicles = veh;
            opts.others = oth;
            opts.hideHostile = !showHostile;
            opts.hideFriendly = !showFriendly;
            opts.hideUnknown = !showUnknown;
            changed = true;
        }
        if (changed) {
            std::lock_guard<std::mutex> lock(s.m);
            s.opts = opts;
        }
    }
    gap(sp::S);
    std::string info;
    if (have) {
        char b[96];
        snprintf(b, sizeof b, "%zu shown of %zu in range  \xC2\xB7  ", snap.entities.size(), snap.inRange);
        info = b + ago(at);
    }
    searchField("##espsearch", &s.filter, "Search Name, Type, Handle", std::min(280.0f, availWidth() / scale() * 0.4f));
    if (have) {
        const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
        const char* note = snap.allegianceKnown ? "Hostility: allegiance table" : "Hostility: other faction";
        const ImVec2 ts = textSize(FontKind::Body, type::Small, note);
        drawText(ImGui::GetWindowDrawList(), FontKind::Body, type::Small, ImVec2(mx.x + px(12), (mn.y + mx.y - ts.y) * 0.5f), u32(pal::Faint), note);
        if (ImGui::IsMouseHoveringRect(ImVec2(mx.x + px(12), mn.y), ImVec2(mx.x + px(12) + ts.x, mx.y)))
            ImGui::SetTooltip("%s", snap.allegianceKnown ? "Allied factions count as friendly (allegiance bitset, ownership proven)."
                                                         : "The allegiance bitset is not proven: any faction other than yours counts as hostile (the Xbox rule).");
    }
    if (refreshBar(ui, "Refresh Nearby", info, "Read the object pool once")) {
        noteQueued("Refresh Nearby");
        ui.post("Refresh Nearby", [](Ctx& c) { refreshEsp(c); });
    }
    gap(sp::S);
    if (!error.empty()) {
        pill(error.c_str(), pal::Warning, icon::Warning);
        gap(sp::S);
    }
    if (!have) {
        emptyState(icon::View, "See Who Is Around You", "Every Living Actor Within the Radius, Nearest First", 150);
        return;
    }
    const float avail = availWidth();
    const bool split = avail >= px(860);
    const float detailW = split ? std::clamp(avail * 0.34f, px(280), px(380)) : avail;
    const float tableW = split ? avail - detailW - px(16) : avail;
    const float tableH = std::clamp(34.0f + kRowH * float(std::max<size_t>(snap.entities.size(), 6)), 180.0f, 420.0f);
    std::vector<const player::EspEntity*> rows;
    for (const player::EspEntity& e : snap.entities)
        if (s.filter.empty() || matchesAll((e.tag ? nameOf(e.tag) : std::string()) + " " + world::typeName(e.type) + " " + hex8(e.handle) + " " +
                                               player::hostilityName(e.hostility) + " " + hex8(e.tag),
                                           s.filter))
            rows.push_back(&e);
    ImGui::BeginGroup();
    if (beginTable("##esp", 6, tableH, ImGuiTableFlags_BordersOuter, tableW / scale())) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##h", ImGuiTableColumnFlags_WidthFixed, px(14));
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, px(64));
        ImGui::TableSetupColumn("Dist.", ImGuiTableColumnFlags_WidthFixed, px(58));
        ImGui::TableSetupColumn("Fac.", ImGuiTableColumnFlags_WidthFixed, px(40));
        ImGui::TableSetupColumn("Handle", ImGuiTableColumnFlags_WidthFixed, px(84));
        tableHeaders();
        ImGui::PushFont(font(FontKind::Body), fontSize(FontKind::Body, type::Label));
        for (const player::EspEntity* ep : rows) {
            const player::EspEntity& e = *ep;
            ImGui::TableNextRow(0, px(kRowH));
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(int(e.handle));
            const ImVec2 cp = ImGui::GetCursorScreenPos();
            const float y = ImGui::GetCursorPosY();
            bool dbl = false;
            if (tableRowSelectable("##row", s.selected == e.handle, &dbl, kRowH - 2 * ImGui::GetStyle().CellPadding.y / scale())) {
                if (s.selected != e.handle) {
                    s.selected = e.handle;
                    s.haveAttached = false;
                    s.attachedError.clear();
                }
            }
            if (dbl) copyToClipboard(hex8(e.handle), "handle");
            ImGui::PopID();
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(cp.x + px(5), cp.y + px(kRowH) * 0.5f - ImGui::GetStyle().CellPadding.y), px(4),
                                                        u32(hostilityColor(e.hostility)));
            ImGui::SetCursorPosY(y);
            ImGui::TableSetColumnIndex(1);
            {
                const std::string nm = e.tag ? nameOf(e.tag) : std::string();
                const bool unnamed = e.tag && nm == hex8(e.tag);
                cellText(!e.tag ? "(reading name...)" : unnamed ? ("Unnamed  ·  " + nm).c_str() : nm.c_str(), !e.tag || unnamed ? pal::Faint : pal::Text,
                         FontKind::Body, type::Label, kRowH);
            }
            ImGui::TableSetColumnIndex(2);
            cellText(world::typeName(e.type), pal::Muted, FontKind::Body, type::Label, kRowH);
            ImGui::TableSetColumnIndex(3);
            char b[24];
            snprintf(b, sizeof b, "%.1f", double(e.distance));
            cellText(b, pal::Text, FontKind::Mono, type::MonoSmall, kRowH);
            ImGui::TableSetColumnIndex(4);
            cellText(e.faction < 0 ? "-" : std::to_string(e.faction).c_str(), pal::Muted, FontKind::Body, type::Label, kRowH);
            ImGui::TableSetColumnIndex(5);
            cellText(hex8(e.handle).c_str(), pal::Faint, FontKind::Mono, type::MonoSmall, kRowH);
        }
        ImGui::PopFont();
        if (rows.empty()) {
            ImGui::TableNextRow(0, px(80));
            ImGui::TableSetColumnIndex(1);
            gap(sp::M);
            caption(!snap.entities.empty() ? "Nothing matches the search." : snap.inRange ? "Everything in range is hidden by the filters." : "Nothing within the radius.",
                    pal::Muted);
        }
        endTable();
    }
    ImGui::EndGroup();
     
    const player::EspEntity* sel = nullptr;
    for (const player::EspEntity& e : snap.entities)
        if (e.handle == s.selected) sel = &e;
    if (split) ImGui::SameLine(0, px(16));
    else gap(sp::M);
    beginCard("##espdetail", nullptr, {.width = split ? detailW : 0, .raised = true});
    if (!sel) {
        emptyState(icon::View, "Pick an Actor", "Click a Row for Its Details; Double-Click Copies the Handle", split ? std::max(140.0f, tableH / scale() - 60) : 120);
    } else {
        const player::EspEntity& e = *sel;
        heading(e.tag ? nameOf(e.tag).c_str() : world::typeName(e.type));
        gap(sp::XS);
        pill(player::hostilityName(e.hostility), hostilityColor(e.hostility));
        ImGui::SameLine(0, px(6));
        char b[64];
        snprintf(b, sizeof b, "%.1f m", double(e.distance));
        pill(b, pal::Muted);
        ImGui::SameLine(0, px(6));
        pill(e.faction < 0 ? "no faction" : ("faction " + std::to_string(e.faction)).c_str(), pal::Info);
        gap(sp::S);
        caption("Handle", pal::Faint);
        ImGui::SameLine(0, px(8));
        hashLabel(e.handle, "handle", pal::Accent);
        if (e.tag) {
            ImGui::SameLine(0, px(12));
            caption("Tag", pal::Faint);
            ImGui::SameLine(0, px(8));
            hashLabel(e.tag, "tag", pal::Muted);
        }
        caption("Position", pal::Faint);
        ImGui::SameLine(0, px(8));
        snprintf(b, sizeof b, "%.2f, %.2f, %.2f", double(e.position.x), double(e.position.y), double(e.position.z));
        copyable(b, "position", pal::Text);
        gap(sp::S);
        {
            auto offline = requireConnection(ui);
            if (button("Attached Definitions", {.icon = icon::List, .small = true, .tooltip = "Read what is attached to this actor (read-only)",
                                                .busy = ui.busy("Read attachments")})) {
                const uint32_t h = e.handle;
                playerui::setAttachmentsTarget(h);
                noteQueued("Read attachments");
                ui.post("Read attachments", [h](Ctx& c) {
                    world::RuntimeReader mem(c.rt());
                    EspUi& t = esp();
                    try {
                        player::Attachments a = player::readAttachments(mem, h);
                        std::lock_guard<std::mutex> lock(t.m);
                        t.attached = std::move(a);
                        t.haveAttached = true;
                        t.attachedError.clear();
                    } catch (const std::exception& ex) {
                        std::lock_guard<std::mutex> lock(t.m);
                        t.haveAttached = false;
                        t.attachedError = ex.what();
                        throw;
                    }
                });
            }
        }
        ImGui::SameLine(0, px(6));
        if (button("Copy Row", {.tone = Tone::Ghost, .icon = icon::Copy, .small = true})) {
            snprintf(b, sizeof b, "%.3f\t%.3f\t%.3f", double(e.position.x), double(e.position.y), double(e.position.z));
            copyToClipboard(hex8(e.handle) + "\t" + hex8(e.tag) + "\t" + nameOf(e.tag) + "\t" + b, "row");
        }
        player::Attachments att;
        bool haveAtt;
        std::string attErr;
        {
            std::lock_guard<std::mutex> lock(s.m);
            att = s.attached;
            haveAtt = s.haveAttached && s.attached.owner == e.handle;
            attErr = s.attachedError;
        }
        if (!attErr.empty()) {
            gap(sp::XS);
            wrapped(attErr.c_str(), pal::Warning, type::Small);
        }
        if (haveAtt) {
            gap(sp::S);
            fieldLabel(("Attached  (" + std::to_string(att.defs.size()) + ")").c_str());
            gap(sp::XS);
            for (size_t i = 0; i < att.defs.size() && i < 12; ++i) {
                const player::Attached& d = att.defs[i];
                mono(hex8(d.tag).c_str(), pal::Muted, type::MonoSmall);
                ImGui::SameLine(0, px(8));
                caption(nameOf(d.tag).c_str(), (d.flags & 2) ? pal::Faint : pal::Text);
            }
            if (att.defs.size() > 12) caption(("+ " + std::to_string(att.defs.size() - 12) + " more").c_str(), pal::Faint);
            gap(sp::XS);
            if (button("Open in Player Tab", {.tone = Tone::Ghost, .icon = icon::List, .small = true,
                                              .tooltip = "Player > Attached definitions on this actor: filters, names, CSV, auto refresh"}))
                playerui::openAttachments(ui, e.handle);
        }
    }
    endCard();
}

 
 
 

void seedDemo() {
    static bool done = false;
    if (done || !playerui::demoMode()) return;
    done = true;
    playerui::seedDemoBoard();
    {
        EditorUi& s = edu();
        std::lock_guard<std::mutex> lock(s.m);
        ed::Weapon w;
        w.handle = 0x5A5B0123;
        w.archetype = 0x80AA2851;
        w.name = "Hand Cannon (Thorn)";
        w.fire.cls = 0x80AAB4E1;
        w.fire.size = 0x1F40;
        w.fire.accessor = 0x6B000F18;
        struct D {
            uint32_t off, id, tag;
            float v;
        };
        const D rows[] = {{0x9C0, 0, 0x8080221A, 7.5f},  {0xD30, 14240, 0x80AA27FE, 0},   {0xF10, 12932, 0x80AA27FE, 1.0f},
                          {0x1A08, 15788, 0x80AA27FE, 2.793f}, {0x10A0, 14000, 0x80AA27FE, 0.0096f}, {0x1168, 14048, 0x80AA27FE, 0.0145f},
                          {0x11E0, 14112, 0x80AA27FE, 0.0785f}, {0x1D8, 9280, 0x80AA27FE, 0.0419f}, {0x278, 9892, 0x80AA27FE, 0.1257f},
                          {0x98, 10064, 0x80AA27FE, 1.8f},  {0xC0, 9360, 0x80AA27FE, 0.333f}, {0x110, 10096, 0x80AA27FE, 0.25f}};
        std::vector<uint8_t> blob(0x1F40, 0);
        for (const D& r : rows) {
            ed::Field f;
            f.offset = r.off;
            f.id = r.id;
            f.tag = r.tag;
            f.raw = f.copy = world::floatBits(r.v);
            const player::FieldInfo* info = player::fieldInfo(r.id, r.tag);
            f.name = info ? info->name : "";
            f.group = info ? info->group : "Other";
            f.unit = info ? info->unit : "value";
            f.note = info ? info->note : "";
            w.fields.push_back(f);
        }
        for (uint32_t o : {0x1928u, 0x1924u}) {
            ed::Field f;
            f.offset = o;
            f.tag = player::wfc::AMMO_TAG;
            f.raw = f.copy = o == 0x1928 ? 12 : 84;
            f.integer = true;
            f.mirrored = false;
            f.name = o == 0x1928 ? "Clip ammo" : "Reserve ammo";
            f.group = "Ammo";
            f.unit = "rounds";
            w.fields.push_back(f);
        }
        std::stable_sort(w.fields.begin(), w.fields.end(), [](const ed::Field& a, const ed::Field& b) {
            return player::groupOrder(a.group) < player::groupOrder(b.group);
        });
        s.scan.player = 0x5A5A0007;
        s.scan.weapons.push_back(w);
        ed::Weapon w2 = w;
        w2.handle = 0x5A5C0140;
        w2.name = "Shotgun (Party Crasher+1)";
        w2.archetype = 0x80AA281F;
        s.scan.weapons.push_back(w2);
        ed::Weapon w3 = w;
        w3.handle = 0x5A5D0151;
        w3.name = "Rocket Launcher (Gjallarhorn)";
        s.scan.weapons.push_back(w3);
        s.have = true;
        s.at = nowSeconds() - 4;
        s.weapon = w.handle;
        s.drafts[draftKey(w.handle, 0x10A0)] = 0.0;
        s.drafts[draftKey(w.handle, 0x1168)] = 0.0;
        s.presets = {{"Laser Thorn", 0x80AA2851, 0x80AAB4E1, {{14000, 0x80AA27FE, 0x10A0, false, 0.0}}},
                     {"Shotgun slug", 0x80AA281F, 0x80AAB4E1, {{9892, 0x80AA27FE, 0x278, false, 0.01}}}};
        s.presetsLoaded = true;
        for (const auto& [label, off] : {std::pair<const char*, uint32_t>{"Rate of fire", 0x9C0u}, {"Vertical recoil", 0x10A0u}}) {
            ed::Edit e;
            e.label = label;
            e.weapon = w.handle;
            e.offset = off;
            e.component = w.fire;
            s.journal.push_back(e);
        }
        s.recovered = 1;
        s.journalLoaded = true;
    }
    {
        EspUi& s = esp();
        std::lock_guard<std::mutex> lock(s.m);
        s.snap.player = 0x5A5A0007;
        s.snap.playerFaction = 3;
        s.snap.allegianceKnown = true;
        struct D {
            uint32_t h, tag;
            uint8_t type;
            float d;
            int8_t f;
            player::Hostility host;
        };
        const D rows[] = {{0x5A610210, 0x80A6A07E, 9, 12.4f, 7, player::Hostility::Hostile},  {0x5A620211, 0x80A6BC21, 9, 18.9f, 7, player::Hostility::Hostile},
                          {0x5A630215, 0x80A70114, 9, 23.0f, 3, player::Hostility::Friendly}, {0x5A640220, 0x80A6D3F0, 12, 31.5f, -1, player::Hostility::Unknown},
                          {0x5A650231, 0x80A5E211, 9, 44.2f, 9, player::Hostility::Hostile},  {0x5A660232, 0, 9, 61.7f, 9, player::Hostility::Hostile}};
        for (const D& r : rows) {
            player::EspEntity e;
            e.handle = r.h;
            e.tag = r.tag;
            e.type = r.type;
            e.distance = r.d;
            e.faction = r.f;
            e.hostility = r.host;
            e.position = {120.5f + r.d, -44.25f, 12.0f};
            s.snap.entities.push_back(e);
        }
        s.snap.inRange = 9;
        s.snap.scanned = 212;
        s.have = true;
        s.at = nowSeconds() - 1;
        s.selected = 0x5A610210;
    }
}

void drawLive(Ui& ui) {
    seedDemo();
    playerui::drawLiveWrites(ui, true);
}

void resetOnDisconnect() {
    EspUi& s = esp();
    std::lock_guard<std::mutex> lock(s.m);
    s.haveAttached = false;
}

 
 
void resetEditorOnDisconnect() {
    if (playerui::demoMode()) return;
    EditorUi& s = edu();
    std::lock_guard<std::mutex> lock(s.m);
    s.scan = {};
    s.have = false;
    s.error.clear();
    s.status.clear();
}

 
void paletteEntries(const std::string& query, std::vector<PaletteEntry>& out) {
    if (query.size() < 2) return;
    EditorUi& s = edu();
    std::vector<std::pair<uint32_t, std::string>> weapons;
    {
        std::lock_guard<std::mutex> lock(s.m);
        for (const ed::Weapon& w : s.scan.weapons) weapons.push_back({w.handle, w.name});
    }
    for (const auto& [h, name] : weapons) {
        if (!matchesAll(name + " weapon editor " + hex8(h), query)) continue;
        PaletteEntry e;
        e.label = name;
        e.where = "Weapon Editor  \xE2\x80\xBA  " + hex8(h);
        e.icon = icon::Sliders;
        e.run = [h] {
            edu().weapon = h;
            openPage(Page::Console, kTab);
        };
        out.push_back(std::move(e));
    }
}

}   

BR_FEATURES(player_weapons_tab) {
    const Page P = Page::Console;
    auto toggle = [&](Feature f, const char* section, const char* label, const char* help, const char* hotkey = "") {
        r.toggle({.id = std::string("weapons.") + player::featureKey(f),
                  .page = P,
                  .tab = kTab,
                  .section = section,
                  .label = label,
                  .help = help,
                  .set = [f](Ctx& c, bool on) { playerui::setFeature(c, f, on); },
                  .badge = kBadgeNeedsCheck,
                  .hotkey = hotkey});
    };
    r.section({.id = "weapons.live", .page = P, .tab = kTab, .section = playerui::kLiveWrites, .order = 5, .draw = drawLive});
     
    toggle(Feature::RateOfFire, kSecFiring, "Rate of Fire",
           "Writes the rate-of-fire value to four fire records of every weapon you hold, every frame (Xbox +0xA34 / +0xA5C / +0xA84 / +0xAAC).");
    toggle(Feature::FullAuto, kSecFiring, "Full Auto",
           "Semi-automatic weapons fire while the trigger is held: the fire-mode word +0x980 goes from 01010000 to 0 (only that encoding is "
           "touched) and comes back when you switch it off.");
    toggle(Feature::Ammo, kSecFiring, "Infinite Ammo",
           "Keeps the ammo count at the ammo value every frame: the Xbox +0x1998 record, else the clip count (+0x1928 when +0x1920 holds the "
           "ammo tag 80802449; proven on Xbox).");
    toggle(Feature::Magazine, kSecFiring, "No Reload",
           "Writes the magazine size to the two magazine records every frame (Xbox +0x24C / +0x29C, Bullet Mag).");
    r.value({.id = "weapons.rpm_value", .page = P, .tab = kTab, .section = kSecFiring, .label = "Rounds per Minute",
             .help = "What Rate of fire writes (Xbox constant 1000). Applies live while it is on.", .min = 1, .max = 2000,
             .def = player::kRpmValue, .step = 1, .format = "%.0f",
             .set = [](Ctx& c, float v) { playerui::editMagnitudes(c, [v](player::Magnitudes& m) { m.rpm = v; }); },
             .parent = "weapons.rate_of_fire"});
    r.value({.id = "weapons.ammo_value", .page = P, .tab = kTab, .section = kSecFiring, .label = "Ammo Count",
             .help = "What Infinite ammo writes, in rounds (Xbox constant 1000). A fusion charge uses 7 rounds.", .min = 1, .max = 9999,
             .def = float(player::kAmmoValue), .step = 1, .format = "%.0f",
             .set = [](Ctx& c, float v) { playerui::editMagnitudes(c, [v](player::Magnitudes& m) { m.ammo = uint32_t(std::lround(v)); }); },
             .parent = "weapons.infinite_ammo"});
    r.value({.id = "weapons.magazine_value", .page = P, .tab = kTab, .section = kSecFiring, .label = "Magazine Size",
             .help = "What No reload writes (Xbox constant 1,000,000).", .min = 10, .max = 1000000, .def = player::kMagazineValue, .step = 10,
             .format = "%.0f",
             .set = [](Ctx& c, float v) { playerui::editMagnitudes(c, [v](player::Magnitudes& m) { m.magazine = v; }); },
             .parent = "weapons.no_reload"});
     
    toggle(Feature::Recoil, kSecAccuracy, "No Recoil", "Writes 0 to the eight recoil records of every weapon you hold, every frame (Xbox +0x10C4, stride 0x28).");
    toggle(Feature::Spread, kSecAccuracy, "No Spread", "Writes 0 to the spread record every frame (Xbox +0xF84).");
    toggle(Feature::Handling, kSecAccuracy, "Instant Handling", "Writes 0 to the three handling records every frame (Xbox +0x94 / +0xBC / +0xE4).");
    toggle(Feature::ZoomMult, kSecAccuracy, "No Zoom Multiplier", "Writes 0 to the zoom-multiplier record every frame (Xbox +0x10C, Zoom Mult).");
    toggle(Feature::ZoomSpeed, kSecAccuracy, "Instant Zoom", "Writes 0 to the zoom-speed record every frame (Xbox +0x134, Zoom Speed).");
     
    toggle(Feature::Damage, kSecDamage, "Max Damage", "Writes the largest float to the damage record of every weapon you hold, every frame (Xbox +0xF34).");
    r.toggle({.id = "weapons.kill_aura",
              .page = P,
              .tab = kTab,
              .section = kSecDamage,
              .label = "Kill Aura",
              .help = "Native TTK hook (Lighthouse port): while on, every object passing through the folded accessor at 0x21F258 gets "
                      "the aura value stored into its TTK scalar (+0x10) just before the accessor reads it. Off removes the hook (its stub "
                      "is retired safely); objects it already touched keep the value until the game rewrites it, as on Xbox.",
              .set = setKillAura,
              .hotkey = "Ctrl+Shift+K"});
    r.value({.id = "weapons.kill_aura_value", .page = P, .tab = kTab, .section = kSecDamage, .label = "Strength",
             .help = "The scalar the kill aura hook stores (the Xbox stub always stored 1000). Applies live while kill aura is on.",
             .min = 1, .max = 10000,
             .def = player::kKillAuraValue, .step = 1, .format = "%.0f", .set = setKillAuraValue, .parent = "weapons.kill_aura"});
     
    r.section({.id = "weapons.editor", .page = P, .tab = kTab, .section = kSecEditor, .order = 60, .draw = drawEditor,
               .onDisconnect = resetEditorOnDisconnect, .badge = kBadgeNeedsCheck});
    r.action({.id = "weapons.editor.refresh", .page = P, .tab = kTab, .section = kSecEditor, .label = "Refresh Weapon Editor",
              .help = "Read your equipped weapons and the fields of their fire components.", .run = refreshEditor});
    r.action({.id = "weapons.editor.restore", .page = P, .tab = kTab, .section = kSecEditor, .label = "Restore Weapon Edits",
              .help = "Undo every weapon editor edit, newest first, where the weapon and component are still the edited ones and the value "
                      "is still the one written (a newer change is never overwritten). The recovery file survives app restarts.",
              .run = restoreAllEdits, .dangerous = true});
     
    r.toggle({.id = "weapons.esp_live",
              .page = P,
              .tab = kTab,
              .section = kSecNearby,
              .label = "Live",
              .help = "Re-read the nearby list every 1.5 seconds (the Xbox ESP snapshot; PS3 has no overlay, so it is a list here).",
              .set = setLiveEsp,
              .badge = kBadgeNeedsCheck});
    r.section({.id = "weapons.nearby", .page = P, .tab = kTab, .section = kSecNearby, .order = 70, .draw = drawNearby, .onDisconnect = resetOnDisconnect});
    r.action({.id = "weapons.nearby.refresh", .page = P, .tab = kTab, .section = kSecNearby, .label = "Refresh Nearby",
              .help = "Read the actors around you once.", .run = refreshEsp});

    styleSection(kSecFiring, {.icon = icon::Play});
    styleSection(kSecAccuracy, {.icon = icon::Sliders});
    styleSection(kSecDamage, {.icon = icon::LootEvents});
    styleSection(kSecEditor, {.fullWidth = true, .icon = icon::Sliders, .ownsActions = true});
    styleSection(kSecNearby, {.fullWidth = true, .icon = icon::View, .ownsActions = true});
    addPaletteProvider(paletteEntries);
}

}   
