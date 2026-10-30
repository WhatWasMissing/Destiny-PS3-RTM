 
 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_set>

#include "ui/shell_internal.h"

namespace brps3::ui {
namespace {

std::vector<Item> g_items;
std::unordered_map<std::string, size_t> g_byId;
std::unordered_map<std::string, std::vector<const Item*>> g_children;    
std::unordered_map<std::string, ItemState> g_states;
std::unordered_map<std::string, ImGuiKeyChord> g_hotkeys;
std::unordered_set<std::string> g_pins, g_sticky;

std::string g_menuId;
bool g_menuRequest = false;
std::string g_captureId;
bool g_captureRequest = false;
int g_captureFrame = -1;
std::string g_captureMessage;
std::string g_listenId;           
int g_listenFrame = -1;

ImGuiID menuPopupId() { return ImHashStr("##brps3-item-menu"); }
ImGuiID capturePopupId() { return ImHashStr("##brps3-hotkey"); }

std::string stickyKey(const std::string& id) { return std::string(key::StickyValue) + id; }
std::string hotkeyKey(const std::string& id) { return std::string(key::Hotkey) + id; }

void loadSet(const char* k, std::unordered_set<std::string>& out) {
    out.clear();
    for (auto& s : settings().list(k)) out.insert(s);
}

void saveSet(const char* k, const std::unordered_set<std::string>& in, const std::vector<std::string>& order) {
    std::vector<std::string> v;
    for (auto& s : order)
        if (in.count(s)) v.push_back(s);
    for (auto& s : in)
        if (std::find(v.begin(), v.end(), s) == v.end()) v.push_back(s);
    settings().setList(k, v);
}

bool highlighted(const Item& it) {
    return shell().highlightId == it.id && shell().now < shell().highlightUntil;
}

void maybeScrollTo(const Item& it) {
    if (shell().scrollToHighlight && shell().highlightId == it.id) {
         
        ImGui::SetScrollFromPosY(ImGui::GetItemRectMin().y - ImGui::GetWindowPos().y - px(16), 0.0f);
        shell().scrollToHighlight = false;
    }
}

void contextMenuFor(const Item& it) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_AllowWhenOverlappedByItem) &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        openItemMenu(it.id);
}

bool menuItem(const char* glyph, const char* text, const char* shortcutText = nullptr, bool checked = false, const ImVec4& color = pal::Text) {
    const float h = px(30);
    const float w = std::max(availWidth(), px(230));
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::Selectable((std::string("##") + text).c_str(), false, ImGuiSelectableFlags_None, ImVec2(w, h));
    ImDrawList* d = ImGui::GetWindowDrawList();
    const ImVec2 is = textSize(FontKind::Icons, 14, glyph);
    drawText(d, FontKind::Icons, 14, ImVec2(pos.x + px(6), pos.y + (h - is.y) * 0.5f), u32(checked ? pal::Text : pal::Muted), glyph);
    const ImVec2 ts = textSize(FontKind::Body, type::Label, text);
    drawText(d, FontKind::Body, type::Label, ImVec2(pos.x + px(32), pos.y + (h - ts.y) * 0.5f), u32(color), text);
    if (checked) {
        const ImVec2 cs = textSize(FontKind::Icons, 12, icon::Check);
        drawText(d, FontKind::Icons, 12, ImVec2(pos.x + w - cs.x - px(8), pos.y + (h - cs.y) * 0.5f), u32(pal::Accent), icon::Check);
    } else if (shortcutText && *shortcutText) {
         
        const float room = std::max(px(40), w - px(32) - ts.x - px(24));
        const std::string sc = ellipsize(shortcutText, room, FontKind::Body, type::Small, true);
        const ImVec2 ss = textSize(FontKind::Body, type::Small, sc.c_str());
        drawText(d, FontKind::Body, type::Small, ImVec2(pos.x + w - ss.x - px(8), pos.y + (h - ss.y) * 0.5f), u32(pal::Faint), sc.c_str());
    }
    return clicked;
}

void popupStyleBegin() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(6), px(6)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(px(4), px(2)));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, px(10));
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableRounding, px(6));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, pal::RaisedFill);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 0.08f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, pal::Fill);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, pal::FillHover);
}

void popupStyleEnd() {
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(4);
}

bool reservedChord(ImGuiKeyChord c) {
    const ImGuiKey k = ImGuiKey(c & ~ImGuiMod_Mask_);
    const ImGuiKeyChord mods = c & ImGuiMod_Mask_;
    if (k == ImGuiKey_Escape || k == ImGuiKey_Tab || k == ImGuiKey_Enter || k == ImGuiKey_KeypadEnter || k == ImGuiKey_Space) return mods == 0 || k == ImGuiKey_Tab;
    if (k == ImGuiKey_GraveAccent && mods == 0) return true;
    if (mods == ImGuiMod_Ctrl && (k == ImGuiKey_K || k == ImGuiKey_L || k == ImGuiKey_C || k == ImGuiKey_V || k == ImGuiKey_X || k == ImGuiKey_A ||
                                  k == ImGuiKey_Z || k == ImGuiKey_Comma || (k >= ImGuiKey_1 && k <= ImGuiKey_9)))
        return true;    
    if (mods == 0 && k == ImGuiKey_Slash) return true;    
    if (mods == 0 && ((k >= ImGuiKey_A && k <= ImGuiKey_Z) || (k >= ImGuiKey_0 && k <= ImGuiKey_9))) return true;    
    return false;
}

}   

 
 
 

const char* kindIcon(const Item& it) {
    switch (it.kind) {
    case ItemKind::Toggle: return icon::Switch;
    case ItemKind::Value: return icon::Sliders;
    case ItemKind::Action:
        if (it.action() && it.action()->icon) return it.action()->icon;
        if (it.action() && it.action()->immediate) return icon::Panic;
        return it.action() && it.action()->dangerous ? icon::Warning : icon::Play;
    default: return pageIcon(int(it.page));
    }
}

const ToggleDef* Item::toggle() const { return kind == ItemKind::Toggle ? &Registry::get().toggles()[size_t(index)] : nullptr; }
const ValueDef* Item::value() const { return kind == ItemKind::Value ? &Registry::get().values()[size_t(index)] : nullptr; }
const ActionDef* Item::action() const { return kind == ItemKind::Action ? &Registry::get().actions()[size_t(index)] : nullptr; }
const SectionDef* Item::sectionDef() const { return kind == ItemKind::Section ? &Registry::get().sections()[size_t(index)] : nullptr; }

