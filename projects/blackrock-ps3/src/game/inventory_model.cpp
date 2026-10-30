#include "game/inventory_model.h"

#include <cstdio>
#include <set>

#include "game/inventory_addr.h"
#include "game/names_catalog.h"

namespace brps3::inventory {

bool RollView::has(const std::string& perk) const {
    const std::string want = lowerAscii(perk);
    for (const RollPerk& p : perks)
        if (lowerAscii(p.name) == want) return true;
    return false;
}

RollView describeRoll(const Item& it, const Catalog& cat) {
    RollView v;
    const ItemDef* d = cat.item(it.def);
    if (!d || d->hash != it.hash) {
        v.why = "not in the item catalogue";
        return v;
    }
    if (d->grid == 0xFFFF) {
        v.why = "no talent grid";
        return v;
    }
    if (!it.hasDetail) {
        v.why = "no matching detail row";
        return v;
    }
    const auto& raw = it.detail;
    uint64_t id = 0;
    for (size_t n = 0; n < 8; ++n) id = id << 8 | raw[n];
    const uint16_t def = uint16_t(raw[off::D_DEF] << 8 | raw[off::D_DEF + 1]);
    const uint16_t grid = uint16_t(raw[off::D_GRID] << 8 | raw[off::D_GRID + 1]);
    v.grid = grid;
    if (id != it.soid || def != it.def || grid != d->grid) {
        v.why = "detail row does not match the item";
        return v;
    }
    const auto* g = cat.rollGrid(grid);
    if (!g || g->size() != cat.rollNodes(grid)) {
        v.why = "grid " + std::to_string(grid) + " is incomplete in the catalogue";
        return v;
    }
    std::set<std::string> seen;
    for (const auto& [index, node] : *g) {
        if (index >= 36 || !node.count || raw[off::D_NODES + index] > node.groups || node.variants.size() != node.count || node.selector >= 8 ||
            node.selector < -1) {
            v.perks.clear();
            v.why = "node " + std::to_string(index) + " does not fit the catalogue";
            return v;
        }
        const unsigned variant = node.selector >= 0 ? raw[off::D_ROLLS + size_t(node.selector)] % node.count : node.count - 1;
        auto var = node.variants.find(variant);
        if (var == node.variants.end()) {
            v.perks.clear();
            v.why = "variant missing for node " + std::to_string(index);
            return v;
        }
        for (std::string name : splitChar(var->second, ';')) {
            while (!name.empty() && name.front() == ' ') name.erase(name.begin());
            while (!name.empty() && name.back() == ' ') name.pop_back();
            if (name.empty()) continue;
            v.perks.push_back({int(index), name, raw[off::D_NODES + index] > 0});
            if (seen.insert(name).second) v.summary += (v.summary.empty() ? "" : " / ") + name;
        }
    }
    v.readable = true;
    if (v.summary.empty()) v.summary = "No named perks in the catalogue";
    return v;
}

std::string itemName(const Item& it, const Catalog& cat) {
    if (const ItemDef* d = cat.item(it.def)) return d->label();
    return "Definition " + std::to_string(it.def);
}

std::string locationName(const Item& it) {
    if (it.owner == Owner::Character) return "Character";
    return it.vault() ? "Vault" : "Account";
}

int rarityOf(const Item& it, const Catalog& cat) {
    const ItemDef* d = cat.item(it.def);
    return d ? d->rarity : 255;
}

Query::Query(const std::string& text) {
    std::string cur;
    bool quoted = false;
    std::vector<std::string> words;
    for (char c : text) {
        if (c == '"') {
            quoted = !quoted;
            continue;
        }
        if (!quoted && (c == ' ' || c == '\t')) {
            if (!cur.empty()) words.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) words.push_back(cur);
    for (std::string w : words) {
        Term t;
        if (w.size() > 1 && w[0] == '-') {
            t.negated = true;
            w.erase(w.begin());
        }
        w = lowerAscii(w);
        const size_t colon = w.find(':');
        if (colon != std::string::npos && colon > 0 && colon + 1 < w.size()) {
            t.key = w.substr(0, colon);
            t.value = w.substr(colon + 1);
        } else {
            t.value = w;
        }
        perks_ |= t.key == "perk" || t.key.empty();
        terms_.push_back(t);
    }
}

bool Query::matches(const Item& it, const Catalog& cat, const RollView* roll) const {
    const ItemDef* d = cat.item(it.def);
    for (const Term& t : terms_) {
        bool hit = false;
        if (t.key == "is") {
            const std::string& v = t.value;
            const int r = d ? d->rarity : 255;
            if (v == "weapon" || v == "weapons") hit = bucketGroup(d ? d->home : -1) == 1 || bucketGroup(it.bucket) == 1;
            else if (v == "armor" || v == "armour") hit = bucketGroup(d ? d->home : -1) == 2 || bucketGroup(it.bucket) == 2;
            else if (v == "general") hit = bucketGroup(d ? d->home : -1) == 0 && bucketGroup(it.bucket) == 0;
            else if (v == "equipped") hit = it.equipped;
            else if (v == "vault") hit = it.vault();
            else if (v == "character") hit = it.owner == Owner::Character;
            else if (v == "account") hit = it.owner == Owner::Account && !it.vault();
            else if (v == "stack") hit = it.soid == 0;
            else if (v == "instance") hit = it.soid != 0;
            else if (v == "exotic") hit = r == 5;
            else if (v == "legendary") hit = r == 4;
            else if (v == "rare") hit = r == 3;
            else if (v == "uncommon") hit = r == 2;
            else if (v == "common") hit = r == 1;
            else if (v == "flagged") hit = it.raw != std::array<uint8_t, 6>{};    
        } else if (t.key == "perk") {
            if (roll)
                for (const RollPerk& p : roll->perks) hit |= lowerAscii(p.name).find(t.value) != std::string::npos;
        } else if (t.key == "bucket") {
            hit = lowerAscii(bucketName(it.bucket)).find(t.value) != std::string::npos;
        } else {
            const std::string& v = t.value;
            std::string hay = d ? cat.hay(it.def) : "";
            char extra[64];
            std::snprintf(extra, sizeof extra, " %08x %016llx %s", it.hash, static_cast<unsigned long long>(it.soid), it.equipped ? "equipped" : "");
            hay += extra;
            hay += " " + lowerAscii(bucketName(it.bucket)) + " " + lowerAscii(locationName(it));
            hit = hay.find(v) != std::string::npos;
            if (!hit && roll)
                for (const RollPerk& p : roll->perks) hit |= lowerAscii(p.name).find(v) != std::string::npos;
        }
        if (hit == t.negated) return false;
    }
    return true;
}

}   
