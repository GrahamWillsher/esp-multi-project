#include "rx_route_registry.h"

#include <Arduino.h>
#include <cstring>
#include <esp32common/espnow/connection_manager.h>
#include <esp32common/espnow/rx_heartbeat_manager.h>
#include <esp32common/espnow/tx_scheduler.h>
#include <esp_now.h>
#include <espnow_peer_manager.h>
#include "rx_connection_handler.h"
#include <logging_config.h>

namespace {

bool should_send_probe_ack(RxProbeAckThrottleState& throttle_state,
                           const uint8_t* mac,
                           EspNowConnectionState state,
                           uint32_t now) {
    (void)state;
    const bool same_peer =
        (std::memcmp(throttle_state.last_probe_ack_mac, mac, sizeof(throttle_state.last_probe_ack_mac)) == 0);

    const bool connected = (state == EspNowConnectionState::CONNECTED);
    // Discovery reliability requires at least one ACK opportunity per channel
    // dwell window. If disconnected throttle is too large and the first ACK
    // enqueue hits transient NO_MEM, the channel can be missed entirely.
    // Use adaptive throttling:
    //   - low pressure: allow retries within dwell (faster acquisition)
    //   - elevated pressure: back off to reduce ACK amplification bursts.
    const uint32_t no_mem = EspnowTxScheduler::get_consecutive_no_mem_count();
    const uint32_t disconnected_throttle_ms = (no_mem >= 2U) ? 1200U : 450U;
    const uint32_t ack_throttle_ms = connected ? 1200U : disconnected_throttle_ms;

    if (same_peer && ((now - throttle_state.last_probe_ack_ms) < ack_throttle_ms)) {
        return false;
    }

    return true;
}

void record_probe_ack_sent(RxProbeAckThrottleState& throttle_state,
                           const uint8_t* mac,
                           uint32_t seq,
                           uint32_t now) {
    throttle_state.last_probe_ack_ms = now;
    throttle_state.last_probe_ack_seq = seq;
    std::memcpy(throttle_state.last_probe_ack_mac, mac, sizeof(throttle_state.last_probe_ack_mac));
}

}  // namespace

bool is_discovery_message_type(uint8_t message_type) {
    return message_type == msg_probe || message_type == msg_ack;
}

bool is_connection_keepalive_message_type(uint8_t message_type) {
    return message_type == msg_heartbeat ||
           message_type == msg_heartbeat_ack ||
           message_type == msg_version_beacon ||
           message_type == msg_time_transitions_snapshot;
}

bool is_payload_activity_message_type(uint8_t message_type) {
    return !is_discovery_message_type(message_type) &&
           !is_connection_keepalive_message_type(message_type);
}

