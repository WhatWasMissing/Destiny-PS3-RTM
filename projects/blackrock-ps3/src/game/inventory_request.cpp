#include "game/inventory_request.h"

#include <chrono>
#include <cstring>
#include <thread>

#include "core/error.h"
#include "core/ppc.h"
#include "core/profile.h"
#include "core/runtime.h"

namespace brps3::inventory {

namespace {

using guard::Cmp;
using guard::Cond;
using guard::Width;

void put32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    b[at] = uint8_t(v >> 24);
    b[at + 1] = uint8_t(v >> 16);
    b[at + 2] = uint8_t(v >> 8);
    b[at + 3] = uint8_t(v);
}
uint32_t get32(const std::vector<uint8_t>& b, size_t at) {
    return uint32_t(b[at]) << 24 | uint32_t(b[at + 1]) << 16 | uint32_t(b[at + 2]) << 8 | b[at + 3];
}

std::vector<uint8_t> soidScratch(uint64_t soid) {
    std::vector<uint8_t> s(16, 0);
    for (int i = 0; i < 8; ++i) s[size_t(i)] = uint8_t(soid >> (56 - 8 * i));
    return s;
}

std::string slotText(int i) { return std::to_string(i); }

}   

const Fingerprints& fingerprintsFor(Action a) {
    switch (a) {
    case Action::Deposit:
    case Action::Withdraw: return kTransferFingerprints;
    case Action::Equip: return kEquipFingerprints;
    case Action::Unequip: return kUnequipFingerprints;
    case Action::Dismantle:
    case Action::Use: return kItemActionFingerprints;
    }
    return kTransferFingerprints;
}

uint32_t nativeFor(Action a) {
    switch (a) {
    case Action::Deposit:
    case Action::Withdraw: return addr::TRANSFER_405;
    case Action::Equip: return addr::EQUIP_403;
    case Action::Unequip: return addr::UNEQUIP_404;
    case Action::Dismantle:
    case Action::Use: return addr::ITEM_ACTION_402;
    }
    return 0;
}

int opFor(Action a) {
    switch (a) {
    case Action::Deposit:
    case Action::Withdraw: return 405;
    case Action::Equip: return 403;
    case Action::Unequip: return 404;
    case Action::Dismantle:
    case Action::Use: return 402;
    }
    return 0;
}

guard::Request buildRequest(const Snapshot& s, const Plan& p) {
    if (s.manager != p.manager || s.familyVersion != p.familyVersion || s.accountSoid != p.account)
        throw Error("the plan was not made from this snapshot");
    guard::Request q(nativeFor(p.action));
    const uint32_t M = addr::PROFILE_MANAGER;
    const uint32_t desc = off::DESCRIPTORS + p.descriptorIndex * off::DESCRIPTOR_SIZE;
     
    q.require(Cond::abs(M, Cmp::Eq, p.manager, Width::U32, 0xFFFFFFFFu, "profile manager unchanged"));
    q.require(Cond::ptr(M, {desc + off::DESC_STATE}, Cmp::Eq, 3, "inventory descriptor ready (state 3)", Width::U8));
    q.require(Cond::ptr(M, {desc + off::DESC_VERSION}, Cmp::Eq, p.familyVersion, "family version unchanged since the preflight"));
    q.require(Cond::ptr(M, {desc + off::DESC_SOID}, Cmp::Eq, uint32_t(p.account >> 32), "same account (high)"));
    q.require(Cond::ptr(M, {desc + off::DESC_SOID + 4}, Cmp::Eq, uint32_t(p.account), "same account (low)"));
     
    const uint32_t row = s.profileOffset(p.owner) + (p.owner == Owner::Account ? off::ACCOUNT_HOLDER : off::CHARACTER_HOLDER) +
                         uint32_t(p.slot) * off::ROW_SIZE;
    q.require(Cond::ptr(M, {row}, Cmp::Eq, p.def, "row " + slotText(p.slot) + " definition", Width::U16));
    q.require(Cond::ptr(M, {row + 8}, Cmp::Eq, uint32_t(p.soid >> 32), "row " + slotText(p.slot) + " SOID (high)"));
    q.require(Cond::ptr(M, {row + 0xC}, Cmp::Eq, uint32_t(p.soid), "row " + slotText(p.slot) + " SOID (low)"));
    q.require(Cond::ptr(M, {row + 0x10}, Cmp::Eq, p.sourceQuantity, "row " + slotText(p.slot) + " quantity"));
    const uint32_t eq = s.profileOffset(Owner::Character) + off::EQUIPPED;
    const bool characterInstance = p.soid && p.owner == Owner::Character;
    if (characterInstance && p.action != Action::Unequip)
        for (uint32_t i = 0; i < off::EQUIPPED_COUNT; ++i)
            q.require(Cond::ptr(M, {eq + i * 8 + 4}, Cmp::Ne, uint32_t(p.soid), "not equipped (equipped slot " + std::to_string(i) + ")"));
    if (p.action == Action::Unequip) {
        if (p.equippedIndex < 0) throw Error("unequip: the item is not in the equipped list");
        q.require(Cond::ptr(M, {eq + uint32_t(p.equippedIndex) * 8}, Cmp::Eq, uint32_t(p.soid >> 32), "equipped (high)"));
        q.require(Cond::ptr(M, {eq + uint32_t(p.equippedIndex) * 8 + 4}, Cmp::Eq, uint32_t(p.soid), "equipped (low)"));
    }
     
    q.scratch = soidScratch(p.action == Action::Use ? 0 : p.soid);
    switch (p.action) {
    case Action::Deposit:
    case Action::Withdraw:
        q.arg(off::TRANSFER_CONTEXT).arg(uint64_t(p.route.source.key)).scratchArg(0).arg(p.def).arg(p.quantity).arg(guard::kMinus1);
        break;
    case Action::Dismantle: q.arg(1).scratchArg(0).arg(p.def).arg(1).arg(guard::kMinus1); break;
    case Action::Use: q.arg(0).scratchArg(0).arg(p.def).arg(p.quantity).arg(guard::kMinus1); break;
    case Action::Equip:
    case Action::Unequip: q.scratchArg(0).arg(guard::kMinus1); break;
    }
    guard::validate(q);
    return q;
}

 

