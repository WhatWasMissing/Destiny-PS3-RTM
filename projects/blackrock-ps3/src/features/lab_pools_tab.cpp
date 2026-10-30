 
 
 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "app/engine.h"
#include "app/registry.h"
#include "app/settings.h"
#include "core/error.h"
#include "features/lab_ui.h"
#include "game/lab_pools.h"
#include "game/world.h"
#include "ui/widgets.h"

namespace brps3 {
namespace {

using namespace ui;
using labui::fmt;
using labui::hx;

constexpr const char* kTab = "Pools & globals";
constexpr const char* kSecPools = "Datum pools";
constexpr const char* kSecGlobals = "Globals";
constexpr const char* kReadAllPools = "Read Every Pool Header";
constexpr const char* kReadPool = "Read the Selected Pool";
constexpr const char* kDumpEntries = "Dump the Selected Pool's Entries";
constexpr const char* kReadAllGlobals = "Resolve Every Global";
constexpr const char* kReadGlobal = "Read the Selected Global";
constexpr const char* kPoolFavs = "lab.pools.favourites";
constexpr const char* kGlobalFavs = "lab.globals.favourites";
constexpr float kRowH = 30;

struct GlobalStatus {
    uint32_t block = 0;
    bool initialised = false, registered = false;
};

struct State {
    bool loaded = false;
    std::string loadError;
    std::vector<world::PoolInfo> pools;
    std::vector<world::GlobalInfo> globals;
    std::vector<std::string> categories;    
     
    std::string poolFilter;
    int poolCat = 0;
    bool poolFavOnly = false;
    std::atomic<int> poolSel{-1};
    std::set<std::string> poolFavs;
    std::atomic<int> first{0}, count{4};
     
    std::string globFilter;
    bool globFavOnly = false;
    std::atomic<int> globSel{-1};
    std::set<std::string> globFavs;
     
