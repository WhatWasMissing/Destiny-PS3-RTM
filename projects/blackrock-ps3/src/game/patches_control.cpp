#include "game/patches_control.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "core/error.h"
#include "core/session.h"
#include "game/world_mem.h"

namespace brps3::patches {

namespace {

constexpr uint32_t kNeutralSpeedBits = 0x3F800000;    
constexpr int kInnerRefreshTicks = 3;                 

std::string hex(uint32_t v) {
    char b[12];
    std::snprintf(b, sizeof b, "%08X", v);
    return b;
}

std::string fmtFloat(float f, const char* fmt = "%.2f") {
    char b[32];
    std::snprintf(b, sizeof b, fmt, double(f));
    return b;
}

std::filesystem::path journalPath() { return state_dir() / "patches-data.txt"; }

uint32_t readValue(Runtime& rt, uint32_t addr, uint8_t size) {
    if (size == 1) return rt.read(addr, 1).at(0);
    return rt.u32(addr);
}

void writeValue(Runtime& rt, uint32_t addr, uint8_t size, uint32_t v) {
    if (size == 1) {
        const uint8_t b = uint8_t(v);
        rt.write(addr, &b, 1, true);
    } else {
        rt.writeU32(addr, v, true);
    }
}

bool anyPart(const EntitleOptions& o) { return o.access || o.overrideByte || o.suppress || o.validBit || o.unlock || o.counter || o.gate; }

EntitleOptions noParts() {
    EntitleOptions o;
    o.access = o.overrideByte = o.suppress = false;
    return o;
}

}   

const char* respawnArgName(RespawnArg a) {
    switch (a) {
    case RespawnArg::PlayersDatum: return "players datum (+0x4C)";
    case RespawnArg::ObjectHandle: return "sObject handle (+0x54)";
    case RespawnArg::Native: return "local_player_handle()";
    }
    return "?";
}

Controller::Controller() = default;
Controller::~Controller() = default;

 

void Controller::settle(Runtime& rt) {
    if (block_ && rt.hasTask(tick::kTaskName)) rt.waitFrames(1, 400);
}

void Controller::ensureTask(Runtime& rt) {
    if (block_ && rt.hasTask(tick::kTaskName)) return;
    if (block_) {                                    
        rt.freeLater(block_);
        block_ = 0;
        liveFlags_ = 0;
    }
    if (rt.hasTask(tick::kTaskName)) rt.removeTask(tick::kTaskName);    
    tick::Block b = blk_;
    b.flags = 0;
    const auto bytes = b.encode();
    block_ = rt.alloc(tick::kBlockSize, bytes.data(), bytes.size());
    liveFlags_ = 0;
    try {
        rt.addTask(tick::kTaskName, [](uint32_t base) { return tick::program(base); }, block_, tick::programSize());
    } catch (...) {
        rt.freeLater(block_);
        block_ = 0;
        throw;
    }
}

void Controller::pushBlock(Runtime& rt, uint32_t changing) {
    if (!blk_.flags) {
        dropTaskIfIdle(rt);
        return;
    }
    ensureTask(rt);
     
     
    const uint32_t paused = liveFlags_ & ~changing;
    if (paused != liveFlags_) {
        rt.writeU32(block_ + tick::off::FLAGS, paused);
        liveFlags_ = paused;
        settle(rt);
    }
     
     
    const auto b = blk_.encode();
    rt.write(block_ + tick::off::FOV, b.data() + tick::off::FOV, tick::off::SPEED_LAST - tick::off::FOV);
    if (!(liveFlags_ & tick::Speed)) rt.write(block_ + tick::off::SPEED_LAST, b.data() + tick::off::SPEED_LAST, 4);
    rt.write(block_ + tick::off::VM_ADDR, b.data() + tick::off::VM_ADDR, tick::off::FRAMES - tick::off::VM_ADDR);
    rt.write(block_ + tick::off::DESC, b.data() + tick::off::DESC, tick::off::CALLS - tick::off::DESC);
    rt.write(block_ + tick::off::ARG, b.data() + tick::off::ARG, 8);
    rt.writeU32(block_ + tick::off::FLAGS, blk_.flags);
    liveFlags_ = blk_.flags;
}

void Controller::dropTaskIfIdle(Runtime& rt) {
    if (blk_.flags || !block_) return;
    if (rt.hasTask(tick::kTaskName)) {
        rt.writeU32(block_ + tick::off::FLAGS, 0);
        liveFlags_ = 0;
        rt.removeTask(tick::kTaskName);              
    }
    rt.freeLater(block_);
    block_ = 0;
}

tick::Counters Controller::readCounters(Runtime& rt) {
    if (!block_ || !rt.hasTask(tick::kTaskName)) return {};
    return tick::decode(rt.read(block_, tick::kBlockSize));
}

 

std::string Controller::journalText(uint32_t pid, const std::vector<DataEdit>& edits) {
    std::ostringstream s;
    s << "BRPATCHDATA 2 " << hex(pid) << "\n";
    for (const DataEdit& e : edits)
        s << hex(e.addr) << ' ' << int(e.size) << ' ' << hex(e.original) << ' ' << hex(e.applied) << ' ' << hex(e.anchor) << ' '
          << hex(e.anchorValue) << ' ' << e.what << "\n";
    return s.str();
}

bool Controller::parseJournal(const std::string& text, uint32_t& pid, std::vector<DataEdit>& edits) {
    std::istringstream in(text);
    std::string magic, version, pidText;
     
    if (!(in >> magic >> version >> pidText) || magic != "BRPATCHDATA" || (version != "1" && version != "2")) return false;
    const bool anchors = version == "2";
    try {
        pid = uint32_t(std::stoul(pidText, nullptr, 16));
    } catch (...) {
        return false;
    }
    edits.clear();
    std::string line;
    std::getline(in, line);
    while (std::getline(in, line)) {
        if (line.empty() || line == "\r") continue;
        std::istringstream ls(line);
        std::string a, o, v, an = "0", av = "0";
        int size = 0;
        if (!(ls >> a >> size >> o >> v) || (size != 1 && size != 4)) return false;
        if (anchors && !(ls >> an >> av)) return false;
        DataEdit e;
        try {
            e.addr = uint32_t(std::stoul(a, nullptr, 16));
            e.original = uint32_t(std::stoul(o, nullptr, 16));
            e.applied = uint32_t(std::stoul(v, nullptr, 16));
            e.anchor = uint32_t(std::stoul(an, nullptr, 16));
            e.anchorValue = uint32_t(std::stoul(av, nullptr, 16));
        } catch (...) {
            return false;
        }
        if (e.anchor && (!world::mapped(e.anchor, 4) || (e.anchor & 3))) return false;
        e.size = uint8_t(size);
        std::getline(ls >> std::ws, e.what);
        if (!e.what.empty() && e.what.back() == '\r') e.what.pop_back();
        if (!world::mapped(e.addr, e.size) || edits.size() >= 64) return false;
        edits.push_back(std::move(e));
    }
    return true;
}

void Controller::saveJournal() {
    std::error_code ec;
    const auto path = journalPath();
    if (edits_.empty()) {
        std::filesystem::remove(path, ec);
        return;
    }
    const auto tmp = path.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc | std::ios::binary);
        f << journalText(pid_, edits_);
        if (!f) {
            coreLog("patches: could not write " + tmp);
            return;
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) coreLog("patches: could not publish " + path.string() + ": " + ec.message());
}

