 
 
 
 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <windows.h>
#include <shellapi.h>

#include "app/engine.h"
#include "app/registry.h"
#include "app/settings.h"
#include "core/error.h"
#include "core/runtime.h"
#include "features/lab_ui.h"
#include "game/loot_events.h"
#include "game/lab_observe.h"
#include "ui/widgets.h"

namespace brps3 {
namespace {

using namespace ui;
using labui::fmt;
using labui::hx;

constexpr const char* kSecDispatch = "Dispatch observer";
constexpr const char* kSecItem = "Item route observer";
constexpr const char* kSecRoots = "Root globals";
constexpr const char* kSecSchemas = "Schema globals";
constexpr const char* kDispatchToggle = "lab.dispatch.observe";
constexpr const char* kItemToggle = "lab.item_route.observe";
constexpr const char* kDispatchLoop = "lab.dispatch.poll";
constexpr const char* kItemLoop = "lab.item_route.poll";
constexpr const char* kValidateRoots = "Validate the LootEvent Root Globals";
constexpr const char* kCheckSchemas = "Check the LootEvent Schema Globals";
constexpr size_t kMaxRows = 2000;
constexpr float kRowH = 30;

std::string loot_eventName(uint32_t hash) {
    try {
        if (const loot_events::Entry* e = loot_events::find(hash)) return e->label;
    } catch (...) {
    }
    return {};
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

 
 
 

template <class Event>
struct Log {
    struct Row {
        uint64_t id = 0;                  
        double t = 0;
        Event e;
        std::string name;
    };
    std::mutex m;
    lab::Observer obs;                    
    lab::RingHeader hdr;
    std::deque<Row> rows;
    size_t total = 0;
    uint64_t nextId = 1;
    double started = 0;
    int failures = 0;                     
    bool pollStopped = false;             
    std::atomic<bool> active{false};
    std::string lastError;
     
    std::string filter;
    int mode = 0;
    bool paused = false;
    std::deque<Row> frozen;
    uint64_t selected = 0;                
};

struct DispatchSummary {
    uint32_t count = 0;
    double last = 0;
};

Log<lab::DispatchEvent>& dl() {
    static Log<lab::DispatchEvent> s;
    return s;
}
Log<lab::ItemEvent>& il() {
    static Log<lab::ItemEvent> s;
    return s;
}

std::map<uint32_t, DispatchSummary>& summary() {    
    static std::map<uint32_t, DispatchSummary> s;
    return s;
}

void pollDispatch(Ctx& c) {
    auto& L = dl();
    std::lock_guard<std::mutex> lock(L.m);
    if (!L.obs.active()) return;
    const auto slots = lab::pollObserver(c.rt(), L.obs, L.hdr);
    const double t = labui::now();
    for (const world::Bytes& s : slots) {
        typename Log<lab::DispatchEvent>::Row r;
        r.id = L.nextId++;
        r.t = t;
        r.e = lab::parseDispatchSlot(s);
        r.name = loot_eventName(r.e.root());
        for (const auto& row : r.e.rows) {
            DispatchSummary& sm = summary()[row[0]];
            ++sm.count;
            sm.last = t;
        }
        L.rows.push_front(std::move(r));
        ++L.total;
    }
    while (L.rows.size() > kMaxRows) L.rows.pop_back();
    if (!slots.empty()) requestRedraw();
}

void pollItems(Ctx& c) {
    auto& L = il();
    std::lock_guard<std::mutex> lock(L.m);
    if (!L.obs.active()) return;
    const auto slots = lab::pollObserver(c.rt(), L.obs, L.hdr);
    const double t = labui::now();
    for (const world::Bytes& s : slots) {
        typename Log<lab::ItemEvent>::Row r;
        r.id = L.nextId++;
        r.t = t;
        r.e = lab::parseItemSlot(s);
        r.name = loot_eventName(r.e.root);
        L.rows.push_front(std::move(r));
        ++L.total;
    }
    while (L.rows.size() > kMaxRows) L.rows.pop_back();
    if (!slots.empty()) requestRedraw();
}

template <class Event>
void setObserver(Ctx& c, bool on, Log<Event>& L, const char* loop, const lab::ObserveFingerprints& fps, const char* what,
                 lab::Observer (*start)(Runtime&), void (*poll)(Ctx&)) {
    if (!on) {
        c.stopLoop(loop);
        std::string err;
        {
            std::lock_guard<std::mutex> lock(L.m);
            lab::stopObserver(c.rt(), L.obs, &err);
        }
        L.active = false;
        if (!err.empty()) c.log(std::string("[lab] ") + what + " stop: " + err + " (still journalled; Disconnect restores it)");
        return;
    }
    if (L.active.load()) return;
    c.require(fps, what);
    {
        std::lock_guard<std::mutex> lock(L.m);
        L.obs = start(c.rt());
        L.hdr = {};
        L.started = labui::now();
        L.lastError.clear();
        L.failures = 0;
        L.pollStopped = false;
    }
    L.active = true;
     
    if (!c.state<bool>(std::string("lab.observer.") + loop)) {
        c.state<bool>(std::string("lab.observer.") + loop) = true;
        c.onDisconnect([&L](Ctx&) {
            std::lock_guard<std::mutex> lock(L.m);
            L.obs = {};
            L.active = false;
        });
    }
     
     
    c.startLoop(loop, 150, [poll, &L](Ctx& lc) {
        try {
            poll(lc);
            std::lock_guard<std::mutex> lock(L.m);
            L.failures = 0;
            L.lastError.clear();
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(L.m);
            L.lastError = e.what();
            if (++L.failures >= 20) {
                L.pollStopped = true;
                throw;
            }
        }
    });
    c.log(std::string("[lab] ") + what + " armed at " + hx(L.obs.site));
}

void setDispatch(Ctx& c, bool on) {
    setObserver(c, on, dl(), kDispatchLoop, lab::kDispatchFingerprints, "Dispatch observer", &lab::startDispatchObserver, &pollDispatch);
}
void setItems(Ctx& c, bool on) {
    setObserver(c, on, il(), kItemLoop, lab::kItemRouteFingerprints, "Item route observer", &lab::startItemObserver, &pollItems);
}

void resetObservers() {
    for (auto* L : {&dl()}) {
        std::lock_guard<std::mutex> lock(L->m);
        L->obs = {};
        L->active = false;
    }
    std::lock_guard<std::mutex> lock(il().m);
    il().obs = {};
    il().active = false;
}

uint32_t loot_eventsSelection() {
    try {
        const std::string h = Engine::get().settings().str("loot_events.selected", "");
        if (h.empty() && labui::demo()) return 0x7A0FD954;    
        return h.empty() ? 0 : uint32_t(std::stoul(h, nullptr, 16));
    } catch (...) {
        return 0;
    }
}

 

void seedDemo() {
    static bool done = false;
    if (done) return;
    done = true;
    if (!labui::demo()) return;
    const double t = labui::now();
    const uint32_t roots[] = {0x7A0FD954, 0x63C848A5, 0x6CEC7A87, 0xE10698B7, 0x7A0FD954, 0x4BE01A52, 0x63C848A5, 0x7A0FD954};
    auto& L = dl();
    for (int i = 0; i < 8; ++i) {
        typename Log<lab::DispatchEvent>::Row r;
        r.id = L.nextId++;
        r.t = t - 2.5 * (8 - i);
        r.e.seq = uint32_t(100 + i);
        r.e.count = i == 3 ? 3 : 1;
        r.e.list = 0x33D41200;
        r.e.caller = i % 3 ? 0x5ADCF4u : 0x5AE6F8u;
        r.e.guard = 1;
        r.e.rows.push_back({roots[i], 0x00000002, 0x3301A2C0 + uint32_t(i) * 0x40});
        if (i == 3) {
            r.e.rows.push_back({0x6CEC7A87, 0x00000002, 0x3301A400});
            r.e.rows.push_back({0x4BE01A52, 0x00000001, 0x3301A440});
        }
        r.name = loot_eventName(roots[i]);
        for (const auto& row : r.e.rows) {
            ++summary()[row[0]].count;
            summary()[row[0]].last = r.t;
        }
        L.rows.push_front(r);
        ++L.total;
    }
    L.hdr.seen = 214;
    L.hdr.busy = 0;
    L.hdr.full = 0;
    L.started = t - 61;
    auto& I = il();
    for (int i = 0; i < 3; ++i) {
        typename Log<lab::ItemEvent>::Row r;
        r.id = I.nextId++;
        r.t = t - 9.0 * (3 - i);
        r.e.seq = uint32_t(7 + i);
        r.e.root = i == 1 ? 0xE10698B7 : 0x63C848A5;
        r.e.schema = 0x80800E33;
        r.e.a = 0x33C0F100;
        r.e.b = 0x2;
        r.e.c = 0x01A4u + uint32_t(i);
        r.e.d = 0x01A93478;
        for (int k = 0; k < 0x20; ++k) r.e.payload[size_t(k)] = uint8_t(k < 0x18 ? (k * 11) & 0xFF : 0);
        r.e.payload[0x18] = 0x01;
        r.e.payload[0x19] = uint8_t(0xA4 + i);
        r.e.wrapper = {0x14118A8, 0x33C0F100, 0x2, 0};
        r.name = loot_eventName(r.e.root);
        I.rows.push_front(r);
        ++I.total;
    }
    I.hdr.seen = 3;
    I.started = t - 40;
}

 
 
 

void counters(const lab::RingHeader& h, size_t total, double started, bool active) {
    const float w = std::floor((availWidth() - px(8) * 3) / 4);
    statTile("seen", "SENDS SEEN", std::to_string(h.seen), active ? "since " + labui::ago(started) : std::string("observer off"), w,
             active ? pal::Success : pal::Muted);
    ImGui::SameLine(0, px(8));
    statTile("rows", "IN THE LOG", std::to_string(total), "newest first, last 2,000 kept", w);
    ImGui::SameLine(0, px(8));
    statTile("full", "DROPPED: RING FULL", std::to_string(h.full), "more than 32 between polls", w, h.full ? pal::Warning : pal::Text);
    ImGui::SameLine(0, px(8));
    statTile("busy", "DROPPED: BUSY", std::to_string(h.busy), "two senders at once", w, h.busy ? pal::Warning : pal::Text);
}

void pollStatus(const std::string& error, bool stopped) {
    if (error.empty()) return;
    labui::errorBox(stopped ? "Reading the ring stopped after repeated errors (" + error + "). Switch the observer off and on to resume."
                            : "Last read failed, retrying: " + error);
}

void drawDispatch(Ui& ui) {
    seedDemo();
    auto& L = dl();
    std::deque<Log<lab::DispatchEvent>::Row> rows;
    lab::RingHeader hdr;
    size_t total;
    double started;
    std::string lastError;
    bool pollStopped;
    std::map<uint32_t, DispatchSummary> sum;
    {
        std::lock_guard<std::mutex> lock(L.m);
        rows = L.rows;
        hdr = L.hdr;
        total = L.total;
        started = L.started;
        lastError = L.lastError;
        pollStopped = L.pollStopped;
        sum = summary();
    }
    if (L.paused) rows = L.frozen;
    const bool active = L.active.load();
    counters(hdr, total, started, active);
    pollStatus(lastError, pollStopped);
    gap(sp::M);
     
    const uint32_t sel = loot_eventsSelection();
    if (sel) {
        size_t n = 0;
        double last = 0;
        if (auto it = sum.find(sel); it != sum.end()) {
            n = it->second.count;
            last = it->second.last;
        }
        const std::string name = loot_eventName(sel);
        const std::string text = n ? fmt("Selected on the LootEvents tab: %s %s dispatched %zu times, last %s", hx(sel).c_str(), name.c_str(), n,
                                         labui::ago(last).c_str())
                                   : fmt("Selected on the LootEvents tab: %s %s has not been dispatched since the observer started", hx(sel).c_str(),
                                         name.c_str());
        pill(text.c_str(), n ? pal::Success : pal::Muted, n ? icon::Check : icon::Info);
        gap(sp::S);
    }
     
    searchField("##dfilter", &L.filter, "Filter Hash or Name", 260);
    ImGui::SameLine(0, px(10));
    std::vector<std::string> modes = {"Live", "By LootEvent"};
    pillTabs("##dmode", modes, &L.mode);
    ImGui::SameLine(0, px(10));
    if (button(L.paused ? "Resume" : "Pause", {.tone = Tone::Ghost, .icon = L.paused ? icon::Play : icon::Pause, .small = true,
                                               .tooltip = "Freeze the list (the observer keeps recording)", .selected = L.paused})) {
        L.paused = !L.paused;
        if (L.paused) L.frozen = rows;
    }
    ImGui::SameLine(0, px(6));
    if (button("Clear", {.tone = Tone::Ghost, .icon = icon::Delete, .small = true})) {
        std::lock_guard<std::mutex> lock(L.m);
        L.rows.clear();
        L.total = 0;
        summary().clear();
        L.frozen.clear();
        L.selected = 0;
    }
    ImGui::SameLine(0, px(6));
    {
        DisabledScope none(rows.empty(), "Nothing to copy");
        if (button("Copy", {.tone = Tone::Ghost, .icon = icon::Copy, .small = true, .tooltip = "Copy the log as tab-separated text"})) {
            std::string out = "seconds\troot\tname\trows\tguard\tcaller\tlist\n";
            for (const auto& r : rows) {
                std::string all;
                for (const auto& row : r.e.rows) all += (all.empty() ? "" : ",") + hx(row[0]);
                out += fmt("%.1f\t%s\t%s\t%u\t%u\t%s\t%s\n", r.t - started, all.c_str(), r.name.c_str(), r.e.count, r.e.guard, hx(r.e.caller).c_str(),
                           hx(r.e.list).c_str());
            }
            copyToClipboard(out, "dispatch log");
        }
    }
    gap(sp::S);
    const std::string f = lower(L.filter);
    if (L.mode == 1) {
        std::vector<std::pair<uint32_t, DispatchSummary>> v(sum.begin(), sum.end());
        std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second.count > b.second.count; });
        if (v.empty()) {
            caption(active ? "Waiting for the game to send loot_events..." : "Turn the observer on, then play: every sent loot_event is counted here.", pal::Faint);
            return;
        }
        if (beginTable("##dsum", 4, 320, ImGuiTableFlags_BordersOuter)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Root", ImGuiTableColumnFlags_WidthFixed, px(92));
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, px(70));
            ImGui::TableSetupColumn("Last Seen", ImGuiTableColumnFlags_WidthFixed, px(110));
            tableHeaders();
            for (const auto& [h, s] : v) {
                const std::string name = loot_eventName(h);
                if (!f.empty() && lower(hx(h) + " " + name).find(f) == std::string::npos) continue;
                ImGui::TableNextRow(0, px(kRowH));
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(int(h));
                labui::copyCell(hx(h), "hash", h == sel ? pal::Accent : pal::Text, kRowH);
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                cellText(name.empty() ? "(not in the catalogue)" : name.c_str(), name.empty() ? pal::Faint : pal::Text);
                ImGui::TableSetColumnIndex(2);
                cellText(std::to_string(s.count).c_str(), pal::Text);
                ImGui::TableSetColumnIndex(3);
                cellText(labui::ago(s.last).c_str(), pal::Muted);
            }
            endTable();
        }
        return;
    }
    if (rows.empty()) {
        caption(active ? "Waiting for the game to send loot_events..." : "Turn the observer on, then play or Call / Replay an loot_event: each send shows up here.",
                pal::Faint);
        return;
    }
    std::vector<int> view;
    for (int i = 0; i < int(rows.size()); ++i) {
        const auto& r = rows[size_t(i)];
        if (!f.empty()) {
            std::string hay = r.name;
            for (const auto& row : r.e.rows) hay += " " + hx(row[0]) + " " + loot_eventName(row[0]);
            if (lower(hay).find(f) == std::string::npos) continue;
        }
        view.push_back(i);
    }
    if (beginTable("##dlog", 6, 300, ImGuiTableFlags_BordersOuter)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, px(56));
        ImGui::TableSetupColumn("Root", ImGuiTableColumnFlags_WidthFixed, px(92));
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Rows", ImGuiTableColumnFlags_WidthFixed, px(48));
        ImGui::TableSetupColumn("Guard", ImGuiTableColumnFlags_WidthFixed, px(52));
        ImGui::TableSetupColumn("Caller", ImGuiTableColumnFlags_WidthFixed, px(92));
        tableHeaders();
        ImGuiListClipper clip;
        clip.Begin(int(view.size()), px(kRowH));
        while (clip.Step()) {
            for (int k = clip.DisplayStart; k < clip.DisplayEnd; ++k) {
                const int i = view[size_t(k)];
                const auto& r = rows[size_t(i)];
                const bool isSel = L.selected == r.id;
                const bool hit = sel && std::any_of(r.e.rows.begin(), r.e.rows.end(), [&](const auto& row) { return row[0] == sel; });
                ImGui::TableNextRow(0, px(kRowH));
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(static_cast<int>(r.id));
                const float y = ImGui::GetCursorPosY();
                if (tableRowSelectable("##d", isSel, nullptr, kRowH - 2 * ImGui::GetStyle().CellPadding.y / scale())) L.selected = isSel ? 0 : r.id;
                ImGui::PopID();
                ImGui::SetCursorPosY(y);
                cellText(fmt("+%.0fs", std::max(0.0, r.t - started)).c_str(), pal::Faint, FontKind::Body, type::Label, kRowH);
                ImGui::TableSetColumnIndex(1);
                cellText(hx(r.e.root()).c_str(), hit ? pal::Accent : pal::Text, FontKind::Mono, type::MonoSmall, kRowH);
                ImGui::TableSetColumnIndex(2);
                const std::string name = r.name.empty() ? "(not in the catalogue)" : r.name;
                cellText((name + (r.e.count > 1 ? fmt("  +%u more", r.e.count - 1) : std::string())).c_str(), r.name.empty() ? pal::Faint : pal::Text,
                         FontKind::Body, type::Label, kRowH);
                ImGui::TableSetColumnIndex(3);
                cellText(std::to_string(r.e.count).c_str(), pal::Muted, FontKind::Body, type::Label, kRowH);
                ImGui::TableSetColumnIndex(4);
                cellText(std::to_string(r.e.guard).c_str(), r.e.guard ? pal::Muted : pal::Warning, FontKind::Body, type::Label, kRowH);
                ImGui::TableSetColumnIndex(5);
                cellText(hx(r.e.caller).c_str(), pal::Faint, FontKind::Mono, type::MonoSmall, kRowH);
            }
        }
        endTable();
    }
    const auto dsel = std::find_if(rows.begin(), rows.end(), [&](const auto& x) { return L.selected && x.id == L.selected; });
    if (dsel != rows.end()) {
        const auto& r = *dsel;
        gap(sp::S);
        beginCard("##dsel", nullptr, {.raised = true});
        caption(fmt("send #%u  \xC2\xB7  list %08X  \xC2\xB7  caller %08X  \xC2\xB7  guard byte %u  \xC2\xB7  %u rows (first 8 copied)", r.e.seq, r.e.list,
                    r.e.caller, r.e.guard, r.e.count)
                    .c_str(),
                pal::Muted);
        gap(sp::XS);
        for (size_t k = 0; k < r.e.rows.size(); ++k) {
            const auto& row = r.e.rows[k];
            ImGui::PushID(int(k));
            hashLabel(row[0], "hash", row[0] == sel ? pal::Accent : pal::Text);
            ImGui::SameLine(0, px(10));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(3));
            const std::string n = loot_eventName(row[0]);
            caption((n.empty() ? std::string("(not in the catalogue)") : n).c_str(), n.empty() ? pal::Faint : pal::Text);
            ImGui::SameLine(0, px(12));
            caption(fmt("%08X %08X", row[1], row[2]).c_str(), pal::Faint);
            ImGui::PopID();
        }
        endCard();
    }
}

 
 
 

