 
 
 
 
 
 
 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <set>

#include "app/engine.h"
#include "app/registry.h"
#include "core/error.h"
#include "core/profile.h"
#include "core/runtime.h"
#include "features/inventory_shared.h"
#include "game/inventory_addr.h"
#include "game/inventory_capture.h"
#include "game/inventory_editor.h"
#include "game/inventory_verify.h"
#include "game/world_mem.h"
#include "ui/widgets.h"

namespace brps3::invui {

using namespace inventory;
using namespace ui;

namespace {

constexpr const char* kProbeSection = "Inventory";
constexpr const char* kDevTab = "Inventory";
constexpr const char* kCaptureLoop = "Inventory capture";
constexpr const char* kVerifyJob = "Verify Native Catalogue";

std::string fmt(const char* f, ...) {
    char buf[600];
    va_list a;
    va_start(a, f);
    std::vsnprintf(buf, sizeof buf, f, a);
    va_end(a);
    return buf;
}

std::string name(uint16_t def) {
    const ItemDef* d = catalog().item(def);
    return d ? d->label() : "definition " + std::to_string(def);
}

Snapshot fresh(Ctx& c, bool talents) {
    Io& io = c.state<Io>("inventory.io");
    c.require(kReadFingerprints, "Inventory reader");
    std::lock_guard<std::mutex> lock(io.m);
    world::RuntimeReader mem(c.rt());
    ReadOptions o;
    o.talents = talents;
    return readSnapshotRetry(mem, io.cache, o, 3, [&c] { return c.cancelled(); });
}

 

void probeSnapshot(Ctx& c) {
    Snapshot s;
    try {
        s = fresh(c, true);
    } catch (const SnapshotError& e) {
        c.log(std::string("snapshot refused: ") + e.what());
        c.log(e.kind == FailKind::NotSignedIn ? "verdict: not signed in - load a character (orbit or an activity) and run it again"
                                              : "verdict: the reader could not take a coherent snapshot (see above)");
        return;
    }
    c.log(fmt("profile manager *(01AD5FA4) = %s", world::hex8(s.manager).c_str()));
    c.log(fmt("account profile %s (28752 bytes, 80801803), character profile %s (28088 bytes, 80801743)", world::hex8(s.accountAddr).c_str(),
              world::hex8(s.characterAddr).c_str()));
    c.log(fmt("account SOID %s, character SOID %s", hex16(s.accountSoid).c_str(), hex16(s.characterSoid).c_str()));
    c.log(fmt("family-4 descriptor %u at %s: state %u, version %u", s.descriptorIndex, world::hex8(s.descriptorAddr).c_str(),
              unsigned(s.descriptor[off::DESC_STATE]), s.familyVersion));
    c.log(fmt("investment provider mode %u; globals refs +0x84 %s, +0x144 %s, +0x29C %s (live probe saw 80A5F239 / 80A5F241 / 80A5F24C)", s.mode,
              world::hex8(s.bucketsTag).c_str(), world::hex8(s.defsTag).c_str(), world::hex8(s.indexTag).c_str()));
    c.log(fmt("definition table %s: %u definitions (catalogue %zu)", world::hex8(s.defsBase).c_str(), s.defCount, catalog().size()));
    size_t chr = 0, acc = 0, vault = 0, eq = 0;
    for (const Item& it : s.items) {
        chr += it.owner == Owner::Character;
        acc += it.owner == Owner::Account;
        vault += it.vault();
    }
    for (uint64_t e : s.equipped) eq += e != 0;
    c.log(fmt("rows: %zu character, %zu account (%zu in the vault buckets); %zu equipped SOIDs at character +0x2900", chr, acc, vault, eq));
    c.log(fmt("detail rows: %u at %s, %zu matched to an item (SOID + definition)", s.detailCount, world::hex8(s.detailAddr).c_str(), s.detailMatched));
    c.log(fmt("%llu PS3MAPI reads, %.0f ms (every range read twice)", static_cast<unsigned long long>(s.reads), s.ms));
    const bool ok = s.defCount == catalog().size() && chr > 0;
    c.log(ok ? "verdict: snapshot OK - compare the counts with the game (character items, vault fill)"
             : "verdict: the snapshot read, but something looks off (definition count or an empty character)");
}

void probeCharacterRows(Ctx& c) {
    const Snapshot s = fresh(c, false);
    c.log("equipped list (character +0x2900, 20 x SOID) - expected: exactly what your character wears:");
    for (int i = 0; i < 20; ++i) {
        if (!s.equipped[size_t(i)]) continue;
        const Item* it = s.find(s.equipped[size_t(i)]);
        c.log(fmt("  [%2d] %s  %s", i, hex16(s.equipped[size_t(i)]).c_str(), it ? (name(it->def) + " (" + bucketName(it->bucket) + ", slot " + std::to_string(it->slot) + ")").c_str()
                                                                              : "NOT FOUND in the holders"));
    }
    c.log("character holder +0x38 (320 rows), weapons and armor:");
    for (const Item& it : s.items) {
        if (it.owner != Owner::Character || !(isWeaponBucket(it.bucket) || isArmorBucket(it.bucket))) continue;
        c.log(fmt("  slot %3u  %-16s  %s%s", it.slot, bucketName(it.bucket).c_str(), name(it.def).c_str(), it.equipped ? "  [equipped]" : ""));
    }
    size_t missing = 0;
    for (uint64_t e : s.equipped) missing += e && !s.find(e);
    c.log(missing ? "verdict: some equipped SOIDs are not in the holders - report this list"
                  : "verdict: compare these names and slots with the character screen (weapons 93-122, armor 43-92)");
}

void probeDetailRows(Ctx& c) {
    const Snapshot s = fresh(c, true);
    c.log(fmt("detail table: %u rows of 168 bytes at %s; %zu items matched (unique SOID + same definition)", s.detailCount, world::hex8(s.detailAddr).c_str(),
              s.detailMatched));
    int shown = 0;
    for (const Item& it : s.items) {
        if (!it.equipped || !it.hasDetail) continue;
        const RollView r = describeRoll(it, catalog());
        c.log(fmt("  %s (grid %u): %s", name(it.def).c_str(), r.grid, r.readable ? r.summary.c_str() : ("unreadable: " + r.why).c_str()));
        ++shown;
    }
    c.log(shown ? "verdict: the perks above should be the perks the equipped items show in game (locked ones included)"
                : "verdict: no equipped item has a matched detail row - report this");
}

void probeGate(Ctx& c) {
    c.require(kGateFingerprints, "Inventory request gate");
    const auto r = c.rt().callMany({NativeCall{addr::PROVIDER_CHECK, {}, {}, profile::TOC}, NativeCall{addr::REQUEST_STATE, {0}, {}, profile::TOC}});
    c.log(fmt("0x1CB82C() = %08X (investment provider; expected non-zero with a character loaded)", r[0].u32()));
    c.log(fmt("0x475D50(0) = %d (request state; every inventory wrapper sends only when it is 3)", r[1].i32()));
    c.log(r[0].u32() && r[1].i32() == 3 ? "verdict: requests can be sent right now (transfer / equip / use / dismantle)"
                                         : "verdict: requests would be dropped right now - expected in menus or while loading; try again in orbit or an activity");
}

void probePostmaster(Ctx& c) {
    const Snapshot s = fresh(c, false);
    c.log(fmt("postmaster registry character +0x3BD0 counter = %u; +0x3BD8 64 x 0x38 entries in use: %zu", s.postmasterCount, s.postmaster.size()));
    for (const PostmasterEntry& e : s.postmaster) c.log(fmt("  %s  amount %u  category %u", name(e.def).c_str(), e.amount, e.category));
    const Item* coins = nullptr;
    for (const Item& it : s.items)
        if (it.owner == Owner::Character && it.def == kThreeOfCoins) coins = &it;
    c.log(coins ? fmt("Three of Coins: %u in character slot %u", coins->quantity, coins->slot) : std::string("Three of Coins: none on the character"));
    c.log(fmt("character +0x549A = %02X (bit 0x02 = Exotic Particles active; offset UNVERIFIED on PS3)", s.exoticFlag));
    c.log("verdict: confirm the postmaster entries match the game; then use one coin and run this again: expect coins -1 and +0x549A bit 0x02 set");
}

void probeEditor(Ctx& c) {
    c.require(kReadFingerprints, "Inventory reader");
    c.require(kEditorFingerprints, "Live editor");
    Io& io = c.state<Io>("inventory.io");
    EditorSnapshot s;
    try {
        std::lock_guard<std::mutex> lock(io.m);
        world::RuntimeReader mem(c.rt());
        s = editorRead(mem, io.cache, editorCatalog(), catalog());
    } catch (const std::exception& e) {
        c.log(std::string("editor read refused: ") + e.what());
        c.log("verdict: the editor cannot match the profiles yet (expected: exactly one owned cache profile listing the equipped SOIDs)");
        return;
    }
    for (const Profile& p : s.profiles)
        c.log(fmt("%s profile at %s", p.kind == Profile::Cache ? "orbit-cache" : "gameplay", world::hex8(p.addr).c_str()));
    int match = 0, differ = 0, unknown = 0;
    for (int i = 0; i < 20; ++i) {
        const SlotState& st = s.slots[size_t(i)];
        if (!st.soid) continue;
        std::string art = "-";
        if (st.art != 0xFFFF) {
            try {
                const auto parts = resolveArt(st.pattern, st.art, i, s.profiles[0].raw, editorCatalog());
                const bool same = std::equal(parts.begin(), parts.end(), st.parts.begin());
                art = same ? "model tags match the catalogue" : "model tags DIFFER: live " + hexBytes(st.parts.data(), 16) + "... vs " + hexBytes(parts.data(), 16) + "...";
                (same ? match : differ)++;
            } catch (const std::exception& e) {
                art = std::string("not checkable: ") + e.what();
                ++unknown;
            }
        }
        c.log(fmt("  [%2d] %-15s %s pattern %u art %u: %s", i, kSlotNames[i], name(st.def).c_str(), st.pattern, st.art, art.c_str()));
    }
    c.log(fmt("%zu item detail rows; character perks: %zu set", s.items.size(), nonEmpty(s.characterPerks).size()));
    c.log(differ ? "verdict: art tags differ - PS3 tag ids are not the catalogue's for art; do not change pattern / art (perks and talents are unaffected)"
                 : fmt("verdict: profiles matched (%zu copies); %d slot(s) prove the catalogue art tags are the PS3 ones", s.profiles.size(), match));
}

void probeTables(Ctx& c) {
    c.require(kCatalogFingerprints, "Native catalogue");
    world::RuntimeReader mem(c.rt());
    const VerifyReport r = verifyCatalog(mem, catalog());
    for (const VerifyLine& l : r.lines) {
        const char* tag = l.level == VerifyLine::Ok ? "ok  " : l.level == VerifyLine::Fail ? "FAIL" : l.level == VerifyLine::Warn ? "warn" : "info";
        c.log(fmt("%s %s: %s", tag, l.what.c_str(), l.detail.c_str()));
    }
    c.log(r.verdict);
}

void probeDryRun(Ctx& c) {
    const auto snap = current();
    const Selection sel = selection();
    const Item* it = snap ? findSelected(*snap, sel) : nullptr;
    if (!it) {
        c.log("no item selected on the Vault page");
        c.log("verdict: select an item on Vault > Inventory and run this again (nothing is sent)");
        return;
    }
    c.require(kReadFingerprints, "Inventory reader");
    Io& io = c.state<Io>("inventory.io");
    c.log("item: " + name(it->def) + " (" + locationName(*it) + ", " + bucketName(it->bucket) + ", slot " + std::to_string(it->slot) + ")");
    int planned = 0;
    for (Action a : {Action::Deposit, Action::Withdraw, Action::Equip, Action::Unequip, Action::Use, Action::Dismantle}) {
        const Eligibility e = eligibility(*snap, *it, a, catalog());
        if (!e.ok) {
            c.log(fmt("%-9s not allowed: %s", actionName(a), e.reason.c_str()));
            continue;
        }
        ActionRequest r;
        r.action = a;
        r.owner = it->owner;
        r.slot = it->slot;
        r.def = it->def;
        r.soid = it->soid;
        r.expectedCharacter = snap->characterSoid;
        r.dryRun = true;
        try {
            std::lock_guard<std::mutex> lock(io.m);
            world::RuntimeReader mem(c.rt());
            const ActionResult res = sendAction(c.rt(), mem, io.cache, r, catalog());
            c.log(fmt("%-9s %s", actionName(a), res.message.c_str()));
            const guard::Request q = buildRequest(res.before, res.plan);
            std::string args;
            for (uint64_t v : q.args) args += fmt("%llX ", static_cast<unsigned long long>(v));
            c.log("          args r3.. = " + args + "(the SOID argument points into the guard block)");
            for (const std::string& cond : res.conditions) c.log("          check: " + cond);
            ++planned;
        } catch (const std::exception& e) {
            c.log(fmt("%-9s preflight refused: %s", actionName(a), e.what()));
        }
    }
    c.log(planned ? "verdict: these are the exact calls and same-frame checks a real action would use; nothing was sent. For equip / "
                    "unequip / use (new on PS3): switch on TESTING > Inventory > Capture inventory requests, do it once in the game's own "
                    "menu, and compare the captured 403 / 404 / 402 arguments with the ones above"
                  : "verdict: no action is allowed for this item right now (see the reasons above)");
}

 

struct VerifyState {
    std::mutex m;
    VerifyReport report;
    bool have = false;
    std::string error;
    std::string progress;
};
VerifyState& vs() {
    static VerifyState s;
    return s;
}

std::string reportText(const VerifyReport& r) {
    std::string out = "Native catalogue verification (mode " + std::to_string(r.mode) + ")\n";
    for (const VerifyLine& l : r.lines) {
        const char* tag = l.level == VerifyLine::Ok ? "ok  " : l.level == VerifyLine::Fail ? "FAIL" : l.level == VerifyLine::Warn ? "warn" : "info";
        out += std::string(tag) + " " + l.what + (l.detail.empty() ? "" : ": " + l.detail) + "\n";
    }
    return out + r.verdict + "\n";
}

void runVerify(Ctx& c) {
    c.require(kCatalogFingerprints, "Native catalogue");
    world::RuntimeReader mem(c.rt());
    const VerifyReport r = verifyCatalog(mem, catalog(), [](const std::string& p) {
        std::lock_guard<std::mutex> lock(vs().m);
        vs().progress = p;
    });
    c.log(reportText(r));
    {
        std::lock_guard<std::mutex> lock(vs().m);
        vs().report = r;
        vs().have = true;
        vs().error.clear();
        vs().progress.clear();
    }
    c.toast(r.ok ? "Native catalogue verified" : "Native catalogue: some checks FAILED (TESTING > Inventory)");
}

void drawVerify(Ui& ui) {
    VerifyState& s = vs();
    const bool busy = ui.busy(kVerifyJob);
    {
        auto offline = requireConnection(ui);
        if (button("Verify", {.tone = Tone::Primary, .icon = icon::Shield, .tooltip = "Read-only: interface, vtable OPDs, tag caches, routing tables, definitions",
                              .busy = busy, .successKey = "inventory.verify"})) {
            noteQueued(kVerifyJob);
            ui.post(
                kVerifyJob, runVerify,
                [](bool ok, const std::string& err) {
                    std::lock_guard<std::mutex> lock(vs().m);
                    if (ok) flashSuccess("inventory.verify");
                    else vs().error = err;
                    vs().progress.clear();
                });
        }
    }
    VerifyReport r;
    bool have;
    std::string error, progress;
    {
        std::lock_guard<std::mutex> lock(s.m);
        r = s.report;
        have = s.have;
        error = s.error;
        progress = s.progress;
    }
    if (have) {
        ImGui::SameLine(0, px(8));
        if (button("Copy Report", {.tone = Tone::Ghost, .icon = icon::Copy})) copyToClipboard(reportText(r), "report");
    }
    gap(sp::M);
    if (busy) {
        progressBar(-1, 0, 4);
        gap(sp::XS);
        caption(progress.empty() ? "Checking..." : progress.c_str(), pal::Muted);
        requestRedraw();
        return;
    }
    if (!error.empty()) pill(error.c_str(), pal::Danger, icon::Warning);
    if (!have) return;
    for (const VerifyLine& l : r.lines) {
        const ImVec4 col = l.level == VerifyLine::Ok ? pal::Success : l.level == VerifyLine::Fail ? pal::Danger : l.level == VerifyLine::Warn ? pal::Warning : pal::Info;
        const char* glyph = l.level == VerifyLine::Ok ? icon::Check : l.level == VerifyLine::Fail ? icon::Error : l.level == VerifyLine::Warn ? icon::Warning : icon::Info;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const ImVec2 is = textSize(FontKind::Icons, 13, glyph);
        drawText(ImGui::GetWindowDrawList(), FontKind::Icons, 13, ImVec2(p.x, p.y + px(3)), u32(col), glyph);
        ImGui::SetCursorScreenPos(ImVec2(p.x + is.x + px(10), p.y));
        ImGui::BeginGroup();
        label(l.what.c_str());
        if (!l.detail.empty()) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + availWidth());
            wrapped(l.detail.c_str(), pal::Muted, type::Small);
            ImGui::PopTextWrapPos();
        }
        ImGui::EndGroup();
        gap(sp::XS);
    }
    gap(sp::S);
    pill(r.verdict.c_str(), r.ok ? pal::Success : pal::Danger, r.ok ? icon::Check : icon::Warning);
}

 