void buildItems() {
    g_items.clear();
    g_byId.clear();
    g_children.clear();
    const Registry& r = Registry::get();
    using K = Registry::Kind;
     
    std::unordered_map<uint64_t, size_t> seqOf;
    auto refKey = [](K k, size_t i) { return (uint64_t(k) << 32) | uint64_t(i); };
    for (size_t s = 0; s < r.order().size(); ++s) seqOf[refKey(r.order()[s].kind, r.order()[s].index)] = s;
     
    auto add = [&](ItemKind kind, K k, size_t i, const std::string& id, const std::string& label, const std::string& help,
                   const std::string& badge) {
        const Registry::Ref ref{k, i};
        const Registry::Place pl = r.placeOf(ref);
        const ToggleDef* parent = k == K::Section ? nullptr : r.parentOf(ref);
        Item it{kind, int(i), id, label, help, pl.tab, pl.section, pl.page, badge};
        if (parent) {
            it.parent = parent->id;
            it.depth = r.depth(ref);
        }
        auto s = seqOf.find(refKey(k, i));
        it.seq = s == seqOf.end() ? 0 : s->second;
        g_items.push_back(std::move(it));
    };
    for (size_t i = 0; i < r.toggles().size(); ++i) {
        const auto& t = r.toggles()[i];
        add(ItemKind::Toggle, K::Toggle, i, t.id, t.label, t.help, t.badge);
    }
    for (size_t i = 0; i < r.values().size(); ++i) {
        const auto& v = r.values()[i];
        add(ItemKind::Value, K::Value, i, v.id, v.label, v.help, v.badge);
    }
    for (size_t i = 0; i < r.actions().size(); ++i) {
        const auto& a = r.actions()[i];
        add(ItemKind::Action, K::Action, i, a.id, a.label, a.help, a.badge);
    }
    for (size_t i = 0; i < r.sections().size(); ++i) {
        const auto& s = r.sections()[i];
        add(ItemKind::Section, K::Section, i, s.id, s.section, std::string(), s.badge);
    }
    for (size_t i = 0; i < g_items.size(); ++i) g_byId.emplace(g_items[i].id, i);
    for (const Item& it : g_items)    
        if (!it.parent.empty()) g_children[it.parent].push_back(&it);
    for (auto& [id, kids] : g_children)
        std::stable_sort(kids.begin(), kids.end(), [](const Item* a, const Item* b) { return a->seq < b->seq; });
     
     
    std::vector<const Item*> unbound;
    for (auto& it : g_items) {
        ItemState& st = g_states[it.id];
        if (const ValueDef* v = it.value()) {
            st.value = st.applied = v->def;
            st.valueKnown = false;
        }
        const int chord = settings().integer(hotkeyKey(it.id), -1);
        if (chord > 0) g_hotkeys[it.id] = ImGuiKeyChord(chord);
        else if (chord < 0) unbound.push_back(&it);
    }
    for (const Item* it : unbound) {
        const std::string text = it->toggle() ? it->toggle()->hotkey : it->action() ? it->action()->hotkey : std::string();
        const ImGuiKeyChord chord = text.empty() ? 0 : parseChord(text);
        if (!chord) continue;
        const bool taken = std::any_of(g_hotkeys.begin(), g_hotkeys.end(), [&](const auto& kv) { return kv.second == chord && kv.first != it->id; });
        if (!taken) g_hotkeys[it->id] = chord;
    }
    loadSet(key::Pins, g_pins);
    loadSet(key::Sticky, g_sticky);
}

ImGuiKeyChord parseChord(const std::string& text) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= text.size()) {
        size_t plus = text.find('+', start);
        if (plus == start && plus + 1 == text.size()) {    
            parts.push_back("+");
            break;
        }
        if (plus == std::string::npos) plus = text.size();
        parts.push_back(text.substr(start, plus - start));
        start = plus + 1;
    }
    if (parts.empty() || parts.back().empty()) return 0;
    auto same = [](const std::string& a, const char* b) { return _stricmp(a.c_str(), b) == 0; };
    ImGuiKeyChord mods = 0;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        const std::string& m = parts[i];
        if (same(m, "Ctrl") || same(m, "Control")) mods |= ImGuiMod_Ctrl;
        else if (same(m, "Shift")) mods |= ImGuiMod_Shift;
        else if (same(m, "Alt")) mods |= ImGuiMod_Alt;
        else if (same(m, "Win") || same(m, "Super")) mods |= ImGuiMod_Super;
        else return 0;
    }
    std::string name = parts.back();
    if (name.size() > 3 && _strnicmp(name.c_str(), "Num", 3) == 0 && _strnicmp(name.c_str(), "NumLock", 7) != 0) name = "Keypad" + name.substr(3);
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
        const ImGuiKey key = ImGuiKey(k);
        if (ImGui::IsLRModKey(key) || (key >= ImGuiKey_ReservedForModCtrl && key <= ImGuiKey_ReservedForModSuper)) continue;
        if (same(name, ImGui::GetKeyName(key))) return mods | key;
    }
    return 0;
}

void reloadUiLists() {
    loadSet(key::Pins, g_pins);
    loadSet(key::Sticky, g_sticky);
}

const std::vector<Item>& items() { return g_items; }

const Item* findItem(const std::string& id) {
    auto it = g_byId.find(id);
    return it == g_byId.end() ? nullptr : &g_items[it->second];
}

ItemState& state(const Item& it) { return g_states[it.id]; }

const std::vector<const Item*>& childrenOf(const std::string& parentId) {
    static const std::vector<const Item*> none;
    auto it = g_children.find(parentId);
    return it == g_children.end() ? none : it->second;
}

bool childVisible(const Item& it) {
    int guard = 0;
    for (const Item* p = it.parent.empty() ? nullptr : findItem(it.parent); p && guard <= kMaxChildDepth; ++guard) {
        if (!state(*p).on) return false;
        p = p->parent.empty() ? nullptr : findItem(p->parent);
    }
    return true;
}

const Item& revealTarget(const Item& it) {
     
    std::vector<const Item*> chain{&it};
    while (!chain.back()->parent.empty() && int(chain.size()) <= kMaxChildDepth) {
        const Item* p = findItem(chain.back()->parent);
        if (!p) break;
        chain.push_back(p);
    }
    const Item* target = chain.back();
    for (size_t k = chain.size() - 1; k > 0; --k) {
        if (!state(*chain[k]).on) break;
        target = chain[k - 1];
    }
    return *target;
}

std::string pathLabel(const Item& it, const char* separator) {
    std::string s = it.label;
    int guard = 0;
    for (const Item* p = it.parent.empty() ? nullptr : findItem(it.parent); p && guard <= kMaxChildDepth; ++guard) {
        s = p->label + separator + s;
        p = p->parent.empty() ? nullptr : findItem(p->parent);
    }
    return s;
}

std::string valueText(const Item& it, float v) {
    if (const ValueDef* d = it.value()) return d->text(v);
    char buf[48];
    snprintf(buf, sizeof(buf), "%.2f", double(v));
    return buf;
}

 
 
 

