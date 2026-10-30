#include "game/loot_console.h"

#include <cstdarg>
#include <cstdio>

#include "core/error.h"
#include "core/runtime.h"

namespace brps3::loot {

void Console::w64(uint32_t addr, uint64_t v, bool verify) {
    std::vector<uint8_t> b(8);
    for (int i = 0; i < 8; ++i) b[size_t(i)] = uint8_t(v >> (56 - 8 * i));
    write(addr, b, verify);
}

std::vector<uint8_t> RuntimeConsole::read(uint32_t addr, uint32_t size) {
    ++reads_;
    return rt_.read(addr, size);
}

void RuntimeConsole::write(uint32_t addr, const std::vector<uint8_t>& data, bool verify) {
    if (data.empty()) return;
    rt_.write(addr, data.data(), data.size(), verify);
}

const world::TagTable& Resolver::table() {
    if (!table_) table_ = world::readTagTable(mem_);
    return *table_;
}

void Resolver::clear() {
    table_.reset();
    pages_.clear();
    bodies_.clear();
}

const Resolver::Page& Resolver::page(uint32_t index) {
    auto it = pages_.find(index);
    if (it != pages_.end()) return it->second;
    Page p;
    const world::TagTable& t = table();
    if (index >= t.pageCount) {
        p.why = "page " + hex(index) + " is beyond the table";
    } else {
        p.desc = world::readPage(mem_, t, index);
        if (!p.desc.live()) p.why = "page " + hex(index) + " is not registered";
        else if (!world::mapped(p.desc.items) || !world::mapped(p.desc.reg, 0x14)) p.why = "page " + hex(index) + " points at unmapped memory";
        else if (p.desc.itemStride == 0) p.why = "page " + hex(index) + " has a zero item stride";
        else {
            p.limit = mem_.u32(p.desc.reg + 0x10);
            p.ok = true;
        }
    }
    return pages_.emplace(index, std::move(p)).first->second;
}

std::optional<uint32_t> Resolver::body(uint32_t handle, bool tagChecks, std::string* why) {
    auto no = [&](std::string w) -> std::optional<uint32_t> {
        if (why) *why = std::move(w);
        return std::nullopt;
    };
    if (!world::validHandle(handle)) return no("not a handle (0 / -1)");
    if (auto it = bodies_.find(handle); it != bodies_.end()) return it->second;
    const uint32_t pi = world::tagPage(handle), index = world::tagIndex(handle);
    const Page& p = page(pi);
    if (!p.ok) return no(p.why);
    if (tagChecks && world::isPackagePage(pi) && p.desc.itemStride != world::kPackageItemStride)
        return no("page " + hex(pi) + " item stride is not 0x10");
    if (p.limit == 0 || index >= p.limit) return no("index " + std::to_string(index) + " is not below the page limit " + std::to_string(p.limit));
    const uint32_t element = p.desc.items + index * p.desc.itemStride;
    uint32_t body = element;
    if (p.desc.mask) {
        const auto b = mem_.tryBytes(element + 8, 4);
        if (!b) return no("element " + hex(element) + " is unreadable");
        body = element - (b->u32(0) & p.desc.mask);
    }
    if (!world::mapped(body)) return no("body " + hex(body) + " is unmapped");
    bodies_[handle] = body;
    return body;
}

std::string fmt(const char* f, ...) {
    char buf[1024];
    va_list a;
    va_start(a, f);
    std::vsnprintf(buf, sizeof buf, f, a);
    va_end(a);
    return buf;
}

std::string hex(uint32_t v) { return world::hex8(v); }

bool heapPtr(uint32_t p, uint32_t size) { return (p & 3) == 0 && world::heap(p, size); }
bool dataPtr(uint32_t p, uint32_t size) { return (p & 3) == 0 && (world::heap(p, size) || world::ebootData(p, size)); }

}   