struct CaptureState {
    std::mutex m;
    uint32_t buffer = 0;
    uint32_t seen = 0;
    std::deque<CaptureRecord> records;    
    std::atomic<bool> on{false};
};
CaptureState& cap() {
    static CaptureState s;
    return s;
}

uint32_t stubSize(const CaptureSite& site) {
    return uint32_t((captureStub(profile::CAVE, site, profile::CONTROL).size() + 15) & ~size_t(15));
}

void captureOff(Ctx& c) {
    CaptureState& s = cap();
    c.stopLoop(kCaptureLoop);
    Runtime& rt = c.rt();
    for (const CaptureSite& site : captureSites())
        if (rt.hooked(site.site)) rt.unhook(site.site);
    uint32_t buffer;
    {
        std::lock_guard<std::mutex> lock(s.m);
        buffer = s.buffer;
        s.buffer = 0;
    }
    if (buffer) rt.freeLater(buffer);
    s.on = false;
}

void captureSet(Ctx& c, bool on) {
    if (!on) {
        captureOff(c);
        return;
    }
    CaptureState& s = cap();
    if (s.on.load()) return;
    c.require(kCaptureFingerprints, "Request capture");
    Runtime& rt = c.rt();
    const auto empty = emptyCaptureBuffer();
    const uint32_t buffer = rt.alloc(kCaptureBuffer, empty.data(), empty.size());
    {
        std::lock_guard<std::mutex> lock(s.m);
        s.buffer = buffer;
        s.seen = 0;
    }
    try {
        for (const CaptureSite& site : captureSites())
            rt.hook(site.site, [&site, buffer](uint32_t base, uint32_t) { return captureStub(base, site, buffer); }, site.word, stubSize(site));
    } catch (...) {
        captureOff(c);
        throw;
    }
    s.on = true;
    c.log("[capture] armed at 0x471DBC (402), 0x471C84 (403), 0x471EEC (404), 0x472028 (405), 0x4730B8 (904); buffer " + world::hex8(buffer));
    c.startLoop(kCaptureLoop, 500, [buffer](Ctx& lc) {
        CaptureState& t = cap();
        const auto bytes = lc.rt().read(buffer, kCaptureBuffer);
        uint32_t seen;
        {
            std::lock_guard<std::mutex> lock(t.m);
            seen = t.seen;
        }
        const auto recs = parseCaptures(bytes, seen);
        std::lock_guard<std::mutex> lock(t.m);
        t.seen = seen;
        for (const CaptureRecord& r : recs) {
            lc.log("[capture] " + r.describe());
            t.records.push_front(r);
        }
        while (t.records.size() > 200) t.records.pop_back();
    });
}

