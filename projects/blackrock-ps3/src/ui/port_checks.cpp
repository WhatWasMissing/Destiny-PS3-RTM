 
 
 
 
 
 
 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "app/engine.h"
#include "app/registry.h"
#include "core/error.h"
#include "core/profile.h"
#include "ui/shell_internal.h"
#include "ui/widgets.h"

namespace brps3::ui {

namespace {

enum class PState { NotRun, Queued, Running, Ok, Failed, Skipped };

struct Result {
    PState state = PState::NotRun;
    std::string output, error;
    double ms = 0;
    std::string when;                    
};

struct State {
    std::mutex m;
    std::map<std::string, Result> results;    
    bool running = false;
    size_t done = 0, total = 0;
    std::string current;
     
    std::string filter;
    std::set<std::string> expanded;
    bool showReport = false;
};

State& st() {
    static State s;
    return s;
}

constexpr const char* kJob = "Port checks";

std::string nowText(const char* fmt) {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    char buf[64];
    std::strftime(buf, sizeof buf, fmt, &tm);
    return buf;
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

 
std::vector<const Item*> probeItems() {
    std::vector<const Item*> out;
    std::vector<std::string> groups;
    for (const Item& it : items())
        if (it.kind == ItemKind::Action && it.page == Page::Testing && it.tab == kPortChecksTab) {
            out.push_back(&it);
            if (std::find(groups.begin(), groups.end(), it.section) == groups.end()) groups.push_back(it.section);
        }
    std::stable_sort(out.begin(), out.end(), [&](const Item* a, const Item* b) {
        return std::find(groups.begin(), groups.end(), a->section) < std::find(groups.begin(), groups.end(), b->section);
    });
    return out;
}

 
class ProbeCtx final : public CtxProxy {
public:
    ProbeCtx(Ctx& inner, std::string prefix, std::string& out) : CtxProxy(inner), prefix_(std::move(prefix)), out_(out) {}
    void log(const std::string& text) override {
        out_ += text;
        if (text.empty() || text.back() != '\n') out_ += '\n';
        inner().log(prefix_ + text);
    }
    void toast(const std::string& text) override { log(text); }
private:
    std::string prefix_;
    std::string& out_;
};

void runProbes(const std::vector<std::string>& ids) {
    if (ids.empty()) return;
    State& s = st();
    {
        std::lock_guard<std::mutex> lock(s.m);
        if (s.running) {
            notify("Port checks are already running", Level::Info);
            return;
        }
        for (const auto& id : ids) s.results[id].state = PState::Queued;
        s.running = true;
        s.done = 0;
        s.total = ids.size();
        s.current.clear();
    }
    noteQueued(kJob);
    Engine::get().post(
        kJob,
        [ids](Ctx& c) {
            State& S = st();
            for (size_t i = 0; i < ids.size(); ++i) {
                const ActionDef* a = Registry::get().findAction(ids[i]);
                if (c.cancelled()) {
                    std::lock_guard<std::mutex> lock(S.m);
                    for (size_t k = i; k < ids.size(); ++k) {
                        S.results[ids[k]].state = PState::Skipped;
                        S.results[ids[k]].error = "stopped before it ran";
                    }
                    break;
                }
                {
                    std::lock_guard<std::mutex> lock(S.m);
                    S.results[ids[i]].state = PState::Running;
                    S.current = a ? a->label : ids[i];
                }
                Result r;
                const auto t0 = std::chrono::steady_clock::now();
                try {
                    if (!a || !a->run) throw Error("probe " + ids[i] + " has no run function");
                    ProbeCtx pc(c, "[" + a->label + "] ", r.output);
                    a->run(pc);
                    r.state = PState::Ok;
                } catch (const std::exception& e) {
                    r.state = PState::Failed;
                    r.error = e.what();
                }
                r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                r.when = nowText("%H:%M:%S");
                std::lock_guard<std::mutex> lock(S.m);
                S.results[ids[i]] = std::move(r);
                ++S.done;
            }
        },
        [ids](bool ok, const std::string& error) {
            State& S = st();
            size_t passed = 0, failed = 0;
            {
                std::lock_guard<std::mutex> lock(S.m);
                S.running = false;
                S.current.clear();
                for (const auto& id : ids) {
                    Result& r = S.results[id];
                    if (r.state == PState::Queued || r.state == PState::Running) {    
                        r.state = PState::Skipped;
                        r.error = ok ? "not run" : error;
                    }
                    passed += r.state == PState::Ok;
                    failed += r.state == PState::Failed;
                }
            }
            if (ids.size() > 1 || !ok) {
                char buf[128];
                std::snprintf(buf, sizeof buf, "Port checks: %zu ran, %zu failed%s", passed + failed, failed, ok ? "" : " (stopped)");
                notify(buf, failed || !ok ? Level::Warn : Level::Good);
            }
        });
}

const char* stateText(PState s) {
    switch (s) {
    case PState::NotRun: return "not run";
    case PState::Queued: return "queued";
    case PState::Running: return "running";
    case PState::Ok: return "OK";
    case PState::Failed: return "FAILED";
    case PState::Skipped: return "skipped";
    }
    return "?";
}

std::string indent(const std::string& text, const char* pad) {
    std::string out;
    size_t start = 0;
    while (start < text.size()) {
        size_t nl = text.find('\n', start);
        if (nl == std::string::npos) nl = text.size();
        out += pad + text.substr(start, nl - start) + "\n";
        start = nl + 1;
    }
    return out;
}

std::string buildReport(const std::vector<const Item*>& probes, const std::map<std::string, Result>& results) {
    const EngineStatus es = Engine::get().status();
    size_t ok = 0, failed = 0, notRun = 0;
    for (const Item* it : probes) {
        auto r = results.find(it->id);
        const PState s = r == results.end() ? PState::NotRun : r->second.state;
        ok += s == PState::Ok;
        failed += s == PState::Failed;
        notRun += s != PState::Ok && s != PState::Failed;
    }
    std::string out = "Blackrock PS3 - port checks report\n";
    out += "Date:      " + nowText("%Y-%m-%d %H:%M:%S") + "\n";
    out += "Game:      " + std::string(profile::TITLE_ID) + " APP_VER " + profile::APP_VER + " build " + profile::BUILD + "\n";
    if (es.state == EngineStatus::Connected) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "Console:   %s, %s; executor %s (%.0f fps, heartbeat %u)\n", es.host.c_str(), es.process.c_str(),
                      es.ticking ? "ticking" : "NOT ticking", double(es.fps), es.heartbeat);
        out += buf;
    } else {
        out += "Console:   not connected\n";
    }
    out += "Summary:   " + std::to_string(probes.size()) + " checks: " + std::to_string(ok) + " ok, " + std::to_string(failed) + " failed, " +
           std::to_string(notRun) + " not run\n";
    std::string group;
    for (const Item* it : probes) {
        if (it->section != group) {
            group = it->section;
            out += "\n== " + (group.empty() ? std::string("General") : group) + " ==\n";
        }
        auto r = results.find(it->id);
        const Result res = r == results.end() ? Result{} : r->second;
        char head[64];
        if (res.state == PState::Ok || res.state == PState::Failed) std::snprintf(head, sizeof head, " [%s, %.0f ms, %s]", stateText(res.state), res.ms, res.when.c_str());
        else std::snprintf(head, sizeof head, " [%s]", stateText(res.state));
        out += "\n-- " + it->label + head + "\n";
        if (!it->help.empty()) out += indent(it->help, "   what: ");
        if (!res.output.empty()) out += indent(res.output, "   ");
        if (!res.error.empty()) out += indent(res.error, "   error: ");
    }
    return out;
}

void saveReport(const std::string& text) {
    try {
        const auto path = state_dir() / ("port-checks-" + nowText("%Y%m%d-%H%M%S") + ".txt");
        std::ofstream f(path, std::ios::binary);
        if (!f) throw Error("cannot write " + path.string());
        f << text;
        notify("Report saved: " + path.filename().string() + " (state folder)", Level::Good);
    } catch (const std::exception& e) {
        notify(std::string("Report not saved: ") + e.what(), Level::Error);
    }
}

 
void statusGlyph(PState s) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = px(22);
    ImGui::Dummy(ImVec2(px(20), h));
    ImDrawList* d = ImGui::GetWindowDrawList();
    const ImVec2 c(p.x + px(9), p.y + h * 0.5f);
    auto glyph = [&](const char* g, const ImVec4& col) {
        const ImVec2 gs = textSize(FontKind::Icons, 15, g);
        drawText(d, FontKind::Icons, 15, c - gs * 0.5f, u32(col), g);
    };
    switch (s) {
    case PState::Ok: glyph(icon::Success, pal::Success); break;
    case PState::Failed: glyph(icon::Error, pal::Danger); break;
    case PState::Skipped: glyph(icon::Warning, pal::Warning); break;
    case PState::Running: {
        const float r = px(6);
        const float a0 = float(ImGui::GetTime() * 6.0);
        d->PathArcTo(c, r, a0, a0 + 4.2f, 20);
        d->PathStroke(u32(pal::Accent), 0, px(2));
        requestRedraw();
        break;
    }
    case PState::Queued: d->AddCircle(c, px(5), u32(pal::Info), 16, px(1.5f)); break;
    case PState::NotRun: d->AddCircleFilled(c, px(4), u32(pal::Decor), 16); break;
    }
}