void Controller::recordEdit(const DataEdit& e) {
    for (DataEdit& x : edits_)
        if (x.addr == e.addr) {
            if (x.anchor != e.anchor || x.anchorValue != e.anchorValue || x.size != e.size) {
                x = e;                                   
                saveJournal();
                return;
            }
            if (x.applied == e.applied) return;
            x.applied = e.applied;                       
            saveJournal();
            return;
        }
    edits_.push_back(e);
    saveJournal();
}

void Controller::forgetEdit(uint32_t addr) {
    const size_t n = edits_.size();
    edits_.erase(std::remove_if(edits_.begin(), edits_.end(), [&](const DataEdit& e) { return e.addr == addr; }), edits_.end());
    if (edits_.size() != n) saveJournal();
}

std::optional<Controller::DataEdit> Controller::edit(uint32_t addr) const {
    for (const DataEdit& e : edits_)
        if (e.addr == addr) return e;
    return std::nullopt;
}

bool Controller::anchorHolds(Runtime& rt, const DataEdit& e) { return !e.anchor || rt.u32(e.anchor) == e.anchorValue; }

void Controller::restoreEdit(Runtime& rt, uint32_t addr, std::vector<std::string>* errors) {
    const auto e = edit(addr);
    if (!e) return;
    try {
         
         
        if (anchorHolds(rt, *e)) {
            const uint32_t cur = readValue(rt, e->addr, e->size);
            if (cur == e->applied && cur != e->original) writeValue(rt, e->addr, e->size, e->original);
        }
        forgetEdit(addr);
    } catch (const std::exception& ex) {
        const std::string msg = e->what + " at " + hex(e->addr) + ": " + ex.what();
        if (errors) errors->push_back(msg);
        else throw Error(msg);
    }
}

void Controller::begin(Runtime& rt) {
    pid_ = rt.session().pid();
    if (recovered_) return;
    try {
        recoverLeftovers(rt);
    } catch (const std::exception& e) {
        log_.push_back(std::string("patches: leftover data check failed: ") + e.what());
    }
}

std::vector<std::string> Controller::recoverLeftovers(Runtime& rt) {
    std::lock_guard<std::recursive_mutex> lock(m_);
    std::vector<std::string> out;
    if (recovered_) return out;
    recovered_ = true;
    pid_ = rt.session().pid();
    std::error_code ec;
    const auto path = journalPath();
    if (!std::filesystem::exists(path, ec)) return out;
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    f.close();
    uint32_t pid = 0;
    std::vector<DataEdit> old;
    if (!parseJournal(ss.str(), pid, old)) {
        out.push_back("patches: ignored an unreadable " + path.filename().string());
    } else if (pid != pid_) {
        out.push_back("patches: " + std::to_string(old.size()) + " value(s) left by an earlier game process were dropped (the game restarted)");
    } else {
        for (const DataEdit& e : old) {
            try {
                if (!anchorHolds(rt, e)) {
                    out.push_back("patches: left " + e.what + " at " + hex(e.addr) + " alone: its object was replaced since (" + hex(e.anchor) +
                                  " no longer holds " + hex(e.anchorValue) + ")");
                    continue;
                }
                const uint32_t cur = readValue(rt, e.addr, e.size);
                if (cur == e.applied && cur != e.original) {
                    writeValue(rt, e.addr, e.size, e.original);
                    out.push_back("patches: restored " + e.what + " at " + hex(e.addr) + " (" + hex(e.applied) + " -> " + hex(e.original) +
                                  ") left by an app that did not close cleanly");
                }
            } catch (const std::exception& ex) {
                out.push_back("patches: could not restore " + e.what + " at " + hex(e.addr) + ": " + ex.what());
            }
        }
    }
    std::filesystem::remove(path, ec);
    for (const auto& l : out) log_.push_back(l);
    return out;
}

std::vector<std::string> Controller::takeLog() {
    std::lock_guard<std::recursive_mutex> lock(m_);
    std::vector<std::string> out;
    out.swap(log_);
    return out;
}

void Controller::noteBorrowed(const SiteBook::Result& r) {
    for (const std::string& u : r.borrowed) {
        const Unit* unit = findUnit(u);
        log_.push_back(std::string("patches: ") + (unit ? unit->label : u.c_str()) + " was already patched by another feature" +
                       (unit && *unit->sharedWith ? std::string(" (") + unit->sharedWith + ")" : std::string()) +
                       ": using it as is; it stays patched until that feature turns it off");
    }
}

 

