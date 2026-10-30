#pragma once
 
 
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>

#include "app/registry.h"
#include "features/spawn_shared.h"
#include "ui/widgets.h"

namespace brps3::spawnui {

 
 
bool post(const std::string& label, std::function<void(Ctx&)> job, std::function<void(bool ok, const std::string& error)> done = {});

 
enum class Res { Unknown, Loaded, PackageLoaded, NotLoaded };
struct ResidencyView {
    std::set<uint32_t> livePages, resident, scannedPages, checkedOut;
    double pagesAt = 0, entriesAt = 0;
    uint32_t registeredPages = 0;
    uint64_t rev = ~0ull;
    bool any() const { return pagesAt > 0 || entriesAt > 0 || !resident.empty(); }
    Res of(uint32_t tag) const;
};
const ResidencyView& residency();        
ImVec4 resColor(Res r);
const char* resLabel(Res r);             
const char* resHelp(Res r);
 
void resDot(Res r, float rowHeight);

 
 
void startSpawn(uint32_t tag, std::optional<spawn::Mode> mode = std::nullopt, const std::string& label = {});
std::string spawnJobLabel(uint32_t tag, int count);
bool spawnBusy();                        

 
void gotoTab(const char* tab);
 
void loadTagOnWorldTab(uint32_t tag);
uint32_t takeWorldTabTag();              

 
std::string ago(double since);
 
inline constexpr float kRowH = 30;

}   
