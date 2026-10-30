#include "game/lab_observe.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

#include "core/error.h"
#include "core/ppc.h"
#include "core/profile.h"
#include "core/runtime.h"

namespace brps3::lab {

using ppc::Asm;
using world::hex8;

const ObserveFingerprints kDispatchFingerprints = {
    {kSendNow, "7C0802A6487EAEADF821FEF1F8010120"},    
};

const ObserveFingerprints kItemRouteFingerprints = {
    {kItemProducer, "7C0802A64815C699F821FF21F80100F0"},      
    {kItemSubmitSite, "4B62F0FD"},                            
    {kEmitInvestment, "7C0802A648B2D519F821F261F8010DB0"},    
};

 

RingHeader parseRingHeader(const world::Bytes& b) {
    RingHeader h;
    h.enabled = b.u32(0);
    h.lock = b.u32(4);
    h.head = b.u32(8);
    h.tail = b.u32(0xC);
    h.seen = b.u32(0x10);
    h.full = b.u32(0x14);
    h.busy = b.u32(0x18);
    return h;
}

std::vector<uint8_t> ringInit() {
    std::vector<uint8_t> init(kRingHeader, 0);
    init[3] = 1;    
    return init;
}

std::vector<world::Bytes> ringSlots(const world::Bytes& b, uint32_t tail, uint32_t* newTail) {
    const RingHeader h = parseRingHeader(b);
    std::vector<world::Bytes> out;
    uint32_t from = tail;
    if (h.head - tail > kRingSlots) from = h.head - kRingSlots;    
    for (uint32_t k = from; k != h.head; ++k) {
        const uint32_t slot = k & (kRingSlots - 1);
        if (!b.has(kRingHeader + (slot + 1) * kRingSlotSize)) break;
        out.push_back(b.slice(kRingHeader + slot * kRingSlotSize, kRingSlotSize));
    }
    if (newTail) *newTail = h.head;
    return out;
}

namespace {

 
 
 
void ringEnter(Asm& a, uint32_t ctrl, const char* skip) {
    a.addr(11, ctrl);
    a.lwz(12, 0, 11).cmpwi(12, 1).bne(skip);
    a.addi(12, 11, 4).lwarx(0, 0, 12).cmpwi(0, 0).bne("busy").li(0, 1).stwcx(0, 0, 12).bne("busy").sync();
    a.lwz(12, 0x10, 11).addi(12, 12, 1).stw(12, 0x10, 11);                        
    a.lwz(12, 8, 11).lwz(0, 0xC, 11).subf(0, 0, 12).cmplwi(0, kRingSlots).bge("full");
    a.andi_(12, 12, kRingSlots - 1).slwi(12, 12, 7).add(12, 12, 11).addi(12, 12, kRingHeader);
}

void ringLeave(Asm& a, const char* after) {
    a.sync();
    a.lwz(12, 8, 11).addi(12, 12, 1).stw(12, 8, 11);                               
    a.b("unlock");
    a.label("full").lwz(12, 0x14, 11).addi(12, 12, 1).stw(12, 0x14, 11);
    a.label("unlock").sync().li(0, 0).stw(0, 4, 11).b(after);
    a.label("busy").lwz(12, 0x18, 11).addi(12, 12, 1).stw(12, 0x18, 11).b(after);
}

}   

 

std::vector<uint8_t> dispatchStub(uint32_t base, uint32_t ctrl, uint32_t original) {
    static_assert(kRingSlotSize == 0x80, "slwi 7 below");
    Asm a(base);
    ringEnter(a, ctrl, "out");
    a.lwz(0, 0, 4).stw(0, 0, 12);           
    a.stw(3, 4, 12);                        
    a.mflr(0).stw(0, 8, 12);                
    a.lbz(0, 0x130, 4).stw(0, 0xC, 12);     
    a.lwz(0, 0x10, 11).stw(0, 0x10, 12);    
    for (int i = 0; i < 24; ++i) a.lwz(0, 4 + 4 * i, 4).stw(0, 0x20 + 4 * i, 12);    
    ringLeave(a, "out");
    a.label("out").word(original).b(kSendNow + 4);    
    return a.link();
}

DispatchEvent parseDispatchSlot(const world::Bytes& s) {
    DispatchEvent e;
    e.count = s.u32(0);
    e.list = s.u32(4);
    e.caller = s.u32(8);
    e.guard = uint8_t(s.u32(0xC));
    e.seq = s.u32(0x10);
    const uint32_t n = std::min<uint32_t>(e.count, 8);
    for (uint32_t i = 0; i < n; ++i) e.rows.push_back({s.u32(0x20 + i * 12), s.u32(0x24 + i * 12), s.u32(0x28 + i * 12)});
    return e;
}

 

std::vector<uint8_t> itemStub(uint32_t base, uint32_t ctrl, uint32_t site) {
    Asm a(base);
    ringEnter(a, ctrl, "call");
    a.lwz(0, 0, 4).stw(0, 0, 12);           
    a.stw(5, 4, 12);                        
    a.stw(28, 8, 12).stw(29, 0xC, 12).stw(30, 0x10, 12).stw(31, 0x14, 12);    
    a.lwz(0, 0x10, 11).stw(0, 0x18, 12);    
    for (int i = 0; i < 8; ++i) a.lwz(0, 4 * i, 6).stw(0, 0x20 + 4 * i, 12);    
    for (int i = 0; i < 4; ++i) a.lwz(0, 4 * i, 3).stw(0, 0x40 + 4 * i, 12);    
    a.stw(4, 0x50, 12).stw(6, 0x54, 12);
    ringLeave(a, "call");
    a.label("call").bl(kEmitInvestment).b(site + 4);    
    return a.link();
}

ItemEvent parseItemSlot(const world::Bytes& s) {
    ItemEvent e;
    e.root = s.u32(0);
    e.schema = s.u32(4);
    e.a = s.u32(8);
    e.b = s.u32(0xC);
    e.c = s.u32(0x10);
    e.d = s.u32(0x14);
    e.seq = s.u32(0x18);
    for (uint32_t i = 0; i < 0x20; ++i) e.payload[i] = s.u8(0x20 + i);
    for (uint32_t i = 0; i < 4; ++i) e.wrapper[i] = s.u32(0x40 + i * 4);
    e.rowPtr = s.u32(0x50);
    e.payloadPtr = s.u32(0x54);
    return e;
}

 

namespace {

Observer startObserver(Runtime& rt, uint32_t site, uint32_t expect, const std::function<std::vector<uint8_t>(uint32_t, uint32_t, uint32_t)>& stub) {
    Observer o;
    o.site = site;
    if (rt.hooked(site)) throw Error(hex8(site) + " is already hooked by another feature");
    const std::vector<uint8_t> init = ringInit();
    o.ctrl = rt.alloc(kRingSize, init.data(), init.size());
    try {
        const uint32_t ctrl = o.ctrl;
         
         
        const uint32_t size = uint32_t((stub(profile::CAVE + 0x400, ctrl, expect).size() + 15) & ~size_t(15));
        rt.hook(site, [&](uint32_t base, uint32_t original) { return stub(base, ctrl, original); }, expect, size);
    } catch (...) {
        try {
            rt.free(o.ctrl);
        } catch (...) {
        }
        o.ctrl = 0;
        throw;
    }
    return o;
}

}   

Observer startDispatchObserver(Runtime& rt) {
    return startObserver(rt, kSendNow, kSendNowWord, [](uint32_t base, uint32_t ctrl, uint32_t original) { return dispatchStub(base, ctrl, original); });
}

Observer startItemObserver(Runtime& rt) {
    return startObserver(rt, kItemSubmitSite, kItemSubmitWord, [](uint32_t base, uint32_t ctrl, uint32_t) { return itemStub(base, ctrl, kItemSubmitSite); });
}

void stopObserver(Runtime& rt, Observer& o, std::string* error) {
    if (!o.ctrl) return;
    auto note = [&](const std::string& s) {
        if (error && error->empty()) *error = s;
    };
    try {
        rt.writeU32(o.ctrl, 0, false);    
    } catch (const std::exception& e) {
        note(std::string("disable: ") + e.what());
    }
    bool unhooked = true;
    try {
        if (rt.hooked(o.site)) rt.unhook(o.site);
    } catch (const std::exception& e) {
        unhooked = false;
        note(std::string("unhook: ") + e.what());
    }
     
     
    if (unhooked) rt.freeLater(o.ctrl);
    o.ctrl = 0;
    o.tail = 0;
}

std::vector<world::Bytes> pollObserver(Runtime& rt, Observer& o, RingHeader& header) {
    if (!o.ctrl) return {};
    const world::Bytes b(o.ctrl, rt.read(o.ctrl, kRingSize));
    header = parseRingHeader(b);
    uint32_t newTail = o.tail;
    std::vector<world::Bytes> slots = ringSlots(b, o.tail, &newTail);
    if (newTail != o.tail) {
        rt.writeU32(o.ctrl + 0xC, newTail, false);
        o.tail = newTail;
    }
    return slots;
}

 

std::vector<RootCheck> checkItemRoots(world::Reader& mem) {
    std::vector<RootCheck> out;
    const world::Bytes items = mem.bytes(kRootItemAction, 8);
    const world::Bytes gather = mem.bytes(kRootAltGather, 8);
    out.push_back({"item_action", kRootItemAction, 0x63C848A5, items.u32(0)});
    out.push_back({"item_acquired", kRootItemAcquired, 0xE10698B7, items.u32(4)});
    out.push_back({"gather_material", kRootGather, 0x7A0FD954, gather.u32(4)});
    out.push_back({"alternate gather", kRootAltGather, 0xED639013, gather.u32(0)});
    const uint32_t obj = mem.u32(kSchemaGlobalItem);
    uint32_t schema = 0;
    if (world::mapped(obj, 4))
        if (const auto b = mem.tryBytes(obj, 4)) schema = b->u32(0);
    out.push_back({"item schema *(" + hex8(kSchemaGlobalItem) + ")", kSchemaGlobalItem, kSchemaItem, schema});
    return out;
}

std::vector<HashBlockRow> readLootEventHashBlock(world::Reader& mem) {
    const world::Bytes b = mem.bytes(kLootEventHashBlock, kLootEventHashBlockSize);
    std::vector<HashBlockRow> out;
    for (uint32_t o = 0; o + 4 <= kLootEventHashBlockSize; o += 4) out.push_back({o, kLootEventHashBlock + o, b.u32(o)});
    return out;
}

std::vector<SchemaRow> schemaRows() {
    std::ifstream f(world::dataFile("loot/loot_events.tsv"), std::ios::binary);
    if (!f) throw Error("the loot_event catalogue (data/loot/loot_events.tsv) is missing");
    std::string line;
    std::getline(f, line);
    if (line.rfind("\xEF\xBB\xBF", 0) == 0) line = line.substr(3);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    auto split = [](const std::string& s) {
        std::vector<std::string> cells;
        std::stringstream ss(s);
        std::string c;
        while (std::getline(ss, c, '\t')) cells.push_back(c);
        return cells;
    };
    const auto header = split(line);
    auto col = [&](const char* n) -> size_t {
        auto it = std::find(header.begin(), header.end(), n);
        if (it == header.end()) throw Error(std::string("the loot_event catalogue has no column ") + n);
        return size_t(it - header.begin());
    };
    const size_t cHash = col("hash"), cSchema = col("schema"), cGlobal = col("schema_global");
    std::map<uint32_t, SchemaRow> rows;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto cells = split(line);
        if (cells.size() <= std::max({cHash, cSchema, cGlobal}) || cells[cGlobal].empty() || cells[cSchema].empty()) continue;
        try {
            const uint32_t g = uint32_t(std::stoul(cells[cGlobal], nullptr, 16));
            SchemaRow& r = rows[g];
            r.xbox = g;
            r.schema = uint32_t(std::stoul(cells[cSchema], nullptr, 16));
            r.inTable = g >= kXboxSchemaTableBegin && g <= kXboxSchemaTableEnd && (g - kXboxSchemaTableBegin) % 4 == 0;
            r.ps3 = r.inTable ? ps3SchemaGlobal(g) : 0;
            r.loot_events.push_back(uint32_t(std::stoul(cells[cHash], nullptr, 16)));
        } catch (const std::exception&) {
            continue;
        }
    }
    std::vector<SchemaRow> out;
    for (auto& [g, r] : rows)
        if (g) out.push_back(std::move(r));    
    std::stable_sort(out.begin(), out.end(), [](const SchemaRow& a, const SchemaRow& b) { return a.inTable > b.inTable; });
    return out;
}

void readSchemaRows(world::Reader& mem, std::vector<SchemaRow>& rows, const std::function<bool()>& cancelled) {
    const uint32_t first = ps3SchemaGlobal(kXboxSchemaTableBegin), last = ps3SchemaGlobal(kXboxSchemaTableEnd);
    const world::Bytes table = mem.bytes(first, last + 4 - first);
    for (SchemaRow& r : rows) {
        if (!r.inTable) continue;
        if (cancelled && cancelled()) return;
        r.object = table.u32(r.ps3 - first);
        r.actual = 0;
        if (world::mapped(r.object, 4))
            if (const auto b = mem.tryBytes(r.object, 4)) r.actual = b->u32(0);
        r.read = true;
    }
}

}   