namespace {
 
 
 
void refuseGateWhileLighthouseArmed(Runtime& rt, const char* what) {
    for (const std::string& n : rt.appliedNames())
        if (n.rfind("loot.lighthouse.", 0) == 0)
            throw Error(std::string(what) + " patches the permission gate 0x36EA5C, which the Walkable Lighthouse needs original while it "
                                            "is armed: disarm it first (Loot > Lighthouse).");
}
}   

void Controller::setPatch(Runtime& rt, Feature f, bool on) {
    std::lock_guard<std::recursive_mutex> lock(m_);
    begin(rt);
    const FeatureInfo& fi = info(f);
    if (on) {
        if (fi.units.empty()) throw Error(std::string(fi.label) + " is not a plain patch");
        if (f == Feature::WorldTransition) refuseGateWhileLighthouseArmed(rt, fi.label);
        const std::vector<std::string> names(fi.units.begin(), fi.units.end());
        noteBorrowed(book_.acquire(rt, fi.key, names));
        on_[size_t(f)] = true;
    } else {
        on_[size_t(f)] = false;
        book_.release(rt, fi.key);
    }
}

bool Controller::patchOn(Feature f) const {
    std::lock_guard<std::recursive_mutex> lock(m_);
    return on_[size_t(f)];
}

 

bool Controller::latch(Runtime& rt, world::PlayerCache& cache, bool force) {
    world::RuntimeReader mem(rt);
    const auto p = cache.get(mem, force ? 0 : 1500);
    if (!p) {
        alive_ = false;
        if (latch_.ok) {
             
             
             
            const uint32_t desc = rt.u32(addr::PLAYER_DESC_PTR);
            const uint32_t elements = world::mapped(desc, 8) ? rt.u32(desc + 4) : 0;
            if (desc != latch_.desc || elements != latch_.elements) {
                latch_ = {};
                if (blk_.flags & tick::Respawn) {
                    blk_.flags &= ~uint32_t(tick::Respawn);
                    pushBlock(rt, tick::Respawn);
                }
            }
        }
        const auto last = cache.last();
        if (!latch_.ok) respawnNote_ = "Waiting for your player: " + (last.message.empty() ? std::string("not spawned") : last.message);
        return false;
    }
    alive_ = true;
    Latch l;
    l.ok = true;
    l.desc = p->descriptor;
    l.elements = p->elements;
    l.element = p->element;
    l.handle = p->handle;
    l.datum = p->playerDatum;
    const bool changed = l.desc != latch_.desc || l.elements != latch_.elements || l.element != latch_.element || l.handle != latch_.handle ||
                         l.datum != latch_.datum || !latch_.ok;
    latch_ = l;
    return changed;
}

void Controller::armRespawn(Runtime& rt) {
    if (!fast_ || !latch_.ok) return;
    if (arg_ == RespawnArg::PlayersDatum && !world::validHandle(latch_.datum)) {
        if (blk_.flags & tick::Respawn) {
            blk_.flags &= ~uint32_t(tick::Respawn);
            pushBlock(rt, tick::Respawn);
        }
        respawnNote_ = "Not armed: your player-array row (+0x4C) was not found. Pick sObject +0x54 or Native as the argument.";
        return;
    }
    blk_.desc = latch_.desc;
    blk_.elements = latch_.elements;
    blk_.element = latch_.element;
    blk_.mode = arg_ == RespawnArg::Native ? 1u : 0u;
    blk_.arg = signExtend(arg_ == RespawnArg::ObjectHandle ? latch_.handle : latch_.datum);
    blk_.flags |= tick::Respawn;
    pushBlock(rt, tick::Respawn);
    respawnNote_ = std::string("Armed with ") + respawnArgName(arg_) +
                   (arg_ == RespawnArg::Native ? std::string() : " = " + hex(uint32_t(blk_.arg))) + ": respawns you the frame you die.";
}

void Controller::setFastRespawn(Runtime& rt, world::PlayerCache& cache, bool on) {
    std::lock_guard<std::recursive_mutex> lock(m_);
    begin(rt);
    const FeatureInfo& fi = info(Feature::FastRespawn);
    if (on) {
        if (fast_) return;
        noteBorrowed(book_.acquire(rt, fi.key, std::vector<std::string>(fi.units.begin(), fi.units.end())));
        fast_ = true;
        on_[size_t(Feature::FastRespawn)] = true;
        try {
            latch(rt, cache, true);
            if (latch_.ok) armRespawn(rt);
            else respawnNote_ = "Patches on. No player handle was seen in this activity yet: the respawn call arms as soon as you are alive.";
        } catch (...) {
            fast_ = false;
            on_[size_t(Feature::FastRespawn)] = false;
            blk_.flags &= ~uint32_t(tick::Respawn);
            try {
                pushBlock(rt, tick::Respawn);
            } catch (const std::exception&) {
            }
            try {
                book_.release(rt, fi.key);
            } catch (const std::exception&) {
            }
            throw;
        }
    } else {
        fast_ = false;
        on_[size_t(Feature::FastRespawn)] = false;
        std::vector<std::string> errors;
        if (blk_.flags & tick::Respawn) {
            blk_.flags &= ~uint32_t(tick::Respawn);
            try {
                pushBlock(rt, tick::Respawn);
            } catch (const std::exception& e) {
                errors.push_back(e.what());
            }
        }
        respawnNote_.clear();
        book_.release(rt, fi.key);
        if (!errors.empty()) throw Error("Auto respawn: " + errors.front());
    }
}

void Controller::setRespawnArg(Runtime& rt, RespawnArg a) {
    std::lock_guard<std::recursive_mutex> lock(m_);
    arg_ = a;
    if (fast_ && latch_.ok) armRespawn(rt);
}

 

