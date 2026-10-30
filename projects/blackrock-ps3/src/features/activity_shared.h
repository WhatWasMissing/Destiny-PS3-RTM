#pragma once
 
 
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "app/settings.h"
#include "game/activity_addr.h"
#include "game/names_catalog.h"

namespace brps3::activity_ui {

inline std::string hx(uint32_t v) {
    char b[12];
    std::snprintf(b, sizeof b, "%08X", v);
    return b;
}

 
inline bool uiDemo() {
    char* v = nullptr;
    size_t n = 0;
    const bool on = _dupenv_s(&v, &n, "BRPS3_UI_DEMO") == 0 && v && v[0] == '1';
    std::free(v);
    return on;
}

 
 
inline constexpr const char* kOrbitKey = "activity.orbit_scenario";
inline uint32_t defaultOrbitTag() { return names::toPs3Tag(activity::addr::ORBIT_SCENARIO_XBOX); }
inline uint32_t orbitTag(const Settings& s) {
    try {
        const std::string v = s.str(kOrbitKey, "");
        if (!v.empty()) return uint32_t(std::stoul(v, nullptr, 16));
    } catch (...) {
    }
    return defaultOrbitTag();
}

}   
