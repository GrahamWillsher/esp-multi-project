/**
 * @file tx_connection_handler.cpp
 * @brief Transmitter-specific connection handler implementation
 */

#include "tx_connection_handler.h"
#include "discovery_task.h"
#include "heartbeat_manager.h"
#include "tx_state_machine.h"
#include "tx_send_guard.h"
#include "../config/task_config.h"
#include <esp32common/espnow/connection_manager.h>
#include <channel_manager.h>
#include <espnow_peer_manager.h>
#include <esp32common/logging/logging_config.h>
#include <Arduino.h>
#include <cstring>

TransmitterConnectionHandler& TransmitterConnectionHandler::instance() {
    static TransmitterConnectionHandler instance;
    return instance;
}

static bool has_valid_mac(const uint8_t* mac) {
    if (!mac) {
        return false;
    }
    for (int index = 0; index < 6; ++index) {
        if (mac[index] != 0) {
            return true;
        }
    }
    return false;
}

TransmitterConnectionHandler::TransmitterConnectionHandler()
    : receiver_channel_(0),
      last_known_channel_(0),
      has_cached_channel_(false) {
    memset(receiver_mac_, 0, sizeof(receiver_mac_));
        memset(deferred_peer_mac_, 0, sizeof(deferred_peer_mac_));
}

void TransmitterConnectionHandler::init() {
    LOG_INFO("TX_CONN", "Initializing transmitter connection handler...");

    // Make common connection manager the single owner of connection timeout.
    // TX liveness is based on heartbeat ACK activity with a 35s threshold.
    EspNowConnectionManager::instance().set_heartbeat_timeout_ms(timing::TX_HEARTBEAT_TIMEOUT_MS);
    EspNowConnectionManager::instance().set_heartbeat_timeout_enabled(true);
    
    // Register state change callback
    EspNowConnectionManager::instance().register_state_callback(
        [](EspNowConnectionState old_state, EspNowConnectionState new_state) {
            LOG_INFO("TX_CONN", "State change: %s → %s",
                     espnow_state_to_string(old_state),
                     espnow_state_to_string(new_state));
            
            if (old_state == EspNowConnectionState::IDLE && new_state == EspNowConnectionState::CONNECTING) {
                TransmitterConnectionHandler::instance().peer_register_event_posted_ = false;
                TransmitterConnectionHandler::instance().flush_deferred_peer_registered();
                // Start discovery when entering CONNECTING state
                LOG_INFO("TX_CONN", "Entering CONNECTING - starting discovery");
                TxStateMachine::instance().on_discovery_started();
                
                // PHASE 0: Check if we should use backoff
                if (EspNowConnectionManager::instance().should_attempt_reconnect()) {
                    EspNowConnectionManager::instance().on_reconnect_attempt();
                    TransmitterConnectionHandler::instance().start_discovery_hopping_only();
                } else {
                    TransmitterConnectionHandler::instance().deferred_discovery_start_ = true;
                    TransmitterConnectionHandler::instance().deferred_discovery_due_ms_ = millis() + timing::DEFERRED_DISCOVERY_POLL_MS;
                    LOG_INFO("TX_CONN", "Backoff active - deferring discovery start");
                }
                
            } else if (new_state == EspNowConnectionState::CONNECTED) {
                TxSendGuard::notify_connection_state(true);
                TransmitterConnectionHandler::instance().peer_register_event_posted_ = false;
                TransmitterConnectionHandler::instance().peer_registered_deferred_ = false;
                // Lock channel when connected
                uint8_t channel = TransmitterConnectionHandler::instance().get_receiver_channel();
                if (channel > 0) {
                    ChannelManager::instance().lock_channel(channel, "TX_CONN");
                    HeartbeatManager::instance().reset();
                    TxStateMachine::instance().on_connected(channel);
                }
                
                LOG_INFO("TX_CONN", "✓ Connected - channel locked");
                
            } else if (old_state == EspNowConnectionState::CONNECTED && 
                       new_state == EspNowConnectionState::IDLE) {
                TxSendGuard::notify_connection_state(false);
                TransmitterConnectionHandler::instance().peer_register_event_posted_ = false;
                TransmitterConnectionHandler::instance().peer_registered_deferred_ = false;
                HeartbeatManager::instance().reset();
                TxStateMachine::instance().on_connection_lost();
                // Clean up peer when connection lost
                const uint8_t* peer_mac = TransmitterConnectionHandler::instance().get_receiver_mac();
                if (peer_mac) {
                    // Check if it's not broadcast address before removing
                    bool is_broadcast = true;
                    for (int i = 0; i < 6; i++) {
                        if (peer_mac[i] != 0xFF) {
                            is_broadcast = false;
                            break;
                        }
                    }
                    
                    if (!is_broadcast && EspnowPeerManager::is_peer_registered(peer_mac)) {
                        if (EspnowPeerManager::remove_peer(peer_mac)) {
                            LOG_INFO("TX_CONN", "✓ Removed peer on connection loss");
                        } else {
                            LOG_WARN("TX_CONN", "Failed to remove peer on connection loss");
                        }
                    }
                }
                
                // Unlock channel and restart discovery when connection lost
                ChannelManager::instance().unlock_channel("TX_CONN");
                
                // Auto-reconnect will automatically restart discovery via CONNECTION_START event
                LOG_INFO("TX_CONN", "✓ Connection lost - peer cleaned up, channel unlocked, auto-reconnect will trigger discovery");
            }
        }
    );
    
    LOG_INFO("TX_CONN", "✓ Transmitter connection handler initialized");
}

void TransmitterConnectionHandler::start_discovery() {
    // Post CONNECTION_START event (common manager)
    post_connection_event(EspNowEvent::CONNECTION_START, nullptr);

    // Start active channel hopping (TX-specific)
    start_discovery_hopping_only();
}