void drawItems(Ui&) {
    seedDemo();
    auto& L = il();
    std::deque<Log<lab::ItemEvent>::Row> rows;
    lab::RingHeader hdr;
    size_t total;
    double started;
    std::string lastError;
    bool pollStopped;
    {
        std::lock_guard<std::mutex> lock(L.m);
        rows = L.rows;
        hdr = L.hdr;
        total = L.total;
        started = L.started;
        lastError = L.lastError;
        pollStopped = L.pollStopped;
    }
    counters(hdr, total, started, L.active.load());
    pollStatus(lastError, pollStopped);
    gap(sp::M);
    searchField("##ifilter", &L.filter, "Filter Root, Item or Name", 260);
    ImGui::SameLine(0, px(8));
    if (button("Clear##items", {.tone = Tone::Ghost, .icon = icon::Delete, .small = true})) {
        std::lock_guard<std::mutex> lock(L.m);
        L.rows.clear();
        L.total = 0;
        L.selected = 0;
    }
    gap(sp::S);
    if (rows.empty()) {
        caption(L.active.load() ? "Waiting for an item event (pick something up, dismantle, claim a reward)..."
                                : "Turn the observer on: every item_action / item_acquired the producer submits shows up here with its payload.",
                pal::Faint);
        return;
    }
    const std::string f = lower(L.filter);
    if (beginTable("##ilog", 6, std::min(300.0f, 38.0f + kRowH * float(rows.size())), ImGuiTableFlags_BordersOuter)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, px(56));
        ImGui::TableSetupColumn("Root", ImGuiTableColumnFlags_WidthFixed, px(92));
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Item (+0x18)", ImGuiTableColumnFlags_WidthFixed, px(96));
        ImGui::TableSetupColumn("a / b", ImGuiTableColumnFlags_WidthFixed, px(150));
        ImGui::TableSetupColumn("Schema", ImGuiTableColumnFlags_WidthFixed, px(92));
        tableHeaders();
        for (int i = 0; i < int(rows.size()); ++i) {
            const auto& r = rows[size_t(i)];
            if (!f.empty() && lower(hx(r.e.root) + " " + r.name + " " + fmt("%04x %u", r.e.item(), r.e.item())).find(f) == std::string::npos) continue;
            const bool isSel = L.selected == r.id;
            ImGui::TableNextRow(0, px(kRowH));
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(static_cast<int>(r.id));
            const float y = ImGui::GetCursorPosY();
            if (tableRowSelectable("##i", isSel, nullptr, kRowH - 2 * ImGui::GetStyle().CellPadding.y / scale())) L.selected = isSel ? 0 : r.id;
            ImGui::PopID();
            ImGui::SetCursorPosY(y);
            cellText(fmt("+%.0fs", std::max(0.0, r.t - started)).c_str(), pal::Faint, FontKind::Body, type::Label, kRowH);
            ImGui::TableSetColumnIndex(1);
            cellText(hx(r.e.root).c_str(), pal::Accent, FontKind::Mono, type::MonoSmall, kRowH);
            ImGui::TableSetColumnIndex(2);
            cellText(r.name.empty() ? "(not in the catalogue)" : r.name.c_str(), r.name.empty() ? pal::Faint : pal::Text, FontKind::Body, type::Label, kRowH);
            ImGui::TableSetColumnIndex(3);
            cellText(fmt("%04X (%u)", r.e.item(), r.e.item()).c_str(), pal::Text, FontKind::Mono, type::MonoSmall, kRowH);
            ImGui::TableSetColumnIndex(4);
            cellText((hx(r.e.a) + " " + hx(r.e.b)).c_str(), pal::Muted, FontKind::Mono, type::MonoSmall, kRowH);
            ImGui::TableSetColumnIndex(5);
            cellText(hx(r.e.schema).c_str(), r.e.schema == lab::kSchemaItem ? pal::Muted : pal::Warning, FontKind::Mono, type::MonoSmall, kRowH);
        }
        endTable();
    }
    const auto isel = std::find_if(rows.begin(), rows.end(), [&](const auto& x) { return L.selected && x.id == L.selected; });
    if (isel != rows.end()) {
        const auto& r = *isel;
        gap(sp::S);
        beginCard("##isel", nullptr, {.raised = true});
        caption(fmt("producer 0xC3C370(a %08X, b %08X, c %04X, d %08X)  \xC2\xB7  row %08X  \xC2\xB7  wrapper %08X %08X %08X %08X", r.e.a, r.e.b, r.e.c & 0xFFFF,
                    r.e.d, r.e.rowPtr, r.e.wrapper[0], r.e.wrapper[1], r.e.wrapper[2], r.e.wrapper[3])
                    .c_str(),
                pal::Muted);
        tooltip("Payload: 0x20 bytes, schema 80800E33; +0x18 is the producer's u16 argument.");
        gap(sp::XS);
        HexViewOptions o;
        o.base = r.e.payloadPtr;
        hexView("##ipayload", r.e.payload.data(), r.e.payload.size(), o);
        endCard();
    }
}

 