    labui::Slot<std::vector<lab::PoolView>> all;           
    std::atomic<size_t> allDone{0}, allTotal{0};
    labui::Slot<lab::PoolView> pool;
    labui::Slot<lab::EntryDump> dump;
    labui::Slot<std::vector<GlobalStatus>> globalsAll;     
    labui::Slot<lab::GlobalView> global;
    std::vector<uint8_t> globalPrev;                       
    uint32_t globalPrevSlot = 0;
    std::vector<uint8_t> globalShown;
    bool seeded = false;
};

State& st() {
    static State s;
    return s;
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool ensureLoaded() {
    State& s = st();
    if (s.loaded) return s.loadError.empty();
    s.loaded = true;
    try {
        s.pools = world::poolCatalog();
        s.globals = world::globalCatalog();
        std::set<std::string> cats;
        for (const auto& p : s.pools) cats.insert(p.category.empty() ? std::string("other") : p.category);
        s.categories = {"All"};
        s.categories.insert(s.categories.end(), cats.begin(), cats.end());
        Settings& set = Engine::get().settings();
        for (const auto& k : set.list(kPoolFavs)) s.poolFavs.insert(k);
        for (const auto& k : set.list(kGlobalFavs)) s.globFavs.insert(k);
    } catch (const std::exception& e) {
        s.loadError = e.what();
    }
    return s.loadError.empty();
}

void seedDemo() {
    State& s = st();
    if (s.seeded || !ensureLoaded()) return;
    s.seeded = true;
    if (!labui::demo()) return;
    std::vector<lab::PoolView> views(s.pools.size());
    for (size_t i = 0; i < s.pools.size(); ++i) {
        lab::PoolView& v = views[i];
        v.info = s.pools[i];
        v.read = true;
        v.header.addr = v.info.ps3;
        v.header.base = 0x60100000 + uint32_t(i) * 0x10000;
        v.header.stride = v.info.elemSize ? v.info.elemSize : 0x10;
        v.header.capacity = v.info.capacity ? v.info.capacity : 64;
        v.header.count = uint32_t((i * 37) % (v.header.capacity + 1));
        v.header.identityOffset = 0xC;
        v.header.identityStride = v.header.stride;
        v.plausible = true;
        v.strideOk = i % 23 != 7;
        v.capacityOk = true;
        v.runtimeName = v.info.name;
        v.nameOk = true;
    }
    s.all.set(views);
    for (size_t i = 0; i < s.pools.size(); ++i)
        if (s.pools[i].name == "sobjects") s.poolSel = int(i);
    if (s.poolSel >= 0) {
        lab::PoolView v = views[size_t(s.poolSel.load())];
        v.header.base = 0x60163880 - 3 * 0xC0;
        v.header.stride = 0xC0;
        v.header.capacity = 1500;
        v.header.count = 214;
        v.header.identityOffset = 0xC;
        v.header.identityStride = 0xC0;
        v.header.mask = 0xFFFF;
        v.header.classWord = 0x80800728;
        v.strideOk = true;
        s.pool.set(v);
        lab::EntryDump d;
        d.first = 3;
        d.count = 2;
        d.stride = 0xC0;
        d.base = v.header.base;
        std::vector<uint8_t> bytes(2 * 0xC0);
        for (size_t k = 0; k < bytes.size(); ++k) bytes[k] = uint8_t((k * 29 + 7) & 0xFF) * uint8_t(k % 6 == 0 ? 0 : 1);
        bytes[0xC] = 0x51, bytes[0xD] = 0xFB, bytes[0xE] = 0x80, bytes[0xF] = 0x03;
        d.bytes = world::Bytes(v.header.entry(3), bytes);
        d.identity = {0x51FB8003u, 0x52038004u};
        s.dump.set(d);
    }
    s.poolFavs = {"sobjects", "player array"};
    std::vector<GlobalStatus> gs(s.globals.size());
    for (size_t i = 0; i < gs.size(); ++i) {
        gs[i].initialised = i % 9 != 4;
        gs[i].registered = gs[i].initialised;
        gs[i].block = gs[i].initialised ? 0x32000010 + uint32_t(i) * 0x400 : 0;
    }
    s.globalsAll.set(gs);
    for (size_t i = 0; i < s.globals.size(); ++i)
        if (s.globals[i].name.rfind("AI Actor Manager", 0) == 0) s.globSel = int(i);
    if (s.globSel >= 0) {
        lab::GlobalView g;
        g.info = s.globals[size_t(s.globSel.load())];
        g.read = true;
        g.block.slot = g.info.slot;
        g.block.block = 0x32004810;
        g.block.initialised = true;
        g.block.header = {0x21, 0x5A1C02E7, 0, 1, 0, 9};
        g.registered = true;
        g.data.resize(std::min<uint32_t>(g.info.size, 0x100));
        for (size_t k = 0; k < g.data.size(); ++k) g.data[k] = uint8_t(k % 7 == 0 ? 0 : (k * 13) & 0xFF);
        s.global.set(g);
    }
}

 

void requireReaders(Ctx& c) { c.require(world::kFingerprints, "World readers"); }

void runReadAllPools(Ctx& c) {
    requireReaders(c);
    State& s = st();
    world::RuntimeReader mem(c.rt());
    const auto& pools = world::poolCatalog();
    s.allDone = 0;
    s.allTotal = 1;
    auto views = lab::readPoolViews(
        mem, pools,
        [&](size_t done, size_t total) {
            s.allDone = done;
            s.allTotal = total;
        },
        [&] { return c.cancelled(); });
    size_t ok = 0, read = 0;
    for (const auto& v : views) {
        read += v.read;
        ok += v.healthy();
    }
    c.log(fmt("[lab] pools: %zu of %zu headers read, %zu match the catalogue (%llu reads)", read, views.size(), ok,
              static_cast<unsigned long long>(mem.reads())));
    s.all.set(std::move(views));
}

void runReadPool(Ctx& c) {
    requireReaders(c);
    State& s = st();
    const int sel = s.poolSel.load();
    const auto& pools = world::poolCatalog();
    if (sel < 0 || size_t(sel) >= pools.size()) throw Error("select a pool first");
    world::RuntimeReader mem(c.rt());
    lab::PoolView v = lab::readPoolView(mem, pools[size_t(sel)]);
    c.log(fmt("[lab] pool %s %08X: base %08X stride 0x%X cap %u count %u: %s", v.info.name.c_str(), v.info.ps3, v.header.base, v.header.stride,
              v.header.capacity, v.header.count, v.verdict().c_str()));
    s.pool.set(v);
    if (auto all = s.all.get(); all && size_t(sel) < all->size()) {
        (*all)[size_t(sel)] = v;
        s.all.set(std::move(*all));
    }
}

void runDump(Ctx& c) {
    requireReaders(c);
    State& s = st();
    const int sel = s.poolSel.load();
    const auto& pools = world::poolCatalog();
    if (sel < 0 || size_t(sel) >= pools.size()) throw Error("select a pool first");
    world::RuntimeReader mem(c.rt());
    const lab::PoolView v = lab::readPoolView(mem, pools[size_t(sel)]);
    s.pool.set(v);
    if (!v.plausible) throw Error("the pool header is not plausible: " + v.verdict());
    s.dump.set(lab::readEntries(mem, v.header, uint32_t(std::max(0, s.first.load())), uint32_t(std::max(1, s.count.load()))));
}

void runReadAllGlobals(Ctx& c) {
    requireReaders(c);
    State& s = st();
    world::RuntimeReader mem(c.rt());
    const auto& globals = world::globalCatalog();
    const std::vector<uint32_t> refs = lab::readRegistryRefs(mem);
     
    std::vector<size_t> order(globals.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return globals[a].slot < globals[b].slot; });
    std::vector<GlobalStatus> out(globals.size());
    for (size_t i = 0; i < order.size();) {
        if (c.cancelled()) break;
        size_t j = i + 1;
        while (j < order.size() && globals[order[j]].slot + 4 - globals[order[i]].slot <= 0x4000) ++j;
        const uint32_t start = globals[order[i]].slot, end = globals[order[j - 1]].slot + 4;
        const world::Bytes chunk = mem.bytes(start, end - start);
        for (size_t k = i; k < j; ++k) {
            GlobalStatus& g = out[order[k]];
            g.block = chunk.u32(globals[order[k]].slot - start);
            g.initialised = world::validHandle(g.block) && g.block >= 16 && world::mapped(g.block - 16, 16);
        }
        i = j;
    }
     
    std::set<uint32_t> slots(refs.begin(), refs.end());
    size_t init = 0;
    for (size_t i = 0; i < globals.size(); ++i) {
        out[i].registered = slots.count(globals[i].slot) != 0;
        init += out[i].initialised;
    }
    c.log(fmt("[lab] globals: %zu of %zu initialised", init, globals.size()));
    s.globalsAll.set(std::move(out));
}

void runReadGlobal(Ctx& c) {
    requireReaders(c);
    State& s = st();
    const int sel = s.globSel.load();
    const auto& globals = world::globalCatalog();
    if (sel < 0 || size_t(sel) >= globals.size()) throw Error("select a global first");
    world::RuntimeReader mem(c.rt());
    const auto refs = lab::readRegistryRefs(mem);
    lab::GlobalView v = lab::readGlobalView(mem, globals[size_t(sel)], refs);
    c.log(fmt("[lab] global %s slot %08X: %s", v.info.name.c_str(), v.info.slot, v.verdict().c_str()));
    s.global.set(std::move(v));
}

 

void starCell(bool fav, bool* toggled) {
    const ImVec2 p = ImGui::GetCursorScreenPos() - ImVec2(px(4), px(3));
    ImGui::SetCursorScreenPos(p);
    if (ImGui::InvisibleButton("##star", ImVec2(px(24), px(24)))) *toggled = true;
    const bool hov = ImGui::IsItemHovered();
    const char* g = fav ? icon::StarFill : icon::Star;
    const ImVec2 is = textSize(FontKind::Icons, 12, g);
    drawText(ImGui::GetWindowDrawList(), FontKind::Icons, 12, p + (ImVec2(px(24), px(24)) - is) * 0.5f, u32(fav ? pal::Accent : hov ? pal::Text : pal::Decor), g);
    if (hov) ImGui::SetTooltip(fav ? "Remove from favourites" : "Add to favourites");
}

void toggleFav(std::set<std::string>& favs, const char* key, const std::string& name) {
    if (!favs.erase(name)) favs.insert(name);
    Engine::get().settings().setList(key, std::vector<std::string>(favs.begin(), favs.end()));
}

void drawPoolDetail(Ui& ui, float width) {
    State& s = st();
    const int sel = s.poolSel.load();
    beginCard("##pooldetail", nullptr, {.width = width, .raised = true});
    if (sel < 0 || size_t(sel) >= s.pools.size()) {
        emptyState(icon::Memory, "Select a Pool", "Pick a Row to Read Its Live Header, Check It Against the Registration and Dump Entries.", 220);
        endCard();
        return;
    }
    const world::PoolInfo& info = s.pools[size_t(sel)];
    heading(info.name.c_str());
    gap(sp::XS);
    hashLabel(info.ps3, "pool address", pal::Accent);
    ImGui::SameLine(0, px(10));
    pill(info.category.empty() ? "other" : info.category.c_str(), pal::Muted);
    ImGui::SameLine(0, px(6));
    pill(info.confidence.c_str(), info.confidence == "verified" ? pal::Success : pal::Warning);
    gap(sp::S);
    caption(fmt("registered: elem 0x%X, capacity %u, align %u", info.elemSize, info.capacity, info.align).c_str(), pal::Muted);
    gap(sp::M);
    if (labui::jobButton(ui, kReadPool, icon::Refresh, "Read the pool struct (header + runtime name)", Tone::Primary, "Read Header"))
        labui::post(ui, kReadPool, runReadPool);
    const auto v = s.pool.get();
    const bool mine = v && v->info.ps3 == info.ps3;
    if (mine) labui::readStamp(s.pool.at());
    labui::errorBox(s.pool.error());
    if (!mine) {
        gap(sp::S);
        caption("Header not read yet.", pal::Faint);
        endCard();
        return;
    }
    const world::PoolHeader& h = v->header;
    gap(sp::S);
    pill(v->verdict().c_str(), v->healthy() ? pal::Success : pal::Warning, v->healthy() ? icon::Check : icon::Warning);
    gap(sp::S);
     
    caption(fmt("%u of %u in use", h.count, h.capacity).c_str(), pal::Muted);
    gap(sp::XS);
    progressBar(std::clamp(v->usage(), 0.0f, 1.0f), 0, 5);
    gap(sp::S);
    if (labui::kvBegin("##ph", 130)) {
        labui::kvHex("+0x00 Header", h.header, "header");
        labui::kvHex("+0x08 Base", h.base, "base");
        labui::kv("+0x0C Stride", fmt("0x%X", h.stride), v->strideOk ? pal::Text : pal::Warning, true,
                  "stride == elem, or align_up(elem + 4, align) with the identity at +elem");
        labui::kv("+0x10 Capacity", std::to_string(h.capacity), v->capacityOk ? pal::Text : pal::Warning);
        labui::kv("+0x14 Count", std::to_string(h.count));
        labui::kv("+0x18 / +0x1C Identity", fmt("+0x%X / 0x%X", h.identityOffset, h.identityStride));
        labui::kv("+0x20 / +0x24 Masks", hx(h.mask) + " / " + hx(h.mask2));
        labui::kv("+0x2C Class", hx(h.classWord));
        labui::kv("+0x30 Namespace", hx(h.nameSpace));
        labui::kv("+0x3D Name", v->runtimeName.empty() ? "-" : v->runtimeName, v->nameOk ? pal::Success : pal::Muted, false);
        labui::kvEnd();
    }
    gap(sp::M);
    fieldLabel("ENTRIES");
    gap(sp::XS);
    int first = s.first.load(), count = s.count.load();
    if (inputInt("##first", &first, 0, int(std::max<uint32_t>(h.capacity, 1)) - 1, 130)) s.first = first;
    tooltip("First entry index");
    ImGui::SameLine(0, px(8));
    if (inputInt("##count", &count, 1, 64, 120)) s.count = count;
    tooltip("How many entries (at most 32 KiB per dump)");
    ImGui::SameLine(0, px(8));
    if (labui::jobButton(ui, kDumpEntries, icon::View, "Read these entries as hex", Tone::Secondary, "Dump")) labui::post(ui, kDumpEntries, runDump);
    labui::errorBox(s.dump.error());
    const auto d = s.dump.get();
    if (d && d->base == h.base && d->stride == h.stride) {
        gap(sp::S);
        for (uint32_t i = 0; i < d->count; ++i) {
            const uint32_t idx = d->first + i;
            ImGui::PushID(int(idx));
            std::string head = fmt("[%u]  %08X", idx, h.entry(idx));
            if (d->identity[i]) head += "  \xC2\xB7  identity " + hx(*d->identity[i]);
            caption(head.c_str(), pal::Text);
            gap(sp::XS);
            HexViewOptions o;
            o.base = h.entry(idx);
            o.height = std::min(220.0f, 24.0f + 20.0f * float((std::min<uint32_t>(d->stride, 0x400) + 15) / 16));
            hexView("##hex", d->bytes.data().data() + size_t(i) * d->stride, std::min<uint32_t>(d->stride, 0x400), o);
            gap(sp::S);
            ImGui::PopID();
        }
    }
    endCard();
}

void drawPools(Ui& ui) {
    State& s = st();
    if (!ensureLoaded()) {
        emptyState(icon::Warning, "Pool Catalogue Unavailable", s.loadError.c_str(), 200);
        return;
    }
    seedDemo();
    const auto all = s.all.get();
     
    searchField("##poolsearch", &s.poolFilter, "Search Name, Category or Address", std::min(320.0f, availWidth() / scale() * 0.4f));
    ImGui::SameLine(0, px(10));
    std::vector<const char*> cats;
    for (const auto& c : s.categories) cats.push_back(c.c_str());
    combo("##poolcat", &s.poolCat, cats.data(), int(cats.size()), 150);
    tooltip("Category");
    ImGui::SameLine(0, px(8));
    if (button("Favourites##pf", {.tone = Tone::Ghost, .icon = s.poolFavOnly ? icon::StarFill : icon::Star,
                                                                   .tooltip = "Show only your favourite pools", .selected = s.poolFavOnly}))
        s.poolFavOnly = !s.poolFavOnly;
    ImGui::SameLine(0, px(8));
    const bool scanning = ui.busy(kReadAllPools);
    if (scanning && Engine::get().status().job == kReadAllPools) {
         
        if (button("Stop", {.tone = Tone::Danger, .icon = icon::Stop, .tooltip = "Stop after the current read (the pools read so far stay)",
                            .spinner = true}))
            Engine::get().cancel();
    } else if (scanning) {
        button("Queued", {.icon = icon::Refresh, .tooltip = "Waiting for the job in front of it", .busy = true});
    } else if (labui::jobButton(ui, kReadAllPools, icon::Refresh, "Read every pool header (neighbouring pools share a read) and check it")) {
        labui::post(ui, kReadAllPools, runReadAllPools);
    }
    labui::readStamp(s.all.at(), "scanned");
    if (scanning) {
        gap(sp::XS);
        const size_t total = std::max<size_t>(1, s.allTotal.load());
        progressBar(float(s.allDone.load()) / float(total), 0, 4);
        requestRedraw();
    }
    labui::errorBox(s.all.error());
    gap(sp::M);
     
    const std::string f = lower(s.poolFilter);
    const std::string cat = s.poolCat > 0 && size_t(s.poolCat) < s.categories.size() ? s.categories[size_t(s.poolCat)] : std::string();
    std::vector<int> view;
    size_t healthy = 0, read = 0;
    for (int i = 0; i < int(s.pools.size()); ++i) {
        const world::PoolInfo& p = s.pools[size_t(i)];
        if (!cat.empty() && (p.category.empty() ? std::string("other") : p.category) != cat) continue;
        if (s.poolFavOnly && !s.poolFavs.count(p.name)) continue;
        if (!f.empty() && lower(p.name + " " + p.category + " " + hx(p.ps3)).find(f) == std::string::npos) continue;
        view.push_back(i);
    }
    std::stable_sort(view.begin(), view.end(), [&](int a, int b) {
        return int(s.poolFavs.count(s.pools[size_t(a)].name)) > int(s.poolFavs.count(s.pools[size_t(b)].name));
    });
    if (all)
        for (const auto& v : *all) {
            read += v.read;
            healthy += v.healthy();
        }
    const float avail = availWidth();
    const bool split = avail >= px(900);
    const float detailW = split ? std::clamp(avail * 0.42f, px(360), px(520)) : avail;
    const float tableW = split ? avail - detailW - px(16) : avail;
    const float tableH = std::clamp(ImGui::GetWindowHeight() / scale() - 260.0f, 380.0f, 760.0f);
    ImGui::BeginGroup();
    caption(all ? fmt("%zu of %zu pools  \xC2\xB7  %zu headers read, %zu match", view.size(), s.pools.size(), read, healthy).c_str()
                : fmt("%zu of %zu pools  \xC2\xB7  press Read Every Pool Header for live status", view.size(), s.pools.size()).c_str(),
            pal::Faint);
    gap(sp::XS);
    std::string toggled;
    if (beginTable("##pools", 6, tableH, ImGuiTableFlags_BordersOuter, tableW / scale())) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##fav", ImGuiTableColumnFlags_WidthFixed, px(16));
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, px(90));
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, px(80));
        ImGui::TableSetupColumn("In Use", ImGuiTableColumnFlags_WidthFixed, px(96));
        ImGui::TableSetupColumn("Check", ImGuiTableColumnFlags_WidthFixed, px(64));
        tableHeaders();
        ImGuiListClipper clip;
        clip.Begin(int(view.size()), px(kRowH));
        while (clip.Step()) {
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
                const int i = view[size_t(r)];
                const world::PoolInfo& p = s.pools[size_t(i)];
                const lab::PoolView* v = all && size_t(i) < all->size() && (*all)[size_t(i)].read ? &(*all)[size_t(i)] : nullptr;
                ImGui::TableNextRow(0, px(kRowH));
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(i);
                const float y = ImGui::GetCursorPosY();
                const bool sel = s.poolSel.load() == i;
                if (tableRowSelectable("##row", sel, nullptr, kRowH - 2 * ImGui::GetStyle().CellPadding.y / scale())) s.poolSel = i;
                ImGui::SetCursorPosY(y);
                bool t = false;
                starCell(s.poolFavs.count(p.name) != 0, &t);
                if (t) toggled = p.name;
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                cellText(p.name.c_str(), sel ? pal::Accent : pal::Text, FontKind::Body, type::Label, kRowH);
                ImGui::TableSetColumnIndex(2);
                cellText(p.category.empty() ? "other" : p.category.c_str(), pal::Muted, FontKind::Body, type::Label, kRowH);
                ImGui::TableSetColumnIndex(3);
                cellText(hx(p.ps3).c_str(), pal::Muted, FontKind::Mono, type::MonoSmall, kRowH);
                ImGui::TableSetColumnIndex(4);
                if (v && v->plausible) {
                    const ImVec2 cp = ImGui::GetCursorScreenPos();
                    const float w = ImGui::GetContentRegionAvail().x;
                    const std::string txt = fmt("%u/%u", v->header.count, v->header.capacity);
                    cellText(txt.c_str(), pal::Text, FontKind::Mono, type::MonoSmall, kRowH);
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    const float by = cp.y + px(kRowH) - px(12);
                    dl->AddRectFilled(ImVec2(cp.x, by), ImVec2(cp.x + w, by + px(3)), u32(pal::FillActive), px(2));
                    dl->AddRectFilled(ImVec2(cp.x, by), ImVec2(cp.x + w * std::clamp(v->usage(), 0.0f, 1.0f), by + px(3)),
                                      u32(v->usage() > 0.9f ? pal::Warning : pal::Accent), px(2));
                } else {
                    cellText("-", pal::Faint, FontKind::Body, type::Label, kRowH);
                }
                ImGui::TableSetColumnIndex(5);
                if (v) cellText(v->healthy() ? "ok" : "check", v->healthy() ? pal::Success : pal::Warning, FontKind::Body, type::Label, kRowH);
                else cellText("-", pal::Faint, FontKind::Body, type::Label, kRowH);
                if (v && !v->healthy()) tooltip(v->verdict().c_str());
            }
        }
        if (view.empty()) {
            ImGui::TableNextRow(0, px(80));
            ImGui::TableSetColumnIndex(1);
            gap(sp::L);
            caption(s.poolFavOnly && s.poolFavs.empty() ? "No favourites yet: click a star." : "No pools match.", pal::Muted);
        }
        endTable();
    }
    ImGui::EndGroup();
    if (!toggled.empty()) toggleFav(s.poolFavs, kPoolFavs, toggled);
    if (split) ImGui::SameLine(0, px(16));
    else gap(sp::M);
    drawPoolDetail(ui, detailW);
}

