 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "app/engine.h"
#include "app/registry.h"
#include "app/settings.h"
#include "core/error.h"
#include "core/ppc.h"
#include "core/profile.h"
#include "core/runtime.h"
#include "features/activity_shared.h"
#include "game/activity_addr.h"
#include "game/activity_changeworld.h"
#include "game/activity_client.h"
#include "game/activity_deploy.h"
#include "game/activity_factions.h"
#include "game/activity_monitor.h"
#include "game/activity_session.h"
#include "game/names_catalog.h"
#include "game/world_factions.h"
#include "game/world_mem.h"
#include "ui/widgets.h"

namespace brps3 {
namespace {

using namespace ui;
using activity_ui::hx;
using activity_ui::uiDemo;

std::filesystem::path activityDir() {
    std::filesystem::path d = state_dir() / "activity";
    std::error_code ec;
    std::filesystem::create_directories(d, ec);
    return d;
}

 
 
 

constexpr const char* kCaptureState = "activity.captures";
constexpr const char* kCaptureLoop = "activity.captures";
 
constexpr const char* kCaptureTitles[activity::kCaptureKinds] = {"Join Refuse", "Activity Host", "Session Disband Reason",
                                                                 "Activity Start Status"};

 
struct CaptureSession {
    std::mutex m;
    uint32_t area = 0;
};

 
struct CaptureUi {
    std::mutex m;
    bool armed[activity::kCaptureKinds] = {};
    activity::Capture caps[activity::kCaptureKinds];
    bool have = false;
    std::atomic<bool> live{false};
};
CaptureUi& cap() {
    static CaptureUi s;
    return s;
}

uint32_t ensureArea(Ctx& c) {
    CaptureSession& cs = c.state<CaptureSession>(kCaptureState);
    std::lock_guard<std::mutex> lock(cs.m);
    if (cs.area) return cs.area;
    const std::vector<uint8_t> zero(activity::kCaptureAreaSize, 0);
    cs.area = c.rt().alloc(activity::kCaptureAreaSize, zero.data(), zero.size());    
    return cs.area;
}

uint32_t sessionArea(Ctx& c) {
    CaptureSession& cs = c.state<CaptureSession>(kCaptureState);
    std::lock_guard<std::mutex> lock(cs.m);
    return cs.area;
}

void setArmed(activity::CaptureKind kind, bool on) {
    CaptureUi& s = cap();
    std::lock_guard<std::mutex> lock(s.m);
    s.armed[int(kind)] = on;
}

void armCapture(Ctx& c, activity::CaptureKind kind) {
    const activity::CaptureSpec& spec = activity::captureSpec(kind);
    c.require(kind == activity::CaptureKind::JoinRefuse ? activity::kJoinRefuseFingerprints : activity::kLobbyFingerprints, spec.label);
    Runtime& rt = c.rt();
    if (!rt.hooked(spec.site)) {
        const uint32_t block = ensureArea(c) + uint32_t(int(kind)) * activity::kCaptureBlockSize;
        const std::vector<uint8_t> zero(activity::kCaptureBlockSize, 0);    
        rt.write(block, zero.data(), zero.size(), false);
        const int destReg = spec.destReg, words = spec.words;
        const uint32_t site = spec.site;
         
        const uint32_t after = rt.sharedCode(
            activity::captureAfterName(kind), [block, words](uint32_t base) { return activity::captureAfter(base, block, words); },
            activity::kCaptureAfterSize);
        rt.hook(
            site, [site, after, block, destReg](uint32_t base, uint32_t) { return activity::captureWrap(base, site, after, block, destReg); },
            activity::addr::MFLR_R0, activity::kCaptureWrapSize);
    }
    setArmed(kind, true);
}

void disarmCapture(Ctx& c, activity::CaptureKind kind) {
    Runtime& rt = c.rt();
    const uint32_t site = activity::captureSpec(kind).site;
    if (rt.hooked(site)) rt.unhook(site);    
    setArmed(kind, false);
}

void readCaptures(Ctx& c) {
    const uint32_t area = sessionArea(c);
    if (!area) return;
    const world::Bytes b(area, c.rt().read(area, activity::kCaptureAreaSize));
    CaptureUi& s = cap();
    std::lock_guard<std::mutex> lock(s.m);
    for (int k = 0; k < activity::kCaptureKinds; ++k) s.caps[k] = activity::parseCapture(b, activity::CaptureKind(k));
    s.have = true;
}

void setCaptureLive(Ui& ui, bool on) {
    cap().live = on;
    if (on)
        ui.post("Watch captures", [](Ctx& c) { c.startLoop(kCaptureLoop, 1000, [](Ctx& lc) { readCaptures(lc); }); });
    else ui.post("Stop watching captures", [](Ctx& c) { c.stopLoop(kCaptureLoop); });
}

std::string captureReport() {
    CaptureUi& s = cap();
    std::lock_guard<std::mutex> lock(s.m);
    std::string r = "Capture hooks\n";
    for (int k = 0; k < activity::kCaptureKinds; ++k) {
        const activity::CaptureSpec& spec = activity::captureSpec(activity::CaptureKind(k));
        r += std::string(spec.label) + " (" + hx(spec.site) + ", " + (s.armed[k] ? "armed" : "off") +
             "): " + activity::describeCapture(activity::CaptureKind(k), s.caps[k]) + "\n";
    }
    return r;
}

void seedCaptureDemo() {
    static bool done = false;
    if (done || !uiDemo()) return;
    done = true;
    CaptureUi& s = cap();
    std::lock_guard<std::mutex> lock(s.m);
    s.have = true;
    s.armed[int(activity::CaptureKind::JoinRefuse)] = true;
    s.armed[int(activity::CaptureKind::SessionDisband)] = true;
    activity::Capture& jr = s.caps[int(activity::CaptureKind::JoinRefuse)];
    jr.counter = 2;
    jr.r3 = 1;
    jr.dest = 0x30661240;
    jr.words = {0x00000001, 0x8A3C01FE, 0x00000002, 0x11AA55F0, 0x0000001A};    
    activity::Capture& dis = s.caps[int(activity::CaptureKind::SessionDisband)];
    dis.counter = 1;
    dis.r3 = 1;
    dis.words = {0x0000000A};
     
    ui::demoShowSwitch("activity.capture.join_refuse", true);
    ui::demoShowSwitch("activity.capture.session_disband", true);
}

void drawCaptures(Ui& ui) {
    CaptureUi& s = cap();
    seedCaptureDemo();
    bool armed[activity::kCaptureKinds];
    activity::Capture caps[activity::kCaptureKinds];
    bool have;
    {
        std::lock_guard<std::mutex> lock(s.m);
        for (int k = 0; k < activity::kCaptureKinds; ++k) {
            armed[k] = s.armed[k];
            caps[k] = s.caps[k];
        }
        have = s.have;
    }
    {
        auto off = requireConnection(ui);
        if (button("Read Captures", {.icon = icon::Refresh, .busy = ui.busy("Read captures")})) ui.post("Read captures", [](Ctx& c) { readCaptures(c); });
        tooltip("Each hook patches one decoder's entry (one word) to a resident stub that records the decoded fields, then runs the decoder "
                "untouched. Not re-entrant: turn the switches off when done. Disconnect and Panic restore every hook. A disband means the "
                "server rejected the lobby (reason names are unknown, shown raw as on Xbox); a populated activity-host means it gave a host.");
        ImGui::SameLine(0, px(8));
        bool live = s.live.load();
        if (toggleSwitch("##caplive", &live)) setCaptureLive(ui, live);
        tooltip("Re-read the capture block every second (lobby_probe.py watch)");
        ImGui::SameLine(0, px(8));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(7));
        caption(live ? "Live" : "Live Off", live ? pal::Text : pal::Muted);
    }
    ImGui::SameLine();
    alignRight(buttonWidth("Copy", icon::Copy, true));
    if (button("Copy##caprep", {.tone = Tone::Ghost, .icon = icon::Copy, .small = true, .tooltip = "Copy every capture as text"}))
        copyToClipboard(captureReport(), "capture report");
    gap(sp::M);
    for (int k = 0; k < activity::kCaptureKinds; ++k) {
        const activity::CaptureKind kind = activity::CaptureKind(k);
        const activity::CaptureSpec& spec = activity::captureSpec(kind);
        ImGui::PushID(k);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = availWidth();
        const float h = px(52);
        const char* st = armed[k] ? "armed" : "off";
        const ImVec4 col = armed[k] ? pal::Success : pal::Muted;
        const float pw = textSize(FontKind::Semibold, type::Small - 0.5f, st).x + px(20);
        const std::string line =
            caps[k].counter || armed[k] || have ? activity::describeCapture(kind, caps[k]) : std::string("Turn the switch above on to record this decoder.");
         
        ImGui::InvisibleButton("##row", ImVec2(w, h));
        const bool hovered = ImGui::IsItemHovered();
        if (caps[k].counter) {
            if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            tooltip(("Click to copy\n" + line).c_str());
            if (ImGui::IsItemClicked()) copyToClipboard(line, spec.label);
        }
        ImDrawList* d = ImGui::GetWindowDrawList();
        d->AddRectFilled(p, p + ImVec2(w, h), u32(hovered && caps[k].counter ? pal::Raised : pal::Input), px(6));
        if (armed[k]) d->AddRectFilled(p + ImVec2(0, px(8)), p + ImVec2(px(3), h - px(8)), u32(pal::Success), px(2));
        drawText(d, FontKind::Semibold, type::Label, p + ImVec2(px(12), px(9)), u32(pal::Text), kCaptureTitles[k]);
        drawText(d, FontKind::Mono, type::MonoSmall, p + ImVec2(px(12) + textSize(FontKind::Semibold, type::Label, kCaptureTitles[k]).x + px(8), px(10)),
                 u32(pal::Faint), hx(spec.site).c_str());
        drawText(d, FontKind::Body, type::Small, p + ImVec2(px(12), px(29)), u32(caps[k].counter ? pal::Muted : pal::Faint),
                 ellipsize(line.c_str(), w - pw - px(36), FontKind::Body, type::Small).c_str());
        ImGui::SetCursorScreenPos(ImVec2(p.x + w - pw - px(12), p.y + (h - px(24)) * 0.5f));
        pill(st, col);
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + px(4)));
        ImGui::Dummy(ImVec2(0, 0));
        ImGui::PopID();
    }
    if (s.live.load()) requestRedraw();
}

 
 
 

