 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <windows.h>
#include <shellapi.h>

#include "app/engine.h"
#include "app/registry.h"
#include "app/settings.h"
#include "core/error.h"
#include "core/profile.h"
#include "core/runtime.h"
#include "core/session.h"
#include "ui/widgets.h"

namespace brps3 {
namespace {

using namespace ui;

constexpr const char* kLiveLoop = "testing.memory.live";

struct CallRecord {
    uint32_t code;
    std::string args, floats;
    CallResult result;
    bool ok;
    std::string error;
};

struct State {
    std::mutex m;
     
    std::vector<std::string> patches;
    bool rtKnown = false;
    std::string rtError;
    bool installed = false;
    size_t seenPatches = size_t(-1), seenHooks = size_t(-1), seenTasks = size_t(-1);
     
    uint32_t code = 0;
    std::string args, floats;
    std::string callError;               
    std::deque<CallRecord> history;
     
    uint32_t addr = profile::CONTROL;
    int size = 0x80;
    uint32_t dataAddr = 0;
    std::vector<uint8_t> data, previous;
    std::string memError;
    bool live = false;
    double readAt = 0;                   
    uint32_t pokeAddr = profile::CONTROL;
    std::string pokeBytes;
    std::string pokeError;
     
    bool journalLoaded = false;
    bool journalPresent = false;
    Journal journal;
    std::string journalError;
};

State& st() {
    static State s;
    return s;
}

std::string hex8(uint32_t v) {
    char b[12];
    snprintf(b, sizeof(b), "%08X", v);
    return b;
}

uint64_t parseHex64(const std::string& t) {
    std::string s = t;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s = s.substr(2);
    if (s.empty()) throw Error("empty number");
    for (char c : s)
        if (!std::isxdigit(static_cast<unsigned char>(c))) throw Error("not a hex number: " + t);
    return std::stoull(s, nullptr, 16);
}

std::vector<std::string> split(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : text) {
        if (c == ' ' || c == ',' || c == '\t' || c == ';') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

void refreshRuntime(Ui& ui) {
    if (ui.busy("Read runtime state")) return;
    ui.post(
        "Read runtime state",
        [](Ctx& c) {
            auto names = c.rt().appliedNames();
            const bool inst = c.rt().installed();
            State& S = st();
            std::lock_guard<std::mutex> lock(S.m);
            S.patches = std::move(names);
            S.installed = inst;
            S.rtKnown = true;
            S.rtError.clear();
        },
        [](bool ok, const std::string& err) {
            if (ok) return;
            State& S = st();
            std::lock_guard<std::mutex> lock(S.m);
            S.rtKnown = true;
            S.rtError = err;
        });
}

 
 
 

void drawRuntime(Ui& ui) {
    State& s = st();
    EngineStatus status = Engine::get().status();
    const bool conn = ui.connected();
    if (ui::isDemo()) {
         
        status.state = EngineStatus::Connected;
        status.ticking = true;
        status.fps = 30;
        status.heartbeat = 18342;
        status.patches = 3;
        status.hooks = 1;
        status.tasks = 1;
        status.latencyMs = 41;
        std::lock_guard<std::mutex> lock(s.m);
        if (!s.rtKnown) {
            s.patches = {"patches.third_person", "patches.fov", "loot.pickup_filter"};
            s.installed = true;
            s.rtKnown = true;
        }
        s.seenPatches = status.patches;
        s.seenHooks = status.hooks;
        s.seenTasks = status.tasks;
    }
     
    if (conn && (status.patches != s.seenPatches || status.hooks != s.seenHooks || status.tasks != s.seenTasks)) {
        s.seenPatches = status.patches;
        s.seenHooks = status.hooks;
        s.seenTasks = status.tasks;
        refreshRuntime(ui);
    }
    if (!conn) {
         
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = availWidth();
        const float h = px(58);
        ImGui::Dummy(ImVec2(w, h));
        ImDrawList* d = ImGui::GetWindowDrawList();
        d->AddRectFilled(p, p + ImVec2(w, h), u32(pal::Raised), px(kRowRounding));
        d->AddRect(p, p + ImVec2(w, h), u32(pal::Border), px(kRowRounding), 0, 1.0f);
        const ImVec2 is = textSize(FontKind::Icons, 16, icon::Plug);
        drawText(d, FontKind::Icons, 16, ImVec2(p.x + px(16), p.y + (h - is.y) * 0.5f), u32(pal::Muted), icon::Plug);
        drawText(d, FontKind::Semibold, type::Label, ImVec2(p.x + px(44), p.y + px(10)), u32(pal::Text), "Connect to See Runtime Metrics");
        drawText(d, FontKind::Body, type::Small, ImVec2(p.x + px(44), p.y + px(30)), u32(pal::Muted),
                 "Executor FPS, Heartbeat, Hooks, Tasks, Latency and the Patches Blackrock Owns.");
    } else {
        const float avail = availWidth();
        const int cols = avail > px(470) ? 4 : 2;
        const float w = std::floor((avail - px(8) * float(cols - 1)) / float(cols));
        char buf[64];
        snprintf(buf, sizeof(buf), "%.0f fps", double(status.fps));
        statTile("exec", "EXECUTOR", status.ticking ? std::string(buf) : std::string("Idle"), {}, w, status.ticking ? pal::Success : pal::Warning);
        ImGui::SameLine(0, px(8));
        statTile("beat", "HEARTBEAT", std::to_string(status.heartbeat), {}, w);
        if (cols == 2) gap(0);
        else ImGui::SameLine(0, px(8));
        snprintf(buf, sizeof(buf), "%zu / %zu", status.hooks, status.tasks);
        statTile("hooks", "HOOKS / TASKS", buf, {}, w);
        ImGui::SameLine(0, px(8));
        snprintf(buf, sizeof(buf), "%.0f ms", status.latencyMs);
        statTile("lat", "LATENCY", buf, {}, w);
    }
    gap(sp::M);

    std::vector<std::string> patches;
    bool known;
    std::string rtError;
    {
        std::lock_guard<std::mutex> lock(s.m);
        patches = s.patches;
        known = s.rtKnown;
        rtError = s.rtError;
    }
    {
        char hdr[64];
        snprintf(hdr, sizeof(hdr), "APPLIED PATCHES  (%zu)", conn && known ? patches.size() : size_t(0));
        fieldLabel(hdr);
        ImGui::SameLine();
        alignRight(px(30));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(8));
        auto offline = requireConnection(ui);
        if (iconButton("##refresh", icon::Refresh, "Read the runtime state again", false, 28)) refreshRuntime(ui);
    }
    gap(sp::XS);
    if (!conn) {
        caption("Named patches appear here once connected, each with a Revert button.", pal::Faint);
    } else if (!known) {
        caption("Reading...", pal::Faint);
    } else if (!rtError.empty()) {
        caption(("Couldn't read the runtime: " + rtError).c_str(), pal::Warning);
    } else if (patches.empty()) {
        caption("No named patches applied. Features add them as you switch things on.", pal::Faint);
    } else {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(px(8), px(4)));
        for (const std::string& name : patches) {
            ImGui::PushID(name.c_str());
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float h = px(36);
            ImGui::Dummy(ImVec2(availWidth(), h));
            ImDrawList* d = ImGui::GetWindowDrawList();
            d->AddRectFilled(p, p + ImVec2(availWidth(), h), u32(pal::Input), px(6));
            const ImVec2 ts = textSize(FontKind::Mono, type::Mono, name.c_str());
            drawText(d, FontKind::Mono, type::Mono, p + ImVec2(px(12), (h - ts.y) * 0.5f), u32(pal::Text), name.c_str());
            const float bw = buttonWidth("Revert", icon::Undo, true);
            ImGui::SetCursorScreenPos(p + ImVec2(availWidth() - bw - px(4), (h - px(kSmallControlH)) * 0.5f));
            const std::string job = "Revert " + name;
            if (button("Revert", {.tone = Tone::Danger, .icon = icon::Undo, .small = true,
                                  .tooltip = "Restore the original instructions of this patch", .busy = ui.busy(job)})) {
                const std::string n = name;
                noteQueued(job);
                ui.post(
                    job,
                    [n](Ctx& c) {
                        c.rt().revert(n);
                        c.log("Reverted " + n);
                    },
                    [n](bool ok, const std::string&) {
                        if (ok) notify("Reverted " + n, Level::Good);
                    });
            }
            ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + px(4)));
            ImGui::Dummy(ImVec2(0, 0));
            ImGui::PopID();
        }
        ImGui::PopStyleVar();
    }
}

 
 
 

