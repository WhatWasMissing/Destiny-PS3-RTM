 
 
 
 
 
 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <windows.h>
#include <shellapi.h>

#include "app/engine.h"
#include "app/registry.h"
#include "app/settings.h"
#include "core/error.h"
#include "core/runtime.h"
#include "features/cmd_ctx.h"
#include "game/loot_events.h"
#include "ui/widgets.h"

namespace brps3 {
namespace {

using namespace ui;
using loot_events::Entry;
using loot_events::Field;

constexpr const char* kFavKey = "loot_events.favorites";
constexpr const char* kSelectedKey = "loot_events.selected";    
constexpr const char* kCountKey = "loot_events.count";
constexpr const char* kReplayLoop = "Replay";                 
constexpr const char* kCallSelectedId = "loot_events.call_selected";
constexpr const char* kReplaySelectedId = "loot_events.replay_selected";
constexpr const char* kCallSelectedLabel = "Call Selected Loot Event";
constexpr const char* kReplaySelectedLabel = "Replay Selected Loot Event";
constexpr float kRowH = 30;

 
 
struct Armed {
    uint32_t hash = 0;
    std::vector<Field> fields;
    bool custom = false;                     
    int count = 1;
    int timeout = 60;
};

struct State {
     
    bool loaded = false;
    std::string loadError;
    const std::vector<Entry>* cat = nullptr;
    std::vector<std::string> hay;            
    std::vector<int> view;                   
    bool viewDirty = true;
    std::string filter;
    int kind = 0;                            
    std::unordered_set<uint32_t> favs;
    ImGuiTableColumnSortSpecs sortSpec{};
    bool sorted = false;
    int selected = -1;                       
    bool scrollToSelected = false;
    bool revealDetail = false;               
    bool lastSearchActive = false;
    float detailH = 0;                       
    float catalogTop = 0;                    
    bool revealCatalog = false;
    double flashUntil = 0;

     
    int fieldsFor = -1;
    std::vector<Field> fields;
    std::unordered_map<uint32_t, std::vector<Field>> edited;    
    std::string fieldsError;
    int count = 1;
    int replayTimeout = 60;