std::string firstLine(const std::string& s) {
    const size_t nl = s.find('\n');
    return nl == std::string::npos ? s : s.substr(0, nl);
}

size_t lineCount(const std::string& s) {
    size_t n = 0;
    for (char c : s) n += c == '\n';
    return n + (!s.empty() && s.back() != '\n');
}

void drawPortChecks(Ui& ui) {
    State& s = st();
    const auto probes = probeItems();
    if (probes.empty()) {
        emptyState(icon::Probe, "No Port Checks Registered", nullptr, 190);
        return;
    }
    std::map<std::string, Result> results;
    bool running;
    size_t done, total;
    std::string current;
    {
        std::lock_guard<std::mutex> lock(s.m);
        results = s.results;
        running = s.running;
        done = s.done;
        total = s.total;
        current = s.current;
    }
    size_t ok = 0, failed = 0;
    for (const Item* it : probes) {
        auto r = results.find(it->id);
        if (r == results.end()) continue;
        ok += r->second.state == PState::Ok;
        failed += r->second.state == PState::Failed;
    }
    const bool conn = ui.connected();
    std::vector<std::string> allIds;
    for (const Item* it : probes) allIds.push_back(it->id);

     
    const float avail = availWidth();
    const float rowRight = ImGui::GetCursorScreenPos().x + avail;
    searchField("##portfilter", &s.filter, "Filter Checks", std::min(px(300), avail * 0.4f) / scale());
    ImGui::SameLine(0, px(12));
    {
        char buf[96];
        std::snprintf(buf, sizeof buf, "%zu checks  \xC2\xB7  %zu ok  \xC2\xB7  %zu failed", probes.size(), ok, failed);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(8));
        caption(buf, failed ? pal::Warning : pal::Faint);
    }
    const float wRun = buttonWidth(running ? "Stop" : "Run All", running ? icon::Stop : icon::Play);
    const float wCopy = buttonWidth("Copy Report", icon::Copy);
    const float wSave = buttonWidth("Save", icon::Folder);
    const float buttonsW = wRun + wCopy + wSave + px(16);
     
    ImGui::SameLine(0, px(12));
    if (ImGui::GetCursorScreenPos().x + buttonsW > rowRight) {
        ImGui::NewLine();
        gap(sp::XS);
    }
    alignRight(buttonsW);
    if (running) {
        if (button("Stop##portstop", {.tone = Tone::Danger, .icon = icon::Stop, .tooltip = "Stop after the probe that is running"}))
            Engine::get().cancel();
    } else {
        auto offline = requireConnection(ui);
        if (button("Run All##portall", {.tone = Tone::Primary, .icon = icon::Play,
                                         .tooltip = "Run every probe in one job (read-only). Load into an activity first."}))
            runProbes(allIds);
    }
    ImGui::SameLine(0, px(8));
    if (button("Copy Report", {.icon = icon::Copy, .tooltip = "Copy the whole report (every probe's output) to the clipboard"}))
        copyToClipboard(buildReport(probes, results), "report");
    ImGui::SameLine(0, px(8));
    if (button("Save", {.tone = Tone::Ghost, .icon = icon::Folder, .tooltip = "Save the report as a text file in the state folder"}))
        saveReport(buildReport(probes, results));

    if (running) {
        gap(sp::M);
        progressBar(total ? float(done) / float(total) : -1.0f, 0, 5);
        gap(sp::XS);
        char buf[256];
        std::snprintf(buf, sizeof buf, "Running %zu of %zu: %s", std::min(done + 1, total), total, current.c_str());
        caption(buf, pal::Muted);
    }
    gap(sp::M);

     
    const std::string q = lower(s.filter);
    const ShellState& sh = shell();
    std::string group;
    bool any = false;
    for (const Item* it : probes) {
        if (!q.empty() && lower(it->label + " " + it->section + " " + it->help).find(q) == std::string::npos) continue;
        any = true;
        if (it->section != group) {
            group = it->section;
            if (ImGui::GetCursorPosY() > px(60)) gap(sp::S);
            size_t n = 0;
            for (const Item* o : probes) n += o->section == group;
            const std::string head = (group.empty() ? std::string("GENERAL") : group) + "   " + std::to_string(n);
            fieldLabel(head.c_str());
        }
        auto rit = results.find(it->id);
        const Result r = rit == results.end() ? Result{} : rit->second;
        ImGui::PushID(it->id.c_str());
        const bool flash = sh.highlightId == it->id && sh.now < sh.highlightUntil;
        const ImVec2 rowMin = ImGui::GetCursorScreenPos();
        const float rowW = availWidth();
        ImDrawList* d = ImGui::GetWindowDrawList();
        d->ChannelsSplit(2);
        d->ChannelsSetCurrent(1);
        ImGui::BeginGroup();
        ImGui::Dummy(ImVec2(0, px(4)));
        ImGui::Indent(px(8));
        statusGlyph(r.state);
        ImGui::SameLine(0, px(8));
        const float runW = buttonWidth("Run", icon::Play, true);
        const bool hasMore = lineCount(r.output) > 1;
        const float rightW = runW + (hasMore || !r.output.empty() ? px(36) : 0.0f) + px(16);
        ImGui::BeginGroup();
        const float textW = std::max(px(120), availWidth() - rightW);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textW);
        {
            FontScope f(FontKind::Semibold, type::Label);
            ImGui::TextUnformatted(it->label.c_str());
        }
        if (!it->badge.empty()) {
            ImGui::SameLine(0, px(8));
            badgePill(it->badge.c_str());
        }
        if (!it->help.empty()) wrapped(it->help.c_str(), pal::Muted, type::Small);
        if (!r.error.empty()) wrapped(r.error.c_str(), r.state == PState::Failed ? pal::Danger : pal::Warning, type::Small);
        if (!r.output.empty()) {
            const bool open = s.expanded.count(it->id) != 0;
            FontScope f(FontKind::Mono, type::MonoSmall);
            ImGui::PushStyleColor(ImGuiCol_Text, open ? pal::Text : pal::Faint);
            if (open) ImGui::TextUnformatted(r.output.c_str());
            else ImGui::TextUnformatted(ellipsize(firstLine(r.output).c_str(), textW, FontKind::Mono, type::MonoSmall).c_str());
            ImGui::PopStyleColor();
        }
        if (r.state == PState::Ok || r.state == PState::Failed) {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%s  \xC2\xB7  %.0f ms", r.when.c_str(), r.ms);
            caption(buf, pal::Faint);
        }
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
         
        const float top = rowMin.y + px(6);
        const float right = rowMin.x + rowW - px(8);
        ImVec2 save = ImGui::GetCursorScreenPos();
        float x = right - runW;
        ImGui::SetCursorScreenPos(ImVec2(x, top));
        {
            DisabledScope off(!conn || running, !conn ? kConnectFirst : "Port checks are running");
            if (button("Run##one", {.icon = icon::Play, .small = true, .tooltip = "Run this probe (read-only)"})) runProbes({it->id});
        }
        if (!r.output.empty()) {
            const bool open = s.expanded.count(it->id) != 0;
            x -= px(36);
            ImGui::SetCursorScreenPos(ImVec2(x, top));
            if (iconButton("##copyone", icon::Copy, "Copy this probe's output", false, 28)) copyToClipboard(r.output, "output");
            if (hasMore) {
                x -= px(32);
                ImGui::SetCursorScreenPos(ImVec2(x, top));
                if (iconButton("##expand", open ? icon::ChevronUp : icon::ChevronDown, open ? "Show less" : "Show the whole output", false, 28)) {
                    if (open) s.expanded.erase(it->id);
                    else s.expanded.insert(it->id);
                }
            }
        }
        ImGui::SetCursorScreenPos(save);
        ImGui::Unindent(px(8));
        ImGui::Dummy(ImVec2(0, px(4)));
        ImGui::EndGroup();
        const ImVec2 rowMax(rowMin.x + rowW, ImGui::GetItemRectMax().y);
        d->ChannelsSetCurrent(0);
        const bool hovered = ImGui::IsMouseHoveringRect(rowMin, rowMax) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
        const float glow = animate(ImGui::GetID("##glow"), flash ? 1.0f : 0.0f, 6.0f);
        ImVec4 bg = hovered ? pal::Raised : alpha(pal::Raised, 0.45f);
        d->AddRectFilled(rowMin, rowMax, u32(bg), px(kRowRounding));
        if (r.state == PState::Failed) d->AddRect(rowMin, rowMax, u32(alpha(pal::Danger, 0.45f)), px(kRowRounding), 0, 1.0f);
        if (glow > 0.01f) d->AddRect(rowMin, rowMax, u32(alpha(pal::Accent, glow)), px(kRowRounding), 0, 1.5f);
        d->ChannelsMerge();
        if (flash && shell().scrollToHighlight) {
            ImGui::SetScrollHereY(0.3f);
            shell().scrollToHighlight = false;
        }
        ImGui::PopID();
        gap(sp::XS);
    }
    if (!any) emptyState(icon::Filter, "No Check Matches the Filter", nullptr, 120);
}

