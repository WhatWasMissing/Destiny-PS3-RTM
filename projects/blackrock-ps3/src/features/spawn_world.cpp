 
 
 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "app/engine.h"
#include "app/registry.h"
#include "core/error.h"
#include "features/spawn_shared.h"
#include "features/spawn_ui.h"
#include "game/names_catalog.h"
#include "game/spawn_catalog.h"
#include "game/world_tags.h"
#include "ui/widgets.h"

namespace brps3::spawnui {
namespace {

using namespace ui;
namespace W = spawn::wtl;

struct WorldPage {
     
    std::string checkText = "80C7A2F0";
     
    std::string pkgFilter;
    uint32_t openPage = 0;
     
    std::string wtlText;
    bool wtlInit = false;
    std::string rscText;
};

WorldPage& wp() {
    static WorldPage p;
    return p;
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string hex(uint32_t v) { return world::hex8(v); }

 
const std::map<uint32_t, std::string>& packagesByPage() {
    static std::map<uint32_t, std::string> m;
    static bool built = false;
    if (!built) {
        built = true;
        for (const names::Entity& e : names::entities().all()) m.emplace(world::tagPage(e.tag), e.package);
    }
    return m;
}

const std::map<uint32_t, std::vector<uint32_t>>& catalogByPage() {
    static std::map<uint32_t, std::vector<uint32_t>> m;
    static bool built = false;
    if (!built) {
        built = true;
        for (const spawn::CatalogRow& r : spawn::catalog().rows()) m[world::tagPage(r.tag)].push_back(r.tag);
    }
    return m;
}

void kvRow(const char* k, const std::string& v, const ImVec4& color = pal::Text, bool monoValue = true) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = px(22);
    ImDrawList* d = ImGui::GetWindowDrawList();
    drawText(d, FontKind::Body, type::Small, ImVec2(p.x, textTop(FontKind::Body, type::Small, p.y + h * 0.5f)), u32(pal::Faint), k);
    const FontKind f = monoValue ? FontKind::Mono : FontKind::Body;
    const float em = monoValue ? type::MonoSmall : type::Small;
    const std::string cut = ellipsize(v.c_str(), availWidth() - px(118), f, em);
    drawText(d, f, em, ImVec2(p.x + px(118), textTop(f, em, p.y + h * 0.5f)), u32(color), cut.c_str());
    ImGui::Dummy(ImVec2(availWidth(), h));
}

 
 
 

void drawResidency(Ui& ui) {
    seedDemo();
    const ResidencyView& rv = residency();
    size_t scanDone, scanTotal;
    bool scanning;
    {
        State& s = st();
        std::lock_guard<std::mutex> lock(s.m);
        scanDone = s.scanDone;
        scanTotal = s.scanTotal;
        scanning = s.scanning.load();
    }
    const float avail = availWidth();
    const float w = std::floor((avail - px(16)) / 3.0f);
    statTile("pages", "Package Pages", rv.pagesAt > 0 ? std::to_string(rv.livePages.size()) : "-",
             rv.pagesAt > 0 ? std::to_string(rv.registeredPages) + " pages registered" : "not read yet", w,
             rv.pagesAt > 0 ? pal::Text : pal::Faint);
    ImGui::SameLine(0, px(8));
    statTile("ents", "Loaded Entities", rv.entriesAt > 0 ? std::to_string(rv.resident.size()) : "-",
             rv.entriesAt > 0 ? "from the last scan" : "run a scan", w, rv.entriesAt > 0 ? pal::Success : pal::Faint);
    ImGui::SameLine(0, px(8));
    const double last = std::max(rv.pagesAt, rv.entriesAt);
    statTile("when", "Last Read", last > 0 ? ago(last) : "-", last > 0 ? "re-read after travelling" : "", w,
             last > 0 ? pal::Text : pal::Faint);
    gap(sp::M);
    {
        auto offline = requireConnection(ui);
        if (button("Refresh Pages", {.icon = icon::Refresh,
                                     .tooltip = "Which content packages are loaded (one 64 KiB read of the tag table). A loaded package does not mean "
                                                "all its entries are; pages below 0x400 are runtime pools, not content.",
                                     .busy = ui.busy(kJobPages)}))
            post(kJobPages, [](Ctx& c) { refreshPages(c); });
        ImGui::SameLine(0, px(8));
        if (scanning) {
            if (button("Stop Scan", {.tone = Tone::Danger, .icon = icon::Stop, .tooltip = "Stop after the current package", .spinner = true}))
                Engine::get().cancel();
        } else if (button("Scan Loaded Entities", {.tone = Tone::Primary, .icon = icon::Search,
                                                    .tooltip = "Read every loaded package's item array (128 KiB each) and list the entity definitions "
                                                               "that are loaded. Takes a few seconds; the catalogue's dots and Loaded filter use it.",
                                                    .busy = ui.busy(kJobScan)})) {
            post(kJobScan, [](Ctx& c) { scanEntries(c); }, [](bool ok, const std::string&) {
                if (ok) notify(std::to_string(residency().resident.size()) + " loaded entities", Level::Good);
            });
        }
    }
    if (scanning) {
        gap(sp::M);
        progressBar(scanTotal ? float(scanDone) / float(scanTotal) : -1.0f, 0, 5);
        gap(sp::XS);
        char buf[64];
        std::snprintf(buf, sizeof buf, "Package %zu of %zu", scanDone, scanTotal);
        caption(buf, pal::Muted);
        requestRedraw();
    }
}

void drawTagCheck(Ui& ui) {
    WorldPage& p = wp();
    fieldLabel("Tag");
    gap(sp::XS + 2);
    const float bw = buttonWidth("Check", icon::Search) + buttonWidth("Selected", nullptr, true) + px(16);
    inputMono("##ctag", &p.checkText, "80C7A2F0", (availWidth() - bw) / scale());
    uint32_t tag = 0;
    const bool valid = spawn::parseTag(p.checkText, tag);
    ImGui::SameLine(0, px(8));
    {
        auto offline = requireConnection(ui);
        DisabledScope bad(!valid, "Enter a tag (8 hex digits)");
        if (button("Check", {.tone = Tone::Primary, .icon = icon::Search, .tooltip = "Resolve it like tag_ref_resolve, with every validity check",
                             .busy = ui.busy(kJobCheck)}))
            post(kJobCheck, [tag](Ctx& c) { checkTag(c, tag); });
    }
    ImGui::SameLine(0, px(8));
    if (button("Selected", {.tone = Tone::Ghost, .small = true, .tooltip = "Use the catalogue selection"})) {
        if (const uint32_t s = selectedTag()) p.checkText = spawn::tagText(s);
    }
    gap(sp::M);
    TagCheck k;
    bool have = false;
    {
        State& s = st();
        std::lock_guard<std::mutex> lock(s.m);
        auto it = valid ? s.checks.find(tag) : s.checks.end();
        if (it != s.checks.end()) {
            k = it->second;
            have = true;
        }
    }
    if (!valid) {
        caption("Type a tag, or press Selected.", pal::Faint);
        return;
    }
    const uint32_t ps3 = names::toPs3Tag(tag);
    kvRow("Name", displayName(tag), pal::Text, false);
    char buf[64];
    std::snprintf(buf, sizeof buf, "page %03X  \xC2\xB7  index %03X", world::tagPage(ps3), world::tagIndex(ps3));
    kvRow("Decodes to", buf, pal::Muted);
    if (const auto& pk = packagesByPage(); pk.count(world::tagPage(ps3))) kvRow("Package", pk.at(world::tagPage(ps3)), pal::Muted, false);
    if (!have) {
        gap(sp::S);
        caption("Not checked yet this session.", pal::Faint);
        return;
    }
    gap(sp::S);
    pill(k.resident ? "Loaded" : "Not Loaded", k.resident ? pal::Success : pal::Muted, k.resident ? icon::Check : icon::Info);
    ImGui::SameLine(0, px(8));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(4));
    caption(ago(k.at).c_str(), pal::Faint);
    gap(sp::S);
    if (k.resident) {
        std::snprintf(buf, sizeof buf, "%u  (%s)", unsigned(k.type), spawn::categoryName(k.type));
        kvRow("Live Type", buf);
        kvRow("Item", hex(k.element), pal::Muted);
        kvRow("Body", hex(k.body), pal::Muted);
        kvRow("Definition", hex(k.definition), pal::Muted);
    } else {
        wrapped(k.why.c_str(), pal::Muted, type::Small);
    }
}

void drawPackages(Ui& ui) {
    WorldPage& p = wp();
    const ResidencyView& rv = residency();
    if (rv.livePages.empty()) {
        emptyState(icon::Loot, "No Package List Yet",
                   ui.connected() ? "Refresh Pages to List the Loaded Packages"
                                  : "Connect, Then Refresh Pages",
                   140);
        return;
    }
    searchField("##pkgs", &p.pkgFilter, "Search Package or Page", std::min(320.0f, availWidth() / scale() * 0.4f));
    gap(sp::S);
    const auto& pk = packagesByPage();
    const auto& cat = catalogByPage();
    const auto words = spawn::queryWords(p.pkgFilter);
    std::vector<uint32_t> pages;
    for (uint32_t page : rv.livePages) {
        auto it = pk.find(page);
        const std::string name = it != pk.end() ? it->second : std::string("(not in the catalogue)");
        char hexPage[16];
        std::snprintf(hexPage, sizeof hexPage, "%03x", page);
        if (!words.empty() && !spawn::matches(lower(name + " " + hexPage), words)) continue;
        pages.push_back(page);
    }
    const float h = std::min(460.0f, 36.0f + float(std::max<size_t>(pages.size(), 1)) * 34.0f);
    if (beginTable("##pages", 5, h, ImGuiTableFlags_BordersOuter)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Page", ImGuiTableColumnFlags_WidthFixed, px(56));
        ImGui::TableSetupColumn("Package", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Catalogued", ImGuiTableColumnFlags_WidthFixed, px(90));
        ImGui::TableSetupColumn("Loaded", ImGuiTableColumnFlags_WidthFixed, px(70));
        ImGui::TableSetupColumn("##open", ImGuiTableColumnFlags_WidthFixed, px(34));
        tableHeaders();
        ImGui::PushFont(font(FontKind::Body), fontSize(FontKind::Body, type::Label));
        for (uint32_t page : pages) {
            ImGui::TableNextRow(0, px(34));
            ImGui::PushID(int(page));
            ImGui::TableSetColumnIndex(0);
            const float y = ImGui::GetCursorPosY();
            const bool open = p.openPage == page;
            if (tableRowSelectable("##row", open, nullptr, 34 - 2 * ImGui::GetStyle().CellPadding.y / scale())) p.openPage = open ? 0 : page;
            ImGui::SetCursorPosY(y);
            char buf[24];
            std::snprintf(buf, sizeof buf, "%03X", page);
            cellText(buf, pal::Accent, FontKind::Mono, type::MonoSmall, 34);
            ImGui::TableSetColumnIndex(1);
            auto it = pk.find(page);
            cellText(it != pk.end() ? it->second.c_str() : "(not in the catalogue)", it != pk.end() ? pal::Text : pal::Warning, FontKind::Body, type::Label, 34);
            ImGui::TableSetColumnIndex(2);
            auto ct = cat.find(page);
            const size_t known = ct != cat.end() ? ct->second.size() : 0;
            cellText(std::to_string(known).c_str(), pal::Muted, FontKind::Body, type::Label, 34);
            ImGui::TableSetColumnIndex(3);
            size_t loaded = 0;
            for (auto r = rv.resident.lower_bound(world::makeTag(page, 0)); r != rv.resident.end() && world::tagPage(*r) == page; ++r) ++loaded;
             
            const bool scanned = rv.scannedPages.count(page) != 0;
            cellText(loaded || scanned ? std::to_string(loaded).c_str() : "-", loaded ? pal::Success : pal::Faint, FontKind::Body, type::Label, 34);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !scanned)
                ImGui::SetTooltip("Not scanned since this package loaded: Scan Loaded Entities to count its entries");
            ImGui::TableSetColumnIndex(4);
            cellText(open ? icon::ChevronUp : icon::ChevronDown, pal::Muted, FontKind::Icons, 10, 34);
            ImGui::PopID();
            if (!open) continue;
             
            std::vector<uint32_t> tags = ct != cat.end() ? ct->second : std::vector<uint32_t>{};
            for (auto r = rv.resident.lower_bound(world::makeTag(page, 0)); r != rv.resident.end() && world::tagPage(*r) == page; ++r)
                if (std::find(tags.begin(), tags.end(), *r) == tags.end()) tags.push_back(*r);
            std::stable_sort(tags.begin(), tags.end(), [&](uint32_t a, uint32_t b) { return rv.resident.count(a) > rv.resident.count(b); });
            if (tags.size() > 400) tags.resize(400);
            for (uint32_t t : tags) {
                ImGui::TableNextRow(0, px(30));
                ImGui::PushID(int(t));
                ImGui::TableSetColumnIndex(0);
                resDot(rv.of(t), 30);
                ImGui::TableSetColumnIndex(1);
                const std::string nm = knownName(t);
                cellText(("    " + (nm.empty() ? unnamedLabel(t) + "  " + spawn::tagText(t) : nm)).c_str(), nm.empty() ? pal::Faint : pal::Text,
                         FontKind::Body, type::Label, 30);
                ImGui::TableSetColumnIndex(2);
                const int ty = catalogType(t);
                cellText(ty >= 0 ? spawn::categoryName(uint8_t(ty)) : "?", pal::Faint, FontKind::Body, type::Small, 30);
                ImGui::TableSetColumnIndex(3);
                {
                    auto offline = requireConnection(ui);
                    if (iconButton("##spawn", icon::Play, "Spawn it with the current placement", false, 26, pal::Accent)) startSpawn(t);
                }
                ImGui::TableSetColumnIndex(4);
                if (iconButton("##sel", icon::List, "Select it in the catalogue", false, 26)) {
                    select(t);
                    gotoTab(kTabSpawn);
                }
                ImGui::PopID();
            }
        }
        ImGui::PopFont();
        if (pages.empty()) {
            ImGui::TableNextRow(0, px(50));
            ImGui::TableSetColumnIndex(1);
            caption("No package matches.", pal::Muted);
        }
        endTable();
    }
}

 
 
 

W::Params wtlParams() {
    State& s = st();
    std::lock_guard<std::mutex> lock(s.m);
    return s.wtlUi;
}

void setWtlParams(const W::Params& p) {
    State& s = st();
    std::lock_guard<std::mutex> lock(s.m);
    s.wtlUi = p;
}

 
void wtlTagField(W::Params& p, bool& changed) {
    WorldPage& w = wp();
    if (!w.wtlInit) {
        w.wtlInit = true;
        if (!p.tag && selectedTag()) {
            p.tag = selectedTag();
            changed = true;
        }
        w.wtlText = p.tag ? spawn::tagText(p.tag) : std::string();
        if (p.trigger == W::Trigger::Now) {
            p.trigger = W::Trigger::LowObjects;
            changed = true;
        }
    }
    if (const uint32_t handed = takeWorldTabTag()) {
        p.tag = handed;
        w.wtlText = spawn::tagText(handed);
        changed = true;
    }
    fieldLabel("Tag");
    gap(sp::XS + 2);
    const float bw = buttonWidth("Selected", nullptr, true) + px(8);
    if (inputMono("##wtag", &w.wtlText, "Tag, e.g. 80C7A2F0", (availWidth() - bw) / scale())) {
        uint32_t t = 0;
        p.tag = spawn::parseTag(w.wtlText, t) ? t : 0;
        changed = true;
    }
    ImGui::SameLine(0, px(8));
    if (button("Selected##wtl", {.tone = Tone::Ghost, .small = true, .tooltip = "Use the catalogue selection"})) {
        if (const uint32_t s = selectedTag()) {
            p.tag = s;
            w.wtlText = spawn::tagText(s);
            changed = true;
        }
    }
    gap(sp::XS);
    if (p.tag) caption(displayName(p.tag).c_str(), pal::Faint);
    else caption("Enter a content tag", pal::Warning);
}

void drawWtl(Ui& ui) {
    seedDemo();
    W::Params p = wtlParams();
    bool changed = false;
    wtlTagField(p, changed);
    gap(sp::M);
    fieldLabel("List");
    gap(sp::XS + 2);
    int kind = p.kind == W::Kind::Manifest ? 1 : 0;
    if (pillTabs("##kind", {"On-Demand Tag", "Manifest"}, &kind)) {
        p.kind = kind ? W::Kind::Manifest : W::Kind::OnDemand;
        changed = true;
    }
    gap(sp::S);
    if (checkbox("Wait Until the Loader Drains", &p.wait)) changed = true;
    tooltip("Blocks the game thread on the loader's wait (0x150C70) until the load completes. On Xbox this did not prevent the reclaimer fault.");
    if (changed) setWtlParams(p);
    gap(sp::M);
    std::string result;
    bool ok;
    {
        State& s = st();
        std::lock_guard<std::mutex> lock(s.m);
        result = s.wtlResult;
        ok = s.wtlOk;
    }
    {
        auto offline = requireConnection(ui);
        const bool content = p.tag && world::isPackagePage(world::tagPage(names::toPs3Tag(p.tag)));
        DisabledScope bad(!content, p.tag ? "That is a runtime page, not content (page below 0x400)" : "Enter a content tag first");
        if (button("Load Now", {.tone = Tone::Danger, .icon = icon::Send,
                                .tooltip = "Load the tag into the live world now (asks first). The engine's own path: create a request, add the tag, "
                                           "release, submit. The page must be registered: a package that is not loaded at all cannot be loaded this way.",
                                .busy = ui.busy(kJobWtl)})) {
            const W::Params q = p;
            confirm("Load " + spawn::tagText(q.tag) + " into the Live World?",
                    "On Xbox, loads into a live activity faulted the sObject reclaimer and took the title down. The engine only loads during "
                    "transitions: Auto-load (next card) fires at that moment instead. Load now anyway?",
                    "Load Now", [q] { post(kJobWtl, [q](Ctx& c) { loadTagNow(c, q); }); });
        }
    }
    if (!result.empty()) {
        gap(sp::M);
        pill(result.size() > 70 ? (result.substr(0, 68) + "..").c_str() : result.c_str(), ok ? pal::Success : pal::Danger, ok ? icon::Check : icon::Warning);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", result.c_str());
    }
}

void drawAuto(Ui& ui) {
    W::Params p = wtlParams();
    bool changed = false;
    AutoStatus a;
    {
        State& s = st();
        std::lock_guard<std::mutex> lock(s.m);
        a = s.autoLoad;
    }
    {
        DisabledScope running(a.running, "Disarm to change the trigger");
        fieldLabel("Fire When");
        gap(sp::XS + 2);
        int trig = p.trigger == W::Trigger::LoaderBusy ? 1 : p.trigger == W::Trigger::WorldState ? 2 : p.trigger == W::Trigger::Never ? 3 : 0;
        if (pillTabs("##trigger", {"Few Objects", "Loader Busy", "World State", "Observe Only"}, &trig)) {
            static const W::Trigger map[] = {W::Trigger::LowObjects, W::Trigger::LoaderBusy, W::Trigger::WorldState, W::Trigger::Never};
            p.trigger = map[trig];
            p.value = p.trigger == W::Trigger::WorldState ? W::STATE_SLICE_LOADING : 8;
            changed = true;
        }
        tooltip(p.trigger == W::Trigger::LowObjects
                    ? "Fires when the live sObject count drops to the threshold: the gap between the old world and the new one (Xbox default 8)."
                : p.trigger == W::Trigger::LoaderBusy ? "Fires when the loader's drain cursor is busy. On Xbox this fired too early (world still live)."
                : p.trigger == W::Trigger::WorldState
                    ? "Fires on a world state. 34 is initial slice-set loading; a slice-set swap stays in 36 and never fires."
                    : "Loads nothing: watches the world state, loader cursor and object count every frame (and whether the tick runs while loading).");
        switch (p.trigger) {
        case W::Trigger::LowObjects: {
            gap(sp::S);
            int v = int(p.value);
            if (inputInt("##thr", &v, 0, 1500, 150)) {
                p.value = uint32_t(v);
                changed = true;
            }
            tooltip("Live object threshold (Xbox default 8)");
            break;
        }
        case W::Trigger::LoaderBusy:
            break;
        case W::Trigger::WorldState: {
            gap(sp::S);
            static std::vector<std::string> names;
            static std::vector<const char*> ptrs;
            if (names.empty()) {
                for (uint32_t i = 0; i < 38; ++i) names.push_back(std::to_string(i) + "  " + W::worldStateName(i));
                for (const auto& n : names) ptrs.push_back(n.c_str());
            }
            int sel = int(std::min<uint32_t>(p.value, 37));
            if (combo("##state", &sel, ptrs.data(), int(ptrs.size()), -1)) {
                p.value = uint32_t(sel);
                changed = true;
            }
            break;
        }
        default:
            break;
        }
    }
    if (changed) setWtlParams(p);
    gap(sp::M);
    {
        auto offline = requireConnection(ui);
        if (a.running) {
            if (button("Disarm", {.tone = Tone::Danger, .icon = icon::Stop, .tooltip = "Remove the per-frame task", .busy = ui.busy("Disarm Auto-Load")}))
                post("Disarm Auto-Load", [](Ctx& c) { disarmAutoLoad(c); });
        } else {
            const bool content = p.tag && world::isPackagePage(world::tagPage(names::toPs3Tag(p.tag)));
            DisabledScope bad(!content && p.trigger != W::Trigger::Never, "Enter a content tag in World tag load");
            const bool observe = p.trigger == W::Trigger::Never;
            if (button(observe ? "Start Observing" : "Arm", {.tone = Tone::Primary, .icon = observe ? icon::View : icon::Play,
                                                             .tooltip = observe ? "Install the per-frame observer (no load)"
                                                                                : "Install the per-frame task; it fires once, on the first frame the trigger "
                                                                                  "holds. Arm in orbit or in an activity, then launch / travel.",
                                                             .busy = ui.busy(kJobArm)})) {
                W::Params q = p;
                if (q.trigger == W::Trigger::Never && !q.tag) q.tag = 0x80C7A2F0;    
                post(kJobArm, [q](Ctx& c) { armAutoLoad(c, q); });
            }
        }
    }
    if (!a.running && !a.havePoll) return;
    gap(sp::M);
     
    const W::State& s = a.state;
    {
        const char* status = a.running ? (s.fired ? "Fired" : a.params.trigger == W::Trigger::Never ? "Observing" : "Armed, Waiting") : (s.fired ? "Fired" : "Stopped");
        const bool stoppedBad = !a.running && !s.fired && !a.error.empty();
        pill(stoppedBad ? "Stopped: Nothing Loaded" : status,
             a.running ? (s.fired ? pal::Success : pal::Accent) : (s.fired ? pal::Success : stoppedBad ? pal::Danger : pal::Muted),
             a.running && !s.fired ? icon::Record : s.fired ? icon::Check : stoppedBad ? icon::Warning : icon::Stop);
        if (a.running && !s.fired) {
            ImGui::SameLine(0, px(10));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(4));
            caption(("checking every frame for " + std::string(W::triggerName(a.params.trigger))).c_str(), pal::Faint);
            requestRedraw();
        }
        if (!a.running && !a.error.empty()) {
            gap(sp::XS);
            wrapped(a.error.c_str(), pal::Danger, type::Small);
        }
    }
    gap(sp::S);
    char buf[96];
    std::snprintf(buf, sizeof buf, "%u  %s", s.lastState, W::worldStateName(s.lastState));
    kvRow("World State", s.lastState == 0xFFFFFFFFu ? std::string("unknown") : std::string(buf));
    kvRow("Loader Cursor", s.lastCursor == 0xFFFF ? std::string("idle (FFFF)") : hex(s.lastCursor), s.lastCursor == 0xFFFF ? pal::Muted : pal::Warning);
    std::snprintf(buf, sizeof buf, "%u  (lowest %u)", s.lastCount, s.minCount == 0xFFFFFFFFu ? s.lastCount : s.minCount);
    kvRow("Live Objects", s.lastCount == 0xFFFFFFFFu ? std::string("-") : std::string(buf));
    kvRow("Frames Run", std::to_string(s.frames), pal::Muted);
    const bool sawLoading = a.statesSeen.count(W::STATE_WORLD_TRANSITION) || a.statesSeen.count(W::STATE_SLICE_LOADING);
    kvRow("Ticks in 33/34", sawLoading || s.transitionFrames ? std::to_string(s.transitionFrames) + (s.transitionFrames ? "  (yes: runs while loading)" : "  (no)")
                                                             : std::string("no transition seen yet"),
          s.transitionFrames ? pal::Success : sawLoading ? pal::Danger : pal::Faint);
    if (s.fired) kvRow("Request", hex(s.request) + "  (handle " + hex(s.handle) + ")", pal::Success);
}

