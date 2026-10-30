#include "features/spawn_ui.h"

#include <algorithm>
#include <atomic>
#include <cstdio>

#include "app/engine.h"
#include "core/error.h"
#include "game/names_catalog.h"
#include "game/world_tags.h"

namespace brps3::spawnui {

using namespace ui;

namespace {
uint32_t g_worldTag = 0;
}   

bool post(const std::string& label, std::function<void(Ctx&)> job, std::function<void(bool, const std::string&)> done) {
    if (!Engine::get().connected()) {
        notify(std::string(kConnectFirst) + ": " + label + " needs the console", Level::Warn);
        return false;
    }
    noteQueued(label);
    Engine::get().post(label, std::move(job), std::move(done));
    return true;
}

Res ResidencyView::of(uint32_t tag) const {
    if (resident.count(tag)) return Res::Loaded;
    if (checkedOut.count(tag)) return Res::NotLoaded;           
    if (!any()) return Res::Unknown;
    const uint32_t page = world::tagPage(names::toPs3Tag(tag));
     
    if (livePages.count(page)) return scannedPages.count(page) ? Res::NotLoaded : Res::PackageLoaded;
    return pagesAt > 0 ? Res::NotLoaded : Res::Unknown;
}

const ResidencyView& residency() {
    static ResidencyView v;
    State& s = st();
    std::lock_guard<std::mutex> lock(s.m);
    if (v.rev != s.residencyRev) {
        v.livePages = s.livePages;
        v.resident = s.resident;
        v.scannedPages = s.scannedPages;
        v.checkedOut = s.checkedOut;
        v.pagesAt = s.pagesAt;
        v.entriesAt = s.entriesAt;
        v.registeredPages = s.registeredPages;
        v.rev = s.residencyRev;
    }
    return v;
}

ImVec4 resColor(Res r) {
    switch (r) {
    case Res::Loaded: return pal::Success;
    case Res::PackageLoaded: return pal::Warning;
    case Res::NotLoaded: return pal::Decor;
    case Res::Unknown: return pal::Decor;
    }
    return pal::Decor;
}

const char* resLabel(Res r) {
    switch (r) {
    case Res::Loaded: return "Loaded";
    case Res::PackageLoaded: return "Package Loaded";
    case Res::NotLoaded: return "Not Loaded";
    case Res::Unknown: return "Unknown";
    }
    return "?";
}

const char* resHelp(Res r) {
    switch (r) {
    case Res::Loaded: return "Its definition is loaded right now: it can be spawned here.";
    case Res::PackageLoaded: return "Its package is loaded; run Scan Loaded Entities (Resident tags) or Check to see if this entry is.";
    case Res::NotLoaded: return "Not loaded here: go where it lives, or load it on the World load tab.";
    case Res::Unknown: return "Residency not read yet: connect, then Refresh on the Resident tags tab (it also refreshes on its own).";
    }
    return "";
}

void resDot(Res r, float rowHeight) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = px(rowHeight) - 2 * ImGui::GetStyle().CellPadding.y;
    const ImVec2 c(p.x + px(6), p.y + h * 0.5f);
    ImDrawList* d = ImGui::GetWindowDrawList();
    if (r == Res::Unknown) d->AddCircle(c, px(3.5f), u32(pal::Decor), 16, 1.2f);
    else if (r == Res::NotLoaded) d->AddCircle(c, px(3.5f), u32(pal::Faint), 16, 1.2f);
    else d->AddCircleFilled(c, px(4), u32(resColor(r)), 16);
    ImGui::Dummy(ImVec2(px(12), h));
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s: %s", resLabel(r), resHelp(r));
}

std::string spawnJobLabel(uint32_t tag, int count) {
    return "Spawn " + shortName(tag) + (count > 1 ? " x" + std::to_string(count) : "");
}

namespace {
std::atomic<int> g_spawnJobs{0};
}

bool spawnBusy() { return g_spawnJobs.load() > 0; }

void startSpawn(uint32_t tag, std::optional<spawn::Mode> mode, const std::string& given) {
    if (!tag) {
        notify("Select an entity first", Level::Info);
        return;
    }
    spawn::Placement p = placement();
    if (mode) p.mode = *mode;
    const std::string label = given.empty() ? displayName(tag) : given;
    const std::string job = spawnJobLabel(tag, p.count);
    ++g_spawnJobs;
    if (!post(job, [tag, p, label](Ctx& c) { runSpawn(c, tag, p, label); },
              [](bool ok, const std::string&) {
                  --g_spawnJobs;
                  if (ok) flashSuccess("spawn.go");
              }))
        --g_spawnJobs;
}

void gotoTab(const char* tab) { openPage(Page::Spawner, tab); }

void loadTagOnWorldTab(uint32_t tag) {
    g_worldTag = tag;
    gotoTab(kTabWorld);
}

uint32_t takeWorldTabTag() {
    const uint32_t t = g_worldTag;
    g_worldTag = 0;
    return t;
}

std::string ago(double since) {
    const double d = std::max(0.0, now() - since);
    char buf[32];
    if (d < 60) std::snprintf(buf, sizeof buf, "%.0f s ago", d);
    else if (d < 3600) std::snprintf(buf, sizeof buf, "%.0f min ago", d / 60);
    else std::snprintf(buf, sizeof buf, "%.1f h ago", d / 3600);
    return buf;
}

}   