    uint32_t selector = 0xC541A0C3;

     
    std::atomic<bool> replayActive{false}, replayStop{false};

     
    std::mutex m;
    Armed armed;
    std::string progress, replayProgress, replayName;
    std::string lastResult;
    bool lastOk = true;
    uint32_t lastResultHash = 0;
    std::string gatherMessage;
    bool gatherOk = false;
};

State& st() {
    static State s;
    return s;
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string hex8(uint32_t v) {
    char b[12];
    snprintf(b, sizeof(b), "%08X", v);
    return b;
}

std::string nameOf(const Entry& e) { return e.label.empty() ? hex8(e.hash) : e.label; }

std::string shortName(const Entry& e) {
    std::string name = nameOf(e);
    if (name.size() > 28) name = name.substr(0, 26) + "..";
    return name;
}

std::vector<std::string> words(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : lower(text)) {
        if (c == ' ' || c == ',') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

int indexOf(uint32_t hash) {
    State& s = st();
    if (!s.cat) return -1;
    for (int i = 0; i < int(s.cat->size()); ++i)
        if ((*s.cat)[size_t(i)].hash == hash) return i;
    return -1;
}

bool ensureCatalog() {
    State& s = st();
    if (s.loaded) return s.cat != nullptr;
    s.loaded = true;
    Settings& settings = Engine::get().settings();
    try {
        s.cat = &loot_events::catalog();
        s.hay.reserve(s.cat->size());
        for (const Entry& e : *s.cat)
            s.hay.push_back(lower(hex8(e.hash) + " " + (e.schema ? hex8(e.schema) : std::string()) + " " + e.label));
        for (const std::string& h : settings.list(kFavKey)) {
            try {
                s.favs.insert(uint32_t(std::stoul(h, nullptr, 16)));
            } catch (...) {
            }
        }
         
        const std::string last = settings.str(kSelectedKey, "");
        if (!last.empty()) {
            try {
                s.selected = indexOf(uint32_t(std::stoul(last, nullptr, 16)));
                s.scrollToSelected = s.selected >= 0;
            } catch (...) {
            }
        }
        s.count = std::clamp(settings.integer(kCountKey, 1), 1, 10000);
    } catch (const std::exception& e) {
        s.cat = nullptr;
        s.loadError = e.what();
    }
     
    char* demo = nullptr;
    size_t n = 0;
    if (s.cat && _dupenv_s(&demo, &n, "BRPS3_UI_DEMO") == 0 && demo && demo[0] == '1') {
        s.selected = indexOf(0x7A0FD954);
        s.scrollToSelected = true;
        s.favs.insert(0x7A0FD954);
    }
    free(demo);
    return s.cat != nullptr;
}

void toggleFavourite(uint32_t hash) {
    State& s = st();
    if (s.favs.count(hash)) s.favs.erase(hash);
    else s.favs.insert(hash);
    Engine::get().settings().toggleInList(kFavKey, hex8(hash));
    if (s.kind == 1) s.viewDirty = true;
}

void rebuildView() {
    State& s = st();
    s.view.clear();
    if (!s.cat) return;
    const std::vector<std::string> w = words(s.filter);
    const auto& cat = *s.cat;
    for (int i = 0; i < int(cat.size()); ++i) {
        const Entry& e = cat[size_t(i)];
        if (s.kind == 1 && !s.favs.count(e.hash)) continue;
        if (s.kind == 2 && e.type == 0) continue;
        if (s.kind == 3 && e.type != 0) continue;
        bool ok = true;
        for (const std::string& word : w)
            if (s.hay[size_t(i)].find(word) == std::string::npos) {
                ok = false;
                break;
            }
        if (ok) s.view.push_back(i);
    }
    if (s.sorted) {
        const ImGuiID col = s.sortSpec.ColumnUserID;
        const bool asc = s.sortSpec.SortDirection != ImGuiSortDirection_Descending;
        std::stable_sort(s.view.begin(), s.view.end(), [&](int a, int b) {
            const Entry& x = cat[size_t(a)];
            const Entry& y = cat[size_t(b)];
            int c = 0;
            switch (col) {
            case 0: c = int(s.favs.count(y.hash)) - int(s.favs.count(x.hash)); break;
            case 1: c = x.hash < y.hash ? -1 : x.hash > y.hash ? 1 : 0; break;
            case 2: c = x.type - y.type; break;
            case 3: c = x.schema < y.schema ? -1 : x.schema > y.schema ? 1 : 0; break;
            case 4: c = int(x.size) - int(y.size); break;
            default: c = lower(x.label).compare(lower(y.label)); break;
            }
            return asc ? c < 0 : c > 0;
        });
    }
    s.viewDirty = false;
}

 
void select(int index) {
    State& s = st();
    if (s.selected == index) return;
    s.selected = index;
    s.scrollToSelected = true;
    if (s.cat && index >= 0) Engine::get().settings().set(std::string(kSelectedKey), hex8((*s.cat)[size_t(index)].hash));
}

std::vector<Field> presetsOf(const Entry& e, std::string* error = nullptr) {
    try {
        return loot_events::parseFields(e.presets);
    } catch (const std::exception& ex) {
        if (error) *error = ex.what();
        return {};
    }
}

void loadPresets(const Entry& e) {
    State& s = st();
    s.fieldsError.clear();
    s.fields = presetsOf(e, &s.fieldsError);
    s.edited.erase(e.hash);
}

bool parseProgress(const std::string& text, float* fraction) {
    const size_t slash = text.find('/');
    if (slash == std::string::npos || slash == 0) return false;
    size_t a = slash;
    while (a > 0 && std::isdigit(static_cast<unsigned char>(text[a - 1]))) --a;
    size_t b = slash + 1;
    while (b < text.size() && std::isdigit(static_cast<unsigned char>(text[b]))) ++b;
    if (a == slash || b == slash + 1) return false;
    const double done = std::stod(text.substr(a, slash - a));
    const double total = std::stod(text.substr(slash + 1, b - slash - 1));
    if (total <= 0) return false;
    *fraction = float(done / total);
    return true;
}

std::string callLabel(const Entry& e, int count) { return "Call " + shortName(e) + (count > 1 ? " x" + std::to_string(count) : ""); }

void setResult(uint32_t hash, bool ok, const std::string& text) {
    State& S = st();
    std::lock_guard<std::mutex> lock(S.m);
    S.lastOk = ok;
    S.lastResult = text;
    S.lastResultHash = hash;
}

 
 
 

void runCall(Ctx& c, const Entry& e, const std::vector<Field>& fields, int count) {
    State& S = st();
    {
        std::lock_guard<std::mutex> lock(S.m);
        S.progress.clear();
    }
     
     
     
    const int n = loot_events::call(
        c.rt(), e, fields, count,
        [&c, &S](const std::string& t) {
            c.log("[call] " + t);
            std::lock_guard<std::mutex> lock(S.m);
            S.progress = t;
        },
        [&c] { return c.cancelled(); });
    const std::string summary = std::to_string(n) + "/" + std::to_string(count) + " emitted";
    c.toast("Called " + nameOf(e) + ": " + summary);
    setResult(e.hash, n == count, summary);
}

 
 
void startReplay(Ctx& c, const Entry& e, int count, double timeout) {
    State& S = st();
    if (S.replayActive.exchange(true)) throw Error("a replay is already armed: stop it first");
    S.replayStop = false;
    {
        std::lock_guard<std::mutex> lock(S.m);
        S.replayProgress.clear();
        S.replayName = shortName(e);
    }
    trackActivity("loot_events.replay", "Replay armed: " + shortName(e), Page::LootEvents, [] { return st().replayActive.load(); },
                  [] { st().replayStop = true; }, pal::Accent);
    c.startLoop(kReplayLoop, 1, [e, count, timeout](Ctx& lc) {
        State& T = st();
        struct Done {
            ~Done() { st().replayActive = false; }
        } done;
        try {
            const int n = loot_events::replay(
                lc.rt(), e, count, timeout,
                [&lc, &T](const std::string& t) {
                    lc.log("[replay] " + t);
                    std::lock_guard<std::mutex> lock(T.m);
                    T.replayProgress = t;
                },
                [&lc, &T] { return lc.cancelled() || T.replayStop.load(); });
            lc.toast("Replayed " + nameOf(e) + ": " + std::to_string(n) + "/" + std::to_string(count));
            setResult(e.hash, n > 0, "Replayed " + std::to_string(n) + "/" + std::to_string(count));
        } catch (const std::exception& ex) {
            setResult(e.hash, false, ex.what());
            throw;    
        }
        lc.stopLoop(kReplayLoop);    
    });
}

 
 
Armed armedSnapshot(Settings& settings) {
    State& S = st();
    Armed a;
    {
        std::lock_guard<std::mutex> lock(S.m);
        a = S.armed;
    }
    if (a.hash == 0) {
        try {
            const std::string last = settings.str(kSelectedKey, "");
            if (!last.empty()) a.hash = uint32_t(std::stoul(last, nullptr, 16));
        } catch (...) {
        }
        a.count = std::clamp(settings.integer(kCountKey, 1), 1, 10000);
    }
    return a;
}

void callSelected(Ctx& c) {
    const Armed a = armedSnapshot(c.settings());
    const Entry* e = a.hash ? loot_events::find(a.hash) : nullptr;
    if (!e) throw Error("select a loot event on the Loot events page first");
    runCall(c, *e, a.custom ? a.fields : presetsOf(*e), a.count);
}

void replaySelected(Ctx& c) {
    const Armed a = armedSnapshot(c.settings());
    const Entry* e = a.hash ? loot_events::find(a.hash) : nullptr;
    if (!e) throw Error("select a loot event on the Loot events page first");
    startReplay(c, *e, a.count, double(a.timeout));
}

 
 
 

void postCall(const Entry& e, const std::vector<Field>& fields, int count) {
    if (!Engine::get().connected()) {
        notify(std::string(kConnectFirst) + " to call " + shortName(e), Level::Warn);
        return;
    }
    const std::string job = callLabel(e, count);
    noteQueued(job);
    const Entry copy = e;
    Engine::get().post(job, [copy, fields, count](Ctx& c) { runCall(c, copy, fields, count); },
                       [hash = e.hash](bool ok, const std::string& err) {
                           if (ok) flashSuccess("loot_events.call");
                           else setResult(hash, false, err);
                       });
}

void postReplay(const Entry& e, int count, int timeout) {
    if (!Engine::get().connected()) {
        notify(std::string(kConnectFirst) + " to replay " + shortName(e), Level::Warn);
        return;
    }
    State& s = st();
    {
        std::lock_guard<std::mutex> lock(s.m);
        s.replayProgress.clear();
    }
    const std::string job = "Arm replay " + shortName(e);
    noteQueued(job);
    const Entry copy = e;
    Engine::get().post(job, [copy, count, timeout](Ctx& c) { startReplay(c, copy, count, double(timeout)); },
                       [hash = e.hash](bool ok, const std::string& err) {
                           if (!ok) setResult(hash, false, err);
                       });
}

 
 
 

void drawFieldsEditor(const Entry& e) {
    State& s = st();
    if (e.type == 0) {
        caption("Basic signal: emitted with the hash alone (24-byte payload, no fields).", pal::Muted);
        return;
    }
    fieldLabel("PAYLOAD FIELDS");
    gap(sp::XS + 2);
    if (!s.fieldsError.empty()) {
        caption(("Catalog presets unreadable: " + s.fieldsError).c_str(), pal::Warning);
        gap(sp::XS);
    }
    if (s.fields.empty()) caption("No fields: the payload is the schema's zero-initialised default.", pal::Faint);
    else {
         
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* d = ImGui::GetWindowDrawList();
        drawText(d, FontKind::Body, type::Tiny, p, u32(pal::Faint), "Offset");
        drawText(d, FontKind::Body, type::Tiny, ImVec2(p.x + px(64 + 6), p.y), u32(pal::Faint), "Width");
        drawText(d, FontKind::Body, type::Tiny, ImVec2(p.x + px(64 + 6 + 96 + 6), p.y), u32(pal::Faint), "Value (Hex)");
        ImGui::Dummy(ImVec2(1, textSize(FontKind::Body, type::Tiny, "O").y));
    }
    int remove = -1;
    bool changed = false;
    static const char* const widths[] = {"1 Byte", "2 Bytes", "4 Bytes"};
    for (size_t i = 0; i < s.fields.size(); ++i) {
        Field& f = s.fields[i];
        ImGui::PushID(int(i));
        uint32_t off = f.offset;
        if (inputHex("##off", &off, 64, 2)) {
            f.offset = std::min<uint32_t>(off, e.size ? e.size - 1 : 0xFFFF);
            changed = true;
        }
        tooltip("Offset in the payload (hex)");
        ImGui::SameLine(0, px(6));
        int w = f.width == 1 ? 0 : f.width == 2 ? 1 : 2;
        if (combo("##w", &w, widths, 3, 96)) {
            f.width = w == 0 ? 1 : w == 1 ? 2 : 4;
            changed = true;
        }
        ImGui::SameLine(0, px(6));
        uint32_t val = f.value;
         
        const float valW = std::clamp((availWidth() - px(4) - px(kControlH)) / scale() - 2.0f, 90.0f, 140.0f);
        if (inputHex("##val", &val, valW, f.width * 2)) {
            const uint32_t mask = f.width == 1 ? 0xFFu : f.width == 2 ? 0xFFFFu : 0xFFFFFFFFu;
            f.value = val & mask;
            changed = true;
        }
        tooltip("Value (Hex)");
        ImGui::SameLine(0, px(4));
        if (iconButton("##rm", icon::Close, "Remove field", false, kControlH)) remove = int(i);
        ImGui::PopID();
    }
    if (remove >= 0) {
        s.fields.erase(s.fields.begin() + remove);
        changed = true;
    }
    gap(sp::XS);
    if (button("Add Field", {.tone = Tone::Ghost, .icon = icon::Add, .small = true})) {
        const uint32_t next = s.fields.empty() ? 0x18 : s.fields.back().offset + uint32_t(s.fields.back().width);
        s.fields.push_back({next, 4, 0});
        changed = true;
    }
    ImGui::SameLine(0, px(6));
    if (button("Presets", {.tone = Tone::Ghost, .icon = icon::Undo, .small = true, .tooltip = "Reset to the catalog presets"})) loadPresets(e);
    ImGui::SameLine(0, px(6));
    if (button("Clear", {.tone = Tone::Ghost, .icon = icon::Delete, .small = true, .tooltip = "Emit with no fields"})) {
        s.fields.clear();
        changed = true;
    }
    if (changed) s.edited[e.hash] = s.fields;    
    if (!s.fields.empty()) {
        gap(sp::S);
        caption("Payload Spec", pal::Faint);
        copyable(loot_events::formatFields(s.fields).c_str(), "payload spec", pal::Muted);
    }
}

void drawDetail(Ui& ui, float width, float height) {
    State& s = st();
    const bool child = height > 0;
    if (child) {
         
         
        ImGui::PushStyleColor(ImGuiCol_ChildBg, pal::Raised);
        ImGui::PushStyleColor(ImGuiCol_Border, pal::Border);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, px(kRowRounding));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(16), px(14)));
        ImGui::SetNextWindowSizeConstraints(ImVec2(width, height), ImVec2(width, FLT_MAX));
        ImGui::BeginChild("##detail", ImVec2(width, 0),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AutoResizeY);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
    } else {
        beginCard("##detailcard", nullptr, {.raised = true});
    }
    if (s.selected < 0 || !s.cat) {
        emptyState(icon::LootEvents, "Select a loot event",
                   "Pick a Row to See Its Schema, Edit the Payload and Call or Replay It. Press / to Search, Then the Arrow Keys to Browse.",
                   child ? std::max(200.0f, height / scale() - 40) : 200);
    } else {
        const Entry& e = (*s.cat)[size_t(s.selected)];
        if (s.fieldsFor != s.selected) {
            s.fieldsFor = s.selected;
            s.fieldsError.clear();
            auto it = s.edited.find(e.hash);
            s.fields = it != s.edited.end() ? it->second : presetsOf(e, &s.fieldsError);
        }
         
        {
            std::lock_guard<std::mutex> lock(s.m);
            s.armed.hash = e.hash;
            s.armed.fields = s.fields;
            s.armed.custom = s.edited.count(e.hash) != 0;
            s.armed.count = s.count;
            s.armed.timeout = s.replayTimeout;
        }
         
        {
            const float starW = px(30);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + availWidth() - starW - px(8));
            heading(e.label.empty() ? "(unnamed loot event)" : e.label.c_str());
            ImGui::PopTextWrapPos();
            const ImVec2 after = ImGui::GetCursorScreenPos();
            ImGui::SameLine();
            alignRight(starW);
            const bool fav = s.favs.count(e.hash) != 0;
            if (iconButton("##fav", fav ? icon::StarFill : icon::Star, fav ? "Remove from favourites" : "Add to favourites (they show first in Ctrl+K)",
                           false, 30, fav ? pal::Accent : pal::Muted))
                toggleFavourite(e.hash);
            ImGui::SetCursorScreenPos(after);
        }
        gap(sp::XS);
        hashLabel(e.hash, "hash", pal::Accent);
        ImGui::SameLine(0, px(10));
        pill(e.type == 0 ? "Basic" : ("Type " + std::to_string(e.type)).c_str(), e.type == 0 ? pal::Muted : pal::Info);
        ImGui::SameLine(0, px(6));
        pill((std::to_string(e.size) + " bytes").c_str(), pal::Muted);
        if (e.schema) {
            gap(sp::XS);
            caption("Schema", pal::Faint);
            ImGui::SameLine(0, px(8));
            hashLabel(e.schema, "schema", pal::Muted);
        }
        divider(sp::L);
        drawFieldsEditor(e);
        divider(sp::L);

         
        fieldLabel("COUNT");
        gap(sp::XS + 2);
        const int oldCount = s.count;
        inputInt("##count", &s.count, 1, 10000, 150);
        tooltip("How many times to emit (one per game frame, about 30 per second)");
        for (int q : {1, 10, 100}) {
            ImGui::SameLine(0, px(6));
            const std::string l = std::to_string(q) + "##q";
            if (button(l.c_str(), {.tone = Tone::Ghost, .small = true, .selected = s.count == q})) s.count = q;
        }
        if (s.count != oldCount) Engine::get().settings().set(std::string(kCountKey), s.count);
        gap(sp::M);

         
        const std::string callJob = callLabel(e, s.count);
        const bool calling = ui.busy(callJob) || ui.busy(kCallSelectedLabel);
        const bool replaying = s.replayActive.load() || ui.busy("Arm replay " + shortName(e)) || ui.busy(kReplaySelectedLabel);
        const float menuW = px(kControlH);
        const float half = (availWidth() - px(8) * 2 - menuW) * 0.5f;
        {
            auto offline = requireConnection(ui);
            if (button("Call", {.tone = Tone::Primary, .icon = icon::Send, .width = half / scale(),
                                .tooltip = "Emit with the fields above (Ctrl+Enter)", .busy = calling, .successKey = "loot_events.call"}))
                postCall(e, s.fields, s.count);
            ImGui::SameLine(0, px(8));
            if (s.replayActive.load()) {
                if (button("Stop Replay", {.tone = Tone::Danger, .icon = icon::Stop, .width = half / scale(), .tooltip = "Disarm the replay hook",
                                           .spinner = true}))
                    s.replayStop = true;
            } else if (button("Replay", {.icon = icon::Repeat, .width = half / scale(),
                                         .tooltip = "Wait for the next natural event with the same schema and emit this hash with its live payload. "
                                                    "Other switches and calls keep working while it waits.",
                                         .busy = replaying})) {
                postReplay(e, s.count, s.replayTimeout);
            }
        }
        ImGui::SameLine(0, px(8));
        if (iconButton("##actionsmenu", icon::More, "Pin \"Call Selected Loot Event\" to Home or give it a hotkey", false, kControlH))
            openItemMenu(kCallSelectedId);
        gap(sp::S);
        {
             
            FontScope f(FontKind::Body, type::Small);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(9));
            label("Replay Timeout", pal::Muted);
        }
        ImGui::SameLine(0, px(8));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - px(9));
        inputInt("##timeout", &s.replayTimeout, 5, 600, 130, 5);
        tooltip("Seconds to wait for a natural event with the same schema");
        ImGui::SameLine(0, px(8));
        {
            FontScope f(FontKind::Body, type::Small);
            label("Seconds", pal::Muted);    
        }

        std::string callProgress, replayProgress, result;
        bool ok;
        uint32_t resultHash;
        {
            std::lock_guard<std::mutex> lock(s.m);
            callProgress = s.progress;
            replayProgress = s.replayProgress;
            result = s.lastResult;
            ok = s.lastOk;
            resultHash = s.lastResultHash;
        }
        if (calling || replaying) {
            const std::string& p = calling ? callProgress : replayProgress;
            gap(sp::M);
            float f = -1;
            if (!parseProgress(p, &f)) f = -1;
            progressBar(f, 0, 5);
            gap(sp::XS);
            caption(p.empty() ? (calling ? "Starting..." : "Arming...") : p.c_str(), pal::Muted);
            if (calling) {
                ImGui::SameLine();
                alignRight(buttonWidth("Stop", icon::Stop, true));
                if (button("Stop##job", {.tone = Tone::Danger, .icon = icon::Stop, .small = true})) Engine::get().cancel();
            }
        } else if (!result.empty() && resultHash == e.hash) {
            gap(sp::M);
            pill(result.c_str(), ok ? pal::Success : pal::Danger, ok ? icon::Check : icon::Warning);
        }
    }
    if (child) {
        gap(sp::XS);
        s.detailH = ImGui::GetCursorPosY() + px(14);    
        ImGui::EndChild();
    } else {
        endCard();
    }
}

 
void catalogHeaders() {
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers, px(30));
    const int n = ImGui::TableGetColumnCount();
    FontScope f(FontKind::Semibold, type::Small - 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, pal::Muted);
    for (int c = 0; c < n; ++c) {
        if (!ImGui::TableSetColumnIndex(c)) continue;
        const char* name = ImGui::TableGetColumnName(c);
        ImGui::TableHeader(name);
        if (c == 0) {
             
            const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
            const ImVec2 is = textSize(FontKind::Icons, 11, icon::Star);
            const float cx = ImGui::TableGetCellBgRect(ImGui::GetCurrentTable(), 0).Min.x + ImGui::GetStyle().CellPadding.x + px(8);
            drawText(ImGui::GetWindowDrawList(), FontKind::Icons, 11, ImVec2(cx - is.x * 0.5f, (mn.y + mx.y - is.y) * 0.5f), u32(pal::Muted), icon::Star);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("Sort favourites first");
        }
    }
    ImGui::PopStyleColor();
}