void drawNativeCall(Ui& ui) {
    State& s = st();
     
    fieldLabel("CODE ADDRESS");
    gap(sp::XS + 2);
    if (inputHex("##code", &s.code, 130)) s.callError.clear();
    tooltip("A code address (not an OPD) below the end of the executable segment");
    gap(sp::S);
    fieldLabel("INTEGER ARGS  r3..r10  (hex)");
    gap(sp::XS + 2);
    if (inputMono("##args", &s.args, "e.g. 1A1D720 0 10", -1)) s.callError.clear();
    gap(sp::S);
    fieldLabel("FLOAT ARGS  f1..f4");
    gap(sp::XS + 2);
    if (inputMono("##floats", &s.floats, "e.g. 1.5, 0.25", -1)) s.callError.clear();
    gap(sp::M);
    const bool busy = ui.busy("Native call");
    bool clicked;
    {
        auto offline = requireConnection(ui);
        clicked = button("Call", {.tone = Tone::Primary, .icon = icon::Send, .busy = busy, .successKey = "testing.ncall"});
    }
    if (clicked) {
        try {
            if (s.code == 0 || s.code >= profile::TEXT_END)
                throw Error("The code address must be inside the executable segment (below " + hex8(profile::TEXT_END) + ").");
            std::vector<uint64_t> args;
            for (const std::string& a : split(s.args)) args.push_back(parseHex64(a));
            std::vector<double> floats;
            for (const std::string& f : split(s.floats)) {
                try {
                    floats.push_back(std::stod(f));
                } catch (...) {
                    throw Error("Not a number in the float args: " + f);
                }
            }
            if (args.size() > 8) throw Error("At most 8 integer arguments (r3..r10).");
            if (floats.size() > 4) throw Error("At most 4 float arguments (f1..f4).");
            s.callError.clear();
            CallRecord rec{s.code, s.args, s.floats, {}, false, {}};
            noteQueued("Native call");
            ui.post(
                "Native call",
                [rec, args, floats](Ctx& c) mutable {
                    rec.result = c.rt().call(rec.code, args, floats);
                    rec.ok = true;
                    char buf[160];
                    snprintf(buf, sizeof(buf), "ncall %08X -> r3 = %016llX (%08X), f1 = %g", rec.code,
                             static_cast<unsigned long long>(rec.result.r3), rec.result.u32(), rec.result.f1);
                    c.log(buf);
                    State& S = st();
                    std::lock_guard<std::mutex> lock(S.m);
                    S.history.push_front(rec);
                    while (S.history.size() > 8) S.history.pop_back();
                },
                [rec](bool ok, const std::string& err) {
                    if (ok) {
                        flashSuccess("testing.ncall");
                        return;
                    }
                    State& S = st();
                    CallRecord failed = rec;
                    failed.error = err;
                    std::lock_guard<std::mutex> lock(S.m);
                    S.history.push_front(failed);
                    while (S.history.size() > 8) S.history.pop_back();
                });
        } catch (const std::exception& e) {
            s.callError = e.what();    
        }
    }
    if (!s.callError.empty()) {
        gap(sp::XS);
        wrapped(s.callError.c_str(), mix(pal::Danger, pal::Text, 0.3f), type::Small);
    }
    std::deque<CallRecord> history;
    {
        std::lock_guard<std::mutex> lock(s.m);
        history = s.history;
    }
    if (history.empty()) return;
    gap(sp::L);
    fieldLabel("RESULTS");
    gap(sp::XS + 2);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(px(8), px(4)));
    for (size_t i = 0; i < history.size(); ++i) {
        const CallRecord& r = history[i];
        ImGui::PushID(int(i));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = availWidth();
        const float h = px(r.ok ? 56 : 44);
        ImGui::SetNextItemAllowOverlap();
        const bool rowClicked = ImGui::InvisibleButton("##hist", ImVec2(w, h));
        const bool hov = ImGui::IsMouseHoveringRect(p, p + ImVec2(w, h)) && ImGui::IsWindowHovered();
        ImDrawList* d = ImGui::GetWindowDrawList();
        d->AddRectFilled(p, p + ImVec2(w, h), u32(hov ? pal::RaisedHi : pal::Input), px(6));
        d->AddRectFilled(p + ImVec2(0, px(8)), p + ImVec2(px(3), h - px(8)), u32(r.ok ? pal::Success : pal::Danger), px(2));
        std::string head = hex8(r.code) + "(" + r.args + (r.floats.empty() ? "" : " | " + r.floats) + ")";
        drawText(d, FontKind::Mono, type::MonoSmall, p + ImVec2(px(14), px(8)), u32(pal::Muted), head.c_str());
        char buf[160];
        if (r.ok)
            snprintf(buf, sizeof(buf), "r3 %016llX   u32 %08X   i32 %d   f1 %g", static_cast<unsigned long long>(r.result.r3),
                     r.result.u32(), r.result.i32(), r.result.f1);
        else
            snprintf(buf, sizeof(buf), "%s", r.error.c_str());
        drawText(d, r.ok ? FontKind::Mono : FontKind::Body, r.ok ? type::Mono : type::Small, p + ImVec2(px(14), px(r.ok ? 29 : 24)),
                 u32(r.ok ? pal::Text : mix(pal::Danger, pal::Text, 0.3f)), buf);
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            tooltip("Click to load these arguments again");
        }
        if (rowClicked) {
            s.code = r.code;
            s.args = r.args;
            s.floats = r.floats;
            s.callError.clear();
        }
        if (r.ok && hov) {
             
            const ImVec2 after = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(p.x + w - px(36), p.y + (h - px(28)) * 0.5f));
            if (iconButton("##copyr3", icon::Copy, "Copy r3", false, 28)) {
                char r3[24];
                snprintf(r3, sizeof(r3), "%016llX", static_cast<unsigned long long>(r.result.r3));
                copyToClipboard(r3, "r3");
            }
            ImGui::SetCursorScreenPos(after);
        }
        ImGui::PopID();
    }
    ImGui::PopStyleVar();
}

 
 
 

double steadySeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

void storeRead(uint32_t addr, std::vector<uint8_t> bytes) {
    State& S = st();
    std::lock_guard<std::mutex> lock(S.m);
    if (S.dataAddr == addr && S.data.size() == bytes.size()) S.previous = S.data;
    else S.previous.clear();
    S.dataAddr = addr;
    S.data = std::move(bytes);
    S.memError.clear();
    S.readAt = steadySeconds();    
}

void peek(Ui& ui) {
    State& s = st();
    const uint32_t addr = s.addr;
    const uint32_t size = uint32_t(s.size);
    noteQueued("Peek memory");
    ui.post("Peek memory", [addr, size](Ctx& c) { storeRead(addr, c.rt().read(addr, size)); },
            [](bool ok, const std::string& err) {
                if (ok) return;
                State& S = st();
                std::lock_guard<std::mutex> lock(S.m);
                S.memError = err;
            });
}

void setLive(Ui& ui, bool on) {
    State& s = st();
    s.live = on;
    const uint32_t addr = s.addr;
    const uint32_t size = uint32_t(s.size);
    if (on)
        ui.post("Start live memory view", [addr, size](Ctx& c) {
            c.startLoop(kLiveLoop, 500, [addr, size](Ctx& lc) { storeRead(addr, lc.rt().read(addr, size)); });
        });
    else
        ui.post("Stop live memory view", [](Ctx& c) { c.stopLoop(kLiveLoop); });
}

