 
 
 
 
 
 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "app/engine.h"
#include "app/registry.h"
#include "app/settings.h"
#include "core/error.h"
#include "core/runtime.h"
#include "features/loot_common.h"
#include "game/loot_addr.h"
#include "game/loot_grimoire.h"
#include "ui/widgets.h"

namespace brps3::lootui {

using namespace ui;
using namespace loot;

namespace {

constexpr const char* kSection = "Grimoire cards";
constexpr const char* kReadJob = "Grimoire: Read Progress";
constexpr const char* kUnlockJob = "Grimoire: unlock cards";
constexpr const char* kStubName = "loot.grimoire.v1";
constexpr float kRowH = 30;

struct State {
    std::mutex m;
    bool known = false;
    std::vector<uint8_t> values;         
    uint32_t unlocked = 0;
    std::string identity, note;
    double readAt = 0;
     
    bool running = false;
    uint32_t done = 0, total = 0;
    std::string current, result;
    bool resultOk = true;
    std::set<uint16_t> session;          
    std::string error;
};
State& st() {
    static State s;
    return s;
}

struct Session {
    bool verified = false;
    uint32_t block = 0;
};

const grim::Catalog& cat() { return grim::catalog(); }

void prepare(Ctx& c, Session& s) {
    c.require(kGrimoireFingerprints, "Grimoire unlock (native flag setter)");
    RuntimeConsole con(c.rt());
    grim::verifyContextClass(con);
    if (!s.verified) {
        Resolver res(con);
        grim::verifyTables(con, res, cat());
        s.verified = true;
        c.log("[grimoire] tables verified (core and unlock-flag SHA-256 match the 862-flag catalogue)");
    }
}

void store(const grim::Profile& p) {
    State& S = st();
    std::lock_guard<std::mutex> lock(S.m);
    S.known = true;
    S.values = p.values;
    S.unlocked = p.unlocked;
    S.identity = p.identityHex();
    S.note = p.note;
    S.readAt = now();
    S.error.clear();
}

void readJob(Ctx& c) {
    if (!cat().error.empty()) throw Error(cat().error);
    Session& s = c.state<Session>("loot.grimoire.session");
    try {
        prepare(c, s);
        RuntimeConsole con(c.rt());
        const grim::Profile p = grim::readProfile(con, cat());
        store(p);
        feed("Grimoire", fmt("Read progress: %u / %zu cards unlocked", p.unlocked, cat().flags.size()));
        c.log(fmt("[grimoire] %u / %zu cards unlocked (account %s)", p.unlocked, cat().flags.size(), p.identityHex().c_str()));
        if (!p.note.empty()) c.log("[grimoire] " + p.note);
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(st().m);
        st().error = e.what();
        throw;
    }
}

void unlockJob(Ctx& c, std::vector<uint16_t> ordinals) {
    if (!cat().error.empty()) throw Error(cat().error);
    State& S = st();
    Session& s = c.state<Session>("loot.grimoire.session");
    {
        std::lock_guard<std::mutex> lock(S.m);
        S.running = true;
        S.done = 0;
        S.total = 0;
        S.current = "Checking the game code and tables...";
        S.result.clear();
    }
    struct Done {
        ~Done() {
            std::lock_guard<std::mutex> lock(st().m);
            st().running = false;
        }
    } done;
    auto finish = [&](const std::string& text, bool ok) {
        feed("Grimoire", text, ok);
        std::lock_guard<std::mutex> lock(S.m);
        S.result = text;
        S.resultOk = ok;
    };
    try {
        prepare(c, s);
        RuntimeConsole con(c.rt());
        grim::Profile before = grim::readProfile(con, cat());
        store(before);
        std::vector<size_t> pending;
        for (uint16_t o : ordinals)
            for (size_t i = 0; i < cat().flags.size(); ++i)
                if (cat().flags[i].ordinal == o && !before.isUnlocked(i)) pending.push_back(i);
        {
            std::lock_guard<std::mutex> lock(S.m);
            S.total = uint32_t(pending.size());
        }
        if (pending.empty()) {
            finish("Every selected card already reads back as unlocked.", true);
            c.toast("Grimoire: nothing to unlock");
            return;
        }
        if (!s.block) s.block = c.rt().alloc(grim::kBatch * 4 + 16);
        const uint32_t arr = s.block, out = s.block + grim::kBatch * 4;
        const uint32_t stub = c.rt().sharedCode(kStubName, [](uint32_t base) { return grim::stubProgram(base); }, grim::kStubSize);
        uint32_t completed = 0;
        for (size_t at = 0; at < pending.size(); at += grim::kBatch) {
            if (c.cancelled()) {
                finish(fmt("Stopped after %u of %zu cards; completed unlocks are kept.", completed, pending.size()), true);
                break;
            }
             
            grim::recheck(con, before);
            const size_t n = std::min<size_t>(grim::kBatch, pending.size() - at);
            std::vector<uint8_t> ords(n * 4);
            for (size_t k = 0; k < n; ++k) {
                const uint32_t o = cat().flags[pending[at + k]].ordinal;
                ords[k * 4] = uint8_t(o >> 24);
                ords[k * 4 + 1] = uint8_t(o >> 16);
                ords[k * 4 + 2] = uint8_t(o >> 8);
                ords[k * 4 + 3] = uint8_t(o);
            }
            con.write(arr, ords);
            con.write(out, std::vector<uint8_t>(16, 0));
            {
                std::lock_guard<std::mutex> lock(S.m);
                S.current = cat().flags[pending[at]].name;
            }
             
            c.rt().call(stub, {arr, n, out, before.account}, {}, 5000);
            const world::Bytes res = con.bytes(out, 12);
            if (res.u32(0) == grim::kOutNoContext) throw Error("The native account context was unavailable; no flag written (load your character)");
            if (res.u32(0) == grim::kOutAccount)
                throw Error(fmt("The investment context holds account %s, not the profile's %s; no flag written", hex(res.u32(8)).c_str(),
                                hex(before.account).c_str()));
            if (res.u32(0) != grim::kOutDone || res.u32(4) != n) throw Error(fmt("Unexpected batch result %u (%u calls)", res.u32(0), res.u32(4)));
            if (res.u32(8) != before.account) throw Error("The native account changed; batch stopped");
            const std::vector<uint8_t> values = grim::readValues(con, before, cat());
            for (size_t k = 0; k < n; ++k) {
                const size_t i = pending[at + k];
                if (values[i] != GRIM_UNLOCKED) throw Error("Card did not read back as unlocked: " + cat().flags[i].name);
            }
            completed += uint32_t(n);
            {
                std::lock_guard<std::mutex> lock(S.m);
                S.done = completed;
                S.values = values;
                S.unlocked = 0;
                for (uint8_t v : values) S.unlocked += v == GRIM_UNLOCKED ? 1 : 0;
                for (size_t k = 0; k < n; ++k) S.session.insert(cat().flags[pending[at + k]].ordinal);
            }
        }
        const grim::Profile after = grim::readProfile(con, cat());
        if (after.identityHex() != before.identityHex()) throw Error("The account changed during the final readback");
        store(after);
        {
            std::lock_guard<std::mutex> lock(S.m);
            if (S.result.empty())
                S.result = fmt("Unlocked %u card%s: %u / %zu read back as unlocked. Persistence after a reload is not verified yet.", completed,
                               completed == 1 ? "" : "s", after.unlocked, cat().flags.size());
        }
        c.toast(fmt("Grimoire: %u card%s unlocked", completed, completed == 1 ? "" : "s"));
        {
            std::string r;
            {
                std::lock_guard<std::mutex> lock(S.m);
                r = S.result;
            }
            feed("Grimoire", r);
        }
        c.log("[grimoire] " + st().result);
    } catch (const std::exception& e) {
        finish(std::string("Stopped: ") + e.what(), false);
        throw;
    }
}

std::vector<uint16_t> lockedOrdinals() {
    State& S = st();
    std::lock_guard<std::mutex> lock(S.m);
    std::vector<uint16_t> out;
    for (size_t i = 0; i < cat().flags.size(); ++i)
        if (!S.known || i >= S.values.size() || S.values[i] != GRIM_UNLOCKED) out.push_back(cat().flags[i].ordinal);
    return out;
}

void unlockAllAction(Ctx& c) { unlockJob(c, lockedOrdinals()); }

 

struct View {
    std::string search;
    int filter = 0;                     
    std::set<uint16_t> selected;
    std::vector<size_t> rows;
    bool dirty = true;
    uint64_t seenUnlocked = ~0ull;
};
View& vw() {
    static View v;
    return v;
}

void seedDemo() {
    static bool seeded = false;
    if (seeded || !demo() || !cat().error.empty()) return;
    seeded = true;
    State& S = st();
    std::lock_guard<std::mutex> lock(S.m);
    S.known = true;
    S.values.assign(cat().flags.size(), 0);
    for (size_t i = 0; i < S.values.size(); ++i)
        if (i % 3 == 0 || i < 120) S.values[i] = GRIM_UNLOCKED;
    S.unlocked = 0;
    for (uint8_t v : S.values) S.unlocked += v == GRIM_UNLOCKED ? 1 : 0;
    S.identity = "0123456789ABCDEF";
    S.readAt = now() - 42;
    S.running = true;
    S.done = 48;
    S.total = 112;
    S.current = cat().flags[200].name;
    for (size_t i = 120; i < 168; ++i) S.session.insert(cat().flags[i].ordinal);
    vw().selected = {cat().flags[121].ordinal, cat().flags[125].ordinal, cat().flags[300].ordinal};
}

void rebuild() {
    View& v = vw();
    State& S = st();
    std::vector<uint8_t> values;
    std::set<uint16_t> session;
    {
        std::lock_guard<std::mutex> lock(S.m);
        values = S.values;
        session = S.session;
    }
    v.rows.clear();
    const std::vector<size_t> hits = cat().search(v.search);
    for (size_t i : hits) {
        const bool un = i < values.size() && values[i] == GRIM_UNLOCKED;
        const uint16_t o = cat().flags[i].ordinal;
        if (v.filter == 1 && un) continue;
        if (v.filter == 2 && !un) continue;
        if (v.filter == 3 && !v.selected.count(o)) continue;
        if (v.filter == 4 && !session.count(o)) continue;
        v.rows.push_back(i);
    }
    v.dirty = false;
}

void postUnlock(std::vector<uint16_t> ords, const std::string& what) {
    if (ords.empty()) {
        notify("Nothing to unlock", Level::Info);
        return;
    }
    const size_t n = ords.size();
    bool known;
    {
        std::lock_guard<std::mutex> lock(st().m);
        known = st().known;
    }
    confirm("Unlock Grimoire Cards",
            fmt("Unlock %s%zu %s on the loaded account? Cards already unlocked are skipped. Unlocks are written to the account profile "
                "and cannot be undone from here.",
                known ? "" : "up to ", n, what.c_str()),
            fmt("Unlock %zu", n), [ords] {
                noteQueued(kUnlockJob);
                Engine::get().post(kUnlockJob, [ords](Ctx& c) { unlockJob(c, ords); });
            },
            false);
}

void drawGrimoire(Ui& ui) {
    seedDemo();
    if (!cat().error.empty()) {
        emptyState(icon::Warning, "Grimoire Catalogue Unavailable", cat().error.c_str(), 180);
        return;
    }
    State& S = st();
    View& v = vw();
    bool known, running, resultOk;
    uint32_t unlocked, done, total;
    std::string identity, note, current, result, error;
    double readAt;
    size_t sessionCount;
    {
        std::lock_guard<std::mutex> lock(S.m);
        known = S.known;
        running = S.running;
        unlocked = S.unlocked;
        done = S.done;
        total = S.total;
        identity = S.identity;
        note = S.note;
        current = S.current;
        result = S.result;
        resultOk = S.resultOk;
        error = S.error;
        readAt = S.readAt;
        sessionCount = S.session.size();
    }
    const size_t all = cat().flags.size();
     
    static bool autoRead = false;
    if (!ui.connected()) autoRead = false;
    else if (!autoRead && !known && !running && !ui.busy(kReadJob)) {
        autoRead = true;
        ui.post(kReadJob, readJob);
    }
    if (unlocked != v.seenUnlocked) {
        v.seenUnlocked = unlocked;
        v.dirty = true;
    }
     
    const float avail = availWidth();
    const int cols = avail > px(700) ? 4 : 2;
    const float w = std::floor((avail - px(8) * float(cols - 1)) / float(cols));
    statTile("unl", "UNLOCKED", known ? fmt("%u / %zu", unlocked, all) : std::string("-"), known ? fmt("%.0f%% of the whitelist", 100.0 * unlocked / double(all)) : "read progress first",
             w, known ? (unlocked == all ? pal::Success : pal::Accent) : pal::Muted);
    ImGui::SameLine(0, px(8));
    statTile("lck", "LOCKED", known ? grouped(all - unlocked) : std::string("-"), "cards, ranks and silent unlocks", w);
    if (cols == 2) gap(px(8) / scale());
    else ImGui::SameLine(0, px(8));
    statTile("acc", "ACCOUNT", known ? identity.substr(0, 8) + "\xE2\x80\xA6" : std::string("-"), "identity checked before every batch", w);
    ImGui::SameLine(0, px(8));
    statTile("ses", "THIS SESSION", grouped(sessionCount), known ? "read " + duration(now() - readAt) + " ago" : std::string("unlocked by Blackrock"), w);
    gap(sp::M);
    if (known && !running) {
        progressBar(float(unlocked) / float(all), 0, 6);
        gap(sp::M);
    }
     
    {
        auto offline = requireConnection(ui);
        if (running) {
            const float f = total ? float(done) / float(total) : -1.0f;
            ImGui::BeginGroup();
            labelf("Unlocking %u / %u", done, total);
            caption(current.c_str(), pal::Muted);
            ImGui::EndGroup();
            ImGui::SameLine();
            alignRight(buttonWidth("Stop", icon::Stop));
            if (button("Stop", {.tone = Tone::Danger, .icon = icon::Stop, .tooltip = "Stop between batches (completed unlocks stay)"})) Engine::get().cancel();
            gap(sp::S);
            progressBar(f, 0, 5);
            requestRedraw();
        } else {
            if (button("Read Progress", {.icon = icon::Refresh, .tooltip = "Read the loaded account's Grimoire flags (read only)", .busy = ui.busy(kReadJob)}))
                ui.post(kReadJob, readJob);
            ImGui::SameLine(0, px(8));
            const std::vector<uint16_t> locked = lockedOrdinals();
            if (button(known ? fmt("Unlock All Remaining (%zu)", locked.size()).c_str() : "Unlock All Remaining",
                       {.tone = Tone::Primary, .icon = icon::Check, .tooltip = "Every whitelisted card that is still locked (asks first)"}))
                postUnlock(locked, "Grimoire cards");
            ImGui::SameLine(0, px(8));
            {
                DisabledScope none(v.selected.empty(), "Tick cards in the list below first");
                if (button(fmt("Unlock Selected (%zu)", v.selected.size()).c_str(), {.icon = icon::Check}))
                    postUnlock(std::vector<uint16_t>(v.selected.begin(), v.selected.end()), "selected cards");
            }
        }
    }
    if (!result.empty()) {
        gap(sp::S);
        checkRow(result.c_str(), resultOk ? Mark::Ok : Mark::Bad);
    }
    if (!error.empty()) checkRow(error.c_str(), Mark::Bad);
    if (!note.empty()) checkRow(note.c_str(), Mark::Waiting);
    gap(sp::M);
     
    if (searchField("##gsearch", &v.search, "Search Cards (Name or #Ordinal)", std::min(px(340), avail * 0.4f) / scale(), true)) v.dirty = true;
    ImGui::SameLine(0, px(10));
    std::vector<std::string> filters = {"All", "Locked", "Unlocked", v.selected.empty() ? std::string("Selected") : "Selected  " + std::to_string(v.selected.size()),
                                        "This Session"};
    if (pillTabs("##gfilter", filters, &v.filter)) v.dirty = true;
    if (v.dirty) rebuild();
    ImGui::SameLine();
    const float selW = buttonWidth("Select Shown", icon::Check, true) + buttonWidth("Clear", icon::Close, true) + px(6);
    alignRight(selW);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(3));
    if (button("Select Shown", {.tone = Tone::Ghost, .icon = icon::Check, .small = true, .tooltip = "Tick every card in the list below"})) {
        for (size_t i : v.rows) v.selected.insert(cat().flags[i].ordinal);
        v.dirty = v.filter == 3;
    }
    ImGui::SameLine(0, px(6));
    if (button("Clear", {.tone = Tone::Ghost, .icon = icon::Close, .small = true, .tooltip = "Untick every card"})) {
        v.selected.clear();
        v.dirty = true;
    }
    gap(sp::S);
    std::vector<uint8_t> values;
    std::set<uint16_t> session;
    {
        std::lock_guard<std::mutex> lock(S.m);
        values = S.values;
        session = S.session;
    }
    const float tableH = std::clamp(ImGui::GetWindowHeight() - px(300), px(260), px(560));
    if (beginTable("##cards", 4, tableH / scale(), ImGuiTableFlags_BordersOuter)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##sel", ImGuiTableColumnFlags_WidthFixed, px(26));
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, px(110));
        ImGui::TableSetupColumn("Card", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Ordinal", ImGuiTableColumnFlags_WidthFixed, px(70));
        tableHeaders();
        ImGuiListClipper clip;
        clip.Begin(int(v.rows.size()), px(kRowH));
        int toggle = -1;
        while (clip.Step()) {
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
                const size_t i = v.rows[size_t(r)];
                const grim::Flag& f = cat().flags[i];
                ImGui::TableNextRow(0, px(kRowH));
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(int(i));
                const float cellY = ImGui::GetCursorPosY();
                const bool sel = v.selected.count(f.ordinal) != 0;
                if (tableRowSelectable("##row", sel, nullptr, kRowH - 2 * ImGui::GetStyle().CellPadding.y / scale())) toggle = int(i);
                ImGui::SetCursorPosY(cellY);
                 
                const ImVec2 bp = ImGui::GetCursorScreenPos() + ImVec2(px(2), px(6));
                ImDrawList* d = ImGui::GetWindowDrawList();
                d->AddRect(bp, bp + ImVec2(px(14), px(14)), u32(sel ? pal::Accent : pal::Decor), px(3), 0, px(1.2f));
                if (sel) d->AddRectFilled(bp + ImVec2(px(3), px(3)), bp + ImVec2(px(11), px(11)), u32(pal::Accent), px(2));
                ImGui::PopID();
                const bool known1 = i < values.size();
                const bool un = known1 && values[i] == GRIM_UNLOCKED;
                ImGui::TableSetColumnIndex(1);
                const char* status = !known1 ? "Unknown" : un ? (session.count(f.ordinal) ? "Unlocked now" : "Unlocked") : "Locked";
                cellText(status, !known1 ? pal::Faint : un ? pal::Success : pal::Muted, FontKind::Body, type::Small, kRowH);
                ImGui::TableSetColumnIndex(2);
                cellText(f.name.c_str(), un ? pal::Text : pal::Soft, FontKind::Body, type::Label, kRowH);
                ImGui::TableSetColumnIndex(3);
                cellText(std::to_string(f.ordinal).c_str(), pal::Faint, FontKind::Mono, type::MonoSmall, kRowH);
            }
        }
        if (v.rows.empty()) {
            ImGui::TableNextRow(0, px(90));
            ImGui::TableSetColumnIndex(2);
            gap(sp::L);
            caption(v.filter == 3 ? "Nothing selected: click cards to tick them." : "No Card Matches", pal::Muted);
        }
        endTable();
        if (toggle >= 0) {
            const uint16_t o = cat().flags[size_t(toggle)].ordinal;
            if (v.selected.count(o)) v.selected.erase(o);
            else v.selected.insert(o);
            if (v.filter == 3) v.dirty = true;
        }
    }
}