void Controller::setFov(Runtime& rt, bool on) {
    std::lock_guard<std::recursive_mutex> lock(m_);
    begin(rt);
    const FeatureInfo& fi = info(Feature::Fov);
    if (on) {
        if (fovOn_) return;
        noteBorrowed(book_.acquire(rt, fi.key, std::vector<std::string>(fi.units.begin(), fi.units.end())));
        try {
            const uint32_t cur = rt.u32(addr::FOV_OVERRIDE);
            const auto had = edit(addr::FOV_OVERRIDE);
            if (!had && !fovGlobalPlausible(world::bitsFloat(cur)))
                throw Error("FOV override: the FOV global " + hex(addr::FOV_OVERRIDE) + " holds " + hex(cur) +
                            " (expected -1.0 or an angle): something else is using it, nothing written");
            const uint32_t bits = world::floatBits(clampFov(fovDeg_));
            recordEdit({addr::FOV_OVERRIDE, 4, had ? had->original : cur, bits, "FOV override"});
            blk_.fov = bits;
            blk_.flags |= tick::Fov;
            pushBlock(rt, tick::Fov);
            rt.writeU32(addr::FOV_OVERRIDE, bits);       
            fovOn_ = true;
            on_[size_t(Feature::Fov)] = true;
        } catch (...) {
            blk_.flags &= ~uint32_t(tick::Fov);
            try {
                pushBlock(rt, tick::Fov);
                restoreEdit(rt, addr::FOV_OVERRIDE, nullptr);
            } catch (const std::exception&) {
            }
            try {
                book_.release(rt, fi.key);
            } catch (const std::exception&) {
            }
            throw;
        }
    } else {
        fovOn_ = false;
        on_[size_t(Feature::Fov)] = false;
        std::vector<std::string> errors;
        if (blk_.flags & tick::Fov) {
            blk_.flags &= ~uint32_t(tick::Fov);
            try {
                pushBlock(rt, tick::Fov);
            } catch (const std::exception& e) {
                errors.push_back(e.what());
            }
        }
        restoreEdit(rt, addr::FOV_OVERRIDE, &errors);
        try {
            book_.release(rt, fi.key);
        } catch (const std::exception& e) {
            errors.push_back(e.what());
        }
        if (!errors.empty()) throw Error("FOV override: " + errors.front());
    }
}

void Controller::setFovDegrees(Runtime& rt, float deg) {
    std::lock_guard<std::recursive_mutex> lock(m_);
    if (!std::isfinite(deg)) throw Error("Field of view: not a number");
    fovDeg_ = clampFov(deg);
    if (!fovOn_) return;
    begin(rt);
    const uint32_t bits = world::floatBits(fovDeg_);
    blk_.fov = bits;
    pushBlock(rt, tick::Fov);
    rt.writeU32(addr::FOV_OVERRIDE, bits);
    if (const auto e = edit(addr::FOV_OVERRIDE)) recordEdit({e->addr, 4, e->original, bits, e->what});
}

 

void Controller::applyViewmodelTarget(Runtime& rt, const ViewmodelTarget& t) {
    const uint32_t bits = viewmodelBits(vmDeg_);
    if (vm_.ok && vm_.target != t.target) forgetEdit(vm_.target);    
    auto had = edit(t.target);
    if (had && had->anchorValue != t.handle) had.reset();           
    if (!had && !viewmodelRadiansValid(world::bitsFloat(t.current)))
        throw Error("Viewmodel FOV: the settings value at " + hex(t.target) + " holds " + hex(t.current) +
                    ", which is not an angle in radians: nothing written");
    recordEdit({t.target, 4, had ? had->original : t.current, bits, "viewmodel FOV", addr::VIEWMODEL_HANDLE, t.handle});
    vm_ = t;
    blk_.vmAddr = t.target;
    blk_.vmBits = bits;
    blk_.vmHandle = t.handle;
    blk_.flags |= tick::Viewmodel;
    pushBlock(rt, tick::Viewmodel);
    rt.writeU32(t.target, bits);
    vmNote_ = "Writing " + fmtFloat(vmDeg_, "%.0f") + "\xC2\xB0 to " + hex(t.target) +
              (t.handle == 0xFFFFFFFFu ? " (default settings block)" : " (settings " + hex(t.handle) + ")") + ". Swap weapons to see it.";
}

void Controller::setViewmodel(Runtime& rt, bool on) {
    std::lock_guard<std::recursive_mutex> lock(m_);
    begin(rt);
    const FeatureInfo& fi = info(Feature::ViewmodelFov);
    if (on) {
        if (vmOn_) return;
        noteBorrowed(book_.acquire(rt, fi.key, std::vector<std::string>(fi.units.begin(), fi.units.end())));
        try {
            world::RuntimeReader mem(rt);
            const ViewmodelTarget t = resolveViewmodel(mem);
            if (!t.ok) throw Error("Viewmodel FOV: " + t.why);
            applyViewmodelTarget(rt, t);
            vmOn_ = true;
            on_[size_t(Feature::ViewmodelFov)] = true;
        } catch (...) {
            blk_.flags &= ~uint32_t(tick::Viewmodel);
            try {
                pushBlock(rt, tick::Viewmodel);
                if (vm_.ok) restoreEdit(rt, vm_.target, nullptr);
            } catch (const std::exception&) {
            }
            vm_ = {};
            try {
                book_.release(rt, fi.key);
            } catch (const std::exception&) {
            }
            throw;
        }
    } else {
        vmOn_ = false;
        on_[size_t(Feature::ViewmodelFov)] = false;
        std::vector<std::string> errors;
        if (blk_.flags & tick::Viewmodel) {
            blk_.flags &= ~uint32_t(tick::Viewmodel);
            try {
                pushBlock(rt, tick::Viewmodel);
            } catch (const std::exception& e) {
                errors.push_back(e.what());
            }
        }
        if (vm_.ok) {
            try {
                world::RuntimeReader mem(rt);
                const ViewmodelTarget now = resolveViewmodel(mem);
                 
                if (now.ok && now.handle == vm_.handle && now.target == vm_.target) restoreEdit(rt, vm_.target, &errors);
                else forgetEdit(vm_.target);
            } catch (const std::exception& e) {
                errors.push_back(e.what());
            }
        }
        vm_ = {};
        vmNote_.clear();
        try {
            book_.release(rt, fi.key);
        } catch (const std::exception& e) {
            errors.push_back(e.what());
        }
        if (!errors.empty()) throw Error("Viewmodel FOV: " + errors.front());
    }
}