bool captureQuery(Ctx& c) {
    for (const CaptureSite& site : captureSites())
        if (!c.rt().hooked(site.site)) return false;
    return true;
}

std::string itemOf(const CaptureRecord& r) {
    uint16_t def = 0xFFFF;
    if (r.op == 402) def = uint16_t(r.r[2]);
    if (r.op == 405) def = uint16_t(r.r[3]);
    if (const auto s = current())
        if (const Item* it = s->find(r.soid)) return itemName(*it, catalog());
    if (def != 0xFFFF)
        if (const ItemDef* d = catalog().item(def)) return d->label();
    return r.soid ? hex16(r.soid) : std::string("-");
}

void drawCapture(Ui&) {
    CaptureState& s = cap();
    std::deque<CaptureRecord> recs;
    {
        std::lock_guard<std::mutex> lock(s.m);
        recs = s.records;
    }
    if (!recs.empty()) {
        if (button("Copy All", {.tone = Tone::Ghost, .icon = icon::Copy, .small = true})) {
            std::string all;
            for (auto it = recs.rbegin(); it != recs.rend(); ++it) all += it->describe() + "\n";
            copyToClipboard(all, "captures");
        }
        ImGui::SameLine(0, px(6));
        if (button("Clear", {.tone = Tone::Ghost, .icon = icon::Delete, .small = true})) {
            std::lock_guard<std::mutex> lock(s.m);
            s.records.clear();
        }
        gap(sp::S);
    }
    if (recs.empty()) {
        wrapped(s.on.load() ? "Armed. Now transfer, equip, unequip, use or dismantle something in the game's own menus: each request shows up here."
                            : "Switch it on, then use the game's own menus: every request the game builds (402 item action, 403 / 404 equip, 405 transfer, "
                              "904 vendor claim) is recorded with its arguments, without changing it.",
                pal::Faint, type::Small);
        return;
    }
    if (beginTable("##caps", 4, std::min(320.0f, 36.0f + 30.0f * float(recs.size())), ImGuiTableFlags_BordersOuter)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, px(34));
        ImGui::TableSetupColumn("Op", ImGuiTableColumnFlags_WidthFixed, px(40));
        ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthFixed, px(170));
        ImGui::TableSetupColumn("Request", ImGuiTableColumnFlags_WidthStretch);
        tableHeaders();
        for (const CaptureRecord& r : recs) {
            ImGui::TableNextRow(0, px(30));
            ImGui::TableSetColumnIndex(0);
            cellText(std::to_string(r.sequence).c_str(), pal::Faint, FontKind::Body, type::Small, 30);
            ImGui::TableSetColumnIndex(1);
            cellText(std::to_string(r.op).c_str(), pal::Accent, FontKind::Mono, type::MonoSmall, 30);
            ImGui::TableSetColumnIndex(2);
            cellText(itemOf(r).c_str(), pal::Text, FontKind::Body, type::Small, 30);
            ImGui::TableSetColumnIndex(3);
            ImGui::PushID(int(r.sequence));
            const std::string d = r.describe();
            const float y = ImGui::GetCursorPosY();
            if (ImGui::InvisibleButton("##copy", ImVec2(availWidth(), px(26)))) copyToClipboard(d, "request");
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s\n(click to copy)", d.c_str());
            ImGui::SetCursorPosY(y);
            cellText(d.c_str(), pal::Muted, FontKind::Mono, type::MonoSmall, 30);
            ImGui::PopID();
        }
        endTable();
    }
}

