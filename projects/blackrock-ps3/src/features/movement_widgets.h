#pragma once
 
#include <cstdint>
#include <string>

#include <imgui.h>

#include "game/movement_pad.h"

namespace brps3::mvui {

 
 
void padCap(movement::Btn b, bool lit = false, float size = 22);
float padCapWidth(movement::Btn b, float size = 22);
 
void padChord(uint32_t chord, bool lit = false, float size = 22);
float padChordWidth(uint32_t chord, float size = 22);

 
void padDiagram(const movement::PadState& p, float width, float height);

 
 
void statusLine(const ImVec4& color, const char* label, const std::string& detail = {}, bool pulse = false, float width = 0);
 
void stickCap(float size = 22, bool lit = false);

 
std::string ago(double at);

}   