void Controller::setViewmodelDegrees(Runtime& rt, float deg) {
    std::lock_guard<std::recursive_mutex> lock(m_);
    if (!std::isfinite(deg)) throw Error("Viewmodel angle: not a number");
    vmDeg_ = clampViewmodel(deg);
    if (!vmOn_ || !vm_.ok) return;
    begin(rt);
    const uint32_t bits = viewmodelBits(vmDeg_);
    if (rt.u32(addr::VIEWMODEL_HANDLE) != vm_.handle) {    
        blk_.vmBits = bits;
        return;
    }
    blk_.vmBits = bits;
    pushBlock(rt, tick::Viewmodel);
    rt.writeU32(vm_.target, bits);
    if (const auto e = edit(vm_.target)) recordEdit({e->addr, 4, e->original, bits, e->what, e->anchor, e->anchorValue});
    vmNote_ = "Writing " + fmtFloat(vmDeg_, "%.0f") + "\xC2\xB0 to " + hex(vm_.target) + ". Swap weapons to see it.";
}

 

void Controller::setSpeed(Runtime& rt, bool on) {
    std::lock_guard<std::recursive_mutex> lock(m_);
    begin(rt);
    if (on) {
        if (speedOn_) return;
        const uint32_t bits = world::floatBits(clampSpeed(speed_));
        blk_.speed = bits;
        blk_.speedLast = bits;
        blk_.flags |= tick::Speed;
        try {
            pushBlock(rt, tick::Speed);
        } catch (...) {
            blk_.flags &= ~uint32_t(tick::Speed);
            try {
                pushBlock(rt, tick::Speed);
            } catch (const std::exception&) {
            }
            throw;
        }
        speedOn_ = true;
         
        const uint32_t p = rt.u32(addr::GAME_TIME_PTR);
        if (world::heap(p, 8) && !(p & 3)) {
            const uint32_t cur = rt.u32(p + addr::GAME_SPEED_OFFSET);
            if (speedValid(world::bitsFloat(cur))) {
                recordEdit({p + addr::GAME_SPEED_OFFSET, 4, cur, bits, "game speed", addr::GAME_TIME_PTR, p});
                if (cur != bits) rt.writeU32(p + addr::GAME_SPEED_OFFSET, bits, false);
            }
        }
    } else {
        speedOn_ = false;
        std::vector<std::string> errors;
        if (blk_.flags & tick::Speed) {
            blk_.flags &= ~uint32_t(tick::Speed);
            try {
                pushBlock(rt, tick::Speed);
            } catch (const std::exception& e) {
                errors.push_back(e.what());
            }
        }
         
         
        try {
            const uint32_t p = rt.u32(addr::GAME_TIME_PTR);
            if (world::heap(p, 8) && !(p & 3)) {
                const uint32_t cur = rt.u32(p + addr::GAME_SPEED_OFFSET);
                if (cur == blk_.speed && cur != kNeutralSpeedBits) rt.writeU32(p + addr::GAME_SPEED_OFFSET, kNeutralSpeedBits, false);
            }
        } catch (const std::exception& e) {
            errors.push_back(e.what());
        }
        edits_.erase(std::remove_if(edits_.begin(), edits_.end(), [](const DataEdit& e) { return e.what == "game speed"; }), edits_.end());
        saveJournal();
        if (!errors.empty()) throw Error("Game speed: " + errors.front());
    }
}

void Controller::setSpeedValue(Runtime& rt, float mult) {
    std::lock_guard<std::recursive_mutex> lock(m_);
    if (!std::isfinite(mult)) throw Error("Speed multiplier: not a number");
    speed_ = clampSpeed(mult);
    if (!speedOn_) return;
    begin(rt);
    const uint32_t old = blk_.speed;
    const uint32_t bits = world::floatBits(speed_);
    blk_.speedLast = old;                                
    blk_.speed = bits;
    pushBlock(rt, tick::Speed);
    blk_.speedLast = bits;
    const uint32_t p = rt.u32(addr::GAME_TIME_PTR);
    if (world::heap(p, 8) && !(p & 3)) {
        const uint32_t cur = rt.u32(p + addr::GAME_SPEED_OFFSET);
        if (cur == old || speedValid(world::bitsFloat(cur))) {
            rt.writeU32(p + addr::GAME_SPEED_OFFSET, bits, false);
            const auto e = edit(p + addr::GAME_SPEED_OFFSET);
            recordEdit({p + addr::GAME_SPEED_OFFSET, 4, e && e->anchorValue == p ? e->original : (cur == old ? kNeutralSpeedBits : cur), bits,
                        "game speed", addr::GAME_TIME_PTR, p});
        }
    }
}

 