void setToggle(const Item& it, bool on, bool user) {
    const ToggleDef* t = it.toggle();
    if (!t) return;
    if (!connected()) {
        notify(std::string(kConnectFirst) + " to use " + it.label, Level::Warn);
        return;
    }
    ItemState& st = state(it);
    if (st.pending) return;
    const bool previous = st.on;
    st.on = on;
    st.pending = true;
    if (user) settings().pushRecent(key::Recent, it.id, 12);
    if (g_sticky.count(it.id)) settings().set(stickyKey(it.id), on);
    auto set = t->set;
    const std::string id = it.id;
    if (user) noteQueued(it.label + (on ? " on" : " off"));
    Engine::get().post(it.label + (on ? " on" : " off"),
                       [set, on](Ctx& c) {
                           if (set) set(c, on);
                       },
                       [id, previous, on](bool ok, const std::string&) {
                           if (const Item* item = findItem(id)) {
                               ItemState& s = state(*item);
                               s.pending = false;
                               s.on = ok ? on : previous;
                           }
                       });
}

void applyValue(const Item& it, float v, bool user) {
    const ValueDef* d = it.value();
    if (!d) return;
    if (!connected()) {
        notify(std::string(kConnectFirst) + " to change " + it.label, Level::Warn);
        return;
    }
    ItemState& st = state(it);
    const float previous = st.applied;
    st.value = v;
    st.pending = true;
    if (user) settings().pushRecent(key::Recent, it.id, 12);
    if (g_sticky.count(it.id)) settings().set(stickyKey(it.id), v);
    auto set = d->set;
    const std::string id = it.id;
    const float def = d->def;
    const std::string shown = d->text(v);
    if (user) noteQueued(it.label + " = " + shown);
    Engine::get().post(it.label + " = " + shown,
                       [set, v](Ctx& c) {
                           if (set) set(c, v);
                       },
                       [id, previous, v, def](bool ok, const std::string&) {
                           if (const Item* item = findItem(id)) {
                               ItemState& s = state(*item);
                               s.pending = false;
                               if (!ok && s.value == v) s.value = previous;
                               if (ok) {
                                   s.applied = v;
                                   s.valueKnown = true;
                                   s.changed = std::fabs(v - def) > 1e-6f;
                               }
                           }
                       });
}

static void resetValue(const Item& it) {
    const ValueDef* d = it.value();
    if (!d) return;
    if (!d->reset) {
        applyValue(it, d->def);
        return;
    }
    if (!connected()) {
        notify(std::string(kConnectFirst) + " to reset " + it.label, Level::Warn);
        return;
    }
    ItemState& st = state(it);
    st.value = d->def;
    st.pending = true;
    auto reset = d->reset;
    const std::string id = it.id;
    Engine::get().post(it.label + " reset", [reset](Ctx& c) { reset(c); },
                       [id](bool ok, const std::string&) {
                           if (const Item* item = findItem(id)) {
                               ItemState& s = state(*item);
                               s.pending = false;
                               if (ok) {
                                   s.applied = s.value;
                                   s.changed = false;
                               }
                           }
                       });
}

void runAction(const Item& it, bool user) {
    const ActionDef* a = it.action();
    if (!a) return;
    if (a->immediate) {    
        a->immediate();
        if (user) flashSuccess(it.id);
        return;
    }
    if (!connected()) {
        notify(std::string(kConnectFirst) + " to run " + it.label, Level::Warn);
        return;
    }
    if (it.page == Page::Testing && it.tab == kPortChecksTab) {    
        if (user) settings().pushRecent(key::Recent, it.id, 12);
        runPortCheck(it.id);
        return;
    }
    const std::string id = it.id;
    auto go = [id, user] {
        const Item* item = findItem(id);
        if (!item || !item->action()) return;
        if (user) settings().pushRecent(key::Recent, id, 12);
        auto run = item->action()->run;
        noteQueued(item->label);
        Engine::get().post(
            item->label,
            [run](Ctx& c) {
                if (run) run(c);
            },
            [id](bool ok, const std::string&) {
                if (ok) flashSuccess(id);    
            });
    };
    if (a->dangerous)
        confirm(it.label + "?", it.help.empty() ? std::string("This changes your game and can't be undone.") : it.help,
                it.label, go, true);
    else
        go();
}

void activate(const Item& it) {
    switch (it.kind) {
    case ItemKind::Toggle: setToggle(it, !state(it).on); break;
    case ItemKind::Action: runAction(it); break;
    default: reveal(it); break;
    }
}

void openPage(Page page) { navigate(int(page)); }
void openPage(Page page, const std::string& tab) { navigate(int(page), tab); }

 

namespace {
RegistryItem publicItem(const Item& it) {
    RegistryItem r;
    switch (it.kind) {
    case ItemKind::Toggle: r.kind = RegistryItem::Kind::Switch; break;
    case ItemKind::Value: r.kind = RegistryItem::Kind::Value; break;
    case ItemKind::Action: r.kind = RegistryItem::Kind::Action; break;
    case ItemKind::Section: r.kind = RegistryItem::Kind::Section; break;
    }
    r.id = it.id;
    r.label = it.label;
    r.help = it.help;
    r.tab = it.tab;
    r.section = it.section;
    r.badge = it.badge;
    r.page = it.page;
    r.parent = it.parent;
    r.where = pageLabel(int(it.page));
    if (!it.tab.empty()) r.where += "  ·  " + it.tab;
    if (!it.parent.empty()) {    
        if (const Item* p = findItem(it.parent)) r.where += "  ·  " + pathLabel(*p, "  ·  ");
    } else if (!it.section.empty() && it.section != it.tab && it.kind != ItemKind::Section) {
        r.where += "  ·  " + it.section;
    }
    r.dangerous = it.action() && it.action()->dangerous;
    r.portCheck = it.kind == ItemKind::Action && it.page == Page::Testing && it.tab == kPortChecksTab;
    r.icon = kindIcon(it);
    return r;
}
}   

std::vector<RegistryItem> registryItems() {
    std::vector<RegistryItem> out;
    out.reserve(g_items.size());
    for (const Item& it : g_items) out.push_back(publicItem(it));
    return out;
}

bool findRegistryItem(const std::string& id, RegistryItem* out) {
    const Item* it = findItem(id);
    if (!it) return false;
    if (out) *out = publicItem(*it);
    return true;
}

bool revealItem(const std::string& id) {
    const Item* it = findItem(id);
    if (!it) return false;
    reveal(*it);
    return true;
}

bool activateItem(const std::string& id) {
    const Item* it = findItem(id);
    if (!it) return false;
    activate(*it);
    return true;
}

bool setItemSwitch(const std::string& id, bool on) {
    const Item* it = findItem(id);
    if (!it || it->kind != ItemKind::Toggle) return false;
    if (state(*it).on != on || state(*it).pending) setToggle(*it, on);
    return true;
}

bool applyItemValue(const std::string& id, float value) {
    const Item* it = findItem(id);
    if (!it || it->kind != ItemKind::Value) return false;
    applyValue(*it, it->value()->quantize(value));
    return true;
}

bool itemSwitchOn(const std::string& id) {
    const Item* it = findItem(id);
    return it && it->kind == ItemKind::Toggle && state(*it).on;
}

bool itemPending(const std::string& id) {
    const Item* it = findItem(id);
    return it && state(*it).pending;
}

