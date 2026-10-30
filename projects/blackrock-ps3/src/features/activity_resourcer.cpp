 
 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>

#include "app/engine.h"
#include "app/registry.h"
#include "core/runtime.h"
#include "features/activity_shared.h"
#include "game/activity_addr.h"
#include "game/activity_resourcer.h"
#include "game/names_catalog.h"
#include "game/world_mem.h"
#include "ui/widgets.h"

namespace brps3 {
namespace {

using namespace ui;

constexpr const char* kRscLoop = "activity.rsc";

using activity_ui::hx;
using activity_ui::uiDemo;

struct RscUi {
    std::mutex m;
    bool have = false;
    activity::RscState rsc;
    activity::WtlState wtl;
    std::string error;
    std::atomic<bool> live{false};
};
RscUi& rscUi() {
    static RscUi s;
    return s;
}

void readRsc(Ctx& c) {
    c.require(activity::kRscFingerprints, "Resourcerer diagnostics");    
    world::RuntimeReader mem(c.rt());
    activity::RscState rsc;
    activity::WtlState wtl;
    std::string err;
    try {
        rsc = activity::readRscState(mem);
        wtl = activity::readWtlState(mem);
    } catch (const std::exception& e) {
        err = e.what();
    }
    RscUi& s = rscUi();
    std::lock_guard<std::mutex> lock(s.m);
    s.have = err.empty();
    s.rsc = rsc;
    s.wtl = wtl;
    s.error = err;
}

void setLive(Ui& ui, bool on) {
    rscUi().live = on;
    if (on)
        ui.post("Watch resourcerer", [](Ctx& c) { c.startLoop(kRscLoop, 200, [](Ctx& lc) { readRsc(lc); }); });
    else
        ui.post("Stop watching resourcerer", [](Ctx& c) { c.stopLoop(kRscLoop); });
}

void stat(const char* cap, const std::string& v, float w, const ImVec4& col = pal::Text) { statTile(cap, cap, v, {}, w, col); }

void seedRscDemo() {
    static bool done = false;
    if (done || !uiDemo()) return;
    done = true;
    RscUi& s = rscUi();
    std::lock_guard<std::mutex> lock(s.m);
    s.have = true;
    s.rsc.cursor = 184;
    s.rsc.mode = 1;
    s.rsc.beginCat0 = 0x0007;
    s.rsc.beginCat1 = 0x0003;
    s.rsc.handleA = 0x0005;
    s.rsc.handleB = 0x0012;
    s.rsc.dirtyAdd = 0x03;
    s.rsc.dirtyDel = 0x00;
    s.rsc.odCount = 704;
    s.rsc.odTruncated = true;
    s.rsc.requests.push_back({0, 0x604A1200, 4, 0x01, 0x01});
    s.rsc.requests.push_back({1, 0x604A1288, 0, 0x00, 0x00});
    const uint32_t tags[] = {0x8108E01D, 0x8108E16F, 0x80A6B027, 0x8106146A, 0x811BC003};
    uint16_t idx = 12;
    for (uint32_t tg : tags) s.rsc.onDemand.push_back({idx++, tg, 0x01});
    s.wtl.ok = true;
    s.wtl.globals = 0x60085370;
    s.wtl.inflightDet = 1;
    s.wtl.inflightNondet = 0;
    s.wtl.queueA = 0xFFFFFFFF;
    s.wtl.queueB = 0xFFFFFFFF;
    s.wtl.queueC = 0x30661240;
}

void drawResourcerer(Ui& ui) {
    RscUi& s = rscUi();
    seedRscDemo();
    {
        auto off = requireConnection(ui);
        if (button("Refresh", {.icon = icon::Refresh, .busy = ui.busy("Read resourcerer")}))
            ui.post("Read resourcerer", [](Ctx& c) { readRsc(c); });
        tooltip("Read-only monitor of the resourcerer and the world tag loader. Never calls the pump or dispatcher (not re-entrant).");
        ImGui::SameLine(0, px(8));
        bool live = s.live.load();
        if (toggleSwitch("##rsclive", &live)) setLive(ui, live);
        tooltip("Poll the resourcerer state continuously, about 200 ms apart (rsc.py; read-only)");
        ImGui::SameLine(0, px(8));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(7));
        caption(live ? "Live" : "Live Off", live ? pal::Text : pal::Muted);
    }
    gap(sp::M);