void resetOnDisconnect() {
    State& S = st();
    std::lock_guard<std::mutex> lock(S.m);
    S.running = false;
}

}   

Summary grimoireSummary() {
    seedDemo();
    Summary s;
    State& S = st();
    std::lock_guard<std::mutex> lock(S.m);
    const size_t all = cat().flags.size();
    if (S.running) {
        s.active = true;
        s.color = pal::Accent;
        s.value = fmt("%u / %u", S.done, S.total);
        s.sub = "Unlocking: " + S.current;
    } else if (S.known) {
        s.value = fmt("%u / %zu", S.unlocked, all);
        s.color = S.unlocked == all ? pal::Success : pal::Text;
        s.sub = S.session.empty() ? "cards unlocked on this account" : fmt("%zu unlocked this session", S.session.size());
    } else {
        s.value = "-";
        s.sub = fmt("%zu cards: read progress", all);
    }
    return s;
}

}   

namespace brps3 {

BR_FEATURES(loot_grimoire) {
    using namespace lootui;
    r.section({.id = "loot.grimoire", .page = Page::Loot, .tab = kTabGrimoire, .section = kSection, .order = 10, .draw = drawGrimoire,
               .onDisconnect = resetOnDisconnect});
    r.action({.id = "loot.grimoire.read", .page = Page::Loot, .tab = kTabGrimoire, .section = kSection, .label = "Grimoire: Read Progress",
              .help = "Read how many of the 862 whitelisted Grimoire cards the loaded account has (read only).", .run = readJob});
    r.action({.id = "loot.grimoire.unlock_all", .page = Page::Loot, .tab = kTabGrimoire, .section = kSection,
              .label = "Grimoire: Unlock All Remaining Cards",
              .help = "Unlock every whitelisted Grimoire card that is still locked on the loaded account (checks each one reads back).",
              .run = unlockAllAction, .dangerous = true});
    ui::styleSection(kSection, {.fullWidth = true,
                                .icon = ui::icon::Star,
                                .ownsActions = true});
}

}   