void drawRsc(Ui& ui) {
    WorldPage& w = wp();
    uint32_t event, category;
    bool raw;
    W::Params p = wtlParams();
    {
        State& s = st();
        std::lock_guard<std::mutex> lock(s.m);
        event = s.rscEvent;
        category = s.rscCategory;
        raw = s.rscRaw;
    }
    if (w.rscText.empty() && p.tag) w.rscText = spawn::tagText(p.tag);
    fieldLabel("Tag");
    gap(sp::XS + 2);
    inputMono("##rtag", &w.rscText, "Tag, e.g. 8108E001 (moon_raid_1 Manifest)", -1);
    uint32_t tag = 0;
    const bool valid = spawn::parseTag(w.rscText, tag);
    {
        State& s = st();
        std::lock_guard<std::mutex> lock(s.m);
        s.rscTag = valid ? tag : 0;      
    }
    gap(sp::S);
    fieldLabel("Event");
    gap(sp::XS + 2);
    static std::vector<std::string> labels;
    static std::vector<const char*> ptrs;
    if (labels.empty()) {
        for (const auto& po : spawn::rsc::posters()) labels.push_back(std::to_string(po.event) + "  " + po.name + (po.adds ? "  (adds)" : ""));
        for (const auto& l : labels) ptrs.push_back(l.c_str());
    }
    int ev = int(event) - 1;
    bool changed = false;
    if (combo("##event", &ev, ptrs.data(), int(ptrs.size()), -1)) {
        event = uint32_t(ev + 1);
        changed = true;
    }
    tooltip("Only the load_* events add an entry; desire_* re-assert one already listed (no-op for an absent tag).");
    gap(sp::S);
    ImGui::BeginGroup();
    fieldLabel(raw ? "Ctx (Raw)" : "Category");
    gap(sp::XS + 2);
    int cat = int(category);
    if (inputInt("##cat", &cat, 0, 0xFFFF, 150)) {
        category = uint32_t(cat);
        changed = true;
    }
    ImGui::EndGroup();
    ImGui::SameLine(0, px(16));
    ImGui::BeginGroup();
    fieldLabel(" ");
    gap(sp::XS + 2);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(8));
    if (checkbox("Raw Post (Control)", &raw)) changed = true;
    tooltip("One bare post without begin / end, as the Xbox v1.2 did: it provably does nothing (kept as the control).");
    ImGui::EndGroup();
    if (changed) {
        State& s = st();
        std::lock_guard<std::mutex> lock(s.m);
        s.rscEvent = event;
        s.rscCategory = category;
        s.rscRaw = raw;
    }
    gap(sp::M);
    {
        auto offline = requireConnection(ui);
        DisabledScope bad(!valid, "Enter a tag");
        if (button(raw ? "Post Raw" : "Run Transaction",
                   {.icon = icon::Send,
                    .tooltip = raw ? "One bare post, no begin / end (the Xbox control; asks first)"
                                   : "Tag check, then begin -> post -> end in one game-thread visit (diagnostic; asks first)",
                    .busy = ui.busy(kJobRsc)}))
            confirm(raw ? "Post a Raw Resourcerer Event?" : "Run the Resourcerer Transaction?",
                    "Diagnostic only. On Xbox this path left a malformed zero-size entry that crashed the next real load pass. World tag "
                    "load (above) is the engine's own path. Run it anyway?",
                    raw ? "Post Raw" : "Run It", [event, tag, raw, category] {
                        post(kJobRsc, [event, tag, raw, category](Ctx& c) { runRsc(c, event, tag, raw, category); });
                    });
    }
    std::string result;
    bool ok;
    {
        State& s = st();
        std::lock_guard<std::mutex> lock(s.m);
        result = s.rscResult;
        ok = s.rscOk;
    }
    if (!result.empty()) {
        gap(sp::M);
        pill(result.size() > 70 ? (result.substr(0, 68) + "..").c_str() : result.c_str(), ok ? pal::Success : pal::Danger, ok ? icon::Check : icon::Warning);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", result.c_str());
    }
}

 
 
 