struct RootsView {
    std::vector<lab::RootCheck> roots;
    std::vector<lab::HashBlockRow> block;
};
labui::Slot<RootsView>& roots() {
    static labui::Slot<RootsView> s;
    return s;
}
labui::Slot<std::vector<lab::SchemaRow>>& schemas() {
    static labui::Slot<std::vector<lab::SchemaRow>> s;
    return s;
}

void runValidateRoots(Ctx& c) {
    c.require(lab::kItemRouteFingerprints, "Item route (lab)");
    world::RuntimeReader mem(c.rt());
    RootsView v;
    v.roots = lab::checkItemRoots(mem);
    v.block = lab::readLootEventHashBlock(mem);
    size_t ok = 0;
    for (const auto& r : v.roots) {
        ok += r.ok();
        c.log(fmt("[lab] %-30s %08X = %08X (expected %08X) %s", r.name.c_str(), r.addr, r.actual, r.expected, r.ok() ? "ok" : "DIFFERS"));
    }
    c.toast(fmt("Root globals: %zu of %zu as expected", ok, v.roots.size()));
    roots().set(std::move(v));
}

void runCheckSchemas(Ctx& c) {
    c.require(lab::kItemRouteFingerprints, "Item route (lab)");
    world::RuntimeReader mem(c.rt());
    auto rows = lab::schemaRows();
    lab::readSchemaRows(mem, rows, [&] { return c.cancelled(); });
    size_t ok = 0, mapped = 0;
    for (const auto& r : rows) {
        mapped += r.inTable;
        ok += r.ok();
    }
    c.log(fmt("[lab] schema globals: %zu of %zu mapped rows hold their schema (%zu catalogue globals)", ok, mapped, rows.size()));
    schemas().set(std::move(rows));
}

