#include "game/lab_library.h"

#include <windows.h>
#include <compressapi.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "core/error.h"
#include "game/names_catalog.h"
#include "game/world_mem.h"

#pragma comment(lib, "Cabinet.lib")

namespace brps3::lab {

namespace {

constexpr const char* kHeader = "System\tKind\tID\tNamespace\tTag\tName\tEvidence\tPackage\tDetails";

char lowerChar(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }

std::string lowerAscii(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = lowerChar(c);
    return out;
}

void appendUnescapedLower(std::string& out, std::string_view raw) {
    for (size_t i = 0; i < raw.size(); ++i) {
        char c = raw[i];
        if (c == '\\' && i + 1 < raw.size()) {
            const char n = raw[++i];
            c = n == 'n' ? '\n' : n == 'r' ? '\r' : n == 't' ? '\t' : n;
        }
        out.push_back(lowerChar(c));
    }
}

bool parseHexTag(std::string_view s, uint32_t& out) {
    if (s.empty() || s.size() > 8) return false;
    uint32_t v = 0;
    for (char c : s) {
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        v = v << 4 | uint32_t(d);
    }
    out = v;
    return true;
}

std::string readFileText(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw Error("cannot read " + p.string());
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string keyOf(const std::string& system, const std::string& kind, const std::string& id) {
     
    const std::string k = system == "Encounters and scripts" && kind == "AI squad controller" ? std::string("AI squad") : kind;
    return system + "/" + k + "/" + id;
}

bool atomicWrite(const std::filesystem::path& path, const std::string& text, std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << text;
        if (!f) {
            error = "cannot write " + tmp.string();
            return false;
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        error = "cannot replace " + path.string() + ": " + ec.message();
        return false;
    }
    return true;
}

}   

std::string unescapeCell(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        char c = raw[i];
        if (c == '\\' && i + 1 < raw.size()) {
            const char n = raw[++i];
            c = n == 'n' ? '\n' : n == 'r' ? '\r' : n == 't' ? '\t' : n;
        }
        out.push_back(c);
    }
    return out;
}

std::vector<std::string> searchWords(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] {
        if (cur.size() > 2 && cur[0] == '0' && cur[1] == 'x') cur = cur.substr(2);
        if (!cur.empty()) out.push_back(cur);
        cur.clear();
    };
    for (char c : text) {
        if (c == ' ' || c == ',' || c == '\t' || c == '\n') flush();
        else cur.push_back(lowerChar(c));
    }
    flush();
    return out;
}

 

std::shared_ptr<Library> Library::parse(std::string text) {
    auto lib = std::make_shared<Library>();
    lib->text_ = std::move(text);
    const std::string& t = lib->text_;
    size_t pos = 0;
    if (t.rfind("\xEF\xBB\xBF", 0) == 0) pos = 3;
    auto nextLine = [&](std::string_view& line) {
        if (pos >= t.size()) return false;
        size_t end = t.find('\n', pos);
        if (end == std::string::npos) end = t.size();
        size_t stop = end;
        if (stop > pos && t[stop - 1] == '\r') --stop;
        line = std::string_view(t).substr(pos, stop - pos);
        pos = end + 1;
        return true;
    };
    std::string_view line;
    if (!nextLine(line) || line != kHeader) throw Error("the control catalogue header is not the Xbox Systems library format");
    lib->lower_.reserve(t.size());
    std::set<std::string> systems;
    std::map<std::string, std::set<std::string>> kinds;
    size_t lineNo = 1;
    while (nextLine(line)) {
        ++lineNo;
        if (line.empty()) continue;
        Row r;
        size_t cell = 0, start = 0;
        for (size_t i = 0; i <= line.size(); ++i) {
            if (i == line.size() || line[i] == '\t') {
                if (cell >= size_t(Col::Count)) throw Error("control catalogue row " + std::to_string(lineNo) + " has more than 9 cells");
                r.off[cell] = uint32_t((line.data() - t.data()) + start);
                r.len[cell] = uint32_t(i - start);
                ++cell;
                start = i + 1;
            }
        }
        if (cell != size_t(Col::Count)) throw Error("control catalogue row " + std::to_string(lineNo) + " has " + std::to_string(cell) + " cells, not 9");
        auto cellView = [&](Col c) { return std::string_view(t).substr(r.off[int(c)], r.len[int(c)]); };
        const std::string system = unescapeCell(cellView(Col::System)), kind = unescapeCell(cellView(Col::Kind)),
                          id = unescapeCell(cellView(Col::Id)), ns = unescapeCell(cellView(Col::Namespace));
        if (system.empty() || kind.empty() || id.empty() || ns.empty())
            throw Error("control catalogue row " + std::to_string(lineNo) + " has no identity (system, kind, id, namespace)");
        const std::string key = keyOf(system, kind, id);
        if (!lib->keys_.emplace(key, lib->rows_.size()).second) throw Error("control catalogue row " + std::to_string(lineNo) + " repeats " + key);
        const std::string_view tagCell = cellView(Col::Tag);
        if (!tagCell.empty() && !parseHexTag(tagCell, r.tag)) throw Error("control catalogue row " + std::to_string(lineNo) + " has an invalid tag");
        if (system == "AI squads" && kind == "Squad definition") {
            const std::string details = unescapeCell(cellView(Col::Details));
            std::istringstream ls(details);
            std::string dl;
            while (std::getline(ls, dl)) {
                if (dl.rfind("Entity references: ", 0) != 0) continue;
                std::istringstream vs(dl.substr(19));
                std::string v;
                while (std::getline(vs, v, ',')) {
                    uint32_t ref = 0;
                    if (v.size() != 8 || !parseHexTag(v, ref) || ref < 0x80800000u || ref > 0x81FFFFFFu)
                        throw Error("control catalogue row " + std::to_string(lineNo) + " has an invalid entity reference");
                    r.refs.push_back(ref);
                }
            }
        }
        r.hay = uint32_t(lib->lower_.size());
        for (int c = 0; c < int(Col::Count); ++c) {
            if (c) lib->lower_.push_back('\t');
            appendUnescapedLower(lib->lower_, cellView(Col(c)));
        }
        r.hayLen = uint32_t(lib->lower_.size() - r.hay);
        if (r.tag) lib->byTag_.emplace(r.tag, lib->rows_.size());
        systems.insert(system);
        kinds[system].insert(kind);
        ++lib->systemCounts_[system];
        lib->rows_.push_back(std::move(r));
    }
    lib->systems_.assign(systems.begin(), systems.end());
    for (auto& [s, ks] : kinds) lib->kinds_[s].assign(ks.begin(), ks.end());
    return lib;
}

