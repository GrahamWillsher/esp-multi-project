#include "rx_connection_handler.h"
#include "rx_heartbeat_manager.h"
#include "../config/logging_config.h"
#include "../../lib/webserver/utils/transmitter_manager.h"
#include <connection_manager.h>
#include <connection_event.h>
#include <channel_manager.h>
#include <espnow_peer_manager.h>
#include <firmware_version.h>
#include <Arduino.h>
#include <cstring>

ReceiverConnectionHandler& ReceiverConnectionHandler::instance() {
    static ReceiverConnectionHandler instance;
    return instance;
}

ReceiverConnectionHandler::ReceiverConnectionHandler()
    : last_rx_time_ms_(0) {
    memset(transmitter_mac_, 0, sizeof(transmitter_mac_));
}

void ReceiverConnectionHandler::init() {
    last_rx_time_ms_ = millis();
    
    // Register state change callback
    EspNowConnectionManager::instance().register_state_callback(
        [](EspNowConnectionState old_state, EspNowConnectionState new_state) {
            LOG_INFO("RX_CONN", "State change: %s → %s",
                     state_to_string(old_state),
                     state_to_string(new_state));
            
            if (new_state == EspNowConnectionState::CONNECTED) {
                // Lock channel when connected (receiver doesn't hop but should lock)
                uint8_t current_channel = ChannelManager::instance().get_channel();
                ChannelManager::instance().lock_channel(current_channel, "RX_CONN");
                LOG_INFO("RX_CONN", "✓ Connected - channel locked at %d", current_channel);
                
                // Send initialization requests now that connection is fully established
                ReceiverConnectionHandler::instance().send_initialization_requests(
                    EspNowConnectionManager::instance().get_peer_mac());
                
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
                            LOG_INFO("RX_CONN", "✓ Removed peer on connection loss");
                        } else {
                            LOG_WARN("RX_CONN", "Failed to remove peer on connection loss");
                        }
                    }
                }
                
                // Unlock channel when connection lost
                ChannelManager::instance().unlock_channel("RX_CONN");
                LOG_INFO("RX_CONN", "✓ Connection lost - peer cleaned up, channel unlocked");
            }
        }
    );
    
    // Post CONNECTION_START event to kick state machine from IDLE → CONNECTING
    // This ensures the receiver is ready to receive peer registration events
    post_connection_event(EspNowEvent::CONNECTION_START, nullptr);
    
    LOG_INFO("RX_CONN", "✓ Receiver connection handler initialized");
}

void ReceiverConnectionHandler::on_probe_received(const uint8_t* transmitter_mac) {
    if (transmitter_mac) {
        memcpy(transmitter_mac_, transmitter_mac, sizeof(transmitter_mac_));
    }
    last_rx_time_ms_ = millis();

    // Post PEER_FOUND event (common manager)
    post_connection_event(EspNowEvent::PEER_FOUND, transmitter_mac_);
}

void ReceiverConnectionHandler::on_peer_registered(const uint8_t* transmitter_mac) {
    if (transmitter_mac) {
        memcpy(transmitter_mac_, transmitter_mac, sizeof(transmitter_mac_));
    }
    last_rx_time_ms_ = millis();

    // When peer is registered, we're moving towards connected state
    // Notify heartbeat manager to reset its timeout
    RxHeartbeatManager::instance().on_connection_established();

    // Only post PEER_REGISTERED event if we're in CONNECTING state
    // This prevents posting in IDLE state when discovery is racing with state transitions
    EspNowConnectionManager& conn_mgr = EspNowConnectionManager::instance();
    if (conn_mgr.get_state() == EspNowConnectionState::CONNECTING) {
        post_connection_event(EspNowEvent::PEER_REGISTERED, transmitter_mac_);
    } else {
        LOG_WARN("RX_CONN", "on_peer_registered() called in state %d (expected CONNECTING), deferring event",
                 static_cast<int>(conn_mgr.get_state()));
    }
}

void ReceiverConnectionHandler::on_data_received(const uint8_t* transmitter_mac) {
    if (transmitter_mac) {
        memcpy(transmitter_mac_, transmitter_mac, sizeof(transmitter_mac_));
    }
    last_rx_time_ms_ = millis();

    // Post DATA_RECEIVED event (common manager)
    post_connection_event(EspNowEvent::DATA_RECEIVED, transmitter_mac_);
}

