#pragma once
 
 
 
 
 
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace brps3::activity {

using Fingerprints = std::vector<std::pair<uint32_t, const char*>>;

namespace addr {
 
inline constexpr uint32_t WORLD_HOLDER = 0x1ABC898;         
inline constexpr uint32_t WORLD_CONTROLLER_PTR = 0x1ABC89C; 
inline constexpr uint32_t ACTIVITY_RECORD_PTR = 0x1ABC8A4;  
inline constexpr uint32_t STATE_NAMES = 0x12F7F00;          
inline constexpr uint32_t HANDLER33 = 0x1ABE978;            
inline constexpr uint32_t TARGET_NAME = 0x1ABEF78;          
inline constexpr uint32_t WTL_GLOBALS_PTR = 0x1A1DCE0;      
inline constexpr uint32_t WTL_CURSOR = 0x1A1DCE8;           
inline constexpr uint32_t SOBJECT_COUNT = 0x1A1D9F4;        
inline constexpr uint32_t ENGINE_CLOCK_LO = 0x1A1D67C;      
inline constexpr uint32_t ACTIVE_ACTIVITY = 0x1A27690;      
 
 
inline constexpr uint32_t DEST_OWNER_PTR = 0x1ABC7AC;
inline constexpr uint32_t DEST_POOL = 0x1A20C70;            
inline constexpr uint32_t DEST_BLOCK = 0x50B0, DEST_DESCRIPTOR = 0x628;    

 
inline constexpr uint32_t SESSION_ROOT_PTR = 0x1ABCA14;     
inline constexpr uint32_t SLOT_STRIDE = 0x12208, SUB_STRIDE = 0x9100, SUB_BASE = 0x10, GOAL_OFFSET = 0x6708, GATE_OFFSET = 0x8468;
inline constexpr uint32_t FIELD_HAS_VALUE = 0xD0, FIELD_VALUE = 0xD8;    

 
inline constexpr uint32_t JOIN_REFUSE_DECODER = 0x8CCDE4;   
inline constexpr uint32_t LOBBY_ACTIVITY_HOST = 0x913AF0;   
inline constexpr uint32_t LOBBY_SESSION_DISBAND = 0x917614; 
inline constexpr uint32_t LOBBY_ACTIVITY_STATUS = 0x914E28; 
inline constexpr uint32_t SAVEGPR0 = 0xD98A20;              
inline constexpr uint32_t CODEC_TABLE = 0x917CA4;           
inline constexpr uint32_t MFLR_R0 = 0x7C0802A6;             

 
inline constexpr uint32_t SLOT_TABLE_PTR = 0x1ABC79C;       
inline constexpr uint32_t CONN_POOL = 0x1A20B70;            
inline constexpr uint32_t ACL_POOL = 0x1A20BF0;             
inline constexpr uint32_t PEER_TABLE_PTR = 0x1AD5DEC;       
inline constexpr uint32_t PEER_OBJECTS_PTR = 0x1B9518C;     
inline constexpr uint32_t RECORD_FIRST = 0x4E0, RECORD_STRIDE = 0x170, RECORD_CHECKSUM = 0x270, RECORD_PEER_INDEX = 0x158;
inline constexpr uint32_t BAP_STRIDE = 0x2288, BAP_STATE = 0x10F8, BAP_REASON = 0x1BEC, BAP_ENDPOINT = 0x1604;
inline constexpr uint32_t CHECKSUM_FN = 0x272A78, BUBBLES_FN = 0x1D8468, PEER_LADDER_FN = 0x475D50;    

 
inline constexpr uint32_t RSC_BEGIN_TABLE = 0x19BA7A8;      
inline constexpr uint32_t RSC_MODE = 0x1934EE0;             
inline constexpr uint32_t RSC_HANDLE_A = 0x1934EE4, RSC_HANDLE_B = 0x1934EE6;    
inline constexpr uint32_t RSC_REQUESTS = 0x19A3F80;         
inline constexpr uint32_t RSC_REQUEST_STRIDE = 0x19A3F88;
inline constexpr uint32_t RSC_OD_HEAD = 0x19B8750;          
inline constexpr uint32_t RSC_OD_POOL = 0x19AF730;          
inline constexpr uint32_t RSC_DIRTY_ADD = 0x1896560, RSC_DIRTY_DEL = 0x1896561;   
inline constexpr uint32_t RSC_TAG_BASE_PTR = 0x189655C;     
inline constexpr uint32_t RSC_PUMP = 0xE2E10, RSC_DISPATCHER = 0xE442C;           
inline constexpr uint32_t WTL_WAIT = 0x150C70;               
inline constexpr uint32_t WTL_CREATE = 0x15216C;             
inline constexpr uint32_t WTL_PAIR_ADD = 0x15135C;           
inline constexpr uint32_t WTL_PAIR_REMOVE = 0x151450;        
inline constexpr uint32_t WTL_FINALIZE = 0x1523C4;           
inline constexpr uint32_t WTL_DISPOSE = 0x1521E4;            
inline constexpr uint32_t WTL_DET_POOL = 0x1A1DD20;          
inline constexpr uint32_t WTL_OWNER_LIST = 0x1C, WTL_OWNER_HEAD = 0xC, WTL_LIST_LIMIT = 0xC03C;
inline constexpr uint32_t WTL_INFLIGHT_DET = 0x4, WTL_INFLIGHT_NONDET = 0x8, WTL_QUEUE_A = 0x1405C, WTL_QUEUE_B = 0x14060, WTL_QUEUE_C = 0x14070;
inline constexpr uint32_t WTL_GLOBALS_SIZE = 0x14078;

 
inline constexpr uint32_t WE_REGISTRY_PTR = 0x1A1D258;      
                                                            
inline constexpr uint32_t WE_RUN = 0x80, WE_DESCRIBE = 0xCC, WE_MASK = 0x118, WE_STATES = 0x11C, WE_PAYLOADS = 0x168, WE_PAYLOAD_SIZE = 0x1804;
inline constexpr uint32_t WE_SLOTS = 19;
inline constexpr uint32_t EVENT_CHANGE_WORLD = 3, EVENT_DEPENDENCY = 11;
inline constexpr uint32_t WE_REGISTRY_GET = 0x126648, WE_PENDING = 0x126A40, WE_REGISTER = 0x1267C4, WE_CURRENT_SLOT = 0x126B84, WE_POST = 0x1268D4;
inline constexpr uint32_t CW_NAMED = 0x332674;               
inline constexpr uint32_t CW_FLAGS = 0x334C70, CW_EVENT_CTOR = 0xE4D6B0;    
inline constexpr uint32_t CW_POST_CALLSITE = 0x3328F4;       
inline constexpr uint32_t CW_POST_CALL_WORD = 0x4BDF3FE1;
inline constexpr uint32_t CW_PHASE_OFFSET = 0x4C0;           
inline constexpr uint32_t SCENARIO_CLIENT_PTR = 0x1A93300;   
inline constexpr uint32_t ORBIT_SCENARIO_XBOX = 0x80B52003;  
inline constexpr uint32_t STATE_IN_WORLD = 36;
inline constexpr uint32_t STATE_CLEANUP = 25;                
inline constexpr uint32_t WE_SLOT_STATE_CW = WE_STATES + 4 * EVENT_CHANGE_WORLD;           
inline constexpr uint32_t WE_PENDING_RUN_11 = WE_RUN + 4 * EVENT_DEPENDENCY;               
inline constexpr uint32_t WE_PENDING_STATE_11 = WE_STATES + 4 * EVENT_DEPENDENCY;          
inline constexpr uint32_t WE_PAYLOAD_CW = WE_PAYLOADS + WE_PAYLOAD_SIZE * EVENT_CHANGE_WORLD;    
inline constexpr uint32_t CW_EVENT_SIZE = 0x17B8;            
}   

 
extern const Fingerprints kMonitorFingerprints;       
extern const Fingerprints kDescriptorFingerprints;    
extern const Fingerprints kJoinRefuseFingerprints;    
extern const Fingerprints kLobbyFingerprints;         
extern const Fingerprints kClientFingerprints;        
extern const Fingerprints kRscFingerprints;           
extern const Fingerprints kChangeWorldFingerprints;   
extern const Fingerprints kResourcerFingerprints;     
extern const Fingerprints kQueueFingerprints;         

 
const char* stateName(uint32_t state);                
inline constexpr uint32_t kStateCount = 38;
std::string stateLabel(uint32_t state);               
const char* abortReasonName(uint32_t reason);         
std::string abortReasonLabel(uint32_t reason);        
 
 
 
const char* joinRefuseReasonName(uint32_t reason);
const char* bapStateName(uint32_t s);                 
const char* bapReasonName(uint32_t r);                
int peerLadder(uint32_t bapState);                    
const char* resourcererEventName(uint32_t e);         

}   
