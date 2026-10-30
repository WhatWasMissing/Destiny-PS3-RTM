#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "game/world_objects.h"

namespace brps3::world {

inline constexpr uint32_t kEntityClass = 0x80800734;       
inline constexpr uint32_t kPlacementClass = 0x80800906;    
inline constexpr uint32_t kTagRefOffset = 0x0C;            
inline constexpr uint32_t kDefTypeOffset = 0x4E;           
inline constexpr uint32_t kFirstPackagePage = 0x400;
inline constexpr uint32_t kPackageItems = 8192, kPackageItemStride = 0x10;

inline uint32_t tagPage(uint32_t h) { return ((h >> 13) & 0xFFFF) & ((h & 0x80000000u) ? 0x3FFFu : 0x3FFu); }
inline uint32_t tagIndex(uint32_t h) { return h & 0x1FFF; }
inline uint32_t makeTag(uint32_t page, uint32_t index) { return 0x80000000u | ((page & 0x3FFF) << 13) | (index & 0x1FFF); }
inline bool isPackagePage(uint32_t page) { return page >= kFirstPackagePage; }

struct TagTable {
    uint32_t manager = 0, base = 0, end = 0, pageStride = 0, pageCount = 0;
    bool valid() const { return base && pageStride == 0x40 && pageCount > 0 && pageCount <= 0x4000 && mapped(base, pageCount * pageStride); }
    bool operator==(const TagTable&) const = default;
};
TagTable readTagTable(Reader& mem);                       

struct TagPage {
    uint32_t page = 0, items = 0, reg = 0, itemsEnd = 0, itemStride = 0, mask = 0;
    bool live() const { return items != 0 && reg != 0; }
    bool operator==(const TagPage&) const = default;
};
TagPage parsePage(const Bytes& rec, uint32_t page);       
TagPage readPage(Reader& mem, const TagTable& t, uint32_t page);               
 
std::vector<TagPage> readPages(Reader& mem, const TagTable& t, uint32_t first = 0, uint32_t count = 0);

struct Resolved {
    bool ok = false;
    std::string why;                     
    uint32_t handle = 0, page = 0, index = 0;
    TagPage desc;
    uint32_t limit = 0;                  
    uint32_t element = 0, body = 0;
    uint32_t item0 = 0, item8 = 0;       
};
 
 
Resolved resolve(Reader& mem, const TagTable& t, uint32_t handle, bool tagChecks = true);
 
uint32_t tagOfObject(Reader& mem, const TagTable& t, const SObject& o);

struct Residency {
    bool resident = false;
    std::string why;                     
    uint32_t tag = 0, element = 0, body = 0, definition = 0;
    uint32_t classWord = 0;              
    uint8_t type = 0;                    
};
Residency checkResident(Reader& mem, const TagTable& t, uint32_t tag);    

 
 
struct ResidentPage {
    uint32_t page = 0;
    std::vector<uint32_t> indices;       
};
std::vector<uint32_t> parseResidentItems(const Bytes& items);           
ResidentPage scanResidentPage(Reader& mem, const TagPage& page);        
std::vector<ResidentPage> scanResident(Reader& mem, const TagTable& t, const std::function<bool()>& cancelled = {},
                                       const std::function<void(size_t done, size_t total)>& progress = {},
                                       size_t maxPages = 0);
size_t residentCount(const std::vector<ResidentPage>& pages);

}   