float itemValue(const std::string& id, float fallback) {
    const Item* it = findItem(id);
    if (!it || it->kind != ItemKind::Value) return fallback;
    return state(*it).value;
}

void demoShowSwitch(const std::string& id, bool on) {
    if (!isDemo()) return;
    if (const Item* it = findItem(id); it && it->kind == ItemKind::Toggle) state(*it).on = on;
}

void navigate(int page, const std::string& tab) {
    if (page < 0 || page >= kPageCount) return;
    shell().page = page;
    if (!tab.empty() && page < int(Page::Count)) {
        const auto tabs = Registry::get().tabs(Page(page));
        for (size_t i = 0; i < tabs.size(); ++i)
            if (tabs[i] == tab) shell().tab[page] = int(i);
    }
    if (!shell().screenshot) setString(key::LastPage, pageLabel(page));
}

void reveal(const Item& it) {
     
    const Item& target = revealTarget(it);
    navigate(int(target.page), target.tab);
    shell().highlightId = target.id;
    shell().highlightUntil = shell().now + 0.3;    
    shell().scrollToHighlight = true;
    if (&target != &it) notify(it.label + " appears under " + target.label + " while it is on", Level::Info);
}

void onConnected() {
     
    struct Read {
        std::vector<std::pair<std::string, bool>> toggles;
        std::vector<std::pair<std::string, float>> values;
    };
    auto result = std::make_shared<Read>();
    struct Query {
        std::string id;
        std::function<bool(Ctx&)> toggle;
        std::function<float(Ctx&)> value;
    };
    std::vector<Query> queries;
    for (const Item& it : g_items) {
        if (const ToggleDef* t = it.toggle(); t && t->query) queries.push_back({it.id, t->query, {}});
        if (const ValueDef* v = it.value(); v && v->query) queries.push_back({it.id, {}, v->query});
    }
    auto reapply = [] {
        int n = 0;
        for (const std::string& id : std::vector<std::string>(g_sticky.begin(), g_sticky.end())) {
            const Item* it = findItem(id);
            if (!it) continue;
            if (it->kind == ItemKind::Toggle) {
                const bool want = settings().flag(stickyKey(id), false);
                if (want != state(*it).on) {
                    setToggle(*it, want, false);
                    ++n;
                }
            } else if (it->kind == ItemKind::Value) {
                const float want = settings().number(stickyKey(id), it->value()->def);
                if (std::fabs(want - state(*it).value) > 1e-6f || !state(*it).valueKnown) {
                    applyValue(*it, want, false);
                    ++n;
                }
            }
        }
        if (n) notify("Re-applied " + std::to_string(n) + (n == 1 ? " sticky item" : " sticky items"), Level::Good);
    };
    if (queries.empty()) {
        reapply();
        return;
    }
    Engine::get().post(
        "Reading feature state",
        [queries, result](Ctx& c) {
            for (const Query& q : queries) {
                try {
                    if (q.toggle) result->toggles.emplace_back(q.id, q.toggle(c));
                    if (q.value) result->values.emplace_back(q.id, q.value(c));
                } catch (const std::exception& e) {
                    c.log("state of " + q.id + ": " + e.what());
                }
            }
        },
        [result, reapply](bool, const std::string&) {
            for (auto& [id, on] : result->toggles)
                if (const Item* it = findItem(id)) state(*it).on = on;
            for (auto& [id, v] : result->values)
                if (const Item* it = findItem(id)) {
                    state(*it).value = state(*it).applied = v;
                    state(*it).valueKnown = true;
                }
            reapply();
        });
}

void onDisconnected() {
    for (const Item& it : g_items) {
        ItemState& st = state(it);
        st.on = false;
        st.pending = false;
        st.changed = false;
        if (const ValueDef* v = it.value()) {
            st.value = st.applied = v->def;
            st.valueKnown = false;
        }
    }
     
}

void onPanicked() {
     
     
    onDisconnected();
}

void panicNow() {
    if (!connected()) {
        notify("Not connected: nothing to turn off", Level::Info);
        return;
    }
    std::vector<std::string> toggles, values;
    std::vector<const Item*> on;
    for (const Item& it : g_items) {
        ItemState& st = state(it);
        if (it.kind == ItemKind::Toggle && (st.on || st.pending)) {
            on.push_back(&it);
            st.pending = true;           
        }
        if (it.kind == ItemKind::Value && st.changed) values.push_back(it.id);
    }
     
    std::stable_sort(on.begin(), on.end(), [](const Item* a, const Item* b) { return a->depth > b->depth; });
    for (const Item* it : on) toggles.push_back(it->id);
    if (!Engine::get().panic(toggles, values)) {
        notify("Not connected: nothing to turn off", Level::Info);
        for (const Item& it : g_items) state(it).pending = false;
    }
}

 
 
 

bool pinned(const std::string& id) { return g_pins.count(id) != 0; }
bool sticky(const std::string& id) { return g_sticky.count(id) != 0; }

static void setPinned(const std::string& id, bool on) {
    if (on) g_pins.insert(id);
    else g_pins.erase(id);
    saveSet(key::Pins, g_pins, settings().list(key::Pins));
}

void setSticky(const Item& it, bool on) {
    if (on) {
        g_sticky.insert(it.id);
        if (it.kind == ItemKind::Toggle) settings().set(stickyKey(it.id), state(it).on);
        if (it.kind == ItemKind::Value) settings().set(stickyKey(it.id), state(it).value);
    } else {
        g_sticky.erase(it.id);
    }
    saveSet(key::Sticky, g_sticky, settings().list(key::Sticky));
}

ImGuiKeyChord hotkey(const std::string& id) {
    auto it = g_hotkeys.find(id);
    return it == g_hotkeys.end() ? 0 : it->second;
}

std::string chordName(ImGuiKeyChord chord) {
    if (!chord) return {};
    std::string s;
    if (chord & ImGuiMod_Ctrl) s += "Ctrl+";
    if (chord & ImGuiMod_Shift) s += "Shift+";
    if (chord & ImGuiMod_Alt) s += "Alt+";
    if (chord & ImGuiMod_Super) s += "Win+";
    const ImGuiKey k = ImGuiKey(chord & ~ImGuiMod_Mask_);
    std::string name = ImGui::GetKeyName(k);
    if (name.rfind("Keypad", 0) == 0) name = "Num" + name.substr(6);
    s += name;
    return s;
}

void setHotkey(const std::string& id, ImGuiKeyChord chord) {
    if (chord) {
        for (auto it = g_hotkeys.begin(); it != g_hotkeys.end();) {
            if (it->second == chord && it->first != id) {
                if (const Item* other = findItem(it->first)) notify(chordName(chord) + " moved from " + other->label, Level::Info);
                settings().set(hotkeyKey(it->first), 0);
                it = g_hotkeys.erase(it);
            } else {
                ++it;
            }
        }
        g_hotkeys[id] = chord;
    } else {
        g_hotkeys.erase(id);
    }
    settings().set(hotkeyKey(id), int(chord));
}