void register_standard_probe_ack_routes(EspnowMessageRouter& router,
                                        EspnowStandardHandlers::ProbeHandlerConfig& probe_config,
                                        EspnowStandardHandlers::AckHandlerConfig& ack_config,
                                        RxProbeAckThrottleState& throttle_state) {
    const auto user_ack_send_result_cb = probe_config.on_ack_send_result;

    // Preserve caller-provided probe/connection callbacks and optional state
    // pointers. The shared registration helper must not clear project wiring
    // (for example ReceiverConnectionHandler::on_probe_received ingress).
    probe_config.send_ack_response = true;
    probe_config.on_ack_send_result = [user_ack_send_result_cb, &throttle_state](const uint8_t* mac,
                                                                                  uint32_t seq,
                                                                                  bool success,
                                                                                  esp_err_t result) {
        if (!success) {
            LOG_WARN("ESPNOW", "Discovery ACK failed (seq=%lu): %s",
                     static_cast<unsigned long>(seq),
                     esp_err_to_name(result));
        } else {
            record_probe_ack_sent(throttle_state, mac, seq, millis());
        }

        if (user_ack_send_result_cb) {
            user_ack_send_result_cb(mac, seq, success, result);
        }
    };

    router.register_route(msg_probe,
        [&probe_config, &throttle_state](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            const auto state = EspNowConnectionManager::instance().get_state();
            bool send_probe_ack = true;

            if (msg && msg->len >= static_cast<int>(sizeof(probe_t))) {
                const auto* probe = reinterpret_cast<const probe_t*>(msg->data);
                const uint32_t now = millis();
                send_probe_ack = should_send_probe_ack(throttle_state, msg->mac, state, now);
            }

            probe_config.send_ack_response = send_probe_ack;
            EspnowStandardHandlers::handle_probe(msg, &probe_config);
        },
        0xFF,
        nullptr);

    router.register_route(msg_ack,
        [&ack_config](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            EspnowStandardHandlers::handle_ack(msg, &ack_config);
        },
        0xFF,
        nullptr);
}

    void register_standard_connect_confirm_route(EspnowMessageRouter& router) {
        // ── msg_connect_confirm (TX → RX, Phase 2 connection handshake) ─────────
        // Received after the TX gets a PROBE ACK. Re-register the TX peer with
        // channel=0 (follow current WiFi AP channel) and queue confirm_ack send.
        // ReceiverConnectionHandler now retries confirm_ack after transient
        // ESP_ERR_ESPNOW_NO_MEM failures instead of treating the handshake as
        // a one-shot operation.
        // Old TX firmware that does not send this message is unaffected — the probe
        // path still posts PEER_REGISTERED via on_peer_registered() as before.
        router.register_route(
            msg_connect_confirm,
            [](const espnow_queue_msg_t* msg, void* /*ctx*/) {
                if (!msg || msg->len < static_cast<int>(sizeof(espnow_connect_confirm_t))) {
                    LOG_WARN("RX_CONN", "connect_confirm: message too short (%d bytes)", msg ? msg->len : 0);
                    return;
                }
                const auto* confirm =
                    reinterpret_cast<const espnow_connect_confirm_t*>(msg->data);

                LOG_INFO("RX_CONN",
                         "connect_confirm received (session=%u  protocol=%u  ch=%d)",
                         static_cast<unsigned>(confirm->session_id),
                         static_cast<unsigned>(confirm->protocol_version),
                         static_cast<int>(confirm->channel));

                // Re-register TX peer with channel=0 so sends follow the AP channel.
                // Remove+add is safe even if peer is not yet registered.
                EspnowPeerManager::remove_peer(msg->mac);
                EspnowPeerManager::add_peer(const_cast<uint8_t*>(msg->mac), /*channel=*/0);
                ReceiverConnectionHandler::instance().on_connect_confirm_received(
                    msg->mac,
                    confirm->session_id,
                    confirm->session_boot_nonce,
                    confirm->protocol_version);
            },
            0xFF,
            nullptr);
    }

void register_standard_type_catalog_fragment_routes(
    EspnowMessageRouter& router,
    const std::function<void(const espnow_queue_msg_t* msg,
                             uint8_t fragment_type,
                             const char* label)>& fragment_handler) {
    if (!fragment_handler) {
        return;
    }

    router.register_route(msg_battery_types_fragment,
        [fragment_handler](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            fragment_handler(msg, msg_battery_types_fragment, "BATTERY_TYPES_FRAGMENT");
        },
        0xFF,
        nullptr);

    router.register_route(msg_inverter_types_fragment,
        [fragment_handler](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            fragment_handler(msg, msg_inverter_types_fragment, "INVERTER_TYPES_FRAGMENT");
        },
        0xFF,
        nullptr);

    router.register_route(msg_inverter_interfaces_fragment,
        [fragment_handler](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            fragment_handler(msg, msg_inverter_interfaces_fragment, "INVERTER_INTERFACES_FRAGMENT");
        },
        0xFF,
        nullptr);
}

void register_standard_packet_subtype_routes(
    EspnowMessageRouter& router,
    const std::function<void(const espnow_queue_msg_t* msg,
                             uint8_t packet_subtype,
                             const char* label)>& packet_handler) {
    if (!packet_handler) {
        return;
    }

    router.register_route(msg_packet,
        [packet_handler](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            packet_handler(msg, subtype_events, "PACKET_EVENTS");
        },
        subtype_events,
        nullptr);

    router.register_route(msg_packet,
        [packet_handler](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            packet_handler(msg, subtype_logs, "PACKET_LOGS");
        },
        subtype_logs,
        nullptr);

    router.register_route(msg_packet,
        [packet_handler](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            packet_handler(msg, subtype_cell_info, "PACKET_CELL_INFO");
        },
        subtype_cell_info,
        nullptr);
}

void register_standard_heartbeat_route(
    EspnowMessageRouter& router,
    const std::function<void(const uint8_t* mac)>& on_received) {
    router.register_route(msg_heartbeat,
        [on_received](const espnow_queue_msg_t* msg, void* /*ctx*/) {
            if (!msg || msg->len < static_cast<int>(sizeof(heartbeat_t))) {
                return;
            }
            const auto* hb = reinterpret_cast<const heartbeat_t*>(msg->data);
            RxHeartbeatManager::instance().on_heartbeat(hb, msg->mac);
            if (on_received) {
                on_received(msg->mac);
            }
        },
        0xFF,
        nullptr);
}