struct LobbyState {
    std::mutex m;
    bool have = false;
    activity::SessionRead read;
    std::string error;
};
LobbyState& lobby() {
    static LobbyState s;
    return s;
}

void seedLobbyDemo() {
    static bool done = false;
    if (done || !uiDemo()) return;
    done = true;
    LobbyState& s = lobby();
    std::lock_guard<std::mutex> lock(s.m);
    s.have = true;
    s.read.ok = true;
    s.read.root = 0x30650150;
    activity::SessionSlot& sl = s.read.slots[0];
    sl.ok = true;
    sl.slot = 0;
    sl.index = 0;
    sl.goal = 0x30656868;
    sl.gate = 5;
    auto words = [](std::initializer_list<std::pair<uint32_t, uint32_t>> at) {
        std::vector<uint8_t> b(activity::kFieldValueBytes, 0);
        for (const auto& [off, v] : at) {
            b[off] = uint8_t(v >> 24);
            b[off + 1] = uint8_t(v >> 16);
            b[off + 2] = uint8_t(v >> 8);
            b[off + 3] = uint8_t(v);
        }
        return b;
    };
    for (const activity::LobbyField& f : activity::kLobbyFields) {
        activity::FieldValue v;
        v.index = f.index;
        v.addr = sl.goal + f.offset;
        v.value.assign(activity::kFieldValueBytes, 0);
        if (f.index == 3) {
            v.has = true;
            v.value = words({{0, 0x0110A3F2}, {4, 0x88C10045}, {0x10, 0xAC613874}, {0x14, 0x1D4D0000}, {0x18, 0x1D4D0000}});
        } else if (f.index == 7) {
            v.has = true;
            v.value = words({{0, 0x000F0000}, {4, 1}, {8, 0x00012}});    
        } else if (f.index == 8) {
            v.has = true;
            v.value = words({{0x40, 0}, {0x44, 0x0000000F}, {0x48, 0x0000000F}});
        }
        sl.fields.push_back(std::move(v));
    }
    activity::SessionSlot& s1 = s.read.slots[1];
    s1.ok = true;
    s1.slot = 1;
    s1.gate = 0;
    s1.why = "slot 1: no goal data (gate 0, the game uses 4..10)";
}

void drawLobbyFields(Ui& ui) {
    LobbyState& s = lobby();
    seedLobbyDemo();
    {
        auto off = requireConnection(ui);
        if (button("Read Lobby Fields", {.icon = icon::Refresh, .busy = ui.busy("Read lobby fields")}))
            ui.post("Read lobby fields", [](Ctx& c) {
                c.require(activity::kDescriptorFingerprints, "Lobby fields");
                world::RuntimeReader mem(c.rt());
                activity::SessionRead r;
                std::string err;
                try {
                    r = activity::readSessionFields(mem);
                } catch (const std::exception& e) {
                    err = e.what();
                }
                LobbyState& st = lobby();
                std::lock_guard<std::mutex> lock(st.m);
                st.have = err.empty();
                st.read = r;
                st.error = err;
            });
        tooltip("Reads the replicated world-controller-goal-data fields directly (no hook). Shows only the latest value of each; click a value "
                "to copy it.");
    }
    gap(sp::M);
    activity::SessionRead r;
    std::string err;
    bool have;
    {
        std::lock_guard<std::mutex> lock(s.m);
        r = s.read;
        err = s.error;
        have = s.have;
    }
    if (!have) {
        caption(err.empty() ? "Read the fields while in a lobby or activity." : err.c_str(), err.empty() ? pal::Faint : pal::Warning);
        return;
    }
    if (!r.ok) {
        pill(r.why.c_str(), pal::Warning, icon::Info);
        return;
    }
    for (int slot = 0; slot < 2; ++slot) {
        const activity::SessionSlot& sl = r.slots[slot];
        if (!sl.ok) continue;
        char hdr[64];
        std::snprintf(hdr, sizeof hdr, "SLOT %d  (sub %u, gate %u)", slot, sl.index, sl.gate);
        fieldLabel(hdr);
        gap(sp::XS);
        if (!sl.active()) {
            caption(sl.why.c_str(), pal::Faint);
            gap(sp::S);
            continue;
        }
        int shown = 0;
        for (const activity::FieldValue& f : sl.fields) {
            if (!f.has) continue;
            ++shown;
            const activity::LobbyField* lf = activity::lobbyField(f.index);
            const std::string text = activity::describeFieldValue(f.index, f.value);
            ImGui::PushID(f.index);
            label(lf ? lf->name : "?");
            ImGui::SameLine(0, px(8));
            copyable(text.c_str(), lf ? lf->name : "field", pal::Muted);
            ImGui::PopID();
        }
        if (!shown) caption("No fields have a value in this slot.", pal::Faint);
        gap(sp::S);
    }
}

 
 
 