std::vector<uint8_t> gateProgram(uint32_t base) {
    using namespace ppc;
    Asm a(base);
    const int64_t frame = 0x90;
    prologue(a, frame, {31, 30});
    a.mr(31, 3);
    a.lwz(0, 0, 31).load32(30, kGateMagic).cmplw(0, 30).beq("magic");
    a.li(30, int64_t(kGateBadBlock)).b("out");
    a.label("magic");
     
    a.load32(0, addr::PROVIDER_CHECK).mtctr(0).bctrl();
    a.stw(3, 0xC, 31).cmpwi(3, 0).bne("provider");
    a.li(30, int64_t(kGateProviderNull)).b("out");
    a.label("provider");
     
    a.li(3, 0).load32(0, addr::REQUEST_STATE).mtctr(0).bctrl();
    a.stw(3, 0x10, 31).cmpwi(3, 3).beq("ready");
    a.li(30, int64_t(kGateNotReady)).b("out");
    a.label("ready");
     
    a.lwz(3, 4, 31).lwz(0, 8, 31).mtctr(0).bctrl();
    a.mr(30, 3);
    a.label("out");
    a.stw(30, 0x14, 31).mr(3, 30);
    epilogue(a, frame, {31, 30});
    a.blr();
    return a.link();
}

uint32_t installGate(Runtime& rt) {
    const uint32_t size = uint32_t((gateProgram(profile::CAVE).size() + 15) & ~size_t(15));
    return rt.sharedCode(kGateStubName, [](uint32_t base) { return gateProgram(base); }, size);
}

std::vector<uint8_t> encodeGateBlock(uint32_t block, uint32_t guardStub, const guard::Request& req) {
    std::vector<uint8_t> all(kGateBlock + guard::blockSize(req), 0);
    put32(all, 0, kGateMagic);
    put32(all, 4, block + kGateBlock);
    put32(all, 8, guardStub);
    put32(all, 0xC, 0xFFFFFFFFu);
    put32(all, 0x10, 0xFFFFFFFFu);
    put32(all, 0x14, 0xFFFFFFFFu);
    const auto g = guard::encodeBlock(req, block + kGateBlock, true, false);
    std::memcpy(all.data() + kGateBlock, g.data(), g.size());
    return all;
}

SendResult decodeGate(uint32_t outcome, uint32_t provider, uint32_t state, const guard::Request& req) {
    SendResult r;
    r.outcome = outcome;
    r.provider = provider;
    r.state = state;
    switch (outcome & 0xFF) {
    case kGateProviderNull: r.reason = "the investment provider is missing (0x1CB82C returned 0): load a character first"; return r;
    case kGateNotReady:
        r.reason = "the game is not accepting inventory requests right now (0x475D50(0) = " + std::to_string(int32_t(state)) +
                   ", needs 3); nothing was sent";
        return r;
    case kGateBadBlock: r.reason = "the gate stub rejected its parameter block; nothing was sent"; return r;
    default: break;
    }
    r.guard = guard::decodeR3(outcome, req);
    r.called = r.guard.called;
    if (!r.called) r.reason = "refused in the same frame: " + (r.guard.reason.empty() ? std::string(guard::statusName(r.guard.status)) : r.guard.reason);
    return r;
}