void seedDemoMemory() {
     
    static bool done = false;
    if (done) return;
    done = true;
    char* v = nullptr;
    size_t n = 0;
    const bool demo = _dupenv_s(&v, &n, "BRPS3_UI_DEMO") == 0 && v && v[0] == '1';
    free(v);
    if (!demo) return;
    State& s = st();
    std::vector<uint8_t> d(0x80);
    const char magic[] = "BRX1";
    memcpy(d.data(), magic, 4);
    d[7] = 1;
    for (size_t i = 8; i < d.size(); ++i) d[i] = uint8_t((i * 37) & 0xFF) * uint8_t(i % 5 == 0 ? 0 : 1);
    std::vector<uint8_t> prev = d;
    d[0x0B] ^= 0x2C;
    d[0x0A] ^= 0x01;
    d[0x42] ^= 0x10;
    std::lock_guard<std::mutex> lock(s.m);
    s.dataAddr = profile::CONTROL;
    s.data = d;
    s.previous = prev;
    s.readAt = steadySeconds() - 0.4;
}

void drawMemory(Ui& ui) {
    State& s = st();
    seedDemoMemory();
    const bool conn = ui.connected();
     
    ImGui::BeginGroup();
    fieldLabel("ADDRESS");
    gap(sp::XS + 2);
    const uint32_t oldAddr = s.addr;
    inputHex("##addr", &s.addr, 130);
    const bool enter = conn && ImGui::IsItemDeactivated() && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter));
    tooltip("Enter peeks");
    ImGui::EndGroup();
    ImGui::SameLine(0, px(12));
    ImGui::BeginGroup();
    fieldLabel("SIZE");
    gap(sp::XS + 2);
    const int oldSize = s.size;
    inputInt("##size", &s.size, 16, 0x1000, 150, 16);
    ImGui::EndGroup();
    ImGui::SameLine(0, px(12));
    ImGui::BeginGroup();
    fieldLabel(" ");
    gap(sp::XS + 2);
    {
        auto offline = requireConnection(ui);
        if (button("Peek", {.tone = Tone::Primary, .icon = icon::View, .busy = ui.busy("Peek memory")}) || enter) peek(ui);
        ImGui::SameLine(0, px(10));
         
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const ImVec2 ls = textSize(FontKind::Body, type::Small, "Live");
        const float hitW = px(22) + px(8) + ls.x;
        const float y = at.y + (px(kControlH) - px(22)) * 0.5f;
        ImGui::SetCursorScreenPos(ImVec2(at.x, y));
        bool live = s.live;
        if (toggleSwitch("##live", &live)) setLive(ui, live);
        tooltip("Re-read the range twice a second and highlight bytes that change");
        ImGui::SetCursorScreenPos(ImVec2(at.x + px(22) + px(8), y));
        if (ImGui::InvisibleButton("##livelabel", ImVec2(ls.x, px(22)))) setLive(ui, !s.live);
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        drawText(ImGui::GetWindowDrawList(), FontKind::Body, type::Small, ImVec2(at.x + px(30), textTop(FontKind::Body, type::Small, y + px(11))),
                 u32(s.live ? pal::Text : pal::Muted), "Live");
        ImGui::SetCursorScreenPos(at);
        ImGui::Dummy(ImVec2(hitW, px(kControlH)));
    }
    ImGui::EndGroup();
    if (s.live && (s.addr != oldAddr || s.size != oldSize)) setLive(ui, true);    

     
    gap(sp::S);
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const ImVec2 ts = textSize(FontKind::Body, type::Small, "Jump to");
        drawText(ImGui::GetWindowDrawList(), FontKind::Body, type::Small, ImVec2(p.x, textTop(FontKind::Body, type::Small, p.y + px(kSmallControlH) * 0.5f)),
                 u32(pal::Faint), "Jump to");
        ImGui::Dummy(ImVec2(ts.x, px(kSmallControlH)));
    }
    struct Q {
        const char* name;
        uint32_t addr;
    } quick[] = {{"Control Block", profile::CONTROL}, {"Call Slots", profile::CONTROL + 0x100}, {"Arena", profile::CONTROL + 0x1000},
                 {"Code Cave", profile::CAVE}, {"Tick Site", profile::TICK_SITE}};
    for (const Q& q : quick) {
        ImGui::SameLine(0, px(6));
        const std::string tip = hex8(q.addr) + (conn ? "  (reads it)" : "");
        if (button(q.name, {.tone = Tone::Ghost, .small = true, .tooltip = tip.c_str(), .selected = s.addr == q.addr})) {
            s.addr = q.addr;
            if (conn) peek(ui);
        }
    }
    gap(sp::M);

    std::vector<uint8_t> data, prev;
    uint32_t base;
    std::string err;
    double readAt;
    {
        std::lock_guard<std::mutex> lock(s.m);
        data = s.data;
        prev = s.previous;
        base = s.dataAddr;
        err = s.memError;
        readAt = s.readAt;
    }
    if (!err.empty()) {
        pill(err.c_str(), pal::Danger, icon::Warning);
        gap(sp::S);
    }
    if (data.empty()) {
        caption("Peek reads live memory; Live re-reads it twice a second and highlights bytes that changed.", pal::Faint);
    } else {
         
        char info[96];
        const double age = std::max(0.0, steadySeconds() - readAt);
        if (s.live) snprintf(info, sizeof(info), "%08X  \xC2\xB7  %zu bytes  \xC2\xB7  live, updated %.1f s ago", base, data.size(), age);
        else if (age < 60) snprintf(info, sizeof(info), "%08X  \xC2\xB7  %zu bytes  \xC2\xB7  read %.1f s ago", base, data.size(), age);
        else snprintf(info, sizeof(info), "%08X  \xC2\xB7  %zu bytes  \xC2\xB7  read %.0f min ago", base, data.size(), age / 60.0);
        caption(info, pal::Faint);
        gap(sp::XS);
        HexViewOptions o;
        o.base = base;
        o.previous = prev.empty() ? nullptr : prev.data();
        o.previousSize = prev.size();
        o.height = 320;
        hexView("##hex", data.data(), data.size(), o);
    }

     
    gap(sp::L);
    fieldLabel("POKE  (DATA MEMORY ONLY)");
    gap(sp::XS + 2);
    inputHex("##pokeaddr", &s.pokeAddr, 130);
    ImGui::SameLine(0, px(8));
    const float bw = buttonWidth("Write", icon::Send);
    if (inputMono("##pokebytes", &s.pokeBytes, "Hex Bytes, e.g. 3F800000", (availWidth() - bw - px(8)) / scale())) s.pokeError.clear();
    ImGui::SameLine(0, px(8));
    bool write;
    {
        auto offline = requireConnection(ui);
        write = button("Write", {.tone = Tone::Danger, .icon = icon::Send, .tooltip = "Write and verify. Code goes through journalled patches instead.",
                                 .busy = ui.busy("Poke memory"), .successKey = "testing.poke"});
    }
    if (write) {
        try {
            if (s.pokeAddr < profile::TEXT_END) throw Error("That is the code segment; code changes go through journalled patches.");
            std::vector<uint8_t> bytes;
            try {
                bytes = parseHexBytes(s.pokeBytes);
            } catch (const std::exception&) {
                throw Error("Enter whole bytes in hex, e.g. 3F800000.");
            }
            if (bytes.empty()) throw Error("Enter the bytes to write.");
            s.pokeError.clear();
            const uint32_t a = s.pokeAddr;
            noteQueued("Poke memory");
            ui.post(
                "Poke memory",
                [a, bytes](Ctx& c) {
                    c.rt().write(a, bytes.data(), bytes.size(), true);
                    c.log("Wrote " + std::to_string(bytes.size()) + " bytes at " + hex8(a));
                },
                [&ui, a, n = bytes.size()](bool ok, const std::string&) {
                    if (!ok) return;
                    flashSuccess("testing.poke");
                    notify("Wrote " + std::to_string(n) + (n == 1 ? " byte at " : " bytes at ") + hex8(a), Level::Good);
                    if (ui.connected()) peek(ui);    
                });
        } catch (const std::exception& e) {
            s.pokeError = e.what();
        }
    }
    if (!s.pokeError.empty()) {
        gap(sp::XS);
        wrapped(s.pokeError.c_str(), mix(pal::Danger, pal::Text, 0.3f), type::Small);
    }
}

 
 
 