constexpr const char* kClientLoop = "activity.client";

struct ClientState {
    std::mutex m;
    bool have = false;
    activity::ClientProbe probe;
    std::string error;
    std::deque<std::pair<double, std::string>> log;    
    std::string lastKey;
    std::atomic<bool> live{false};
};
ClientState& client() {
    static ClientState s;
    return s;
}

void storeProbe(const activity::ClientProbe& p, const std::string& err, bool logChanges) {
    ClientState& st = client();
    std::lock_guard<std::mutex> lock(st.m);
    st.have = err.empty();
    st.error = err;
    if (!err.empty()) return;
    const std::string key = p.key();
    if (logChanges && key != st.lastKey) {
        const double now = ui::detail::activityClock();
        std::vector<std::string> lines;
        lines.push_back("chk " + hx(p.chk) + (p.ok ? "" : "  (" + p.why + ")"));
        for (const activity::ClientSlot& sl : p.slots) lines.push_back(activity::describeSlot(sl));
        for (auto it = lines.rbegin(); it != lines.rend(); ++it) st.log.push_front({now, *it});
        while (st.log.size() > 300) st.log.pop_back();
    }
    st.lastKey = key;
     
    activity::ClientProbe merged = p;
    if (logChanges)
        for (activity::ClientSlot& sl : merged.slots)
            for (const activity::ClientSlot& old : st.probe.slots)
                if (std::string(old.name) == sl.name && old.connHandle == sl.connHandle && old.aclHandle == sl.aclHandle) sl.detail = old.detail;
    st.probe = std::move(merged);
}

void runClientProbe(Ctx& c, bool details, bool logChanges) {
    c.require(activity::kClientFingerprints, "Activity client probe");
    world::RuntimeReader mem(c.rt());
    activity::ClientProbe p;
    std::string err;
    try {
        p = activity::readClientProbe(mem, details);
    } catch (const std::exception& e) {
        err = e.what();
    }
    storeProbe(p, err, logChanges);
}

void setClientLive(Ui& ui, bool on) {
    client().live = on;
    if (on)
        ui.post("Watch activity client", [](Ctx& c) {
            c.startLoop(kClientLoop, 100, [](Ctx& lc) { runClientProbe(lc, false, true); });
        });
    else ui.post("Stop watching activity client", [](Ctx& c) { c.stopLoop(kClientLoop); });
}

std::string clientReport() {
    ClientState& s = client();
    std::lock_guard<std::mutex> lock(s.m);
    const activity::ClientProbe& p = s.probe;
    std::string r = "Activity client probe\nrecord base " + hx(p.recordBase) + " chk[268/26C/270] " + hx(p.chk268) + "/" + hx(p.chk26C) + "/" + hx(p.chk) +
                    " clock " + hx(p.clock) + "\n";
    if (!p.why.empty()) r += p.why + "\n";
    for (const activity::ClientSlot& sl : p.slots) {
        r += activity::describeSlot(sl) + "\n";
        if (sl.detail.read) r += "  checksum: " + sl.detail.checksum + "\n  bubbles: " + sl.detail.bubbles + "\n";
    }
    for (auto it = s.log.rbegin(); it != s.log.rend(); ++it) {
        char t[24];
        std::snprintf(t, sizeof t, "%8.2f ", it->first);
        r += t + it->second + "\n";
    }
    return r;
}

void seedClientDemo() {
    static bool done = false;
    if (done || !uiDemo()) return;
    done = true;
    ClientState& s = client();
    std::lock_guard<std::mutex> lock(s.m);
    s.have = true;
    s.probe.ok = true;
    s.probe.recordBase = 0x3073DB40;
    s.probe.slotTable = 0x3073C210;
    s.probe.chk = 0x8108E01C;
    activity::ClientSlot a;
    a.name = "CURRENT-PRIVATE";
    a.recordIndex = 0;
    a.astat = 8;
    a.connHandle = 0x2A010003;
    a.aclHandle = 0x2B010001;
    a.aclResolved = true;
    a.aclValid = true;
    a.peer.ok = true;
    a.peer.peerIndex = 0;
    a.peer.handle = 0;
    a.peer.state = 8;
    a.peer.ladder = 3;
    a.peer.endpoint = "172.97.56.116:7501";
    a.detail.read = true;
    a.detail.checksumOk = true;
    a.detail.checksum = "MATCH (left 5A11C0DE, right 5A11C0DE, widx 0)";
    a.detail.bubblesOk = true;
    a.detail.bubbles = "COUNT OK (3)";
    s.probe.slots.push_back(a);
    activity::ClientSlot b;
    b.name = "TARGET-PUBLIC";
    b.recordIndex = 2;
    b.astat = 33;
    b.connHandle = 0x2A010007;
    b.aclHandle = 0x2B010004;
    b.aclResolved = true;
    b.aclVerdict20 = 0xFFFFFFFF;
    b.peer.ok = true;
    b.peer.peerIndex = 1;
    b.peer.handle = 1;
    b.peer.state = 9;
    b.peer.reason = 11;
    b.peer.ladder = 5;
    b.peer.endpoint = "10.0.0.42:7501";
    b.detail.read = true;
    b.detail.checksum = "CHECKSUM MISMATCH (left 00000000, right 5A11C0DE, widx 1)";
    b.detail.bubbles = "INVALID HOST ACTIVITY STATE (have 0, want 3)";
    s.probe.slots.push_back(b);
    s.log.push_front({-2, "TARGET-PUBLIC idx2 ASTAT=33 comp=0 aclflag=1 conn=2A010007 acl=2B010004 -> rejected; BAP _dead (ladder 5 NOT READY)"});
    s.log.push_front({0, "CURRENT-PRIVATE idx0 ASTAT=8 comp=0 aclflag=1 conn=2A010003 acl=2B010001 -> validated; BAP _connected (ladder 3 READY)"});
}