void startListening(const std::string& id) {
    g_listenId = g_listenId == id ? std::string() : id;    
    g_listenFrame = ImGui::GetFrameCount();
}

bool listening(const std::string& id) { return !g_listenId.empty() && g_listenId == id; }

namespace {
 
 
void listenForKey() {
    if (g_listenId.empty() || ImGui::GetFrameCount() <= g_listenFrame) return;
    const std::string id = g_listenId;
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right) || !findItem(id)) {
        g_listenId.clear();
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        g_listenId.clear();
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false) || ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        setHotkey(id, 0);
        g_listenId.clear();
        return;
    }
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
        const ImGuiKey key = ImGuiKey(k);
        if ((key >= ImGuiKey_MouseLeft && key <= ImGuiKey_MouseWheelY) || ImGui::IsLRModKey(key) ||
            (key >= ImGuiKey_ReservedForModCtrl && key <= ImGuiKey_ReservedForModSuper) || (key >= ImGuiKey_Gamepad_BEGIN && key < ImGuiKey_Gamepad_END))
            continue;
        if (!ImGui::IsKeyPressed(key, false)) continue;
        ImGuiKeyChord chord = key;
        if (io.KeyCtrl) chord |= ImGuiMod_Ctrl;
        if (io.KeyShift) chord |= ImGuiMod_Shift;
        if (io.KeyAlt) chord |= ImGuiMod_Alt;
        if (reservedChord(chord)) {
            notify(chordName(chord) + " is reserved: add Ctrl, Alt or Shift, or use F1-F12", Level::Warn);
        } else {
            setHotkey(id, chord);
            if (const Item* it = findItem(id)) notify(chordName(chord) + " now runs " + it->label, Level::Good);
            g_listenId.clear();
        }
        break;
    }
}
}   

void handleHotkeys() {
    if (!g_listenId.empty()) {
        listenForKey();
        return;
    }
    if (ImGui::GetIO().WantTextInput || paletteOpen() || detail::modalOpen()) return;
    if (ImGui::IsPopupOpen(capturePopupId(), ImGuiPopupFlags_None)) return;
    for (auto& [id, chord] : g_hotkeys) {
        if (!ImGui::IsKeyChordPressed(chord)) continue;
        if (const Item* it = findItem(id)) activate(*it);
        break;
    }
}

void openItemMenu(const std::string& id) {
    g_menuId = id;
    g_menuRequest = true;
}

void openHotkeyCapture(const std::string& id) {
    g_captureId = id;
    g_captureRequest = true;
    g_captureMessage.clear();
}

void drawItemPopups() {
    if (g_menuRequest) {
        ImGui::OpenPopup(menuPopupId());
        g_menuRequest = false;
    }
    popupStyleBegin();
    if (ImGui::BeginPopupEx(menuPopupId(), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove)) {
        const Item* it = findItem(g_menuId);
        if (!it || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            ImGui::CloseCurrentPopup();
        } else {
            {
                const ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(px(230), px(34)));
                ImDrawList* d = ImGui::GetWindowDrawList();
                drawText(d, FontKind::Semibold, type::Label, p + ImVec2(px(8), px(2)), u32(pal::Text), titleCase(it->label).c_str());
                const std::string where = titleCase(std::string(pageLabel(int(it->page))) + (it->tab.empty() ? "" : "  /  " + it->tab) +
                                                    (it->section.empty() || it->kind == ItemKind::Section ? "" : "  /  " + it->section));
                drawText(d, FontKind::Body, type::Tiny, p + ImVec2(px(8), px(20)), u32(pal::Faint), where.c_str());
            }
            ImGui::Separator();
            const bool isPinned = pinned(it->id);
            if (menuItem(isPinned ? icon::Unpin : icon::Pin, isPinned ? "Unpin From Home" : "Pin to Home")) {
                setPinned(it->id, !isPinned);
                notify(isPinned ? "Unpinned " + it->label : "Pinned " + it->label + " to Home", Level::Good);
            }
            if (it->kind == ItemKind::Toggle || it->kind == ItemKind::Value) {
                const bool st = sticky(it->id);
                if (menuItem(icon::Repeat, "Re-Apply on Connect", nullptr, st)) {
                    setSticky(*it, !st);
                    notify(st ? it->label + " is no longer sticky" : it->label + " will be re-applied after connecting", Level::Good);
                }
            }
            const ImGuiKeyChord hk = hotkey(it->id);
            const std::string hkName = chordName(hk);
            if (menuItem(icon::Keyboard, hk ? "Change Hotkey..." : "Set Hotkey...", hkName.c_str())) openHotkeyCapture(it->id);
            if (hk && menuItem(icon::Remove, "Remove Hotkey")) setHotkey(it->id, 0);
            if (it->kind == ItemKind::Value && menuItem(icon::Undo, "Reset to Default", it->value()->text(it->value()->def).c_str())) resetValue(*it);
            ImGui::Separator();
            if (it->kind != ItemKind::Section && menuItem(icon::Search, "Show in Page")) reveal(*it);
            if (menuItem(icon::Copy, "Copy ID", it->id.c_str())) copyToClipboard(it->id, "id");
        }
        ImGui::EndPopup();
    }
    popupStyleEnd();

     
    if (g_captureRequest) {
        ImGui::OpenPopup(capturePopupId());
        g_captureRequest = false;
        g_captureFrame = ImGui::GetFrameCount();
    }
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(16), px(16)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(10));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, pal::RaisedFill);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 0.08f));
    if (ImGui::BeginPopupEx(capturePopupId(), ImGuiWindowFlags_Modal | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                                  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                                  ImGuiWindowFlags_AlwaysAutoResize)) {
        const Item* it = findItem(g_captureId);
        ImGui::Dummy(ImVec2(px(380), 0));
        heading("Set Hotkey");
        if (it) {
            FontScope f(FontKind::Body, type::Small);
            ImGui::PushStyleColor(ImGuiCol_Text, pal::Muted);
            ImGui::Text("For %s", titleCase(it->label).c_str());
            ImGui::PopStyleColor();
        }
        gap(sp::L);
         
        ImGuiIO& io = ImGui::GetIO();
        std::string held;
        if (io.KeyCtrl) held += "Ctrl+";
        if (io.KeyShift) held += "Shift+";
        if (io.KeyAlt) held += "Alt+";
        const ImVec2 boxPos = ImGui::GetCursorScreenPos();
        const ImVec2 boxSize(px(380), px(56));
        ImGui::Dummy(boxSize);
        ImDrawList* d = ImGui::GetWindowDrawList();
        d->AddRectFilled(boxPos, boxPos + boxSize, u32(pal::Fill), px(8));
        const float pulse = 0.5f + 0.5f * std::sin(float(ImGui::GetTime()) * 4.0f);
        d->AddRect(boxPos, boxPos + boxSize, u32(pal::Accent, 0.25f + 0.35f * pulse), px(8), 0, 1.0f);
        requestRedraw();
        const std::string shown = held.empty() ? (hotkey(g_captureId) ? chordName(hotkey(g_captureId)) : std::string("Press a Key Combination"))
                                               : held + "...";
        const ImVec2 ts = textSize(FontKind::Semibold, type::Label, shown.c_str());
        drawText(d, FontKind::Semibold, type::Label, boxPos + (boxSize - ts) * 0.5f, u32(held.empty() && !hotkey(g_captureId) ? pal::Muted : pal::Text), shown.c_str());
        gap(sp::S);
        if (!g_captureMessage.empty()) {
            FontScope f(FontKind::Body, type::Small);
            ImGui::PushStyleColor(ImGuiCol_Text, pal::Warning);
            ImGui::TextUnformatted(g_captureMessage.c_str());
            ImGui::PopStyleColor();
        } else {
            FontScope f(FontKind::Body, type::Small);
            ImGui::PushStyleColor(ImGuiCol_Text, pal::Faint);
            ImGui::TextUnformatted("Esc cancels  \xC2\xB7  Backspace removes the hotkey");
            ImGui::PopStyleColor();
        }
        bool close = false;
        if (ImGui::GetFrameCount() <= g_captureFrame) {
             
        } else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) close = true;
        else if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false) || ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            setHotkey(g_captureId, 0);
            close = true;
        } else {
            for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
                const ImGuiKey key = ImGuiKey(k);
                if ((key >= ImGuiKey_MouseLeft && key <= ImGuiKey_MouseWheelY) || ImGui::IsLRModKey(key) ||
                    (key >= ImGuiKey_ReservedForModCtrl && key <= ImGuiKey_ReservedForModSuper) ||
                    (key >= ImGuiKey_Gamepad_BEGIN && key < ImGuiKey_Gamepad_END))
                    continue;
                if (!ImGui::IsKeyPressed(key, false)) continue;
                ImGuiKeyChord chord = key;
                if (io.KeyCtrl) chord |= ImGuiMod_Ctrl;
                if (io.KeyShift) chord |= ImGuiMod_Shift;
                if (io.KeyAlt) chord |= ImGuiMod_Alt;
                if (reservedChord(chord)) {
                    g_captureMessage = chordName(chord) + " is reserved; add Ctrl, Alt or Shift, or use F1-F12.";
                } else {
                    setHotkey(g_captureId, chord);
                    if (it) notify(chordName(chord) + " now runs " + it->label, Level::Good);
                    close = true;
                }
                break;
            }
        }
        gap(sp::M);
        alignRight(buttonWidth("Cancel"));
        if (button("Cancel")) close = true;
        if (close) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

 
 
 

