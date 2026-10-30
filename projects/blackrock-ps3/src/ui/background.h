#pragma once
 
 
 
 
 
#include <imgui.h>

namespace brps3::ui {

 
 
void setBackgroundDevice(void* d3d11Device);
void releaseBackground();
 
 
void drawBackground(ImDrawList* d, ImVec2 min, ImVec2 max);

}   
