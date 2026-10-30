#include "game/loot_poe.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "core/error.h"
#include "core/ppc.h"

namespace brps3::loot::poe {

using world::Bytes;

namespace {

uint32_t be32At(const std::vector<uint8_t>& b, uint32_t off) {
    return uint32_t(b[off]) << 24 | uint32_t(b[off + 1]) << 16 | uint32_t(b[off + 2]) << 8 | b[off + 3];
}

void put32(std::vector<uint8_t>& b, uint32_t off, uint32_t v) {
    b[off] = uint8_t(v >> 24);
    b[off + 1] = uint8_t(v >> 16);
    b[off + 2] = uint8_t(v >> 8);
    b[off + 3] = uint8_t(v);
}

 
std::string definitionProblem(const Bytes& def, Family f) {
    const uint32_t key = f == Family::Main ? POE_MAIN_KEY : POE_SIDE_KEY;
    const float life = f == Family::Main ? POE_MAIN_LIFETIME_S : POE_SIDE_LIFETIME_S;
    if (def.u32(chestdef::ROOT) != POE_ROOT || def.u32(chestdef::KEY) != key) return "PoE chest original gather/key mismatch";
    if (def.f32(chestdef::THRESHOLD) != 1.0f) return fmt("chest open threshold %.3f is not 1.0", double(def.f32(chestdef::THRESHOLD)));
    if (def.f32(chestdef::LIFETIME_S) != life)
        return fmt("%s chest lifetime %.1f s is not %.0f s", familyName(f), double(def.f32(chestdef::LIFETIME_S)), double(life));
    return {};
}

}   

const char* familyName(Family f) { return f == Family::Main ? "main" : "side"; }

std::string Chest::label() const { return family == Family::Main ? "Main chest" : "Side chest"; }

std::string worldStateName(world::Reader& mem, uint32_t state) {
    if (state > 64) return {};
    try {
        const uint32_t p = mem.u32(STATE_NAMES + 4 * state);
        if (!world::mapped(p, 1)) return {};
        return mem.bytes(p, 40).cstr(0, 40);
    } catch (const std::exception&) {
        return {};
    }
}

Context readContext(world::Reader& mem, bool requireInWorld) {
    Context c;
     
    const Bytes blk = mem.bytes(SOBJECT_POOL, SLICE_MANAGER_PTR + 4 - SOBJECT_POOL);
    c.pool = world::parsePool(blk, SOBJECT_POOL);
    c.slices = blk.u32(SLICE_MANAGER_PTR - SOBJECT_POOL);
    c.world = mem.u32(WORLD_PTR);
    if (!heapPtr(c.world, WORLD_STATE + 4)) throw Error("No world loaded (world pointer " + hex(c.world) + "): load into an activity first");
    c.worldState = mem.u32(c.world + WORLD_STATE);
    if (requireInWorld && c.worldState != WORLD_IN_WORLD) {
        c.stateName = worldStateName(mem, c.worldState);
        throw Error(fmt("Not in an activity (world state %u%s%s): load into the Prison of Elders first", c.worldState,
                        c.stateName.empty() ? "" : ", ", c.stateName.c_str()));
    }
    if (!heapPtr(c.slices, SLICE_LOADED + 4)) throw Error("The slice manager is not set (" + hex(c.slices) + ")");
    const Bytes head = mem.bytes(c.slices, 4);
    c.map = head.u32(0);
    c.sliceIndex = mem.u32(c.slices + SLICE_CURRENT);
    c.listRoot = mem.u32(INTERACTABLE_LIST);
    if (!dataPtr(c.listRoot, 8)) throw Error("The interactable list root is not set (" + hex(c.listRoot) + ")");
    c.clock = mem.u32(CLOCK_OBJECT_PTR);
    if (!dataPtr(c.clock, 8)) throw Error("The clock object is not set (" + hex(c.clock) + ")");
    c.scale = int32_t(mem.u32(c.clock + 4));
    if (c.scale != TICKS_PER_SECOND) throw Error(fmt("Unverified clock scale %d (expected 30 ticks per second)", c.scale));
    if (!c.pool.base || c.pool.stride != world::sobj::STRIDE || c.pool.capacity == 0 || c.pool.capacity > 8192 || !c.pool.plausible())
        throw Error(fmt("The sObject pool is not the verified shape (base %s, stride 0x%X, capacity %u)", hex(c.pool.base).c_str(),
                        c.pool.stride, c.pool.capacity));
    return c;
}

Timers parseTimers(const Bytes& rec, uint32_t off) {
    Timers t;
    t.open = rec.u8(off);
    t.looted = rec.u8(off + 1);
    t.countdown = int32_t(rec.u32(off + 2));
    t.authority = rec.u8(off + 6);
    t.lifetime = int32_t(rec.u32(off + 10));
    return t;
}

uint32_t Discovery::mains() const {
    return uint32_t(std::count_if(chests.begin(), chests.end(), [](const Chest& c) { return c.family == Family::Main; }));
}
uint32_t Discovery::sides() const {
    return uint32_t(std::count_if(chests.begin(), chests.end(), [](const Chest& c) { return c.family == Family::Side; }));
}

Discovery discover(world::Reader& mem, Resolver& res, const Cancel& cancel, const Progress& progress) {
    Discovery d;
    d.ctx = readContext(mem);
    const Context& ctx = d.ctx;
    uint32_t link = ctx.listRoot;
    Bytes ref = mem.bytes(link, 8);
    std::set<uint32_t> seen;
    std::map<uint32_t, uint32_t> definitionBases;    
    bool ended = false;
    for (uint32_t step = 0; step < MAX_LIST_NODES; ++step) {
        if (cancel && cancel()) throw Error("Stopped");
        const uint32_t h = ref.u32(0), off = ref.u32(4);
        if (h == 0xFFFFFFFFu) {
            ended = true;
            break;
        }
        if (!seen.insert(link).second) throw Error("The interactable list contains a cycle");
        std::string why;
        const auto comp = res.body(h, false, &why);
        if (!comp) throw Error("Invalid interactable handle " + hex(h) + ": " + why);
        if (off > 0x10000) throw Error(fmt("Implausible interactable list link offset 0x%X", off));
        const uint32_t span = std::max<uint32_t>(chest::READ_SIZE, off + 8 <= 0x400 ? off + 8 : 0);
        const Bytes rec = mem.bytes(*comp, span);
        Node n;
        n.handle = h;
        n.address = *comp;
        n.sequence = rec.u32(chest::SEQUENCE);
        n.classTag = rec.u32(chest::CLASS);
        const Timers t = parseTimers(rec, chest::OPEN);
        n.opened = t.open == 1 && t.authority == 1;
        if (n.opened) {
             
            const uint32_t defOffset = rec.u32(chest::DEF_OFFSET);
            std::optional<uint32_t> base;
            if (auto it = definitionBases.find(n.sequence); it != definitionBases.end()) base = it->second;
            else if (world::validHandle(n.sequence) && (n.sequence & 0x80000000u)) {
                base = res.body(n.sequence, true);
                if (base) definitionBases[n.sequence] = *base;
            }
            if (!base || defOffset > 0x100000) {
                n.note = "opened, no chest definition";
            } else {
                const Bytes def = mem.bytes(*base + defOffset, chestdef::SIZE);
                const uint32_t root = def.u32(chestdef::ROOT), key = def.u32(chestdef::KEY);
                if (root != POE_ROOT || (key != POE_MAIN_KEY && key != POE_SIDE_KEY)) {
                    n.note = fmt("opened, gather root %s key %s (not PoE)", hex(root).c_str(), hex(key).c_str());
                } else {
                    Chest c;
                    c.family = key == POE_MAIN_KEY ? Family::Main : Family::Side;
                    if (const std::string p = definitionProblem(def, c.family); !p.empty()) throw Error(p);
                    c.address = *comp;
                    c.sequence = n.sequence;
                    c.defOffset = defOffset;
                    c.classTag = n.classTag;
                    c.handle = rec.u32(chest::HANDLE);
                    c.owner = rec.u32(chest::OWNER);
                    c.definitionBase = *base;
                    c.definition = *base + defOffset;
                    c.definitionBytes = def.data();
                    c.linkAddress = link;
                    c.link0 = h;
                    c.link1 = off;
                    c.expiry0 = rec.u32(chest::EXPIRY);
                    c.expiry1 = rec.u32(chest::EXPIRY + 4);
                    c.timers = t;
                    if (c.handle != h) throw Error(fmt("Chest component handle %s differs from its list link %s", hex(c.handle).c_str(), hex(h).c_str()));
                    if (!world::validHandle(c.owner) || (c.owner & 0x1FFF) >= ctx.pool.capacity) throw Error("Invalid chest owner " + hex(c.owner));
                    c.ownerGenAddr = ctx.pool.entry(c.owner & 0x1FFF) + world::sobj::HANDLE;
                    if (mem.u32(c.ownerGenAddr) != c.owner) throw Error("Chest owner generation changed (" + hex(c.owner) + ")");
                    if (c.classTag != POE_COMPONENT_CLASS)
                        d.warnings.push_back(fmt("%s component class %s differs from the Xbox 808034A1 (port map: likely)",
                                                 familyName(c.family), hex(c.classTag).c_str()));
                    n.note = std::string(familyName(c.family)) + " chest";
                    d.chests.push_back(std::move(c));
                }
            }
        }
        d.nodes.push_back(n);
        if (progress && d.nodes.size() % 8 == 0) progress(fmt("Walking the interactable list: %zu nodes", d.nodes.size()));
        link = *comp + off;
        ref = rec.has(off, 8) ? rec.slice(off, 8) : mem.bytes(link, 8);
    }
    if (!ended) throw Error(fmt("The interactable list exceeds %u nodes", MAX_LIST_NODES));
     
    if (mem.u32(INTERACTABLE_LIST) != ctx.listRoot) throw Error("The interactable list changed during the scan: try again");
    const world::PoolHeader pool = world::readPool(mem, SOBJECT_POOL);
    if (pool.base != ctx.pool.base || pool.stride != ctx.pool.stride || pool.capacity != ctx.pool.capacity)
        throw Error("The owner pool changed during the scan: try again");
     
    Resolver fresh(mem);
    for (const auto& [sequence, base] : definitionBases)
        if (fresh.body(sequence, true) != std::optional<uint32_t>(base)) throw Error("A chest definition moved during the scan: try again");
    return d;
}

std::vector<Chest> select(const Discovery& d, bool mainOnly) {
    std::vector<Chest> mains, sides;
    for (const Chest& c : d.chests) (c.family == Family::Main ? mains : sides).push_back(c);
    std::vector<Chest> out;
    if (mainOnly) {
        if (mains.size() != 1)
            throw Error(fmt("Open the main PoE chest normally first: expected one opened main chest, found %zu", mains.size()));
        out = mains;
    } else {
        std::set<uint32_t> owners;
        for (const Chest& c : d.chests) owners.insert(c.owner);
        if (mains.size() != 1 || sides.size() != 2 || owners.size() != 3)
            throw Error(fmt("Open all three PoE chests normally: need one main and two side chests with distinct owners "
                            "(found %zu main, %zu side)",
                            mains.size(), sides.size()));
        std::sort(sides.begin(), sides.end(), [](const Chest& a, const Chest& b) { return a.owner < b.owner; });
        out = {mains[0], sides[0], sides[1]};
    }
    for (const Chest& c : out) {
        if (c.timers.looted != 1) throw Error(c.label() + " callback has not settled at 1: wait for its reward, then start");
        if (c.timers.countdown > 0) throw Error(c.label() + " is still opening: wait a moment, then start");
        if (c.timers.lifetime <= MIN_TICKS)
            throw Error(c.label() + fmt(" has %.0f s left: start with more than 30 seconds remaining", double(c.timers.seconds())));
    }
    return out;
}

Timers validate(world::Reader& mem, Resolver& res, const Context& admitted, const Chest& c, bool deep) {
    const Context now = readContext(mem);
    if (!now.sameAs(admitted)) throw Error("The PoE world or slice context changed (left the treasure room?)");
    const Bytes link = mem.bytes(c.linkAddress, 8);
    if (link.u32(0) != c.link0 || link.u32(4) != c.link1) throw Error(c.label() + " left or moved within the interactable list");
    const Bytes rec = mem.bytes(c.address, chest::READ_SIZE);
    if (rec.u32(chest::SEQUENCE) != c.sequence || rec.u32(chest::DEF_OFFSET) != c.defOffset || rec.u32(chest::CLASS) != c.classTag ||
        rec.u32(chest::HANDLE) != c.handle || rec.u32(chest::OWNER) != c.owner)
        throw Error(c.label() + " identity changed");
    if (mem.u32(c.ownerGenAddr) != c.owner) throw Error(c.label() + " owner generation changed");
    if (res.body(c.handle) != std::optional<uint32_t>(c.address)) throw Error(c.label() + " component relocated");
    const Timers t = parseTimers(rec, chest::OPEN);
    if (t.open != 1 || t.authority != 1) throw Error(c.label() + " is no longer opened and authoritative");
    if (t.countdown > 0 || t.lifetime <= 0) throw Error(c.label() + " timer is not settled");
    if (deep) {
        if (rec.u32(chest::EXPIRY) != c.expiry0 || rec.u32(chest::EXPIRY + 4) != c.expiry1) throw Error(c.label() + " expiry reference changed");
        if (res.body(c.sequence, true) != std::optional<uint32_t>(c.definitionBase)) throw Error(c.label() + " definition moved");
        if (mem.read(c.definition, chestdef::SIZE) != c.definitionBytes) throw Error(c.label() + " definition changed");
    }
    return t;
}

std::optional<int32_t> refillTarget(int32_t ticks) {
    if (ticks <= 0) throw Error("Invalid lifetime countdown");
    if (ticks <= MIN_TICKS) throw Error("Too close to expiry: keepalive requires more than 30 seconds");
    if (ticks > REFILL_AT) return std::nullopt;
    return REFILL_TO;
}

void naturalWindow(int32_t previousTicks, int32_t currentTicks, double elapsedSeconds) {
    if (elapsedSeconds < 0) throw Error("Clock moved backwards");
    const double maximum = std::ceil(elapsedSeconds * TICKS_PER_SECOND * 2) + 3;
    const double drop = double(previousTicks) - double(currentTicks);
    if (drop < 0 || drop > maximum) throw Error("Chest countdown changed outside the native decrement window");
}

bool validConfig(const LoopConfig& c, std::string* why) {
    auto no = [&](const char* w) {
        if (why) *why = w;
        return false;
    };
    if (!std::isfinite(c.intervalS) || c.intervalS < 0 || c.intervalS > 3600) return no("Delay must be between 0 and 3600 seconds");
    if (!std::isfinite(c.timeoutS) || c.timeoutS < 0.1 || c.timeoutS > 10) return no("Callback timeout must be 0.1 to 10 seconds");
    return true;
}

 

PcLoop::PcLoop(Context ctx, std::vector<Chest> chests, LoopConfig cfg, uint64_t nowMs)
    : ctx_(std::move(ctx)), chests_(std::move(chests)), cfg_(cfg) {
    const size_t n = chests_.size();
    stats_.chestCycles.assign(n, 0);
    stats_.chestRefills.assign(n, 0);
    stats_.lifetimes.resize(n);
    nextPoll_.resize(n);
    lastPollAt_.assign(n, nowMs);
    lastTicks_.resize(n);
     
    const uint64_t spacing = n ? 5000 / n : 5000;
    for (size_t i = 0; i < n; ++i) {
        lastTicks_[i] = chests_[i].timers.lifetime;
        stats_.lifetimes[i] = lastTicks_[i];
        nextPoll_[i] = lastTicks_[i] <= REFILL_AT ? nowMs : nowMs + spacing * (i + 1);
    }
}

std::string PcLoop::pollTimer(Console& con, size_t i, uint64_t nowMs) {
    Resolver res(con);
    const Chest& c = chests_[i];
    const Timers t = validate(con, res, ctx_, c, true);
    naturalWindow(lastTicks_[i], t.lifetime, double(nowMs - lastPollAt_[i]) / 1000.0);
    lastTicks_[i] = t.lifetime;
    lastPollAt_[i] = nowMs;
    stats_.lifetimes[i] = t.lifetime;
    nextPoll_[i] = nowMs + 5000;
    const auto target = refillTarget(t.lifetime);
    if (!target || !cfg_.keepalive) return {};
     
    Resolver again(con);    
    const Timers fresh = validate(con, again, ctx_, c, false);
    if (refillTarget(fresh.lifetime) != target) throw Error(c.label() + " countdown is no longer eligible for the planned refill");
    con.w32(c.address + chest::LIFETIME, uint32_t(*target), false);
    lastTicks_[i] = *target;
    stats_.lifetimes[i] = *target;
    ++stats_.refills;
    ++stats_.chestRefills[i];
    return fmt("%s timer refilled: %.1f s -> %.0f s", c.label().c_str(), double(fresh.seconds()), double(*target) / TICKS_PER_SECOND);
}

std::string PcLoop::keepalive(Console& con, uint64_t nowMs) {
     
    for (size_t k = 0; k < chests_.size(); ++k)
        if (nowMs >= nextPoll_[k]) return pollTimer(con, k, nowMs);
    return {};
}

std::string PcLoop::step(Console& con, uint64_t nowMs) {
    replayStage_ = false;
    if (stats_.done || chests_.empty()) return {};
     
    if (std::string r = keepalive(con, nowMs); !r.empty()) return r;
    replayStage_ = true;
    const size_t i = stats_.current % chests_.size();
    const Chest& c = chests_[i];
    if (!stats_.pending) {
        if (nowMs < nextWriteAt_) return {};
        Resolver res(con);
        const Timers t = validate(con, res, ctx_, c, false);
        if (t.looted != 1) throw Error("Callback byte changed before reset; stopping");
        stats_.lifetimes[i] = t.lifetime;
        stats_.pending = true;
        pendingSince_ = nowMs;
        con.w8(c.address + chest::LOOTED, 0, false);    
        ++stats_.writes;
        return {};
    }
    const uint8_t v = con.u8(c.address + chest::LOOTED);
    if (v == 0) {
        if (double(nowMs - pendingSince_) > cfg_.timeoutS * 1000.0) throw Error("Native update did not consume the reset; no retry");
        return {};
    }
    if (v != 1) throw Error(fmt("Unexpected callback-complete byte %u", v));
    Resolver res(con);
    const Timers t = validate(con, res, ctx_, c, false);
    if (t.looted != 1) throw Error("Callback completion changed during validation");
    stats_.pending = false;
    ++stats_.cycles;
    ++stats_.chestCycles[i];
    stats_.lifetimes[i] = t.lifetime;
    stats_.current = uint32_t((i + 1) % chests_.size());
    nextWriteAt_ = nowMs + uint64_t(cfg_.intervalS * 1000.0);
    if (cfg_.cycleLimit && stats_.cycles >= cfg_.cycleLimit) stats_.done = true;
    return fmt("Cycle %u: %s callback returned to 1", stats_.cycles, c.label().c_str());
}

bool PcLoop::restore(Console& con, std::string* why) {
    if (!stats_.pending) return true;
    const Chest& c = chests_[stats_.current % chests_.size()];
    try {
        Resolver res(con);
        const Timers t = validate(con, res, ctx_, c, false);
        if (t.looted == 0) con.w8(c.address + chest::LOOTED, 1, false);
        if (validate(con, res, ctx_, c, false).looted != 1) throw Error("completion byte restoration failed");
        stats_.pending = false;
        return true;
    } catch (const std::exception& e) {
        if (why) *why = std::string("Cleanup not verified: ") + e.what() + ". No stale-address write attempted.";
        return false;
    }
}

 

const char* faultText(uint32_t code) {
    switch (code) {
    case kFaultNone: return "none";
    case kFaultSlices: return "the slice manager changed (left the activity?)";
    case kFaultMap: return "the map changed (left the treasure room?)";
    case kFaultWorld: return "the world object changed";
    case kFaultState: return "the world left the in-activity state";
    case kFaultIdentity: return "a selected chest's identity changed";
    case kFaultOwner: return "a chest owner's generation changed";
    case kFaultLink: return "a chest left or moved within the interactable list";
    case kFaultNotOpen: return "a chest is no longer opened and authoritative";
    case kFaultCountdown: return "a chest's open countdown is running again";
    case kFaultExpiry: return "a chest is within 30 s of expiry";
    case kFaultByte: return "unexpected callback-complete byte";
    case kFaultTimeout: return "the native update did not consume the reset in time";
    case kFaultNotSettled: return "the callback byte was not settled at 1 before the reset";
    default: return "unknown fault";
    }
}

uint32_t framesFor(double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0) return 0;
    return uint32_t(std::lround(seconds * TICKS_PER_SECOND));
}