namespace {
 
void padBelow(float logical) { ImGui::Dummy(ImVec2(1, std::max(0.0f, px(logical) - ImGui::GetStyle().ItemSpacing.y))); }
}   

void masonry(const std::vector<CardSpec>& cards, float gapPx) {
    const float avail = availWidth();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const int cols = avail >= px(640) ? 2 : 1;
    const float colW = cols == 2 ? std::floor((avail - gapPx) * 0.5f) : avail;
    float colY[2] = {origin.y, origin.y};
    for (const CardSpec& c : cards) {
        float x, y, w;
        int col = 0;
        if (c.full || cols == 1) {
            x = origin.x;
            y = std::max(colY[0], colY[1]);
            w = avail;
        } else {
            col = colY[0] <= colY[1] + 1.0f ? 0 : 1;
            x = origin.x + float(col) * (colW + gapPx);
            y = colY[col];
            w = col == 1 ? avail - colW - gapPx : colW;
        }
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        CardOptions o;
        o.subtitle = c.subtitle.empty() ? nullptr : c.subtitle.c_str();    
        o.width = w;
        o.badge = c.badge.empty() ? nullptr : c.badge.c_str();
        o.pad = c.pad;
        beginCard(c.key.c_str(), c.title.empty() ? nullptr : c.title.c_str(), o);
        if (!c.menuId.empty() && cardHeaderHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) openItemMenu(c.menuId);
        if (c.header) c.header();
        if (c.body) c.body();
        endCard();
        if (c.flash) {
            const float t = std::clamp(float(shell().highlightUntil - shell().now + 0.9) / 1.2f, 0.0f, 1.0f);
            if (t > 0) {
                ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin() + ImVec2(0, c.title.empty() ? 0.0f : px(22)), ImGui::GetItemRectMax(),
                                                    u32(pal::Accent, 0.5f * t), px(kCardRounding), 0, 1.0f);
                requestRedraw();
            }
            if (shell().scrollToHighlight) {
                ImGui::SetScrollFromPosY(ImGui::GetItemRectMin().y - ImGui::GetWindowPos().y - px(16), 0.0f);
                shell().scrollToHighlight = false;
            }
        }
        const float bottom = ImGui::GetItemRectMax().y + gapPx;
        if (c.full || cols == 1) colY[0] = colY[1] = bottom;
        else colY[col] = bottom;
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x, std::max(colY[0], colY[1]) - gapPx));
    ImGui::Dummy(ImVec2(avail, 0));
}