SendResult send(Runtime& rt, const guard::Request& req, int timeoutMs) {
    guard::validate(req);
    const uint32_t guardStub = guard::installStub(rt);
    const uint32_t gate = installGate(rt);
    const uint32_t total = kGateBlock + guard::blockSize(req);
    const uint32_t block = rt.alloc(total);
    std::vector<uint8_t> all;
    try {
        all = encodeGateBlock(block, guardStub, req);
        rt.write(block, all.data(), all.size(), true);
    } catch (...) {
        rt.freeLater(block);
        throw;
    }
    CallResult cr;
    try {
        cr = rt.call(gate, {block}, {}, timeoutMs);
    } catch (const std::exception& e) {
        rt.freeLater(block);    
        SendResult r;
        r.uncertain = true;
        r.reason = std::string("no answer from the game thread (") + e.what() +
                   "): the request may still be sent; watching the inventory, never retried";
        return r;
    }
    uint32_t provider = 0, state = 0;
    try {
        const auto back = rt.read(block, kGateBlock);
        provider = get32(back, 0xC);
        state = get32(back, 0x10);
    } catch (const std::exception&) {
    }
    SendResult r = decodeGate(cr.u32(), provider, state, req);
    try {
        rt.free(block);
    } catch (const std::exception&) {
        rt.freeLater(block);
    }
    return r;
}

 

ActionResult sendAction(Runtime& rt, world::Reader& mem, DefinitionCache& cache, const ActionRequest& r, const Catalog& cat,
                        const ActionHooks& hooks) {
    auto progress = [&](const std::string& t) {
        if (hooks.progress) hooks.progress(t);
    };
    auto cancelled = [&] { return hooks.cancelled && hooks.cancelled(); };
    ActionResult out;
    progress("Reading a fresh snapshot");
    ReadOptions ro;
    ro.expectedCharacter = r.expectedCharacter;
    out.before = readSnapshotRetry(mem, cache, ro, 3, cancelled);
    const Item* item = r.soid ? out.before.find(r.soid) : out.before.at(r.owner, r.slot);
    if (!item) throw Error(r.soid ? "the item is no longer in the inventory; refresh" : "the stack is gone; refresh");
    if (item->def != r.def || item->owner != r.owner || item->slot != r.slot) throw Error("the item moved since the page was refreshed; refresh");
    out.plan = plan(out.before, *item, r.action, r.quantity, cat);
    validate(out.before, out.plan, cat);
    if (r.action == Action::Deposit || r.action == Action::Withdraw) {
         
        progress("Checking the item's live definition");
        verifyTransferDefinition(mem, cache, item->def, cat);
    }
    const guard::Request req = buildRequest(out.before, out.plan);
    for (const Cond& c : req.conds) out.conditions.push_back(c.describe());
    if (r.dryRun) {
        out.message = "dry run: " + out.plan.summary() + " would call " + world::hex8(req.native) + " after " + std::to_string(req.conds.size()) +
                      " same-frame checks (nothing sent)";
        return out;
    }
    if (cancelled()) throw Error("cancelled before anything was sent");
    progress("Sending (gated, " + std::to_string(req.conds.size()) + " same-frame checks)");
    out.sent = send(rt, req);
    if (!out.sent.called && !out.sent.uncertain) {
        out.outcome.state = Outcome::Failed;
        out.outcome.text = out.sent.reason;
        out.message = out.plan.summary() + ": " + out.sent.reason;
    }
    return out;
}

bool needsConfirmation(const ActionResult& a) { return a.sent.called || a.sent.uncertain; }

namespace {

ReadOptions confirmRead(const ActionResult& out) {
    ReadOptions ro;
    ro.expectedCharacter = out.before.characterSoid;
    return ro;
}

void finishConfirm(ActionResult& out, const ActionHooks& hooks) {
    out.confirmed = out.outcome.state == Outcome::Confirmed;
    switch (out.outcome.state) {
    case Outcome::Confirmed: out.message = out.plan.summary() + ": confirmed (" + out.outcome.text + ")"; break;
    case Outcome::Partial: out.message = out.plan.summary() + ": partly done (" + out.outcome.text + ")"; break;
    case Outcome::Failed: out.message = out.plan.summary() + ": " + out.outcome.text; break;
    case Outcome::Pending:
        out.message = out.plan.summary() + ": not confirmed within " + std::to_string(hooks.confirmMs / 1000) +
                      " s (the server may have rejected it); not retried";
        break;
    }
}

}   

