 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

#include "ui/shell_internal.h"

namespace brps3::ui {
namespace {

void connectionCard() {
    ShellState& S = shell();
    const EngineStatus& s = S.status;
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float avail = availWidth();
    const bool isConn = s.state == EngineStatus::Connected;
    const bool busy = s.state == EngineStatus::Connecting || s.state == EngineStatus::Disconnecting;
    const bool failed = !s.error.empty() && !isConn && !busy;

     
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float rowH = px(56);
    const float circle = px(36);
    const ImVec4 col = isConn ? (s.ticking ? pal::Success : pal::Warning) : failed ? pal::Red : pal::Muted;
    const ImVec2 cc(p.x + circle * 0.5f, p.y + rowH * 0.5f);
    d->AddCircleFilled(cc, circle * 0.5f, u32(alpha(col, 0.12f)), 40);
    if (busy) {
        ImGui::SetCursorScreenPos(cc - ImVec2(px(8), px(8)));
        spinner(7, pal::Text, 1.8f);
    } else {
        const char* glyph = isConn ? (s.ticking ? icon::Check : icon::Warning) : failed ? icon::Error : icon::Plug;
        const ImVec2 is = textSize(FontKind::Icons, 15, glyph);
        drawText(d, FontKind::Icons, 15, cc - is * 0.5f, u32(col), glyph);
    }
    std::string headline, detailText;
    if (isConn) {
        headline = (s.error.empty() ? "Connected to " : "Connection degraded - ") + s.host;
        detailText = !s.error.empty() ? s.error :
                     (s.process.empty() ? std::string("Destiny") : s.process) + (s.build.empty() ? "" : "  \xC2\xB7  build " + s.build) +
                     (s.ticking ? "  \xC2\xB7  executor ticking" : "  \xC2\xB7  executor idle: load into orbit or a patrol");
    } else if (s.state == EngineStatus::Connecting) {
        headline = "Connecting to " + S.host;
    } else if (s.state == EngineStatus::Disconnecting) {
        headline = "Disconnecting";
    } else if (failed) {
        headline = "Connection Failed";
        detailText = s.error;
    } else {
        headline = "Not Connected";
    }
    const float btnW = std::max(buttonWidth("Disconnect"), px(110));
    const float textX = p.x + circle + px(14);
    const float textW = std::max(px(120), avail - circle - px(14) - btnW - px(16));
    const std::string hl = ellipsize(headline.c_str(), textW, FontKind::Semibold, type::Label);
    const std::string dt = ellipsize(detailText.c_str(), textW, FontKind::Body, type::Caption);
    const ImVec2 hs = textSize(FontKind::Semibold, type::Label, hl.c_str());
    const ImVec2 ds = dt.empty() ? ImVec2(0, 0) : textSize(FontKind::Body, type::Caption, dt.c_str());
    const float blockH = hs.y + (dt.empty() ? 0.0f : px(2) + ds.y);
    const float ty = p.y + (rowH - blockH) * 0.5f;
    drawText(d, FontKind::Semibold, type::Label, ImVec2(textX, ty), u32(pal::Text), hl.c_str());
    if (!dt.empty()) drawText(d, FontKind::Body, type::Caption, ImVec2(textX, ty + hs.y + px(2)), u32(failed ? pal::Red : pal::Muted), dt.c_str());
    if (dt != detailText) {
        ImGui::SetCursorScreenPos(ImVec2(textX, ty));
        ImGui::Dummy(ImVec2(textW, blockH));
        tooltip(detailText.c_str());
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x + avail - btnW, p.y + (rowH - px(kControlH)) * 0.5f));
    {
        ButtonOptions o;
        o.tone = isConn || busy ? Tone::Secondary : Tone::Primary;
        o.width = btnW / scale();
        o.busy = busy;
        const char* label = isConn ? "Disconnect###homeconn" : failed ? "Try Again###homeconn" : "Connect###homeconn";
        o.tooltip = isConn ? "Stops loops and restores every byte Blackrock wrote" : "webMAN MOD PS3MAPI, port 7887";
        if (button(label, o)) {
            if (isConn) Engine::get().disconnect();
            else connectNow();
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + rowH));
    ImGui::Dummy(ImVec2(avail, 0));