void drawCatalog(Ui& ui) {
    State& s = st();
    if (!ensureCatalog()) {
        emptyState(icon::Warning, "Loot Event Catalog Unavailable", s.loadError.c_str(), 220);
        return;
    }
     
    s.catalogTop = ImGui::GetCursorScreenPos().y - ImGui::GetWindowPos().y + ImGui::GetScrollY();
    if (s.revealCatalog) {
        ImGui::SetScrollY(std::max(0.0f, s.catalogTop - px(96)));
        s.revealCatalog = false;
    }
    ImGuiIO& io = ImGui::GetIO();
     
    const float avail = availWidth();
    ImGuiID searchInput = 0;
    {
        const float searchW = std::min(px(360), avail * 0.42f);
        if (searchField("##search", &s.filter, "Search Hash, Schema or Name", searchW / scale(), true)) s.viewDirty = true;
        ImGui::PushID("##search");
        searchInput = ImGui::GetID("##input");
        ImGui::PopID();
        ImGui::SameLine(0, px(12));
        std::vector<std::string> kinds = {"All", s.favs.empty() ? std::string("Favourites") : "Favourites  " + std::to_string(s.favs.size()), "Typed",
                                          "Basic"};
        if (pillTabs("##kind", kinds, &s.kind)) s.viewDirty = true;
        if (s.viewDirty) rebuildView();
        char buf[64];
        snprintf(buf, sizeof(buf), "%zu of %zu", s.view.size(), s.cat->size());
        const ImVec2 ts = textSize(FontKind::Body, type::Small, buf);
        ImGui::SameLine();
        alignRight(ts.x);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(8));
        caption(buf, pal::Faint);
    }
    gap(sp::M);

     
    const bool split = avail >= px(760);
    const float detailW = split ? std::clamp(avail * 0.40f, px(300), px(460)) : avail;
    const float tableW = split ? avail - detailW - px(16) : avail;
    const bool compactCols = tableW < px(520);    
    const float viewH = ImGui::GetWindowHeight();
    const float baseH = std::clamp(viewH - px(250), px(360), px(820));
    const float tableH = split ? std::max(baseH, std::min(s.detailH, px(900))) : baseH;    

     
     
    const ImVec2 tablePos = ImGui::GetCursorScreenPos();
    const bool tableHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseHoveringRect(tablePos, tablePos + ImVec2(tableW, tableH));
    const bool searchActive = ImGui::GetActiveID() == searchInput;
    const bool popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    if (!popup && !s.view.empty() && (searchActive || (tableHovered && !io.WantTextInput))) {
        int pos = -1;
        for (int i = 0; i < int(s.view.size()); ++i)
            if (s.view[size_t(i)] == s.selected) pos = i;
        const int last = int(s.view.size()) - 1;
        const int page = std::max(1, int(tableH / px(kRowH)) - 2);
        int next = pos;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) next = std::min(last, pos + 1);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) next = std::max(0, pos - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) next = std::min(last, std::max(0, pos) + page);
        if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) next = std::max(0, pos - page);
        if (!searchActive && ImGui::IsKeyPressed(ImGuiKey_Home, false)) next = 0;
        if (!searchActive && ImGui::IsKeyPressed(ImGuiKey_End, false)) next = last;
        if (next != pos && next >= 0) select(s.view[size_t(next)]);
    }
     
    if (s.lastSearchActive && !searchActive && !s.view.empty() &&
        (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) &&
        std::find(s.view.begin(), s.view.end(), s.selected) == s.view.end())
        select(s.view.front());
    s.lastSearchActive = searchActive;
     
    if (!popup && s.selected >= 0 && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Enter) && (searchActive || !io.WantTextInput) &&
        !ui.busy(callLabel((*s.cat)[size_t(s.selected)], s.count)))
        postCall((*s.cat)[size_t(s.selected)], s.fields, s.count);

    ImGui::BeginGroup();
    if (beginTable("##catalog", compactCols ? 4 : 6, tableH / scale(),
                   ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate | ImGuiTableFlags_BordersOuter, tableW / scale())) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("##fav", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, px(16), 0);
        ImGui::TableSetupColumn("Hash", ImGuiTableColumnFlags_WidthFixed, px(84), 1);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, px(44), 2);
        if (!compactCols) {
            ImGui::TableSetupColumn("Schema", ImGuiTableColumnFlags_WidthFixed, px(84), 3);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, px(40), 4);
        }
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0, 5);
        catalogHeaders();
        if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs(); specs && specs->SpecsDirty) {
            s.sorted = specs->SpecsCount > 0;
            if (s.sorted) s.sortSpec = specs->Specs[0];
            specs->SpecsDirty = false;
            rebuildView();
        }
        const int nameCol = compactCols ? 3 : 5;
        ImGuiListClipper clip;
        clip.Begin(int(s.view.size()), px(kRowH));
        int scrollTo = -1;
        if (s.scrollToSelected) {
            for (int i = 0; i < int(s.view.size()); ++i)
                if (s.view[size_t(i)] == s.selected) scrollTo = i;
            if (scrollTo >= 0) clip.IncludeItemByIndex(scrollTo);
            s.scrollToSelected = false;
        }
        int toggleFav = -1;
        ImGui::PushFont(font(FontKind::Body), fontSize(FontKind::Body, type::Label));
        while (clip.Step()) {
            for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
                const int idx = s.view[size_t(r)];
                const Entry& e = (*s.cat)[size_t(idx)];
                ImGui::TableNextRow(0, px(kRowH));
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(idx);
                const float cellY = ImGui::GetCursorPosY();
                if (tableRowSelectable("##row", s.selected == idx, nullptr, kRowH - 2 * ImGui::GetStyle().CellPadding.y / scale())) {
                    select(idx);
                    s.revealDetail = true;    
                }
                if (r == scrollTo) ImGui::SetScrollHereY(0.4f);
                 
                ImGui::SetCursorPosY(cellY - px(3));
                const bool fav = s.favs.count(e.hash) != 0;
                const ImVec2 sp = ImGui::GetCursorScreenPos() - ImVec2(px(4), 0);
                ImGui::SetCursorScreenPos(sp);
                if (ImGui::InvisibleButton("##star", ImVec2(px(24), px(24)))) toggleFav = int(e.hash);
                const bool sh = ImGui::IsItemHovered();
                const char* g = fav ? icon::StarFill : icon::Star;
                const ImVec2 is = textSize(FontKind::Icons, 12, g);
                drawText(ImGui::GetWindowDrawList(), FontKind::Icons, 12, sp + (ImVec2(px(24), px(24)) - is) * 0.5f,
                         u32(fav ? pal::Accent : sh ? pal::Text : pal::Decor), g);
                if (sh) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip(fav ? "Remove from favourites" : "Add to favourites");
                }
                ImGui::PopID();
                char buf[16];
                const bool sel = s.selected == idx;
                ImGui::TableSetColumnIndex(1);
                snprintf(buf, sizeof(buf), "%08X", e.hash);
                cellText(buf, sel ? pal::Accent : pal::Text, FontKind::Mono, type::MonoSmall, kRowH);
                ImGui::TableSetColumnIndex(2);
                cellText(e.type == 0 ? "basic" : std::to_string(e.type).c_str(), e.type == 0 ? pal::Faint : pal::Muted, FontKind::Body, type::Label, kRowH);
                if (!compactCols) {
                    ImGui::TableSetColumnIndex(3);
                    snprintf(buf, sizeof(buf), "%08X", e.schema);
                    cellText(e.schema ? buf : "-", e.schema ? pal::Muted : pal::Faint, FontKind::Mono, type::MonoSmall, kRowH);
                    ImGui::TableSetColumnIndex(4);
                    cellText(std::to_string(e.size).c_str(), pal::Muted, FontKind::Body, type::Label, kRowH);
                }
                ImGui::TableSetColumnIndex(nameCol);
                cellText(e.label.c_str(), sel ? pal::Text : pal::Soft, FontKind::Body, type::Label, kRowH);
            }
        }
        ImGui::PopFont();
        if (s.view.empty()) {
            ImGui::TableNextRow(0, px(120));
            ImGui::TableSetColumnIndex(nameCol);
            gap(sp::XL);
            caption(s.kind == 1 && s.favs.empty() ? "No favourites yet: click the star next to a loot event." : "No loot events match.", pal::Muted);
        }
        endTable();
        if (toggleFav >= 0) toggleFavourite(uint32_t(toggleFav));
    }
    ImGui::EndGroup();
     
    if (ImGui::GetTime() < s.flashUntil) {
        const float t = std::clamp(float(s.flashUntil - ImGui::GetTime()) / 0.8f, 0.0f, 1.0f);
        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin() - ImVec2(px(2), px(2)), ImGui::GetItemRectMax() + ImVec2(px(2), px(2)),
                                            u32(pal::Accent, 0.8f * t), px(kRowRounding + 2), 0, px(1.5f));
        requestRedraw();
    }
    if (split) {
        ImGui::SameLine(0, px(16));
        drawDetail(ui, detailW, tableH);
    } else {
        gap(sp::M);
        if (s.revealDetail) ImGui::SetScrollHereY(0.0f);    
        drawDetail(ui, detailW, 0);
    }
    s.revealDetail = false;
}

 
 
 