bool confirmStep(world::Reader& mem, DefinitionCache& cache, ActionResult& out, ConfirmWatch& w, const Catalog& cat, const ActionHooks& hooks) {
    auto progress = [&](const std::string& t) {
        if (hooks.progress) hooks.progress(t);
    };
    if (!w.started) {
        w.started = true;
        w.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(hooks.confirmMs);
        w.seen = out.before.familyVersion;
        progress(out.sent.uncertain ? "Uncertain send; watching the inventory" : "Sent; waiting for the inventory to confirm");
    }
    if (hooks.cancelled && hooks.cancelled()) {
        out.confirmed = false;
        out.message = out.plan.summary() + ": sent, stopped watching (refresh to see the result; not retried)";
        return true;
    }
    const ReadOptions ro = confirmRead(out);
    if (std::chrono::steady_clock::now() >= w.deadline) {
         
        if (out.outcome.state == Outcome::Pending) {
            try {
                out.after = readSnapshotRetry(mem, cache, ro, 2);
                out.haveAfter = true;
                out.outcome = reconcile(out.before, out.after, out.plan, cat);
            } catch (const std::exception&) {
            }
        }
        finishConfirm(out, hooks);
        return true;
    }
    DescriptorState d;
    try {
        d = readDescriptor(mem, out.before);
    } catch (const std::exception&) {
        return false;
    }
    if (!d.sameOwner(out.before)) {
        out.outcome.state = Outcome::Failed;
        out.outcome.text = "the account changed while waiting";
        finishConfirm(out, hooks);
        return true;
    }
    if (d.version == w.seen || d.state != 3) return false;
    w.seen = d.version;
    try {
        out.after = readSnapshot(mem, cache, ro);
        out.haveAfter = true;
    } catch (const std::exception&) {
        w.seen = out.before.familyVersion;    
        return false;
    }
    out.outcome = reconcile(out.before, out.after, out.plan, cat);
    if (out.outcome.state != Outcome::Pending) {
        finishConfirm(out, hooks);
        return true;
    }
    progress("Inventory changed (" + out.outcome.text + "); still waiting");
    return false;
}

void confirmAction(world::Reader& mem, DefinitionCache& cache, ActionResult& out, const Catalog& cat, const ActionHooks& hooks) {
    ConfirmWatch w;
    while (!confirmStep(mem, cache, out, w, cat, hooks)) std::this_thread::sleep_for(std::chrono::milliseconds(hooks.pollMs));
}

ActionResult runAction(Runtime& rt, world::Reader& mem, DefinitionCache& cache, const ActionRequest& r, const Catalog& cat,
                       const ActionHooks& hooks) {
    ActionResult out = sendAction(rt, mem, cache, r, cat, hooks);
    if (!r.dryRun && needsConfirmation(out)) confirmAction(mem, cache, out, cat, hooks);
    return out;
}

 

void verifyTransferDefinition(world::Reader& mem, const DefinitionCache& cache, uint16_t def, const Catalog& cat) {
    const ItemDef* d = cat.item(def);
    if (!d) throw Error("definition " + std::to_string(def) + " is not in the item catalogue");
    if (!cache.valid() || !cache.header || def >= cache.count) throw Error("the live definition table is not loaded; refresh");
    const world::Bytes row = mem.bytes(cache.header + 16 + uint32_t(def) * 12, 12);
    if (row.u32(0) != d->hash)
        throw Error("live definition " + std::to_string(def) + " maps to " + world::hex8(row.u32(0)) + ", not " + world::hex8(d->hash) +
                    " (selected definition mapping changed)");
    const uint32_t tag = row.u32(8);
    uint32_t body = 0;
    try {
        body = resolveInvestmentTag(mem, tag, cache.mode);
    } catch (const SnapshotError& e) {
        throw Error(std::string("the item's definition ") + world::hex8(tag) + " cannot be checked: " + e.what() +
                    (e.kind == FailKind::Transient ? " (load the item in game, e.g. open the character screen, then retry)" : ""));
    }
    const world::Bytes f = mem.bytes(body + 0x10, 0x35);    
    auto at = [](uint32_t o) { return o - 0x10; };
    std::string diff;
    auto field = [&](const char* name, uint32_t live, uint32_t bundled) {
        if (live != bundled) diff += std::string(diff.empty() ? "" : ", ") + name + " " + world::hex8(live) + " (catalogue " + world::hex8(bundled) + ")";
    };
    field("related reference +0x10", f.u32(at(0x10)), d->related);
    field("hash +0x30", f.u32(at(0x30)), d->hash);
    field("uniqueness group +0x38", f.u32(at(0x38)), d->uniq);
    field("max stack +0x3C", f.u32(at(0x3C)), d->maxStack);
    field("home bucket +0x40", f.u8(at(0x40)), d->home);
    field("fallback bucket +0x41", f.u8(at(0x41)), d->fallback);
    field("per instance +0x43", f.u8(at(0x43)), d->perInstance ? 1u : 0u);
    field("compatibility gate +0x44", f.u8(at(0x44)), d->compat ? 1u : 0u);
    if (!diff.empty())
        throw Error("the live definition of " + d->label() + " differs from the catalogue (" + diff + "); transfer refused (selected transfer fields changed)");
}

}   
