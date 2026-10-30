#include "game/activity_factions.h"

#include "core/runtime.h"
#include "game/world_mem.h"

namespace brps3::activity {

namespace {

constexpr const char* kJournalKey = "activity.allegiance.journal";
constexpr const char* kPanicKey = "activity.allegiance";

AllegianceJournal& journal(Ctx& c) { return c.state<AllegianceJournal>(kJournalKey); }

world::Allegiance proven(Ctx& c, world::Reader& mem) {
    c.require(world::kFactionFingerprints, "Faction allegiance");
    world::Allegiance a = world::readAllegianceStable(mem);    
    if (!a.owned()) throw Error("allegiance ownership not proven: " + a.proof);
    return a;
}

 
 
void record(Ctx& c, const AllegianceEdit& e) {
    AllegianceJournal& j = journal(c);
    j.edits.push_back(e);
    if (!j.hooked) {
        j.hooked = true;
        c.onDisconnect([](Ctx& cc) { restoreAll(cc); });
    }
    c.onPanic(kPanicKey, [](Ctx& cc) { restoreAll(cc); });
}

uint8_t pairMask(int a, int b) { return uint8_t(1u << (unsigned(world::allegianceBit(a, b)) % 8)); }

 
void writeBlock(Ctx& c, const world::Allegiance& live, const std::array<uint8_t, 64>& to) {
    c.rt().write(live.body, to.data(), to.size(), true);
}

}   

std::string AllegianceEdit::text() const {
    if (kind == Kind::Fill)
        return std::string("all pairs \xE2\x86\x92 ") + (after ? "allied" : "hostile") + " (" + std::to_string(world::allegianceCount(original)) +
               " were allied)";
    return std::to_string(a) + " + " + std::to_string(b) + ": " + (before ? "allied" : "hostile") + " \xE2\x86\x92 " + (after ? "allied" : "hostile");
}

world::Allegiance readAllegiance(Ctx& c) {
    world::RuntimeReader mem(c.rt());
    return proven(c, mem);
}

std::vector<AllegianceEdit> journalEdits(Ctx& c) { return journal(c).edits; }

bool setPair(Ctx& c, int a, int b, bool allied) {
    if (world::allegianceBit(a, b) < 0) throw Error("choose two different factions from 0 to 31");
    world::RuntimeReader mem(c.rt());
    const world::Allegiance live = proven(c, mem);
    if (world::allegiancePair(live.bits, a, b) == allied) return false;    
    const unsigned bit = unsigned(world::allegianceBit(a, b));
    const unsigned byte = world::allegianceByte(bit);
    const uint8_t mask = pairMask(a, b);
    const uint8_t original = live.bits[byte];
    const uint8_t to = allied ? uint8_t(original | mask) : uint8_t(original & ~mask);

    Runtime& rt = c.rt();
    rt.write(live.body + byte, &to, 1, true);
     
    const world::Allegiance after = world::readAllegianceStable(mem);
    if (!after.sameOwner(live) || after.bits[byte] != to) {
         
        rt.write(live.body + byte, &original, 1, false);
        throw Error("allegiance write did not verify (owner or byte changed); restored");
    }
    AllegianceEdit e;
    e.kind = AllegianceEdit::Kind::Pair;
    e.a = a;
    e.b = b;
    e.before = !allied;
    e.after = allied;
    e.body = live.body;
    e.byteIndex = byte;
    e.originalByte = original;
    record(c, e);
    return true;
}

std::array<uint8_t, 64> filledBits(const std::array<uint8_t, 64>& original, bool allied) {
    std::array<uint8_t, 64> out = original;
    for (int a = 0; a < world::kFactionCount; ++a)
        for (int b = a + 1; b < world::kFactionCount; ++b) world::setAllegiancePair(out, a, b, allied);
    return out;
}

std::array<uint8_t, 64> unfilledBits(const std::array<uint8_t, 64>& live, const AllegianceEdit& fill) {
    std::array<uint8_t, 64> out = live;
    for (int a = 0; a < world::kFactionCount; ++a)
        for (int b = a + 1; b < world::kFactionCount; ++b)
            if (world::allegiancePair(live, a, b) == fill.after) world::setAllegiancePair(out, a, b, world::allegiancePair(fill.original, a, b));
    return out;
}

void fillAll(Ctx& c, bool allied) {
    world::RuntimeReader mem(c.rt());
    const world::Allegiance live = proven(c, mem);
    const std::array<uint8_t, 64> to = filledBits(live.bits, allied);
    if (to == live.bits) return;    
    writeBlock(c, live, to);
    const world::Allegiance after = world::readAllegianceStable(mem);
    if (!after.sameOwner(live) || after.bits != to) {
        c.rt().write(live.body, live.bits.data(), live.bits.size(), false);
        throw Error("allegiance fill did not verify (owner or block changed); restored");
    }
    AllegianceEdit e;
    e.kind = AllegianceEdit::Kind::Fill;
    e.after = allied;
    e.body = live.body;
    e.original = live.bits;
    record(c, e);
}

namespace {

 
std::array<uint8_t, 64> undone(const std::array<uint8_t, 64>& live, const AllegianceEdit& e) {
    if (e.kind == AllegianceEdit::Kind::Fill) return unfilledBits(live, e);
    std::array<uint8_t, 64> out = live;
    world::setAllegiancePair(out, e.a, e.b, e.before);
    return out;
}

}   

void restoreEdit(Ctx& c, size_t index) {
    AllegianceJournal& j = journal(c);
    if (index >= j.edits.size()) throw Error("that allegiance edit is gone");
    const AllegianceEdit e = j.edits[index];
    world::RuntimeReader mem(c.rt());
    const world::Allegiance live = proven(c, mem);
    if (live.body != e.body) throw Error("the allegiance block was replaced; restore refused");
     
    const std::array<uint8_t, 64> to = undone(live.bits, e);
    if (to != live.bits) writeBlock(c, live, to);
    j.edits.erase(j.edits.begin() + std::ptrdiff_t(index));
}

void restoreAll(Ctx& c) {
    AllegianceJournal& j = journal(c);
    if (j.edits.empty()) return;
     
     
    world::Allegiance live;
    try {
        world::RuntimeReader mem(c.rt());
        live = world::readAllegiance(mem);
    } catch (const std::exception& ex) {
        c.log(std::string("faction allegiance not restored (block unreadable): ") + ex.what());
        j.edits.clear();
        return;
    }
    std::array<uint8_t, 64> bits = live.bits;
    size_t skipped = 0;
    for (size_t i = j.edits.size(); i > 0; --i) {
        const AllegianceEdit& e = j.edits[i - 1];
        if (!live.owned() || live.body != e.body) {
            ++skipped;
            continue;
        }
        bits = undone(bits, e);
    }
    if (live.owned() && bits != live.bits) {
        try {
            c.rt().write(live.body, bits.data(), bits.size(), false);
        } catch (const std::exception& ex) {
            c.log(std::string("could not restore the faction allegiance: ") + ex.what());
        }
    }
    if (skipped)
        c.log("faction allegiance: " + std::to_string(skipped) + " edit(s) not restored: the block was replaced or its ownership no longer proves (" +
              (live.owned() ? "different block" : live.proof) + ")");
    j.edits.clear();
}

}   
