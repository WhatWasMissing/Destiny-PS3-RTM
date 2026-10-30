#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace brps3::lab {

enum class Col : int { System, Kind, Id, Namespace, Tag, Name, Evidence, Package, Details, Count };

class Library {
public:
     
     
    static std::shared_ptr<Library> parse(std::string text);

    size_t size() const { return rows_.size(); }
    std::string_view raw(size_t row, Col c) const;           
    std::string text(size_t row, Col c) const;               
    uint32_t tag(size_t row) const { return rows_[row].tag; }
    const std::vector<uint32_t>& entityRefs(size_t row) const { return rows_[row].refs; }
    std::string key(size_t row) const;                        
    const std::vector<std::string>& systems() const { return systems_; }    
    std::vector<std::string> kinds(const std::string& system = {}) const;   
    size_t count(const std::string& system) const;

     
     
     
     
    struct Query {
        std::string text, system, kind;
        uint32_t entityRef = 0;
    };
    std::vector<uint32_t> filter(const Query& q, const std::function<const std::string*(uint32_t row)>& extra = {},
                                 const std::function<bool(uint32_t row)>& keep = {}, const std::function<bool()>& cancelled = {},
                                 size_t limit = 0) const;

    std::optional<size_t> findKey(const std::string& key) const;
    std::vector<size_t> rowsWithTag(uint32_t tag) const;      

     
    void addAlias(size_t row, const std::string& text);

private:
    struct Row {
        uint32_t off[int(Col::Count)] = {};
        uint32_t len[int(Col::Count)] = {};
        uint32_t hay = 0, hayLen = 0;      
        uint32_t tag = 0;
        std::vector<uint32_t> refs;
    };
    std::string text_, lower_;
    std::vector<Row> rows_;
    std::vector<std::string> systems_;
    std::map<std::string, std::vector<std::string>> kinds_;   
    std::map<std::string, size_t> systemCounts_;
    std::map<std::string, size_t> keys_;
    std::multimap<uint32_t, size_t> byTag_;
    std::map<size_t, std::string> aliases_;
};

std::string unescapeCell(std::string_view raw);              
std::vector<std::string> searchWords(const std::string& text);    

 
std::string mszipDecompress(const std::vector<uint8_t>& blob);
std::vector<uint8_t> mszipCompress(const std::string& text);    

 
 
std::string loadLibraryText();
 
std::shared_ptr<Library> loadLibrary();

 
struct ResourceNote {
    std::string name, notes;
    bool operator==(const ResourceNote&) const = default;
};

class LibraryStore {
public:
    static LibraryStore& get();          
    bool saved(const std::string& key) const;
     
     
     
    bool toggleSaved(const std::string& key);
    std::set<std::string> savedKeys() const;
    std::optional<ResourceNote> note(const std::string& key) const;
    bool setNote(const std::string& key, const ResourceNote& n);    
    std::map<std::string, ResourceNote> notes() const;
    bool savedEditable() const;          
    bool notesEditable() const;          
    void reload();                       
    std::string error() const;           
    std::filesystem::path folder() const;
    uint64_t revision() const { return revision_.load(); }    

     
     
    static std::set<std::string> parseSaved(const std::string& text);
    static std::string serialiseSaved(const std::set<std::string>& keys);
    static std::map<std::string, ResourceNote> parseNotes(const std::string& text);
    static std::string serialiseNotes(const std::map<std::string, ResourceNote>& notes);
    void reloadFrom(const std::filesystem::path& dir);    

private:
    LibraryStore() = default;
    void loadLocked();
    bool writeLocked(const char* file, const std::string& text);
    mutable std::mutex m_;
    bool loaded_ = false;
    bool savedOk_ = true, notesOk_ = true;
    std::filesystem::path dir_;
    std::set<std::string> saved_;
    std::map<std::string, ResourceNote> notes_;
    std::string error_;
    std::atomic<uint64_t> revision_{1};
};

}   
