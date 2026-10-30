 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "ui/shell_internal.h"

namespace brps3::ui {

namespace {

std::string oneLine(std::string s) {
    for (char& c : s)
        if (c == '\n' || c == '\t' || c == '\r') c = ' ';
    return s;
}

struct Entry {
    const char* kind;
    std::string id, tab, section, label, help, badge, hotkey;
    Page page;
    std::string parent, unit;
};

std::vector<Entry> entries() {
    std::vector<Entry> out;
    const Registry& r = Registry::get();
    for (const auto& t : r.toggles())
        out.push_back({"switch", t.id, Registry::effectiveTab(t.page, t.tab), t.section, t.label, t.help, t.badge, t.hotkey, t.page, t.parent, {}});
    for (const auto& v : r.values())
        out.push_back({"value", v.id, Registry::effectiveTab(v.page, v.tab), v.section, v.label, v.help, v.badge, {}, v.page, v.parent, v.unit});
    for (const auto& a : r.actions())
        out.push_back({"action", a.id, Registry::effectiveTab(a.page, a.tab), a.section, a.label, a.help, a.badge, a.hotkey, a.page, a.parent, {}});
    for (const auto& s : r.sections())
        out.push_back({"section", s.id, Registry::effectiveTab(s.page, s.tab), s.section, s.section, {}, s.badge, {}, s.page, {}, {}});
    return out;
}

bool integral(float v) { return std::floor(v) == v; }

}   

int runSelfCheck(const std::string& reportPath, const std::string& itemsPath) {
    std::vector<std::string> errors, warnings;
    const Registry& r = Registry::get();
    const std::vector<Entry> all = entries();

     
    std::map<std::string, std::vector<const Entry*>> byId;
    for (const Entry& e : all) byId[e.id].push_back(&e);
    for (const auto& [id, list] : byId)
        if (list.size() > 1) {
            std::string where;
            for (const Entry* e : list) where += std::string(where.empty() ? "" : ", ") + e->kind + " '" + e->label + "' (" + pageName(e->page) + " / " + e->tab + ")";
            errors.push_back("duplicate id " + id + ": " + where);
        }
        else if (id.empty()) errors.push_back("an item has an empty id: '" + list.front()->label + "'");

     
    for (const auto& t : r.toggles())
        if (!t.set) errors.push_back("switch " + t.id + " has no set()");
    for (const auto& v : r.values()) {
        if (!v.set) errors.push_back("value " + v.id + " has no set()");
        if (!(v.min < v.max)) errors.push_back("value " + v.id + ": min >= max");
        else if (v.def < v.min || v.def > v.max) errors.push_back("value " + v.id + ": default outside [min, max]");
        if (v.logScale && !(v.min > 0)) errors.push_back("value " + v.id + ": logScale needs min > 0");
        if (v.integer && !(integral(v.min) && integral(v.max) && integral(v.def) && integral(v.step)))
            errors.push_back("value " + v.id + ": integer, but min/max/def/step are not whole numbers");
        if (!v.unit.empty() && (v.unit.front() == ' ' || v.unit.back() == ' '))
            warnings.push_back("value " + v.id + ": unit '" + v.unit + "' has spaces (the UI adds them)");
    }

     
     
    for (const Registry::Ref& ref : r.order()) {
        const std::string problem = r.parentProblem(ref);
        if (!problem.empty()) {
            errors.push_back(r.idOf(ref) + ": " + problem + " (drawn as a top-level item)");
            continue;
        }
        const ToggleDef* parent = r.parentOf(ref);
        if (!parent) continue;
        Page page = parent->page;
        std::string tab;
        if (ref.kind == Registry::Kind::Toggle) {
            page = r.toggles()[ref.index].page;
            tab = r.toggles()[ref.index].tab;
        } else if (ref.kind == Registry::Kind::Value) {
            page = r.values()[ref.index].page;
            tab = r.values()[ref.index].tab;
        } else if (ref.kind == Registry::Kind::Action) {
            page = r.actions()[ref.index].page;
            tab = r.actions()[ref.index].tab;
        }
        const Registry::Place where = r.placeOf(ref);
        if (page != where.page || (!tab.empty() && Registry::effectiveTab(page, tab) != where.tab))
            warnings.push_back(r.idOf(ref) + " declares " + pageName(page) + " / " + Registry::effectiveTab(page, tab) + " but its parent " +
                               parent->id + " is on " + pageName(where.page) + " / " + where.tab + " (it is drawn under the parent)");
    }
    for (const auto& a : r.actions())
        if (!a.run && !a.immediate) errors.push_back("action " + a.id + " has neither run() nor immediate()");
    for (const auto& s : r.sections())
        if (!s.draw) errors.push_back("section " + s.id + " has no draw()");

     
    std::map<ImGuiKeyChord, std::string> chords;
    for (const Entry& e : all) {
        if (e.hotkey.empty()) continue;
        const ImGuiKeyChord c = parseChord(e.hotkey);
        if (!c) {
            errors.push_back("hotkey '" + e.hotkey + "' of " + e.id + " does not parse");
            continue;
        }
        auto [it, fresh] = chords.emplace(c, e.id);
        if (!fresh) errors.push_back("default hotkey " + e.hotkey + " is used by both " + it->second + " and " + e.id);
    }

     
    for (const Entry& e : all)
        if (std::string(e.kind) != "section" && e.help.empty()) warnings.push_back(std::string(e.kind) + " " + e.id + " ('" + e.label + "') has no help text");

     
    std::map<std::string, std::set<std::string>> titlePlaces;
    for (const Entry& e : all) {
        if (!e.parent.empty()) continue;    
        const std::string title = !e.section.empty() ? e.section : e.tab;
        titlePlaces[title].insert(std::string(pageName(e.page)) + " / " + e.tab);
    }
    for (const auto& [title, places] : titlePlaces)
        if (places.size() > 1 && findSectionStyle(title)) {
            std::string where;
            for (const auto& p : places) where += (where.empty() ? "" : ", ") + p;
            warnings.push_back("section title '" + title + "' is styled once but used on " + where);
        }

     
    for (const std::string& t : detail::sectionStyleConflicts())
        errors.push_back("section title '" + t + "' is styled twice with different styles (styleSection is keyed by title)");

     
    for (const Entry& e : all)
        if (!e.badge.empty() && e.badge != kBadgeNeedsCheck && e.badge != kBadgeBlocked && e.badge != kBadgeDev)
            warnings.push_back(e.id + " has the badge '" + e.badge + "' (drawn as a neutral pill)");

     
    for (int p = 0; p < int(Page::Count); ++p) {
        const auto canon = Registry::canonicalTabs(Page(p));
        if (canon.empty()) continue;
        for (const std::string& t : r.tabs(Page(p)))
            if (std::find(canon.begin(), canon.end(), t) == canon.end())
                warnings.push_back(std::string("tab '") + t + "' on " + pageName(Page(p)) + " is not in the canonical tab order");
    }

     
    std::string text = "Blackrock PS3 registry self-check\n\n";
    char buf[256];
    snprintf(buf, sizeof(buf), "%zu switches, %zu values, %zu actions, %zu sections (%zu items)\n", r.toggles().size(), r.values().size(),
             r.actions().size(), r.sections().size(), all.size());
    text += buf;
    for (int p = 0; p < int(Page::Count); ++p) {
        const auto tabs = r.tabs(Page(p));
        size_t n = 0;
        for (const Entry& e : all) n += e.page == Page(p) ? 1 : 0;
        text += std::string("  ") + pageName(Page(p)) + " (" + std::to_string(n) + " items): ";
        for (size_t i = 0; i < tabs.size(); ++i) text += (i ? ", " : "") + tabs[i];
        text += "\n";
    }
    text += "\nErrors: " + std::to_string(errors.size()) + "\n";
    for (const auto& e : errors) text += "  " + e + "\n";
    text += "\nWarnings: " + std::to_string(warnings.size()) + "\n";
    for (const auto& w : warnings) text += "  " + w + "\n";

    auto writeFile = [](const std::string& path, const std::string& body) {
        FILE* f = nullptr;
        if (fopen_s(&f, path.c_str(), "wb") != 0 || !f) return false;
        fwrite(body.data(), 1, body.size(), f);
        fclose(f);
        return true;
    };
    bool ok = reportPath.empty() || writeFile(reportPath, text);
    if (!itemsPath.empty()) {
        std::string tsv = "kind\tid\tpage\ttab\tsection\tlabel\tbadge\thotkey\thelp\tparent\tunit\n";
        for (const Entry& e : all)
            tsv += std::string(e.kind) + "\t" + e.id + "\t" + pageName(e.page) + "\t" + e.tab + "\t" + oneLine(e.section) + "\t" + oneLine(e.label) +
                   "\t" + e.badge + "\t" + e.hotkey + "\t" + oneLine(e.help) + "\t" + e.parent + "\t" + oneLine(e.unit) + "\n";
        ok = writeFile(itemsPath, tsv) && ok;
    }
    return !ok ? 2 : errors.empty() ? 0 : 1;
}

}   