void resetOnDisconnect() {
     
    State& s = st();
    std::lock_guard<std::mutex> lock(s.m);
    if (!s.running) return;
    for (auto& [id, r] : s.results)
        if (r.state == PState::Queued || r.state == PState::Running) {
            r.state = PState::Skipped;
            r.error = "disconnected";
        }
}

}   

 
void seedPortChecksDemo() {
    State& s = st();
    std::lock_guard<std::mutex> lock(s.m);
    s.results["world.check.readers"] = {PState::Ok,
                                        "sObjects           01A1D9E0: base 60100000 stride 0xC0 cap 1500 count 212 -> ok\n"
                                        "player array       01A1D720: base 60400000 stride 0x1B0 cap 20 count 1 -> ok\n"
                                        "tag table 3030F390: 2048 pages, 234 registered, 54 package pages\n"
                                        "verdict: shared readers OK\n",
                                        "", 418, "21:04:10"};
    s.results["world.check.player_position"] = {PState::Ok,
                                                "element +0x2810 (teleport target, likely): (12.402, -340.118, 25.003)\n"
                                                "sObject +0xB0 (engine position, verified): (12.402, -340.118, 25.003)\n"
                                                "distance +0x2810 to +0xB0: 0.000 (expected < 0.5 while standing still)\n"
                                                "verdict: +0x2810 matches the engine position: confirmed\n",
                                                "", 266, "21:04:11"};
    s.results["world.check.guard"] = {PState::Failed, "", "the game thread is not running the executor (not in a game session?)", 1503,
                                      "21:04:13"};
}

 
void runPortCheck(const std::string& id) { runProbes({id}); }

BR_FEATURES(ui_port_checks) {
    r.section({.id = "testing.port_checks",
               .page = Page::Testing,
               .tab = kPortChecksTab,
               .section = "Port checks",
               .order = 10,
               .draw = drawPortChecks,
               .onDisconnect = resetOnDisconnect});
    ui::styleSection("Port checks", {.fullWidth = true,
                                     .subtitle = "Read-only probes for ported features that still need an in-game check. Load into an "
                                                 "activity, Run all, then copy the report.",
                                     .icon = ui::icon::Probe});
}

}   
