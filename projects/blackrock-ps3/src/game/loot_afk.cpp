#include "game/loot_afk.h"

#include <algorithm>

#include "core/error.h"
#include "game/loot_addr.h"

namespace brps3::loot::afk {

using world::Bytes;

const char* methodName(Method m) { return m == Method::ZeroLimit ? "Turn the kick limit off" : "Reset the idle timer"; }

std::string Binding::modeName() const {
    if (mode == 0xFFFFFFFF) return "no mode";
    if (noLimit) return fmt("mode %u (no limit)", mode);
    return fmt("mode %u (%u min)", mode, original / 60000);
}

Binding inspect(world::Reader& mem, bool needSlot) {
    Binding b;
    b.root = mem.u32(AFK_CONFIG_ROOT);
    if (!heapPtr(b.root, AFK_BLOCK + 0xA0)) throw Error("The AFK configuration is not loaded (" + hex(b.root) + ")");
    const Bytes mb = mem.bytes(AFK_MODE_BLOCK + AFK_IN_WORLD, AFK_MODE + 4 - AFK_IN_WORLD);
    b.inWorld = mb.u32(0);
    b.mode = mb.u32(AFK_MODE - AFK_IN_WORLD);
    if (b.inWorld != 2 || b.mode == 0xFFFFFFFF) throw Error("No selected in-world player: load into an activity first");
    if (b.mode > 2) {
        b.noLimit = true;    
    } else {
        static constexpr uint32_t offs[3] = {AFK_LIMIT_MODE0, AFK_LIMIT_MODE1, AFK_LIMIT_MODE2};
        static constexpr uint32_t originals[3] = {AFK_ORIGINAL_MODE0, AFK_ORIGINAL_MODE1, AFK_ORIGINAL_MODE2};
        b.limitAddr = b.root + AFK_BLOCK + offs[b.mode];
        b.original = originals[b.mode];
        b.limit = mem.u32(b.limitAddr);
    }
    if (needSlot) {
        b.playerRoot = mem.u32(AFK_SELECTED_PLAYER);
        if (!dataPtr(b.playerRoot, AFK_SLOT + 4)) throw Error("No selected local player (" + hex(b.playerRoot) + ")");
        b.slot = mem.u32(b.playerRoot + AFK_SLOT);
        if (b.slot > 3) throw Error(fmt("No selected player slot (%d)", int32_t(b.slot)));
        b.activityAddr = AFK_PLAYER_BASE + b.slot * AFK_PLAYER_STRIDE + AFK_ACTIVITY;
        const Bytes a = mem.bytes(b.activityAddr, 0x10);
        b.activity = a.u64(0);
        b.elapsed = a.u64(AFK_ELAPSED - AFK_ACTIVITY);
    }
    return b;
}

bool Keeper::heldBy(const std::string& owner) const { return std::find(owners_.begin(), owners_.end(), owner) != owners_.end(); }

std::string Keeper::status() const {
    if (owners_.empty()) return "Off";
    if (!binding_.root) return "Waiting for an activity (arms itself when one loads)";
    if (binding_.noLimit) return "This activity has no AFK limit (" + binding_.modeName() + ")";
    if (method_ == Method::ZeroLimit) return fmt("Kick limit off (the %u-minute limit comes back on stop)", binding_.original / 60000);
    return fmt("Idle timer reset %u time%s (the %u-minute limit is kept)", resets_, resets_ == 1 ? "" : "s", binding_.original / 60000);
}

void Keeper::forget() {
    owners_.clear();
    limitZeroed_ = false;
    binding_ = {};
    resets_ = 0;
    lastNote_.clear();
}

std::string Keeper::acquire(Console& con, Method method, const std::string& owner, uint64_t nowMs) {
    if (!owners_.empty()) {
        if (!heldBy(owner)) owners_.push_back(owner);
        return status();
    }
    Binding b;
    try {
        b = inspect(con, method == Method::ResetIdle);
    } catch (const Error&) {
         
         
        binding_ = {};
        method_ = method;
        resets_ = 0;
        limitZeroed_ = false;
        nextPoll_ = nowMs + AFK_POLL_MS;
        owners_.push_back(owner);
        return status();
    }
    if (!b.noLimit) {
        if (b.limit != b.original && !(method == Method::ResetIdle && b.limit == 0))
            throw Error(fmt("The AFK limit (%u ms) differs from its original value (%u ms): another tool owns it", b.limit, b.original));
        if (method == Method::ZeroLimit) {
            const Binding again = inspect(con, false);
            if (!again.sameBinding(b) || again.limit != b.limit)
                throw Error("The AFK binding changed during inspection: try again");
            con.w32(b.limitAddr, 0);
            limitZeroed_ = true;
        }
    }
    binding_ = b;
    method_ = method;
    resets_ = 0;
    nextPoll_ = nowMs;    
    owners_.push_back(owner);
    return status();
}

std::string Keeper::poll(Console& con, uint64_t nowMs) {
    if (owners_.empty() || nowMs < nextPoll_) return {};
    nextPoll_ = nowMs + AFK_POLL_MS;
    Binding cur;
    try {
        cur = inspect(con, method_ == Method::ResetIdle);
    } catch (const std::exception&) {
        return {};    
    }
    if (!cur.sameBinding(binding_)) {
         
        std::string why;
        if (limitZeroed_ && cur.root == binding_.root) {
            const uint32_t v = con.u32(binding_.limitAddr);
            if (v == 0) con.w32(binding_.limitAddr, binding_.original);
        }
        limitZeroed_ = false;
        binding_ = cur;    
        if (!cur.noLimit && method_ == Method::ZeroLimit) {
            if (cur.limit != cur.original) return once("AFK: the new activity's limit is not its original value; left alone");
            con.w32(cur.limitAddr, 0);
            limitZeroed_ = true;
        }
        lastNote_.clear();
        return "AFK keepalive re-armed for " + cur.modeName();
    }
    if (cur.noLimit) return {};
    if (method_ == Method::ZeroLimit) {
        if (cur.limit == 0) return {};
        if (cur.limit == cur.original) {    
            con.w32(cur.limitAddr, 0);
            limitZeroed_ = true;
            lastNote_.clear();
            return "AFK kick limit turned off again";
        }
        return once(fmt("AFK limit holds %u ms (neither 0 nor the original): left alone", cur.limit));
    }
     
    if (cur.elapsed < AFK_RESET_AT_MS) return {};
    const uint64_t target = cur.activity + cur.elapsed;
    const Bytes fresh = con.bytes(cur.activityAddr, 8);
    if (fresh.u64(0) != cur.activity) return {};    
    con.w64(cur.activityAddr, target, false);
    if (con.u64(cur.activityAddr) < target) throw Error("AFK reset readback mismatch");
    ++resets_;
    return fmt("AFK idle timer reset (was idle %.0f s)", double(cur.elapsed) / 1000.0);
}

std::string Keeper::once(const std::string& line) {
    if (line == lastNote_) return {};
    lastNote_ = line;
    return line;
}

bool Keeper::release(Console& con, const std::string& owner, std::string* why) {
    owners_.erase(std::remove(owners_.begin(), owners_.end(), owner), owners_.end());
    if (!owners_.empty()) return true;
    bool ok = true;
    if (limitZeroed_) {
        try {
            if (con.u32(AFK_CONFIG_ROOT) != binding_.root) throw Error("the AFK configuration moved");
            const uint32_t v = con.u32(binding_.limitAddr);
            if (v == 0) con.w32(binding_.limitAddr, binding_.original);
            else if (v != binding_.original) throw Error(fmt("the limit word holds %u (expected 0 or %u)", v, binding_.original));
        } catch (const std::exception& e) {
            ok = false;
            if (why) *why = std::string("AFK limit restore not verified: ") + e.what();
        }
    }
    limitZeroed_ = false;
    binding_ = {};
    resets_ = 0;
    lastNote_.clear();
    return ok;
}

}   
