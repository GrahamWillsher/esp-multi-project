#pragma once

#include <cstdint>
#include <functional>
#include <esp32common/espnow/common.h>
#include <esp32common/espnow/message_router.h>
#include <esp32common/espnow/standard_handlers.h>

struct RxProbeAckThrottleState {
    uint32_t last_probe_ack_ms = 0;
    uint32_t last_probe_ack_seq = 0;
    uint8_t last_probe_ack_mac[6] = {0};
};

bool is_discovery_message_type(uint8_t message_type);
bool is_connection_keepalive_message_type(uint8_t message_type);
bool is_payload_activity_message_type(uint8_t message_type);

void register_standard_probe_ack_routes(EspnowMessageRouter& router,
                                        EspnowStandardHandlers::ProbeHandlerConfig& probe_config,
                                        EspnowStandardHandlers::AckHandlerConfig& ack_config,
                                        RxProbeAckThrottleState& throttle_state);

void register_standard_type_catalog_fragment_routes(
    EspnowMessageRouter& router,
    const std::function<void(const espnow_queue_msg_t* msg,
                             uint8_t fragment_type,
                             const char* label)>& fragment_handler);

void register_standard_packet_subtype_routes(
    EspnowMessageRouter& router,
    const std::function<void(const espnow_queue_msg_t* msg,
                             uint8_t packet_subtype,
                             const char* label)>& packet_handler);

// Registers the shared msg_heartbeat route.
// Decodes the heartbeat_t payload and forwards to RxHeartbeatManager.
// Optional on_received callback is invoked after the manager processes the heartbeat;
// use it for receiver-specific post-processing (activity marking, log flags, etc.).
void register_standard_heartbeat_route(
    EspnowMessageRouter& router,
    const std::function<void(const uint8_t* mac)>& on_received = nullptr);

// Registers the msg_connect_confirm route (Phase 2 bidirectional handshake).
// Must be called once during route setup on every receiver.
// Re-registers the TX peer with channel=0 and sends connect_confirm_ack;
// on success calls ReceiverConnectionHandler::on_peer_registered().
void register_standard_connect_confirm_route(EspnowMessageRouter& router);