void Controller::setNoInactivity(Runtime& rt, bool on) {
    std::lock_guard<std::recursive_mutex> lock(m_);
    begin(rt);
    if (on) {
        if (afkOn_) return;
         
        const uint32_t vt = rt.u32(addr::SIGNIN_LISTENER);
        if (vt != addr::SIGNIN_LISTENER_VTABLE)
            throw Error("No inactivity kick: the sign-in listener at " + hex(addr::SIGNIN_LISTENER) + " holds " + hex(vt) + " (expected vtable " +
                        hex(addr::SIGNIN_LISTENER_VTABLE) + "): not set up yet, nothing written");
        blk_.flags |= tick::Afk;
        try {
            pushBlock(rt, 0);
        } catch (...) {
            blk_.flags &= ~uint32_t(tick::Afk);
            try {
                pushBlock(rt, tick::Afk);
            } catch (const std::exception&) {
            }
            throw;
        }
        const uint8_t zero = 0;
        rt.write(addr::NO_INACTIVITY_FLAG, &zero, 1, false);
        afkOn_ = true;
    } else {
        afkOn_ = false;                                  
        if (blk_.flags & tick::Afk) {
            blk_.flags &= ~uint32_t(tick::Afk);
            pushBlock(rt, tick::Afk);
        }
    }
}

 

void Controller::entitleOverrideOn(Runtime& rt) {
    uint32_t inner = 0;
    try {
        inner = rt.call(addr::ENTITLE_INNER_GET).u32();
    } catch (const std::exception& e) {
        entitleNote_ = std::string("Override byte waiting: ") + e.what();
        return;
    }
    const uint32_t at = inner + addr::ENTITLE_OVERRIDE_OFFSET;
    if (!world::heap(inner, addr::ENTITLE_OVERRIDE_OFFSET + 1)) {
        entitleNote_ = "Override byte waiting: the entitlement object is not resolvable yet (inner " + hex(inner) + "); retried every few seconds.";
        return;
    }
    if (inner != inner_) {
        if (inner_) forgetEdit(inner_ + addr::ENTITLE_OVERRIDE_OFFSET);    
        const uint8_t orig = rt.read(at, 1).at(0);
        inner_ = inner;
        overrideRoot_ = rt.u32(addr::ENTITLE_ROOT_PTR);
        recordEdit({at, 1, orig, 1, "entitlement override byte", addr::ENTITLE_ROOT_PTR, overrideRoot_});
    }
    if (rt.read(at, 1).at(0) != 1) {
        const uint8_t one = 1;
        rt.write(at, &one, 1, true);
    }
    entitleNote_ = "Override byte held at " + hex(at) + ".";
}

void Controller::entitleOverrideOff(Runtime& rt, std::vector<std::string>* errors) {
    if (!inner_) return;
    const uint32_t at = inner_ + addr::ENTITLE_OVERRIDE_OFFSET;
    bool same = false;
    try {
        same = rt.call(addr::ENTITLE_INNER_GET).u32() == inner_;
    } catch (const std::exception&) {
         
        try {
            same = overrideRoot_ && rt.u32(addr::ENTITLE_ROOT_PTR) == overrideRoot_;
        } catch (const std::exception& e) {
            if (errors) errors->push_back(e.what());
        }
    }
    if (same) restoreEdit(rt, at, errors);
    else forgetEdit(at);
    inner_ = 0;
    overrideRoot_ = 0;
}

void Controller::entitlePartsOff(Runtime& rt, const EntitleOptions& p, std::vector<std::string>* errors) {
    auto guard = [&](const char* what, auto&& fn) {
        try {
            fn();
        } catch (const std::exception& e) {
            if (errors) errors->push_back(std::string(what) + ": " + e.what());
        }
    };
    if (p.overrideByte) guard("override byte", [&] { entitleOverrideOff(rt, errors); });
    if (p.suppress) guard("fly-in suppress", [&] {
        if (blk_.flags & tick::Suppress) {
            blk_.flags &= ~uint32_t(tick::Suppress);
            pushBlock(rt, tick::Suppress);
        }
        if (suppressObj_) {
            if (rt.u32(addr::FLYIN_SUPPRESS_PTR) == suppressObj_) restoreEdit(rt, suppressObj_, errors);
            else forgetEdit(suppressObj_);
        }
        suppressObj_ = 0;
    });
    if (p.validBit) guard("valid bit", [&] {
        if (blk_.flags & tick::ValidBit) {
            blk_.flags &= ~uint32_t(tick::ValidBit);
            pushBlock(rt, tick::ValidBit);
        }
        if (root_ && !validOriginal_ && rt.u32(addr::ENTITLE_ROOT_PTR) == root_) {
            const uint32_t cur = rt.u32(root_ + addr::ENTITLE_VALID_OFFSET);
            if (cur & 1) rt.writeU32(root_ + addr::ENTITLE_VALID_OFFSET, cur & ~1u, false);    
        }
        root_ = 0;
    });
    if (p.gate) guard("permission gate", [&] { book_.release(rt, info(Feature::EntitleGate).key); });
    if (p.counter) guard("counter leaf", [&] {
        if (rt.hooked(kCounterLeaf)) rt.unhook(kCounterLeaf);
    });
    if (p.unlock) guard("unlock leaf", [&] { book_.release(rt, info(Feature::EntitleUnlock).key); });
    if (p.access) guard("access predicate", [&] { book_.release(rt, info(Feature::EntitleAccess).key); });
}