std::string_view Library::raw(size_t row, Col c) const {
    const Row& r = rows_[row];
    return std::string_view(text_).substr(r.off[int(c)], r.len[int(c)]);
}

std::string Library::text(size_t row, Col c) const { return unescapeCell(raw(row, c)); }

std::string Library::key(size_t row) const { return keyOf(text(row, Col::System), text(row, Col::Kind), text(row, Col::Id)); }

std::vector<std::string> Library::kinds(const std::string& system) const {
    if (!system.empty()) {
        auto it = kinds_.find(system);
        return it == kinds_.end() ? std::vector<std::string>{} : it->second;
    }
    std::set<std::string> all;
    for (const auto& [s, ks] : kinds_) all.insert(ks.begin(), ks.end());
    return {all.begin(), all.end()};
}

size_t Library::count(const std::string& system) const {
    if (system.empty()) return rows_.size();
    auto it = systemCounts_.find(system);
    return it == systemCounts_.end() ? 0 : it->second;
}

void Library::addAlias(size_t row, const std::string& text) {
    if (text.empty() || row >= rows_.size()) return;
    std::string& a = aliases_[row];
    if (!a.empty()) a.push_back(' ');
    a += lowerAscii(text);
}

std::vector<uint32_t> Library::filter(const Query& q, const std::function<const std::string*(uint32_t)>& extra,
                                      const std::function<bool(uint32_t)>& keep, const std::function<bool()>& cancelled, size_t limit) const {
    const std::vector<std::string> words = searchWords(q.text);
    std::vector<uint32_t> out;
    const std::string_view lower(lower_);
    for (uint32_t i = 0; i < uint32_t(rows_.size()); ++i) {
        if ((i & 4095) == 0 && cancelled && cancelled()) return {};
        if (!q.system.empty() && raw(i, Col::System) != q.system) continue;
        if (!q.kind.empty() && raw(i, Col::Kind) != q.kind) continue;
        const Row& r = rows_[i];
        if (q.entityRef && std::find(r.refs.begin(), r.refs.end(), q.entityRef) == r.refs.end()) continue;
        if (!words.empty()) {
            const std::string_view hay = lower.substr(r.hay, r.hayLen);
            const std::string* alias = nullptr;
            if (!aliases_.empty()) {
                auto it = aliases_.find(i);
                if (it != aliases_.end()) alias = &it->second;
            }
            const std::string* ex = extra ? extra(i) : nullptr;
            bool all = true;
            for (const std::string& w : words) {
                if (hay.find(w) != std::string_view::npos) continue;
                if (alias && alias->find(w) != std::string::npos) continue;
                if (ex && ex->find(w) != std::string::npos) continue;
                all = false;
                break;
            }
            if (!all) continue;
        }
        if (keep && !keep(i)) continue;
        out.push_back(i);
        if (limit && out.size() >= limit) break;
    }
    return out;
}