void drawClientProbe(Ui& ui) {
    ClientState& s = client();
    seedClientDemo();
    {
        auto off = requireConnection(ui);
        if (button("Run Probe", {.tone = Tone::Primary, .icon = icon::Probe, .busy = ui.busy("Activity Client Probe")}))
            ui.post("Activity Client Probe", [](Ctx& c) { runClientProbe(c, true, false); });
        tooltip("Read-only: the connection / activity-client slots, the two validators' verdicts (checksum, bubbles) and the BAP peer behind "
                "each slot (state, failure reason, endpoint). Mirrors the Xbox actchk.py.");
        ImGui::SameLine(0, px(8));
        bool live = s.live.load();
        if (toggleSwitch("##acllive", &live)) setClientLive(ui, live);
        tooltip("Sample the slots continuously and log every change (actchk.py watch); the in-world window is only ~3 s wide");
        ImGui::SameLine(0, px(8));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(7));
        caption(live ? "Watching" : "Watch Off", live ? pal::Text : pal::Muted);
    }
    ImGui::SameLine();
    alignRight(buttonWidth("Copy", icon::Copy, true));
    if (button("Copy##aclrep", {.tone = Tone::Ghost, .icon = icon::Copy, .small = true, .tooltip = "Copy the probe and the change log"}))
        copyToClipboard(clientReport(), "activity client report");
    gap(sp::M);
    activity::ClientProbe p;
    std::string err;
    bool have;
    std::deque<std::pair<double, std::string>> log;
    {
        std::lock_guard<std::mutex> lock(s.m);
        p = s.probe;
        err = s.error;
        have = s.have;
        log = s.log;
    }
    if (s.live.load()) requestRedraw();
    if (!have) {
        caption(err.empty() ? "Run the probe while in an activity or matchmaking." : err.c_str(), err.empty() ? pal::Faint : pal::Warning);
        return;
    }
    if (!p.ok) {
        pill(p.why.c_str(), pal::Warning, icon::Info);
        return;
    }
    {
        fieldLabel("ACTIVITY CHECKSUM TAG");
        ImGui::SameLine(0, px(8));
        hashLabel(p.chk, "checksum tag", p.chk == 0xFFFFFFFFu ? pal::Muted : pal::Accent);
        const std::string tagName = p.chk != 0xFFFFFFFFu ? names::tagName(p.chk) : std::string("none: nothing to check");
        if (!tagName.empty()) {
            ImGui::SameLine(0, px(8));
            caption(tagName.c_str(), pal::Faint);
        }
    }
    gap(sp::S);
    if (p.slots.empty()) caption(p.why.c_str(), pal::Faint);
    for (const activity::ClientSlot& sl : p.slots) {
        ImGui::PushID(sl.name);
        beginCard(sl.name, sl.name, {.raised = true});
        const char* v = sl.verdict();
        const std::string vs = v;
        pill(v, vs == "validated" ? pal::Success : vs == "rejected" ? pal::Danger : pal::Muted);
        ImGui::SameLine(0, px(10));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(4));
        monof("idx %d   ASTAT %d   comp %u   aclflag %u   conn %s   acl %s", sl.recordIndex, sl.astat, sl.comp, sl.aclFlag, hx(sl.connHandle).c_str(),
              hx(sl.aclHandle).c_str());
        if (sl.peer.ok) {
            const char* st = activity::bapStateName(sl.peer.state);
            gap(sp::XS);
            labelf("BAP %s  (ladder %d)", st ? st : "?", sl.peer.ladder);
            ImGui::SameLine(0, px(8));
            pill(sl.peer.ready() ? "ready" : "not ready: ASTAT 33", sl.peer.ready() ? pal::Success : pal::Warning);
            if (sl.peer.reason) {
                const char* rn = activity::bapReasonName(sl.peer.reason);
                gap(sp::XS);
                caption((std::string("failure reason ") + (rn ? rn : "?") + " (" + std::to_string(sl.peer.reason) + ")").c_str(), pal::Danger);
            }
            if (!sl.peer.endpoint.empty()) {
                gap(sp::XS);
                caption("Endpoint", pal::Faint);
                tooltip("Which server this activity talks to");
                ImGui::SameLine(0, px(6));
                copyable(sl.peer.endpoint.c_str(), "endpoint", pal::Text);
            }
        }
        if (sl.detail.read) {
            gap(sp::XS);
            caption(("checksum (A): " + sl.detail.checksum).c_str(), sl.detail.checksumOk ? pal::Muted : pal::Warning);
            gap(sp::XS);
            caption(("bubbles (B): " + sl.detail.bubbles).c_str(), sl.detail.bubblesOk ? pal::Muted : pal::Warning);
        }
        endCard();
        gap(sp::S);
        ImGui::PopID();
    }
    if (!log.empty()) {
        char hdr[48];
        std::snprintf(hdr, sizeof hdr, "CHANGES  (%zu)", log.size());
        fieldLabel(hdr);
        ImGui::SameLine();
        alignRight(buttonWidth("Clear", icon::Delete, true));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(8));
        if (button("Clear##acllog", {.tone = Tone::Ghost, .icon = icon::Delete, .small = true})) {
            std::lock_guard<std::mutex> lock(s.m);
            s.log.clear();
        }
        gap(sp::XS);
        if (beginTable("##acllog", 2, 200, ImGuiTableFlags_BordersOuter)) {
            ImGui::TableSetupColumn("At", ImGuiTableColumnFlags_WidthFixed, px(52));
            ImGui::TableSetupColumn("Change", ImGuiTableColumnFlags_WidthStretch);
            tableHeaders();
            const double t0 = log.front().first;
            for (const auto& [t, text] : log) {
                ImGui::TableNextRow(0, px(26));
                ImGui::TableSetColumnIndex(0);
                char when[24];
                std::snprintf(when, sizeof when, "-%.1fs", std::max(0.0, t0 - t));
                cellText(when, pal::Faint, FontKind::Body, type::Small, 26);
                ImGui::TableSetColumnIndex(1);
                cellText(text.c_str(), text.find("NOT READY") != std::string::npos || text.find("rejected") != std::string::npos ? pal::Warning : pal::Text,
                         FontKind::Mono, type::MonoSmall, 26);
                if (ImGui::IsItemHovered()) tooltip(text.c_str());
            }
            endTable();
        }
    }
}

 
 
 

constexpr const char* kScanLoop = "activity.mirror_scan";
constexpr const char* kRangesKey = "activity.mirror_ranges";

struct DeployState {
    std::mutex m;
    activity::MirrorScan scans[2];       
    bool have[2] = {};
    std::string status;
    std::atomic<bool> scanning{false}, stop{false};
    std::atomic<int> target{-1};         
    std::atomic<float> progress{0};
     
    int scanId = 0;                      
    int chosenId = 15;
    bool init = false;
    std::string ranges;                  
    std::string rangesError;
};
DeployState& deploy() {
    static DeployState s;
    return s;
}

void startScan(Ctx& c, int slot, int chosenId, std::vector<activity::HeapRange> ranges) {
    DeployState& s = deploy();
    if (s.scanning.exchange(true)) throw Error("a scan is already running");
    s.stop = false;    
    s.target = slot;
    s.progress = 0;
    trackActivity("activity.scan", std::string("Scanning for mirrors (") + (slot == 0 ? "A" : "B") + ")", Page::Console,
                  [] { return deploy().scanning.load(); }, [] { deploy().stop = true; }, pal::Accent);
    c.startLoop(kScanLoop, 1, [slot, chosenId, ranges](Ctx& lc) {
        DeployState& t = deploy();
        struct Done {
            ~Done() {
                deploy().scanning = false;
                deploy().stop = false;
            }
        } done;
        try {
            lc.require(activity::kDescriptorFingerprints, "Mirror finder");
            world::RuntimeReader mem(lc.rt());
            uint32_t id = uint32_t(chosenId);
            if (chosenId < 0) {
                const activity::DescriptorRead d = activity::readCurrentActivity(mem);
                if (!d.ok) throw Error("no current activity to scan for (" + d.why + "); enter an activity or scan for a specific id");
                id = d.id;
            }
            const activity::MirrorScan scan = activity::scanMirrors(
                mem, id, ranges, [](uint64_t a, uint64_t b) { deploy().progress = b ? float(double(a) / double(b)) : 1.0f; },
                [&lc] { return lc.cancelled() || deploy().stop.load(); });
             
            try {
                std::ofstream f(activityDir() / (slot == 0 ? "mirror_a.json" : "mirror_b.json"), std::ios::binary);
                f << activity::toJson(scan);
            } catch (...) {
            }
            {
                std::lock_guard<std::mutex> lock(t.m);
                t.scans[slot] = scan;
                t.have[slot] = true;
                t.status = (scan.cancelled ? "stopped early: " : "") + std::string("scan ") + (slot == 0 ? "A" : "B") + ": " +
                           std::to_string(scan.hits.size()) + " hits for id " + std::to_string(id);
            }
            lc.toast(t.status);
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(t.m);
            t.status = std::string("scan failed: ") + e.what();
            throw;
        }
        lc.stopLoop(kScanLoop);    
    });
}

