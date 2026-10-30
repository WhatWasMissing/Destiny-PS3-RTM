 
 
 
 
 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "app/engine.h"
#include "app/registry.h"
#include "core/error.h"
#include "core/runtime.h"
#include "features/lab_ui.h"
#include "game/lab_components.h"
#include "game/lab_effects.h"
#include "game/lab_facets.h"
#include "game/names_catalog.h"
#include "game/world.h"
#include "ui/widgets.h"

namespace brps3 {
namespace {

using namespace ui;
using labui::fmt;
using labui::hx;

 
constexpr const char* kReadKeys = "Read Component Type Keys";
constexpr const char* kReadChain = "Read the Component Chain";
constexpr const char* kLookup = "Look Up the Component Owner and Dispatch";
constexpr const char* kReadEffectType = "Read the Effect Type";
constexpr const char* kFindEffect = "Find the Effect on the Inspected Object";
constexpr const char* kCrossCheck = "Cross-Check the Effect with the Game";
constexpr const char* kReadParams = "Read Effect Parameters";
constexpr const char* kWatchParamsId = "lab.effects.watch";
constexpr const char* kWatchLoop = "lab.effects.watch";
constexpr const char* kReadFacetManager = "Read the Facet Manager";
constexpr const char* kReadFacet = "Read the Inspected Object's Facet";
constexpr const char* kWalkChain = "Walk the Facet Chain";

constexpr const char* kSecObject = "Inspected object";
constexpr const char* kSecKeys = "Type keys";
constexpr const char* kSecChain = "Component chain";
constexpr const char* kSecLookup = "Owner lookup and dispatch";
constexpr const char* kSecEffectType = "Effect type";
constexpr const char* kSecEffect = "Effect on the object";
constexpr const char* kSecParams = "Effect parameters";
constexpr const char* kSecManager = "Facet manager";
constexpr const char* kSecFacet = "Facet of the object";
constexpr const char* kSecFacetChain = "Facet chain";

world::TagTable tagTable(world::Reader& mem) { return world::readTagTable(mem); }

std::string tagText(uint32_t tag) {
    if (!world::validHandle(tag)) return "-";
    const std::string n = names::tagName(names::fromPs3Tag(tag));
    return n.empty() ? hx(tag) : hx(tag) + "  " + n;
}

ImVec4 lookupColor(lab::Lookup r) { return r == lab::Lookup::Found ? pal::Success : r == lab::Lookup::Missing ? pal::Muted : pal::Danger; }

 
 
 

 
struct ArenaRow {
    world::Arena arena;
    uint32_t handle = 0, owner = 0, flags = 0, definition = 0, offset = 0;
};
struct CompRow {
    world::Accessor a;
    uint32_t handle = 0;                  
};
struct ChainView {
    uint32_t handle = 0, entry = 0, head = 0, tag = 0;
    std::vector<ArenaRow> arenas;
    std::vector<CompRow> components;
};
struct LookupView {
    uint32_t handle = 0, key = 0, slot = 0x38;
    std::string keyFrom;
    lab::OwnerLookup owner;
    std::optional<lab::Dispatch> dispatch;
};

struct CompState {
    labui::Slot<std::vector<lab::TypeKey>> keys;
    labui::Slot<ChainView> chain;
    labui::Slot<LookupView> lookup;
    std::atomic<int> keySource{0};        
    std::atomic<uint32_t> customKey{0};
    std::atomic<int> slot{0};             
    std::atomic<bool> headOnly{false};
    bool seeded = false;
};
CompState& cs() {
    static CompState s;
    return s;
}

void requireComponents(Ctx& c) {
    c.require(world::kFingerprints, "World readers");
    c.require(lab::kComponentFingerprints, "Components (lab)");
}

void runReadKeys(Ctx& c) {
    requireComponents(c);
    world::RuntimeReader mem(c.rt());
    const world::TagTable t = tagTable(mem);
    std::vector<lab::TypeKey> keys;
    for (uint32_t a : {lab::kTypeAnchorPrimary, lab::kTypeAnchorSecondary, lab::kTypeAnchorEffect}) {
        keys.push_back(lab::readTypeKey(mem, t, a));
        const lab::TypeKey& k = keys.back();
        c.log(fmt("[lab] type key %s %08X: descriptor %08X, handle %08X, key %08X%s", lab::anchorName(a), a, k.descriptor, k.handle, k.key,
                  k.ok() ? "" : (" (" + k.why + ")").c_str()));
    }
    cs().keys.set(std::move(keys));
}

void runReadChain(Ctx& c) {
    requireComponents(c);
    const uint32_t h = labui::pickedHandle();
    if (!h) throw Error("pick an object first (Local player, the live list or a handle)");
    world::RuntimeReader mem(c.rt());
    const auto obj = world::readSObject(mem, h);
    if (!obj) throw Error(hx(h) + " is not a live sObject any more");
    ChainView v;
    v.handle = h;
    v.entry = obj->addr;
    v.head = obj->componentHead;
    const world::TagTable t = tagTable(mem);
    v.tag = world::tagOfObject(mem, t, *obj);
    const world::AccessorTable table = world::readAccessorTable(mem);
    const auto arenas = world::arenaChain(table, v.head);
    for (const world::Arena& a : arenas) {
        ArenaRow r;
        r.arena = a;
        const world::Accessor acc = table.at(a.index);
        r.owner = acc.identity;
        if (const auto id = lab::poolIdentity(mem, table.pool.header, a.index, &table.entries)) r.handle = id->canonical;
        if (const auto b = mem.tryBytes(a.data, 12)) {
            r.flags = b->u32(0);
            r.definition = b->u32(4);
            r.offset = b->u32(8);
        }
        v.arenas.push_back(r);
    }
    for (const world::Accessor& a : world::componentsIn(table, arenas)) {
        CompRow cr{a};
        if (const auto id = lab::poolIdentity(mem, table.pool.header, a.index, &table.entries)) cr.handle = id->canonical;
        v.components.push_back(cr);
    }
    c.log(fmt("[lab] %08X: %zu arenas, %zu components, tag %08X", h, v.arenas.size(), v.components.size(), v.tag));
    cs().chain.set(std::move(v));
}

void runLookup(Ctx& c) {
    requireComponents(c);
    const uint32_t h = labui::pickedHandle();
    if (!h) throw Error("pick an object first (Local player, the live list or a handle)");
    world::RuntimeReader mem(c.rt());
    const world::TagTable t = tagTable(mem);
    LookupView v;
    v.handle = h;
    v.slot = cs().slot.load() ? 0x58 : 0x38;
    const int src = cs().keySource.load();
    if (src == 3) {
        v.key = cs().customKey.load();
        v.keyFrom = "custom";
    } else {
        const uint32_t anchor = src == 0 ? lab::kTypeAnchorPrimary : src == 1 ? lab::kTypeAnchorSecondary : lab::kTypeAnchorEffect;
        const lab::TypeKey k = lab::readTypeKey(mem, t, anchor);
        if (!k.ok()) throw Error(std::string("type key (") + lab::anchorName(anchor) + "): " + k.why);
        v.key = k.key;
        v.keyFrom = lab::anchorName(anchor);
    }
    v.owner = lab::findOwnerComponent(mem, t, h, v.key, cs().headOnly.load(), true);
    if (v.owner.found >= 0) v.dispatch = lab::readDispatch(mem, t, v.owner.arenas[size_t(v.owner.found)].descriptor.words[6], v.slot);
    c.log(fmt("[lab] owner lookup %08X key %08X (%s): %s%s", h, v.key, v.keyFrom.c_str(), lab::lookupName(v.owner.result),
              v.owner.why.empty() ? "" : (" - " + v.owner.why).c_str()));
    cs().lookup.set(std::move(v));
}

void seedComponentsDemo() {
    CompState& s = cs();
    if (s.seeded) return;
    s.seeded = true;
    if (!labui::demo()) return;
    std::vector<lab::TypeKey> keys(3);
    keys[0] = {lab::kTypeAnchorPrimary, 0x1BBD310, 0x8090A1C4, 0x3A0B44F1, 0x30A14E80, ""};
    keys[1] = {lab::kTypeAnchorSecondary, 0x1AB4F50, 0x8090A0F2, 0x9D1C2E07, 0x30A13900, ""};
    keys[2] = {lab::kTypeAnchorEffect, 0x1AAF520, 0x8090A221, 0x6B3E9A12, 0x30A15F40, ""};
    s.keys.set(keys);
    ChainView v;
    v.handle = 0x51FB8003;
    v.entry = 0x60163880;
    v.head = 0x2C410003;
    v.tag = 0x80A6A07E;
    const uint32_t defs[] = {0x80A6A07E, 0x8090A1C4, 0x80B42E10};
    for (uint32_t i = 0; i < 3; ++i) {
        ArenaRow r;
        r.arena = {3 + i * 17, 0x60A12000 + i * 0x1800, 0x1400 - i * 0x200};
        r.handle = 0x2C410003 + i * 17;
        r.owner = 0x51FB8003;
        r.flags = i == 1 ? 2 : 0;
        r.definition = defs[i];
        r.offset = 0x40 + i * 0x20;
        v.arenas.push_back(r);
    }
    for (uint32_t i = 0; i < 6; ++i) {
        world::Accessor a;
        a.index = 40 + i;
        a.addr = 0x30F00000 + a.index * 0x18;
        a.flags = 0xC0000000u | (0x40 + i * 0x30);
        a.identity = 0x2C410003;    
        a.word14 = 0x80800A10 + i;
        a.data = 0x60A12000 + 0x60 + i * 0x80;
        v.components.push_back({a, 0x2C450000u | a.index});
    }
    s.chain.set(v);
    LookupView l;
    l.handle = 0x51FB8003;
    l.key = 0x3A0B44F1;
    l.keyFrom = "primary (spawner)";
    l.owner.result = lab::Lookup::Found;
    l.owner.owner = l.handle;
    l.owner.object = 0x60163880;
    l.owner.head = 0x2C410003;
    for (uint32_t i = 0; i < 2; ++i) {
        lab::ArenaStep st;
        st.handle = 0x2C410003 + i * 17;
        st.index = 3 + i * 17;
        st.body = 0x60A12000 + i * 0x1800;
        st.size = 0x1400;
        st.definition = defs[i];
        st.offset = 0x40;
        st.identityOk = true;
        st.result = i == 0 ? lab::Lookup::Missing : lab::Lookup::Found;
        if (i == 1) {
            st.componentOffset = 0x1E0;
            st.descriptor.index = 4;
            const uint32_t w[7] = {0x00140002, 0x1E0, 0x80800A11, 0x0, 0x58, 0xFFFFFFFF, 0x8090A1C4};
            std::copy(w, w + 7, st.descriptor.words);
        }
        l.owner.arenas.push_back(st);
    }
    l.owner.found = 1;
    l.owner.component = 0x60A139E0;
    l.owner.runtime = 0x60A139E0;
    l.owner.child = 0x2C45002B;
    l.owner.childOk = true;
    l.owner.childOwner = 0x2C410014;
    l.owner.childTarget = 0x51FB8003;
    l.owner.childCanonical = 0x2C45002B;
    l.owner.passes = 2;
    l.owner.arenas[0].identityOk = l.owner.arenas[1].identityOk = true;
    l.owner.arenas[0].canonical = l.owner.arenas[0].handle;
    l.owner.arenas[1].canonical = l.owner.arenas[1].handle;
    lab::Dispatch d;
    d.typeTag = 0x8090A1C4;
    d.slot = 0x38;
    d.definition.tag = 0x8090A1C4;
    d.definition.body = 0x30A14E80;
    d.definition.extent = 0x40000;
    d.relative = 0x30;
    d.table = 0x30A14EC0;
    d.fn = 0x1852A30;
    d.adjust = 0;
    d.opd = 0x1852A30;
    d.opdAligned = d.opdInRange = d.tocOk = d.codeInText = true;
    d.code = 0x255FA8;
    d.toc = lab::kToc;
    const uint32_t ins[4] = {0x7C0802A6, 0x48B42A71, 0xF821FF51, 0xF80100C0};
    std::copy(ins, ins + 4, d.instructions);
    l.dispatch = d;
    s.lookup.set(l);
}

void drawObject(Ui& ui) { labui::drawPicker(ui); }

void drawKeys(Ui& ui) {
    seedComponentsDemo();
    CompState& s = cs();
    if (labui::jobButton(ui, kReadKeys, icon::Refresh,
                         "Read the three type-key anchors (read only). Anchor -> descriptor -> type handle -> tag body +4 = key. The "
                         "spawner's primary component (0x255E4C), the secondary component (0x206A8C) and the effect getter (0x1EE060) each "
                         "look a component up by one of these keys."))
        labui::post(ui, kReadKeys, runReadKeys);
    labui::readStamp(s.keys.at());
    labui::errorBox(s.keys.error());
    gap(sp::S);
    const auto keys = s.keys.get();
    if (!keys) return;
    if (beginTable("##keys", 4, 0, ImGuiTableFlags_BordersOuter)) {
        ImGui::TableSetupColumn("Anchor", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Handle", ImGuiTableColumnFlags_WidthFixed, px(84));
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, px(112));
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, px(84));
        tableHeaders();
        for (const lab::TypeKey& k : *keys) {
            ImGui::TableNextRow(0, px(30));
            ImGui::TableSetColumnIndex(0);
            cellText(lab::anchorName(k.anchor), pal::Text, FontKind::Body, type::Label);
            tooltip(fmt("anchor %08X -> descriptor %08X", k.anchor, k.descriptor).c_str());
            ImGui::TableSetColumnIndex(1);
            cellText(world::validHandle(k.handle) ? hx(k.handle).c_str() : "-", pal::Muted, FontKind::Mono, type::MonoSmall);
            ImGui::TableSetColumnIndex(2);
            ImGui::PushID(int(k.anchor));
            if (k.ok()) labui::copyCell(hx(k.key), "key", pal::Accent);
            else cellText("-", pal::Faint);
            ImGui::PopID();
            ImGui::TableSetColumnIndex(3);
            cellText(k.ok() ? "published" : "not yet", k.ok() ? pal::Success : pal::Warning, FontKind::Body, type::Label);
            if (!k.ok()) tooltip(k.why.c_str());
        }
        endTable();
    }
}

void drawChain(Ui& ui) {
    seedComponentsDemo();
    CompState& s = cs();
    {
        DisabledScope none(labui::pickedHandle() == 0, "Pick an object first");
        if (labui::jobButton(ui, kReadChain, icon::Refresh,
                             "Walk sObject +0x3C through the component accessor pool (one read of the table). Every component of an object "
                             "lives in one of its arenas: the chain starts at sObject +0x3C and follows accessor +0x14. Each arena body "
                             "starts with {flags, definition tag, definition offset}; flags bit 1 hides it from the type lookup."))
            labui::post(ui, kReadChain, runReadChain);
    }
    labui::readStamp(s.chain.at());
    labui::errorBox(s.chain.error());
    gap(sp::S);
    const auto v = s.chain.get();
    if (!v) return;
    if (labui::kvBegin("##chainhdr", 120)) {
        labui::kvHex("Object", v->handle, "handle", pal::Accent);
        labui::kvHex("sObject Entry", v->entry, "entry");
        labui::kvHex("+0x3C Head", v->head, "arena handle");
        labui::kv("Tag", tagText(v->tag), pal::Text, true, "The object's definition tag: *(resolve(+0x3C) + 4)");
        labui::kvEnd();
    }
    gap(sp::M);
    fieldLabel(fmt("ARENAS  (%zu)", v->arenas.size()).c_str());
    gap(sp::XS);
    if (v->arenas.empty()) caption("The chain is empty or broken (head -1, an unallocated link, or a loop).", pal::Faint);
    else if (beginTable("##arenas", 5, 0, ImGuiTableFlags_BordersOuter)) {
        ImGui::TableSetupColumn("Accessor", ImGuiTableColumnFlags_WidthFixed, px(84));
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, px(60));
        ImGui::TableSetupColumn("Flags", ImGuiTableColumnFlags_WidthFixed, px(52));
        ImGui::TableSetupColumn("Definition", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthFixed, px(52));
        tableHeaders();
        for (const ArenaRow& a : v->arenas) {
            ImGui::TableNextRow(0, px(30));
            ImGui::TableSetColumnIndex(0);
            cellText(a.handle ? hx(a.handle).c_str() : "?", pal::Text, FontKind::Mono, type::MonoSmall);
            tooltip(fmt("index %u, data %08X, %u bytes; handle from the pool identity table; owner (entry +0xC, 0x1F8C1C) %08X", a.arena.index,
                        a.arena.data, a.arena.size, a.owner)
                        .c_str());
            ImGui::TableSetColumnIndex(1);
            cellText(fmt("0x%X", a.arena.size).c_str(), pal::Muted, FontKind::Mono, type::MonoSmall);
            ImGui::TableSetColumnIndex(2);
            cellText((a.flags & 2) ? "skip" : fmt("%X", a.flags).c_str(), (a.flags & 2) ? pal::Warning : pal::Muted, FontKind::Mono, type::MonoSmall);
            tooltip(fmt("arena flags %08X%s", a.flags, (a.flags & 2) ? ": bit 1 set, the type lookup skips this arena" : "").c_str());
            ImGui::TableSetColumnIndex(3);
            cellText(tagText(a.definition).c_str(), pal::Text, FontKind::Mono, type::MonoSmall);
            ImGui::TableSetColumnIndex(4);
            cellText(fmt("+%X", a.offset).c_str(), pal::Muted, FontKind::Mono, type::MonoSmall);
        }
        endTable();
    }
    gap(sp::M);
    fieldLabel(fmt("COMPONENTS INSIDE THE ARENAS  (%zu)", v->components.size()).c_str());
    gap(sp::XS);
    if (v->components.empty()) {
        caption("No allocated accessor (0x20 bytes or more) lies inside these arenas.", pal::Faint);
        return;
    }
    if (beginTable("##comps", 5, std::min(300.0f, 34.0f + 30.0f * float(v->components.size())), ImGuiTableFlags_BordersOuter)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, px(52));
        ImGui::TableSetupColumn("Handle", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Owner", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, px(60));
        ImGui::TableSetupColumn("Entry +0x14", ImGuiTableColumnFlags_WidthStretch);
        tableHeaders();
        for (const CompRow& c : v->components) {
            const world::Accessor& a = c.a;
            ImGui::TableNextRow(0, px(30));
            ImGui::TableSetColumnIndex(0);
            cellText(std::to_string(a.index).c_str(), pal::Muted);
            ImGui::TableSetColumnIndex(1);
            cellText(c.handle ? hx(c.handle).c_str() : "?", pal::Text, FontKind::Mono, type::MonoSmall);
            tooltip(fmt("data %08X (handle from the pool identity table)", a.data).c_str());
            ImGui::TableSetColumnIndex(2);
            cellText(hx(a.identity).c_str(), pal::Muted, FontKind::Mono, type::MonoSmall);
            tooltip("Accessor entry +0xC: the owning arena (0x1F8C1C)");
            ImGui::TableSetColumnIndex(3);
            cellText(fmt("0x%X", a.size()).c_str(), pal::Muted, FontKind::Mono, type::MonoSmall);
            ImGui::TableSetColumnIndex(4);
            cellText(hx(a.cls()).c_str(), pal::Text, FontKind::Mono, type::MonoSmall);
        }
        endTable();
    }
}

