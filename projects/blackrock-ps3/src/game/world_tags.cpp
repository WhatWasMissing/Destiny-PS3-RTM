#include "game/world_tags.h"

#include <algorithm>

namespace brps3::world {

TagTable readTagTable(Reader& mem) {
    TagTable t;
    t.manager = mem.u32(addr::TAG_TABLE_PTR);
    if (!validHandle(t.manager) || !mapped(t.manager, 0x14)) throw Error("the tag table pointer at " + hex8(addr::TAG_TABLE_PTR) + " is not set");
    const Bytes m = mem.bytes(t.manager, 0x14);
    t.base = m.u32(0);
    t.end = m.u32(8);
    t.pageStride = m.u32(0x10);
    if (t.pageStride && t.end > t.base) t.pageCount = (t.end - t.base) / t.pageStride;
    if (!t.valid())
        throw Error("the tag table at " + hex8(t.manager) + " is not plausible (base " + hex8(t.base) + ", end " + hex8(t.end) + ", stride " +
                    hex8(t.pageStride) + ")");
    return t;
}

TagPage parsePage(const Bytes& rec, uint32_t page) {
    TagPage p;
    p.page = page;
    p.items = rec.u32(0x10);
    p.reg = rec.u32(0x18);
    p.itemsEnd = rec.u32(0x20);
    p.itemStride = rec.u32(0x30);
    p.mask = rec.u32(0x34);
    return p;
}

TagPage readPage(Reader& mem, const TagTable& t, uint32_t page) {
    if (page >= t.pageCount) throw Error("tag page " + hex8(page) + " is beyond the table (" + std::to_string(t.pageCount) + " pages)");
    return parsePage(mem.bytes(t.base + page * t.pageStride, 0x40), page);
}

std::vector<TagPage> readPages(Reader& mem, const TagTable& t, uint32_t first, uint32_t count) {
    if (first >= t.pageCount) return {};
    const uint32_t n = count ? std::min(count, t.pageCount - first) : t.pageCount - first;
    const Bytes all = mem.bytes(t.base + first * t.pageStride, n * t.pageStride);
    std::vector<TagPage> out;
    out.reserve(n);
    for (uint32_t i = 0; i < n; ++i) out.push_back(parsePage(all.slice(i * t.pageStride, 0x40), first + i));
    return out;
}

Resolved resolve(Reader& mem, const TagTable& t, uint32_t handle, bool tagChecks) {
    Resolved r;
    r.handle = handle;
    r.page = tagPage(handle);
    r.index = tagIndex(handle);
    auto no = [&](std::string why) {
        r.ok = false;
        r.why = std::move(why);
        return r;
    };
    if (!validHandle(handle)) return no("not a handle (0 / -1)");
    if (r.page >= t.pageCount) return no("page " + hex8(r.page) + " is beyond the table (" + std::to_string(t.pageCount) + " pages)");
    r.desc = readPage(mem, t, r.page);
    if (!r.desc.live()) return no("page " + hex8(r.page) + " is not registered (package not loaded)");
    if (!mapped(r.desc.items) || !mapped(r.desc.reg, 0x14)) return no("page " + hex8(r.page) + " points at unmapped memory");
    if (tagChecks && isPackagePage(r.page) && r.desc.itemStride != kPackageItemStride)
        return no("page " + hex8(r.page) + " item stride " + hex8(r.desc.itemStride) + " is not 0x10");
    if (r.desc.itemStride == 0) return no("page " + hex8(r.page) + " has a zero item stride");
    r.limit = mem.u32(r.desc.reg + 0x10);
    if (r.limit == 0 || r.index >= r.limit) return no("index " + std::to_string(r.index) + " is not below the page limit " + std::to_string(r.limit));
    r.element = r.desc.items + r.index * r.desc.itemStride;
    const auto item = mem.tryBytes(r.element, 0x10);
    if (!item) return no("element " + hex8(r.element) + " is unreadable");
    r.item0 = item->u32(0);
    r.item8 = item->u32(8);
    r.body = r.element - (r.item8 & r.desc.mask);
    r.ok = true;
    return r;
}

uint32_t tagOfObject(Reader& mem, const TagTable& t, const SObject& o) {
    if (!validHandle(o.componentHead)) return 0;
    const Resolved r = resolve(mem, t, o.componentHead, false);
    if (!r.ok || !mapped(r.body + 4, 4)) return 0;
    const auto b = mem.tryBytes(r.body + 4, 4);
    return b ? b->u32(0) : 0;
}

Residency checkResident(Reader& mem, const TagTable& t, uint32_t tag) {
    Residency res;
    res.tag = tag;
    if (!isPackagePage(tagPage(tag))) {
        res.why = "not a package tag (page " + hex8(tagPage(tag)) + ")";
        return res;
    }
    const Resolved r = resolve(mem, t, tag, true);
    if (!r.ok) {
        res.why = r.why;
        return res;
    }
    res.element = r.element;
    res.classWord = r.item0;
    res.body = r.body;
    if (r.item0 != kEntityClass) {
        res.why = (r.item0 & 0xFFFF) == 0xFEFE ? "not loaded on the PS3 right now (empty tag-table slot)"
                                                : "item class " + hex8(r.item0) + " is not an entity definition";
        return res;
    }
     
    if (!mapped(r.body, kTagRefOffset + kDefTypeOffset + 1)) {
        res.why = "definition body " + hex8(r.body) + " is unmapped";
        return res;
    }
    res.definition = r.body + kTagRefOffset;
    const auto type = mem.tryBytes(res.definition + kDefTypeOffset, 1);
    if (!type) {
        res.why = "definition " + hex8(res.definition) + " is unreadable";
        return res;
    }
    res.type = type->u8(0);
    if (res.type == 0) {
        res.why = "type byte 0 (definition not loaded)";
        return res;
    }
    res.resident = true;
    return res;
}

std::vector<uint32_t> parseResidentItems(const Bytes& items) {
    std::vector<uint32_t> out;
    for (uint32_t off = 0; off + kPackageItemStride <= items.size(); off += kPackageItemStride)
        if (items.u32(off) == kEntityClass) out.push_back(off / kPackageItemStride);
    return out;
}

ResidentPage scanResidentPage(Reader& mem, const TagPage& page) {
    ResidentPage rp;
    rp.page = page.page;
     
     
    if (!page.live() || page.itemStride != kPackageItemStride || page.itemsEnd <= page.items) return rp;
    const uint32_t bytes = std::min<uint32_t>(page.itemsEnd - page.items, kPackageItems * kPackageItemStride) & ~(kPackageItemStride - 1);
    if (!bytes || !mapped(page.items, bytes)) return rp;
    rp.indices = parseResidentItems(mem.bytes(page.items, bytes));
    return rp;
}

std::vector<ResidentPage> scanResident(Reader& mem, const TagTable& t, const std::function<bool()>& cancelled,
                                       const std::function<void(size_t, size_t)>& progress, size_t maxPages) {
    std::vector<TagPage> pages;
    for (const TagPage& p : readPages(mem, t, kFirstPackagePage))
        if (p.live() && p.itemStride == kPackageItemStride) pages.push_back(p);
    if (maxPages && pages.size() > maxPages) pages.resize(maxPages);
    std::vector<ResidentPage> out;
    for (size_t i = 0; i < pages.size(); ++i) {
        if (cancelled && cancelled()) break;
        ResidentPage rp = scanResidentPage(mem, pages[i]);
        if (!rp.indices.empty()) out.push_back(std::move(rp));
        if (progress) progress(i + 1, pages.size());
    }
    return out;
}

size_t residentCount(const std::vector<ResidentPage>& pages) {
    size_t n = 0;
    for (const auto& p : pages) n += p.indices.size();
    return n;
}

}   
