#pragma once
 
 
 
 
 
#include <cstdint>

#include <imgui.h>

namespace brps3::ui {

 
constexpr ImVec4 rgb(uint32_t hex, float a = 1.0f) {
    return ImVec4(float((hex >> 16) & 0xFF) / 255.0f, float((hex >> 8) & 0xFF) / 255.0f,
                  float(hex & 0xFF) / 255.0f, a);
}

 
 
namespace pal {
 
inline constexpr ImVec4 Window = rgb(0x151619);                 
inline constexpr ImVec4 Surface = rgb(0x1E1F23, 0.82f);         
inline constexpr ImVec4 RaisedFill = rgb(0x26272C);             
inline constexpr ImVec4 Rail = rgb(0x101113, 0.72f);            
inline constexpr ImVec4 Line = ImVec4(1, 1, 1, 0.06f);          
inline constexpr ImVec4 Fill = ImVec4(1, 1, 1, 0.06f);          
inline constexpr ImVec4 FillHover = ImVec4(1, 1, 1, 0.10f);     
inline constexpr ImVec4 FillActive = ImVec4(1, 1, 1, 0.14f);    
inline constexpr ImVec4 Selection = rgb(0xE8E8EA, 0.16f);       
inline constexpr ImVec4 Green = rgb(0x63C299);                  
inline constexpr ImVec4 Red = rgb(0xF0706E);                    
inline constexpr ImVec4 DangerFill = rgb(0xC4453B);             
inline constexpr ImVec4 Soft = rgb(0xC6C7CC);                   

 
inline constexpr ImVec4 Bg = Window;                   
inline constexpr ImVec4 Sidebar = Rail;                
inline constexpr ImVec4 Card = Surface;                
inline constexpr ImVec4 Border = Line;                 
inline constexpr ImVec4 Raised = rgb(0x26272C);        
inline constexpr ImVec4 RaisedHi = rgb(0x2F3036);      
inline constexpr ImVec4 Input = Fill;                  
inline constexpr ImVec4 InputBorder = ImVec4(1, 1, 1, 0.10f);    
inline constexpr ImVec4 Text = rgb(0xEDEEF0);          
inline constexpr ImVec4 Muted = rgb(0x9C9EA5);         
inline constexpr ImVec4 Faint = rgb(0x63656D);         
inline constexpr ImVec4 Decor = rgb(0x4A4C53);         
inline constexpr ImVec4 Accent = rgb(0xE8E8EA);        
inline constexpr ImVec4 AccentHover = rgb(0xFFFFFF);
inline constexpr ImVec4 AccentActive = rgb(0xD2D2D6);
inline constexpr ImVec4 OnAccent = Window;             
inline constexpr ImVec4 Success = Green;
inline constexpr ImVec4 Danger = Red;
inline constexpr ImVec4 Warning = rgb(0xE59A6A);       
inline constexpr ImVec4 Info = rgb(0x8EA9D6);          
}   

 
namespace sp {
inline constexpr float XS = 4, S = 8, M = 12, L = 16, XL = 24, XXL = 32;
inline constexpr float Card = 14;       
inline constexpr float Page = 24;       
}

 
 
namespace type {
inline constexpr float Body = 14, Small = 13, Tiny = 11.5f, H1 = 18, H2 = 15, Mono = 13, Icon = 16;
inline constexpr float Label = 14;        
inline constexpr float Caption = 12.5f;   
inline constexpr float Title = 17;        
inline constexpr float Stat = 19;         
inline constexpr float Micro = 10.5f;     
inline constexpr float Compact = 13.5f;   
inline constexpr float Nav = 14;         
inline constexpr float Lead = 16.5f;      
inline constexpr float MonoSmall = 13;    
}

inline constexpr float kCardRounding = 10, kFrameRounding = 7, kRowRounding = 8;
inline constexpr float kControlH = 30;    
inline constexpr float kSmallControlH = 24;

enum class FontKind { Body, Semibold, Mono, Icons };

struct Fonts {
    ImFont* body = nullptr;        
    ImFont* semibold = nullptr;    
    ImFont* mono = nullptr;        
    ImFont* icons = nullptr;       
};
const Fonts& fonts();
ImFont* font(FontKind kind);
 
float fontSize(FontKind kind, float em);
 
 
float textTop(FontKind kind, float em, float midY);
 
 
float ascent(FontKind kind, float em);
inline float baselineTop(FontKind kind, float em, float baselineY) { return baselineY - ascent(kind, em); }

 
float scale();
inline float px(float v) { return v * scale(); }

 
struct FontScope {
    FontScope(FontKind kind, float em);
    ~FontScope();
    FontScope(const FontScope&) = delete;
    FontScope& operator=(const FontScope&) = delete;
};
 
ImVec2 textSize(FontKind kind, float em, const char* text, const char* end = nullptr, float wrap = 0.0f);
void drawText(ImDrawList* dl, FontKind kind, float em, ImVec2 pos, ImU32 col, const char* text,
              const char* end = nullptr, float wrap = 0.0f);

 
inline ImU32 u32(const ImVec4& c) { return ImGui::GetColorU32(c); }
inline ImU32 u32(const ImVec4& c, float alphaMul) { return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, c.w * alphaMul)); }
inline ImVec4 alpha(const ImVec4& c, float a) { return ImVec4(c.x, c.y, c.z, a); }
inline ImVec4 mix(const ImVec4& a, const ImVec4& b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

 
void initTheme();                                    
void applyScale(float dpiScale, float userScale);    
bool reduceMotion();
void setReduceMotion(bool on);

}   