void drawDispatch(const lab::Dispatch& d) {
    fieldLabel(fmt("DISPATCH  (XBOX SLOT +0x%X, CONSUMER %08X)", d.slot, d.consumer()).c_str());
    gap(sp::XS);
    if (labui::kvBegin("##disp", 150)) {
        labui::kv("Type Definition", tagText(d.typeTag) + (d.definition.body ? "  body " + hx(d.definition.body) : std::string()));
        if (d.table) {
            labui::kvHex("Table", d.table, "table", pal::Text, "type body + 0x10 + *(type body + 0x10)");
            labui::kv("Member Pointer", hx(d.fn) + "  adjust " + hx(d.adjust), pal::Text, true, "{fn, this-adjust}: odd fn = vtable offset + 1");
        }
        if (d.table && d.isVirtual()) labui::kv("Kind", fmt("virtual: vtable +0x%X (depends on the instance)", d.vtableOffset()), pal::Info, false);
        else if (d.opd) {
            labui::kvHex("OPD", d.opd, "OPD");
            if (d.code) labui::kvHex("Code", d.code, "code address", pal::Accent);
            if (d.code)
                labui::kv("First Words", fmt("%08X %08X %08X %08X", d.instructions[0], d.instructions[1], d.instructions[2], d.instructions[3]),
                          pal::Muted);
        }
        labui::kvEnd();
    }
    if (d.opd) {
        gap(sp::XS);
        labui::check(d.opdAligned && d.opdInRange, "OPD 8-aligned inside .opd 01849508..01889798");
        labui::check(d.tocOk, "OPD +4 is the EBOOT TOC 01891798");
        labui::check(d.codeInText, "code inside the text segment");
    }
    if (!d.ok()) labui::warnLine(d.why);
}