void drawGlobalDetail(Ui& ui, float width) {
    State& s = st();
    const int sel = s.globSel.load();
    beginCard("##globdetail", nullptr, {.width = width, .raised = true});
    if (sel < 0 || size_t(sel) >= s.globals.size()) {
        emptyState(icon::Memory, "Select a Global", "Pick a Row to Resolve Its Slot, Check the Block Header and the Registry, and Read the Block.", 220);
        endCard();
        return;
    }
    const world::GlobalInfo& info = s.globals[size_t(sel)];
    heading(info.name.c_str());
    gap(sp::XS);
    hashLabel(info.slot, "slot", pal::Accent);
    ImGui::SameLine(0, px(10));
    pill(fmt("0x%X bytes", info.size).c_str(), pal::Muted);
    ImGui::SameLine(0, px(6));
    pill(info.confidence.c_str(), info.confidence == "verified" ? pal::Success : pal::Warning);
    gap(sp::M);
    if (labui::jobButton(ui, kReadGlobal, icon::Refresh,
                         "Resolve the slot, read the block header, the registry and the first bytes. A zero slot means the subsystem has not "
                         "initialised yet (that is information, not a failure).",
                         Tone::Primary, "Resolve and Read"))
        labui::post(ui, kReadGlobal, runReadGlobal);
    const auto v = s.global.get();
    const bool mine = v && v->info.slot == info.slot;
    if (mine) labui::readStamp(s.global.at());
    labui::errorBox(s.global.error());
    if (!mine) {
        endCard();
        return;
    }
     
    if (v->data != s.globalShown || info.slot != s.globalPrevSlot) {
        s.globalPrev = info.slot == s.globalPrevSlot ? s.globalShown : std::vector<uint8_t>();
        s.globalShown = v->data;
        s.globalPrevSlot = info.slot;
    }
    gap(sp::S);
    pill(v->verdict().c_str(), v->verdict() == "ok" ? pal::Success : pal::Warning, v->verdict() == "ok" ? icon::Check : icon::Info);
    if (v->block.initialised) {
        gap(sp::S);
        const world::BlockHeader& h = v->block.header;
        if (labui::kvBegin("##gh", 130)) {
            labui::kvHex("Block", v->block.block, "block", pal::Accent);
            labui::kv("Header Index", std::to_string(h.index));
            labui::kv("Name Hash", hx(h.hash), pal::Text, true, "Stored hash (the PS3 registrar uses FNV-1 order with a per-character transform)");
            labui::kv("Region / Category", fmt("%u / %u", h.region, h.category));
            labui::kv("Owner / Flags", fmt("%u / %08X", h.owner, h.flags));
            labui::kv("Registry", v->registered ? "back-reference names this slot" : "no back-reference", v->registered ? pal::Success : pal::Warning, false);
            labui::kvEnd();
        }
        if (!v->data.empty()) {
            gap(sp::S);
            HexViewOptions o;
            o.base = v->block.block;
            o.previous = s.globalPrev.empty() ? nullptr : s.globalPrev.data();
            o.previousSize = s.globalPrev.size();
            o.height = 260;
            hexView("##ghex", v->data.data(), v->data.size(), o);
            caption(fmt("first %zu of 0x%X bytes; read again to highlight what changed", v->data.size(), info.size).c_str(), pal::Faint);
        }
    }
    endCard();
}

