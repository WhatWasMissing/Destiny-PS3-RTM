#include "game/movement_scale.h"

#include <algorithm>
#include <cmath>

#include "core/error.h"
#include "game/world_addr.h"
#include "game/world_objects.h"

namespace brps3::movement {

bool validScale(float s) { return std::isfinite(s) && s >= kScaleMin && s <= kScaleMax; }

std::optional<float> readScale(world::Reader& mem, uint32_t handle) {
    const auto o = world::readSObject(mem, handle);
    if (!o) return std::nullopt;
    return o->scale;
}

float ScaleJournal::set(world::Reader& mem, const WriteFn& write, uint32_t handle, float scale, bool player) {
    if (!validScale(scale)) throw Error("Scale must be between 0.05 and 20.");
    const world::PoolHeader pool = world::readPool(mem, world::addr::SOBJECT_POOL);
    const auto o = world::readSObject(mem, pool, handle);
    if (!o) throw Error("Object " + world::hex8(handle) + " is gone (stale handle or being deleted).");
    if (!std::isfinite(o->scale)) throw Error("Object " + world::hex8(handle) + " +0x9C is not a number: not touching it.");
    float original;
    {
        std::lock_guard<std::mutex> lock(m_);
        auto it = std::find_if(edits_.begin(), edits_.end(), [&](const ScaleEdit& e) { return e.handle == handle && e.entry == o->addr; });
        if (it == edits_.end()) {
            edits_.push_back({handle, o->addr, o->type, o->scale, o->scale, player});
            it = edits_.end() - 1;
        }
        original = it->original;
    }
     
    const auto again = world::readSObject(mem, pool, handle);
    if (!again || again->addr != o->addr) throw Error("Object " + world::hex8(handle) + " changed before the write: nothing was written.");
    write(o->addr + world::sobj::SCALE, world::beF32(scale));
    {
        std::lock_guard<std::mutex> lock(m_);
        for (ScaleEdit& e : edits_)
            if (e.handle == handle && e.entry == o->addr) e.applied = scale;
    }
    return original;
}

bool ScaleJournal::restore(world::Reader& mem, const WriteFn& write, uint32_t handle, std::string* why) {
    std::optional<ScaleEdit> edit;
    {
        std::lock_guard<std::mutex> lock(m_);
        auto it = std::find_if(edits_.begin(), edits_.end(), [&](const ScaleEdit& e) { return e.handle == handle; });
        if (it == edits_.end()) {
            if (why) *why = "nothing to restore";
            return false;
        }
        edit = *it;
    }
    auto drop = [&] {
        std::lock_guard<std::mutex> lock(m_);
        edits_.erase(std::remove_if(edits_.begin(), edits_.end(), [&](const ScaleEdit& e) { return e.handle == handle && e.entry == edit->entry; }),
                     edits_.end());
    };
     
     
    const auto o = world::readSObject(mem, handle);
    if (!o || o->addr != edit->entry) {
        drop();
        if (why) *why = "object " + world::hex8(handle) + " is gone (nothing to restore)";
        return false;
    }
    write(o->addr + world::sobj::SCALE, world::beF32(edit->original));
    drop();
    return true;
}

size_t ScaleJournal::restoreAll(world::Reader& mem, const WriteFn& write, std::vector<std::string>* notes) {
    size_t n = 0;
    for (const ScaleEdit& e : edits()) {
        std::string why;
        try {
            if (restore(mem, write, e.handle, &why)) ++n;
            else if (notes) notes->push_back(why);
        } catch (const std::exception& ex) {
            if (notes) notes->push_back(world::hex8(e.handle) + ": " + ex.what());
        }
    }
    return n;
}

std::vector<ScaleEdit> ScaleJournal::edits() const {
    std::lock_guard<std::mutex> lock(m_);
    return edits_;
}

std::optional<ScaleEdit> ScaleJournal::find(uint32_t handle) const {
    std::lock_guard<std::mutex> lock(m_);
    for (const ScaleEdit& e : edits_)
        if (e.handle == handle) return e;
    return std::nullopt;
}

std::optional<ScaleEdit> ScaleJournal::playerEdit() const {
    std::lock_guard<std::mutex> lock(m_);
    for (const ScaleEdit& e : edits_)
        if (e.player) return e;
    return std::nullopt;
}

void ScaleJournal::forget() {
    std::lock_guard<std::mutex> lock(m_);
    edits_.clear();
}

}   