void seedRootsDemo() {
    static bool done = false;
    if (done) return;
    done = true;
    if (!labui::demo()) return;
    RootsView v;
    v.roots = {{"item_action", lab::kRootItemAction, 0x63C848A5, 0x63C848A5},
               {"item_acquired", lab::kRootItemAcquired, 0xE10698B7, 0xE10698B7},
               {"gather_material", lab::kRootGather, 0x7A0FD954, 0x7A0FD954},
               {"alternate gather", lab::kRootAltGather, 0xED639013, 0xED639013},
               {"item schema *(018DF3D8)", lab::kSchemaGlobalItem, lab::kSchemaItem, lab::kSchemaItem}};
    const uint32_t hashes[] = {0x9B44A61F, 0x3E1F2D20, 0x7A0FD954, 0xED639013, 0x63C848A5, 0x52D21B0E};
    for (uint32_t i = 0; i < 6; ++i) v.block.push_back({i * 4, lab::kLootEventHashBlock + i * 4, hashes[i]});
    roots().set(v);
    try {
        auto rows = lab::schemaRows();
        for (auto& r : rows)
            if (r.inTable) {
                r.read = true;
                r.object = 0x1AB4060 + (r.ps3 - 0x18DF3B0) * 8;
                r.actual = r.schema;
            }
        schemas().set(rows);
    } catch (...) {
    }
}

