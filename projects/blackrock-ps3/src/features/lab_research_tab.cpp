 
 
 
#include <string>
#include <vector>

#include "app/registry.h"
#include "core/error.h"
#include "features/lab_ui.h"
#include "game/lab_research.h"
#include "ui/widgets.h"

namespace brps3 {
namespace {

using namespace ui;
using labui::fmt;
using labui::hx;

constexpr const char* kSecAudio = "Audio queue and notifications";
constexpr const char* kSecProbes = "blackrock-testing probes";
constexpr const char* kReadAudio = "Read the Audio State Header";
constexpr const char* kReadProbes = "Read the Testing-Probe Anchors";

labui::Slot<lab::AudioState>& audio() {
    static labui::Slot<lab::AudioState> s;
    return s;
}
labui::Slot<lab::ProbeAnchors>& probes() {
    static labui::Slot<lab::ProbeAnchors> s;
    return s;
}

void runReadAudio(Ctx& c) {
    c.require(lab::kAudioFingerprints, "Audio anchors (lab)");
    world::RuntimeReader mem(c.rt());
    const lab::AudioState a = lab::readAudioState(mem);
    for (const lab::Check& k : a.checks) c.log(fmt("[lab] audio %s: %s (expected %s) %s", k.what.c_str(), k.value.c_str(), k.expected.c_str(), k.ok ? "ok" : "DIFFERS"));
    audio().set(a);
}

void runReadProbes(Ctx& c) {
    c.require(lab::kProbeFingerprints, "Testing-probe anchors (lab)");
    world::RuntimeReader mem(c.rt());
    const lab::ProbeAnchors p = lab::readProbeAnchors(mem);
    for (const lab::Check& k : p.checks) c.log(fmt("[lab] %s: %s (expected %s) %s", k.what.c_str(), k.value.c_str(), k.expected.c_str(), k.ok ? "ok" : "DIFFERS"));
    probes().set(p);
}

void seedDemo() {
    static bool done = false;
    if (done) return;
    done = true;
    if (!labui::demo()) return;
    lab::AudioState a;
    a.state.slot = lab::kAudioStateSlot;
    a.state.block = 0x30A2C010;
    a.state.initialised = true;
    a.state.header = {0x132, lab::kAudioStateHash, 0, 6, 3, 26};
    a.spatial = 0x30A30010;
    a.checks = {{"state block *(01ADBB4C)", "30A2C010", "a heap block", true},
                {"header hash", "EBF701A3", "EBF701A3", true},
                {"header category", "3", "3", true},
                {"header owner", "26", "26", true},
                {"spatial data *(01ADBB50)", "30A30010", "a mapped block or 0", true}};
    audio().set(a);
    lab::ProbeAnchors p;
    p.latch = 0x30B40020;
    p.latchRead = true;
    p.sessionPool = 0x60A00000;
    p.provider = 0x3301F000;
    p.vtable = lab::kInvestmentVtable;
    p.slot1FC = lab::kInvestmentVt1FC;
    p.code1FC = lab::kInvestVt1FCCode;
    p.checks = {{"profile error latch *(01AB638C)", "30B40020", "a mapped pointer", true},
                {"session pool *(01ABCA14)", "60A00000", "a mapped pointer", true},
                {"investment provider *(01897CEC)", "3301F000", "a mapped object", true},
                {"provider vtable", "013E6478", "013E6478", true},
                {"vtable +0x1FC (OPD)", "0184ECD0", "0184ECD0", true},
                {"OPD -> code", "001CCA50", "001CCA50", true}};
    probes().set(p);
}

void blockedIntro(const char* missing, const char* why) {
    labui::warnLine(missing);
    tooltip(why);
}

void drawChecks(const std::vector<lab::Check>& checks) {
    for (const lab::Check& k : checks)
        labui::check(k.ok, k.what + ": " + k.value, k.ok ? nullptr : ("expected " + k.expected).c_str());
}

void drawAudio(Ui& ui) {
    seedDemo();
    blockedIntro("Blocked: the Wwise queue, allocator and lifecycle functions are not mapped on PS3.",
                 "The Xbox notification built a 20-byte type-8 message and enqueued it once through 830CB058 under the queue's critical "
                 "section, after XDK lifecycle hooks on AK init / shutdown (relays published with icbi + KeIpiGenericCall) and a "
                 "title-terminate fence. A PS3 version needs its own design (the sys_lwmutex owner of the queue, journalled hooks) and "
                 "the unmapped functions first. Development-only and low value, so it is not ported.");
    gap(sp::M);
    fieldLabel("VERIFIED ANCHORS (READ ONLY)");
    gap(sp::XS);
    if (labui::jobButton(ui, kReadAudio, icon::View,
                         "Read and check the audio state block header (read only). State slot 01ADBB4C (16144 bytes, hash EBF701A3, "
                         "category 3, owner 26), spatial slot 01ADBB50, registrar 005E9718.")) labui::post(ui, kReadAudio, runReadAudio);
    labui::readStamp(audio().at());
    labui::errorBox(audio().error());
    if (const auto a = audio().get()) {
        gap(sp::S);
        drawChecks(a->checks);
    }
}

void drawProbes(Ui& ui) {
    seedDemo();
    blockedIntro("Blocked: the reward-pool and item-constructor natives are unmapped, and the context probe's session layout differs on PS3.",
                 "Custom pool: the descriptor init 832CF000, weighted select 832CDE08, descriptor fill 832CCD30 and the item-detail "
                 "constructor 8329F648 have no PS3 address (loot group). Context probe: the PS3 session record uses player stride 0x12208, "
                 "buffer stride 0x9100 and state +0x8478 (Xbox 0x123C8 / 0x91E0 / +0x8548), so the descriptor offset 0x7010 must be "
                 "re-derived before anything is written there.");
    gap(sp::M);
    fieldLabel("WHAT CAN BE READ NOW");
    gap(sp::XS);
    if (labui::jobButton(ui, kReadProbes, icon::View,
                         "Read the latch, the session pool pointer and the custom-pool provider precheck (read only): the native-route latch "
                         "*(01AB638C), the session pool 01ABCA14 and provider *(01897CEC), vtable 013E6478, vtable +0x1FC -> OPD 0184ECD0 -> "
                         "001CCA50."))
        labui::post(ui, kReadProbes, runReadProbes);
    labui::readStamp(probes().at());
    labui::errorBox(probes().error());
    if (const auto p = probes().get()) {
        gap(sp::S);
        drawChecks(p->checks);
        if (p->latchRead) {
            gap(sp::XS);
            caption(fmt("profile error latch bytes: %02X %02X (0x2A84DC clears both)", p->latchBytes[0], p->latchBytes[1]).c_str(), pal::Muted);
        }
    }
}

void reset() {
    audio().clear();
    probes().clear();
}

}   

BR_FEATURES(lab_research) {
    const Page T = Page::Testing;
    r.section({.id = "lab.research.audio_card", .page = T, .tab = "Research", .section = kSecAudio, .order = 10, .draw = drawAudio, .onDisconnect = reset,
               .badge = kBadgeBlocked});
    r.section({.id = "lab.research.probes_card", .page = T, .tab = "Research", .section = kSecProbes, .order = 20, .draw = drawProbes, .badge = kBadgeBlocked});
    r.action({.id = "lab.research.audio", .page = T, .tab = "Research", .section = kSecAudio, .label = kReadAudio,
              .help = "Read and check the audio state block header (slot 0x1ADBB4C) and the spatial slot. Read only.", .run = runReadAudio,
              .badge = kBadgeBlocked});
    r.action({.id = "lab.research.probes", .page = T, .tab = "Research", .section = kSecProbes, .label = kReadProbes,
              .help = "Read the profile error latch, the session pool pointer and the investment provider vtable precheck. Read only.",
              .run = runReadProbes, .badge = kBadgeBlocked});
    ui::styleSection(kSecAudio, {.icon = ui::icon::Info, .ownsActions = true});
    ui::styleSection(kSecProbes, {.icon = ui::icon::Probe, .ownsActions = true});
}

}   