void loadSavedScans() {
    DeployState& s = deploy();
    for (int slot = 0; slot < 2; ++slot) {
        try {
            std::ifstream f(activityDir() / (slot == 0 ? "mirror_a.json" : "mirror_b.json"), std::ios::binary);
            if (!f) continue;
            std::stringstream ss;
            ss << f.rdbuf();
            std::lock_guard<std::mutex> lock(s.m);
            s.scans[slot] = activity::fromJson(ss.str());
            s.have[slot] = true;
        } catch (...) {
        }
    }
}

void seedDeployDemo() {
    if (!uiDemo()) return;
    DeployState& s = deploy();
    auto mk = [](uint32_t id, const char* name, std::vector<uint32_t> addrs, bool fnvFirst) {
        activity::MirrorScan sc;
        sc.id = id;
        sc.activity = name;
        sc.state = 36;
        sc.root = 0x30650150;
        for (size_t i = 0; i < addrs.size(); ++i) {
            activity::MirrorHit h;
            h.addr = addrs[i];
            h.tailZero = true;
            h.fnvBefore = fnvFirst && i == 0;
            sc.hits.push_back(h);
        }
        return sc;
    };
    std::lock_guard<std::mutex> lock(s.m);
    s.scans[0] = mk(15, "moon_raid_1", {0x3065B3A4, 0x3066E9E8, 0x60631450, 0x60781894, 0x604A2210}, true);
    s.scans[1] = mk(27, "patrol_moon", {0x3065B3A4, 0x3066E9E8, 0x60631450, 0x60781894}, true);
    s.have[0] = s.have[1] = true;
    s.status = "scan B: 4 hits for id 27";
}

void drawDeploy(Ui& ui) {
    DeployState& s = deploy();
    if (!s.init) {
        s.init = true;
        loadSavedScans();
        seedDeployDemo();
        s.ranges = ui.settings().str(kRangesKey, activity::rangesText(activity::kDefaultScanRanges));
    }
    const bool scanning = s.scanning.load();
    const int tgt = s.target.load();
    std::vector<activity::HeapRange> ranges;
    {
        DisabledScope busy(scanning, "Scanning: stop it first");
        fieldLabel("SEARCH FOR");
        gap(sp::XS);
        const char* const modes[] = {"Current Activity ID", "Specific ID"};
        combo("##scanmode", &s.scanId, modes, 2, 180);
        if (s.scanId == 1) {
            ImGui::SameLine(0, px(8));
            inputInt("##scanid", &s.chosenId, 1, 0xFFFF, 120);
            tooltip("Activity id (the deploy catalogue's ids, 1..337 for the Xbox list)");
        }
        gap(sp::S);
        fieldLabel("HEAP RANGES  (hex LO-HI)");
        gap(sp::XS);
        const float rw = std::min(px(420), availWidth() - buttonWidth("Default", icon::Undo, true) - px(8));
        if (inputMono("##ranges", &s.ranges, "30000000-31000000 60000000-60800000", rw / scale())) ui.settings().set(std::string(kRangesKey), s.ranges);
        ImGui::SameLine(0, px(8));
        if (button("Default", {.tone = Tone::Ghost, .icon = icon::Undo, .small = true, .tooltip = "The windows seen live (session root, pools)"})) {
            s.ranges = activity::rangesText(activity::kDefaultScanRanges);
            ui.settings().set(std::string(kRangesKey), s.ranges);
        }
        try {
            ranges = activity::parseRanges(s.ranges);
            s.rangesError.clear();
            uint64_t total = 0;
            for (const activity::HeapRange& r : ranges) total += r.hi - r.lo;
            gap(sp::XS);
            captionf("%.1f MiB, about %llu reads (~%.0f s)", double(total) / 1048576.0, static_cast<unsigned long long>(total / activity::kScanChunk),
                     double(total / activity::kScanChunk) * 0.06);
        } catch (const std::exception& e) {
            s.rangesError = e.what();
            gap(sp::XS);
            caption(s.rangesError.c_str(), pal::Danger);
        }
    }
    gap(sp::S);
    {
        auto off = requireConnection(ui);
        DisabledScope busy(scanning, "Scanning: stop it first");
        DisabledScope bad(ranges.empty(), "Fix the heap ranges first");
        for (int slot = 0; slot < 2; ++slot) {
            if (slot) ImGui::SameLine(0, px(8));
            const std::string lbl = std::string(slot == 0 ? "Scan A" : "Scan B");
            if (button(lbl.c_str(), {.tone = slot == 0 ? Tone::Primary : Tone::Secondary, .icon = icon::Search,
                                     .tooltip = slot == 0 ? "Scan in the first activity. Activity deploy wrote the launched id into four loader "
                                                            "heap words with no code reference on PS3, so they must be found live, once per "
                                                            "boot: this read-only finder scans the heap for the record {id, 0x01000000}. Scan "
                                                            "two different activities; addresses present in both are the mirror candidates. "
                                                            "Re-check after a reboot before trusting them."
                                                          : "Scan again in a different activity"})) {
                const int chosen = s.scanId == 0 ? -1 : s.chosenId;
                noteQueued("Scan mirrors");
                ui.post("Scan mirrors", [slot, chosen, ranges](Ctx& c) { startScan(c, slot, chosen, ranges); });
            }
        }
    }
    if (scanning) {
        ImGui::SameLine(0, px(8));
        if (button("Stop", {.tone = Tone::Danger, .icon = icon::Stop, .spinner = true})) s.stop = true;
        gap(sp::S);
        progressBar(s.progress.load(), 0, 5);
        gap(sp::XS);
        captionf("Scanning %s... %.0f%%", tgt == 0 ? "A" : "B", s.progress.load() * 100.0);
        requestRedraw();
    }
    std::string status;
    activity::MirrorScan scans[2];
    bool have[2];
    {
        std::lock_guard<std::mutex> lock(s.m);
        status = s.status;
        for (int i = 0; i < 2; ++i) {
            scans[i] = s.scans[i];
            have[i] = s.have[i];
        }
    }
    if (!status.empty() && !scanning) {
        gap(sp::S);
        caption(status.c_str(), status.rfind("scan failed", 0) == 0 ? pal::Danger : pal::Muted);
    }
    gap(sp::M);
    for (int slot = 0; slot < 2; ++slot) {
        if (!have[slot]) continue;
        const activity::MirrorScan& sc = scans[slot];
        char hdr[128];
        std::snprintf(hdr, sizeof hdr, "SCAN %s  \xC2\xB7  id %u%s%s  \xC2\xB7  %zu hits%s", slot == 0 ? "A" : "B", sc.id, sc.activity.empty() ? "" : "  ",
                      sc.activity.c_str(), sc.hits.size(), sc.cancelled ? "  (stopped early)" : "");
        fieldLabel(hdr);
        gap(sp::XS);
        if (sc.hits.empty()) {
            caption("No records matched. Scan while in an activity, or add heap ranges above.", pal::Faint);
            gap(sp::S);
            continue;
        }
        if (beginTable((std::string("##scan") + std::to_string(slot)).c_str(), 3, std::min(160.0f, 34.0f + 26.0f * float(sc.hits.size())),
                       ImGuiTableFlags_BordersOuter)) {
            ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, px(110));
            ImGui::TableSetupColumn("Flags", ImGuiTableColumnFlags_WidthFixed, px(170));
            ImGui::TableSetupColumn("Context", ImGuiTableColumnFlags_WidthStretch);
            tableHeaders();
            for (size_t i = 0; i < sc.hits.size(); ++i) {
                const activity::MirrorHit& h = sc.hits[i];
                ImGui::PushID(int(i));
                ImGui::TableNextRow(0, px(26));
                ImGui::TableSetColumnIndex(0);
                const ImVec2 cp = ImGui::GetCursorScreenPos();
                cellText(hx(h.addr).c_str(), pal::Accent, FontKind::Mono, type::MonoSmall, 26);
                ImGui::SetCursorScreenPos(ImVec2(cp.x, cp.y - ImGui::GetStyle().CellPadding.y));
                ImGui::InvisibleButton("##copy", ImVec2(std::max(px(1), ImGui::GetContentRegionAvail().x), px(26)));
                if (ImGui::IsItemHovered()) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    tooltip("Click to copy");
                }
                if (ImGui::IsItemClicked()) copyToClipboard(hx(h.addr), "mirror address");
                ImGui::TableSetColumnIndex(1);
                std::string flags;
                if (h.fnvBefore) flags += "FNV ";
                if (h.tailZero) flags += "tail0 ";
                if (h.inSession) flags += "in-session ";
                cellText(flags.empty() ? "-" : flags.c_str(), pal::Muted, FontKind::Body, type::Small, 26);
                ImGui::TableSetColumnIndex(2);
                cellText(h.context.empty() ? "-" : h.context.c_str(), pal::Faint, FontKind::Mono, type::MonoSmall, 26);
                ImGui::PopID();
            }
            endTable();
        }
        gap(sp::S);
    }
    if (have[0] && have[1]) {
        std::vector<uint32_t> cand = activity::intersect(scans[0], scans[1]);
        divider(sp::S);
        fieldLabel("MIRROR CANDIDATES  (in both scans)");
        gap(sp::XS);
        if (scans[0].id == scans[1].id) caption("Both scans searched the same id: scan B in a different activity for a real comparison.", pal::Warning);
        if (cand.empty()) {
            caption("No addresses were present in both scans. Scan two different activities (different ids).", pal::Faint);
        } else {
            captionf("%zu address%s carried both ids (expect 4 or more):", cand.size(), cand.size() == 1 ? "" : "es");
            gap(sp::XS);
            std::string all;
            for (uint32_t a : cand) {
                ImGui::PushID(int(a));
                copyable(hx(a).c_str(), "mirror address", pal::Accent);
                ImGui::PopID();
                all += hx(a) + "\n";
            }
            gap(sp::XS);
            if (button("Copy All", {.tone = Tone::Ghost, .icon = icon::Copy, .small = true})) copyToClipboard(all, "mirror candidates");
        }
    }
}

 
 
 