void drawLookup(Ui& ui) {
    seedComponentsDemo();
    CompState& s = cs();
    fieldLabel("KEY");
    gap(sp::XS);
    int src = s.keySource.load();
    std::vector<std::string> sources = {"Primary", "Secondary", "Effect", "Custom"};
    if (pillTabs("##keysrc", sources, &src)) {
        s.keySource = src;
         
         
        if (src == 0) {
            s.slot = 0;
            s.headOnly = false;
        } else if (src == 1) {
            s.slot = 1;
            s.headOnly = true;
        }
    }
    tooltip("Primary: the spawner's component (chain lookup, slot +0x38). Secondary: the secondary component (head arena only, slot "
            "+0x58). Effect: the effect key. Custom: any key you type.");
    if (src == 3) {
        ImGui::SameLine(0, px(10));
        uint32_t k = s.customKey.load();
        if (inputHex("##customkey", &k, 120)) s.customKey = k;
        tooltip("A component type key (tag body +4 of a component type)");
    }
    ImGui::SameLine(0, px(18));
    int slot = s.slot.load();
    std::vector<std::string> slots = {"Slot +0x38", "Slot +0x58"};
    if (pillTabs("##slot", slots, &slot)) s.slot = slot;
    tooltip("Xbox dispatch slot: +0x38 (PS3 consumer 0x291AA0) or +0x58 (0x291714)");
    gap(sp::S);
    bool head = s.headOnly.load();
    if (checkbox("Head Arena Only", &head)) s.headOnly = head;
    tooltip("The direct lookup 0x147518 the secondary component uses");
    gap(sp::S);
    {
        DisabledScope none(labui::pickedHandle() == 0, "Pick an object first");
        if (labui::jobButton(ui, kLookup, icon::Search,
                             "Walk the owner's arenas, decode each definition's hash table for the key, then read the dispatch slot. Mirrors "
                             "the native mode-1 lookup: the first arena (flags bit 1 clear) whose definition maps the key wins. Nothing is "
                             "called: the definitions are read through a bounded window, since the PS3 tag heap block sizes are not mapped.",
                             Tone::Primary, "Look Up"))
            labui::post(ui, kLookup, runLookup);
    }
    labui::readStamp(s.lookup.at());
    labui::errorBox(s.lookup.error());
    const auto v = s.lookup.get();
    gap(sp::M);
    if (!v) return;
    const lab::OwnerLookup& o = v->owner;
    pill(fmt("%s  key %08X (%s)", lab::lookupName(o.result), v->key, v->keyFrom.c_str()).c_str(), lookupColor(o.result),
         o.result == lab::Lookup::Found ? icon::Check : o.result == lab::Lookup::Missing ? icon::Info : icon::Warning);
    if (!o.why.empty()) {
        gap(sp::XS);
        labui::warnLine(o.why);
    }
    gap(sp::S);
    if (!o.arenas.empty() && beginTable("##steps", 7, 0, ImGuiTableFlags_BordersOuter)) {
        ImGui::TableSetupColumn("Arena", ImGuiTableColumnFlags_WidthFixed, px(86));
        ImGui::TableSetupColumn("Identity", ImGuiTableColumnFlags_WidthFixed, px(70));
        ImGui::TableSetupColumn("Body", ImGuiTableColumnFlags_WidthFixed, px(86));
        ImGui::TableSetupColumn("Definition", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthFixed, px(56));
        ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthFixed, px(90));
        ImGui::TableSetupColumn("Component", ImGuiTableColumnFlags_WidthFixed, px(86));
        tableHeaders();
        for (size_t i = 0; i < o.arenas.size(); ++i) {
            const lab::ArenaStep& a = o.arenas[i];
            ImGui::TableNextRow(0, px(30));
            ImGui::TableSetColumnIndex(0);
            cellText(hx(a.handle).c_str(), int(i) == o.found ? pal::Accent : pal::Text, FontKind::Mono, type::MonoSmall);
            ImGui::TableSetColumnIndex(1);
            cellText(a.identityOk ? "ok" : "stale", a.identityOk ? pal::Success : pal::Danger, FontKind::Body, type::Label);
            tooltip(fmt("Pool identity word %08X gives %08X (Xbox SequencePoolHandle: it must be this handle)", a.identity, a.canonical).c_str());
            ImGui::TableSetColumnIndex(2);
            cellText(hx(a.body).c_str(), pal::Muted, FontKind::Mono, type::MonoSmall);
            ImGui::TableSetColumnIndex(3);
            cellText(tagText(a.definition).c_str(), pal::Text, FontKind::Mono, type::MonoSmall);
            ImGui::TableSetColumnIndex(4);
            cellText(fmt("+%X", a.offset).c_str(), pal::Muted, FontKind::Mono, type::MonoSmall);
            ImGui::TableSetColumnIndex(5);
            cellText(a.skipped ? "skipped" : lab::lookupName(a.result), a.skipped ? pal::Faint : lookupColor(a.result), FontKind::Body, type::Label);
            if (!a.why.empty()) tooltip(a.why.c_str());
            ImGui::TableSetColumnIndex(6);
            cellText(a.result == lab::Lookup::Found ? fmt("+%X", a.componentOffset).c_str() : "-", pal::Muted, FontKind::Mono, type::MonoSmall);
        }
        endTable();
    }
    if (o.found < 0) return;
    const lab::ArenaStep& f = o.arenas[size_t(o.found)];
    gap(sp::M);
    const float w = availWidth();
    const bool split = w > px(720);
    const float half = split ? std::floor((w - px(28)) * 0.5f) : w;
    ImGui::BeginGroup();
    fieldLabel("COMPONENT");
    gap(sp::XS);
    if (labui::kvBegin("##found", 130, half)) {
        if (o.runtime)
            labui::kvHex("Runtime", o.runtime, "runtime component", pal::Accent,
                         "arena body + descriptor word 1: the component the mode-1 lookup returns (component_owner.inl)");
        else labui::kv("Runtime", "-", pal::Faint, true, "descriptor word 1 is not a runtime offset inside the arena");
        labui::kvHex("Record", o.component, "record", pal::Text, "arena body + records +12 (the effect route's record)");
        labui::kv("Own Accessor", world::validHandle(o.child) ? hx(o.child) + (o.childOk ? "  resolves back" : "  does not resolve back") : "-",
                  o.childOk ? pal::Success : pal::Muted, true,
                  o.childOk ? "*(record + 0x14): allocated, data == record, target == owner, pool identity maps it back"
                            : (o.childWhy.empty() ? "not checked" : o.childWhy.c_str()));
        if (world::validHandle(o.child)) labui::kv("Owner / Target", hx(o.childOwner) + "  /  " + hx(o.childTarget), pal::Muted, true,
                                       "accessor entry +0xC (its owning arena, 0x1F8C1C) / data +0x1C (the sObject)");
        labui::kv("Descriptor", fmt("#%u", f.descriptor.index), pal::Muted);
        labui::kvEnd();
    }
    mono(fmt("%08X %08X %08X %08X", f.descriptor.words[0], f.descriptor.words[1], f.descriptor.words[2], f.descriptor.words[3]).c_str(),
         pal::Muted, type::MonoSmall);
    mono(fmt("%08X %08X %08X", f.descriptor.words[4], f.descriptor.words[5], f.descriptor.words[6]).c_str(), pal::Muted, type::MonoSmall);
    ImGui::EndGroup();
    if (v->dispatch) {
        if (split) ImGui::SameLine(0, px(28));
        else gap(sp::M);
        ImGui::BeginGroup();
        drawDispatch(*v->dispatch);
        ImGui::EndGroup();
    }
}

 
 
 

struct EffectView {
    uint32_t handle = 0;
    lab::EffectType type;
    lab::OwnerLookup owner;
};
struct CrossView {
    uint32_t handle = 0, ours = 0;
    lab::NativeEffect native;
};
struct ParamView {
    lab::EffectParameters params;
    std::map<uint64_t, uint32_t> previous;    
};

struct EffectState {
    labui::Slot<lab::EffectType> type;
    labui::Slot<EffectView> effect;
    labui::Slot<CrossView> cross;
    labui::Slot<ParamView> params;
    std::string filter;
    bool seeded = false;
};
EffectState& es() {
    static EffectState s;
    return s;
}

void requireEffects(Ctx& c) {
    requireComponents(c);
    c.require(lab::kEffectFingerprints, "Effects (lab)");
}

void runReadEffectType(Ctx& c) {
    requireEffects(c);
    world::RuntimeReader mem(c.rt());
    const lab::EffectType e = lab::readEffectType(mem, tagTable(mem));
    c.log(fmt("[lab] effect type: anchor %08X -> %08X (expected %08X), handle %08X, key %08X%s", lab::kTypeAnchorEffect, e.type.descriptor,
              lab::kEffectDescriptor, e.type.handle, e.type.key, e.type.ok() ? "" : (" - " + e.type.why).c_str()));
    es().type.set(e);
}

EffectView findEffect(Ctx& c) {
    requireEffects(c);
    const uint32_t h = labui::pickedHandle();
    if (!h) throw Error("pick an object first (Local player, the live list or a handle)");
    world::RuntimeReader mem(c.rt());
    const world::TagTable t = tagTable(mem);
    EffectView v;
    v.handle = h;
    v.type = lab::readEffectType(mem, t);
    es().type.set(v.type);
    if (!v.type.type.ok()) throw Error("effect type: " + v.type.type.why);
    v.owner = lab::findOwnerComponent(mem, t, h, v.type.type.key, false, false, lab::OwnerMode::Effect);
    c.log(fmt("[lab] effect on %08X: %s, component %08X, own accessor %08X%s", h, lab::lookupName(v.owner.result), v.owner.component, v.owner.child,
              v.owner.why.empty() ? "" : (" - " + v.owner.why).c_str()));
    return v;
}

