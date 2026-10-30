#pragma once
 
 
#include <cstdint>
#include <functional>
#include <string>

#include "app/registry.h"
#include "game/player_writes.h"

namespace brps3::playerui {

inline constexpr const char* kWritesLoop = "player.writes";     
inline constexpr const char* kProbeSection = "Player and weapons";    
inline constexpr const char* kLiveWrites = "Live writes";             

 
bool demoMode();

 
 
player::WriteManager& writes(Ctx& c);
void setFeature(Ctx& c, player::Feature f, bool on);
void editMagnitudes(Ctx& c, const std::function<void(player::Magnitudes&)>& edit);

 
void drawLiveWrites(Ui& ui, bool weapons);
void seedDemoBoard();

 
void setAttachmentsTarget(uint32_t handle);
 
void openAttachments(Ui& ui, uint32_t handle);

std::string hex8(uint32_t v);

}   
