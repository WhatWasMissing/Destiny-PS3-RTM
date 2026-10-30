#pragma once
 
 
 
#include <mutex>

#include "app/registry.h"
#include "game/cmd_handler.h"

namespace brps3 {

struct CmdSession {
    std::mutex m;
    bool hooked = false;
    cmd::Handler handler;
};

inline cmd::Handler& cmdHandler(Ctx& c) {
    CmdSession& s = c.state<CmdSession>("cmd.handler");
    {
        std::lock_guard<std::mutex> lock(s.m);
        if (!s.hooked) {
            s.hooked = true;
            c.onDisconnect([](Ctx& k) { k.state<CmdSession>("cmd.handler").handler.shutdown(k.rt()); });
        }
    }
     
    c.require(cmd::fingerprints(), "In-game command handler (sobject_new, loot_event emitters)");
    return s.handler;
}

}   