void loadJournal() {
    State& s = st();
    s.journalLoaded = true;
    s.journalPresent = false;
    s.journalError.clear();
    try {
        const auto path = Journal::path();
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) return;
        std::ifstream f(path, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        s.journal = Journal::parse(ss.str());
        s.journalPresent = true;
    } catch (const std::exception& e) {
        s.journalPresent = true;
        s.journalError = e.what();
    }
}

void drawRecovery(Ui& ui) {
    State& s = st();
    const EngineStatus status = Engine::get().status();
    static bool lastPending = false;
    if (!s.journalLoaded || status.journalPending != lastPending) {
        lastPending = status.journalPending;
        loadJournal();
    }
    const bool conn = ui.connected();
    const bool pending = status.journalPending || s.journalPresent;
    ImDrawList* d = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec4 tone = conn ? pal::Info : pending ? pal::Warning : pal::Success;
    const char* glyph = conn ? icon::Shield : pending ? icon::Restore : icon::Check;
    const float c = px(36);
    d->AddCircleFilled(p + ImVec2(c, c) * 0.5f, c * 0.5f, u32(alpha(tone, 0.14f)), 32);
    const ImVec2 is = textSize(FontKind::Icons, 16, glyph);
    drawText(d, FontKind::Icons, 16, p + (ImVec2(c, c) - is) * 0.5f, u32(tone), glyph);
    ImGui::SetCursorScreenPos(p + ImVec2(c + px(12), 0));
    ImGui::BeginGroup();
    std::string title, body;
    if (conn) {
        title = "Journal Active";
        body = "Original bytes of everything Blackrock writes are in the journal first. Disconnecting restores them (" +
               std::to_string(status.patches) + (status.patches == 1 ? " patch" : " patches") + " now).";
    } else if (pending) {
        title = "Recovery Journal Found";
        body = s.journalError.empty()
                   ? "From build " + (s.journal.build.empty() ? std::string("?") : s.journal.build) + ", pid " + hex8(s.journal.pid) + ": " +
                         std::to_string(s.journal.patches.size()) + " patches, " + std::to_string(s.journal.scratch.size()) +
                         " scratch regions. Recover restores only bytes that are still ours."
                   : "The journal could not be read: " + s.journalError;
    } else {
        title = "Nothing to Recover";
        body = "No journal: every byte Blackrock wrote has been restored.";
    }
    {
        FontScope f(FontKind::Semibold, type::Body);
        ImGui::TextUnformatted(title.c_str());
    }
    wrapped(body.c_str(), pal::Muted, type::Small);
    ImGui::EndGroup();
    gap(sp::M);
    if (conn) {
        if (button("Disconnect and Restore", {.icon = icon::Power})) Engine::get().disconnect();
    } else {
        DisabledScope none(!pending, "No journal to recover");
        if (button("Recover", {.tone = Tone::Primary, .icon = icon::Restore})) {
             
            std::string host = currentHost();
            if (host.empty()) host = Engine::get().settings().str("host", "192.168.0.254");
            Engine::get().recover(host);
        }
    }
    ImGui::SameLine(0, px(8));
    if (button("State Folder", {.tone = Tone::Ghost, .icon = icon::Folder})) {
        const std::wstring dir = state_dir().wstring();
        ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    if (s.journalPresent && s.journalError.empty() && !s.journal.patches.empty() && !conn) {
        gap(sp::M);
        if (beginTable("##journal", 3, std::min(200.0f, 34.0f + 30.0f * float(s.journal.patches.size())), ImGuiTableFlags_BordersOuter)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, px(90));
            ImGui::TableSetupColumn("Original", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Written", ImGuiTableColumnFlags_WidthStretch);
            tableHeaders();
            for (const auto& patch : s.journal.patches) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                mono(hex8(patch.address).c_str(), pal::Accent, 13);
                ImGui::TableSetColumnIndex(1);
                mono(hexBytes(patch.original).substr(0, 24).c_str(), pal::Muted, 13);
                ImGui::TableSetColumnIndex(2);
                mono(hexBytes(patch.written).substr(0, 24).c_str(), pal::Muted, 13);
            }
            endTable();
        }
    }
}