void drawTown(Ui& ui) {
    seedDemo();
    std::optional<spawn::town::State> town;
    std::string err;
    std::vector<spawn::town::Edit> edits;
    {
        State& s = st();
        std::lock_guard<std::mutex> lock(s.m);
        town = s.town;
        err = s.townError;
        edits = s.townEdits;
    }
    if (town) {
        const float w = std::floor((availWidth() - px(8)) * 0.5f);
        statTile("applied", "Applied", town->current() ? "Town" : "Normal", "what the engine uses now", w, town->current() ? pal::Accent : pal::Success);
        ImGui::SameLine(0, px(8));
        statTile("requested", "Requested", town->desired() ? "Town" : "Normal", town->dirty() ? "recompute pending" : "settled", w,
                 town->desired() ? pal::Accent : pal::Success);
        gap(sp::S);
        char buf[96];
        std::snprintf(buf, sizeof buf, "latch %u  \xC2\xB7  refs %u  \xC2\xB7  dirty %u  \xC2\xB7  block %08X", unsigned(town->latch()), town->refs(),
                      unsigned(town->dirty()), town->block);
        caption(buf, pal::Faint);
    } else {
        caption(err.empty() ? "Not read yet." : err.c_str(), err.empty() ? pal::Faint : pal::Warning);
    }
    gap(sp::M);
    {
        auto offline = requireConnection(ui);
        const bool busy = ui.busy(kJobTown);
        if (button("Refresh", {.tone = Tone::Ghost, .icon = icon::Refresh, .small = true, .busy = busy})) post(kJobTown, [](Ctx& c) { refreshTown(c); });
        ImGui::SameLine(0, px(6));
        if (button("Request Normal", {.icon = icon::Shield, .small = true,
                                      .tooltip = "Normal (activity) equipment: what the switch above does. On Xbox this allowed firing in the Tower "
                                                 "(no damage, no weapon HUD, other players do not see it); re-equip a weapon if nothing changes.",
                                      .busy = busy}))
            post(kJobTown, [](Ctx& c) { requestTown(c, false); });
        ImGui::SameLine(0, px(6));
        if (button("Request Town", {.icon = icon::Home, .small = true, .tooltip = "Social-space equipment", .busy = busy}))
            post(kJobTown, [](Ctx& c) { requestTown(c, true); });
    }
    if (!edits.empty()) {
        gap(sp::M);
        fieldLabel("Edits  (Restore Newest First)");
        gap(sp::XS + 2);
        for (size_t i = edits.size(); i-- > 0;) {
            const auto& e = edits[i];
            ImGui::PushID(int(i));
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float h = px(34);
            ImGui::Dummy(ImVec2(availWidth(), h));
            ImDrawList* d = ImGui::GetWindowDrawList();
            d->AddRectFilled(p, p + ImVec2(availWidth(), h), u32(pal::Input), px(6));
            const std::string text = std::string(e.town ? "Town" : "Normal") + " requested  \xC2\xB7  original " + hexBytes(e.original.data(), 8);
            drawText(d, FontKind::Mono, type::MonoSmall, ImVec2(p.x + px(12), textTop(FontKind::Mono, type::MonoSmall, p.y + h * 0.5f)), u32(pal::Muted),
                     text.c_str());
            const float bw = buttonWidth("Restore", icon::Undo, true);
            ImGui::SetCursorScreenPos(p + ImVec2(availWidth() - bw - px(4), (h - px(kSmallControlH)) * 0.5f));
            {
                auto offline = requireConnection(ui);
                if (button("Restore", {.tone = Tone::Ghost, .icon = icon::Undo, .small = true, .tooltip = "Put the original inputs back"}))
                    post(kJobTown, [i](Ctx& c) { restoreTown(c, i); });
            }
            ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + px(4)));
            ImGui::Dummy(ImVec2(0, 0));
            ImGui::PopID();
        }
    }
}

}   
}   