void runFindEffect(Ctx& c) { es().effect.set(findEffect(c)); }

void runCrossCheck(Ctx& c) {
    const EffectView v = findEffect(c);
    es().effect.set(v);
    CrossView x;
    x.handle = v.handle;
    x.ours = v.owner.result == lab::Lookup::Found ? v.owner.child : 0xFFFFFFFFu;
    x.native = lab::nativeEffectOf(c.rt(), v.handle, v.type.type.handle);
    c.log(fmt("[lab] effect getter 0x1EE060(%08X): %s -> %08X; the PC lookup says %08X", v.handle,
              x.native.called ? "called" : ("refused: " + x.native.refused).c_str(), x.native.handle, x.ours));
    es().cross.set(x);
}

 
 
void readParams(Ctx& c, bool watching) {
    std::optional<EffectView> v = es().effect.get();
    const bool other = !v || v->handle != labui::pickedHandle();
    if (other || (!watching && v->owner.result != lab::Lookup::Found)) {
        v = findEffect(c);
        es().effect.set(*v);
    }
    if (v->owner.result != lab::Lookup::Found) throw Error("the object has no effect component (" + std::string(lab::lookupName(v->owner.result)) + ")");
    if (!v->owner.childAccessor || !v->owner.childOk) throw Error("the effect component's own accessor does not resolve back to it");
    world::RuntimeReader mem(c.rt());
    ParamView pv;
    pv.params = lab::readEffectParameters(mem, tagTable(mem), v->owner.childAccessor->data, v->owner.childAccessor->size());
    if (const auto old = es().params.get(); old && old->params.record == pv.params.record)
        for (const lab::ParamComponent& pc : old->params.components)
            for (size_t i = 0; i < pc.values.size(); ++i) pv.previous[uint64_t(pc.index) << 32 | i] = pc.values[i];
    es().params.set(std::move(pv));
}

void runReadParams(Ctx& c) { readParams(c, false); }

void seedEffectsDemo() {
    EffectState& s = es();
    if (s.seeded) return;
    s.seeded = true;
    if (!labui::demo()) return;
    lab::EffectType t;
    t.type = {lab::kTypeAnchorEffect, lab::kEffectDescriptor, 0x8090A221, 0x6B3E9A12, 0x30A15F40, ""};
    t.descriptorExpected = true;
    s.type.set(t);
    EffectView v;
    v.handle = 0x51FB8003;
    v.type = t;
    v.owner.result = lab::Lookup::Found;
    v.owner.owner = v.handle;
    v.owner.component = 0x60A139E0;
    v.owner.child = 0x2C45002B;
    v.owner.childOk = true;
    v.owner.childOwner = 0x2C410014;
    v.owner.childTarget = 0x51FB8003;
    v.owner.childCanonical = 0x2C45002B;
    v.owner.mode = lab::OwnerMode::Effect;
    v.owner.passes = 2;
    lab::ArenaStep st;
    st.handle = 0x2C410014;
    st.body = 0x60A13800;
    st.size = 0x1400;
    st.definition = 0x80B42E10;
    st.offset = 0x40;
    st.result = lab::Lookup::Found;
    st.componentOffset = 0x1E0;
    st.descriptor.index = 2;
    {
        const uint32_t w[7] = {0x00100001, 0x1E0, 0x80801B07, 0, 0x60, 0xFFFFFFFF, 0x8090A221};
        std::copy(w, w + 7, st.descriptor.words);
    }
    v.owner.arenas.push_back(st);
    v.owner.found = 0;
    s.effect.set(v);
    CrossView x;
    x.handle = v.handle;
    x.ours = 0x2C45002B;
    x.native.called = true;
    x.native.handle = 0x2C45002B;
    s.cross.set(x);
    ParamView pv;
    pv.params.record = 0x60A139E0;
    pv.params.recordSize = 0x180;
    pv.params.definition.tag = 0x80B42E10;
    pv.params.definition.body = 0x30C01200;
    pv.params.definition.extent = 0x40000;
    pv.params.root = 0x140;
    pv.params.count = 3;
    const uint32_t keys[3][3] = {{0xA1B2C3D4, 0x5E6F7081, 0}, {0x0C0FFEE1, 0, 0}, {0x13579BDF, 0x2468ACE0, 0xDEADC0DE}};
    const uint32_t vals[3][3] = {{0x3F800000, 0x00000004, 0}, {0x41200000, 0, 0}, {0x00000001, 0x3E99999A, 0xFFFFFFFF}};
    const int counts[3] = {2, 1, 3};
    for (uint32_t ci = 0; ci < 3; ++ci) {
        lab::ParamComponent pc;
        pc.index = ci;
        pc.offset = 0x300 + ci * 0x40;
        pc.kind = 2;
        pc.runtime = 0x80 + ci * 0x20;
        pc.notification = ci == 1 ? 0xFFFFFFFF : 0x2C460010 + ci;
        for (int k = 0; k < counts[ci]; ++k) {
            pc.keys.push_back(keys[ci][k]);
            pc.values.push_back(vals[ci][k]);
        }
        pv.params.components.push_back(pc);
    }
    pv.previous[uint64_t(0) << 32 | 1] = 0x00000003;
    s.params.set(pv);
}

void drawEffectType(Ui& ui) {
    seedEffectsDemo();
    EffectState& s = es();
    if (labui::jobButton(ui, kReadEffectType, icon::Refresh,
                         "Read the effect anchor chain (read only). Anchor 0x18DF174 must hold 0x1AAF520 (verified EBOOT relocation); that "
                         "descriptor publishes the effect type handle once a level is loaded, and the handle's tag body +4 is the key every "
                         "effect lookup uses."))
        labui::post(ui, kReadEffectType, runReadEffectType);
    labui::readStamp(s.type.at());
    labui::errorBox(s.type.error());
    gap(sp::S);
    const auto t = s.type.get();
    if (!t) return;
    if (labui::kvBegin("##etype", 130)) {
        labui::kv("Anchor", hx(lab::kTypeAnchorEffect) + "  ->  " + hx(t->type.descriptor), t->descriptorExpected ? pal::Text : pal::Warning);
        labui::kv("Type Handle", world::validHandle(t->type.handle) ? hx(t->type.handle) : "not published", pal::Text);
        if (t->type.ok()) labui::kvHex("Key", t->type.key, "effect key", pal::Accent);
        labui::kvEnd();
    }
    gap(sp::XS);
    labui::check(t->descriptorExpected, "anchor holds the expected descriptor 01AAF520");
    labui::check(t->type.ok(), t->type.ok() ? "type handle resolves; key read twice and stable" : t->type.why);
}

void drawEffect(Ui& ui) {
    seedEffectsDemo();
    EffectState& s = es();
    {
        DisabledScope none(labui::pickedHandle() == 0, "Pick an object first");
        if (labui::jobButton(ui, kFindEffect, icon::Search,
                             "Owner lookup with the effect key (read only): finds the object's effect component the way the effect getter "
                             "does (first arena whose definition maps the effect key), then checks that the component's own accessor (+0x14) "
                             "resolves back to it.",
                             Tone::Primary, "Find Effect"))
            labui::post(ui, kFindEffect, runFindEffect);
        ImGui::SameLine(0, px(8));
        if (labui::jobButton(ui, kCrossCheck, icon::Shield,
                             "Calls the game's effect type getter 0x1EE060 once through the guard stub (the object's identity and the published "
                             "effect type are checked on the game thread in the same frame) and compares its answer with the PC lookup. It only "
                             "writes Blackrock's own scratch.",
                             Tone::Secondary, "Cross-Check with the Game"))
            labui::post(ui, kCrossCheck, runCrossCheck);
    }
    labui::readStamp(s.effect.at());
    labui::errorBox(s.effect.error());
    labui::errorBox(s.cross.error());
    gap(sp::S);
    const auto v = s.effect.get();
    if (!v) return;
    const lab::OwnerLookup& o = v->owner;
    pill(fmt("%s on %08X", lab::lookupName(o.result), v->handle).c_str(), lookupColor(o.result));
    if (!o.why.empty()) {
        gap(sp::XS);
        labui::warnLine(o.why);
    }
    if (o.found >= 0) {
        const lab::ArenaStep& a = o.arenas[size_t(o.found)];
        gap(sp::S);
        if (labui::kvBegin("##eff", 130)) {
            labui::kvHex("Effect Record", o.component, "effect record", pal::Accent);
            labui::kv("Own Accessor", hx(o.child) + (o.childOk ? "  resolves back" : "  does NOT resolve back"), o.childOk ? pal::Success : pal::Warning,
                      true,
                      o.childOk ? "*(record + 0x14): allocated, data == record, target (+0x1C) == the object, pool identity maps it back"
                                : o.childWhy.c_str());
            labui::kv("Owner / Target", hx(o.childOwner) + "  /  " + hx(o.childTarget), pal::Muted, true,
                      "accessor entry +0xC (its owning arena, 0x1F8C1C) / data +0x1C (the sObject it belongs to)");
            labui::kv("Arena", hx(a.handle) + "  body " + hx(a.body));
            labui::kv("Definition", tagText(a.definition) + fmt("  +%X", a.offset));
            labui::kv("Descriptor", fmt("#%u  runtime +%X  type %08X", a.descriptor.index, a.descriptor.words[1], a.descriptor.words[6]), pal::Muted);
            labui::kvEnd();
        }
    }
    const auto x = s.cross.get();
    if (x && x->handle == v->handle) {
        gap(sp::S);
        fieldLabel("GAME CROSS-CHECK  (0x1EE060)");
        gap(sp::XS);
        if (!x->native.called) labui::warnLine("refused: " + x->native.refused);
        else {
            const bool same = x->native.handle == x->ours;
            labui::check(same, fmt("the game says %08X, the PC lookup %08X", x->native.handle, x->ours),
                         "Equal handles confirm the PS3 effect-owner decoding for this object");
        }
    }
}