void ReceiverConnectionHandler::send_initialization_requests(const uint8_t* transmitter_mac) {
    if (!transmitter_mac) {
        LOG_WARN("CONN_HANDLER", "Cannot send initialization - invalid transmitter MAC");
        return;
    }
    
    // Check if device is in CONNECTED state before sending requests
    auto& conn_mgr = EspNowConnectionManager::instance();
    auto state = conn_mgr.get_state();
    
    if (state != EspNowConnectionState::CONNECTED) {
        LOG_WARN("CONN_HANDLER", "Cannot send initialization - transmitter state is %u (need CONNECTED)",
                 (uint8_t)state);
        return;
    }
    
    // Mark that we've sent initialization for this connection
    // Flag will be reset only when connection is lost
    first_data_received_ = true;
    
    LOG_INFO("CONN_HANDLER", "[INIT] Connection CONFIRMED (both devices ready) - sending initialization requests");

    // Request static config sections immediately (no legacy snapshot)
    config_section_request_t mqtt_req;
    mqtt_req.type = msg_config_section_request;
    mqtt_req.section = config_section_mqtt;
    mqtt_req.requested_version = 0;  // Force send
    esp_now_send(transmitter_mac, (const uint8_t*)&mqtt_req, sizeof(mqtt_req));

    config_section_request_t net_req;
    net_req.type = msg_config_section_request;
    net_req.section = config_section_network;
    net_req.requested_version = 0;  // Force send
    esp_now_send(transmitter_mac, (const uint8_t*)&net_req, sizeof(net_req));

    config_section_request_t meta_req;
    meta_req.type = msg_config_section_request;
    meta_req.section = config_section_metadata;
    meta_req.requested_version = 0;  // Force send
    esp_now_send(transmitter_mac, (const uint8_t*)&meta_req, sizeof(meta_req));
    
    // Send REQUEST_DATA to ensure power profile stream is active
    request_data_t req_msg = { msg_request_data, subtype_power_profile };
    esp_err_t result = esp_now_send(transmitter_mac, (const uint8_t*)&req_msg, sizeof(req_msg));
    if (result == ESP_OK) {
        LOG_INFO("CONN_HANDLER", "Requested power profile data stream");
    } else if (result == ESP_ERR_ESPNOW_NOT_FOUND) {
        LOG_WARN("CONN_HANDLER", "Transmitter peer not yet ready for data request - will retry");
    } else {
        LOG_WARN("CONN_HANDLER", "Failed to request power profile: %s", esp_err_to_name(result));
    }
    
    // Send version information (static data, sent once on connection)
    version_announce_t announce;
    announce.type = msg_version_announce;
    announce.firmware_version = FW_VERSION_NUMBER;
    announce.protocol_version = PROTOCOL_VERSION;
    strncpy(announce.device_type, DEVICE_NAME, sizeof(announce.device_type) - 1);
    announce.device_type[sizeof(announce.device_type) - 1] = '\0';
    strncpy(announce.build_date, __DATE__, sizeof(announce.build_date) - 1);
    announce.build_date[sizeof(announce.build_date) - 1] = '\0';
    strncpy(announce.build_time, __TIME__, sizeof(announce.build_time) - 1);
    announce.build_time[sizeof(announce.build_time) - 1] = '\0';
    
    result = esp_now_send(transmitter_mac, (const uint8_t*)&announce, sizeof(announce));
    if (result == ESP_OK) {
        LOG_INFO("CONN_HANDLER", "Sent version info to transmitter: %d.%d.%d", 
                 FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);
    } else if (result == ESP_ERR_ESPNOW_NOT_FOUND) {
        LOG_WARN("CONN_HANDLER", "Transmitter peer not yet ready for version - will retry");
    } else {
        LOG_WARN("CONN_HANDLER", "Failed to send version info: %s", esp_err_to_name(result));
    }
    
    LOG_INFO("CONN_HANDLER", "[INIT] Initialization requests sent (will retry any that failed)");
}

void ReceiverConnectionHandler::on_connection_lost() {
    // Reset first_data_received flag to allow re-initialization on reconnect
    // This ensures that if connection is lost and then re-established,
    // we'll send initialization requests again on the next connection
    if (first_data_received_) {
        LOG_INFO("CONN_HANDLER", "[CONN_LOST] Clearing 'first data received' flag for reconnection");
        first_data_received_ = false;
    }
    
    // Log connection loss event
    LOG_WARN("CONN_HANDLER", "[CONN_LOST] Connection lost - ready for reconnection");
}
