 
 
 
 
 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "app/engine.h"
#include "app/registry.h"
#include "app/settings.h"
#include "features/lab_ui.h"
#include "game/lab_library.h"
#include "game/names_catalog.h"
#include "game/world_addr.h"
#include "ui/widgets.h"

namespace brps3 {
namespace {

using namespace ui;
using labui::fmt;
using labui::hx;
using lab::Col;

constexpr const char* kSection = "Systems library";
constexpr const char* kRecentKey = "lab.library.recent";
constexpr const char* kSelectedKey = "lab.library.selected";
constexpr float kRowH = 30;

struct Query {
    std::string text, system, kind;
    uint32_t ref = 0;
    int only = 0;                         
    uint64_t rev = 0;                     
    bool operator==(const Query&) const = default;
};

struct State {
     
    std::mutex m;
    std::shared_ptr<const lab::Library> lib;
    std::string loadError;
    double loadSeconds = 0;
    std::atomic<bool> loading{false};
    bool started = false;
     
    std::atomic<uint64_t> gen{0};
    std::atomic<bool> filtering{false};
    std::vector<uint32_t> view;           
    uint64_t viewGen = 0;
    Query applied, wanted;
    double wantedAt = 0;
     
    std::string text;
    int system = 0, kind = 0, only = 0;
    uint32_t ref = 0;
    int selected = -1;                    
    std::string selectedKey;              
    bool scrollTo = false;
    bool editing = false;
    std::string editName, editNotes;
    bool editVerified = false;
    uint64_t labelEdits = 0;
};

State& st() {
    static State s;
    return s;
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::shared_ptr<const lab::Library> lib() {
    std::lock_guard<std::mutex> l(st().m);
    return st().lib;
}

void startLoad() {
    State& s = st();
    if (s.started) return;
    s.started = true;
    s.loading = true;
    s.selectedKey = Engine::get().settings().str(kSelectedKey, "");
    if (labui::demo()) {
        s.selectedKey = "AI squads/Squad definition/80C7A053";    
        s.text = "queen";
    }
    auto load = [] {
        const double t0 = labui::now();
        std::shared_ptr<const lab::Library> l;
        std::string err;
        try {
            l = lab::loadLibrary();
        } catch (const std::exception& e) {
            err = e.what();
        }
        State& S = st();
        {
            std::lock_guard<std::mutex> lock(S.m);
            S.lib = l;
            S.loadError = err;
            S.loadSeconds = labui::now() - t0;
        }
        S.loading = false;
    };
     
    if (labui::demo()) load();
    else std::thread(load).detach();
}

 

std::string displayName(const lab::Library& L, uint32_t row) {
    const uint32_t tag = L.tag(row);
    if (tag)
        if (const auto own = names::Labels::get().find(names::toPs3Tag(tag)); own && !own->name.empty()) return own->name;
    if (!tag)
        if (const auto n = lab::LibraryStore::get().note(L.key(row)); n && !n->name.empty()) return n->name;
    std::string name = L.text(row, Col::Name);
    if (name.empty() && tag) name = names::tagName(tag);
    return name.empty() ? L.text(row, Col::Id) : name;
}

std::string nameEvidence(const lab::Library& L, uint32_t row) {
    const uint32_t tag = L.tag(row);
    if (!tag) {
        if (const auto n = lab::LibraryStore::get().note(L.key(row)); n && !n->name.empty()) return "Your saved name";
    } else {
        if (const auto own = names::Labels::get().find(names::toPs3Tag(tag)); own && !own->name.empty())
            return own->verified ? "Your verified name" : "Your saved name";
        if (const names::Entity* e = names::entities().find(tag); e && e->hasName())
            return "Recovered candidate" + (e->confidence.empty() ? std::string() : " (" + e->confidence + ")");
    }
    return L.text(row, Col::Evidence);
}

std::string detailsText(const lab::Library& L, uint32_t row) {
    std::string t = displayName(L, row) + "\n" + L.text(row, Col::Namespace) + ": " + L.text(row, Col::Id) + "\nName evidence: " + nameEvidence(L, row);
    if (const uint32_t tag = L.tag(row)) {
        if (const auto own = names::Labels::get().find(names::toPs3Tag(tag)); own && !own->notes.empty()) t += "\nYour notes: " + own->notes;
    } else if (const auto n = lab::LibraryStore::get().note(L.key(row)); n && !n->notes.empty()) {
        t += "\nYour notes: " + n->notes;
    }
    return t + "\n" + L.text(row, Col::Details);
}

 

Query currentQuery(const lab::Library& L) {
    State& s = st();
    Query q;
    q.text = s.text;
    q.system = s.system > 0 && size_t(s.system) <= L.systems().size() ? L.systems()[size_t(s.system - 1)] : std::string();
    const auto kinds = L.kinds(q.system);
    q.kind = s.kind > 0 && size_t(s.kind) <= kinds.size() ? kinds[size_t(s.kind - 1)] : std::string();
    q.ref = s.ref;
    q.only = s.only;
    q.rev = lab::LibraryStore::get().revision() * 1000003u + s.labelEdits;
    return q;
}

void startFilter(std::shared_ptr<const lab::Library> L, const Query& q) {
    State& s = st();
    const uint64_t gen = ++s.gen;
    s.filtering = true;
     
    const std::set<std::string> saved = lab::LibraryStore::get().savedKeys();
    const auto notes = lab::LibraryStore::get().notes();
    const auto labels = names::Labels::get().all();
    std::set<std::string> recent;
    for (const auto& k : Engine::get().settings().list(kRecentKey)) recent.insert(k);
    auto work = [L, q, gen, saved, notes, labels, recent] {
        State& S = st();
         
        std::map<uint32_t, std::string> extra;
        std::set<uint32_t> annotated;
        for (const auto& [tag, label] : labels)
            for (size_t row : L->rowsWithTag(names::fromPs3Tag(tag))) {
                extra[uint32_t(row)] += " " + lower(label.name + " " + label.notes);
                annotated.insert(uint32_t(row));
            }
        for (const auto& [key, note] : notes)
            if (const auto row = L->findKey(key)) {
                extra[uint32_t(*row)] += " " + lower(note.name + " " + note.notes);
                annotated.insert(uint32_t(*row));
            }
        auto extraFn = [&](uint32_t row) -> const std::string* {
            auto it = extra.find(row);
            return it == extra.end() ? nullptr : &it->second;
        };
        auto keep = [&](uint32_t row) {
            if (q.only == 1) return saved.count(L->key(row)) != 0;
            if (q.only == 2) return recent.count(L->key(row)) != 0;
            if (q.only == 3) return annotated.count(row) != 0;
            return true;
        };
        std::vector<uint32_t> rows = L->filter({q.text, q.system, q.kind, q.ref}, extraFn, keep, [&] { return S.gen.load() != gen; });
        if (S.gen.load() != gen) return;
        {
            std::lock_guard<std::mutex> lock(S.m);
            S.view = std::move(rows);
            S.viewGen = gen;
        }
        S.filtering = false;
        requestRedraw();
    };
    if (labui::demo()) work();    
    else std::thread(work).detach();
}

void select(const lab::Library& L, int row) {
    State& s = st();
    if (s.selected == row) return;
    s.selected = row;
    s.scrollTo = true;
    s.editing = false;
    if (row >= 0) {
        const std::string key = L.key(uint32_t(row));
        Engine::get().settings().set(std::string(kSelectedKey), key);
        Engine::get().settings().pushRecent(kRecentKey, key, 30);
    }
}

 
void toggleSavedResource(const std::string& key) {
    lab::LibraryStore& st = lab::LibraryStore::get();
    if (!st.toggleSaved(key)) notify("Saved resources unchanged: " + st.error(), Level::Error);
}

 
 
void reloadAll() {
    State& s = st();
    if (s.loading.load()) return;
    lab::LibraryStore::get().reload();
    ++s.gen;    
    {
        std::lock_guard<std::mutex> lock(s.m);
        s.lib.reset();
        s.loadError.clear();
        s.view.clear();
        s.viewGen = 0;
    }
    s.applied = s.wanted = Query{};
    s.selected = -1;
    s.editing = false;
    s.started = false;
    notify("Reloading the Systems library", Level::Info);
}

void searchFor(const std::string& text) {
    State& s = st();
    s.text = text;
    s.system = s.kind = s.only = 0;
    s.ref = 0;
}

 

Page pageFor(const lab::Library& L, uint32_t row, const char** label, const char** tab) {
    const std::string ns = L.text(row, Col::Namespace);
    const names::Entity* e = names::entities().find(L.tag(row));
    if (ns == "entity tag" && e && e->type >= 20 && e->type <= 23) {
        *label = "Open in Hop-Ons";
        *tab = "Hop-ons";
        return Page::HopOns;
    }
    if (ns == "root tag") {
        *label = "Open in Encounters";
        *tab = "Encounters";
        return Page::Spawner;
    }
    if (ns == "squad definition tag") {
        *label = "Open in Squads";
        *tab = "Squads";
        return Page::Spawner;
    }
    *label = "Open in Spawner";
    *tab = "Spawn";
    return Page::Spawner;
}

void drawEditor(const lab::Library& L, uint32_t row) {
    State& s = st();
    const uint32_t tag = L.tag(row);
    beginCard("##edit", nullptr, {.raised = true});
    fieldLabel(tag ? "YOUR NAME FOR THIS TAG (SHARED WITH EVERY PAGE)" : "YOUR NAME FOR THIS RESOURCE");
    gap(sp::XS);
    inputText("##ename", &s.editName, "Name", -1);
    gap(sp::S);
    fieldLabel("NOTES");
    gap(sp::XS);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, pal::Input);
    ImGui::InputTextMultiline("##enotes", &s.editNotes[0], s.editNotes.capacity() + 1, ImVec2(-1, px(90)), ImGuiInputTextFlags_CallbackResize,
                              [](ImGuiInputTextCallbackData* d) {
                                  if (d->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                                      auto* str = static_cast<std::string*>(d->UserData);
                                      str->resize(size_t(d->BufTextLen));
                                      d->Buf = str->data();
                                  }
                                  return 0;
                              },
                              &s.editNotes);
    ImGui::PopStyleColor();
    if (tag) {
        gap(sp::XS);
        checkbox("Seen on Screen (Verified)", &s.editVerified);
    }
    gap(sp::S);
    if (button("Save", {.tone = Tone::Primary, .icon = icon::Check, .tooltip = "Leave both empty to remove your entry"})) {
        if (tag) {
            if (s.editName.empty() && s.editNotes.empty()) names::Labels::get().remove(names::toPs3Tag(tag));
            else names::Labels::get().set(names::toPs3Tag(tag), {s.editName, s.editNotes, s.editVerified});
            const std::string err = names::Labels::get().error();
            if (!err.empty()) notify("Names: " + err, Level::Error);
        } else if (!lab::LibraryStore::get().setNote(L.key(row), {s.editName, s.editNotes})) {
            notify("Notes not saved: " + lab::LibraryStore::get().error(), Level::Error);
            endCard();
            return;
        }
        ++s.labelEdits;
        s.editing = false;
        notify("Saved", Level::Good);
    }
    ImGui::SameLine(0, px(8));
    if (button("Cancel", {.tone = Tone::Ghost})) s.editing = false;
    endCard();
}

void drawDetail(const lab::Library& L, float width, float height) {
    State& s = st();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, pal::Raised);
    ImGui::PushStyleColor(ImGuiCol_Border, pal::Border);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(kRowRounding));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(16), px(14)));
    ImGui::BeginChild("##libdetail", ImVec2(width, height), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    if (s.selected < 0 || size_t(s.selected) >= L.size()) {
        emptyState(icon::Vault, "Select a Resource",
                   "Pick a Row to See Its Relationships, Name Evidence and Details. Press / to Search; Every Word Must Match Some Field, a Recovered "
                   "Name or Your Own Notes.",
                   std::max(200.0f, height / scale() - 40));
        ImGui::EndChild();
        return;
    }
    const uint32_t row = uint32_t(s.selected);
    const std::string key = L.key(row);
    const uint32_t tag = L.tag(row);
    const bool saved = lab::LibraryStore::get().saved(key);
     
    {
        const float starW = px(30);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + availWidth() - starW - px(8));
        heading(displayName(L, row).c_str());
        ImGui::PopTextWrapPos();
        const ImVec2 after = ImGui::GetCursorScreenPos();
        ImGui::SameLine();
        alignRight(starW);
        DisabledScope locked(!lab::LibraryStore::get().savedEditable(), "lab_library_saved.txt could not be read: fix or remove it, then Reload");
        if (iconButton("##save", saved ? icon::StarFill : icon::Star, saved ? "Remove from saved resources" : "Save this resource", false, 30,
                       saved ? pal::Accent : pal::Muted))
            toggleSavedResource(key);
        ImGui::SetCursorScreenPos(after);
    }
    gap(sp::XS);
    pill(L.text(row, Col::System).c_str(), pal::Info);
    ImGui::SameLine(0, px(6));
    pill(L.text(row, Col::Kind).c_str(), pal::Muted);
    gap(sp::S);
    caption((L.text(row, Col::Namespace) + ":").c_str(), pal::Faint);
    ImGui::SameLine(0, px(6));
    copyable(L.text(row, Col::Id).c_str(), "id", pal::Accent);
    if (tag) {
        if (L.text(row, Col::Id) != hx(tag)) {
            caption("Tag", pal::Faint);
            ImGui::SameLine(0, px(6));
            hashLabel(tag, "tag", pal::Text);
        }
        const std::string recovered = names::tagName(tag);
        if (!recovered.empty() && recovered != displayName(L, row)) caption(("Recovered name: " + recovered).c_str(), pal::Muted);
    }
    gap(sp::XS);
    caption(("Name evidence: " + nameEvidence(L, row)).c_str(), pal::Muted);
    if (!L.raw(row, Col::Package).empty()) caption(("Package: " + L.text(row, Col::Package)).c_str(), pal::Faint);
    gap(sp::M);
     
    labui::Flow flow;
    flow.next(buttonWidth("Copy Details", icon::Copy, true));
    if (button("Copy Details", {.icon = icon::Copy, .small = true})) copyToClipboard(detailsText(L, row), "details");
    if (tag) {
        flow.next(buttonWidth("Linked Resources", icon::Link, true));
        if (button("Linked Resources", {.icon = icon::Link, .small = true, .tooltip = "Search the library for this tag"})) searchFor(hx(tag));
        const char* label = nullptr;
        const char* tab = nullptr;
        const Page page = pageFor(L, row, &label, &tab);
        flow.next(buttonWidth(label, icon::OpenExternal, true));
        if (button(label, {.tone = Tone::Ghost, .icon = icon::OpenExternal, .small = true, .tooltip = "Copies the tag and opens the page"})) {
            copyToClipboard(hx(tag), "tag");
            openPage(page, tab);
            notify("Tag " + hx(tag) + " copied: paste it into that page's search", Level::Info);
        }
    }
    flow.next(buttonWidth("Name / Notes", icon::Tag, true));
    bool openEditor = false;
    {
        DisabledScope notesLocked(!tag && !lab::LibraryStore::get().notesEditable(),
                                  "lab_library_notes.txt could not be read: fix or remove it, then Reload");
        openEditor = button("Name / Notes", {.tone = Tone::Ghost, .icon = icon::Tag, .small = true,
                                             .tooltip = tag ? "Your name for this tag (shared with every page)" : "Your name and notes for this resource",
                                             .selected = s.editing});
    }
    if (openEditor) {
        s.editing = !s.editing;
        if (s.editing) {
            s.editName.clear();
            s.editNotes.clear();
            s.editVerified = false;
            if (tag) {
                if (const auto own = names::Labels::get().find(names::toPs3Tag(tag))) {
                    s.editName = own->name;
                    s.editNotes = own->notes;
                    s.editVerified = own->verified;
                }
            } else if (const auto n = lab::LibraryStore::get().note(key)) {
                s.editName = n->name;
                s.editNotes = n->notes;
            }
            s.editNotes.reserve(256);
        }
    }
    if (s.editing) {
        gap(sp::S);
        drawEditor(L, row);
    } else {
        std::string own;
        if (tag) {
            if (const auto l = names::Labels::get().find(names::toPs3Tag(tag)); l && !l->notes.empty()) own = l->notes;
        } else if (const auto n = lab::LibraryStore::get().note(key); n && !n->notes.empty()) {
            own = n->notes;
        }
        if (!own.empty()) {
            gap(sp::S);
            wrapped(("Your notes: " + own).c_str(), pal::Text, type::Small);
        }
    }
     
    const auto& refs = L.entityRefs(row);
    if (!refs.empty()) {
        gap(sp::M);
        fieldLabel(fmt("REFERENCED ENTITIES  (%zu)", refs.size()).c_str());
        tooltip("Authored squad variants; opening them does not create an equipped AI actor.");
        gap(sp::XS);
        for (uint32_t ref : refs) {
            ImGui::PushID(int(ref));
            hashLabel(ref, "entity tag", pal::Text);
            ImGui::SameLine(0, px(8));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(3));
            caption(names::tagLabel(ref).c_str(), pal::Muted);
            ImGui::SameLine();
            alignRight(buttonWidth("Squads", icon::Filter, true));
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(3));
            if (button("Squads", {.tone = Tone::Ghost, .icon = icon::Filter, .small = true, .tooltip = "Every squad that references this entity"})) {
                searchFor("");
                s.ref = ref;
            }
            ImGui::PopID();
        }
    }
    gap(sp::M);
    fieldLabel("DETAILS");
    gap(sp::XS);
    const std::string details = L.text(row, Col::Details);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, pal::Input);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(6));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(10), px(8)));
    ImGui::BeginChild("##details", ImVec2(0, std::max(px(120), ImGui::GetContentRegionAvail().y - px(4))), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    {
        FontScope f(FontKind::Mono, type::MonoSmall);
        ImGui::PushStyleColor(ImGuiCol_Text, pal::Soft);
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(details.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    ImGui::EndChild();
}

 

void drawLibrary(Ui&) {
    State& s = st();
    startLoad();
    const auto L = lib();
    if (!L) {
        std::string err;
        {
            std::lock_guard<std::mutex> l(s.m);
            err = s.loadError;
        }
        if (!err.empty()) emptyState(icon::Warning, "Systems Library Unavailable", err.c_str(), 220);
        else {
            gap(sp::L);
            caption("Loading 82,663 resources (decompressing the catalogue)...", pal::Muted);
            gap(sp::S);
            progressBar(-1, 0, 4);
            requestRedraw();
        }
        return;
    }
     
    if (!s.selectedKey.empty()) {
        if (const auto row = L->findKey(s.selectedKey)) {
            s.selected = int(*row);
            s.scrollTo = true;
        }
        s.selectedKey.clear();
    }
    const float avail = availWidth();
     
    ImGuiID searchId = 0;
    {
        const float availL = avail / scale();
        const bool oneLine = availL >= 760;
        const float searchL = oneLine ? std::min(420.0f, availL * 0.38f) : availL;
        searchField("##libsearch", &s.text, "Search Names, Tags, Aliases, Linked Assets...", searchL, true);
        ImGui::PushID("##libsearch");
        searchId = ImGui::GetID("##input");
        ImGui::PopID();
        const float rest = oneLine ? availL - searchL - 10 : availL;
        const float sysL = std::min(270.0f, (rest - 8) * 0.55f), kindL = std::min(240.0f, rest - 8 - sysL);
        if (oneLine) ImGui::SameLine(0, px(10));
        else gap(sp::S);
        std::vector<std::string> sysNames = {"All Systems"};
        for (const auto& sys : L->systems()) sysNames.push_back(sys + "  (" + labui::thousands(L->count(sys)) + ")");
        std::vector<const char*> sp1;
        for (const auto& n : sysNames) sp1.push_back(n.c_str());
        const int oldSystem = s.system;
        combo("##system", &s.system, sp1.data(), int(sp1.size()), sysL);
        tooltip("System");
        if (s.system != oldSystem) s.kind = 0;
        ImGui::SameLine(0, px(8));
        const std::string sysName = s.system > 0 ? L->systems()[size_t(s.system - 1)] : std::string();
        std::vector<std::string> kindNames = {"All Kinds"};
        for (const auto& k : L->kinds(sysName)) kindNames.push_back(k);
        std::vector<const char*> kp;
        for (const auto& n : kindNames) kp.push_back(n.c_str());
        if (s.kind >= int(kp.size())) s.kind = 0;
        combo("##kind", &s.kind, kp.data(), int(kp.size()), kindL);
        tooltip("Kind");
    }
    gap(sp::S);
    {
        std::vector<std::string> onlyTabs = {"Everything", "Saved", "Recent", "Named / Noted"};
        pillTabs("##only", onlyTabs, &s.only);
        if (s.ref) {
            ImGui::SameLine(0, px(10));
            if (button(("Squads Referencing " + hx(s.ref)).c_str(), {.tone = Tone::Ghost, .icon = icon::Close, .small = true, .tooltip = "Remove this filter",
                                                                      .selected = true}))
                s.ref = 0;
        }
        const bool any = !s.text.empty() || s.system || s.kind || s.only || s.ref;
        if (any) {
            ImGui::SameLine(0, px(10));
            if (button("Clear Filters", {.tone = Tone::Ghost, .icon = icon::Undo, .small = true})) searchFor("");
        }
    }
     
    const Query q = currentQuery(*L);
    if (!(q == s.wanted)) {
        s.wanted = q;
        s.wantedAt = labui::now();
    }
    if (!(s.wanted == s.applied) && (labui::demo() || labui::now() - s.wantedAt > 0.15 || s.wanted.text == s.applied.text)) {
        s.applied = s.wanted;
        startFilter(L, s.applied);
    } else if (s.viewGen == 0 && !s.filtering.load()) {
        startFilter(L, s.applied = q);
    }
    if (!(s.wanted == s.applied)) requestRedraw();
    std::vector<uint32_t> view;
    {
        std::lock_guard<std::mutex> l(s.m);
        view = s.view;
    }
    {
        const std::string count = s.filtering.load() ? std::string("Searching...") : (labui::thousands(view.size()) + " of " + labui::thousands(L->size()) + " resources");
        const ImVec2 ts = textSize(FontKind::Body, type::Small, count.c_str());
        const float copyW = buttonWidth("Copy Results", icon::Copy, true), reloadW = buttonWidth("Reload", icon::Refresh, true);
        ImGui::SameLine();
        alignRight(ts.x + px(12) + copyW + px(6) + reloadW);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(5));
        caption(count.c_str(), pal::Faint);
        ImGui::SameLine(0, px(12));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(5));
        const float buttonsY = ImGui::GetCursorPosY();
        {
            DisabledScope none(view.empty(), "Nothing to copy");
            if (button("Copy Results", {.tone = Tone::Ghost, .icon = icon::Copy, .small = true, .tooltip = "Copy the details of the first 1,000 results"})) {
                std::string text;
                for (size_t i = 0; i < view.size() && i < 1000; ++i) text += (i ? "\n\n" : "") + detailsText(*L, view[i]);
                copyToClipboard(text, "results");
            }
        }
        ImGui::SameLine(0, px(6));
        ImGui::SetCursorPosY(buttonsY);
        if (button("Reload", {.tone = Tone::Ghost, .icon = icon::Refresh, .small = true,
                              .tooltip = "Read the catalogue, your saved resources and notes again (after copying the Xbox control-favorites.txt / "
                                         "control-resource-notes.txt over as lab_library_saved.txt / lab_library_notes.txt)"}))
            reloadAll();
        if (s.filtering.load()) requestRedraw();
    }
     
    if (const std::string err = lab::LibraryStore::get().error(); !err.empty()) labui::errorBox(err);
    gap(sp::M);
     
    const bool searchActive = ImGui::GetActiveID() == searchId;
    if (searchActive && !view.empty()) {
        int pos = -1;
        for (int i = 0; i < int(view.size()); ++i)
            if (int(view[size_t(i)]) == s.selected) pos = i;
        int next = pos;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) next = std::min(int(view.size()) - 1, pos + 1);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) next = std::max(0, pos - 1);
        if (next != pos && next >= 0) select(*L, int(view[size_t(next)]));
    }
    const bool split = avail >= px(900);
    const float detailW = split ? std::clamp(avail * 0.40f, px(340), px(520)) : avail;
    const float tableW = split ? avail - detailW - px(16) : avail;
    const float tableH = std::clamp(ImGui::GetWindowHeight() / scale() - 250.0f, 400.0f, 860.0f);
    const bool compact = tableW < px(640);
    std::string toggleKey;
    ImGui::BeginGroup();
    if (beginTable("##lib", compact ? 3 : 5, tableH, ImGuiTableFlags_BordersOuter, tableW / scale())) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##fav", ImGuiTableColumnFlags_WidthFixed, px(16));
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        if (!compact) {
            ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, px(96));
            ImGui::TableSetupColumn("Name Evidence", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        }
        tableHeaders();
        ImGuiListClipper clip;
        clip.Begin(int(view.size()), px(kRowH));
        int scrollRow = -1;
        if (s.scrollTo) {
            for (int i = 0; i < int(view.size()); ++i)
                if (int(view[size_t(i)]) == s.selected) scrollRow = i;
            if (scrollRow >= 0) clip.IncludeItemByIndex(scrollRow);
            s.scrollTo = false;
        }
        const std::set<std::string> saved = lab::LibraryStore::get().savedKeys();
        while (clip.Step()) {
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                const uint32_t row = view[size_t(i)];
                const std::string key = L->key(row);
                const bool sel = int(row) == s.selected;
                ImGui::TableNextRow(0, px(kRowH));
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(int(row));
                const float y = ImGui::GetCursorPosY();
                if (tableRowSelectable("##r", sel, nullptr, kRowH - 2 * ImGui::GetStyle().CellPadding.y / scale())) select(*L, int(row));
                if (i == scrollRow) ImGui::SetScrollHereY(0.4f);
                ImGui::SetCursorPosY(y - px(3));
                const bool fav = saved.count(key) != 0;
                const ImVec2 sp0 = ImGui::GetCursorScreenPos() - ImVec2(px(4), 0);
                ImGui::SetCursorScreenPos(sp0);
                if (ImGui::InvisibleButton("##star", ImVec2(px(24), px(24)))) toggleKey = key;
                const bool sh = ImGui::IsItemHovered();
                const char* g = fav ? icon::StarFill : icon::Star;
                const ImVec2 is = textSize(FontKind::Icons, 12, g);
                drawText(ImGui::GetWindowDrawList(), FontKind::Icons, 12, sp0 + (ImVec2(px(24), px(24)) - is) * 0.5f,
                         u32(fav ? pal::Accent : sh ? pal::Text : pal::Decor), g);
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                cellText(displayName(*L, row).c_str(), sel ? pal::Accent : pal::Text, FontKind::Body, type::Label, kRowH);
                ImGui::TableSetColumnIndex(2);
                cellText(L->text(row, Col::Kind).c_str(), pal::Muted, FontKind::Body, type::Label, kRowH);
                if (!compact) {
                    ImGui::TableSetColumnIndex(3);
                    cellText(L->text(row, Col::Id).c_str(), pal::Muted, FontKind::Mono, type::MonoSmall, kRowH);
                    ImGui::TableSetColumnIndex(4);
                    cellText(nameEvidence(*L, row).c_str(), pal::Faint, FontKind::Body, type::Label, kRowH);
                }
            }
        }
        if (view.empty() && !s.filtering.load()) {
            ImGui::TableNextRow(0, px(110));
            ImGui::TableSetColumnIndex(1);
            gap(sp::XL);
            caption(s.only == 1 ? "No saved resources yet: click a star." : s.only == 2 ? "Nothing opened recently." : "No resources match.", pal::Muted);
        }
        endTable();
    }
    ImGui::EndGroup();
    if (!toggleKey.empty()) toggleSavedResource(toggleKey);
    if (split) ImGui::SameLine(0, px(16));
    else gap(sp::M);
    drawDetail(*L, detailW, split ? px(tableH) : px(520));
}

}   

BR_FEATURES(lab_library) {
    r.section({.id = "lab.library", .page = Page::Testing, .tab = "Systems library", .section = kSection, .order = 10, .draw = drawLibrary});
    ui::styleSection(kSection, {.fullWidth = true, .icon = ui::icon::Vault});
}

}   