void drawRoots(Ui& ui) {
    seedRootsDemo();
    if (labui::jobButton(ui, kValidateRoots, icon::Shield,
                         "Read the item / gather roots, the item schema global and the loot_event hash block (read only). The research route "
                         "needs item_action 63C848A5 at 0x1A93478, item_acquired E10698B7 at 0x1A9347C, the gather roots at 0x1A207A4 / "
                         "0x1A207A0 and schema 80800E33 behind 0x18DF3D8. These are written by the game at start-up.",
                         Tone::Primary, "Validate"))
        labui::post(ui, kValidateRoots, runValidateRoots);
    labui::readStamp(roots().at());
    labui::errorBox(roots().error());
    gap(sp::S);
    const auto v = roots().get();
    if (!v) return;
    if (beginTable("##roots", 4, 0, ImGuiTableFlags_BordersOuter)) {
        ImGui::TableSetupColumn("Root", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, px(80));
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, px(80));
        ImGui::TableSetupColumn("##ok", ImGuiTableColumnFlags_WidthFixed, px(70));
        tableHeaders();
        for (const lab::RootCheck& r : v->roots) {
            ImGui::TableNextRow(0, px(28));
            ImGui::TableSetColumnIndex(0);
            cellText(r.name.c_str(), pal::Text, FontKind::Body, type::Label, 28);
            ImGui::TableSetColumnIndex(1);
            cellText(hx(r.addr).c_str(), pal::Muted, FontKind::Mono, type::MonoSmall, 28);
            ImGui::TableSetColumnIndex(2);
            cellText(hx(r.actual).c_str(), r.ok() ? pal::Text : pal::Danger, FontKind::Mono, type::MonoSmall, 28);
            if (!r.ok()) tooltip(("expected " + hx(r.expected)).c_str());
            ImGui::TableSetColumnIndex(3);
            cellText(r.ok() ? "ok" : "differs", r.ok() ? pal::Success : pal::Danger, FontKind::Body, type::Label, 28);
        }
        endTable();
    }
    gap(sp::M);
    fieldLabel("LOOT_EVENT HASH BLOCK  01A2074C  (SAME LAYOUT AS XBOX 8344DC38)");
    gap(sp::XS);
    if (beginTable("##hblock", 3, std::min(260.0f, 34.0f + 28.0f * float(v->block.size())), ImGuiTableFlags_BordersOuter)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthFixed, px(60));
        ImGui::TableSetupColumn("Hash", ImGuiTableColumnFlags_WidthFixed, px(92));
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        tableHeaders();
        for (const auto& b : v->block) {
            ImGui::TableNextRow(0, px(28));
            ImGui::TableSetColumnIndex(0);
            cellText(fmt("+%02X", b.offset).c_str(), pal::Faint, FontKind::Mono, type::MonoSmall, 28);
            ImGui::TableSetColumnIndex(1);
            cellText(hx(b.hash).c_str(), pal::Text, FontKind::Mono, type::MonoSmall, 28);
            ImGui::TableSetColumnIndex(2);
            const std::string n = loot_eventName(b.hash);
            cellText(n.empty() ? "-" : n.c_str(), n.empty() ? pal::Faint : pal::Text, FontKind::Body, type::Label, 28);
        }
        endTable();
    }
}

