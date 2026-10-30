 
 
 
 
 
 
 
 
#include <cstdio>
#include <string>

#include "game/activity_addr.h"

namespace brps3::activity {

const Fingerprints kMonitorFingerprints = {
    {0x36E260, "7C0802A648A2A7C1F821FF81F8010090"},    
    {0x356494, "F821FF917C0802A6F801008038800000"},    
    {0x355CE4, "F821FF917C0802A6F80100804BFF5CF1"},    
    {0x14E858, "3C6001A28063DCE04E800020F821FF91"},    
};

const Fingerprints kDescriptorFingerprints = {
    {0x35CD7C, "7C0802A648A3BCA1F821FF71F80100A0"},    
    {0x33D020, "F821FF917C0802A6F8010080480031E5"},    
    {0x1DA240, "306350B04E800020"},                    
    {0x1D56D0, "F821FF817C0802A6F801009060650000"},    
    {0x108960, "54A704FE80A4001C7CA539D681040018"},    
    {0x34B9E0, "3CA001AC38C000002C83FFFF60C40000"},    
    {0x1048074, "7C0802A64805A121F821FF81F8010090"},   
};

const Fingerprints kJoinRefuseFingerprints = {
    {addr::JOIN_REFUSE_DECODER, "7C0802A6484CBC39F821FF81F8010090"},
    {addr::SAVEGPR0, "FBC1FFF0FBE1FFF8F80100104E800020"},
};

const Fingerprints kLobbyFingerprints = {
    {addr::LOBBY_ACTIVITY_HOST, "7C0802A648484F2DF821FF81F8010090"},
    {addr::LOBBY_SESSION_DISBAND, "7C0802A648481409F821FF81F8010090"},
    {addr::LOBBY_ACTIVITY_STATUS, "7C0802A648483BF5F821FF81F8010090"},
    {addr::SAVEGPR0, "FBC1FFF0FBE1FFF8F80100104E800020"},
    {addr::CODEC_TABLE, "7C0802A648480D65F821FF31F80100E0"},
};

const Fingerprints kClientFingerprints = {
    {addr::CHECKSUM_FN, "7C0802A648B25F99F821FF61F80100B0"},
    {addr::BUBBLES_FN, "7C0802A648BC0575F821FEE1F8010130"},
    {addr::PEER_LADDER_FN, "7C0802A648922CCDF821FF81F8010090"},
};

const Fingerprints kRscFingerprints = {
    {addr::RSC_PUMP, "7C0802A648CB5BEDF821FF01F8010110"},
    {addr::RSC_DISPATCHER, "7C0802A648CB45C5F821FF01F8010110"},
    {0xE76D0, "7C0802A648CB134DF821FF81F8010090"},       
    {0xF4904, "7C0802A648CA411DF821FF81F8010090"},       
    {addr::WTL_WAIT, "7C0802A648C47DB1F821FF81F8010090"},
};

const Fingerprints kChangeWorldFingerprints = {
    {addr::CW_NAMED, "7C0802A648A66391F821E151F8011EC0"},
    {addr::CW_FLAGS, "7C0802A648A63DADF821FF81F8010090"},
    {addr::CW_EVENT_CTOR, "7C0802A648254AA5F821FEF1F8010120"},
    {addr::WE_REGISTRY_GET, "3C6001A28063D2584E8000207C0802A6"},
    {addr::WE_PENDING, "788417647C8320143860000078840020"},
    {addr::WE_POST, "7C0802A648C72141F821FF71F80100A0"},
    {addr::CW_POST_CALLSITE, "4BDF3FE1"},
};

const Fingerprints kResourcerFingerprints = {
    {0x14E858, "3C6001A28063DCE04E800020F821FF91"},    
    {addr::WTL_CREATE, "7C0802A648C468B5F821FF81F8010090"},
    {addr::WTL_PAIR_ADD, "7C0802A648C476B9F821FF71F80100A0"},
    {addr::WTL_PAIR_REMOVE, "7C0802A648C475C5F821FF71F80100A0"},
    {addr::WTL_FINALIZE, "7C0802A648C4663DF821FF41F80100D0"},
    {addr::WTL_DISPOSE, "7C0802A648C46835F821FF61F80100B0"},
};

const Fingerprints kQueueFingerprints = {
    {addr::WE_REGISTRY_GET, "3C6001A28063D2584E8000207C0802A6"},
    {addr::WE_PENDING, "788417647C8320143860000078840020"},
    {addr::WE_REGISTER, "7C0802A648C72259F821FF81F8010090"},
    {addr::WE_CURRENT_SLOT, "38A0000038C0FFFF54A4103A7C832014"},
    {addr::WE_POST, "7C0802A648C72141F821FF71F80100A0"},
};

namespace {

const char* const kStates[kStateCount] = {
    "pregame",
    "bootflow:media_check",
    "bootflow:bootload",
    "bootflow:intro_screen_display",
    "bootflow:start",
    "bootflow:platform_signin",
    "bootflow:platform_account_warnings",
    "bootflow:enumerate_content_rights",
    "bootflow:account_signin",
    "bootflow:eula",
    "bootflow:upsell",
    "bootflow:email",
    "bootflow:confirm_account_transfer",
    "bootflow:content_check",
    "bootflow:content_install",
    "bootflow:insufficient_content_space",
    "bootflow:illegal_dlc_media",
    "bootflow:queuing",
    "bootflow:package_registration",
    "bootflow:bap_signin",
    "bootflow:investment_signin",
    "bootflow:profile_setup",
    "bootflow:prepare_for_orbit",
    "character:signin",
    "character:prologue",
    "cleanup",
    "setup:orbit_loading",
    "setup:orbit",
    "setup:activity_session_creation",
    "setup:matchmaking",
    "setup:activity_host_setup",
    "setup:prologue_intro_loading",
    "setup:orbit_outro",
    "setup:activity_world_transition",
    "activity:initial_slice_set_loading",
    "activity:physics_join",
    "activity:in_world",
    "activity:watch_video",
};

struct Reason {
    uint32_t code;
    const char* name;
    bool verified;    
};
 
 
const Reason kReasons[] = {
    {0, "generic", false},
    {5, "state-33 work bailout (Xbox sub_82739400)", false},
    {9, "Xbox sub_8272EC80 / 8272EA50", false},
    {11, "Xbox sub_8272D1D0", false},
    {15, "Xbox sub_827386E0", false},
    {23, "requires DLC", true},
    {24, "requires permissions", true},
    {27, "Xbox sub_8272DF98", false},
    {54, "activity host setup failed (state 30)", false},
    {55, "transition not correct / no active activity", true},
    {56, "slice-set instantiate failed", false},
    {57, "Xbox sub_827386E0", false},
    {58, "in-world work failed (Xbox sub_824670C8)", false},
    {63, "Xbox sub_82731560", false},
    {66, "Xbox sub_82725D88", false},
    {80, "Xbox sub_8272D1D0", false},
    {84, "shared poll refusal (Xbox sub_82725A88)", false},
    {121, "activity validator", false},
    {122, "activity validator", false},
    {124, "Xbox sub_8272C830", false},
    {130, "Xbox sub_82725D88", false},
    {133, "Xbox sub_8272B7B0", false},
    {134, "Xbox sub_8272B7B0", false},
    {139, "Xbox sub_8272B7B0", false},
    {140, "Xbox sub_8272B7B0", false},
    {141, "Xbox sub_8272B7B0", false},
    {142, "Xbox sub_82725BD8", false},
    {144, "Xbox sub_8272B7B0", false},
    {149, "Xbox sub_8272C118", false},
    {150, "Xbox sub_8272C038", false},
    {164, "Xbox sub_8272C688", false},
    {165, "activity validator", false},
    {166, "activity validator", false},
    {167, "activity validator", false},
    {168, "activity validator", false},
    {169, "activity validator", false},
    {170, "activity validator", false},
    {171, "activity validator", false},
    {173, "Xbox sub_82726AD0", false},
    {175, "Xbox sub_827273E0", false},
    {181, "Xbox sub_827273E0", false},
    {273, "Xbox sub_82725D88", false},
    {274, "Xbox sub_82725D88", false},
    {276, "activity validator", false},
    {283, "Xbox sub_8272CC18", false},
};

const char* const kJoinRefuse[] = {
    "none",
    "tried_to_join_self",
    "unable_to_connect",
    "join_timed_out",
    "session_not_found",
    "session_privacy",
    "session_not_joinable",
    "session_full",
    "no_ambassador_reservation",
    "no_split_screen_on_alpha",
    "session_disbanded",
    "session_boot",
    "address_invalid",
    "address_decode_failed",
    "too_many_observers",
    "abort_successful",
    "abort_ignored",
    "wrong_silo_id",
    "wrong_payload_type",
    "no_reservation",
    "in_matchmaking",
    "player_count_zero",
    "player_not_online_enabled",
    "player_add_pending",
    "player_add_failed",
    "host_timed_out",
    "rejected",
    "peer_version_too_low",
    "host_version_too_low",
    "executable_type_mismatch",
    "holding_in_queue",
    "holding_in_queue_while_joining",
    "join_to_joining_host_aborted",
    "join_to_joining_host_timed_out",
    "enough_in_progress_entries",
    "mismatched_content_revisions",
};

const char* const kBapStates[] = {
    "_none", "_acquiring_server", "_connecting", "_listening", "_channel_starting", "_authenticating",
    "_setup_primary_register_subscriber", "_setup_primary_register_relay_client", "_connected", "_dead",
};

const char* const kBapReasons[] = {
    "_none",
    "reset",
    "failed_to_acquire_server",
    "failed_to_create_transport_ip",
    "failed_to_call_transport_endpoint_async_connect",
    "failed_to_call_transport_endpoint_async_is_connected",
    "failed_to_set_socket_options",
    "failed_to_generate_login_message",
    "failed_to_generate_keepalive",
    "failed_to_generate_push_registration",
    "failed_to_generate_relay_client_registration",
    "timed_out",
    "envelope_version_mismatch",
    "unknown_envelope_type",
    "unknown_message_type",
    "failed_to_decode_bap_header",
    "failed_to_decode_header",
    "header_base_decode_mismatch",
    "failed_to_find_message_recipient",
    "failed_to_get_memory_for_response_message",
    "failed_to_get_memory_for_push_message",
    "failed_to_handle_response_fatal",
    "failed_to_handle_response_retry",
    "handling_response_closed_bap_connection",
    "failed_to_handle_push_fatal",
    "failed_to_handle_push_retry",
    "handling_push_closed_bap_connection",
    "failed_to_read_endpoint_closed",
    "failed_to_read_unknown",
    "failed_to_send_endpoint_closed",
    "failed_to_send_unknown",
    "consumer_failure",
    "suicide",
    "listener_failed_to_bind",
    "listener_failed_to_listen",
    "listener_failed_to_set_socket",
    "invalid_message_signature",
    "failed_to_establish_secure_channel",
    "failed_to_allocate_for_scratch_buffer",
    "failed_to_allocate_for_in_flight_request",
    "failed_to_allocate_for_queue_fill",
    "failed_to_encode_message_for_queue_fill",
    "failed_to_notify_message_in_flight",
    "bad_signature",
    "failed_to_generate_channel_startup_request",
};

const char* const kRscEvents[] = {
    "(none)",
    "request_begin_events",
    "load_manifest",
    "desire_loaded_manifest",
    "unload_manifest",
    "load_on_demand_tag",
    "desire_loaded_on_demand_tag",
    "unload_on_demand_tag",
    "request_end_events",
    "resolve_request",
    "pause",
    "unpause",
    "cancel",
    "create_gear_plate",
    "destroy_gear_plate",
    "set_error_state",
    "register_package_async",
    "unregister_package_async",
    "verify_all_packages",
};

template <size_t N>
const char* pick(const char* const (&table)[N], uint32_t i) {
    return i < N ? table[i] : nullptr;
}

}   

const char* stateName(uint32_t state) { return pick(kStates, state); }

std::string stateLabel(uint32_t state) {
    if (const char* n = stateName(state)) return std::to_string(state) + " " + n;
    if (state == 0xFFFFFFFFu) return "none";
    return "state " + std::to_string(state);
}

const char* abortReasonName(uint32_t reason) {
    for (const Reason& r : kReasons)
        if (r.code == reason) return r.name;
    return nullptr;
}

std::string abortReasonLabel(uint32_t reason) {
    for (const Reason& r : kReasons)
        if (r.code == reason) {
            const bool xboxName = std::string(r.name).rfind("Xbox ", 0) == 0;
            return std::to_string(reason) + " " + r.name + (r.verified || xboxName ? "" : " (Xbox label)");
        }
    return std::to_string(reason) + " (unknown code)";
}

const char* joinRefuseReasonName(uint32_t reason) { return pick(kJoinRefuse, reason); }
const char* bapStateName(uint32_t s) { return pick(kBapStates, s); }
const char* bapReasonName(uint32_t r) { return pick(kBapReasons, r); }

int peerLadder(uint32_t s) {
    switch (s) {
    case 0: return 1;
    case 8: return 3;
    case 3: return 2;
    case 9: return 5;
    default: return 4;
    }
}

const char* resourcererEventName(uint32_t e) { return pick(kRscEvents, e); }

}   