struct GatherPreset {
    const char* name;
    uint32_t selector;
};
const GatherPreset kGather[] = {
    {"Epiphany Chest 9", 0xC541A0C3},
};

void drawGather(Ui& ui) {
    State& s = st();
    fieldLabel("SELECTOR");
    gap(sp::XS + 2);
    inputHex("##selector", &s.selector, 120);
    for (const GatherPreset& p : kGather) {
        ImGui::SameLine(0, px(8));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(3));
        if (button(p.name, {.tone = Tone::Ghost, .small = true, .tooltip = "Use this selector", .selected = s.selector == p.selector}))
            s.selector = p.selector;
    }
    gap(sp::M);
    const bool busy = ui.busy("Gather");
    {
        auto offline = requireConnection(ui);
        if (button("Gather", {.tone = Tone::Primary, .icon = icon::Play, .busy = busy})) {
            const uint32_t sel = s.selector;
            noteQueued("Gather");
            ui.post("Gather", [sel](Ctx& c) {
                std::string msg;
                const bool ok = loot_events::gather(c.rt(), sel, msg);
                c.log("[gather] " + hex8(sel) + ": " + msg);
                State& S = st();
                std::lock_guard<std::mutex> lock(S.m);
                S.gatherOk = ok;
                S.gatherMessage = msg.empty() ? (ok ? std::string("emitted") : std::string("not emitted")) : msg;
            });
        }
    }
    std::string msg;
    bool ok;
    {
        std::lock_guard<std::mutex> lock(s.m);
        msg = s.gatherMessage;
        ok = s.gatherOk;
    }
    if (!msg.empty()) {
        ImGui::SameLine(0, px(12));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + px(5));
        pill(msg.c_str(), ok ? pal::Success : pal::Warning, ok ? icon::Check : icon::Info);
    }
}

