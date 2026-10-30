 
 
 
 
 
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>

#include "ui/shell_internal.h"

namespace brps3::ui {

namespace {

std::string g_query;
bool g_open = false;
bool g_focus = false;
ImRect g_box;                  
bool g_boxActive = false;

std::string lower(const std::string& s) {
    std::string r(s);
    for (char& c : r) c = char(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

std::vector<std::string> words(const std::string& q) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : lower(q)) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool matchesAll(const std::vector<std::string>& terms, const std::string& textLower) {
    for (const std::string& t : terms)
        if (textLower.find(t) == std::string::npos) return false;
    return true;
}

void collectLabels(const Item& it, std::string& out, int depth = 0) {
    out += ' ';
    out += it.label;
    if (depth > kMaxChildDepth) return;
    for (const Item* k : childrenOf(it.id)) collectLabels(*k, out, depth + 1);
}

struct Hit {
    const Item* root;
    int score;
};

struct Extra {
    std::string label, where;
    std::function<void()> run;
};

int tabCount(Page p) { return int(Registry::get().tabs(p).size()); }

std::string headerPath(const Item& it) {
    std::string path;
    if (!it.tab.empty() && tabCount(it.page) > 1) path += " / " + titleCase(it.tab);
    if (it.kind != ItemKind::Section && !it.section.empty() && lower(it.section) != lower(it.tab) && lower(it.section) != lower(pageLabel(int(it.page))))
        path += " / " + titleCase(it.section);
    return path;
}

std::vector<Hit> findHits(const std::vector<std::string>& terms) {
    std::vector<Hit> hits;
    if (terms.empty()) return hits;
    for (const Item& it : items()) {
        if (!it.parent.empty()) continue;
        std::string text;
        collectLabels(it, text);
        const std::string labelLower = lower(it.label);
        text = lower(text + " " + pageLabel(int(it.page)) + " " + it.tab + " " + it.section);
        if (!matchesAll(terms, text)) continue;
        int score = 0;
        if (labelLower.rfind(terms.front(), 0) == 0) score = 3;
        else if (matchesAll(terms, labelLower)) score = 2;
        else if (matchesAll(terms, lower(it.label + " " + it.section))) score = 1;
        hits.push_back({&it, score});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.score > b.score; });
    if (hits.size() > 40) hits.resize(40);
    return hits;
}

std::vector<Extra> findExtras(const std::vector<std::string>& terms, const std::string& queryLower) {
    std::vector<Extra> out;
    if (terms.empty()) return out;
     
    for (int p = 0; p < kPageCount; ++p) {
        if (!pageReady(p)) continue;
        if (matchesAll(terms, lower(pageLabel(p)))) out.push_back({pageLabel(p), "Page", [p] { navigate(p); }});
        if (p >= int(Page::Count)) continue;
        const auto tabs = Registry::get().tabs(Page(p));
        if (tabs.size() < 2) continue;
        for (const std::string& tab : tabs)
            if (matchesAll(terms, lower(tab + " " + pageLabel(p)))) out.push_back({titleCase(tab), pageLabel(p), [p, tab] { navigate(p, tab); }});
    }
     
    std::vector<PaletteEntry> provided;
    for (const PaletteProvider& pr : detail::paletteProviders()) pr(queryLower, provided);
    std::stable_sort(provided.begin(), provided.end(), [](const PaletteEntry& a, const PaletteEntry& b) { return a.boost > b.boost; });
    for (PaletteEntry& pe : provided) {
        if (!matchesAll(terms, lower(pe.label + " " + pe.where))) continue;
        out.push_back({std::move(pe.label), std::move(pe.where), std::move(pe.run)});
        if (out.size() >= 30) break;
    }
    return out;
}

float estimateHeight(const std::vector<Hit>& hits, const std::vector<Extra>& extras) {
    float h = 0;
    for (const Hit& hit : hits) {
        int rows = 1;
        std::function<void(const Item&, int)> count = [&](const Item& it, int depth) {
            if (it.kind != ItemKind::Toggle || !state(it).on || depth > kMaxChildDepth) return;
            for (const Item* k : childrenOf(it.id)) {
                ++rows;
                count(*k, depth + 1);
            }
        };
        count(*hit.root, 0);
        h += px(24) + float(rows) * px(kRowHeight) + px(13);
    }
    if (!extras.empty()) h += px(24) + float(extras.size()) * px(30) + px(8);
    return h;
}

 
bool extraRow(const Extra& e, int index) {
    const float w = availWidth();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = px(30);
    ImGui::PushID(index);
    const bool clicked = ImGui::InvisibleButton("##extra", ImVec2(w, h));
    const bool hov = ImGui::IsItemHovered();
    ImGui::PopID();
    ImDrawList* d = ImGui::GetWindowDrawList();
    if (hov) {
        d->AddRectFilled(pos - ImVec2(px(8), 0), pos + ImVec2(w + px(8), h), u32(pal::Fill), px(6));
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    const float cy = pos.y + h * 0.5f;
    const std::string where = ellipsize(e.where.c_str(), w * 0.45f, FontKind::Body, type::Caption);
    const ImVec2 ws = textSize(FontKind::Body, type::Caption, where.c_str());
    drawText(d, FontKind::Body, type::Caption, ImVec2(pos.x + w - ws.x, textTop(FontKind::Body, type::Caption, cy)), u32(pal::Faint), where.c_str());
    const std::string label = ellipsize(e.label.c_str(), w - ws.x - px(16), FontKind::Body, type::Label);
    drawText(d, FontKind::Body, type::Label, ImVec2(pos.x, textTop(FontKind::Body, type::Label, cy)), u32(pal::Text), label.c_str());
    return clicked;
}

 
bool resultHeader(const Item& it, int index) {
    const float w = availWidth();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = px(24);
    ImGui::PushID(index);
    const bool clicked = ImGui::InvisibleButton("##hdr", ImVec2(w, h));
    const bool hov = ImGui::IsItemHovered();
    ImGui::PopID();
    ImDrawList* d = ImGui::GetWindowDrawList();
    if (hov) {
        d->AddRectFilled(pos - ImVec2(px(8), 0), pos + ImVec2(w + px(8), h), u32(pal::Fill), px(6));
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    const char* page = pageLabel(int(it.page));
    const float ty = textTop(FontKind::Body, type::Caption, pos.y + h * 0.5f + px(1));
    const ImVec2 ps = textSize(FontKind::Semibold, type::Caption, page);
    drawText(d, FontKind::Semibold, type::Caption, ImVec2(pos.x, ty), u32(pal::Text), page);
    const std::string path = ellipsize(headerPath(it).c_str(), w - ps.x, FontKind::Body, type::Caption);
    drawText(d, FontKind::Body, type::Caption, ImVec2(pos.x + ps.x, ty), u32(pal::Muted), path.c_str());
    return clicked;
}

void closeSearch() {
    g_open = false;
    if (g_boxActive) ImGui::ClearActiveID();
}

}   

void openPalette() {
    g_open = true;
    g_focus = true;
}

bool paletteOpen() { return g_open; }

void drawRailSearch(float width) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = px(kControlH);
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImDrawListSplitter split;
    split.Split(d, 2);
    split.SetCurrentChannel(d, 1);
    ImGui::PushID("##railsearch");
    FontScope f(FontKind::Body, type::Label);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, pal::Faint);
    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(10), (h - ImGui::GetFontSize()) * 0.5f));
    ImGui::SetCursorScreenPos(pos);
    ImGui::SetNextItemWidth(width);
    if (g_focus) {
        ImGui::SetKeyboardFocusHere();
        g_focus = false;
    }
    const std::string before = g_query;
    const bool enter = ImGui::InputTextWithHint("##q", "Search Features", g_query.data(), g_query.capacity() + 1,
                                                ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_EnterReturnsTrue,
                                                [](ImGuiInputTextCallbackData* cb) {
                                                    if (cb->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                                                        auto* s = static_cast<std::string*>(cb->UserData);
                                                        s->resize(size_t(cb->BufTextLen));
                                                        cb->Buf = s->data();
                                                    }
                                                    return 0;
                                                },
                                                &g_query);
    g_boxActive = ImGui::IsItemActive();
    const bool hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemActivated() || g_query != before) g_open = true;
    if (g_boxActive && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) closeSearch();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && !g_boxActive) ImGui::SetTooltip("Search every feature and setting (Ctrl+K)");
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(5);
    ImGui::PopID();
    split.SetCurrentChannel(d, 0);
    const float t = animate(ImGui::GetID("##railsearchfill"), g_boxActive ? 1.0f : hovered ? 0.5f : 0.0f, 20.0f);
    d->AddRectFilled(pos, pos + ImVec2(width, h), u32(ImVec4(1, 1, 1, 0.06f + 0.07f * t)), px(kFrameRounding));
    split.Merge(d);
    g_box = ImRect(pos, pos + ImVec2(width, h));
    if (enter) {
        const auto terms = words(g_query);
        const auto hits = findHits(terms);
        if (!hits.empty()) {
            closeSearch();
            reveal(*hits.front().root);
        } else {
            auto extras = findExtras(terms, lower(g_query));
            if (!extras.empty() && extras.front().run) {
                closeSearch();
                extras.front().run();
            }
        }
    }
}