namespace brps3 {

BR_FEATURES(spawn_world) {
    using namespace spawnui;
    const Page P = Page::Spawner;
     
    r.section({.id = "spawn.residency", .page = P, .tab = kTabResident, .section = kSecResidency, .order = 10, .draw = drawResidency});
    r.section({.id = "spawn.tagcheck", .page = P, .tab = kTabResident, .section = kSecTagCheck, .order = 20, .draw = drawTagCheck});
    r.section({.id = "spawn.packages", .page = P, .tab = kTabResident, .section = kSecPackages, .order = 30, .draw = drawPackages});
    r.action({.id = kActPages, .page = P, .tab = kTabResident, .section = kSecResidency, .label = "Refresh Loaded Packages",
              .help = "Read which content packages are loaded right now (one read of the tag table).", .run = [](Ctx& c) {
                  refreshPages(c);
                  size_t n;
                  {
                      State& s = st();
                      std::lock_guard<std::mutex> lock(s.m);
                      n = s.livePages.size();
                  }
                  c.toast(std::to_string(n) + " content packages loaded");
              }});
    r.action({.id = kActScan, .page = P, .tab = kTabResident, .section = kSecResidency, .label = "Scan Loaded Entities",
              .help = "Read every loaded package's item array and list the entity definitions that are loaded (the catalogue dots).",
              .run = [](Ctx& c) { scanEntries(c); }});
    ui::styleSection(kSecResidency, {.icon = ui::icon::Loot, .ownsActions = true});
    ui::styleSection(kSecTagCheck, {.icon = ui::icon::Search});
    ui::styleSection(kSecPackages, {.fullWidth = true, .icon = ui::icon::Folder});

     
    r.section({.id = "spawn.wtl", .page = P, .tab = kTabWorld, .section = kSecWtl, .order = 10, .draw = drawWtl});
    r.section({.id = "spawn.autoload", .page = P, .tab = kTabWorld, .section = kSecAuto, .order = 20, .draw = drawAuto});
    r.section({.id = "spawn.town", .page = P, .tab = kTabWorld, .section = kSecTown, .order = 30, .draw = drawTown});
    r.section({.id = "spawn.rsc", .page = P, .tab = kTabWorld, .section = kSecRsc, .order = 40, .draw = drawRsc, .badge = kBadgeDev});
    r.action({.id = kActWtlLoad, .page = P, .tab = kTabWorld, .section = kSecWtl, .label = "Load Tag Now (World Tag Load)",
              .help = "Load the World load tab's tag (else the catalogue selection) into the live world now. Xbox loads into a live world faulted "
                      "the reclaimer: prefer auto-load.",
              .run = [](Ctx& c) {
                  W::Params p = wtlParams();
                  if (!p.tag) p.tag = selectedTag();
                  if (!p.tag) throw Error("Pick a tag on the World load tab or select an entity first");
                  loadTagNow(c, p);
              },
              .dangerous = true});
    r.action({.id = kActWtlArm, .page = P, .tab = kTabWorld, .section = kSecAuto, .label = "Arm Auto-Load",
              .help = "Arm the World load tab's tag (else the catalogue selection) with its trigger: it loads once, during the next transition.",
              .run = [](Ctx& c) {
                  W::Params p = wtlParams();
                  if (p.trigger == W::Trigger::Now) p.trigger = W::Trigger::LowObjects;
                  if (!p.tag) p.tag = selectedTag();
                  if (!p.tag && p.trigger != W::Trigger::Never) throw Error("Pick a tag on the World load tab or select an entity first");
                  if (!p.tag) p.tag = 0x80C7A2F0;
                  armAutoLoad(c, p);
              }});
    r.action({.id = kActWtlDisarm, .page = P, .tab = kTabWorld, .section = kSecAuto, .label = "Disarm Auto-Load",
              .help = "Remove the auto-load task (Panic does it too).", .run = disarmAutoLoad});
    r.action({.id = kActRsc, .page = P, .tab = kTabWorld, .section = kSecRsc, .label = "Run Resourcerer Transaction",
              .help = "Diagnostic: tag check, then begin -> post -> end with the Resourcerer card's tag, event and category (asks first).", .run = [](Ctx& c) {
                  uint32_t event, category, tag;
                  bool raw;
                  {
                      State& s = st();
                      std::lock_guard<std::mutex> lock(s.m);
                      event = s.rscEvent;
                      category = s.rscCategory;
                      raw = s.rscRaw;
                      tag = s.rscTag ? s.rscTag : s.wtlUi.tag;
                  }
                  if (!tag) tag = selectedTag();
                  if (!tag) throw Error("Pick a tag first");
                  runRsc(c, event, tag, raw, category);
              },
              .dangerous = true,
              .badge = kBadgeDev});
    r.toggle({.id = kToggleTown, .page = P, .tab = kTabWorld, .section = kSecTown, .label = "Normal Equipment in Social Spaces",
              .help = "Turns town presence off (latch / refcount, then desired) so the Tower treats your equipment as in an activity. Off puts the "
                      "original inputs back.",
              .set = [](Ctx& c, bool on) {
                  if (on) {
                      refreshTown(c);
                      requestTown(c, false);
                  } else {
                      restoreAllTown(c);
                  }
              }});
    r.action({.id = "world.town.request_town", .page = P, .tab = kTabWorld, .section = kSecTown, .label = "Request Town Equipment",
              .help = "Ask for social-space (town) equipment: latch / refcount, then desired. Restore puts the original inputs back.",
              .run = [](Ctx& c) {
                  refreshTown(c);
                  requestTown(c, true);
                  c.toast("Requested town equipment");
              }});
    r.action({.id = "world.town.restore", .page = P, .tab = kTabWorld, .section = kSecTown, .label = "Restore Town Presence Inputs",
              .help = "Put back the original town presence inputs of every request made this session (newest first).",
              .run = [](Ctx& c) {
                  size_t n;
                  {
                      State& s = st();
                      std::lock_guard<std::mutex> lock(s.m);
                      n = s.townEdits.size();
                  }
                  if (!n) throw Error("No town presence request to restore this session");
                  restoreAllTown(c);
                  c.toast("Town presence inputs restored");
              }});
    r.action({.id = kActTownRefresh, .page = P, .tab = kTabWorld, .section = kSecTown, .label = "Refresh Town Presence",
              .help = "Read the town presence block (applied, requested, latch, refcount).", .run = [](Ctx& c) {
                  refreshTown(c);
                  State& s = st();
                  std::lock_guard<std::mutex> lock(s.m);
                  c.toast(s.town ? "Town presence: " + spawn::town::describe(*s.town) : "Town presence: " + s.townError);
              }});
    ui::styleSection(kSecWtl, {.icon = ui::icon::Send, .ownsActions = true});
    ui::styleSection(kSecAuto, {.icon = ui::icon::Repeat, .ownsActions = true});
    ui::styleSection(kSecRsc, {.icon = ui::icon::Memory, .ownsActions = true});
    ui::styleSection(kSecTown, {.icon = ui::icon::Home, .ownsActions = true});
}

}   