void drawItemRow(const Item& it, bool isConnected, int depth, bool actionAsRow) {
    ItemState& st = state(it);
    const ImGuiKeyChord hk = hotkey(it.id);
    const std::string hkName = chordName(hk);
    const std::string label = titleCase(it.label);
    switch (it.kind) {
    case ItemKind::Toggle: {
        bool v = st.on;
        bool clicked, chip = false;
        {
            DisabledScope off(!isConnected);
            RowOptions o;
            o.help = it.help.empty() ? nullptr : it.help.c_str();
            o.pending = st.pending;
            o.hotkey = hk ? hkName.c_str() : nullptr;
            o.sticky = sticky(it.id);
            o.highlight = highlighted(it);
            o.badge = it.badge.empty() ? nullptr : it.badge.c_str();
            o.depth = depth;
            o.chip = true;
            o.listening = listening(it.id);
            o.chipClicked = &chip;
            clicked = toggleRow(it.id.c_str(), label.c_str(), &v, o);
        }
        if (st.pending && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
            const std::string job = shell().status.job;
            if (!job.empty() && job.rfind(it.label, 0) != 0) ImGui::SetTooltip("Waiting for: %s", job.c_str());
        }
        maybeScrollTo(it);
        contextMenuFor(it);
        if (chip) startListening(it.id);
        if (clicked) setToggle(it, v);
        break;
    }
    case ItemKind::Value: {
        const ValueDef* d = it.value();
        float v = st.value;
        Edit e;
        {
            DisabledScope off(!isConnected);
            ValueOptions o;
            o.help = it.help.empty() ? nullptr : it.help.c_str();
            o.min = d->min;
            o.max = d->max;
            o.step = d->integer ? std::max(1.0f, std::round(d->step)) : d->step;
            o.def = d->def;
            o.format = d->numberFormat();
            o.unit = d->unit.empty() ? nullptr : d->unit.c_str();
            o.logScale = d->logScale && d->min > 0;
            o.integer = d->integer;
            o.pending = st.pending;
            o.hotkey = hk ? hkName.c_str() : nullptr;
            o.sticky = sticky(it.id);
            o.highlight = highlighted(it);
            o.badge = it.badge.empty() ? nullptr : it.badge.c_str();
            o.depth = depth;
            e = valueRow(it.id.c_str(), label.c_str(), &v, o);
        }
        maybeScrollTo(it);
        contextMenuFor(it);
        if (e == Edit::Reset) resetValue(it);
        else if (e == Edit::Committed) applyValue(it, d->quantize(v));
        else if (e == Edit::Changing) {
            st.value = d->quantize(v);
            if (d->applyWhileDragging && !st.pending) applyValue(it, st.value, false);
        }
        break;
    }
    case ItemKind::Action: {
        const ActionDef* a = it.action();
        const bool busy = Engine::get().busy(it.label);
        bool clicked;
        if (actionAsRow) {
            bool chip = false;
            {
                DisabledScope off(!isConnected && !a->immediate);
                RowOptions o;
                o.help = it.help.empty() ? nullptr : it.help.c_str();
                o.pending = busy;
                o.hotkey = hk ? hkName.c_str() : nullptr;
                o.highlight = highlighted(it);
                o.badge = it.badge.empty() ? nullptr : it.badge.c_str();
                o.depth = depth;
                o.chip = true;
                o.chipAlways = true;
                o.listening = listening(it.id);
                o.chipClicked = &chip;
                clicked = actionRow(it.id.c_str(), label.c_str(), "Run", o);
            }
            maybeScrollTo(it);
            contextMenuFor(it);
            if (chip) startListening(it.id);
            if (clicked) runAction(it);
            break;
        }
        std::string tip = it.help;
        if (hk) tip += (tip.empty() ? "" : "\n") + std::string("Hotkey: ") + hkName;
        {
            DisabledScope off(!isConnected && !a->immediate);
            ButtonOptions o;
            o.tone = a->dangerous || a->immediate ? Tone::Danger : Tone::Secondary;
            o.tooltip = tip.empty() ? nullptr : tip.c_str();
            o.busy = busy;
            o.successKey = it.id.c_str();
            clicked = button((label + "##" + it.id).c_str(), o);
        }
        if (highlighted(it)) {
            ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin() - ImVec2(px(2), px(2)), ImGui::GetItemRectMax() + ImVec2(px(2), px(2)),
                                                u32(pal::Accent, 0.8f), px(kFrameRounding + 2), 0, 1.0f);
            requestRedraw();
        }
        maybeScrollTo(it);
        contextMenuFor(it);
        if (!it.badge.empty()) {
            const ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
            ImGui::SameLine(0, px(6));
            const ImVec2 p = ImGui::GetCursorScreenPos();
            drawBadgePill(ImGui::GetWindowDrawList(), ImVec2(p.x, bmin.y + (bmax.y - bmin.y - px(18)) * 0.5f), it.badge.c_str());
            ImGui::Dummy(ImVec2(badgePillWidth(it.badge.c_str()), bmax.y - bmin.y));
        }
        if (clicked) runAction(it);
        break;
    }
    case ItemKind::Section: {
        const SectionDef* s = it.sectionDef();
        if (s->draw) s->draw(shell().bridge);
        break;
    }
    }
}

void drawItemTile(const Item& it, float width, bool isConnected) {
    ImGui::PushID(it.id.c_str());
    ItemState& st = state(it);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = px(52);
    ImGui::SetNextItemAllowOverlap();
    const bool clicked = ImGui::InvisibleButton("##tile", ImVec2(width, h));
    const bool hovered = ImGui::IsItemHovered();
    contextMenuFor(it);
    ImDrawList* d = ImGui::GetWindowDrawList();
    const ImVec4 bg = animateColor(ImGui::GetItemID(), ImVec4(1, 1, 1, hovered ? 0.06f : 0.035f), 20.0f);
    d->AddRectFilled(pos, pos + ImVec2(width, h), u32(bg), px(kRowRounding));
     
    float controlW = 0;
    const float cy = pos.y + h * 0.5f;
    const float padX = px(12);
    if (it.kind == ItemKind::Toggle) {
        controlW = px(22);
        ImGui::SetCursorScreenPos(ImVec2(pos.x + width - padX - controlW, cy - px(11)));
        bool v = st.on;
        DisabledScope off(!isConnected);
        if (toggleSwitch("##sw", &v, st.pending)) setToggle(it, v);
    } else if (it.kind == ItemKind::Action) {
        const bool busy = Engine::get().busy(it.label);
        controlW = buttonWidth("Run", nullptr, true);
        ImGui::SetCursorScreenPos(ImVec2(pos.x + width - padX - controlW, cy - px(kSmallControlH) * 0.5f));
        DisabledScope off(!isConnected);
        ButtonOptions o;
        o.tone = it.action()->dangerous ? Tone::Danger : Tone::Secondary;
        o.small = true;
        o.busy = busy;
        o.successKey = it.id.c_str();
        if (button("Run", o)) runAction(it);
    } else if (it.kind == ItemKind::Value) {
        const std::string text = trimZeros(valueText(it, st.value));
        const ImVec2 ts = textSize(FontKind::Body, type::Label, text.c_str());
        controlW = ts.x;
        drawText(d, FontKind::Body, type::Label, ImVec2(pos.x + width - padX - ts.x, textTop(FontKind::Body, type::Label, cy)), u32(pal::Text), text.c_str());
    } else {
        const ImVec2 cs = textSize(FontKind::Icons, 10, icon::ChevronRight);
        controlW = cs.x;
        drawText(d, FontKind::Icons, 10, ImVec2(pos.x + width - padX - cs.x, cy - cs.y * 0.5f), u32(hovered ? pal::Text : pal::Muted), icon::ChevronRight);
    }
     
    const float tx = pos.x + padX;
    const float tw = std::max(px(40), pos.x + width - padX - controlW - px(10) - tx);
    ImGui::PushClipRect(ImVec2(tx, pos.y), ImVec2(tx + tw, pos.y + h), true);
    const std::string full = titleCase(it.label);
    const std::string labelText = ellipsize(full.c_str(), tw, FontKind::Body, type::Label);
    const ImVec2 ls = textSize(FontKind::Body, type::Label, labelText.c_str());
    std::string context = it.kind == ItemKind::Section ? std::string() : it.section;
    if (!it.parent.empty())
        if (const Item* p = findItem(it.parent)) context = pathLabel(*p, " / ");    
    const std::string where = ellipsize(titleCase(std::string(pageLabel(int(it.page))) + (context.empty() ? "" : " / " + context)).c_str(), tw,
                                        FontKind::Body, type::Caption);
    const ImVec2 ws = textSize(FontKind::Body, type::Caption, where.c_str());
    const float ty = pos.y + (h - ls.y - ws.y) * 0.5f;
    drawText(d, FontKind::Body, type::Label, ImVec2(tx, ty), u32(pal::Text), labelText.c_str());
    drawText(d, FontKind::Body, type::Caption, ImVec2(tx, ty + ls.y), u32(pal::Muted), where.c_str());
    if (labelText != full && hovered) ImGui::SetTooltip("%s", full.c_str());
    ImGui::PopClipRect();
    if (clicked && it.kind == ItemKind::Toggle) setToggle(it, !st.on);    
    if (clicked && (it.kind == ItemKind::Value || it.kind == ItemKind::Section)) reveal(it);
    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy(ImVec2(width, h));
    ImGui::PopID();
}