    if (isConn) {
        gap(sp::XS);
        const int cols = avail > px(700) ? 4 : 2;
        const float tileW = std::floor((avail - px(10) * float(cols - 1)) / float(cols));
        char buf[128];
        snprintf(buf, sizeof(buf), "%.0f fps", double(s.fps));
        const std::string execValue = s.ticking ? buf : "Idle";
        snprintf(buf, sizeof(buf), "%s  \xC2\xB7  heartbeat %u", s.ticking ? "Ticking" : "Not ticking", unsigned(s.heartbeat));
        const std::string execSub = buf;
        snprintf(buf, sizeof(buf), "%.0f ms", s.latencyMs);
        const std::string latValue = buf;
        const std::string ownValue = std::to_string(s.patches);
        snprintf(buf, sizeof(buf), "%zu %s  \xC2\xB7  %zu %s", s.hooks, s.hooks == 1 ? "hook" : "hooks", s.tasks, s.tasks == 1 ? "task" : "tasks");
        const std::string ownSub = buf;
        const std::string jobValue = s.job.empty() ? std::string("Idle") : s.job;
        snprintf(buf, sizeof(buf), "%zu queued  \xC2\xB7  %zu %s", s.queued, s.loops, s.loops == 1 ? "loop" : "loops");
        const std::string jobSub = buf;
        struct T {
            const char* id;
            const char* cap;
            std::string v, sub;
            ImVec4 c;
        } tiles[] = {
            {"exec", "Executor", execValue, execSub, s.ticking ? pal::Success : pal::Warning},
            {"lat", "Latency", latValue, "PS3MAPI round trip", pal::Text},
            {"own", "Patches", ownValue, ownSub, pal::Text},
            {"jobs", "Jobs", jobValue, jobSub, s.job.empty() ? pal::Muted : pal::Text},
        };
        for (int i = 0; i < 4; ++i) {
            if (i % cols) ImGui::SameLine(0, px(10));
            statTile(tiles[i].id, tiles[i].cap, tiles[i].v, tiles[i].sub, tileW, tiles[i].c);
        }
        gap(sp::XS);
    } else if (busy) {
        progressBar(-1, 0, 3);
        gap(sp::S);
    } else {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
        beginRow("homehost", "PS3 Address", 0, {.help = "webMAN MOD PS3MAPI, port 7887. Enter connects."});
        if (inputMono("##homehost", &S.host, "192.168.0.254", kControlColumn, ImGuiInputTextFlags_CharsNoBlank | ImGuiInputTextFlags_EnterReturnsTrue))
            connectNow();
        if (ImGui::IsItemDeactivatedAfterEdit() && !S.screenshot) setString(key::Host, S.host);
        endRow();
        ImGui::PopStyleVar();
    }

     
    if (s.journalPending && !isConn) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
        RowOptions o;
        o.help = "Recover restores only the bytes Blackrock still owns from an earlier session, then removes the journal.";
        {
            DisabledScope off(s.state != EngineStatus::Disconnected, "Disconnect first");
            if (actionRow("recover", "Recovery Journal Waiting", "Recover", o) && !S.screenshot) Engine::get().recover(S.host);
        }
        ImGui::PopStyleVar();
    }

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
    bool autoConnect = settings().flag(key::AutoConnect, true);
    if (toggleRow("autoconnect", "Connect Automatically on Launch", &autoConnect,
                  {.help = "Connects to the last address when Blackrock starts and re-applies your sticky switches."}) &&
        !S.screenshot)
        settings().set(std::string(key::AutoConnect), autoConnect);
    ImGui::PopStyleVar();
}

void tileGrid(const std::vector<const Item*>& list, float minTile) {
    const float avail = availWidth();
    const float gapW = px(8);
    const int cols = std::max(1, int((avail + gapW) / (px(minTile) + gapW)));
    const float w = std::floor((avail - gapW * float(cols - 1)) / float(cols));
    const bool isConn = connected();
    for (size_t i = 0; i < list.size(); ++i) {
        if (i % size_t(cols)) ImGui::SameLine(0, gapW);
        drawItemTile(*list[i], w, isConn);
    }
}

std::vector<const Item*> pinnedItems() {
    std::vector<const Item*> list;
    for (const std::string& id : settings().list(key::Pins))
        if (const Item* it = findItem(id)) list.push_back(it);
    return list;
}

std::vector<const Item*> recentItems() {
    std::vector<const Item*> list;
    for (const std::string& id : settings().list(key::Recent)) {
        if (const Item* it = findItem(id)) list.push_back(it);
        if (list.size() >= 6) break;
    }
    return list;
}

void pinsCard() {
    const auto list = pinnedItems();
    if (list.empty()) {
        emptyState(icon::Pin, "Nothing Pinned Yet", "Right-click a row to pin it here.", 120);
        return;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(px(8), px(8)));
    tileGrid(list, 230);
    ImGui::PopStyleVar();
}

void recentCard() {
    const auto list = recentItems();
    if (list.empty()) {
        emptyState(icon::History, "Nothing Recent", nullptr, 120);
        return;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(px(8), px(8)));
    tileGrid(list, 230);
    ImGui::PopStyleVar();
}

}   

void drawHomePage() {
    std::vector<CardSpec> cards;
    CardSpec conn{"home.connection", "Connection", "", nullptr, true, connectionCard};
    conn.pad = 6;
    cards.push_back(std::move(conn));
    cards.push_back({"home.pins", "Pinned", "", nullptr, false, pinsCard});
    cards.push_back({"home.recent", "Recent", "", nullptr, false, recentCard});
    masonry(cards, px(sp::L));
     
    bool any = false;
    for (const Item& it : items())
        if (it.page == Page::Home) any = true;
    if (any) {
        gap(sp::L);
        drawRegistryPage(int(Page::Home));
    }
}

}   