void resetOnDisconnect() {
    State& s = st();
     
    s.replayActive = false;
    std::lock_guard<std::mutex> lock(s.m);
    s.progress.clear();
    s.replayProgress.clear();
}

 
 
 

void paletteEntries(const std::string& query, std::vector<PaletteEntry>& out) {
    if (!ensureCatalog()) return;
    State& s = st();
    auto add = [&](int idx, bool fav) {
        const Entry& e = (*s.cat)[size_t(idx)];
        PaletteEntry pe;
        pe.label = e.label.empty() ? "(unnamed loot event)" : e.label;
        pe.where = "Loot event  \xE2\x80\xBA  " + hex8(e.hash) + (fav ? "  \xC2\xB7  favourite" : "");
        pe.icon = fav ? icon::StarFill : icon::LootEvents;
        pe.badge = e.type == 0 ? "basic" : "type " + std::to_string(e.type);
        pe.boost = fav ? 300 : 0;
        pe.run = [idx] {
            State& T = st();
            if (T.kind != 0 || !T.filter.empty()) {
                T.kind = 0;
                T.filter.clear();
                T.viewDirty = true;
            }
            select(idx);
            T.revealCatalog = true;
            T.flashUntil = ImGui::GetTime() + 1.2;
            openPage(Page::LootEvents);
        };
        pe.alt = [idx] {
            State& T = st();
            const Entry& entry = (*T.cat)[size_t(idx)];
            auto it = T.edited.find(entry.hash);
            postCall(entry, it != T.edited.end() ? it->second : presetsOf(entry), T.count);
        };
        pe.altHint = "Call";
        out.push_back(std::move(pe));
    };
    for (uint32_t h : s.favs)
        if (const int i = indexOf(h); i >= 0) add(i, true);
    if (query.size() < 2) return;
    const std::vector<std::string> w = words(query);
    int n = 0;
    for (int i = 0; i < int(s.cat->size()) && n < 40; ++i) {
        if (s.favs.count((*s.cat)[size_t(i)].hash)) continue;
        bool ok = true;
        for (const std::string& word : w)
            if (s.hay[size_t(i)].find(word) == std::string::npos) {
                ok = false;
                break;
            }
        if (ok) {
            add(i, false);
            ++n;
        }
    }
}

}   

BR_FEATURES(loot_events_page) {
    r.section({.id = "loot_events.catalog", .page = Page::LootEvents, .section = "Loot event catalog", .order = 10, .draw = drawCatalog,
               .onDisconnect = resetOnDisconnect});
    r.section({.id = "loot_events.gather", .page = Page::LootEvents, .section = "Native gather", .order = 30, .draw = drawGather});
     
    r.action({.id = kCallSelectedId, .page = Page::LootEvents, .section = "Loot event catalog", .label = kCallSelectedLabel,
              .help = "Emit the loot event selected on the Loot events page, with its payload fields and count.", .run = callSelected});
    r.action({.id = kReplaySelectedId, .page = Page::LootEvents, .section = "Loot event catalog", .label = kReplaySelectedLabel,
              .help = "Arm a replay of the selected loot event: on the next natural event with its schema, emit it with that live payload.",
              .run = replaySelected});
    ui::styleSection("Loot event catalog", {.fullWidth = true, .icon = ui::icon::LootEvents, .ownsActions = true});
    ui::styleSection("Native gather", {.icon = ui::icon::Loot});
    ui::addPaletteProvider(paletteEntries);
}

}   