void drawParams(Ui& ui) {
    seedEffectsDemo();
    EffectState& s = es();
    {
        DisabledScope none(labui::pickedHandle() == 0, "Pick an object first");
        if (labui::jobButton(ui, kReadParams, icon::Refresh,
                             "Read the authored parameter keys and the live values of the object's effect. Authored side: definition root "
                             "+0xC0 count, +0xC4 component array; kind-2 components list their keys (+0x2C count, +0x30 array). Runtime "
                             "side: record +0x5C table, value array at component +0x18, notification handle at +0x10."))
            labui::post(ui, kReadParams, runReadParams);
    }
    ImGui::SameLine(0, px(12));
    searchField("##pfilter", &s.filter, "Filter Keys", 200);
    labui::readStamp(s.params.at());
    labui::errorBox(s.params.error());
    gap(sp::S);
    const auto pv = s.params.get();
    if (!pv) return;
    const lab::EffectParameters& p = pv->params;
    if (!p.ok()) {
        labui::warnLine(p.why);
        return;
    }
    caption(fmt("record %08X (%u bytes)  \xC2\xB7  definition %s  \xC2\xB7  root +%X  \xC2\xB7  %u components, %zu keys", p.record, p.recordSize,
                hx(p.definition.tag).c_str(), p.root, p.count, p.keyCount())
                .c_str(),
            pal::Muted);
    gap(sp::S);
    std::string f;
    for (char ch : s.filter) f += char(std::tolower(static_cast<unsigned char>(ch)));
    size_t shownRows = 0;    
    for (const lab::ParamComponent& pc : p.components) shownRows += std::max<size_t>(pc.keys.size(), 1);
    if (beginTable("##params", 6, std::min(380.0f, 40.0f + 30.0f * float(std::max<size_t>(shownRows, 1))),
                   ImGuiTableFlags_BordersOuter)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Component", ImGuiTableColumnFlags_WidthFixed, px(84));
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, px(92));
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, px(92));
        ImGui::TableSetupColumn("As Float", ImGuiTableColumnFlags_WidthFixed, px(100));
        ImGui::TableSetupColumn("Notification", ImGuiTableColumnFlags_WidthFixed, px(96));
        ImGui::TableSetupColumn("Notes", ImGuiTableColumnFlags_WidthStretch);
        tableHeaders();
        for (const lab::ParamComponent& c : p.components) {
            if (c.keys.empty()) {
                if (!f.empty()) continue;
                ImGui::TableNextRow(0, px(30));
                ImGui::TableSetColumnIndex(0);
                cellText(fmt("#%u", c.index).c_str(), pal::Muted);
                ImGui::TableSetColumnIndex(1);
                cellText("-", pal::Faint);
                ImGui::TableSetColumnIndex(5);
                cellText(!c.why.empty() ? c.why.c_str() : fmt("kind %u (no keyed parameters)", c.kind).c_str(), c.why.empty() ? pal::Faint : pal::Warning);
                continue;
            }
            for (size_t i = 0; i < c.keys.size(); ++i) {
                if (!f.empty() && hx(c.keys[i]).find(f) == std::string::npos && fmt("%08x", c.keys[i]).find(f) == std::string::npos) continue;
                ImGui::TableNextRow(0, px(30));
                ImGui::TableSetColumnIndex(0);
                cellText(fmt("#%u", c.index).c_str(), pal::Muted);
                ImGui::TableSetColumnIndex(1);
                ImGui::PushID(int(c.index * 1000 + i));
                labui::copyCell(hx(c.keys[i]), "parameter key");
                ImGui::PopID();
                ImGui::TableSetColumnIndex(2);
                if (i < c.values.size()) {
                    auto it = pv->previous.find(uint64_t(c.index) << 32 | i);
                    const bool changed = it != pv->previous.end() && it->second != c.values[i];
                    cellText(hx(c.values[i]).c_str(), changed ? pal::Accent : pal::Text, FontKind::Mono, type::MonoSmall);
                    if (changed) tooltip(("was " + hx(it->second)).c_str());
                    ImGui::TableSetColumnIndex(3);
                    const float fv = world::bitsFloat(c.values[i]);
                    const bool plausible = std::isfinite(fv) && (fv == 0.0f || (std::fabs(fv) >= 1e-6f && std::fabs(fv) < 1e9f));
                    cellText(plausible ? fmt("%g", double(fv)).c_str() : "-", pal::Muted, FontKind::Mono, type::MonoSmall);
                } else {
                    cellText("-", pal::Faint);
                }
                ImGui::TableSetColumnIndex(4);
                cellText(world::validHandle(c.notification) ? hx(c.notification).c_str() : "-", pal::Faint, FontKind::Mono, type::MonoSmall);
                ImGui::TableSetColumnIndex(5);
                if (!c.why.empty()) cellText(c.why.c_str(), pal::Warning);
            }
        }
        endTable();
    }
}

void watchParams(Ctx& c, bool on) {
    if (!on) {
        c.stopLoop(kWatchLoop);
        return;
    }
    readParams(c, false);    
    c.startLoop(kWatchLoop, 500, [](Ctx& lc) {
        try {
            readParams(lc, true);
        } catch (const Error& e) {
            es().params.fail(e.what());    
        }
    });
}

 
 
 

struct ManagerView {
    lab::FacetManagerView manager;        
    lab::BlockCheck lookup;
    lab::DeferredContext deferred;
    std::vector<lab::ActivityBundle> bundles;
    std::string bundlesWhy;
};
struct ChainResult {
    uint32_t head = 0;
    std::string from, why;
    std::vector<lab::ChainNode> nodes;
};

struct FacetState {
    labui::Slot<ManagerView> manager;
    labui::Slot<lab::FacetOfObject> facet;
    labui::Slot<ChainResult> chain;
    std::atomic<int> region{-1};          
    std::atomic<int> slot{0};
    bool seeded = false;
};
FacetState& fs() {
    static FacetState s;
    return s;
}

void requireFacets(Ctx& c) {
    c.require(world::kFingerprints, "World readers");
    c.require(lab::kFacetFingerprints, "Facets (lab)");
}

void runReadFacetManager(Ctx& c) {
    requireFacets(c);
    world::RuntimeReader mem(c.rt());
    const lab::RegistrySnapshot reg = lab::readRegistry(mem);
    ManagerView v;
    v.manager = lab::readFacetManager(mem, reg);
    v.manager.body = world::Bytes();
    v.lookup = lab::checkBlock(mem, reg, lab::kLookupSpec);
    v.deferred = lab::readDeferredContext(mem, reg);
    v.bundles = lab::readActivityBundles(mem, v.bundlesWhy);
    c.log(fmt("[lab] facet manager %s, %u regions / %u heads; components lookup %s; deferred context %s (mode %u)",
              v.manager.check.ok() ? "valid" : ("INVALID: " + v.manager.check.failures.front()).c_str(), v.manager.directory.regions,
              v.manager.directory.total, v.lookup.ok() ? "valid" : "invalid", v.deferred.check.ok() ? "valid" : "invalid", v.deferred.mode));
    fs().manager.set(std::move(v));
}

void runReadFacet(Ctx& c) {
    requireFacets(c);
    const uint32_t h = labui::pickedHandle();
    if (!h) throw Error("pick an object first (Local player, the live list or a handle)");
    world::RuntimeReader mem(c.rt());
    const lab::RegistrySnapshot reg = lab::readRegistry(mem);
    const lab::FacetManagerView m = lab::readFacetManager(mem, reg);
    lab::FacetOfObject f = lab::readFacetOfObject(mem, m, reg, h);
    c.log(fmt("[lab] facet of %08X: map %X -> %08X, route %s%s", h, f.mapIndex, f.facet, lab::routeName(f.route), f.ok() ? "" : (" - " + f.why).c_str()));
    fs().facet.set(std::move(f));
}

void runWalkChain(Ctx& c) {
    requireFacets(c);
    world::RuntimeReader mem(c.rt());
    const lab::RegistrySnapshot reg = lab::readRegistry(mem);
    const lab::FacetManagerView m = lab::readFacetManager(mem, reg);
    if (!m.check.ok()) throw Error("facet manager: " + m.check.failures.front());
    ChainResult r;
    const int region = fs().region.load();
    if (region < 0) {
        const uint32_t h = labui::pickedHandle();
        if (!h) throw Error("pick an object first, or choose a directory region");
        const lab::FacetOfObject f = lab::readFacetOfObject(mem, m, reg, h);
        if (f.facet == 0xFFFFFFFFu) throw Error("the object has no facet: " + f.why);
        r.head = f.facet;
        r.from = "the facet of " + hx(h);
    } else {
        const auto& rows = m.directory.rows;
        if (size_t(region) >= rows.size()) throw Error("the directory has no region " + std::to_string(region));
        const auto& reg0 = rows[size_t(region)];
        const int slot = fs().slot.load();
        if (slot < 0 || size_t(slot) >= reg0.heads.size()) throw Error(fmt("region %d has %zu heads", region, reg0.heads.size()));
        r.head = reg0.heads[size_t(slot)];
        r.from = fmt("directory region %d head %d", region, slot);
    }
    r.nodes = lab::walkChain(mem, m.facets, r.head, r.why);
    c.log(fmt("[lab] facet chain from %08X: %zu nodes%s", r.head, r.nodes.size(), r.why.empty() ? "" : (" - " + r.why).c_str()));
    fs().chain.set(std::move(r));
}