namespace {

struct Group {
    std::string title;
    int order = 50;
    size_t first = 0;
    std::vector<const Item*> toggles, values, actions, sections;
    std::string badge;             
};

 
void drawActionLine(const std::vector<const Item*>& actions, bool isConnected) {
    const float avail = availWidth();
    float x = 0;
    bool first = true;
    for (const Item* it : actions) {
        const std::string label = titleCase(it->label);
        const float w = buttonWidth(label.c_str(), nullptr) + (it->badge.empty() ? 0.0f : badgePillWidth(it->badge.c_str()) + px(6));
        if (!first) {
            if (x + px(8) + w <= avail) {
                ImGui::SameLine(0, px(8));
                x += px(8);
            } else {
                x = 0;
            }
        }
        first = false;
        drawItemRow(*it, isConnected);
        x += w;
    }
}

}   

 
 
 
 
void drawChildren(const Item& parent, bool isConnected, int depth) {
    const auto& kids = childrenOf(parent.id);
    if (kids.empty() || !state(parent).on) return;
    for (const Item* k : kids) {
        drawItemRow(*k, isConnected, depth, true);
        if (k->kind == ItemKind::Toggle) drawChildren(*k, isConnected, depth + 1);
    }
}

namespace {

void drawGroupBody(const Group& g, bool isConnected) {
    const bool rows = !g.toggles.empty() || !g.values.empty();
    if (rows) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 0.0f));
        for (const Item* it : g.toggles) {
            drawItemRow(*it, isConnected);
            drawChildren(*it, isConnected, 1);
        }
        for (const Item* it : g.values) drawItemRow(*it, isConnected);
        ImGui::PopStyleVar();
    }
     
    const bool more = !g.sections.empty() || !g.actions.empty();
    if (rows && more) padBelow(sp::M);
    for (size_t i = 0; i < g.sections.size(); ++i) {
        if (i > 0) gap(sp::M);
        drawItemRow(*g.sections[i], isConnected);
    }
    if (!g.actions.empty()) {
        if (!g.sections.empty()) gap(sp::M);
        drawActionLine(g.actions, isConnected);
    }
    if (rows && more) padBelow(sp::M);
}

std::string lowerText(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}   

BR_FEATURES(ui_panic) {
    r.action({.id = kPanicId,
              .page = Page::Testing,
              .tab = "Runtime",
              .section = "Runtime",
              .label = "Panic: all off",
              .help = "Every switch off, changed values back to default, loops stopped, every patch, hook and task reverted. "
                      "Stays connected (the executor keeps running). Also in the top bar.",
              .run = [](Ctx&) {},
              .hotkey = "Ctrl+Shift+P",
              .immediate = [] { panicNow(); }});
}

void drawRegistryPage(int page) {
    const Page p = Page(page);
    const auto tabs = Registry::get().tabs(p);    
    int& sel = shell().tab[page];
    sel = std::clamp(sel, 0, std::max(0, int(tabs.size()) - 1));
    const std::string tab = tabs.empty() ? std::string() : tabs[size_t(sel)];
    std::vector<Group> groups;
    auto groupFor = [&](const Item& it, size_t idx) -> Group& {
        const std::string title = !it.section.empty() ? it.section : !it.tab.empty() ? it.tab : std::string("General");
        for (Group& g : groups)
            if (g.title == title) return g;
        groups.push_back({title, 50, idx, {}, {}, {}, {}, {}});
        return groups.back();
    };
    const auto& all = items();
    for (size_t i = 0; i < all.size(); ++i) {
        const Item& it = all[i];
        if (it.page != p) continue;
        if (tabs.size() > 1 && it.tab != tab) continue;    
        if (!it.parent.empty()) continue;                  
        if (it.kind == ItemKind::Action) {
             
            if (p == Page::Testing && it.tab == kPortChecksTab) continue;
            const SectionStyle* style = findSectionStyle(it.section);
            if (style && style->ownsActions) continue;    
        }
        Group& g = groupFor(it, i);
        switch (it.kind) {
        case ItemKind::Toggle: g.toggles.push_back(&it); break;
        case ItemKind::Value: g.values.push_back(&it); break;
        case ItemKind::Action: g.actions.push_back(&it); break;
        case ItemKind::Section:
            g.sections.push_back(&it);
            g.order = g.sections.size() == 1 ? it.sectionDef()->order : std::min(g.order, it.sectionDef()->order);
            if (g.badge.empty()) g.badge = it.badge;
            break;
        }
    }
    if (groups.empty()) {
        beginCard("##empty", nullptr);
        const std::string text = std::string("This page is next in the port: ") + pagePlans(page) + ".";
        emptyState(pageIcon(page), (std::string(pageLabel(page)) + " Is Coming").c_str(), text.c_str(), 180);
        const float bw1 = buttonWidth("Open Loot events"), bw2 = buttonWidth("Open TESTING");
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (availWidth() - bw1 - bw2 - px(8)) * 0.5f));
        if (button("Open Loot events")) navigate(int(Page::LootEvents));
        ImGui::SameLine(0, px(8));
        if (button("Open TESTING", {.tone = Tone::Ghost})) navigate(int(Page::Testing));
        gap(sp::L);
        endCard();
        return;
    }
    std::stable_sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) {
        return a.order != b.order ? a.order < b.order : a.first < b.first;
    });
    const bool isConnected = connected();
     
    const std::string pageLower = lowerText(pageLabel(page)), tabLower = lowerText(tab);
    std::vector<CardSpec> cards;
    for (const Group& g : groups) {
        CardSpec c;
        c.key = g.title;
        const std::string tl = lowerText(g.title);
        c.title = tl == pageLower || tl == tabLower || tl == "general" ? std::string() : g.title;
        if (const SectionStyle* s = findSectionStyle(g.title)) {
            c.full = s->fullWidth;
            c.subtitle = s->subtitle;
        }
        if (g.sections.size() == 1) c.menuId = g.sections.front()->id;    
        c.badge = g.badge;
        c.pad = (!g.toggles.empty() || !g.values.empty()) ? 0.0f : -1.0f;
        for (const Item* it : g.sections)
            if (highlighted(*it)) c.flash = true;
        c.body = [&g, isConnected] { drawGroupBody(g, isConnected); };
        cards.push_back(std::move(c));
    }
    masonry(cards, px(sp::L));
}

}   