std::optional<size_t> Library::findKey(const std::string& key) const {
    auto it = keys_.find(key);
    if (it == keys_.end()) return std::nullopt;
    return it->second;
}

std::vector<size_t> Library::rowsWithTag(uint32_t tag) const {
    std::vector<size_t> out;
    auto [a, b] = byTag_.equal_range(tag);
    for (auto it = a; it != b; ++it) out.push_back(it->second);
    std::sort(out.begin(), out.end());
    return out;
}

 

std::string mszipDecompress(const std::vector<uint8_t>& blob) {
    DECOMPRESSOR_HANDLE h = nullptr;
    if (!CreateDecompressor(COMPRESS_ALGORITHM_MSZIP, nullptr, &h)) throw Error("the Windows decompressor is unavailable");
    struct Close {
        DECOMPRESSOR_HANDLE h;
        ~Close() { CloseDecompressor(h); }
    } close{h};
    SIZE_T need = 0;
    if (!Decompress(h, blob.data(), blob.size(), nullptr, 0, &need) && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        throw Error("the Systems library data is not MSZIP data");
    if (need == 0 || need > (SIZE_T(1) << 30)) throw Error("the Systems library data has an implausible size");
    std::string out(need, '\0');
    SIZE_T got = 0;
    if (!Decompress(h, blob.data(), blob.size(), out.data(), out.size(), &got)) throw Error("the Systems library data is corrupt");
    out.resize(got);
    return out;
}

std::vector<uint8_t> mszipCompress(const std::string& text) {
    COMPRESSOR_HANDLE h = nullptr;
    if (!CreateCompressor(COMPRESS_ALGORITHM_MSZIP, nullptr, &h)) throw Error("the Windows compressor is unavailable");
    struct Close {
        COMPRESSOR_HANDLE h;
        ~Close() { CloseCompressor(h); }
    } close{h};
    std::vector<uint8_t> out(text.size() + 0x10000);
    SIZE_T got = 0;
    if (!Compress(h, text.data(), text.size(), out.data(), out.size(), &got)) throw Error("compression failed");
    out.resize(got);
    return out;
}

std::string loadLibraryText() {
    const auto tsv = world::dataFile("lab/control_catalog.tsv");
    std::error_code ec;
    if (std::filesystem::exists(tsv, ec)) return readFileText(tsv);
    const auto zip = world::dataFile("lab/control_catalog.mszip");
    if (!std::filesystem::exists(zip, ec)) throw Error("the Systems library data is missing (" + zip.string() + ")");
    const std::string blob = readFileText(zip);
    return mszipDecompress(std::vector<uint8_t>(blob.begin(), blob.end()));
}

std::shared_ptr<Library> loadLibrary() {
    auto lib = Library::parse(loadLibraryText());
    const names::EntityCatalog& ents = names::entities();
    auto nameText = [&](uint32_t tag) -> std::string {
        const names::Entity* e = ents.find(tag);
        if (!e) return {};
        std::string s = e->name;
        if (!e->aliases.empty()) s += (s.empty() ? "" : " ") + e->aliases;
        return s;
    };
    for (size_t i = 0; i < lib->size(); ++i) {
        if (const uint32_t tag = lib->tag(i)) lib->addAlias(i, nameText(tag));
        for (uint32_t ref : lib->entityRefs(i)) lib->addAlias(i, nameText(ref));
    }
    return lib;
}

 

LibraryStore& LibraryStore::get() {
    static LibraryStore s;
    return s;
}

std::set<std::string> LibraryStore::parseSaved(const std::string& text) {
    std::istringstream f(text);
    std::set<std::string> out;
    for (;;) {
        f >> std::ws;
        if (f.eof()) break;
        std::string key;
        if (f.peek() != '"' || !(f >> std::quoted(key)) || key.empty() || key.size() > 4096 || key.find('\0') != std::string::npos ||
            !out.insert(key).second || out.size() > 100000)
            throw Error("malformed or duplicate saved resources");
    }
    return out;
}

std::string LibraryStore::serialiseSaved(const std::set<std::string>& keys) {
    std::ostringstream f;
    for (const std::string& k : keys) f << std::quoted(k) << '\n';
    return f.str();
}

std::map<std::string, ResourceNote> LibraryStore::parseNotes(const std::string& text) {
    std::istringstream f(text);
    std::string magic;
    unsigned version = 0;
    size_t count = 0;
    if (!(f >> magic >> version >> count) || magic != "BR_RESOURCE_NOTES" || version != 1 || count > 100000)
        throw Error("invalid resource notes file");
    std::map<std::string, ResourceNote> out;
    for (size_t i = 0; i < count; ++i) {
        std::string key;
        ResourceNote n;
        if (!(f >> std::quoted(key) >> std::quoted(n.name) >> std::quoted(n.notes)) || key.empty() || key.size() > 4096 || n.name.size() > 256 ||
            n.notes.size() > 4096 || !out.emplace(key, n).second)
            throw Error("malformed or duplicate resource notes");
    }
    return out;
}

std::string LibraryStore::serialiseNotes(const std::map<std::string, ResourceNote>& notes) {
    std::ostringstream f;
    f << "BR_RESOURCE_NOTES 1\n" << notes.size() << '\n';
    for (const auto& [k, n] : notes) f << std::quoted(k) << ' ' << std::quoted(n.name) << ' ' << std::quoted(n.notes) << '\n';
    return f.str();
}

void LibraryStore::reloadFrom(const std::filesystem::path& dir) {
    std::lock_guard<std::mutex> lock(m_);
    dir_ = dir;
    loaded_ = false;
    loadLocked();
    ++revision_;
}

void LibraryStore::loadLocked() {
    if (loaded_) return;
    loaded_ = true;
    if (dir_.empty()) dir_ = state_dir();
    saved_.clear();
    notes_.clear();
    error_.clear();
    savedOk_ = notesOk_ = true;
    std::error_code ec;
    const auto savedPath = dir_ / "lab_library_saved.txt", notesPath = dir_ / "lab_library_notes.txt";
    try {
        if (std::filesystem::exists(savedPath, ec)) saved_ = parseSaved(readFileText(savedPath));
    } catch (const std::exception& e) {
        savedOk_ = false;
        error_ = savedPath.string() + ": " + e.what() + " (saving resources is off until it reads)";
    }
    try {
        if (std::filesystem::exists(notesPath, ec)) notes_ = parseNotes(readFileText(notesPath));
    } catch (const std::exception& e) {
        notesOk_ = false;
        error_ = notesPath.string() + ": " + e.what() + " (editing notes is off until it reads)";
    }
}

bool LibraryStore::writeLocked(const char* file, const std::string& text) {
    std::string err;
    if (!atomicWrite(dir_ / file, text, err)) {
        error_ = err;
        return false;
    }
    return true;
}

bool LibraryStore::saved(const std::string& key) const {
    std::lock_guard<std::mutex> lock(m_);
    const_cast<LibraryStore*>(this)->loadLocked();
    return saved_.count(key) != 0;
}

bool LibraryStore::toggleSaved(const std::string& key) {
    std::lock_guard<std::mutex> lock(m_);
    loadLocked();
    if (!savedOk_) return false;    
    std::set<std::string> next = saved_;
    if (!next.erase(key)) next.insert(key);
    if (!writeLocked("lab_library_saved.txt", serialiseSaved(next))) return false;
    saved_ = std::move(next);
    ++revision_;
    return true;
}

std::set<std::string> LibraryStore::savedKeys() const {
    std::lock_guard<std::mutex> lock(m_);
    const_cast<LibraryStore*>(this)->loadLocked();
    return saved_;
}

std::optional<ResourceNote> LibraryStore::note(const std::string& key) const {
    std::lock_guard<std::mutex> lock(m_);
    const_cast<LibraryStore*>(this)->loadLocked();
    auto it = notes_.find(key);
    if (it == notes_.end()) return std::nullopt;
    return it->second;
}

bool LibraryStore::setNote(const std::string& key, const ResourceNote& n) {
    std::lock_guard<std::mutex> lock(m_);
    loadLocked();
    if (!notesOk_) return false;    
    ResourceNote c = n;
    if (c.name.size() > 256) c.name.resize(256);
    if (c.notes.size() > 4096) c.notes.resize(4096);
    std::map<std::string, ResourceNote> next = notes_;
    if (c.name.empty() && c.notes.empty()) next.erase(key);
    else next[key] = c;
    if (!writeLocked("lab_library_notes.txt", serialiseNotes(next))) return false;
    notes_ = std::move(next);
    ++revision_;
    return true;
}

bool LibraryStore::savedEditable() const {
    std::lock_guard<std::mutex> lock(m_);
    const_cast<LibraryStore*>(this)->loadLocked();
    return savedOk_;
}

bool LibraryStore::notesEditable() const {
    std::lock_guard<std::mutex> lock(m_);
    const_cast<LibraryStore*>(this)->loadLocked();
    return notesOk_;
}

void LibraryStore::reload() {
    std::lock_guard<std::mutex> lock(m_);
    loaded_ = false;
    loadLocked();
    ++revision_;
}

std::filesystem::path LibraryStore::folder() const {
    std::lock_guard<std::mutex> lock(m_);
    const_cast<LibraryStore*>(this)->loadLocked();
    return dir_;
}

std::map<std::string, ResourceNote> LibraryStore::notes() const {
    std::lock_guard<std::mutex> lock(m_);
    const_cast<LibraryStore*>(this)->loadLocked();
    return notes_;
}

std::string LibraryStore::error() const {
    std::lock_guard<std::mutex> lock(m_);
    return error_;
}

}   