void drawPalette() {
    if (!g_open) return;
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const auto terms = words(g_query);
    const std::vector<Hit> hits = findHits(terms);
    std::vector<Extra> extras = findExtras(terms, lower(g_query));
    const float w = std::min(px(460), vp->Size.x - px(24));
    const float padX = px(14), padY = px(8);
    const float maxH = std::min(px(430), vp->Size.y - g_box.Max.y - px(12));
    const float contentH = terms.empty() || (hits.empty() && extras.empty()) ? px(30) : estimateHeight(hits, extras);
    const float h = std::max(px(46), std::min(maxH, contentH + padY * 2));
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + px(12), g_box.Max.y + px(6)));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padX, padY));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px(8));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(px(8), 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, px(6));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, pal::RaisedFill);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 0.10f));
    std::function<void()> run;
    if (ImGui::Begin("##fspop", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoCollapse)) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        ImDrawList* d = ImGui::GetWindowDrawList();
        auto empty = [&](const char* text) {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(availWidth(), px(30)));
            drawText(d, FontKind::Body, type::Label, ImVec2(p.x, textTop(FontKind::Body, type::Label, p.y + px(15))), u32(pal::Muted), text);
        };
        if (terms.empty()) {
            empty("Type a Feature or Setting Name");
        } else if (hits.empty() && extras.empty()) {
            empty("No Matching Features or Settings");
        } else {
            const bool isConnected = connected();
            int index = 0;
            for (const Hit& hit : hits) {
                const Item& it = *hit.root;
                ImGui::PushID(it.id.c_str());
                if (resultHeader(it, index++)) {
                    const Item* target = &it;
                    run = [target] { reveal(*target); };
                }
                if (it.kind == ItemKind::Section) {
                    RowOptions o;
                    if (actionRow("##open", titleCase(it.label).c_str(), "Open", o)) {
                        const Item* target = &it;
                        run = [target] { reveal(*target); };
                    }
                } else {
                    drawItemRow(it, isConnected, 0, true);
                    if (it.kind == ItemKind::Toggle) drawChildren(it, isConnected, 1);
                }
                const ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(availWidth(), px(13)));
                d->AddRectFilled(ImVec2(p.x, p.y + px(4)), ImVec2(p.x + availWidth(), p.y + px(5)), u32(pal::Line));
                ImGui::PopID();
            }
            if (!extras.empty()) {
                const ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(availWidth(), px(24)));
                drawText(d, FontKind::Semibold, type::Caption, ImVec2(p.x, textTop(FontKind::Semibold, type::Caption, p.y + px(12))), u32(pal::Text),
                         "Pages and Catalogs");
                for (size_t i = 0; i < extras.size(); ++i)
                    if (extraRow(extras[i], int(i)) && extras[i].run) run = extras[i].run;
                ImGui::Dummy(ImVec2(1, px(8)));
            }
        }
         
         
        ImGuiContext& g = *GImGui;
        const bool overResults = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) ||
                                 (g.HoveredWindow && (g.HoveredWindow->Flags & ImGuiWindowFlags_Popup));
        if ((ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) && !overResults &&
            !g_box.Contains(ImGui::GetIO().MousePos))
            g_open = false;
        if (!g_boxActive && ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !ImGui::IsAnyItemActive()) g_open = false;
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(5);
    if (run) {
        closeSearch();
        run();
    }
}

}   