void resetChecks() {
    cap().on = false;
    std::lock_guard<std::mutex> lock(cap().m);
    cap().buffer = 0;
}

}   

}   

namespace brps3 {

BR_FEATURES(inventory_checks) {
    using namespace invui;
    auto probe = [&](const char* id, const char* label, const char* help, void (*run)(Ctx&)) {
        r.action({.id = id, .page = Page::Testing, .tab = kPortChecksTab, .section = kProbeSection, .label = label, .help = help, .run = run,
                  .badge = kBadgeNeedsCheck});
    };
    probe("inventory.check_snapshot", "Inventory Snapshot",
          "Reads a full coherent snapshot and logs the manager, profiles, descriptor (state 3, version), provider mode, followed tag references, "
          "definition count (expect 6557) and row counts. Read-only.",
          probeSnapshot);
    probe("inventory.check_rows", "Character Rows and Equipped List",
          "Logs the 20 equipped SOIDs (character +0x2900) with names and every weapon / armor row (character +0x38). Expect exactly what your "
          "character wears and carries. Read-only.",
          probeCharacterRows);
    probe("inventory.check_details", "Detail Rows and Rolls",
          "Logs the predicted perks of each equipped item from its 168-byte detail row. Expect the perks the game shows (locked ones included). Read-only.",
          probeDetailRows);
    probe("inventory.check_gate", "Request Gate (0x1CB82C / 0x475D50)",
          "Calls the two read-only getters every inventory wrapper checks: provider non-zero and request state 3 mean requests can be sent now.",
          probeGate);
    probe("inventory.check_postmaster", "Postmaster and Three of Coins Flag",
          "Logs the postmaster registry (character +0x3BD8), the Three of Coins stack and the unverified +0x549A byte. Use one coin and run it again: "
          "expect one coin less and bit 0x02 set. Read-only.",
          probePostmaster);
    probe("inventory.check_editor", "Editor Profiles and Art Tags",
          "Matches the orbit-cache and gameplay profiles to the equipped list and checks that the catalogue's model tags reproduce the live ones "
          "(that proves the art tag ids are the PS3 ones). Read-only.",
          probeEditor);
    probe("inventory.check_tables", "Native Catalogue and Transfer Tables",
          "Investment interface and methods (vtable -> OPD -> code), tag caches, the bucket table and the row-26 transfer pairs against the bundled "
          "tables, the definition hashes against the item catalogue. Read-only.",
          probeTables);
    probe("inventory.check_dryrun", "Dry Run: Selected Item",
          "For the item selected on Vault > Inventory: the preflight, the exact wrapper arguments and every same-frame check each allowed action "
          "would use. Nothing is sent.",
          probeDryRun);
    r.section({.id = "inventory.verify", .page = Page::Testing, .tab = kDevTab, .section = "Native catalog verification", .order = 10, .draw = drawVerify,
               .badge = kBadgeDev});
    r.action({.id = "inventory.verify_run", .page = Page::Testing, .tab = kDevTab, .section = "Native catalog verification",
              .label = "Verify Native Catalogue",
              .help = "Read-only: investment interface and methods, tag caches, bucket and transfer tables, definition hashes. Results on TESTING > Inventory.",
              .run = runVerify, .badge = kBadgeDev});
    r.toggle({.id = "inventory.capture", .page = Page::Testing, .tab = kDevTab, .section = "Request capture", .label = "Capture Inventory Requests",
              .help = "Hooks the builder call inside the 402 / 403 / 404 / 405 / 904 wrappers and records what the game sends (it never sends anything "
                      "itself). Restored by turning it off, Panic or disconnecting.",
              .set = captureSet, .query = captureQuery, .badge = kBadgeDev});
    r.section({.id = "inventory.capture_list", .page = Page::Testing, .tab = kDevTab, .section = "Request capture", .order = 20, .draw = drawCapture,
               .onDisconnect = resetChecks, .badge = kBadgeDev});
    ui::styleSection("Native catalog verification",
                     {.icon = ui::icon::Shield,
                      .ownsActions = true});
    ui::styleSection("Request capture", {.icon = ui::icon::Record});
}

}   