void drawGlobals(Ui& ui) {
    State& s = st();
    if (!ensureLoaded()) {
        emptyState(icon::Warning, "Globals Catalogue Unavailable", s.loadError.c_str(), 200);
        return;
    }
    seedDemo();
    const auto all = s.globalsAll.get();
    searchField("##globsearch", &s.globFilter, "Search Name or Slot", std::min(320.0f, availWidth() / scale() * 0.4f));
    ImGui::SameLine(0, px(8));
    if (button("Favourites##gf", {.tone = Tone::Ghost, .icon = s.globFavOnly ? icon::StarFill : icon::Star, .tooltip = "Show only your favourite globals",
                                  .selected = s.globFavOnly}))
        s.globFavOnly = !s.globFavOnly;
    ImGui::SameLine(0, px(8));
    if (labui::jobButton(ui, kReadAllGlobals, icon::Refresh, "Read every slot (clustered reads) and the registry back-references"))
        labui::post(ui, kReadAllGlobals, runReadAllGlobals);
    labui::readStamp(s.globalsAll.at(), "resolved");
    labui::errorBox(s.globalsAll.error());
    gap(sp::M);
    const std::string f = lower(s.globFilter);
    std::vector<int> view;
    size_t init = 0;
    if (all)
        for (const auto& g : *all) init += g.initialised;
    for (int i = 0; i < int(s.globals.size()); ++i) {
        const world::GlobalInfo& g = s.globals[size_t(i)];
        if (s.globFavOnly && !s.globFavs.count(g.name)) continue;
        if (!f.empty() && lower(g.name + " " + hx(g.slot)).find(f) == std::string::npos) continue;
        view.push_back(i);
    }
    std::stable_sort(view.begin(), view.end(), [&](int a, int b) {
        return int(s.globFavs.count(s.globals[size_t(a)].name)) > int(s.globFavs.count(s.globals[size_t(b)].name));
    });
    const float avail = availWidth();
    const bool split = avail >= px(900);
    const float detailW = split ? std::clamp(avail * 0.42f, px(360), px(520)) : avail;
    const float tableW = split ? avail - detailW - px(16) : avail;
    const float tableH = std::clamp(ImGui::GetWindowHeight() / scale() - 300.0f, 360.0f, 700.0f);
    ImGui::BeginGroup();
    caption(all ? fmt("%zu of %zu globals  \xC2\xB7  %zu initialised", view.size(), s.globals.size(), init).c_str()
                : fmt("%zu of %zu globals", view.size(), s.globals.size()).c_str(),
            pal::Faint);
    gap(sp::XS);
    std::string toggled;
    if (beginTable("##globals", 5, tableH, ImGuiTableFlags_BordersOuter, tableW / scale())) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##fav", ImGuiTableColumnFlags_WidthFixed, px(16));
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, px(80));
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, px(60));
        ImGui::TableSetupColumn("Block", ImGuiTableColumnFlags_WidthFixed, px(90));
        tableHeaders();
        ImGuiListClipper clip;
        clip.Begin(int(view.size()), px(kRowH));
        while (clip.Step()) {
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
                const int i = view[size_t(r)];
                const world::GlobalInfo& g = s.globals[size_t(i)];
                ImGui::TableNextRow(0, px(kRowH));
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(i);
                const float y = ImGui::GetCursorPosY();
                const bool sel = s.globSel.load() == i;
                if (tableRowSelectable("##row", sel, nullptr, kRowH - 2 * ImGui::GetStyle().CellPadding.y / scale())) s.globSel = i;
                ImGui::SetCursorPosY(y);
                bool t = false;
                starCell(s.globFavs.count(g.name) != 0, &t);
                if (t) toggled = g.name;
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                cellText(g.name.c_str(), sel ? pal::Accent : pal::Text, FontKind::Body, type::Label, kRowH);
                ImGui::TableSetColumnIndex(2);
                cellText(hx(g.slot).c_str(), pal::Muted, FontKind::Mono, type::MonoSmall, kRowH);
                ImGui::TableSetColumnIndex(3);
                cellText(fmt("0x%X", g.size).c_str(), pal::Muted, FontKind::Mono, type::MonoSmall, kRowH);
                ImGui::TableSetColumnIndex(4);
                if (all && size_t(i) < all->size()) {
                    const GlobalStatus& gs = (*all)[size_t(i)];
                    cellText(gs.initialised ? hx(gs.block).c_str() : "null", gs.initialised ? (gs.registered ? pal::Text : pal::Warning) : pal::Faint,
                             FontKind::Mono, type::MonoSmall, kRowH);
                } else {
                    cellText("-", pal::Faint, FontKind::Body, type::Label, kRowH);
                }
            }
        }
        if (view.empty()) {
            ImGui::TableNextRow(0, px(80));
            ImGui::TableSetColumnIndex(1);
            gap(sp::L);
            caption(s.globFavOnly && s.globFavs.empty() ? "No favourites yet: click a star." : "No globals match.", pal::Muted);
        }
        endTable();
    }
    ImGui::EndGroup();
    if (!toggled.empty()) toggleFav(s.globFavs, kGlobalFavs, toggled);
    if (split) ImGui::SameLine(0, px(16));
    else gap(sp::M);
    drawGlobalDetail(ui, detailW);
}

