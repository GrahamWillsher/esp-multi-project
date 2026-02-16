/**
 * @file tx_connection_handler.cpp
 * @brief Transmitter-specific connection handler implementation
 */

#include "tx_connection_handler.h"
#include "discovery_task.h"
#include <connection_manager.h>
#include <channel_manager.h>
#include <espnow_peer_manager.h>
#include <logging_config.h>
#include <Arduino.h>
#include <cstring>

TransmitterConnectionHandler& TransmitterConnectionHandler::instance() {
    static TransmitterConnectionHandler instance;
    return instance;
}

TransmitterConnectionHandler::TransmitterConnectionHandler()
    : receiver_channel_(0) {
    memset(receiver_mac_, 0, sizeof(receiver_mac_));
}

void TransmitterConnectionHandler::init() {
    LOG_INFO("TX_CONN", "Initializing transmitter connection handler...");
    
    // Register state change callback
    EspNowConnectionManager::instance().register_state_callback(
        [](EspNowConnectionState old_state, EspNowConnectionState new_state) {
            LOG_INFO("TX_CONN", "State change: %s → %s",
                     state_to_string(old_state),
                     state_to_string(new_state));
            
            if (old_state == EspNowConnectionState::IDLE && new_state == EspNowConnectionState::CONNECTING) {
                // Start discovery when entering CONNECTING state
                LOG_INFO("TX_CONN", "Entering CONNECTING - starting discovery");
                TransmitterConnectionHandler::instance().start_discovery();
                
            } else if (new_state == EspNowConnectionState::CONNECTED) {
                // Lock channel when connected
                uint8_t channel = TransmitterConnectionHandler::instance().get_receiver_channel();
                if (channel > 0) {
                    ChannelManager::instance().lock_channel(channel, "TX_CONN");
                }
                
                LOG_INFO("TX_CONN", "✓ Connected - channel locked");
                
            } else if (old_state == EspNowConnectionState::CONNECTED && 
                       new_state == EspNowConnectionState::IDLE) {
                // Clean up peer when connection lost
                const uint8_t* peer_mac = EspNowConnectionManager::instance().get_peer_mac();
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
    DiscoveryTask::instance().start_active_channel_hopping();
}

void TransmitterConnectionHandler::on_ack_received(const uint8_t* receiver_mac, uint8_t channel) {
    if (receiver_mac) {
        memcpy(receiver_mac_, receiver_mac, sizeof(receiver_mac_));
    }
    receiver_channel_ = channel;

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

    // Only post PEER_REGISTERED event if we're in CONNECTING state
    // This prevents posting in IDLE state when discovery is racing with state transitions
    EspNowConnectionManager& conn_mgr = EspNowConnectionManager::instance();
    if (conn_mgr.get_state() == EspNowConnectionState::CONNECTING) {
        post_connection_event(EspNowEvent::PEER_REGISTERED, receiver_mac_);
    } else {
        LOG_WARN("TX_CONN", "on_peer_registered() called in state %d (expected CONNECTING), deferring event",
                 static_cast<int>(conn_mgr.get_state()));
    }
}
