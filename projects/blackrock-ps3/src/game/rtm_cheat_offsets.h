#pragma once

#include <cstdint>

namespace brps3::rtm_offsets {

struct WordSite {
    uint32_t address;
    uint32_t original;
    uint32_t patched;
};

inline constexpr WordSite kInstantRespawn[] = {
    {0x0071C348, 0x93BF0030, 0x60000000},
};

inline constexpr WordSite kNoRecoil[] = {
    {0x005056DC, 0x3F800000, 0x00000000},
    {0x00505D24, 0x3F800000, 0x00000000},
    {0x0030DC44, 0x3F800000, 0x00000000},
};

inline constexpr WordSite kUnlimitedAbilities[] = {
    {0x002C1DB4, 0xD03B019C, 0x60000000},
    {0x00CCB9E8, 0x3063FFFF, 0x30630001},
    {0x002C36FC, 0xC3C30000, 0xC3C30004},
};

inline constexpr WordSite kUnlimitedAmmo[] = {
    {0x003108EC, 0x63C40000, 0x3880270F},
    {0x00310554, 0x63C50000, 0x38A0270F},
    {0x00CF95AC, 0x63DD0000, 0x60000000},
    {0x00CF95B0, 0x40820008, 0xFC1EF800},
    {0x00CF95B4, 0x3BA00001, 0x3FC0461C},
    {0x00CF95B8, 0x807F001C, 0x63DE3C00},
    {0x00CF95BC, 0xD3DF0110, 0x93DF0110},
};

inline constexpr WordSite kNoCollision[] = {
    {0x014FC690, 0x3F800000, 0x00000000},
};

// The RTM changes the low halfword at +2. This aligned word preserves the
// unchanged high halfword while keeping Blackrock's compare-and-restore model.
inline constexpr WordSite kOneHitKill[] = {
    {0x0043E624, 0x3C603F80, 0x3C60447A},
};

inline constexpr WordSite kRapidFire[] = {
    {0x00307590, 0x3F800000, 0x49F42400},
};

inline constexpr WordSite kNoTarget[] = {
    {0x0018681C, 0x3F800000, 0xC0000000},
    {0x001891E4, 0x3F800000, 0xC0000000},
};

inline constexpr WordSite kGodMode[] = {
    {0x00444CA0, 0xC0210358, 0xC0210448},
};

inline constexpr WordSite kStopBullets[] = {
    {0x0051E7E0, 0x3F800000, 0x00000000},
};

inline constexpr WordSite kNoGravity[] = {
    {0x0143AD08, 0x3F800000, 0xBF800000},
};

inline constexpr WordSite kNoPushbacks[] = {
    {0x01389760, 0x3F800000, 0xC0A00000},
    {0x01389764, 0x3F800000, 0x00000000},
    {0x01389768, 0x3F800000, 0x00000000},
};

inline constexpr WordSite kNoFloors[] = {
    {0x01689470, 0x3F800000, 0xBF800000},
};

inline constexpr WordSite kStaticActors[] = {
    {0x002EDAC4, 0x3F800000, 0x00000000},
};

inline constexpr WordSite kUnlimitedSparrow[] = {
    {0x0050A700, 0xD023002C, 0x60000000},
};

inline constexpr WordSite kVariableBuff[] = {
    {0x0005E614, 0x3F800000, 0x403F0004},
};

}  // namespace brps3::rtm_offsets