void drawSchemas(Ui& ui) {
    seedRootsDemo();
    if (labui::jobButton(ui, kCheckSchemas, icon::Shield,
                         "Read the PS3 schema-global table and each schema object (one read each). The catalogue's schema_global column "
                         "holds Xbox addresses; on PS3 the table is Xbox - 0x8344EBD4 + 0x18DF3BC (20 pointers, deltas identical). Each "
                         "pointer leads to an object whose first word must be the schema.",
                         Tone::Primary, "Check"))
        labui::post(ui, kCheckSchemas, runCheckSchemas);
    labui::readStamp(schemas().at());
    labui::errorBox(schemas().error());
    gap(sp::S);
    const auto rows = schemas().get();
    if (!rows) return;
    size_t ok = 0, mapped = 0;
    for (const auto& r : *rows) {
        ok += r.ok();
        mapped += r.inTable;
    }
    caption(fmt("%zu of %zu mapped globals hold their schema  \xC2\xB7  %zu catalogue globals in all", ok, mapped, rows->size()).c_str(), pal::Muted);
    gap(sp::S);
    if (beginTable("##schemas", 5, 300, ImGuiTableFlags_BordersOuter)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Xbox", ImGuiTableColumnFlags_WidthFixed, px(78));
        ImGui::TableSetupColumn("PS3 Slot", ImGuiTableColumnFlags_WidthFixed, px(78));
        ImGui::TableSetupColumn("Schema", ImGuiTableColumnFlags_WidthFixed, px(78));
        ImGui::TableSetupColumn("Live", ImGuiTableColumnFlags_WidthFixed, px(78));
        ImGui::TableSetupColumn("LootEvents", ImGuiTableColumnFlags_WidthStretch);
        tableHeaders();
        for (const auto& r : *rows) {
            ImGui::TableNextRow(0, px(28));
            ImGui::TableSetColumnIndex(0);
            cellText(hx(r.xbox).c_str(), r.inTable ? pal::Muted : pal::Faint, FontKind::Mono, type::MonoSmall, 28);
            ImGui::TableSetColumnIndex(1);
            cellText(r.inTable ? hx(r.ps3).c_str() : "outside", r.inTable ? pal::Text : pal::Faint, FontKind::Mono, type::MonoSmall, 28);
            if (r.inTable) tooltip(r.read ? fmt("*(%08X) = object %08X", r.ps3, r.object).c_str() : "not read yet");
            else tooltip("Outside the mapped table 8344EBC8..8344EC14: no PS3 address");
            ImGui::TableSetColumnIndex(2);
            cellText(hx(r.schema).c_str(), r.inTable ? pal::Text : pal::Faint, FontKind::Mono, type::MonoSmall, 28);
            ImGui::TableSetColumnIndex(3);
            cellText(r.read ? hx(r.actual).c_str() : "-", !r.read ? pal::Faint : r.ok() ? pal::Success : pal::Danger, FontKind::Mono, type::MonoSmall, 28);
            ImGui::TableSetColumnIndex(4);
            std::string names;
            for (size_t i = 0; i < r.loot_events.size() && i < 3; ++i) {
                const std::string n = loot_eventName(r.loot_events[i]);
                names += (i ? ", " : "") + (n.empty() ? hx(r.loot_events[i]) : n);
            }
            if (r.loot_events.size() > 3) names += fmt("  +%zu", r.loot_events.size() - 3);
            cellText(names.c_str(), r.inTable ? pal::Muted : pal::Faint, FontKind::Body, type::Label, 28);
            if (r.loot_events.size() > 3) tooltip(fmt("%zu loot_events use this global", r.loot_events.size()).c_str());
        }
        endTable();
    }
}