void resetState() {
    State& s = st();
    s.all.clear();
    s.pool.clear();
    s.dump.clear();
    s.globalsAll.clear();
    s.global.clear();
    s.globalPrev.clear();
    s.globalShown.clear();
}

void probePools(Ctx& c) {
    requireReaders(c);
    world::RuntimeReader mem(c.rt());
    const auto& pools = world::poolCatalog();
    const auto views = lab::readPoolViews(mem, pools, {}, [&] { return c.cancelled(); });
    size_t ok = 0, names = 0, read = 0;
    for (const lab::PoolView& v : views) {
        if (!v.read) continue;
        ++read;
        ok += v.healthy();
        names += v.nameOk;
        if (!v.healthy()) c.log(fmt("  %-40s %08X: %s", v.info.name.c_str(), v.info.ps3, v.verdict().c_str()));
    }
    c.log(fmt("%zu of %zu pool headers read in %llu reads; %zu match stride/capacity; %zu runtime names (+0x3D) equal the catalogue", read,
              views.size(), static_cast<unsigned long long>(mem.reads()), ok, names));
    c.log("expected in an activity: (nearly) every pool ok; pools listed above are not registered yet or differ");
    c.log(ok + 3 >= read ? "verdict: pool catalogue matches the live headers" : "verdict: several pools differ from the catalogue (send this report)");
}

}   