void Controller::setEntitle(Runtime& rt, bool on, const EntitleOptions& o) {
    std::lock_guard<std::recursive_mutex> lock(m_);
    begin(rt);
    std::vector<std::string> errors;
    if (!on) {
        if (entitleOn_) entitlePartsOff(rt, entitle_, &errors);
        entitleOn_ = false;
        entitle_ = noParts();
        entitleNote_.clear();
        on_[size_t(Feature::EntitleAccess)] = on_[size_t(Feature::EntitleUnlock)] = on_[size_t(Feature::EntitleCounter)] =
            on_[size_t(Feature::EntitleGate)] = false;
        if (!errors.empty()) throw Error("Internal mode: " + errors.front());
        return;
    }
    if (!anyPart(o)) throw Error("Internal mode: pick at least one lever");
    EntitleOptions have = entitleOn_ ? entitle_ : noParts();
     
    EntitleOptions drop = noParts();
    drop.access = have.access && !o.access;
    drop.overrideByte = have.overrideByte && !o.overrideByte;
    drop.suppress = have.suppress && !o.suppress;
    drop.validBit = have.validBit && !o.validBit;
    drop.unlock = have.unlock && !o.unlock;
    drop.counter = have.counter && !o.counter;
    drop.gate = have.gate && !o.gate;
    entitlePartsOff(rt, drop, &errors);
    if (drop.access) have.access = false;
    if (drop.overrideByte) have.overrideByte = false;
    if (drop.suppress) have.suppress = false;
    if (drop.validBit) have.validBit = false;
    if (drop.unlock) have.unlock = false;
    if (drop.counter) have.counter = false;
    if (drop.gate) have.gate = false;
     
    EntitleOptions added = noParts();
    try {
        if (o.access && !have.access) {
            noteBorrowed(book_.acquire(rt, info(Feature::EntitleAccess).key, {"site.00C35140"}));
            have.access = added.access = true;
        }
        if (o.unlock && !have.unlock) {
            noteBorrowed(book_.acquire(rt, info(Feature::EntitleUnlock).key, {"site.00C5BE34"}));
            have.unlock = added.unlock = true;
        }
        if (o.counter && !have.counter) {
            if (!rt.hooked(kCounterLeaf))
                rt.hook(kCounterLeaf, [](uint32_t base, uint32_t) { return counterLeafStub(base); }, kCounterLeafOriginal, 0x10);
            have.counter = added.counter = true;
        }
        if (o.gate && !have.gate) {
            refuseGateWhileLighthouseArmed(rt, "Internal mode (Keep permission gate)");
            noteBorrowed(book_.acquire(rt, info(Feature::EntitleGate).key, {"site.0036EA5C"}));
            have.gate = added.gate = true;
        }
        if (o.validBit && !have.validBit) {
            const uint32_t root = rt.u32(addr::ENTITLE_ROOT_PTR);
            if (world::heap(root, addr::ENTITLE_VALID_OFFSET + 4) && !(root & 3)) {
                root_ = root;
                validOriginal_ = (rt.u32(root + addr::ENTITLE_VALID_OFFSET) & 1) != 0;
            } else {
                root_ = 0;                               
            }
            blk_.flags |= tick::ValidBit;
            pushBlock(rt, 0);
            have.validBit = added.validBit = true;
        }
        if (o.suppress && !have.suppress) {
            const uint32_t obj = rt.u32(addr::FLYIN_SUPPRESS_PTR);
            suppressObj_ = 0;
            if (world::heap(obj, 1)) {
                suppressObj_ = obj;
                const uint8_t orig = rt.read(obj, 1).at(0);
                recordEdit({obj, 1, orig, 1, "fly-in suppress", addr::FLYIN_SUPPRESS_PTR, obj});
            }
            blk_.flags |= tick::Suppress;
            pushBlock(rt, 0);
            have.suppress = added.suppress = true;
        }
        if (o.overrideByte && !have.overrideByte) {
            entitleOverrideOn(rt);                       
            innerAge_ = 0;
            have.overrideByte = added.overrideByte = true;
        }
    } catch (...) {
        entitlePartsOff(rt, added, nullptr);
        entitle_ = have;
        if (added.access) entitle_.access = false;
        if (added.unlock) entitle_.unlock = false;
        if (added.counter) entitle_.counter = false;
        if (added.gate) entitle_.gate = false;
        if (added.validBit) entitle_.validBit = false;
        if (added.suppress) entitle_.suppress = false;
        if (added.overrideByte) entitle_.overrideByte = false;
        entitleOn_ = anyPart(entitle_);
        throw;
    }
    entitle_ = have;
    entitleOn_ = true;
    on_[size_t(Feature::EntitleAccess)] = have.access;
    on_[size_t(Feature::EntitleUnlock)] = have.unlock;
    on_[size_t(Feature::EntitleCounter)] = have.counter;
    on_[size_t(Feature::EntitleGate)] = have.gate;
    if (!have.overrideByte) entitleNote_ = "Levers on.";
    if (!errors.empty()) throw Error("Internal mode: " + errors.front());
}

 

void Controller::setTracking(bool on) {
    std::lock_guard<std::recursive_mutex> lock(m_);
    track_ = on;
}

bool Controller::monitorNeeded() const {
    std::lock_guard<std::recursive_mutex> lock(m_);
    return track_ || fast_ || vmOn_ || (entitleOn_ && entitle_.overrideByte);
}

void Controller::monitor(Runtime& rt, world::PlayerCache& cache) {
    std::lock_guard<std::recursive_mutex> lock(m_);
    std::string problem;
     
     
    if (fast_ || track_) {
        try {
             
             
             
             
            bool resolve = !latch_.ok || (fast_ && !(blk_.flags & tick::Respawn)) || ++latchAge_ >= 10;
            if (!resolve) {
                if (rt.u32(addr::PLAYER_DESC_PTR) != latch_.desc) {
                    resolve = true;                          
                } else {
                    const uint32_t h = rt.u32(latch_.element + 0x84);
                    if (h == latch_.handle) alive_ = true;
                    else if (h == 0 || h == 0xFFFFFFFFu) alive_ = false;
                    else resolve = true;                     
                }
            }
            if (resolve) {
                latchAge_ = 0;
                const bool changed = latch(rt, cache, true);
                if (fast_ && alive_ && (changed || !(blk_.flags & tick::Respawn))) armRespawn(rt);
            }
        } catch (const std::exception& e) {
            problem = std::string("auto respawn: ") + e.what();
        }
    }
    if (vmOn_) {
        try {
            const uint32_t handle = rt.u32(addr::VIEWMODEL_HANDLE);
            if (handle != blk_.vmHandle || !vm_.ok) {
                world::RuntimeReader mem(rt);
                const ViewmodelTarget t = resolveViewmodel(mem);
                if (t.ok) applyViewmodelTarget(rt, t);
                else vmNote_ = "Waiting for the viewmodel settings: " + t.why;
            }
        } catch (const std::exception& e) {
            problem = std::string("viewmodel FOV: ") + e.what();
        }
    }
    if (entitleOn_ && entitle_.overrideByte && ++innerAge_ >= kInnerRefreshTicks) {
        innerAge_ = 0;
        try {
            entitleOverrideOn(rt);
        } catch (const std::exception& e) {
            problem = std::string("override byte: ") + e.what();
        }
    }
    try {
        counters_ = readCounters(rt);
    } catch (const std::exception& e) {
        if (problem.empty()) problem = std::string("per-frame task: ") + e.what();
    }
    lastError_ = problem;
}

