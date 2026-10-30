#pragma once
 
 
 
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "app/registry.h"
#include "game/world_mem.h"
#include "ui/widgets.h"

namespace brps3::labui {

bool demo();                                    
double now();                                   
std::string ago(double at);                     
std::string hx(uint32_t v);                     
std::string fmt(const char* f, ...);
std::string thousands(size_t n);                 

 
template <class T>
class Slot {
public:
    void set(T v) {
        std::lock_guard<std::mutex> l(m_);
        value_ = std::move(v);
        error_.clear();
        at_ = now();
    }
    void fail(std::string e) {
        std::lock_guard<std::mutex> l(m_);
        error_ = std::move(e);
        at_ = now();
    }
    void clear() {
        std::lock_guard<std::mutex> l(m_);
        value_.reset();
        error_.clear();
        at_ = 0;
    }
    std::optional<T> get() const {
        std::lock_guard<std::mutex> l(m_);
        return value_;
    }
    std::string error() const {
        std::lock_guard<std::mutex> l(m_);
        return error_;
    }
    double at() const {
        std::lock_guard<std::mutex> l(m_);
        return at_;
    }
    bool has() const {
        std::lock_guard<std::mutex> l(m_);
        return value_.has_value();
    }

private:
    mutable std::mutex m_;
    std::optional<T> value_;
    std::string error_;
    double at_ = 0;
};

 
 
void post(Ui& ui, const char* label, std::function<void(Ctx&)> run, std::function<void(const std::string&)> onError = {});

 
bool kvBegin(const char* id, float keyWidth = 150, float width = 0);    
void kv(const char* key, const std::string& value, const ImVec4& color = ui::pal::Text, bool mono = true, const char* tip = nullptr);
void kvHex(const char* key, uint32_t value, const char* what, const ImVec4& color = ui::pal::Text, const char* tip = nullptr);
void kvEnd();
 
void copyText(const std::string& text, const char* what, const ImVec4& color = ui::pal::Text);
 
void copyCell(const std::string& text, const char* what, const ImVec4& color = ui::pal::Text, float rowHeight = 30);
void check(bool ok, const std::string& text, const char* tip = nullptr);    
void warnLine(const std::string& text);
void hint(const char* text);                   
void errorBox(const std::string& text);        
void readStamp(double at, const char* what = "read");    
 
class Flow {
public:
    Flow();
    void next(float width, float spacing = 6);
private:
    float avail_, x_ = 0;
    bool first_ = true;
};
 
bool jobButton(Ui& ui, const char* label, const char* icon, const char* tooltip, ui::Tone tone = ui::Tone::Secondary, const char* text = nullptr);

 
struct ObjRow {
    uint32_t handle = 0, index = 0, parent = 0, head = 0;
    uint8_t type = 0;
    bool deleting = false;
    world::Vec3 pos;
};
uint32_t pickedHandle();                       
std::string pickedText();                      
void pick(uint32_t handle, uint8_t type, bool fromList);
void drawPicker(Ui& ui);                       
void runFindPlayer(Ctx& c);                    
void runListObjects(Ctx& c);                   
void resetPicker();                            
inline constexpr const char* kFindPlayerLabel = "Inspect the Local Player";
inline constexpr const char* kListObjectsLabel = "List Live Objects";

}   