void resetOnDisconnect() {
    State& s = st();
    s.live = false;
    s.seenPatches = s.seenHooks = s.seenTasks = size_t(-1);
    std::lock_guard<std::mutex> lock(s.m);
    s.rtKnown = false;
    s.patches.clear();
}

void statusReport(Ctx& c) {
    Runtime& rt = c.rt();
    const bool alive = rt.waitFrames(1, 1500);
    char buf[256];
    snprintf(buf, sizeof(buf), "%s pid %08X; build %s; executor %s, heartbeat %u", rt.session().process().c_str(), rt.session().pid(),
             profile::BUILD, alive ? "ticking" : "NOT ticking (not in a game session?)", rt.heartbeat());
    std::string out = buf;
    const auto names = rt.appliedNames();
    std::string list;
    for (const auto& n : names) list += (list.empty() ? "" : ", ") + n;
    out += "\nnamed patches: " + (list.empty() ? std::string("none") : list);
    out += "\nhooks: " + std::to_string(rt.hookCount()) + "; tasks: " + std::to_string(rt.taskCount()) +
           "; owned patches: " + std::to_string(rt.session().patchCount());
    c.log(out);
    c.toast(alive ? "Executor ticking: status written to the log" : "Executor not ticking: load into a game session");
}

}   

BR_FEATURES(testing_page) {
    r.section({.id = "testing.runtime", .page = Page::Testing, .section = "Runtime", .order = 10, .draw = drawRuntime,
               .onDisconnect = resetOnDisconnect});
    r.section({.id = "testing.ncall", .page = Page::Testing, .section = "Native call", .order = 20, .draw = drawNativeCall});
    r.section({.id = "testing.recovery", .page = Page::Testing, .section = "Recovery", .order = 30, .draw = drawRecovery});
    r.section({.id = "testing.memory", .page = Page::Testing, .section = "Memory", .order = 40, .draw = drawMemory});
    r.action({.id = "testing.status_report", .page = Page::Testing, .section = "Runtime", .label = "Log Status Report",
              .help = "Write the connection, build, executor heartbeat, named patches, hooks and tasks to the log.", .run = statusReport});
    ui::styleSection("Runtime", {.icon = ui::icon::Testing});
    ui::styleSection("Native call", {.icon = ui::icon::Code});
    ui::styleSection("Memory", {.fullWidth = true, .icon = ui::icon::Memory});
    ui::styleSection("Recovery", {.icon = ui::icon::Shield});

     
    r.action({.id = "runtime.check.cave", .page = Page::Testing, .tab = kPortChecksTab, .section = "Runtime (shared)",
              .label = "Overflow Code Cave Below 0x183E000 (Read Only)",
              .help = "Reads 0x183D110..0x183E000: the process-param / PRX info at 0x183D110..0x183D178 and the zero padding after it that "
                      "the app uses as an overflow cave once the 7 KiB cave is full. Expected: the overflow cave was claimed at connect "
                      "(it read zero), and it holds only zeros or Blackrock stubs.",
              .run =
                  [](Ctx& c) {
                      Runtime& rt = c.rt();
                      const auto info = rt.read(0x183D110, 0x70);
                      c.log("process param / PRX info 0x183D110: " + hexBytes(info));
                      const auto low = rt.read(profile::CAVE_LOW, profile::CAVE_LOW_SIZE);
                      size_t used = 0;
                      uint32_t first = 0;
                      for (size_t i = 0; i < low.size(); ++i)
                          if (low[i]) {
                              if (!used) first = profile::CAVE_LOW + uint32_t(i);
                              ++used;
                          }
                      c.log(std::string("overflow cave ") + hex32(profile::CAVE_LOW) + ".." + hex32(profile::CAVE) + ": " +
                            (rt.caveExtended() ? "claimed at connect (it read zero)" : "NOT claimed (it was not zero at connect)"));
                      c.log(used ? std::to_string(used) + " non-zero bytes now, first at " + hex32(first) +
                                       (rt.caveExtended() ? " (Blackrock stubs: the 7 KiB cave filled up this session)" : " (game data: leave it alone)")
                                 : std::string("all zero now"));
                      c.log(rt.caveExtended() ? "verdict: usable overflow cave" : "verdict: not used; the app works with the 7 KiB cave only");
                  },
              .badge = kBadgeNeedsCheck});
}

}   