void Controller::refresh(Runtime& rt) {
    std::lock_guard<std::recursive_mutex> lock(m_);
    counters_ = readCounters(rt);
}

 

std::vector<std::string> Controller::allOff(Runtime& rt) {
    std::lock_guard<std::recursive_mutex> lock(m_);
    std::vector<std::string> errors;
    try {
        begin(rt);                                       
    } catch (const std::exception& e) {
        errors.push_back(e.what());
    }
     
    if (block_) {
        try {
            if (rt.hasTask(tick::kTaskName)) {
                rt.writeU32(block_ + tick::off::FLAGS, 0);
                liveFlags_ = 0;
                settle(rt);
            }
        } catch (const std::exception& e) {
            errors.push_back(std::string("per-frame task: ") + e.what());
        }
    }
    const uint32_t speedTarget = blk_.speed;
    blk_.flags = 0;
     
    if (fovOn_ || edit(addr::FOV_OVERRIDE)) restoreEdit(rt, addr::FOV_OVERRIDE, &errors);
    if (vm_.ok) {
        try {
            world::RuntimeReader mem(rt);
            const ViewmodelTarget now = resolveViewmodel(mem);
            if (now.ok && now.handle == vm_.handle && now.target == vm_.target) restoreEdit(rt, vm_.target, &errors);
            else forgetEdit(vm_.target);
        } catch (const std::exception& e) {
            errors.push_back(std::string("viewmodel FOV: ") + e.what());
        }
    }
    if (speedOn_) {
        try {
            const uint32_t p = rt.u32(addr::GAME_TIME_PTR);
            if (world::heap(p, 8) && !(p & 3) && rt.u32(p + addr::GAME_SPEED_OFFSET) == speedTarget && speedTarget != kNeutralSpeedBits)
                rt.writeU32(p + addr::GAME_SPEED_OFFSET, kNeutralSpeedBits, false);
        } catch (const std::exception& e) {
            errors.push_back(std::string("game speed: ") + e.what());
        }
        edits_.erase(std::remove_if(edits_.begin(), edits_.end(), [](const DataEdit& e) { return e.what == "game speed"; }), edits_.end());
    }
    if (entitleOn_) {
        EntitleOptions data = noParts();
        data.overrideByte = entitle_.overrideByte;
        data.suppress = entitle_.suppress;
        data.validBit = entitle_.validBit;
        data.counter = entitle_.counter;
        entitlePartsOff(rt, data, &errors);
    }
    if (rt.hooked(kCounterLeaf)) {
        try {
            rt.unhook(kCounterLeaf);
        } catch (const std::exception& e) {
            errors.push_back(std::string("counter leaf: ") + e.what());
        }
    }
     
    for (const std::string& e : book_.releaseAll(rt)) errors.push_back(e);
     
    try {
        dropTaskIfIdle(rt);
    } catch (const std::exception& e) {
        errors.push_back(std::string("per-frame task: ") + e.what());
        block_ = 0;
    }
     
    book_.forget();
    on_.fill(false);
    fovOn_ = vmOn_ = speedOn_ = afkOn_ = fast_ = entitleOn_ = false;
    entitle_ = noParts();
    vm_ = {};
    latch_ = {};
    alive_ = false;
    inner_ = overrideRoot_ = suppressObj_ = root_ = 0;
    respawnNote_.clear();
    vmNote_.clear();
    entitleNote_.clear();
    counters_ = {};
    blk_ = {};
    liveFlags_ = 0;
    saveJournal();                                       
    return errors;
}

 

Snapshot Controller::snapshot() const {
    std::lock_guard<std::recursive_mutex> lock(m_);
    Snapshot s;
    s.taskRunning = block_ != 0;
    s.block = block_;
    s.flags = blk_.flags;
    s.frames = counters_.frames;
    s.fastRespawn = fast_;
    s.respawnArmed = fast_ && (blk_.flags & tick::Respawn);
    s.respawnArg = arg_;
    s.latchedHandle = latch_.handle;
    s.latchedDatum = latch_.datum;
    s.latchedElement = latch_.element;
    s.argument = blk_.arg;
    s.respawnCalls = counters_.calls;
    s.lastSeen = counters_.seen;
    s.playerAlive = alive_;
    s.respawnNote = respawnNote_;
    s.fovOn = fovOn_;
    s.speedOn = speedOn_;
    s.viewmodelOn = vmOn_;
    s.afkOn = afkOn_;
    s.fovDeg = fovDeg_;
    s.speedMult = speed_;
    s.viewmodelDeg = vmDeg_;
    s.speedSkips = counters_.speedSkips;
    s.speedSeen = world::bitsFloat(counters_.speedSeen);
    s.viewmodelNote = vmNote_;
    s.entitleOn = entitleOn_;
    s.entitle = entitle_;
    s.entitleInner = inner_;
    s.entitleNote = entitleNote_;
    s.patchOn = on_;
    s.units = book_.snapshot();
    s.lastError = lastError_;
    s.dataEdits = edits_.size();
    return s;
}

}   