void seedFacetsDemo() {
    FacetState& s = fs();
    if (s.seeded) return;
    s.seeded = true;
    if (!labui::demo()) return;
    ManagerView v;
    v.manager.check.body = 0x3291C010;
    v.manager.check.slot = lab::kFacetManagerSlot;
    v.manager.check.header = {0x3A, lab::kFacetManagerHash, 0, 0, 0, 25};
    v.manager.check.allocation = 0x1917A40;
    v.manager.check.payload = 0x32900000;
    v.manager.check.extent = 0x40000;
    v.manager.directory.regions = 3;
    v.manager.directory.total = 7;
    for (uint32_t i = 0; i < 3; ++i) {
        lab::DirectoryRegion r;
        r.index = i;
        r.count = i == 1 ? 3 : 2;
        r.start = i == 0 ? 0 : i == 1 ? 2 : 5;
        r.handle = 0x7F100000u | i;
        for (uint32_t k = 0; k < r.count; ++k) r.heads.push_back(0x40C80000u | (i * 9 + k));
        v.manager.directory.rows.push_back(r);
    }
    v.lookup.body = 0x32940010;
    v.lookup.header = {0x3B, lab::kComponentsLookupHash, 0, 1, 0, 25};
    v.deferred.check.body = 0x33D00810;
    v.deferred.check.header = {0x12C, lab::kDeferredContextHash, 1, 1, 3, 25};
    v.deferred.mode = 1;
    v.deferred.context = 0x33D00900;
    for (uint32_t i = 0; i < 4; ++i) v.bundles.push_back({i, 0x91A20000u + i * 0x111, i, 0x10 + i, 0x7F100000u | i});
    s.manager.set(v);
    lab::FacetOfObject f;
    f.object = 0x51FB8003;
    f.entry = 0x60163880;
    f.arena = 0x2C410003;
    f.domain = 0;
    f.mapIndex = 3;
    f.facet = 0x40C80009;
    lab::FacetRecord r;
    r.handle = 0x40C80009;
    r.index = 9;
    r.addr = 0x60B00000 + 9 * 0x120;
    r.identity = 0x40C8;
    r.identityOk = true;
    r.dirtyPointer = r.addr + 0x98;
    r.dirtyDescriptor = 0xFFF8080F;
    r.dirty[0] = 0x00000004;
    r.self = r.handle;
    r.linked = 0x2C45002C;
    r.arena = f.arena;
    r.networkObject = 0x00130002;
    r.key = 0xFFFFFFFF;
    r.marker = 0xFFFF0000;
    r.next = 0x40C8000A;
    r.flags = 1;
    f.record = r;
    f.glueArena = f.arena;
    f.glueOwner = f.object;
    f.glueFacet = f.facet;
    f.glueOk = true;
    f.arenaCanonical = f.arena;
    f.linkedOwner = f.arena;
    f.linkedCanonical = r.linked;
    f.linkedIdentityOk = true;
    f.lookupValue = 2;
    f.contextMode = 1;
    f.route = lab::Route::Deferred;
    f.dirtyWord = 0;
    f.dirtyBit = 2;
    s.facet.set(f);
    ChainResult c;
    c.head = 0x40C80009;
    c.from = "the facet of 51FB8003";
    c.nodes = {{0x40C80009, 0x40C8000A, true}, {0x40C8000A, 0x40C80012, true}, {0x40C80012, 0xFFFFFFFF, true}};
    s.chain.set(c);
}

void drawBlock(const char* name, const lab::BlockCheck& b, const char* tip) {
    const std::string text = b.ok() ? fmt("%s  %08X  (index %u, region %u, owner %u)", name, b.body, b.header.index, b.header.region, b.header.owner)
                                    : std::string(name) + ": " + (b.failures.empty() ? std::string("not read") : b.failures.front());
    labui::check(b.ok(), text, tip);
}

void drawManager(Ui& ui) {
    seedFacetsDemo();
    FacetState& s = fs();
    if (labui::jobButton(ui, kReadFacetManager, icon::Refresh,
                         "Validate the three facet globals against the registry and read the directory. Each global is checked the way the "
                         "Xbox validators did: block header (hash, region, category, owner), the registry back-reference and record, and "
                         "the allocation-context record that must contain it."))
        labui::post(ui, kReadFacetManager, runReadFacetManager);
    labui::readStamp(s.manager.at());
    labui::errorBox(s.manager.error());
    gap(sp::S);
    const auto v = s.manager.get();
    if (!v) return;
    drawBlock("Network Facet Manager", v->manager.check, "slot 0x1A20FC0, 0x8D40 bytes, hash 3A851ADD, owner 25");
    drawBlock("Components Lookup", v->lookup, "slot 0x1A21124, 0x4000 bytes, hash 263D40D1, region 1");
    drawBlock("Deferred Context", v->deferred.check, "slot 0x1A93690, 12 bytes, hash 521B83CD, category 3");
    if (v->deferred.check.ok()) {
        gap(sp::XS);
        caption(fmt("deferred context: mode %u (%s), context %08X, state %u", v->deferred.mode,
                    (v->deferred.mode & 7) ? "deferred notifications" : "immediate", v->deferred.context, v->deferred.state)
                    .c_str(),
                pal::Muted);
    }
    const lab::Directory& d = v->manager.directory;
    gap(sp::M);
    fieldLabel(fmt("DIRECTORY  (%u REGIONS, %u LIST HEADS)", d.regions, d.total).c_str());
    gap(sp::XS);
    if (!d.why.empty()) labui::warnLine(d.why);
    else if (d.rows.empty()) caption("No regions registered (load into an activity).", pal::Faint);
    else if (beginTable("##dir", 4, std::min(240.0f, 34.0f + 30.0f * float(d.rows.size())), ImGuiTableFlags_BordersOuter)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Region", ImGuiTableColumnFlags_WidthFixed, px(60));
        ImGui::TableSetupColumn("Bundle", ImGuiTableColumnFlags_WidthFixed, px(92));
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, px(92));
        ImGui::TableSetupColumn("Heads", ImGuiTableColumnFlags_WidthStretch);
        tableHeaders();
        for (const lab::DirectoryRegion& r : d.rows) {
            ImGui::TableNextRow(0, px(30));
            ImGui::TableSetColumnIndex(0);
            cellText(std::to_string(r.index).c_str(), pal::Muted);
            ImGui::TableSetColumnIndex(1);
            cellText(hx(r.handle).c_str(), pal::Text, FontKind::Mono, type::MonoSmall);
            ImGui::TableSetColumnIndex(2);
            std::string key = "-";
            for (const lab::ActivityBundle& b : v->bundles)
                if (b.index == (r.handle & 0x1FFF) && b.used()) key = hx(b.key);
            cellText(key.c_str(), pal::Muted, FontKind::Mono, type::MonoSmall);
            ImGui::TableSetColumnIndex(3);
            std::string heads;
            for (uint32_t h : r.heads) heads += (heads.empty() ? "" : " ") + hx(h);
            cellText(heads.c_str(), pal::Muted, FontKind::Mono, type::MonoSmall);
        }
        endTable();
    }
    size_t used = 0;
    for (const auto& b : v->bundles) used += b.used();
    gap(sp::XS);
    caption(v->bundlesWhy.empty() ? fmt("activity bundles: %zu of %zu in use (pool 0x1A21050, stride 0xC)", used, v->bundles.size()).c_str()
                                  : v->bundlesWhy.c_str(),
            v->bundlesWhy.empty() ? pal::Faint : pal::Warning);
}

void drawFacet(Ui& ui) {
    seedFacetsDemo();
    FacetState& s = fs();
    {
        DisabledScope none(labui::pickedHandle() == 0, "Pick an object first");
        if (labui::jobButton(ui, kReadFacet, icon::Search,
                             "Map the object's arena to its facet and decode the record (read only). Map index = (accessor pool +0x28 << 13) "
                             "| (+0x3C & 0x1FFF) into the manager's first 0x8000 bytes (helper 0x1E4B14). The record checks are how the "
                             "likely offsets get confirmed.",
                             Tone::Primary, "Read Facet"))
            labui::post(ui, kReadFacet, runReadFacet);
    }
    labui::readStamp(s.facet.at());
    labui::errorBox(s.facet.error());
    gap(sp::S);
    const auto f = s.facet.get();
    if (!f) return;
    if (labui::kvBegin("##facet", 130)) {
        labui::kvHex("Object", f->object, "handle", pal::Accent);
        labui::kv("Arena / Domain", hx(f->arena) + "  /  " + hx(f->domain));
        labui::kv("Map Index", fmt("0x%X", f->mapIndex));
        labui::kv("Facet", f->facet == 0xFFFFFFFFu ? "none" : hx(f->facet), f->facet == 0xFFFFFFFFu ? pal::Faint : pal::Accent);
        labui::kvEnd();
    }
    if (!f->ok()) {
        gap(sp::XS);
        labui::warnLine(f->why);
    }
    if (!f->record) return;
    const lab::FacetRecord& r = *f->record;
    gap(sp::M);
    const bool split = availWidth() > px(760);
    const float half = split ? std::floor((availWidth() - px(28)) * 0.5f) : availWidth();
    ImGui::BeginGroup();
    fieldLabel("RECORD  (PS3 STRIDE 0x120)");
    gap(sp::XS);
    if (labui::kvBegin("##rec", 120, half)) {
        labui::kvHex("Address", r.addr, "record");
        labui::kv("Identity +0x110", hx(r.identity) + (r.identityOk ? "" : "  stale"), r.identityOk ? pal::Text : pal::Warning);
        labui::kv("Next +0x104", r.next == 0xFFFFFFFFu ? "end" : hx(r.next), pal::Text, true, "verified");
        labui::kv("Network Object +F8", hx(r.networkObject));
        labui::kv("Key / Marker", hx(r.key) + " / " + hx(r.marker));
        labui::kv("Flags +0x10C", fmt("%04X", r.flags));
        labui::kv("Dirty Bits", fmt("%08X %08X %08X %08X", r.dirty[0], r.dirty[1], r.dirty[2], r.dirty[3]), pal::Muted);
        labui::kvEnd();
    }
    ImGui::EndGroup();
    if (split) ImGui::SameLine(0, px(28));
    else gap(sp::M);
    ImGui::BeginGroup();
    fieldLabel("LIKELY OFFSETS, CHECKED");
    gap(sp::XS);
    labui::check(r.selfOk(), fmt("+0xA8 self == facet handle (%08X)", r.self));
    labui::check(r.arena == f->arena, fmt("+0xB0 canonical arena == object +0x3C (%08X)", r.arena));
    labui::check(r.dirtyPointerOk(), fmt("+0x90 dirty pointer == record + 0x98 (%08X)", r.dirtyPointer));
    labui::check(r.descriptorOk(), fmt("+0x94 descriptor == FFF8080F (%08X)", r.dirtyDescriptor));
    labui::check(f->glueOk, fmt("+0xAC linked accessor %08X: owner, +0x14 arena, +0x1C object, +0x20 facet agree", r.linked),
                 f->glueOk ? "Xbox FacetGluePass: allocated, pool identity, entry +0xC and body +0x14 = arena, +0x1C = object, +0x20 = facet"
                           : f->glueWhy.c_str());
    gap(sp::M);
    fieldLabel("NOTIFICATION ROUTE");
    gap(sp::XS);
    const ImVec4 rc = f->route == lab::Route::Deferred || f->route == lab::Route::Immediate ? pal::Info
                      : f->route == lab::Route::Unknown                                    ? pal::Faint
                                                                                           : pal::Muted;
    pill(lab::routeName(f->route), rc);
    tooltip(lab::routeHelp(f->route));
    if (f->route == lab::Route::Deferred && f->lookupValue < 128)
        caption(fmt("components lookup %u -> dirty word %u bit %u at record +0x98", f->lookupValue, f->dirtyWord, f->dirtyBit).c_str(), pal::Muted);
    ImGui::EndGroup();
}