struct FactionState {
    std::mutex m;
    bool have = false;
    bool owned = false;
    std::string proof;
    uint32_t body = 0;
    unsigned count = 0;
    std::array<uint8_t, 64> bits{};
    std::vector<activity::AllegianceEdit> edits;
    std::string error;
    int a = 0, b = 1;                    
};
FactionState& fac() {
    static FactionState s;
    return s;
}

 
void publishAllegiance(Ctx& c) {
    const world::Allegiance a = activity::readAllegiance(c);
    const std::vector<activity::AllegianceEdit> edits = activity::journalEdits(c);
    FactionState& s = fac();
    std::lock_guard<std::mutex> lock(s.m);
    s.have = true;
    s.owned = a.owned();
    s.proof = a.proof;
    s.body = a.body;
    s.bits = a.bits;
    s.count = world::allegianceCount(a.bits);
    s.edits = edits;
    s.error.clear();
}

void factionJob(Ui& ui, const std::string& label, std::function<void(Ctx&)> fn) {
    ui.post(
        label,
        [fn](Ctx& c) {
            if (fn) fn(c);
            publishAllegiance(c);
        },
        [](bool ok, const std::string& err) {
            if (ok) return;
            FactionState& s = fac();
            std::lock_guard<std::mutex> lock(s.m);
            s.error = err;
        });
}

void seedFactionsDemo() {
    static bool done = false;
    if (done || !uiDemo()) return;
    done = true;
    FactionState& s = fac();
    std::lock_guard<std::mutex> lock(s.m);
    s.have = true;
    s.owned = true;
    s.body = 0x600A8610;
    s.a = 1;
    s.b = 6;
    for (int a = 0; a < 32; ++a)
        for (int b = a + 1; b < 32; ++b)
            if ((a * 7 + b * 3) % 4 == 0) world::setAllegiancePair(s.bits, a, b, true);
    world::setAllegiancePair(s.bits, 1, 6, true);
    s.count = world::allegianceCount(s.bits);
    activity::AllegianceEdit e1;
    e1.a = 1;
    e1.b = 6;
    e1.before = false;
    e1.after = true;
    e1.body = 0x600A8610;
    s.edits.push_back(e1);
    activity::AllegianceEdit e2;
    e2.a = 3;
    e2.b = 9;
    e2.before = true;
    e2.after = false;
    e2.body = 0x600A8610;
    s.edits.push_back(e2);
}