BR_FEATURES(lab_pools) {
    const Page T = Page::Testing;
    r.section({.id = "lab.pools.pools", .page = T, .tab = kTab, .section = kSecPools, .order = 10, .draw = drawPools, .onDisconnect = resetState});
    r.section({.id = "lab.pools.globals", .page = T, .tab = kTab, .section = kSecGlobals, .order = 20, .draw = drawGlobals});
    r.action({.id = "lab.pools.read_all", .page = T, .tab = kTab, .section = kSecPools, .label = kReadAllPools,
              .help = "Read every datum pool header and check it against its registration (stride rule, capacity, runtime name).",
              .run = runReadAllPools});
    r.action({.id = "lab.pools.read", .page = T, .tab = kTab, .section = kSecPools, .label = kReadPool,
              .help = "Read the selected datum pool's header.", .run = runReadPool});
    r.action({.id = "lab.pools.dump", .page = T, .tab = kTab, .section = kSecPools, .label = kDumpEntries,
              .help = "Dump the selected pool's entries (first / count from the detail panel) as hex.", .run = runDump});
    r.action({.id = "lab.globals.read_all", .page = T, .tab = kTab, .section = kSecGlobals, .label = kReadAllGlobals,
              .help = "Resolve every global slot and its registry back-reference.", .run = runReadAllGlobals});
    r.action({.id = "lab.globals.read", .page = T, .tab = kTab, .section = kSecGlobals, .label = kReadGlobal,
              .help = "Resolve the selected global: block header, registry and its first bytes.", .run = runReadGlobal});
    ui::styleSection(kSecPools, {.fullWidth = true,
                                 .icon = ui::icon::Memory,
                                 .ownsActions = true});
    ui::styleSection(kSecGlobals, {.fullWidth = true,
                                   .icon = ui::icon::List,
                                   .ownsActions = true});
    r.action({.id = "lab.check.pools", .page = T, .tab = kPortChecksTab, .section = "Lab (development)", .label = "Pools: Catalogue vs Live Headers",
              .help = "In an activity: reads every datum pool header and compares stride, capacity and the runtime name at +0x3D with the "
                      "catalogue. Expected: nearly every pool ok.",
              .run = probePools});
}

}   
