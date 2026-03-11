#include "rx_connection_handler.h"
#include "rx_heartbeat_manager.h"
#include "rx_state_machine.h"
#include "../config/logging_config.h"
#include "../../lib/webserver/utils/transmitter_manager.h"
#include <connection_manager.h>
#include <connection_event.h>
#include <channel_manager.h>
#include <espnow_peer_manager.h>
#include <espnow_transmitter.h>
#include <firmware_version.h>
#include <esp_now.h>
#include <Arduino.h>
#include <cstring>

ReceiverConnectionHandler& ReceiverConnectionHandler::instance() {
    static ReceiverConnectionHandler instance;
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

ReceiverConnectionHandler::ReceiverConnectionHandler()
    : last_rx_time_ms_(0),
      power_data_confirmed_(false),
      connected_at_ms_(0),
      last_retry_ms_(0) {
    memset(transmitter_mac_, 0, sizeof(transmitter_mac_));
        memset(deferred_peer_mac_, 0, sizeof(deferred_peer_mac_));
}

void ReceiverConnectionHandler::init() {
    last_rx_time_ms_ = millis();

    // Make common connection manager the single owner of connection timeout.
    // Receiver treats any ESP-NOW traffic as keep-alive and uses 90s threshold.
    EspNowConnectionManager::instance().set_heartbeat_timeout_ms(90000);
    EspNowConnectionManager::instance().set_heartbeat_timeout_enabled(true);
    
    // Register state change callback
    EspNowConnectionManager::instance().register_state_callback(
        [](EspNowConnectionState old_state, EspNowConnectionState new_state) {
            LOG_INFO("RX_CONN", "State change: %s → %s",
                     espnow_state_to_string(old_state),
                     espnow_state_to_string(new_state));
            
            if (new_state == EspNowConnectionState::CONNECTED) {
                RxStateMachine::instance().on_connection_established();
                ReceiverConnectionHandler::instance().peer_registered_event_posted_ = false;
                ReceiverConnectionHandler::instance().peer_registered_deferred_ = false;
                // Lock channel when connected (receiver doesn't hop but should lock)
                uint8_t current_channel = ChannelManager::instance().get_channel();
                ChannelManager::instance().lock_channel(current_channel, "RX_CONN");
                LOG_INFO("RX_CONN", "✓ Connected - channel locked at %d", current_channel);
                
                // PHASE 0: Reset heartbeat monitor on connection
                RxHeartbeatManager::instance().on_connection_established();
                
                // Arm the REQUEST_DATA retry timer
                ReceiverConnectionHandler::instance().connected_at_ms_ = millis();
                ReceiverConnectionHandler::instance().last_retry_ms_    = millis();
                ReceiverConnectionHandler::instance().power_data_confirmed_ = false;

                // Send initialization requests now that connection is fully established
                ReceiverConnectionHandler::instance().send_initialization_requests(
                    EspNowConnectionManager::instance().get_peer_mac());
                
            } else if (old_state == EspNowConnectionState::CONNECTED && 
                       new_state == EspNowConnectionState::IDLE) {
                RxStateMachine::instance().on_connection_lost();
                ReceiverConnectionHandler::instance().on_connection_lost();
                ReceiverConnectionHandler::instance().peer_registered_event_posted_ = false;
                ReceiverConnectionHandler::instance().peer_registered_deferred_ = false;
                // Clean up peer when connection lost
                const uint8_t* peer_mac = ReceiverConnectionHandler::instance().get_transmitter_mac();
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
            } else if (new_state == EspNowConnectionState::CONNECTING) {
                ReceiverConnectionHandler::instance().peer_registered_event_posted_ = false;
                ReceiverConnectionHandler::instance().flush_deferred_peer_registered();
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

    // If we latched a registration event while not CONNECTING, attempt to flush now.
    flush_deferred_peer_registered();
}

void ReceiverConnectionHandler::on_peer_registered(const uint8_t* transmitter_mac) {
    if (transmitter_mac) {
        memcpy(transmitter_mac_, transmitter_mac, sizeof(transmitter_mac_));
    }
    last_rx_time_ms_ = millis();

    // When peer is registered, we're moving towards connected state
    // Notify heartbeat manager to reset its timeout
    RxHeartbeatManager::instance().on_connection_established();

    // Post PEER_REGISTERED only once while CONNECTING.
    // If we are not CONNECTING, latch a real deferred event with TTL.
    EspNowConnectionManager& conn_mgr = EspNowConnectionManager::instance();
    const auto state = conn_mgr.get_state();

    if (state == EspNowConnectionState::CONNECTING) {
        if (peer_registered_event_posted_) {
            LOG_DEBUG("RX_CONN", "Duplicate on_peer_registered() while CONNECTING - ignoring");
            return;
        }
        post_connection_event(EspNowEvent::PEER_REGISTERED, transmitter_mac_);
        peer_registered_event_posted_ = true;
        peer_registered_deferred_ = false;
        deferred_peer_registered_ms_ = 0;
        memset(deferred_peer_mac_, 0, sizeof(deferred_peer_mac_));
    } else if (state == EspNowConnectionState::CONNECTED) {
        // Expected on reboot/discovery noise while already connected; do not warn.
        LOG_DEBUG("RX_CONN", "on_peer_registered() received while CONNECTED - ignoring duplicate");
    } else {
        peer_registered_deferred_ = has_valid_mac(transmitter_mac_);
        if (peer_registered_deferred_) {
            memcpy(deferred_peer_mac_, transmitter_mac_, sizeof(deferred_peer_mac_));
            deferred_peer_registered_ms_ = millis();
            LOG_INFO("RX_CONN", "on_peer_registered() in state %d - deferred until CONNECTING",
                     static_cast<int>(state));
        } else {
            LOG_WARN("RX_CONN", "on_peer_registered() in state %d but MAC invalid - dropping",
                     static_cast<int>(state));
        }
    }
}

void ReceiverConnectionHandler::on_data_received(const uint8_t* transmitter_mac) {
    if (transmitter_mac) {
        memcpy(transmitter_mac_, transmitter_mac, sizeof(transmitter_mac_));
    }
    last_rx_time_ms_ = millis();
    
    // PHASE 0: Notify connection manager of heartbeat
    EspNowConnectionManager::instance().on_heartbeat_received();

    // Post DATA_RECEIVED event (common manager)
    post_connection_event(EspNowEvent::DATA_RECEIVED, transmitter_mac_);
}

void ReceiverConnectionHandler::on_link_activity(const uint8_t* transmitter_mac) {
    if (transmitter_mac) {
        memcpy(transmitter_mac_, transmitter_mac, sizeof(transmitter_mac_));
    }
    last_rx_time_ms_ = millis();

    if (connected_at_ms_ == 0 && has_valid_mac(transmitter_mac_)) {
        connected_at_ms_ = millis();
        last_retry_ms_ = millis();
    }

    flush_deferred_peer_registered();
}

void ReceiverConnectionHandler::flush_deferred_peer_registered() {
    if (!peer_registered_deferred_) {
        return;
    }

    const uint32_t now = millis();
    if (deferred_peer_registered_ms_ > 0 && (now - deferred_peer_registered_ms_) > DEFERRED_PEER_TTL_MS) {
        LOG_WARN("RX_CONN", "Dropping stale deferred PEER_REGISTERED (%lu ms old)",
                 now - deferred_peer_registered_ms_);
        peer_registered_deferred_ = false;
        deferred_peer_registered_ms_ = 0;
        memset(deferred_peer_mac_, 0, sizeof(deferred_peer_mac_));
        return;
    }

    if (EspNowConnectionManager::instance().get_state() != EspNowConnectionState::CONNECTING) {
        return;
    }

    if (peer_registered_event_posted_) {
        return;
    }

    post_connection_event(EspNowEvent::PEER_REGISTERED, deferred_peer_mac_);
    peer_registered_event_posted_ = true;
    peer_registered_deferred_ = false;
    deferred_peer_registered_ms_ = 0;
    memset(deferred_peer_mac_, 0, sizeof(deferred_peer_mac_));
    LOG_INFO("RX_CONN", "Flushed deferred PEER_REGISTERED in CONNECTING state");
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

void ReceiverConnectionHandler::on_power_data_received() {
    if (!power_data_confirmed_) {
        power_data_confirmed_ = true;
        LOG_INFO("CONN_HANDLER", "[RETRY] Power-profile data confirmed — stopping REQUEST_DATA retries");
    }
}

void ReceiverConnectionHandler::tick() {
    flush_deferred_peer_registered();

    const auto rx_state = RxStateMachine::instance().connection_state();
    const auto rx_stats = RxStateMachine::instance().stats();
    const uint32_t now = millis();
    const bool recent_power_data =
        (rx_stats.last_message_ms > 0) && ((now - rx_stats.last_message_ms) <= POWER_DATA_FRESHNESS_MS);

    // Do not rely on ACTIVE state alone here: ACTIVE may lag stale transition by up to the
    // configured stale timeout window. Freshness-based gating allows faster REQUEST_DATA retries.
    const bool data_stream_active = (rx_state == EspNowDeviceState::ACTIVE) && recent_power_data;
    power_data_confirmed_ = data_stream_active;

    if (data_stream_active) {
        return;
    }

    if (connected_at_ms_ == 0 || !has_valid_mac(transmitter_mac_)) {
        return;
    }

    const bool recent_link_activity = EspNowConnectionManager::instance().ms_since_last_heartbeat() < 20000;
    if (!recent_link_activity) {
        return;
    }

    // Wait RETRY_REQUEST_TIMEOUT_MS after connecting before first retry
    if ((now - connected_at_ms_) < RETRY_REQUEST_TIMEOUT_MS) return;

    // Throttle retries to RETRY_INTERVAL_MS
    if ((now - last_retry_ms_) < RETRY_INTERVAL_MS) return;

    last_retry_ms_ = now;
    LOG_WARN("CONN_HANDLER", "[RETRY] No power-profile data yet — re-sending REQUEST_DATA (connected %lu ms ago)",
             now - connected_at_ms_);

    request_data_t req = { msg_request_data, subtype_power_profile };
    esp_err_t result = esp_now_send(transmitter_mac_, (const uint8_t*)&req, sizeof(req));
    if (result != ESP_OK) {
        LOG_WARN("CONN_HANDLER", "[RETRY] esp_now_send failed: %s", esp_err_to_name(result));
    }
}

void ReceiverConnectionHandler::on_transmitter_reboot_detected() {
    // TX reboot usually resets TX state to CONNECTED and stops stream until REQUEST_DATA is
    // received again. Re-arm retry engine immediately so recovery happens in seconds, not after
    // stale timeout.
    power_data_confirmed_ = false;
    connected_at_ms_ = millis();
    last_retry_ms_ = 0;  // allow near-immediate retry after RETRY_REQUEST_TIMEOUT_MS

    LOG_WARN("CONN_HANDLER", "[RETRY] TX reboot detected - re-arming REQUEST_DATA retry window");
}

void ReceiverConnectionHandler::on_connection_lost() {
    // Reset first_data_received flag to allow re-initialization on reconnect
    // This ensures that if connection is lost and then re-established,
    // we'll send initialization requests again on the next connection
    if (first_data_received_) {
        LOG_INFO("CONN_HANDLER", "[CONN_LOST] Clearing 'first data received' flag for reconnection");
        first_data_received_ = false;
    }
    
    // Reset REQUEST_DATA retry state
    power_data_confirmed_ = false;
    connected_at_ms_      = 0;
    last_retry_ms_        = 0;
    peer_registered_event_posted_ = false;
    peer_registered_deferred_ = false;
    deferred_peer_registered_ms_ = 0;
    memset(deferred_peer_mac_, 0, sizeof(deferred_peer_mac_));

    // Log connection loss event
    LOG_WARN("CONN_HANDLER", "[CONN_LOST] Connection lost - ready for reconnection");
}

void ReceiverConnectionHandler::on_config_update_sent() {
    // Notify state machine that a config update was sent
    // This extends the stale detection grace window to prevent false timeouts
    // during rare config synchronization events (ethernet/MQTT changes)
    RxStateMachine::instance().on_config_update_sent();
    LOG_DEBUG("CONN_HANDLER", "Config update sent - stale detection grace window started");
}