void drawFacetChain(Ui& ui) {
    seedFacetsDemo();
    FacetState& s = fs();
    const auto m = s.manager.get();
    int region = s.region.load();
    std::vector<std::string> sources = {"Object's Facet", "Directory Head"};
    int mode = region < 0 ? 0 : 1;
    if (pillTabs("##chainsrc", sources, &mode)) {
        region = mode == 0 ? -1 : 0;
        s.region = region;
    }
    if (mode == 1) {
        ImGui::SameLine(0, px(12));
        int r = std::max(region, 0);
        const int maxRegion = m ? std::max(0, int(m->manager.directory.rows.size()) - 1) : 31;
        if (inputInt("##region", &r, 0, maxRegion, 120)) s.region = r;
        tooltip("Directory region");
        ImGui::SameLine(0, px(8));
        int slot = s.slot.load();
        const int maxSlot = m && size_t(r) < m->manager.directory.rows.size() ? std::max(0, int(m->manager.directory.rows[size_t(r)].count) - 1) : 749;
        if (inputInt("##slot", &slot, 0, maxSlot, 120)) s.slot = slot;
        tooltip("List head within the region");
    }
    ImGui::SameLine(0, px(12));
    if (labui::jobButton(ui, kWalkChain, icon::Follow,
                         "Follow facet +0x104 from the head (one read of the facets pool): the same links the unlink native 0x1E4080 "
                         "follows, with a loop check and the identity of every node."))
        labui::post(ui, kWalkChain, runWalkChain);
    labui::readStamp(s.chain.at());
    labui::errorBox(s.chain.error());
    gap(sp::S);
    const auto c = s.chain.get();
    if (!c) return;
    caption(fmt("from %s: %zu nodes", c->from.c_str(), c->nodes.size()).c_str(), pal::Muted);
    if (!c->why.empty()) labui::warnLine(c->why);
    gap(sp::XS);
     
    const float lineW = availWidth();
    const float chipW = textSize(FontKind::Semibold, type::Small - 0.5f, "00000000").x + px(20);
    const ImVec2 is = textSize(FontKind::Icons, 11, icon::ChevronRight);
    const float step = chipW + is.x + px(12);
    float x = 0;
    for (size_t i = 0; i < c->nodes.size(); ++i) {
        const lab::ChainNode& n = c->nodes[i];
        if (i) {
            if (x + step > lineW) {
                x = 0;
            } else {
                ImGui::SameLine(0, px(6));
                const ImVec2 p = ImGui::GetCursorScreenPos();
                drawText(ImGui::GetWindowDrawList(), FontKind::Icons, 11, ImVec2(p.x, p.y + (px(24) - is.y) * 0.5f), u32(pal::Decor), icon::ChevronRight);
                ImGui::Dummy(ImVec2(is.x, px(24)));
                ImGui::SameLine(0, px(6));
            }
        }
        pill(hx(n.handle).c_str(), n.identityOk ? (n.handle == c->head ? pal::Accent : pal::Info) : pal::Danger);
        x += step;
    }
}

void resetObjects() {
    labui::resetPicker();
    cs().keys.clear();
    cs().chain.clear();
    cs().lookup.clear();
    es().type.clear();
    es().effect.clear();
    es().cross.clear();
    es().params.clear();
    fs().manager.clear();
    fs().facet.clear();
    fs().chain.clear();
}

 
 
 

constexpr const char* kProbeSection = "Lab (development)";

void probeEffects(Ctx& c) {
    requireEffects(c);
    world::RuntimeReader mem(c.rt());
    const world::LocalPlayer p = world::requireLocalPlayer(mem);
    const world::TagTable t = tagTable(mem);
    const lab::EffectType e = lab::readEffectType(mem, t);
    c.log(fmt("effect anchor %08X -> %08X (expected %08X), type handle %08X, key %08X", lab::kTypeAnchorEffect, e.type.descriptor, lab::kEffectDescriptor,
              e.type.handle, e.type.key));
    if (!e.type.ok()) {
        c.log("effect type: " + e.type.why);
        c.log("verdict: effect type not readable (load into an activity and run again)");
        return;
    }
    const lab::OwnerLookup o = lab::findOwnerComponent(mem, t, p.handle, e.type.key, false, false, lab::OwnerMode::Effect);
    c.log(fmt("PC owner lookup on the player %08X: %s, %zu arenas walked, record %08X, own accessor %08X (%s)", p.handle, lab::lookupName(o.result),
              o.arenas.size(), o.component, o.child, o.childOk ? "resolves back" : "does not resolve back"));
    for (const lab::ArenaStep& a : o.arenas)
        c.log(fmt("  arena %08X: identity word %08X -> %08X (%s), body %08X, flags %X, definition %08X +%X: %s", a.handle, a.identity, a.canonical,
                  a.identityOk ? "ok" : "DIFFERS", a.body, a.flags, a.definition, a.offset, a.skipped ? "skipped" : lab::lookupName(a.result)));
    if (o.found >= 0)
        c.log(fmt("  own accessor %08X: owner (+0xC) %08X, target (+0x1C) %08X, pool identity -> %08X%s", o.child, o.childOwner, o.childTarget,
                  o.childCanonical, o.childWhy.empty() ? "" : (" - " + o.childWhy).c_str()));
    if (!o.why.empty()) c.log("  " + o.why);
    const lab::NativeEffect n = lab::nativeEffectOf(c.rt(), p.handle, e.type.handle);
    c.log(fmt("game getter 0x1EE060(%08X): %s, handle %08X", p.handle, n.called ? "called" : ("refused: " + n.refused).c_str(), n.handle));
    const uint32_t ours = o.result == lab::Lookup::Found ? o.child : 0xFFFFFFFFu;
    c.log(fmt("expected: the game's handle == the PC lookup's own-accessor handle (%08X == %08X), or both -1 when the player has no effect", n.handle, ours));
    if (!n.called) c.log("verdict: the guard refused the call (see above); run again standing still");
    else if (n.handle == ours) c.log("verdict: effect owner decoding matches the game");
    else c.log("verdict: MISMATCH between the PC lookup and the game (send this report)");
}

void probeComponents(Ctx& c) {
    requireComponents(c);
    world::RuntimeReader mem(c.rt());
    const world::LocalPlayer p = world::requireLocalPlayer(mem);
    const world::TagTable t = tagTable(mem);
    bool ok = true;
    for (uint32_t a : {lab::kTypeAnchorPrimary, lab::kTypeAnchorSecondary}) {
        const lab::TypeKey k = lab::readTypeKey(mem, t, a);
        c.log(fmt("%s anchor %08X: descriptor %08X, handle %08X, key %08X %s", lab::anchorName(a), a, k.descriptor, k.handle, k.key,
                  k.ok() ? "" : ("(" + k.why + ")").c_str()));
        if (!k.ok()) {
            ok = false;
            continue;
        }
        const lab::OwnerLookup o = lab::findOwnerComponent(mem, t, p.handle, k.key, a == lab::kTypeAnchorSecondary, false);
        c.log(fmt("  player %08X: %s%s", p.handle, lab::lookupName(o.result), o.why.empty() ? "" : (" - " + o.why).c_str()));
        for (const lab::ArenaStep& s : o.arenas)
            c.log(fmt("    arena %08X: identity word %08X -> %08X (%s), flags %X, definition %08X +%X: %s", s.handle, s.identity, s.canonical,
                      s.identityOk ? "ok" : "DIFFERS", s.flags, s.definition, s.offset, s.skipped ? "skipped" : lab::lookupName(s.result)));
        if (o.result == lab::Lookup::Malformed) ok = false;
        if (o.found >= 0) {
            const lab::Dispatch d = lab::readDispatch(mem, t, o.arenas[size_t(o.found)].descriptor.words[6], a == lab::kTypeAnchorSecondary ? 0x58 : 0x38);
            c.log(fmt("  dispatch slot +0x%X: fn %08X adjust %08X -> %s", d.slot, d.fn, d.adjust,
                      d.isVirtual() ? "virtual" : d.ok() ? fmt("code %08X", d.code).c_str() : d.why.c_str()));
            if (!d.ok()) ok = false;
        }
    }
    c.log("expected: both keys published; every arena's identity ok; lookups Found or Missing (a player has no spawner component), never "
          "Malformed; OPDs valid");
    c.log(ok ? "verdict: component type keys and lookups behave as decoded" : "verdict: something above did not decode (send this report)");
}