    activity::RscState rsc;
    activity::WtlState wtl;
    std::string err;
    bool have;
    {
        std::lock_guard<std::mutex> lock(s.m);
        rsc = s.rsc;
        wtl = s.wtl;
        err = s.error;
        have = s.have;
    }
    if (!have) {
        caption(err.empty() ? "Refresh while in or loading an activity." : err.c_str(), err.empty() ? pal::Faint : pal::Warning);
        return;
    }
    const float avail = availWidth();
    const int cols = avail > px(560) ? 3 : 2;
    const float tw = std::floor((avail - px(8) * float(cols - 1)) / float(cols));
    stat("WTL CURSOR", rsc.cursor == 0xFFFF ? "idle" : ("busy #" + std::to_string(rsc.cursor)), tw, rsc.loaderBusy() ? pal::Warning : pal::Muted);
    ImGui::SameLine(0, px(8));
    stat("ON-DEMAND", std::to_string(rsc.odCount) + (rsc.odTruncated ? "+" : ""), tw, pal::Muted);
    if (cols == 3) ImGui::SameLine(0, px(8));
    stat("MODE", std::to_string(rsc.mode), tw, pal::Muted);
    gap(sp::S);
    labelf("begin table %04X / %04X   handles %04X / %04X   dirty add %02X del %02X", rsc.beginCat0, rsc.beginCat1, rsc.handleA, rsc.handleB,
           rsc.dirtyAdd, rsc.dirtyDel);
    gap(sp::XS);
    if (wtl.ok)
        labelf("WTL globals %s   in-flight det %u nondet %u   queues %s/%s/%s", hx(wtl.globals).c_str(), wtl.inflightDet, wtl.inflightNondet,
               hx(wtl.queueA).c_str(), hx(wtl.queueB).c_str(), hx(wtl.queueC).c_str());
    else caption(wtl.why.c_str(), pal::Faint);

     
    if (!rsc.requests.empty()) {
        gap(sp::M);
        fieldLabel("RESOURCERER REQUESTS");
        gap(sp::XS);
        for (const activity::RscRequest& rq : rsc.requests)
            labelf("req %d @%s  state %u  dirty %02X  ran %02X", rq.index, hx(rq.addr).c_str(), rq.state, rq.dirty, rq.ran);
    }

     
    gap(sp::M);
    {
        char hdr[64];
        std::snprintf(hdr, sizeof hdr, "ON-DEMAND LIST  (%zu%s)", rsc.odCount, rsc.odTruncated ? ", truncated" : "");
        fieldLabel(hdr);
    }
    gap(sp::XS);
    if (rsc.onDemand.empty()) {
        caption("The on-demand tag list is empty.", pal::Faint);
        if (s.live.load()) requestRedraw();
        return;
    }
    if (beginTable("##rscod", 4, std::min(260.0f, 34.0f + 26.0f * float(rsc.onDemand.size())), ImGuiTableFlags_BordersOuter)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Idx", ImGuiTableColumnFlags_WidthFixed, px(44));
        ImGui::TableSetupColumn("Tag", ImGuiTableColumnFlags_WidthFixed, px(92));
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Flag", ImGuiTableColumnFlags_WidthFixed, px(44));
        tableHeaders();
        ImGui::PushFont(font(FontKind::Body), fontSize(FontKind::Body, type::Label));
        for (const activity::OnDemandEntry& e : rsc.onDemand) {
            ImGui::TableNextRow(0, px(26));
            ImGui::TableSetColumnIndex(0);
            cellText(std::to_string(e.index).c_str(), pal::Faint, FontKind::Body, type::Small, 26);
            ImGui::TableSetColumnIndex(1);
            cellText(hx(e.tag).c_str(), pal::Accent, FontKind::Mono, type::MonoSmall, 26);
            ImGui::TableSetColumnIndex(2);
            const std::string name = names::tagLabel(names::fromPs3Tag(e.tag));
            const bool unnamed = name == hx(e.tag);    
            cellText(unnamed ? "(not in the catalogue)" : name.c_str(), unnamed ? pal::Faint : pal::Text, FontKind::Body, type::Label, 26);
            ImGui::TableSetColumnIndex(3);
            char f[8];
            std::snprintf(f, sizeof f, "%02X", e.flag);
            cellText(f, pal::Muted, FontKind::Mono, type::MonoSmall, 26);
        }
        ImGui::PopFont();
        endTable();
    }
    if (s.live.load()) requestRedraw();
}

 

struct PluginUi {
    std::mutex m;
    bool have = false;
    activity::WtlState wtl;
    std::string error;
};
PluginUi& pluginUi() {
    static PluginUi s;
    return s;
}

void drawResourcerPlugin(Ui& ui) {
    PluginUi& s = pluginUi();
    {
        auto off = requireConnection(ui);
        if (button("Read Loader State", {.icon = icon::Refresh, .busy = ui.busy("Read loader state")}))
            ui.post("Read loader state", [](Ctx& c) {
                c.require(activity::kRscFingerprints, "World tag loader state");
                world::RuntimeReader mem(c.rt());
                activity::WtlState w;
                std::string err;
                try {
                    w = activity::readWtlState(mem);
                } catch (const std::exception& e) {
                    err = e.what();
                }
                PluginUi& st = pluginUi();
                std::lock_guard<std::mutex> lock(st.m);
                st.have = err.empty();
                st.wtl = w;
                st.error = err;
            });
        tooltip("The blackrock-resourcer plugin acquired and released a manifest owner through the world tag loader from a balanced "
                "owner/alloc scope. On PS3 the owner/alloc balanced-scope functions and the Mercury import/remove calls are unmapped, so the "
                "live acquire/release is not ported; this reads the loader state the experiment watches.\n\n"
                "Mapped: WTL create 0x15216C, pair add 0x15135C, pair remove 0x151450, finalize 0x1523C4, dispose 0x1521E4; world-event "
                "queue register 0x1267C4 / current-slot 0x126B84 / post 0x1268D4. Unmapped: the 8 owner/alloc scope functions and Mercury "
                "import/remove (8253BCA0 / 8253BF78).");
    }
    gap(sp::M);
    activity::WtlState w;
    bool have;
    std::string err;
    {
        std::lock_guard<std::mutex> lock(s.m);
        w = s.wtl;
        have = s.have;
        err = s.error;
    }
    if (!have) {
        caption(err.empty() ? "Read the loader state while in an activity." : err.c_str(), err.empty() ? pal::Faint : pal::Warning);
        return;
    }
    if (!w.ok) {
        pill(w.why.c_str(), pal::Warning, icon::Info);
        return;
    }
    labelf("globals %s", hx(w.globals).c_str());
    gap(sp::XS);
    labelf("in-flight requests: deterministic %u, nondeterministic %u", w.inflightDet, w.inflightNondet);
    gap(sp::XS);
    labelf("queue heads %s / %s / %s", hx(w.queueA).c_str(), hx(w.queueB).c_str(), hx(w.queueC).c_str());
    gap(sp::XS);
    pill(w.idle() ? "loader idle" : "loader busy", w.idle() ? pal::Success : pal::Warning);
}

}   

BR_FEATURES(activity_resourcer) {
    r.section({.id = "activity.resourcerer", .page = Page::Testing, .tab = "Resourcer", .section = "Resourcerer", .order = 10,
               .draw = drawResourcerer,
               .onDisconnect = [] {
                   RscUi& s = rscUi();
                   s.live = false;
                   std::lock_guard<std::mutex> lock(s.m);
                   s.have = false;
               }});
    r.section({.id = "activity.resourcer_plugin", .page = Page::Testing, .tab = "Resourcer", .section = "Resourcer plugin", .order = 20,
               .draw = drawResourcerPlugin, .badge = kBadgeBlocked});
    ui::styleSection("Resourcerer", {.fullWidth = true, .icon = ui::icon::Memory});
    ui::styleSection("Resourcer plugin", {.fullWidth = true, .icon = ui::icon::Memory});
}

}   