std::vector<uint8_t> taskProgram(uint32_t base) {
    using namespace ctl;
    ppc::Asm a(base);
     
    a.lwz(0, ENABLED, 3).cmpwi(0, 1).bne("ret");
    a.lwz(5, FRAMES, 3).addi(5, 5, 1).stw(5, FRAMES, 3);
     
    a.load32(6, SLICE_MANAGER_PTR).lwz(5, 0, 6).lwz(6, CTX_SLICES, 3).li(7, kFaultSlices).cmplw(5, 6).bne("fault");
    a.lwz(5, 0, 5).lwz(6, CTX_MAP, 3).li(7, kFaultMap).cmplw(5, 6).bne("fault");
    a.load32(6, WORLD_PTR).lwz(5, 0, 6).lwz(6, CTX_WORLD, 3).li(7, kFaultWorld).cmplw(5, 6).bne("fault");
    a.lwz(5, int64_t(WORLD_STATE), 5).li(7, kFaultState).cmpwi(5, int64_t(WORLD_IN_WORLD)).bne("fault");
     
     
    a.lwz(8, COUNT, 3).addi(4, 3, CHESTS);
    a.label("chest");
    a.lwz(9, C_ADDR, 4);
    a.li(7, kFaultIdentity);
    a.lwz(5, chest::SEQUENCE, 9).lwz(6, C_SEQ, 4).cmplw(5, 6).bne("fault");
    a.lwz(5, chest::DEF_OFFSET, 9).lwz(6, C_DEFOFF, 4).cmplw(5, 6).bne("fault");
    a.lwz(5, chest::CLASS, 9).lwz(6, C_CLASS, 4).cmplw(5, 6).bne("fault");
    a.lwz(5, chest::HANDLE, 9).lwz(6, C_HANDLE, 4).cmplw(5, 6).bne("fault");
    a.lwz(5, chest::OWNER, 9).lwz(6, C_OWNER, 4).cmplw(5, 6).bne("fault");
    a.li(7, kFaultOwner).lwz(10, C_GEN, 4).lwz(5, 0, 10).cmplw(5, 6).bne("fault");
    a.li(7, kFaultLink).lwz(10, C_LINK, 4).lwz(5, 0, 10).lwz(6, C_LINK0, 4).cmplw(5, 6).bne("fault");
    a.lwz(5, 4, 10).lwz(6, C_LINK1, 4).cmplw(5, 6).bne("fault");
    a.li(7, kFaultNotOpen).lbz(5, chest::OPEN, 9).cmpwi(5, 1).bne("fault");
    a.lbz(5, chest::AUTHORITY, 9).cmpwi(5, 1).bne("fault");
    a.li(7, kFaultCountdown).lwz(5, chest::COUNTDOWN, 9).cmpwi(5, 0).bgt("fault");
    a.lwz(5, chest::LIFETIME, 9).stw(5, C_LIFETIME, 4);
    a.li(7, kFaultExpiry).cmpwi(5, MIN_TICKS).ble("fault");
    a.cmpwi(5, REFILL_AT).bgt("next");
    a.lwz(0, KEEPALIVE, 3).cmpwi(0, 0).beq("next");
    a.li(5, REFILL_TO).stw(5, chest::LIFETIME, 9).stw(5, C_LIFETIME, 4);
    a.lwz(5, C_REFILLS, 4).addi(5, 5, 1).stw(5, C_REFILLS, 4);
    a.lwz(5, REFILLS, 3).addi(5, 5, 1).stw(5, REFILLS, 3);
    a.label("next");
    a.addi(4, 4, CHEST).addi(8, 8, -1).cmpwi(8, 0).bgt("chest");
     
    a.lwz(10, CURRENT, 3).mulli(10, 10, CHEST).add(4, 3, 10).addi(4, 4, CHESTS);
    a.lwz(9, C_ADDR, 4).lbz(5, chest::LOOTED, 9);
    a.lwz(0, PENDING, 3).cmpwi(0, 0).beq("idle");
     
    a.cmpwi(5, 1).beq("consumed");
    a.li(7, kFaultByte).cmpwi(5, 0).bne("fault");
    a.lwz(5, WAIT, 3).addi(5, 5, 1).stw(5, WAIT, 3);
    a.lwz(6, TIMEOUT, 3).li(7, kFaultTimeout).cmplw(5, 6).bgt("fault");
    a.blr();
    a.label("consumed");
    a.li(0, 0).stw(0, PENDING, 3);
    a.lwz(5, CYCLES, 3).addi(5, 5, 1).stw(5, CYCLES, 3);
    a.lwz(5, C_CYCLES, 4).addi(5, 5, 1).stw(5, C_CYCLES, 4);
    a.lwz(5, CURRENT, 3).addi(5, 5, 1).lwz(6, COUNT, 3).cmplw(5, 6).blt("store");
    a.li(5, 0);
    a.label("store").stw(5, CURRENT, 3);
    a.lwz(6, LIMIT, 3).cmpwi(6, 0).beq("pace");
    a.lwz(7, CYCLES, 3).cmplw(7, 6).blt("pace");
    a.li(0, 0).stw(0, ENABLED, 3).li(0, int64_t(TaskStatus::Done)).stw(0, STATUS, 3).blr();
     
     
     
    a.label("pace");
    a.lwz(6, INTERVAL, 3).cmpwi(6, 0).bgt("delay");
    a.mulli(10, 5, CHEST).add(4, 3, 10).addi(4, 4, CHESTS);
    a.lwz(9, C_ADDR, 4).lbz(5, chest::LOOTED, 9).b("write");
    a.label("delay");
    a.addi(6, 6, -1).stw(6, DELAY, 3).blr();
    a.label("idle");
    a.lwz(6, DELAY, 3).cmpwi(6, 0).ble("write");
    a.addi(6, 6, -1).stw(6, DELAY, 3).blr();
    a.label("write");
    a.li(7, kFaultNotSettled).cmpwi(5, 1).bne("fault");
    a.li(0, 0).stb(0, chest::LOOTED, 9);
    a.li(0, 1).stw(0, PENDING, 3).li(0, 0).stw(0, WAIT, 3);
    a.lwz(5, WRITES, 3).addi(5, 5, 1).stw(5, WRITES, 3);
    a.label("ret").blr();
    a.label("fault");
    a.stw(7, FAULT, 3).li(0, 0).stw(0, ENABLED, 3).li(0, int64_t(TaskStatus::Faulted)).stw(0, STATUS, 3).blr();
    return a.link();
}

