#include "game/movement_tick.h"

#include <algorithm>
#include <cmath>
#include <thread>

#include "core/error.h"
#include "core/runtime.h"
#include "game/movement_teleport.h"
#include "game/world_addr.h"

namespace brps3::movement {

namespace {

using Clock = std::chrono::steady_clock;

std::vector<uint8_t> be32v(uint32_t v) { return world::be32(v); }

}   

Control controlFor(const Config& cfg, const Want& w, const std::optional<PlayerLink>& link) {
    Control c;
    c.padOn = w.pad;
    c.probe = w.probe;
    if (link) {
        c.handle = link->handle;
        c.element = link->element;
        c.entry = link->entry;
        c.row = link->row;
        c.body = link->body;
        c.physicsSlot = link->physicsSlot;
    }
    c.flyOn = w.fly;
    c.brake = cfg.brake;
    c.precision = cfg.precision;
    c.step = stepPerFrame(cfg.speed, cfg.fps);
    c.precisionFactor = std::clamp(std::isfinite(cfg.precisionFactor) ? cfg.precisionFactor : d1::PRECISION, 0.01f, 1.0f);
    c.gather = w.gather;
    c.edge = w.edge;
    c.dist = std::clamp(std::isfinite(cfg.dist) ? cfg.dist : d1::OPK_DIST, 1.0f, 20.0f);
    return c;
}

Config TickSession::config() const {
    std::lock_guard<std::mutex> lock(m_);
    return cfg_;
}

bool TickSession::installed() const {
    std::lock_guard<std::mutex> lock(m_);
    return ctrl_ != 0;
}

uint32_t TickSession::block() const {
    std::lock_guard<std::mutex> lock(m_);
    return ctrl_;
}

std::string TickSession::note() const {
    std::lock_guard<std::mutex> lock(m_);
    return note_;
}

std::optional<PlayerLink> TickSession::link() const {
    std::lock_guard<std::mutex> lock(m_);
    return link_;
}

std::optional<BodyChain> TickSession::chain() const {
    std::lock_guard<std::mutex> lock(m_);
    return chain_;
}

Want TickSession::effectiveLocked() const {
    Want w = cfg_.want;
    if (warpHold_) w.probe = true;
    return w;
}

void TickSession::apply(Runtime& rt, world::Reader& mem, const Config& cfg) {
    std::lock_guard<std::mutex> lock(m_);
    const Config old = cfg_;
    cfg_ = cfg;
    try {
        syncLocked(rt, mem);
    } catch (...) {
        cfg_ = old;                                    
        try {
            syncLocked(rt, mem);
        } catch (...) {
        }
        throw;
    }
}

void TickSession::removeLocked(Runtime& rt) {
    rt.removeTask(kOpkTaskName);                       
    rt.removeTask(kTaskName);
    if (ctrl_) rt.freeLater(ctrl_);                    
    ctrl_ = 0;
    written_.reset();
}

bool TickSession::quickValidLocked(world::Reader& mem) const {
    if (!link_) return false;
    try {
        const PlayerLink& l = *link_;
        if (mem.u32(l.element + world::player::ELEM_HANDLE) != l.handle) return false;
        const world::Bytes e = mem.bytes(l.entry, 0x10);
        if (e.u32(world::sobj::HANDLE) != l.handle || (e.u32(world::sobj::FLAGS) & world::sobj::FLAG_DELETING)) return false;
        if (mem.u32(l.row + world::player::ENTRY_HANDLE) != l.handle) return false;
        if (mem.u32(l.body) != addr::VT_RIGID_BODY) return false;
        const uint32_t physics = mem.u32(l.physicsSlot);
        return taskHeap(physics) && mem.u32(physics + chain::PHYSICS_BODY) == l.body;
    } catch (const std::exception&) {
        return false;
    }
}

void TickSession::resolveLocked(world::Reader& mem) {
    lastResolve_ = Clock::now();
    const LinkResult r = resolveLink(mem);
    chain_ = r.chain;
    if (r.ok) {
        link_ = r.link;
        note_.clear();
    } else {
        note_ = r.message.empty() ? std::string("The player could not be resolved.") : r.message;
    }
}

void TickSession::writeConfigLocked(Runtime& rt) {
    const Control c = controlFor(cfg_, effectiveLocked(), link_);
    if (written_ == link_) {                           
        const std::vector<uint8_t> bytes = encodeConfig(c);
        rt.write(ctrl_ + ctl::CONFIG, bytes.data(), bytes.size(), true);
        return;
    }
     
     
     
     
    Control parked = c;
    parked.handle = 0;
    const std::vector<uint8_t> bytes = encodeConfig(parked);
    written_.reset();
    rt.write(ctrl_ + ctl::CONFIG, bytes.data(), bytes.size(), true);
    rt.writeU32(ctrl_ + ctl::P_HANDLE, c.handle, true);
    written_ = link_;
}

void TickSession::syncLocked(Runtime& rt, world::Reader& mem) {
    if (ctrl_ && !rt.hasTask(kTaskName)) {             
        rt.removeTask(kOpkTaskName);
        rt.freeLater(ctrl_);
        ctrl_ = 0;
        written_.reset();
    }
    const Want w = effectiveLocked();
    if (!w.any()) {
        removeLocked(rt);
        return;
    }
    if (w.needsPlayer() && (!link_ || (!ctrl_ && !quickValidLocked(mem)))) resolveLocked(mem);
    if (!ctrl_) {
        if (rt.hasTask(kTaskName)) rt.removeTask(kTaskName);    
        const std::vector<uint8_t> bytes = encodeControl(controlFor(cfg_, w, link_));
        const uint32_t block = rt.alloc(ctl::SIZE, bytes.data(), bytes.size());
        try {
            rt.addTask(kTaskName, [](uint32_t base) { return tickProgram(base); }, block, tickProgramSize());
        } catch (...) {
            rt.freeLater(block);
            throw;
        }
        ctrl_ = block;
        written_ = link_;                              
    } else {
        writeConfigLocked(rt);
    }
     
    const bool opk = w.gather || w.edge;
    if (opk && !rt.hasTask(kOpkTaskName)) rt.addTask(kOpkTaskName, [](uint32_t base) { return opkProgram(base); }, ctrl_, opkProgramSize());
    if (!opk) rt.removeTask(kOpkTaskName);
}

std::optional<Control> TickSession::read(Runtime& rt) {
    std::lock_guard<std::mutex> lock(m_);
    if (!ctrl_) return std::nullopt;
    if (!rt.hasTask(kTaskName)) {
        rt.removeTask(kOpkTaskName);
        rt.freeLater(ctrl_);
        ctrl_ = 0;
        written_.reset();
        return std::nullopt;
    }
    const std::vector<uint8_t> bytes = rt.read(ctrl_, ctl::SIZE);
    return decodeControl(world::Bytes(ctrl_, bytes));
}

bool TickSession::maintain(Runtime& rt, world::Reader& mem, const Control& c, int minIntervalMs) {
    std::lock_guard<std::mutex> lock(m_);
    if (!ctrl_ || !effectiveLocked().needsPlayer()) return false;
    if (c.playerStatus != uint32_t(PlayerCheck::Body) && c.playerStatus != uint32_t(PlayerCheck::Identity)) return false;
    if (Clock::now() - lastResolve_ < std::chrono::milliseconds(minIntervalMs)) return false;
    const std::optional<PlayerLink> old = link_;
    resolveLocked(mem);
    if (!link_ || link_ == old) return false;
    writeConfigLocked(rt);
    return true;
}

void TickSession::relink(Runtime& rt, world::Reader& mem) {
    std::lock_guard<std::mutex> lock(m_);
    resolveLocked(mem);
    if (ctrl_ && link_) writeConfigLocked(rt);
}

void TickSession::shutdown(Runtime& rt) {
    std::lock_guard<std::mutex> lock(m_);
    cfg_.want = Want{};
    warpHold_ = false;
    removeLocked(rt);
}

world::Vec3 TickSession::warp(Runtime& rt, world::Reader& mem, const world::Vec3& target, uint32_t frames) {
    std::string why;
    if (!validTarget(target, &why)) throw Error(why);
    uint32_t block = 0;
    world::Vec3 com = target;
    {
        std::lock_guard<std::mutex> lock(m_);
        bool ok = link_ && quickValidLocked(mem);
        if (!ok) {
            resolveLocked(mem);
            ok = link_ && note_.empty();               
        }
        if (!ok) throw Error(note_.empty() ? std::string("No spawned player: load into an activity first.") : note_);
         
         
        try {
            const world::Vec3 c1 = mem.bytes(link_->body + hk::BODY_COM1, 12).vec3(0);
            const world::Vec3 eng = mem.bytes(link_->entry + world::sobj::POSITION, 12).vec3(0);
            const world::Vec3 off = c1 - eng;
            if (c1.finite() && eng.finite() && off.length() < kMaxComOffset) com = target + off;
        } catch (const std::exception&) {
        }
        if (!validTarget(com, &why)) throw Error(why);
        warpHold_ = true;
        try {
            syncLocked(rt, mem);
            block = ctrl_;
            const std::vector<uint8_t> xyz = world::beVec3(com);
            rt.write(block + ctl::WARP_X, xyz.data(), xyz.size(), true);
            const std::vector<uint8_t> n = be32v(std::clamp<uint32_t>(frames, 1, 30));
            rt.write(block + ctl::WARP_N, n.data(), n.size(), false);    
        } catch (...) {
            warpHold_ = false;
            try {
                syncLocked(rt, mem);
            } catch (...) {
            }
            throw;
        }
    }
     
    bool done = false;
    uint32_t status = 0;
    const auto deadline = Clock::now() + std::chrono::milliseconds(1500);
    while (Clock::now() < deadline) {
        try {
            if (rt.u32(block + ctl::WARP_N) == 0) {
                done = true;
                break;
            }
        } catch (const std::exception&) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    std::lock_guard<std::mutex> lock(m_);
    if (!done && ctrl_ == block && block) {
        try {
            status = rt.u32(block + ctl::P_STATUS);
            const std::vector<uint8_t> zero = be32v(0);
            rt.write(block + ctl::WARP_N, zero.data(), zero.size(), false);
        } catch (...) {
        }
    }
    warpHold_ = false;
    syncLocked(rt, mem);
    if (!done)
        throw Error(std::string("The game thread did not take the warp (player check: ") + playerCheckText(status) +
                    "). Is the executor ticking and the player spawned?");
    return com;
}

}   