void probeFacets(Ctx& c) {
    requireFacets(c);
    world::RuntimeReader mem(c.rt());
    const world::LocalPlayer p = world::requireLocalPlayer(mem);
    const lab::RegistrySnapshot reg = lab::readRegistry(mem);
    const lab::FacetManagerView m = lab::readFacetManager(mem, reg);
    c.log(fmt("facet manager *(01A20FC0) = %08X: %s", m.check.body, m.check.ok() ? "valid" : m.check.failures.front().c_str()));
    c.log(fmt("facets pool 01A20FD0: base %08X stride 0x%X capacity %u identity +0x%X (expected stride 0x120, identity +0x110)", m.facets.base,
              m.facets.stride, m.facets.capacity, m.facets.identityOffset));
    c.log(fmt("directory: %u regions, %u heads%s", m.directory.regions, m.directory.total, m.directory.why.empty() ? "" : (" - " + m.directory.why).c_str()));
    const lab::FacetOfObject f = lab::readFacetOfObject(mem, m, reg, p.handle);
    c.log(fmt("player %08X: arena %08X (pool identity -> %08X), domain %08X, map index 0x%X -> facet %08X%s", p.handle, f.arena, f.arenaCanonical,
              f.domain, f.mapIndex, f.facet, f.ok() ? "" : (" - " + f.why).c_str()));
    if (!f.record) {
        c.log("verdict: no facet record to check (in an activity? the player normally has one)");
        return;
    }
    const lab::FacetRecord& r = *f.record;
    c.log(fmt("+0xA8 self %08X (expected %08X) %s", r.self, r.handle, r.selfOk() ? "ok" : "DIFFERS"));
    c.log(fmt("+0xB0 canonical arena %08X (expected %08X) %s", r.arena, f.arena, r.arena == f.arena ? "ok" : "DIFFERS"));
    c.log(fmt("+0x90 dirty pointer %08X (expected %08X) %s", r.dirtyPointer, r.addr + 0x98, r.dirtyPointerOk() ? "ok" : "DIFFERS"));
    c.log(fmt("+0x94 descriptor %08X (expected FFF8080F) %s", r.dirtyDescriptor, r.descriptorOk() ? "ok" : "DIFFERS"));
    c.log(fmt("+0xAC linked accessor %08X (pool identity -> %08X, owner +0xC %08X): glue +0x14 %08X, +0x1C %08X, +0x20 %08X %s", r.linked,
              f.linkedCanonical, f.linkedOwner, f.glueArena, f.glueOwner, f.glueFacet, f.glueOk ? "ok" : ("DIFFERS: " + f.glueWhy).c_str()));
    c.log(fmt("+0xF8 network object %08X, +0x10C flags %04X, route %s", r.networkObject, r.flags, lab::routeName(f.route)));
    const bool ok = r.selfOk() && r.arena == f.arena && r.dirtyPointerOk() && r.descriptorOk() && f.glueOk;
    c.log(ok ? "verdict: the likely facet record offsets are confirmed" : "verdict: facet record offsets differ (send this report)");
}

}   

BR_FEATURES(lab_objects) {
    const Page T = Page::Testing;
     
    r.section({.id = "lab.components.object", .page = T, .tab = "Components", .section = kSecObject, .order = 10, .draw = drawObject,
               .onDisconnect = resetObjects});
    r.section({.id = "lab.effects.object", .page = T, .tab = "Effects", .section = kSecObject, .order = 10, .draw = drawObject});
    r.section({.id = "lab.facets.object", .page = T, .tab = "Facets", .section = kSecObject, .order = 10, .draw = drawObject});
    r.action({.id = "lab.object.player", .page = T, .tab = "Components", .section = kSecObject, .label = labui::kFindPlayerLabel,
              .help = "Make your own player the inspected object on the Components, Effects and Facets tabs.", .run = labui::runFindPlayer});
    r.action({.id = "lab.object.list", .page = T, .tab = "Components", .section = kSecObject, .label = labui::kListObjectsLabel,
              .help = "Read every live sObject (one read of the pool) for the object picker.", .run = labui::runListObjects});
    ui::styleSection(kSecObject, {.fullWidth = true,
                                  .icon = ui::icon::View,
                                  .ownsActions = true});

     
    r.section({.id = "lab.components.keys", .page = T, .tab = "Components", .section = kSecKeys, .order = 20, .draw = drawKeys, .badge = kBadgeDev});
    r.section({.id = "lab.components.chain", .page = T, .tab = "Components", .section = kSecChain, .order = 30, .draw = drawChain, .badge = kBadgeDev});
    r.section({.id = "lab.components.lookup_card", .page = T, .tab = "Components", .section = kSecLookup, .order = 40, .draw = drawLookup, .badge = kBadgeDev});
    r.action({.id = "lab.components.read_keys", .page = T, .tab = "Components", .section = kSecKeys, .label = kReadKeys,
              .help = "Read the primary, secondary and effect type-key anchors (anchor -> descriptor -> handle -> key).", .run = runReadKeys,
              .badge = kBadgeDev});
    r.action({.id = "lab.components.read_chain", .page = T, .tab = "Components", .section = kSecChain, .label = kReadChain,
              .help = "List the inspected object's component arenas and the components inside them.", .run = runReadChain, .badge = kBadgeDev});
    r.action({.id = "lab.components.lookup", .page = T, .tab = "Components", .section = kSecLookup, .label = kLookup,
              .help = "Find the inspected object's component for the chosen type key and read its dispatch member pointer.", .run = runLookup,
              .badge = kBadgeDev});
    ui::styleSection(kSecKeys, {.icon = ui::icon::Tag, .ownsActions = true});
    ui::styleSection(kSecChain, {.icon = ui::icon::List, .ownsActions = true});
    ui::styleSection(kSecLookup, {.fullWidth = true,
                                  .icon = ui::icon::Search,
                                  .ownsActions = true});

     
    r.section({.id = "lab.effects.type", .page = T, .tab = "Effects", .section = kSecEffectType, .order = 20, .draw = drawEffectType, .badge = kBadgeNeedsCheck});
    r.section({.id = "lab.effects.effect", .page = T, .tab = "Effects", .section = kSecEffect, .order = 30, .draw = drawEffect, .badge = kBadgeNeedsCheck});
    r.section({.id = "lab.effects.params", .page = T, .tab = "Effects", .section = kSecParams, .order = 40, .draw = drawParams, .badge = kBadgeNeedsCheck});
    r.action({.id = "lab.effects.read_type", .page = T, .tab = "Effects", .section = kSecEffectType, .label = kReadEffectType,
              .help = "Read the effect type anchor 0x18DF174 -> 0x1AAF520 -> handle -> key.", .run = runReadEffectType, .badge = kBadgeNeedsCheck});
    r.action({.id = "lab.effects.find", .page = T, .tab = "Effects", .section = kSecEffect, .label = kFindEffect,
              .help = "Owner lookup with the effect key on the inspected object (read only).", .run = runFindEffect, .badge = kBadgeNeedsCheck});
    r.action({.id = "lab.effects.cross_check", .page = T, .tab = "Effects", .section = kSecEffect, .label = kCrossCheck,
              .help = "Call the game's effect getter 0x1EE060 once through the guard stub and compare it with the PC lookup.", .run = runCrossCheck,
              .badge = kBadgeNeedsCheck});
    r.action({.id = "lab.effects.read_params", .page = T, .tab = "Effects", .section = kSecParams, .label = kReadParams,
              .help = "Read the authored parameter keys and live values of the inspected object's effect.", .run = runReadParams,
              .badge = kBadgeNeedsCheck});
    r.toggle({.id = kWatchParamsId, .page = T, .tab = "Effects", .section = kSecParams, .label = "Watch Parameters Live",
              .help = "Re-read the effect parameters twice a second; values that changed since the previous read are highlighted.", .set = watchParams,
              .badge = kBadgeNeedsCheck});
    ui::styleSection(kSecEffectType, {.icon = ui::icon::Lightbulb, .ownsActions = true});
    ui::styleSection(kSecEffect, {.icon = ui::icon::Search,
                                  .ownsActions = true});
    ui::styleSection(kSecParams, {.fullWidth = true,
                                  .icon = ui::icon::Sliders,
                                  .ownsActions = true});

     
    r.section({.id = "lab.facets.manager", .page = T, .tab = "Facets", .section = kSecManager, .order = 20, .draw = drawManager, .badge = kBadgeNeedsCheck});
    r.section({.id = "lab.facets.facet", .page = T, .tab = "Facets", .section = kSecFacet, .order = 30, .draw = drawFacet, .badge = kBadgeNeedsCheck});
    r.section({.id = "lab.facets.chain", .page = T, .tab = "Facets", .section = kSecFacetChain, .order = 25, .draw = drawFacetChain, .badge = kBadgeNeedsCheck});
    r.action({.id = "lab.facets.read_manager", .page = T, .tab = "Facets", .section = kSecManager, .label = kReadFacetManager,
              .help = "Validate the facet manager, components lookup and deferred context globals; read the directory and activity bundles.",
              .run = runReadFacetManager, .badge = kBadgeNeedsCheck});
    r.action({.id = "lab.facets.read_facet", .page = T, .tab = "Facets", .section = kSecFacet, .label = kReadFacet,
              .help = "Map the inspected object to its network facet, decode the record and predict the notification route.", .run = runReadFacet,
              .badge = kBadgeNeedsCheck});
    r.action({.id = "lab.facets.walk", .page = T, .tab = "Facets", .section = kSecFacetChain, .label = kWalkChain,
              .help = "Follow facet +0x104 from the object's facet or a directory head.", .run = runWalkChain, .badge = kBadgeNeedsCheck});
    ui::styleSection(kSecManager, {.icon = ui::icon::Memory, .ownsActions = true});
    ui::styleSection(kSecFacet, {.fullWidth = true,
                                 .icon = ui::icon::Link,
                                 .ownsActions = true});
    ui::styleSection(kSecFacetChain, {.icon = ui::icon::Follow, .ownsActions = true});

     
    auto probe = [&](const char* id, const char* label, const char* help, void (*run)(Ctx&), const char* badge) {
        r.action({.id = id, .page = T, .tab = kPortChecksTab, .section = kProbeSection, .label = label, .help = help, .run = run, .badge = badge});
    };
    probe("lab.check.effects", "Effect Type and Owner Lookup vs the Game",
          "In an activity: reads the effect anchor chain, runs the PC owner lookup on your player (every arena's pool identity and the "
          "effect's own accessor are logged) and calls the effect getter 0x1EE060 once through the guard stub. Expected: the game's handle "
          "equals the PC lookup's own-accessor handle (or both -1), every identity ok.",
          probeEffects, kBadgeNeedsCheck);
    probe("lab.check.facets", "Facet Record Layout (Likely Offsets)",
          "In an activity: maps your player to its network facet (the arena's pool identity first) and checks +0xA8 self, +0xB0 arena, +0x90 "
          "dirty pointer, +0x94 FFF8080F and the +0xAC glue accessor (owner +0xC, +0x14 / +0x1C / +0x20). Expected: every line ok.",
          probeFacets, kBadgeNeedsCheck);
    probe("lab.check.components", "Component Type Keys, Lookups and Dispatch",
          "In an activity: reads the primary and secondary type keys, runs both lookups on your player (with every arena's pool identity) "
          "and decodes any dispatch member pointer. Expected: keys published, identities ok, no Malformed lookup, valid OPDs.",
          probeComponents, "");
}

}   
