 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "app/engine.h"
#include "core/runtime.h"
#include "features/player_shared.h"
#include "game/world.h"
#include "ui/widgets.h"

namespace brps3::playerui {

using namespace ui;
using player::Feature;
using player::FeatureState;

bool demoMode() {
    static const bool on = [] {
        char* v = nullptr;
        size_t n = 0;
        const bool d = _dupenv_s(&v, &n, "BRPS3_UI_DEMO") == 0 && v && v[0] == '1';
        free(v);
        return d;
    }();
    return on;
}

std::string hex8(uint32_t v) {
    char b[12];
    snprintf(b, sizeof b, "%08X", v);
    return b;
}

namespace {

struct SessionFlags {
    bool armed = false;
};

player::WriteManager::Log logTo(Ctx& c) {
    return [&c](const std::string& s) { c.log(s); };
}

void shutdownWrites(Ctx& c) {
    try {
        world::RuntimeReader mem(c.rt());
        writes(c).shutdown(c.rt(), mem, logTo(c));
    } catch (const std::exception& e) {
        c.log(std::string("[player] stopping the writes: ") + e.what());
    }
    player::resetBoard();
}

void armSession(Ctx& c) {
     
     
    auto& flags = c.state<SessionFlags>("player.session");
    if (!flags.armed) {
        flags.armed = true;
        c.onDisconnect([](Ctx& d) { shutdownWrites(d); });
    }
    c.onPanic("player.writes", [](Ctx& p) { shutdownWrites(p); });
}

void ensureLoop(Ctx& c) {
    if (c.loopRunning(kWritesLoop)) return;
    c.startLoop(kWritesLoop, 500, [](Ctx& lc) {
        try {
            world::RuntimeReader mem(lc.rt());
            writes(lc).tick(lc.rt(), mem, logTo(lc));
        } catch (const std::exception& e) {
            lc.log(std::string("[player] writes check: ") + e.what());    
        }
        if (!writes(lc).anyOn()) lc.stopLoop(kWritesLoop);
    });
}

}   

player::WriteManager& writes(Ctx& c) { return c.state<player::WriteManager>("player.writes"); }

void setFeature(Ctx& c, Feature f, bool on) {
    player::WriteManager& m = writes(c);
    if (on) {
        c.require(world::kFingerprints, "Player and weapon switches");
        armSession(c);
    }
    world::RuntimeReader mem(c.rt());
    m.setFeature(c.rt(), mem, f, on, logTo(c));
    if (m.anyOn()) ensureLoop(c);
    else c.stopLoop(kWritesLoop);
}

void editMagnitudes(Ctx& c, const std::function<void(player::Magnitudes&)>& edit) {
    player::WriteManager& m = writes(c);
    player::Magnitudes mags = m.magnitudes();
    edit(mags);
    world::RuntimeReader mem(c.rt());
    m.setMagnitudes(c.rt(), mem, mags, logTo(c));
}

 

namespace {

ImVec4 levelColor(FeatureState::Level l) {
    switch (l) {
    case FeatureState::Active: return pal::Success;
    case FeatureState::Partial: return pal::Warning;
    case FeatureState::Waiting: return pal::Info;
    case FeatureState::Refused: return pal::Danger;
    case FeatureState::Off: break;
    }
    return pal::Muted;
}

const char* levelWord(const FeatureState& s) {
    switch (s.level) {
    case FeatureState::Active: return s.landing ? "Writing" : "Armed";
    case FeatureState::Partial: return s.landing ? "Partly" : "Partial";
    case FeatureState::Waiting: return "Waiting";
    case FeatureState::Refused: return "No match";
    case FeatureState::Off: break;
    }
    return "Off";
}

void chip(Feature f, const FeatureState& s, float w) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = px(52);
    ImGui::PushID(int(f));
    ImGui::InvisibleButton("##chip", ImVec2(w, h));
    const bool hov = ImGui::IsItemHovered();
    ImGui::PopID();
    ImDrawList* d = ImGui::GetWindowDrawList();
    const ImVec4 tone = levelColor(s.level);
    d->AddRectFilled(p, p + ImVec2(w, h), u32(hov ? pal::RaisedHi : pal::Raised), px(kRowRounding));
    d->AddRect(p, p + ImVec2(w, h), u32(pal::Border), px(kRowRounding), 0, 1.0f);
    d->AddRectFilled(p + ImVec2(0, px(10)), p + ImVec2(px(3), h - px(10)), u32(tone), px(2));
     
    const char* word = levelWord(s);
    const ImVec2 ws = textSize(FontKind::Semibold, type::Tiny, word);
    const ImVec2 ps(ws.x + px(s.landing ? 30 : 16), px(20));
    const ImVec2 pp(p.x + w - ps.x - px(12), p.y + (h - ps.y) * 0.5f);
    d->AddRectFilled(pp, pp + ps, u32(alpha(tone, 0.14f)), ps.y * 0.5f);
    float tx = pp.x + px(8);
    if (s.landing) {
        const float t = float(ImGui::GetTime());
        const float pulse = 0.55f + 0.45f * std::sin(t * 6.0f);
        d->AddCircleFilled(ImVec2(pp.x + px(12), pp.y + ps.y * 0.5f), px(3.5f), u32(tone, pulse));
        tx += px(14);
        requestRedraw();
    }
    drawText(d, FontKind::Semibold, type::Tiny, ImVec2(tx, pp.y + (ps.y - ws.y) * 0.5f), u32(mix(tone, pal::Text, 0.3f)), word);
     
