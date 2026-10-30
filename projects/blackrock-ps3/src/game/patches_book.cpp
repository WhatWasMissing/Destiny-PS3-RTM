#include "game/patches_book.h"

#include <algorithm>
#include <cstdio>

#include "core/error.h"

namespace brps3::patches {

namespace {

std::string hex(uint32_t v) {
    char b[12];
    std::snprintf(b, sizeof b, "%08X", v);
    return b;
}

}   

std::vector<SiteState> readUnit(Runtime& rt, const Unit& u, std::vector<uint32_t>* current) {
    std::vector<SiteState> out;
    if (current) current->clear();
    for (const Word& w : u.words) {
        const uint32_t v = rt.u32(w.addr);
        if (current) current->push_back(v);
        out.push_back(classify(w, v));
    }
    return out;
}

std::string checkFeatureBytes(Runtime& rt, Feature f) {
    const FeatureInfo& fi = info(f);
    std::string bad;
    auto add = [&](const std::string& s) { bad += (bad.empty() ? "" : "; ") + s; };
    for (const auto& [a, h] : fi.code) {
        const std::vector<uint8_t> want = parseHexBytes(h);
        const std::vector<uint8_t> got = rt.read(a, uint32_t(want.size()));
        if (got != want) add(hex(a) + ": " + hexBytes(got) + " (expected " + hexBytes(want) + ")");
    }
    for (const char* name : fi.units) {
        const Unit* u = findUnit(name);
        if (!u) continue;
        std::vector<uint32_t> cur;
        const auto states = readUnit(rt, *u, &cur);
        for (size_t i = 0; i < states.size(); ++i)
            if (states[i] == SiteState::Foreign)
                add(hex(u->words[i].addr) + ": " + hex(cur[i]) + " (expected " + hex(u->words[i].original) + " or " + hex(u->words[i].patched) + ")");
    }
    if (f == Feature::EntitleCounter) {
        const uint32_t v = rt.u32(kCounterLeaf);
        if (v != kCounterLeafOriginal && (v >> 26) != 18u)    
            add(hex(kCounterLeaf) + ": " + hex(v) + " (expected " + hex(kCounterLeafOriginal) + ")");
    }
    return bad;
}

SiteBook::Result SiteBook::acquire(Runtime& rt, const std::string& holder, const std::vector<std::string>& unitNames) {
    std::lock_guard<std::mutex> lock(m_);
    enum class Action { Apply, Ours, Borrow };
    struct Step {
        const Unit* unit;
        Action action;
    };
    std::vector<Step> plan;
     
    for (const std::string& name : unitNames) {
        const Unit* u = findUnit(name);
        if (!u) throw Error("unknown patch unit " + name);
        std::vector<uint32_t> cur;
        const auto states = readUnit(rt, *u, &cur);
        const bool allOriginal = std::all_of(states.begin(), states.end(), [](SiteState s) { return s == SiteState::Original; });
        const bool allPatched = std::all_of(states.begin(), states.end(), [](SiteState s) { return s == SiteState::Patched; });
        if (allOriginal) {
            if (rt.applied(name)) {
                 
                 
                try {
                    rt.revert(name);
                } catch (const std::exception&) {
                }
            }
            ours_.erase(name);
            borrowed_.erase(name);
            plan.push_back({u, Action::Apply});
        } else if (allPatched) {
            if (ours_.count(name) && rt.applied(name)) {
                plan.push_back({u, Action::Ours});
            } else {
                ours_.erase(name);
                 
                 
                 
                 
                const bool runtimeHeld = std::all_of(u->words.begin(), u->words.end(),
                                                     [&](const Word& w) { return !rt.siteHolders(w.addr).empty(); });
                plan.push_back({u, runtimeHeld ? Action::Apply : Action::Borrow});
            }
        } else {
            std::string what;
            for (size_t i = 0; i < u->words.size(); ++i) {
                if (states[i] == SiteState::Original) continue;
                what += (what.empty() ? "" : "; ") + hex(u->words[i].addr) + " holds " + hex(cur[i]) + " (original " +
                        hex(u->words[i].original) + ", patch " + hex(u->words[i].patched) + ")";
            }
            std::string msg = std::string(u->label) + ": the game code was changed by something else, nothing written: " + what;
            if (*u->sharedWith) msg += ". These sites are also used by " + std::string(u->sharedWith) + ".";
            throw Error(msg);
        }
    }
     
    Result res;
    std::vector<std::string> done;
    try {
        for (const Step& s : plan) {
            const std::string name = s.unit->name;
            if (s.action == Action::Apply) {
                if (rt.apply(name, runtimeWords(*s.unit))) {
                    ours_.insert(name);
                    done.push_back(name);
                    res.applied.push_back(name);
                } else {
                    borrowed_.insert(name);                  
                    res.borrowed.push_back(name);
                }
            } else if (s.action == Action::Borrow) {
                borrowed_.insert(name);
                res.borrowed.push_back(name);
            }
        }
    } catch (...) {
        for (auto it = done.rbegin(); it != done.rend(); ++it) {
            auto h = holders_.find(*it);
            if (h != holders_.end() && !h->second.empty()) continue;
            try {
                rt.revert(*it);
                ours_.erase(*it);
            } catch (const std::exception& e) {
                coreLog(*it + ": undo failed: " + e.what());
            }
        }
        throw;
    }
    for (const std::string& name : unitNames) holders_[name].insert(holder);
    return res;
}

void SiteBook::releaseLocked(Runtime& rt, const std::string& holder, std::vector<std::string>& errors) {
    for (auto it = holders_.begin(); it != holders_.end();) {
        auto& hs = it->second;
        if (!hs.count(holder)) {
            ++it;
            continue;
        }
        if (hs.size() > 1) {
            hs.erase(holder);
            ++it;
            continue;
        }
        const std::string unit = it->first;
        if (ours_.count(unit)) {
            try {
                rt.revert(unit);                             
                ours_.erase(unit);
            } catch (const std::exception& e) {
                errors.push_back(unit + ": " + e.what());
                ++it;                                        
                continue;
            }
        } else {
            borrowed_.erase(unit);
        }
        it = holders_.erase(it);
    }
}

void SiteBook::release(Runtime& rt, const std::string& holder) {
    std::lock_guard<std::mutex> lock(m_);
    std::vector<std::string> errors;
    releaseLocked(rt, holder, errors);
    if (!errors.empty()) {
        std::string msg = "could not restore the original code: ";
        for (size_t i = 0; i < errors.size(); ++i) msg += (i ? "; " : "") + errors[i];
        throw Error(msg);
    }
}

std::vector<std::string> SiteBook::releaseAll(Runtime& rt) {
    std::lock_guard<std::mutex> lock(m_);
    std::set<std::string> all;
    for (const auto& [unit, hs] : holders_)
        for (const auto& h : hs) all.insert(h);
    std::vector<std::string> errors;
    for (const std::string& h : all) releaseLocked(rt, h, errors);
    return errors;
}

void SiteBook::forget() {
    std::lock_guard<std::mutex> lock(m_);
    holders_.clear();
    ours_.clear();
    borrowed_.clear();
}

bool SiteBook::holds(const std::string& holder) const {
    std::lock_guard<std::mutex> lock(m_);
    for (const auto& [unit, hs] : holders_)
        if (hs.count(holder)) return true;
    return false;
}

std::vector<std::string> SiteBook::holders(const std::string& unit) const {
    std::lock_guard<std::mutex> lock(m_);
    auto it = holders_.find(unit);
    return it == holders_.end() ? std::vector<std::string>{} : std::vector<std::string>(it->second.begin(), it->second.end());
}

bool SiteBook::borrowed(const std::string& unit) const {
    std::lock_guard<std::mutex> lock(m_);
    return borrowed_.count(unit) != 0;
}

bool SiteBook::applied(const std::string& unit) const {
    std::lock_guard<std::mutex> lock(m_);
    return ours_.count(unit) != 0;
}

std::vector<SiteBook::UnitStatus> SiteBook::snapshot() const {
    std::lock_guard<std::mutex> lock(m_);
    std::vector<UnitStatus> out;
    for (const auto& [unit, hs] : holders_) {
        UnitStatus s;
        s.unit = unit;
        s.holders.assign(hs.begin(), hs.end());
        s.ours = ours_.count(unit) != 0;
        s.borrowed = borrowed_.count(unit) != 0;
        out.push_back(std::move(s));
    }
    return out;
}

}   
