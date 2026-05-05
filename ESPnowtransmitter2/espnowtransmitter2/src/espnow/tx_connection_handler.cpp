/**
 * @file tx_connection_handler.cpp
 * @brief Transmitter connection handler -- thin bridge between
 *        EspNowConnectionManager callbacks and TxReconnectManager.
 *
 * DESIGN
 * ──────
 * This class now has a single job: translate EspNowConnectionManager
 * state-change events into TxReconnectManager notifications, and keep
 * a local cache of receiver MAC/channel for other components to read.
 *
 * No reconnect orchestration, no backoff, no deferred peer registration,
 * no cross-task shared flags.  Those concerns are in TxReconnectManager.
 */

#include "tx_connection_handler.h"
#include "tx_reconnect_manager.h"
#include "heartbeat_manager.h"
#include "tx_state_machine.h"
#include "tx_send_guard.h"
#include "version_beacon_manager.h"
#include "../battery_emulator/devboard/utils/led_handler.h"
#include <esp32common/espnow/connection_manager.h>
#include <esp32common/espnow/mac_utils.h>
#include <esp32common/config/timing_config.h>
#include <espnow_peer_manager.h>
#include <esp32common/logging/logging_config.h>
#include <Arduino.h>
#include <cstring>

// ============================================================================
// Singleton
// ============================================================================
TransmitterConnectionHandler& TransmitterConnectionHandler::instance() {
    static TransmitterConnectionHandler inst;
    return inst;
}

TransmitterConnectionHandler::TransmitterConnectionHandler() {
    memset(receiver_mac_, 0, sizeof(receiver_mac_));
}

// ============================================================================
// init — register state-change callbacks
// ============================================================================
void TransmitterConnectionHandler::init() {
    LOG_INFO("TX_CONN", "Initialising transmitter connection handler...");

    // Heartbeat drives CONNECTION_LOST when TX stops receiving ACKs for 35 s.
    EspNowConnectionManager::instance().set_heartbeat_timeout_ms(
        TimingConfig::TX_HEARTBEAT_TIMEOUT_MS);
    EspNowConnectionManager::instance().set_heartbeat_timeout_enabled(true);

    EspNowConnectionManager::instance().register_state_callback(
        [](EspNowConnectionState old_state, EspNowConnectionState new_state) {
            LOG_INFO("TX_CONN", "State: %s -> %s",
                     espnow_state_to_string(old_state),
                     espnow_state_to_string(new_state));

            // ── IDLE -> CONNECTING (initial boot or auto-reconnect) ───────
            if (old_state == EspNowConnectionState::IDLE &&
                new_state == EspNowConnectionState::CONNECTING) {
                TxStateMachine::instance().on_discovery_started();
                // Tell the reconnect manager to start/continue scanning.
                // If a scan is already running (e.g. CONNECTING timeout
                // caused IDLE->CONNECTING re-entry), manager ignores this.
                TxReconnectManager::instance().notify(ReconnectEvent::CONNECT);
            }

            // ── -> CONNECTED ─────────────────────────────────────────────
            else if (new_state == EspNowConnectionState::CONNECTED) {
                TxSendGuard::notify_connection_state(true);
                HeartbeatManager::instance().reset();

                const uint8_t channel =
                    TransmitterConnectionHandler::instance().get_receiver_channel();
                if (channel > 0) {
                    TxStateMachine::instance().on_connected(channel);
                }

                // Force one authoritative LED state replay so the receiver UI
                // converges even if early publishes were missed.
                const uint8_t* peer_mac =
                    EspNowConnectionManager::instance().get_peer_mac();
                if (peer_mac) {
                    const esp_err_t rc = led_publish_current_state(true, peer_mac);
                    if (rc != ESP_OK &&
                        rc != ESP_ERR_INVALID_STATE &&
                        rc != ESP_ERR_INVALID_ARG) {
                        LOG_WARN("TX_CONN",
                                 "LED replay on connect failed: %s",
                                 esp_err_to_name(rc));
                    }
                }

                // Tell reconnect manager we are done (it is likely already
                // in IDLE, but this is a no-op if so).
                TxReconnectManager::instance().notify(ReconnectEvent::STOP);
                LOG_INFO("TX_CONN", "Connected on ch=%d", channel);
            }

            // ── CONNECTED -> IDLE (heartbeat timeout / connection lost) ───
            else if (old_state == EspNowConnectionState::CONNECTED &&
                     new_state == EspNowConnectionState::IDLE) {
                TxSendGuard::notify_connection_state(false);
                HeartbeatManager::instance().reset();
                TxStateMachine::instance().on_connection_lost();

                // Remove peer and unlock channel
                const uint8_t* peer_mac =
                    TransmitterConnectionHandler::instance().get_receiver_mac();
                if (peer_mac && !EspNowMacUtils::is_broadcast_mac(peer_mac) &&
                    EspnowPeerManager::is_peer_registered(peer_mac)) {
                    if (EspnowPeerManager::remove_peer(peer_mac)) {
                        LOG_INFO("TX_CONN", "Peer removed on connection loss");
                    }
                }

                // NOTE: auto-reconnect will post CONNECTION_START -> CONNECTING
                // which triggers the IDLE->CONNECTING callback above, which
                // notifies TxReconnectManager.  No explicit notify needed here.
                LOG_INFO("TX_CONN",
                         "Connection lost -- auto-reconnect will trigger discovery");
            }

            // ── CONNECTING -> IDLE (35 s timeout cycling) ─────────────────
            // Auto-reconnect posts CONNECTION_START -> CONNECTING immediately.
            // The resulting IDLE->CONNECTING callback notifies the manager,
            // which ignores the duplicate CONNECT if a scan is already running.
            else if (old_state == EspNowConnectionState::CONNECTING &&
                     new_state == EspNowConnectionState::IDLE) {
                LOG_INFO("TX_CONN",
                         "CONNECTING timeout -> IDLE (auto-reconnect will re-enter CONNECTING)");
            }
        });

    LOG_INFO("TX_CONN", "Transmitter connection handler initialised");
}

// ============================================================================
// start_discovery — posts CONNECTION_START to kick off first connection
// ============================================================================
void TransmitterConnectionHandler::start_discovery() {
    post_connection_event(EspNowEvent::CONNECTION_START, nullptr);
}

// ============================================================================
// on_ack_received — cache update only (called by TxReconnectManager)
// ============================================================================
void TransmitterConnectionHandler::on_ack_received(const uint8_t* receiver_mac,
                                                    uint8_t        channel) {
    if (receiver_mac) memcpy(receiver_mac_, receiver_mac, sizeof(receiver_mac_));
    receiver_channel_ = channel;
    LOG_DEBUG("TX_CONN", "ACK cached: ch=%d", channel);
}

// ============================================================================
// on_peer_registered — posts PEER_REGISTERED to drive CONNECTING->CONNECTED
// ============================================================================
void TransmitterConnectionHandler::on_peer_registered(const uint8_t* receiver_mac) {
    if (receiver_mac) memcpy(receiver_mac_, receiver_mac, sizeof(receiver_mac_));

    if (EspNowConnectionManager::instance().get_state() ==
        EspNowConnectionState::CONNECTING) {
        post_connection_event(EspNowEvent::PEER_REGISTERED, receiver_mac_);
        LOG_INFO("TX_CONN", "PEER_REGISTERED posted -> CONNECTED");
    } else {
        LOG_WARN("TX_CONN",
                 "on_peer_registered called in state %s -- ignored",
                 espnow_state_to_string(
                     EspNowConnectionManager::instance().get_state()));
    }
}