void resetAll() {
    resetObservers();
    roots().clear();
    schemas().clear();
}

 

void probeRoots(Ctx& c) {
    c.require(lab::kItemRouteFingerprints, "Item route (lab)");
    world::RuntimeReader mem(c.rt());
    size_t ok = 0;
    const auto rs = lab::checkItemRoots(mem);
    for (const auto& r : rs) {
        ok += r.ok();
        c.log(fmt("%-26s %08X = %08X (expected %08X) %s", r.name.c_str(), r.addr, r.actual, r.expected, r.ok() ? "ok" : "DIFFERS"));
    }
    auto rows = lab::schemaRows();
    lab::readSchemaRows(mem, rows, [&] { return c.cancelled(); });
    size_t sok = 0, mapped = 0;
    for (const auto& r : rows) {
        mapped += r.inTable;
        sok += r.ok();
        if (r.inTable && !r.ok()) c.log(fmt("  schema global %08X -> %08X: object %08X holds %08X, expected %08X", r.xbox, r.ps3, r.object, r.actual, r.schema));
    }
    c.log(fmt("schema globals: %zu of %zu mapped rows hold their schema", sok, mapped));
    c.log("expected: every root and schema as listed (they are set while the game starts)");
    c.log(ok == rs.size() && sok == mapped ? "verdict: loot_event root and schema globals confirmed" : "verdict: some globals differ (send this report)");
}

