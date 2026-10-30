#pragma once
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
 
#include <cstdint>
#include <string>
#include <vector>

#include "game/world_mem.h"

namespace brps3::activity {

struct BapPeer {
    bool ok = false;
    int peerIndex = -1;
    uint32_t handle = 0xFFFFFFFFu;
    uint32_t state = 0, reason = 0;
    std::string endpoint;                
    int ladder = 0;                      
    bool ready() const { return ladder == 3; }
};

 
struct ClientDetail {
    bool read = false;
    uint32_t conn = 0, acl = 0;          
    std::string checksum;                
    bool checksumOk = false;
    std::string bubbles;                 
    bool bubblesOk = false;
    uint32_t nbubbles = 0;               
    std::vector<uint8_t> chosen;         
};

struct ClientSlot {
    const char* name = "";               
    int recordIndex = -1;
    int32_t astat = 0;                   
    uint8_t comp = 0;                    
    uint32_t connHandle = 0xFFFFFFFFu, aclHandle = 0xFFFFFFFFu;
    uint8_t aclFlag = 0;
    bool aclResolved = false;            
    bool aclValid = false;               
    uint32_t aclVerdict20 = 0;           
    BapPeer peer;
    ClientDetail detail;
    bool populated() const { return connHandle != 0xFFFFFFFFu || aclHandle != 0xFFFFFFFFu; }
    const char* verdict() const;         
     
    std::string key() const;
};

struct ClientProbe {
    bool ok = false;
    std::string why;
    uint32_t recordBase = 0, slotTable = 0;
    uint32_t chk268 = 0, chk26C = 0, chk = 0xFFFFFFFFu;    
    std::vector<ClientSlot> slots;       
    uint32_t clock = 0;
    std::string key() const;             
};
 
ClientProbe readClientProbe(world::Reader& mem, bool details = true);
 
std::string describeSlot(const ClientSlot& s);

}   
