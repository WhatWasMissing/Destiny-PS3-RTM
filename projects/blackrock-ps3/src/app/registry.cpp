#include "app/registry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <utility>

namespace brps3 {

namespace {

 
 
 
struct Canon {
    std::vector<std::string> front, back;
};

const Canon& canon(Page p) {
    static const Canon none{};
    static const Canon spawner{{"Spawn", "Objects", "Resident tags", "World load", "Squads", "Encounters"}, {}};
    static const Canon hopons{{"Hop-ons", "Tracked", "Catalog"}, {}};
    static const Canon console{{"Player", "Weapons", "Movement", "Camera", "World", "Session"}, {}};
    static const Canon loot{{"Loot", "PoE", "Pickup filter", "Grimoire", "Lighthouse"}, {}};
    static const Canon loot_events{{"Loot events", "Dispatch", "Item route"}, {}};
    static const Canon vault{{"Inventory", "Vault", "Editor", "Farms", "Catalog"}, {}};
    static const Canon testing{{"Runtime", "Systems library", "Pools & globals", "Components", "Effects", "Facets", "Movement",
                                "Inventory", "Resourcer", "Research"},
                               {kPortChecksTab}};
    switch (p) {
    case Page::Spawner: return spawner;
    case Page::HopOns: return hopons;
    case Page::Console: return console;
    case Page::Loot: return loot;
    case Page::LootEvents: return loot_events;
    case Page::Vault: return vault;
    case Page::Testing: return testing;
    default: return none;
    }
}

template <class Def>
const Def* findById(const std::vector<Def>& v, const std::string& id) {
    for (const Def& d : v)
        if (d.id == id) return &d;
    return nullptr;
}

const std::string& emptyString() {
    static const std::string s;
    return s;
}

}   

 

const char* ValueDef::numberFormat() const {
    if (integer && (format.empty() || format == "%.2f")) return "%.0f";
    return format.empty() ? "%.2f" : format.c_str();
}

float ValueDef::quantize(float v) const {
    if (!std::isfinite(v)) v = def;
    v = std::clamp(v, min, max);
    float s = step;
    if (integer) s = std::max(1.0f, std::round(s));
    if (s > 0) v = std::clamp(min + std::round((v - min) / s) * s, min, max);
    if (integer && std::ceil(min) <= std::floor(max)) {
        v = std::round(v);
        if (v < min) v = std::ceil(min);    
        if (v > max) v = std::floor(max);
    }
    return v;
}

std::string ValueDef::text(float v) const {
    char buf[64];
    std::snprintf(buf, sizeof buf, numberFormat(), double(v));
    std::string s = buf;
    if (unit.empty()) return s;
     
    const bool tight = unit == "x" || unit == "%" || unit.rfind("\xC2\xB0", 0) == 0;
    return s + (tight ? "" : " ") + unit;
}

 

const char* pageName(Page p) {
    switch (p) {
        case Page::Home: return "Home";
        case Page::Spawner: return "Spawner";
        case Page::HopOns: return "Hop-ons";
        case Page::Console: return "Console";
        case Page::Loot: return "Loot";
        case Page::LootEvents: return "Loot events";
        case Page::Vault: return "Vault";
        case Page::Testing: return "TESTING";
        case Page::Count: break;
    }
    return "?";
}

Registry& Registry::get() {
    static Registry r;
    return r;
}

void Registry::toggle(ToggleDef d) {
    order_.push_back({Kind::Toggle, toggles_.size()});
    toggles_.push_back(std::move(d));
}

void Registry::value(ValueDef d) {
    order_.push_back({Kind::Value, values_.size()});
    values_.push_back(std::move(d));
}

void Registry::action(ActionDef d) {
    order_.push_back({Kind::Action, actions_.size()});
    actions_.push_back(std::move(d));
}

void Registry::section(SectionDef d) {
    order_.push_back({Kind::Section, sections_.size()});
    sections_.push_back(std::move(d));
}

std::vector<std::string> Registry::canonicalTabs(Page p) {
    const Canon& c = canon(p);
    std::vector<std::string> out = c.front;
    out.insert(out.end(), c.back.begin(), c.back.end());
    return out;
}

std::string Registry::defaultTab(Page p) {
    const Canon& c = canon(p);
    return c.front.empty() ? std::string("General") : c.front.front();
}

std::vector<std::string> Registry::tabs(Page p) const {
     
     
    std::set<std::string> used;    
    for (const Ref& r : order_) {
        const Place pl = placeOf(r);
        if (pl.page == p) used.insert(pl.tab);
    }
    const Canon& c = canon(p);
    auto known = [&](const std::string& t) {
        return std::find(c.front.begin(), c.front.end(), t) != c.front.end() || std::find(c.back.begin(), c.back.end(), t) != c.back.end();
    };
    std::vector<std::string> out;
    for (const std::string& t : c.front)
        if (used.count(t)) out.push_back(t);
    for (const std::string& t : used)
        if (!known(t)) out.push_back(t);
    for (const std::string& t : c.back)
        if (used.count(t)) out.push_back(t);
    return out;
}

const ToggleDef* Registry::findToggle(const std::string& id) const { return findById(toggles_, id); }
const ValueDef* Registry::findValue(const std::string& id) const { return findById(values_, id); }
const ActionDef* Registry::findAction(const std::string& id) const { return findById(actions_, id); }

 

const std::string& Registry::idOf(Ref r) const {
    switch (r.kind) {
    case Kind::Toggle: return r.index < toggles_.size() ? toggles_[r.index].id : emptyString();
    case Kind::Value: return r.index < values_.size() ? values_[r.index].id : emptyString();
    case Kind::Action: return r.index < actions_.size() ? actions_[r.index].id : emptyString();
    case Kind::Section: return r.index < sections_.size() ? sections_[r.index].id : emptyString();
    }
    return emptyString();
}

const std::string& Registry::declaredParent(Ref r) const {
    switch (r.kind) {
    case Kind::Toggle: return r.index < toggles_.size() ? toggles_[r.index].parent : emptyString();
    case Kind::Value: return r.index < values_.size() ? values_[r.index].parent : emptyString();
    case Kind::Action: return r.index < actions_.size() ? actions_[r.index].parent : emptyString();
    case Kind::Section: break;
    }
    return emptyString();
}

std::string Registry::parentProblem(Ref r) const {
    const std::string& parent = declaredParent(r);
    if (parent.empty()) return {};
    const std::string& self = idOf(r);
    if (parent == self) return "is its own parent";
    const ToggleDef* p = findToggle(parent);
    if (!p) {
        if (findValue(parent) || findAction(parent)) return "parent '" + parent + "' is not a switch (only a ToggleDef can own sub-rows)";
        return "parent '" + parent + "' is not a registered switch";
    }
     
    std::vector<std::string> seen{self};
    int depth = 1;
    for (const ToggleDef* cur = p; cur; ++depth) {
        if (std::find(seen.begin(), seen.end(), cur->id) != seen.end()) return "parent chain loops back to '" + cur->id + "'";
        if (depth > kMaxChildDepth) return "parent chain is deeper than " + std::to_string(kMaxChildDepth) + " levels";
        seen.push_back(cur->id);
        if (cur->parent.empty()) break;
        const ToggleDef* next = findToggle(cur->parent);
        if (!next) return "ancestor '" + cur->id + "' has an invalid parent '" + cur->parent + "'";
        cur = next;
    }
    return {};
}

const ToggleDef* Registry::parentOf(Ref r) const {
    const std::string& parent = declaredParent(r);
    if (parent.empty() || !parentProblem(r).empty()) return nullptr;
    return findToggle(parent);
}

const ToggleDef* Registry::parentOf(const std::string& id) const {
    for (const Ref& r : order_)
        if (r.kind != Kind::Section && idOf(r) == id) return parentOf(r);
    return nullptr;
}

const ToggleDef* Registry::rootOf(Ref r) const {
    const ToggleDef* p = parentOf(r);
     
    while (p && !p->parent.empty()) {
        const ToggleDef* next = findToggle(p->parent);
        if (!next) break;
        p = next;
    }
    return p;
}

int Registry::depth(Ref r) const {
    const ToggleDef* p = parentOf(r);
    int d = 0;
    while (p && d <= kMaxChildDepth) {
        ++d;
        p = p->parent.empty() ? nullptr : findToggle(p->parent);
    }
    return d;
}

std::vector<Registry::Ref> Registry::children(const std::string& parentId) const {
    std::vector<Ref> out;
    if (parentId.empty()) return out;
    for (const Ref& r : order_)
        if (r.kind != Kind::Section && declaredParent(r) == parentId && parentOf(r)) out.push_back(r);
    return out;
}

Registry::Place Registry::placeOf(Ref r) const {
    Place pl{Page::Console, {}, {}};
    auto set = [&](Page page, const std::string& tab, const std::string& section) {
        pl.page = page;
        pl.tab = effectiveTab(page, tab);
        pl.section = section;
    };
    if (r.kind != Kind::Section) {
        if (const ToggleDef* root = rootOf(r)) {
            set(root->page, root->tab, root->section);
            return pl;
        }
    }
    switch (r.kind) {
    case Kind::Toggle: set(toggles_[r.index].page, toggles_[r.index].tab, toggles_[r.index].section); break;
    case Kind::Value: set(values_[r.index].page, values_[r.index].tab, values_[r.index].section); break;
    case Kind::Action: set(actions_[r.index].page, actions_[r.index].tab, actions_[r.index].section); break;
    case Kind::Section: set(sections_[r.index].page, sections_[r.index].tab, sections_[r.index].section); break;
    }
    return pl;
}

}   