void drawFactions(Ui& ui) {
    FactionState& s = fac();
    seedFactionsDemo();
    {
        auto off = requireConnection(ui);
        if (button("Read Allegiance", {.icon = icon::Refresh, .busy = ui.busy("Read allegiance")})) factionJob(ui, "Read allegiance", {});
        tooltip("Change the relationship between two faction IDs. It affects every object using that pair; it does not create an AI actor or "
                "assign an object to a faction. Each write is journalled and restored on disconnect and Panic.");
    }
    gap(sp::M);
    bool have, owned;
    unsigned count;
    uint32_t body;
    std::array<uint8_t, 64> bits;
    std::vector<activity::AllegianceEdit> edits;
    std::string error, proof;
    {
        std::lock_guard<std::mutex> lock(s.m);
        have = s.have;
        owned = s.owned;
        count = s.count;
        body = s.body;
        bits = s.bits;
        edits = s.edits;
        error = s.error;
        proof = s.proof;
    }
    if (!error.empty()) {
        pill(error.c_str(), pal::Warning, icon::Info);
        gap(sp::S);
    }
    if (!have) {
        caption("Read the allegiance block while in an activity (it needs an owned world).", pal::Faint);
        return;
    }
    if (!owned) {
        pill(("ownership not proven: " + proof).c_str(), pal::Danger, icon::Warning);
        return;
    }
    captionf("%u / 496 distinct pairs allied", count);
    ImGui::SameLine(0, px(8));
    caption(("block " + hx(body)).c_str(), pal::Faint);
    gap(sp::S);
    ImGui::BeginGroup();
    fieldLabel("FACTION A");
    gap(sp::XS);
    inputInt("##fa", &s.a, 0, 31, 110);
    ImGui::EndGroup();
    ImGui::SameLine(0, px(16));
    ImGui::BeginGroup();
    fieldLabel("FACTION B");
    gap(sp::XS);
    inputInt("##fb", &s.b, 0, 31, 110);
    ImGui::EndGroup();
    const int a = s.a, b = s.b;
    gap(sp::S);
    if (a == b) {
        caption("Same faction: always allied, no bit consulted.", pal::Faint);
    } else {
        const bool allied = world::allegiancePair(bits, a, b);
        labelf("Faction %d + faction %d:", a, b);
        ImGui::SameLine(0, px(8));
        pill(allied ? "allied" : "hostile", allied ? pal::Success : pal::Danger);
        const int bit = world::allegianceBit(a, b);
        ImGui::SameLine(0, px(8));
        captionf("bit %d, byte +0x%X, mask 0x%02X", bit, world::allegianceByte(unsigned(bit)), 1u << (unsigned(bit) % 8));
        gap(sp::S);
        auto off = requireConnection(ui);
        const bool busy = ui.busy("Set allegiance");
        if (!allied) {
            if (button("Make Allied", {.tone = Tone::Primary, .icon = icon::Check, .busy = busy}))
                factionJob(ui, "Set allegiance", [a, b](Ctx& c) { activity::setPair(c, a, b, true); });
        } else {
            if (button("Make Hostile", {.tone = Tone::Danger, .icon = icon::Close, .busy = busy}))
                factionJob(ui, "Set allegiance", [a, b](Ctx& c) { activity::setPair(c, a, b, false); });
        }
    }
     
    gap(sp::M);
    fieldLabel("WHOLE TABLE");
    gap(sp::XS);
    {
        auto off = requireConnection(ui);
        const bool busy = ui.busy("Fill allegiance");
        if (button("All Allied", {.tone = Tone::Secondary, .icon = icon::Check, .small = true, .busy = busy}))
            confirm("Make Every Faction Pair Allied?", "All 496 faction pairs become allied: enemies stop being hostile everywhere. Journalled; restored on "
                    "disconnect and Panic, or with Restore below.",
                    "All Allied", [&ui] { factionJob(ui, "Fill allegiance", [](Ctx& c) { activity::fillAll(c, true); }); });
        ImGui::SameLine(0, px(8));
        if (button("All Hostile", {.tone = Tone::Secondary, .icon = icon::Close, .small = true, .busy = busy}))
            confirm("Make Every Faction Pair Hostile?", "All 496 faction pairs become hostile: allies and neutral factions turn on each other. Journalled; "
                    "restored on disconnect and Panic, or with Restore below.",
                    "All Hostile", [&ui] { factionJob(ui, "Fill allegiance", [](Ctx& c) { activity::fillAll(c, false); }); });
    }
    if (!edits.empty()) {
        divider(sp::M);
        fieldLabel("SESSION EDITS  (newest first)");
        gap(sp::XS);
        for (size_t i = edits.size(); i > 0; --i) {
            const activity::AllegianceEdit& e = edits[i - 1];
            ImGui::PushID(int(i));
            label(e.text().c_str());
            ImGui::SameLine();
            alignRight(buttonWidth("Restore", icon::Undo, true));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(2));
            auto off = requireConnection(ui);
            if (button("Restore", {.tone = Tone::Ghost, .icon = icon::Undo, .small = true, .tooltip = "Put back the original relationship (other pairs are kept)"})) {
                const size_t idx = i - 1;
                factionJob(ui, "Restore allegiance", [idx](Ctx& c) { activity::restoreEdit(c, idx); });
            }
            ImGui::PopID();
        }
    }
}

 
 
 

constexpr const char* kProbeSection = "Activity";

void probeMonitor(Ctx& c) {
    c.require(activity::kMonitorFingerprints, "World-state monitor");
    c.require(activity::kDescriptorFingerprints, "Current activity (destination descriptor)");
    world::RuntimeReader mem(c.rt());
    const activity::WorldSnapshot w = activity::readWorldSnapshot(mem);
    c.log("controller " + hx(w.controller) + " state " + activity::stateLabel(w.state) + ", requested " + activity::stateLabel(w.requested) +
          ", request parameter " + activity::abortReasonLabel(w.reason) + (w.aborting() ? " (ABORT to cleanup)" : ""));
    c.log("scenario " + hx(w.scenario) + ", transition target \"" + w.target + "\", tag loader " + (w.loaderBusy() ? "busy" : "idle") +
          ", sObjects (pool +0x14, likely) " + std::to_string(w.sobjectCount) + ", clock " + std::to_string(w.clock));
    c.log("active activity name \"" + w.activeName + "\"");
    const activity::DescriptorRead d = activity::readCurrentActivity(mem);
    if (d.ok)
        c.log("current activity id " + std::to_string(d.id) + " (override " + std::to_string(d.overrideId) + ") \"" + d.name + "\", descriptor " + hx(d.desc) +
              ", FNV before " + hx(d.fnvBefore) + (d.fnvBefore == activity::kFnvBasis ? " (expected)" : " (NOT the 811C9DC5 basis: layout differs)"));
    else c.log("current activity: " + d.why);
    c.log("expected in an activity: state 36 activity:in_world, a small live sObject count, a descriptor with FNV 811C9DC5 and the activity name");
    c.log(w.controllerOk ? "verdict: world-state monitor reads OK" : "verdict: world controller not readable (in a world?)");
}

void probeSession(Ctx& c) {
    c.require(activity::kDescriptorFingerprints, "Lobby fields");
    world::RuntimeReader mem(c.rt());
    const activity::SessionRead r = activity::readSessionFields(mem);
    if (!r.ok) {
        c.log("session root " + hx(r.root) + ": " + r.why);
        c.log("verdict: no session goal data (not in a lobby/activity yet)");
        return;
    }
    for (int slot = 0; slot < 2; ++slot) {
        const activity::SessionSlot& sl = r.slots[slot];
        if (!sl.ok) {
            c.log("slot " + std::to_string(slot) + ": " + sl.why);
            continue;
        }
        int withValue = 0;
        for (const activity::FieldValue& f : sl.fields)
            if (f.has) ++withValue;
        c.log("slot " + std::to_string(slot) + " sub " + std::to_string(sl.index) + " goal " + hx(sl.goal) + ", gate " + std::to_string(sl.gate) +
              (sl.active() ? " (active)" : " (no goal data)") + ", " + std::to_string(withValue) + " fields set");
        for (const activity::FieldValue& f : sl.fields)
            if (f.has) {
                const activity::LobbyField* lf = activity::lobbyField(f.index);
                c.log("  " + std::string(lf ? lf->name : "?") + ": " + activity::describeFieldValue(f.index, f.value));
            }
    }
    c.log("expected in a live lobby: one slot with gate 4..10 and activity-host / activity / activity-start-status set");
    c.log("verdict: session goal-data fields read (confirm the activity-start-status index matches the activity you loaded)");
}

void probeClient(Ctx& c) {
    c.require(activity::kClientFingerprints, "Activity client probe");
    world::RuntimeReader mem(c.rt());
    const activity::ClientProbe p = activity::readClientProbe(mem, true);
    c.log("record base " + hx(p.recordBase) + ", slot table " + hx(p.slotTable) + ", chk[268/26C/270] " + hx(p.chk268) + "/" + hx(p.chk26C) + "/" +
          hx(p.chk) + ", " + std::to_string(p.slots.size()) + " populated slots");
    for (const activity::ClientSlot& sl : p.slots) {
        c.log(activity::describeSlot(sl));
        if (sl.detail.read) c.log("  checksum: " + sl.detail.checksum + "; bubbles: " + sl.detail.bubbles);
    }
    c.log("expected: a connected BAP peer shows _connected (ladder 3) and an ip:port endpoint; a validated slot shows checksum MATCH and COUNT OK");
    c.log(p.ok ? "verdict: activity client probe read OK" : "verdict: " + p.why);
}

