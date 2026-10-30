#pragma once
 
 
 
#include <memory>
#include <string>

namespace brps3::ui {

struct LaunchOptions {
    bool screenshot = false;         
    std::string page;                
    std::string tab;                 
    bool openLog = false;
    bool openPalette = false;
    bool collapsed = false;
    bool demo = false;               
    unsigned iconSheet = 0;          
};

class Shell {
public:
    explicit Shell(const LaunchOptions& o);
    ~Shell();
    Shell(const Shell&) = delete;
    Shell& operator=(const Shell&) = delete;

    void frame();
     
    bool wantsFullRate() const;
     
    bool needsFrame() const;
     
    unsigned idleWaitMs() const;
     
    bool takeScaleChange(float* userScale);
    float userScale() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

 
 
int runSelfCheck(const std::string& reportPath, const std::string& itemsPath);

}   
