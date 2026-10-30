#include "game/inventory_policy.h"

#include <algorithm>
#include <set>

#include "core/error.h"

namespace brps3::inventory {

namespace {

void need(bool ok, const std::string& reason) {
    if (!ok) throw Error(reason);
}

const ItemDef& defOf(const Catalog& cat, uint16_t def) {
    const ItemDef* d = cat.item(def);
    need(d != nullptr, "definition " + std::to_string(def) + " is not in the item catalogue");
    return *d;
}

 
 
bool reviewedStack(const ItemDef& d) {
    if (d.def == 3256) return d.uniq == 0x088156EC && d.maxStack == 5 && d.related == 0;    
    if (d.def == 3621) return d.uniq == 0x030ABA8B && d.maxStack == 1 && d.related == 0;    
    return d.uniq == kDefaultUniq;
}

 
uint64_t capacity(const std::vector<Row>& dst, const Bucket& b, const ItemDef& d) {
    uint64_t total = 0;
    bool free = false;
    for (int i = b.first; i < b.first + b.count; ++i) {
        const Row& r = dst[size_t(i)];
        if (r.empty()) {
            free = true;
            if (d.uniq == kDefaultUniq) total += d.maxStack;
        } else if (r.def == d.def) {
            const uint64_t room = r.quantity < d.maxStack ? d.maxStack - r.quantity : 0;
            if (d.uniq != kDefaultUniq) return room;
            total += room;
        }
    }
    return d.uniq == kDefaultUniq ? total : free ? d.maxStack : 0;
}

void matchingRows(const std::vector<Row>& rows, const Bucket& b, const ItemDef& d) {
    int matches = 0;
    for (int i = b.first; i < b.first + b.count; ++i) {
        const Row& r = rows[size_t(i)];
        if (r.empty() || r.def != d.def) continue;
        ++matches;
        need(r.quantity > 0 && r.quantity <= d.maxStack, "a matching stack exceeds the stack limit");
    }
    if (d.uniq != kDefaultUniq) need(matches <= 1, "several stacks of a unique item are not supported");
}

uint64_t total(const std::vector<Row>& rows, const Bucket& b, uint16_t def) {
    uint64_t n = 0;
    for (int i = b.first; i < b.first + b.count; ++i)
        if (!rows[size_t(i)].empty() && rows[size_t(i)].def == def) n += rows[size_t(i)].quantity;
    return n;
}

 
bool sameRow(const Row& a, const Row& b) { return a.def == b.def && a.soid == b.soid && a.quantity == b.quantity; }

 
 
bool allowedRow(const Row& b, const Row& a, bool src, uint16_t def, uint32_t maximum) {
    if (src) {
        if (b.empty() || b.def != def) return sameRow(a, b);
        if (a.empty()) return true;
        return a.def == def && a.soid == b.soid && a.quantity > 0 && a.quantity <= b.quantity && b.quantity <= maximum;
    }
    if (b.empty()) {
        if (a.empty()) return sameRow(a, b);
        return a.def == def && a.quantity > 0 && a.quantity <= maximum;
    }
    if (b.def != def) return sameRow(a, b);
    return !a.empty() && a.def == def && a.soid == b.soid && b.quantity <= a.quantity && a.quantity <= maximum;
}

 
 
 
std::string stackDeltaProblem(const std::vector<Row>& sb, const std::vector<Row>& sa, const std::vector<Row>& db, const std::vector<Row>& da,
                              const Route& r, const ItemDef& d) {
    const uint32_t maximum = d.maxStack;
    for (size_t i = 0; i < sa.size() && i < sb.size(); ++i) {
        const bool inRoute = r.source.contains(int(i));
        if (inRoute ? !allowedRow(sb[i], sa[i], true, d.def, maximum) : !sameRow(sb[i], sa[i]))
            return "source row " + std::to_string(i) + (inRoute ? " changed in an unexpected way" : " outside the route changed");
    }
    for (size_t i = 0; i < da.size() && i < db.size(); ++i) {
        const bool inRoute = r.destination.contains(int(i));
        if (inRoute ? !allowedRow(db[i], da[i], false, d.def, maximum) : !sameRow(db[i], da[i]))
            return "destination row " + std::to_string(i) + (inRoute ? " changed in an unexpected way" : " outside the route changed");
    }
    for (const auto& [rows, b] : {std::pair{&sa, &r.source}, std::pair{&da, &r.destination}}) {
        int matches = 0;
        for (int i = b->first; i < b->first + b->count; ++i) {
            const Row& row = (*rows)[size_t(i)];
            if (row.empty() || row.def != d.def) continue;
            ++matches;
            if (row.quantity == 0 || row.quantity > maximum) return "a matching stack exceeds the stack limit";
        }
        if (d.uniq != kDefaultUniq && matches > 1) return "several stacks of a unique item";
    }
    return {};
}

bool equipBucket(int key) { return isWeaponBucket(key) || isArmorBucket(key) || key == 12 || key == 13 || key == 14 || key == 25 || key == 27 || key == 28 || key == 29 || key == 35 || key == 6; }

 
uint32_t check(const Snapshot& s, const Item& it, Action a, const Catalog& cat, Route* routeOut) {
    need(s.characterSoid && s.accountSoid, "not signed in");
    const ItemDef& d = defOf(cat, it.def);
    need(it.hash == d.hash, "live item hash " + hex16(it.hash).substr(8) + " does not match the catalogue");
    switch (a) {
    case Action::Deposit:
    case Action::Withdraw: {
        const bool fromCharacter = a == Action::Deposit;
        need(fromCharacter ? it.owner == Owner::Character : it.owner == Owner::Account,
             fromCharacter ? "only items on your character can go to the vault" : "only vault items can go to your character");
        need(!it.equipped, "unequip this item in game before transferring it");
        const Route r = resolveRoute(cat, d, fromCharacter, it.slot);
        if (routeOut) *routeOut = r;
        const std::vector<Row>& dst = fromCharacter ? s.account : s.character;
        if (r.perInstance) {
            need(it.soid != 0 && it.quantity == 1, "not a quantity-one item instance");
            for (const Row& row : dst) need(row.empty() || row.soid != it.soid, "this item is already at the destination");
            bool free = false;
            for (int i = r.destination.first; i < r.destination.first + r.destination.count; ++i) free |= dst[size_t(i)].empty();
            need(free, bucketName(r.destination.key) + " is full");
            return 1;
        }
        need(!d.perInstance && it.soid == 0, "not a common stack");
        need(!r.source.eviction && !r.destination.eviction, "stack transfers from or into this bucket are disabled");
        need(d.maxStack > 0 && d.maxStack < 0x80000000u, "invalid stack limit");
        need(reviewedStack(d), "stack transfers of this item are not reviewed (unique item)");
        matchingRows(fromCharacter ? s.character : s.account, r.source, d);
        matchingRows(dst, r.destination, d);
        const uint64_t room = capacity(dst, r.destination, d);
        need(room > 0, bucketName(r.destination.key) + " has no room for this stack");
        return uint32_t(std::min<uint64_t>(it.quantity, room));
    }
    case Action::Dismantle:
        need(it.owner == Owner::Character, "dismantle works on items on your character (withdraw it first)");
        need(it.soid != 0 && it.quantity == 1, "only single item instances can be dismantled");
        need(!it.equipped, "unequip this item in game before dismantling it");
        need(isWeaponBucket(it.bucket) || isArmorBucket(it.bucket), "only weapons and armor can be dismantled here");
        return 1;
    case Action::Use: {
        need(it.owner == Owner::Character, "only items on your character can be used");
        need(it.soid == 0, "only stacks (consumables) can be used");
        need(it.bucket == 1, "only consumables can be used");
        need(it.quantity >= 1 && it.quantity <= kMaxUseQuantity, "stack quantity outside the reviewed bounds");
        need(it.raw == std::array<uint8_t, 6>{}, "this stack has non-zero reserved bytes (not the reviewed stack shape)");
        int rows = 0;
        for (const Row& r : s.character) rows += !r.empty() && r.def == it.def;
        need(rows == 1, "several stacks of this item on the character are ambiguous");
        for (const PostmasterEntry& p : s.postmaster) need(p.def != it.def, "this item is waiting at the postmaster; pick it up first");
        return it.quantity;
    }
    case Action::Equip:
        need(it.owner == Owner::Character, "only items on your character can be equipped");
        need(it.soid != 0 && it.quantity == 1, "only item instances can be equipped");
        need(!it.equipped, "already equipped");
        need(equipBucket(it.bucket), "this kind of item cannot be equipped");
        return 1;
    case Action::Unequip:
        need(it.owner == Owner::Character && it.soid != 0, "only equipped items on your character can be unequipped");
        need(it.equipped && s.equippedIndex(it.soid) >= 0, "not equipped");
        return 1;
    }
    return 1;
}

const Item* locate(const Snapshot& s, const Plan& p) {
    if (p.soid) return s.find(p.soid);
    return s.at(p.owner, p.slot);
}

}   

const char* actionName(Action a) {
    switch (a) {
    case Action::Deposit: return "Deposit";
    case Action::Withdraw: return "Withdraw";
    case Action::Equip: return "Equip";
    case Action::Unequip: return "Unequip";
    case Action::Dismantle: return "Dismantle";
    case Action::Use: return "Use";
    }
    return "?";
}

const char* actionVerb(Action a) {
    switch (a) {
    case Action::Deposit: return "Transfer to vault";
    case Action::Withdraw: return "Transfer to character";
    case Action::Equip: return "Equip";
    case Action::Unequip: return "Unequip";
    case Action::Dismantle: return "Dismantle";
    case Action::Use: return "Use";
    }
    return "?";
}

Route resolveRoute(const Catalog& cat, const ItemDef& d, bool fromCharacter, int slot) {
    const Bucket* src = cat.bucketAt(!fromCharacter, slot);
    need(src != nullptr, "slot " + std::to_string(slot) + " is outside every bucket");
    const auto key0 = cat.route(src->key);
    need(key0.has_value(), "no native transfer route from " + bucketName(src->key));
    int key = *key0;
    need(!d.compat || key == 255 || key == d.home, "the item's restriction rejects that destination");
    if (key == 255) key = d.home;
    const Bucket* dst = cat.bucket(key);
    need(dst != nullptr && dst->account != src->account, "the route from " + bucketName(src->key) + " does not reach the other holder");
    Route r;
    r.source = *src;
    r.destination = *dst;
    r.home = d.home;
    r.perInstance = d.perInstance;
    r.compat = d.compat;
    return r;
}

std::string Plan::summary() const {
    switch (action) {
    case Action::Deposit: return "Transfer " + name + (stack ? " x" + std::to_string(quantity) : "") + " to the vault";
    case Action::Withdraw: return "Transfer " + name + (stack ? " x" + std::to_string(quantity) : "") + " to your character";
    case Action::Equip: return "Equip " + name;
    case Action::Unequip: return "Unequip " + name;
    case Action::Dismantle: return "Dismantle " + name;
    case Action::Use: return "Use " + name;
    }
    return name;
}

Eligibility eligibility(const Snapshot& s, const Item& item, Action a, const Catalog& cat) {
    Eligibility e;
    try {
        e.maxQuantity = check(s, item, a, cat, nullptr);
        e.ok = true;
    } catch (const std::exception& ex) {
        e.reason = ex.what();
    }
    return e;
}

Plan plan(const Snapshot& s, const Item& item, Action a, uint32_t quantity, const Catalog& cat) {
    Plan p;
    p.action = a;
    p.owner = item.owner;
    p.slot = item.slot;
    p.def = item.def;
    p.hash = item.hash;
    p.soid = item.soid;
    p.sourceQuantity = item.quantity;
    p.stack = item.soid == 0;
    p.manager = s.manager;
    p.descriptorIndex = s.descriptorIndex;
    p.familyVersion = s.familyVersion;
    p.account = s.accountSoid;
    p.character = s.characterSoid;
    const ItemDef* d = cat.item(item.def);
    p.name = d ? d->label() : "definition " + std::to_string(item.def);
    const uint32_t max = check(s, item, a, cat, &p.route);
    if (a == Action::Use) {
        p.quantity = item.quantity;    
    } else if (p.stack && (a == Action::Deposit || a == Action::Withdraw)) {
        need(quantity >= 1 && quantity <= max, "choose a quantity between 1 and " + std::to_string(max));
        p.quantity = quantity;
    } else {
        p.quantity = 1;
    }
    if (a == Action::Unequip) p.equippedIndex = s.equippedIndex(item.soid);
    return p;
}

void validate(const Snapshot& fresh, const Plan& p, const Catalog& cat) {
    need(fresh.accountSoid == p.account && fresh.characterSoid == p.character, "the account or character changed; refresh");
    need(fresh.manager == p.manager, "the profile manager moved; refresh");
    const Item* it = locate(fresh, p);
    need(it != nullptr, p.soid ? "the item is gone" : "the stack is gone");
    need(it->owner == p.owner && it->slot == p.slot && it->def == p.def && it->hash == p.hash, "the item moved; refresh");
    need(it->quantity == p.sourceQuantity, "the stack quantity changed; refresh");
    Route r;
    const uint32_t max = check(fresh, *it, p.action, cat, &r);
    if (p.action == Action::Deposit || p.action == Action::Withdraw) {
        need(r.source.key == p.route.source.key && r.destination.key == p.route.destination.key, "the transfer route changed");
        need(!p.stack || p.quantity <= max, "the destination no longer has room for " + std::to_string(p.quantity));
    }
    if (p.action == Action::Unequip) need(fresh.equippedIndex(p.soid) == p.equippedIndex, "the equipped slot changed; refresh");
}

Outcome reconcile(const Snapshot& before, const Snapshot& after, const Plan& p, const Catalog& cat) {
    Outcome o;
    if (after.accountSoid != before.accountSoid || after.characterSoid != before.characterSoid || after.manager != before.manager) {
        o.state = Outcome::Failed;
        o.text = "the account or character changed before the result was seen";
        return o;
    }
    if (after.familyVersion == before.familyVersion) {
        o.text = "waiting for the inventory to change";
        return o;
    }
    if (int32_t(after.familyVersion - before.familyVersion) < 0) {    
        o.state = Outcome::Failed;
        o.text = "uncertain: the inventory revision went backwards (" + std::to_string(before.familyVersion) + " -> " +
                 std::to_string(after.familyVersion) + "); check your inventory (not retried)";
        return o;
    }
    const bool deposit = p.action == Action::Deposit;
    switch (p.action) {
    case Action::Deposit:
    case Action::Withdraw: {
        const std::vector<Row>& src = deposit ? after.character : after.account;
        const std::vector<Row>& dst = deposit ? after.account : after.character;
        if (!p.stack) {
            for (const Row& r : src)
                if (!r.empty() && r.soid == p.soid) {
                    o.text = "still in the source holder";
                    return o;
                }
            int matches = 0;
            for (size_t i = 0; i < dst.size(); ++i) {
                if (dst[i].empty() || dst[i].soid != p.soid) continue;
                if (dst[i].def != p.def || dst[i].quantity != 1 || !p.route.destination.contains(int(i))) {
                    o.state = Outcome::Failed;
                    o.text = "the item arrived somewhere unexpected (slot " + std::to_string(i) + ")";
                    return o;
                }
                ++matches;
            }
            if (matches == 1) {
                o.state = Outcome::Confirmed;
                o.moved = 1;
                o.text = deposit ? "now in " + bucketName(p.route.destination.key) : "now on your character";
            } else {
                o.text = "not at the destination yet";
            }
            return o;
        }
        const std::vector<Row>& sb = deposit ? before.character : before.account;
        const std::vector<Row>& db = deposit ? before.account : before.character;
        const uint64_t oldS = total(sb, p.route.source, p.def), newS = total(src, p.route.source, p.def);
        const uint64_t oldD = total(db, p.route.destination, p.def), newD = total(dst, p.route.destination, p.def);
        if (!(oldS > newS && newD > oldD)) {
            o.text = "no transfer delta yet";
            return o;
        }
        const uint64_t moved = oldS - newS;
        if (moved != newD - oldD || moved > p.quantity) {
            o.state = Outcome::Failed;
            o.text = "unbalanced stack delta (" + std::to_string(moved) + " left, " + std::to_string(newD - oldD) + " arrived)";
            return o;
        }
        if (const ItemDef* d = cat.item(p.def)) {
            const std::string why = stackDeltaProblem(sb, src, db, dst, p.route, *d);
            if (!why.empty()) {
                o.state = Outcome::Failed;
                o.text = "uncertain: " + why + " at the same time, so the change cannot be attributed to this transfer; check your inventory "
                         "(not retried)";
                return o;
            }
        }
        o.moved = uint32_t(moved);
        o.state = moved == p.quantity ? Outcome::Confirmed : Outcome::Partial;
        o.text = std::to_string(moved) + " of " + std::to_string(p.quantity) + " moved";
        return o;
    }
    case Action::Dismantle:
        if (after.find(p.soid)) {
            o.text = "still in the inventory";
            return o;
        }
        o.state = Outcome::Confirmed;
        o.moved = 1;
        o.text = "dismantled";
        return o;
    case Action::Use: {
        const Row& now = after.character[p.slot];
        uint32_t left = 0;
        if (!now.empty() && now.def == p.def) left = now.quantity;
        else
            for (const Row& r : after.character)
                if (!r.empty() && r.def == p.def) left += r.quantity;
        if (left >= p.sourceQuantity) {
            o.text = "quantity unchanged so far";
            return o;
        }
        o.moved = p.sourceQuantity - left;
        o.state = Outcome::Confirmed;
        o.text = "used " + std::to_string(o.moved) + " (" + std::to_string(left) + " left)";
        return o;
    }
    case Action::Equip:
        if (after.equippedIndex(p.soid) >= 0) {
            o.state = Outcome::Confirmed;
            o.text = "equipped";
        } else {
            o.text = "not in the equipped list yet";
        }
        return o;
    case Action::Unequip:
        if (after.equippedIndex(p.soid) < 0) {
            o.state = Outcome::Confirmed;
            o.text = "unequipped";
        } else {
            o.text = "still equipped";
        }
        return o;
    }
    return o;
}

int usedRows(const Snapshot& s, const Bucket& b) {
    const std::vector<Row>& rows = b.account ? s.account : s.character;
    int n = 0;
    for (int i = b.first; i < b.first + b.count && size_t(i) < rows.size(); ++i) n += !rows[size_t(i)].empty();
    return n;
}

int freeRows(const Snapshot& s, const Bucket& b) { return b.count - usedRows(s, b); }

}   