std::vector<uint8_t> encodeControl(const Context& ctx, const std::vector<Chest>& chests, const LoopConfig& cfg, bool enabled) {
    using namespace ctl;
    if (chests.empty() || chests.size() > 3) throw Error("1 to 3 chests");
    std::vector<uint8_t> b(SIZE, 0);
    put32(b, MAGIC, MAGIC_VALUE);
    put32(b, ENABLED, enabled ? 1 : 0);
    put32(b, STATUS, enabled ? uint32_t(TaskStatus::Running) : uint32_t(TaskStatus::Idle));
    put32(b, INTERVAL, framesFor(cfg.intervalS));
    put32(b, TIMEOUT, std::max<uint32_t>(3, framesFor(cfg.timeoutS)));
    put32(b, COUNT, uint32_t(chests.size()));
    put32(b, LIMIT, cfg.cycleLimit);
    put32(b, CTX_SLICES, ctx.slices);
    put32(b, CTX_MAP, ctx.map);
    put32(b, CTX_WORLD, ctx.world);
    put32(b, KEEPALIVE, cfg.keepalive ? 1 : 0);
    for (size_t i = 0; i < chests.size(); ++i) {
        const Chest& c = chests[i];
        const uint32_t o = CHESTS + uint32_t(i) * CHEST;
        put32(b, o + C_ADDR, c.address);
        put32(b, o + C_SEQ, c.sequence);
        put32(b, o + C_DEFOFF, c.defOffset);
        put32(b, o + C_HANDLE, c.handle);
        put32(b, o + C_OWNER, c.owner);
        put32(b, o + C_GEN, c.ownerGenAddr);
        put32(b, o + C_LINK, c.linkAddress);
        put32(b, o + C_LINK0, c.link0);
        put32(b, o + C_LINK1, c.link1);
        put32(b, o + C_LIFETIME, uint32_t(c.timers.lifetime));
        put32(b, o + C_CLASS, c.classTag);
    }
    return b;
}

TaskState decodeControl(const std::vector<uint8_t>& b) {
    using namespace ctl;
    TaskState s;
    if (b.size() < SIZE || be32At(b, MAGIC) != MAGIC_VALUE) return s;
    s.valid = true;
    s.enabled = be32At(b, ENABLED) == 1;
    s.status = TaskStatus(std::min<uint32_t>(be32At(b, STATUS), 3));
    s.fault = be32At(b, FAULT);
    s.cycles = be32At(b, CYCLES);
    s.writes = be32At(b, WRITES);
    s.refills = be32At(b, REFILLS);
    s.frames = be32At(b, FRAMES);
    s.current = be32At(b, CURRENT);
    s.pending = be32At(b, PENDING) != 0;
    s.wait = be32At(b, WAIT);
    s.count = std::min<uint32_t>(be32At(b, COUNT), 3);
    for (uint32_t i = 0; i < s.count; ++i) {
        const uint32_t o = CHESTS + i * CHEST;
        s.chests.push_back({int32_t(be32At(b, o + C_LIFETIME)), be32At(b, o + C_REFILLS), be32At(b, o + C_CYCLES)});
    }
    return s;
}

}   