void probeChangeWorld(Ctx& c) {
    const uint32_t orbit = activity_ui::orbitTag(c.settings());
    const activity::ChangeWorldReady r = activity::readChangeWorldReady(c, orbit);
    c.log("controller state " + activity::stateLabel(r.state) + ", activity scenario " + hx(r.scenario) + ", orbit tag setting " + hx(orbit) +
          (r.inOrbit ? " (match)" : " (no match)"));
    c.log("registry " + hx(r.registry) + ", change-world slot state " + std::to_string(r.slotState) + ", dependency (event 11) pending " +
          (r.dependencyPending ? "yes" : "no"));
    c.log(r.scenarioClientOk ? "loot_event-client scenario *(*(0x1A93300)+0x10) (likely) " + hx(r.scenarioClient) +
                                   (r.scenarioClient == r.scenario ? " = the activity scenario" : " != the activity scenario")
                             : std::string("loot_event-client scenario *(*(0x1A93300)+0x10) (likely): not readable"));
    c.log("expected in orbit: state 36, the activity scenario equals the orbit tag (Xbox 80B52003; set it on Console > World if PS3 differs), "
          "slot state 0, no pending dependency, and the loot_event-client scenario equal to it (Xbox CMD 76 required both)");
    c.log(r.ready() ? "verdict: change world is ready (the launch re-checks state, scenario and slot in the call's frame)" : "verdict: not ready: " + r.blocker());
}

void probeMirrorScanInfo(Ctx& c) {
    c.require(activity::kDescriptorFingerprints, "Mirror finder");
    world::RuntimeReader mem(c.rt());
    const activity::DescriptorRead d = activity::readCurrentActivity(mem);
    if (d.ok) c.log("current activity id " + std::to_string(d.id) + " \"" + d.name + "\" (what a scan searches for)");
    else c.log("current activity: " + d.why);
    c.log("session root " + hx(mem.u32(activity::addr::SESSION_ROOT_PTR)));
    c.log("Activity deploy is BLOCKED: run the mirror finder (Console > World > Activity deploy) in two activities, then compare.");
    c.log(d.ok ? "verdict: ready to scan for mirrors in this activity" : "verdict: enter an activity before scanning");
}

}   

void registerActivitySession(Registry& r) {
    for (int k = 0; k < activity::kCaptureKinds; ++k) {
        const activity::CaptureKind kind = activity::CaptureKind(k);
        const activity::CaptureSpec& spec = activity::captureSpec(kind);
        static const char* const kHelp[activity::kCaptureKinds] = {
            "Hook the join-refuse decoder 0x8CCDE4: records the session id, peer id and the refuse reason (the Xbox _join_refuse_reason enum).",
            "Hook the activity-host field decoder 0x913AF0: populated means the server gave a host (the failure is a real connection).",
            "Hook the session-disband-reason decoder 0x917614: fires when the server disbanded the lobby (reason shown raw).",
            "Hook the activity-start-status decoder 0x914E28: the final start status (activity index, flag, 20-bit status).",
        };
        r.toggle({.id = std::string("activity.capture.") + spec.id, .page = Page::Console, .tab = "Session", .section = "Capture hooks",
                  .label = std::string("Capture ") + kCaptureTitles[k], .help = kHelp[k],
                  .set = [kind](Ctx& c, bool on) { on ? armCapture(c, kind) : disarmCapture(c, kind); },
                  .query = [kind](Ctx& c) { return c.rt().hooked(activity::captureSpec(kind).site); }});
    }
    r.section({.id = "activity.captures", .page = Page::Console, .tab = "Session", .section = "Capture hooks", .order = 10, .draw = drawCaptures,
               .onDisconnect = [] {
                   CaptureUi& s = cap();
                   s.live = false;
                   std::lock_guard<std::mutex> lock(s.m);
                   for (int k = 0; k < activity::kCaptureKinds; ++k) s.armed[k] = false;
                   if (!Engine::get().connected()) {    
                       for (int k = 0; k < activity::kCaptureKinds; ++k) s.caps[k] = {};
                       s.have = false;
                   }
               }});
    r.section({.id = "activity.lobby", .page = Page::Console, .tab = "Session", .section = "Lobby fields", .order = 20, .draw = drawLobbyFields});
    r.section({.id = "activity.client", .page = Page::Console, .tab = "Session", .section = "Activity client probe", .order = 30,
               .draw = drawClientProbe, .onDisconnect = [] { client().live = false; }});
    ui::styleSection("Capture hooks", {.fullWidth = true, .icon = ui::icon::Record});
    ui::styleSection("Lobby fields", {.icon = ui::icon::List});
    ui::styleSection("Activity client probe", {.fullWidth = true, .icon = ui::icon::Probe});
}

void registerActivityDeploy(Registry& r) {
    r.section({.id = "activity.deploy", .page = Page::Console, .tab = "World", .section = "Activity deploy", .order = 95, .draw = drawDeploy,
               .onDisconnect = [] {
                    
                   deploy().scanning = false;
                   deploy().stop = false;
               },
               .badge = kBadgeBlocked});
    ui::styleSection("Activity deploy", {.fullWidth = true, .icon = ui::icon::Search});
}

void registerActivityFactions(Registry& r) {
    r.section({.id = "activity.factions", .page = Page::Console, .tab = "World", .section = "Faction allegiance", .order = 80, .draw = drawFactions,
               .onDisconnect = [] {
                   FactionState& s = fac();
                   std::lock_guard<std::mutex> lock(s.m);
                   s.have = false;
                   s.edits.clear();
                   s.error.clear();
               }});
    ui::styleSection("Faction allegiance", {.icon = ui::icon::Shield});
}

void registerActivityPortChecks(Registry& r) {
    auto probe = [&](const char* id, const char* label, const char* help, void (*run)(Ctx&), const char* badge = kBadgeNeedsCheck) {
        r.action(
            {.id = id, .page = Page::Testing, .tab = kPortChecksTab, .section = kProbeSection, .label = label, .help = help, .run = run, .badge = badge});
    };
    probe("activity.check.monitor", "World-State Monitor",
          "Reads the world controller (state, requested, request parameter), the activity scenario, the live sObject count (likely) and the "
          "current activity descriptor. Expected in an activity: state 36, descriptor FNV 811C9DC5 and its name.",
          probeMonitor, "");
    probe("activity.check.session", "Session Goal-Data Fields",
          "Reads the replicated world-controller-goal-data fields (activity-host, activity, start-status...) from the session root. "
          "Expected: one slot with gate 4..10 and values in a live lobby.",
          probeSession);
    probe("activity.check.client", "Activity Client Probe",
          "Reads the connection / activity-client slots, both validators' inputs and the BAP peer behind them. Expected: a connected "
          "peer shows _connected and an endpoint.",
          probeClient, "");
    probe("activity.check.change_world", "Change-World Readiness",
          "Reads the change-world guards (world state, orbit scenario vs the orbit tag setting, the event-registry slot) and the likely "
          "loot_event-client scenario. Expected in orbit: ready. No world change is made.",
          probeChangeWorld);
    probe("activity.check.deploy", "Activity Deploy (Mirror Scan Info)",
          "Reads the current activity id and session root so the owner can run the blocked deploy's mirror finder. No scan or write here.",
          probeMirrorScanInfo, kBadgeBlocked);
}

}   