void probeHookSites(Ctx& c) {
    const struct {
        const char* what;
        uint32_t site, word;
    } sites[] = {{"loot_event_send_now (dispatch observer)", lab::kSendNow, lab::kSendNowWord},
                 {"item submit bl 0x26B504 (item route observer)", lab::kItemSubmitSite, lab::kItemSubmitWord}};
    bool ok = true;
    for (const auto& s : sites) {
        const uint32_t w = c.rt().u32(s.site);
        const bool hooked = c.rt().hooked(s.site);
        c.log(fmt("%-46s %08X: %08X (original %08X)%s", s.what, s.site, w, s.word, hooked ? ", hooked by Blackrock now" : ""));
        if (w != s.word && !hooked) ok = false;
    }
    c.require(lab::kDispatchFingerprints, "Dispatch observer");
    c.require(lab::kItemRouteFingerprints, "Item route observer");
    c.log("fingerprints of loot_event_send_now, the item producer and loot_event_emit_investment match");
    c.log(ok ? "verdict: both hook sites hold their original words (the observers can be armed)" : "verdict: a hook site holds something else (send this report)");
}

}   

BR_FEATURES(lab_dispatch) {
    const Page P = Page::LootEvents;
    r.toggle({.id = kDispatchToggle, .page = P, .tab = "Dispatch", .section = kSecDispatch, .label = "Observe Dispatch",
              .help = "Hook loot_event_send_now (0x5ADB4C) and list every loot_event list the game sends. Reverted when you switch it off, on "
                      "Panic and on disconnect.",
              .set = setDispatch});
    r.section({.id = "lab.dispatch.log", .page = P, .tab = "Dispatch", .section = kSecDispatch, .order = 10, .draw = drawDispatch,
               .onDisconnect = resetAll});
    ui::styleSection(kSecDispatch, {.fullWidth = true,
                                    .icon = ui::icon::LootEvents});

    r.toggle({.id = kItemToggle, .page = P, .tab = "Item route", .section = kSecItem, .label = "Observe the Item Route",
              .help = "Hook the item producer's submit call (0xC3C408) and list each item_action / item_acquired with its payload. The "
                      "original call still runs. Research route: reverted on off, Panic and disconnect.",
              .set = setItems, .badge = kBadgeDev});
    r.section({.id = "lab.item_route.log", .page = P, .tab = "Item route", .section = kSecItem, .order = 10, .draw = drawItems, .badge = kBadgeDev});
    r.section({.id = "lab.item_route.roots", .page = P, .tab = "Item route", .section = kSecRoots, .order = 20, .draw = drawRoots, .badge = kBadgeDev});
    r.section({.id = "lab.item_route.schemas_card", .page = P, .tab = "Item route", .section = kSecSchemas, .order = 30, .draw = drawSchemas,
               .badge = kBadgeDev});
    r.action({.id = "lab.item_route.validate", .page = P, .tab = "Item route", .section = kSecRoots, .label = kValidateRoots,
              .help = "Read the item / gather root hashes, the item schema global and the loot_event hash block (read only).", .run = runValidateRoots,
              .badge = kBadgeDev});
    r.action({.id = "lab.item_route.schemas", .page = P, .tab = "Item route", .section = kSecSchemas, .label = kCheckSchemas,
              .help = "Check every loot_event schema global from the catalogue against the PS3 table (read only).", .run = runCheckSchemas,
              .badge = kBadgeDev});
    ui::styleSection(kSecItem, {.fullWidth = true,
                                .icon = ui::icon::Loot});
    ui::styleSection(kSecRoots, {.icon = ui::icon::Shield, .ownsActions = true});
    ui::styleSection(kSecSchemas, {.icon = ui::icon::List,
                                   .ownsActions = true});

    auto probe = [&](const char* id, const char* label, const char* help, void (*run)(Ctx&)) {
        r.action({.id = id, .page = Page::Testing, .tab = kPortChecksTab, .section = "Lab (development)", .label = label, .help = help, .run = run});
    };
    probe("lab.check.roots", "LootEvent Root and Schema Globals",
          "Reads the item / gather roots, the item schema global and every mapped loot_event schema global. Expected: all as listed.", probeRoots);
    probe("lab.check.hook_sites", "Observer Hook Sites (Read Only)",
          "Reads the words at loot_event_send_now and the item submit call and checks the fingerprints, without hooking. Expected: the original "
          "words 7C0802A6 / 4B62F0FD.",
          probeHookSites);
}

}   