void TransmitterConnectionHandler::start_discovery_hopping_only() {
    deferred_discovery_start_ = false;
    deferred_discovery_due_ms_ = 0;
    DiscoveryTask::instance().start_active_channel_hopping();
}

void TransmitterConnectionHandler::on_ack_received(const uint8_t* receiver_mac, uint8_t channel) {
    if (EspNowConnectionManager::instance().is_connected() &&
        receiver_mac &&
        memcmp(receiver_mac_, receiver_mac, sizeof(receiver_mac_)) == 0 &&
        receiver_channel_ == channel) {
        LOG_DEBUG("TX_CONN", "ACK for already-connected peer on channel %u - ignoring duplicate PEER_FOUND", channel);
        return;
    }

    if (receiver_mac) {
        memcpy(receiver_mac_, receiver_mac, sizeof(receiver_mac_));
    }
    receiver_channel_ = channel;
    
    // PHASE 0: Cache channel for fast reconnection
    last_known_channel_ = channel;
    has_cached_channel_ = true;
    LOG_DEBUG("TX_CONN", "Cached channel %d for fast reconnection", channel);

    // Post PEER_FOUND event (common manager)
    post_connection_event(EspNowEvent::PEER_FOUND, receiver_mac);
}

void TransmitterConnectionHandler::on_peer_registered(const uint8_t* receiver_mac_param) {
    if (receiver_mac_param) {
        memcpy(receiver_mac_, receiver_mac_param, sizeof(receiver_mac_));
        
        LOG_INFO("TX_CONN", "Peer registered: %02X:%02X:%02X:%02X:%02X:%02X",
                 receiver_mac_param[0], receiver_mac_param[1], receiver_mac_param[2],
                 receiver_mac_param[3], receiver_mac_param[4], receiver_mac_param[5]);
    }

    // Post PEER_REGISTERED only once while CONNECTING.
    // If not CONNECTING, latch a real deferred event with TTL.
    EspNowConnectionManager& conn_mgr = EspNowConnectionManager::instance();
    const auto state = conn_mgr.get_state();

    if (state == EspNowConnectionState::CONNECTING) {
        if (peer_register_event_posted_) {
            LOG_DEBUG("TX_CONN", "Duplicate on_peer_registered() while CONNECTING - ignoring");
            return;
        }
        post_connection_event(EspNowEvent::PEER_REGISTERED, receiver_mac_);
        peer_register_event_posted_ = true;
        peer_registered_deferred_ = false;
        deferred_peer_registered_ms_ = 0;
        memset(deferred_peer_mac_, 0, sizeof(deferred_peer_mac_));
    } else if (state == EspNowConnectionState::CONNECTED) {
        LOG_DEBUG("TX_CONN", "on_peer_registered() received while CONNECTED - ignoring duplicate");
    } else {
        peer_registered_deferred_ = has_valid_mac(receiver_mac_);
        if (peer_registered_deferred_) {
            memcpy(deferred_peer_mac_, receiver_mac_, sizeof(deferred_peer_mac_));
            deferred_peer_registered_ms_ = millis();
            LOG_INFO("TX_CONN", "on_peer_registered() in state %d - deferred until CONNECTING",
                     static_cast<int>(state));
        } else {
            LOG_WARN("TX_CONN", "on_peer_registered() in state %d but MAC invalid - dropping",
                     static_cast<int>(state));
        }
    }
}

void TransmitterConnectionHandler::flush_deferred_peer_registered() {
    if (!peer_registered_deferred_) {
        return;
    }

    const uint32_t now = millis();
    if (deferred_peer_registered_ms_ > 0 && (now - deferred_peer_registered_ms_) > DEFERRED_PEER_TTL_MS) {
        LOG_WARN("TX_CONN", "Dropping stale deferred PEER_REGISTERED (%lu ms old)",
                 now - deferred_peer_registered_ms_);
        peer_registered_deferred_ = false;
        deferred_peer_registered_ms_ = 0;
        memset(deferred_peer_mac_, 0, sizeof(deferred_peer_mac_));
        return;
    }

    if (EspNowConnectionManager::instance().get_state() != EspNowConnectionState::CONNECTING) {
        return;
    }

    if (peer_register_event_posted_) {
        return;
    }

    post_connection_event(EspNowEvent::PEER_REGISTERED, deferred_peer_mac_);
    peer_register_event_posted_ = true;
    peer_registered_deferred_ = false;
    deferred_peer_registered_ms_ = 0;
    memset(deferred_peer_mac_, 0, sizeof(deferred_peer_mac_));
    LOG_INFO("TX_CONN", "Flushed deferred PEER_REGISTERED in CONNECTING state");
}

void TransmitterConnectionHandler::tick() {
    flush_deferred_peer_registered();

    if (!deferred_discovery_start_) {
        return;
    }

    // Defer window not yet reached
    const uint32_t now = millis();
    if ((int32_t)(now - deferred_discovery_due_ms_) < 0) {
        return;
    }

    // If no longer connecting, cancel deferred start
    if (EspNowConnectionManager::instance().get_state() != EspNowConnectionState::CONNECTING) {
        deferred_discovery_start_ = false;
        deferred_discovery_due_ms_ = 0;
        return;
    }

    // Re-check common backoff gate and launch when allowed
    if (EspNowConnectionManager::instance().should_attempt_reconnect()) {
        EspNowConnectionManager::instance().on_reconnect_attempt();
        LOG_INFO("TX_CONN", "Backoff window elapsed - starting deferred discovery");
        start_discovery_hopping_only();
    } else {
        // Poll gently until backoff allows next attempt
        deferred_discovery_due_ms_ = now + timing::DEFERRED_DISCOVERY_POLL_MS;
    }
}