    const float lx = p.x + px(16);
    const float room = pp.x - lx - px(10);
    ImGui::PushClipRect(p, ImVec2(pp.x - px(6), p.y + h), true);
    drawText(d, FontKind::Semibold, type::Label, ImVec2(lx, p.y + px(8)), u32(pal::Text), player::featureName(f));
    const std::string text = ellipsize(s.text.c_str(), room, FontKind::Body, type::Small);
    drawText(d, FontKind::Body, type::Small, ImVec2(lx, p.y + px(28)), u32(pal::Muted), text.c_str());
    ImGui::PopClipRect();
    if (hov && !s.text.empty()) tooltip(s.text.c_str());
}

}   

void drawLiveWrites(Ui& ui, bool weaponsTab) {
    const player::Board b = player::boardSnapshot();
    std::vector<Feature> on;
    for (size_t i = 0; i < player::kFeatureCount; ++i) {
        const Feature f = Feature(i);
        if (player::weaponFeature(f) != weaponsTab) continue;
        if (b.features[i].level != FeatureState::Off) on.push_back(f);
    }
    ImDrawList* d = ImGui::GetWindowDrawList();
    if (on.empty()) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = availWidth();
        const float h = px(58);
        ImGui::Dummy(ImVec2(w, h));
        tooltip("Each switch writes only where the live record signature confirms the field; the console re-checks the owner chain every "
                "frame and the originals come back when you switch it off.");
        d->AddRectFilled(p, p + ImVec2(w, h), u32(pal::Raised), px(kRowRounding));
        d->AddRect(p, p + ImVec2(w, h), u32(pal::Border), px(kRowRounding), 0, 1.0f);
        const ImVec2 is = textSize(FontKind::Icons, 16, icon::Shield);
        drawText(d, FontKind::Icons, 16, ImVec2(p.x + px(16), p.y + (h - is.y) * 0.5f), u32(pal::Muted), icon::Shield);
        drawText(d, FontKind::Semibold, type::Label, ImVec2(p.x + px(44), textTop(FontKind::Semibold, type::Label, p.y + h * 0.5f)), u32(pal::Text),
                 ui.connected() ? "No Switch on This Tab Is On" : "Connect, Load into an Activity, Then Switch Something On");
        return;
    }
     
    {
        int writing = 0;
        for (Feature f : on) writing += b.features[size_t(f)].level == FeatureState::Active || b.features[size_t(f)].level == FeatureState::Partial;
        char head[160];
        snprintf(head, sizeof head, "%d of %zu writing", writing, on.size());
        std::string sub;
        if (b.playerHandle) sub += "player " + hex8(b.playerHandle);
        if (weaponsTab && b.playerHandle) sub += "  \xC2\xB7  " + std::to_string(b.weapons) + (b.weapons == 1 ? " weapon" : " weapons");
        if (b.frames) {
            char fr[48];
            snprintf(fr, sizeof fr, "  \xC2\xB7  %u frames", b.frames);
            sub += fr;
        }
        const bool good = writing == int(on.size());
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(4));
        statusDot(good ? pal::Success : writing ? pal::Warning : pal::Info, writing > 0);
        ImGui::SameLine(0, px(8));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(4));
        {
            FontScope fs(FontKind::Semibold, type::Label);
            ImGui::TextUnformatted(head);
        }
        if (!sub.empty()) {
            ImGui::SameLine(0, px(10));
            caption(sub.c_str(), pal::Faint);
        }
        if (!b.problems.empty() || !b.lastError.empty()) {
            std::string notes;
            if (!b.lastError.empty()) notes += b.lastError + "\n";
            for (const std::string& pr : b.problems) notes += pr + "\n";
            const std::string label = std::to_string(b.problems.size() + (b.lastError.empty() ? 0 : 1)) + " note" +
                                      (b.problems.size() + (b.lastError.empty() ? 0 : 1) == 1 ? "" : "s");
            ImGui::SameLine();
            alignRight(badgePillWidth(label.c_str()));
            badgePill(label.c_str());
            tooltip(notes.c_str());
        }
    }
    gap(sp::S);
     
    const float avail = availWidth();
    const int cols = avail > px(620) ? 2 : 1;
    const float cw = std::floor((avail - px(8) * float(cols - 1)) / float(cols));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(px(8), px(8)));
    for (size_t i = 0; i < on.size(); ++i) {
        if (i % size_t(cols)) ImGui::SameLine(0, px(8));
        chip(on[i], b.features[size_t(on[i])], cw);
    }
    ImGui::PopStyleVar();
}

void seedDemoBoard() {
    player::Board b;
    auto set = [&](Feature f, FeatureState::Level l, const char* text, bool landing) {
        auto& s = b.features[size_t(f)];
        s.level = l;
        s.text = text;
        s.landing = landing;
    };
    set(Feature::God, FeatureState::Active, "Writing 2 words every frame (record +25A8, accessor + 4)", true);
    set(Feature::Super, FeatureState::Active, "Writing 2 words every frame", true);
    set(Feature::Pickup, FeatureState::Refused, "No matching record: player component: no record signature at +2D3C", false);
    set(Feature::Recoil, FeatureState::Active, "Writing 16 words on 3 weapons", true);
    set(Feature::Ammo, FeatureState::Partial, "Writing 1 word on 2 of 3 weapons", true);
    set(Feature::FullAuto, FeatureState::Waiting, "Waiting for an equipped weapon", false);
    b.running = true;
    b.frames = 18342;
    b.playerHandle = 0x5A5A0007;
    b.weapons = 3;
    b.problems = {"weapon 0005A1C2: gate +0x64 reads 00000000 (expected 1: not a live fire component)"};
    player::publishBoard(std::move(b));
     
    for (const char* id : {"player.god", "player.super", "player.pickup_range", "weapons.no_recoil", "weapons.infinite_ammo", "weapons.full_auto"})
        ui::demoShowSwitch(id, true);
}

}   
