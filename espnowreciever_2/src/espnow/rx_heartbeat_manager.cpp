#include "rx_heartbeat_manager.h"
#include "rx_connection_handler.h"
#include "../config/logging_config.h"
#include <espnow_transmitter.h>
#include <connection_event.h>
#include "../webserver/utils/transmitter_manager.h"

void RxHeartbeatManager::init() {
    if (m_initialized) {
        LOG_WARN("HEARTBEAT", "Already initialized");
        return;
    }
    
    m_last_heartbeat_seq = 0;
    m_last_rx_time_ms = millis();  // Initialize to prevent false timeout on startup
    m_heartbeats_received = 0;
    m_acks_sent = 0;
    m_initialized = true;
    
    LOG_INFO("HEARTBEAT", "RX Heartbeat manager initialized (timeout: %u ms)", HEARTBEAT_TIMEOUT_MS);
}

void RxHeartbeatManager::tick() {
    if (!m_initialized) return;
    
    // Only check timeout when connected
    if (EspNowConnectionManager::instance().get_state() != EspNowConnectionState::CONNECTED) {
        // Reset timer when returning to non-connected state to avoid false timeout on next connection
        m_last_rx_time_ms = millis();
        return;
    }

    // Treat any ESP-NOW traffic as keep-alive to avoid false disconnects when
    // heartbeats are delayed but other messages (beacons/data) are still flowing.
    uint32_t last_activity = ReceiverConnectionHandler::instance().get_last_rx_time_ms();
    if (last_activity > m_last_rx_time_ms) {
        m_last_rx_time_ms = last_activity;
    }
    
    uint32_t now = millis();
    uint32_t time_since_last = now - m_last_rx_time_ms;
    
    // Check for heartbeat timeout
    // Only trigger if we've received at least one heartbeat (m_heartbeats_received > 0)
    // This prevents false timeout on initial connection before first heartbeat arrives
    if (m_heartbeats_received > 0 && time_since_last > HEARTBEAT_TIMEOUT_MS) {
        LOG_ERROR("HEARTBEAT", "Connection lost: No heartbeat for %u ms (timeout: %u ms, total received: %u)",
                  time_since_last, HEARTBEAT_TIMEOUT_MS, m_heartbeats_received);
        
        // Reset connection handler's first_data_received flag for reconnection
        ReceiverConnectionHandler::instance().on_connection_lost();
        
        EspNowConnectionManager::instance().post_event(EspNowEvent::CONNECTION_LOST);
    }
}

void RxHeartbeatManager::on_heartbeat(const heartbeat_t* hb, const uint8_t* mac) {
    if (!hb || !mac) {
        LOG_ERROR("HEARTBEAT", "on_heartbeat called with invalid pointers");
        return;
    }
    
    // Validate CRC
    if (!validate_crc16(hb, sizeof(*hb))) {
        LOG_ERROR("HEARTBEAT", "CRC validation failed for seq=%u", hb->seq);
        return;
    }
    
    // Detect sequence regression (TX reboot)
    if (hb->seq < m_last_heartbeat_seq) {
        LOG_WARN("HEARTBEAT", "TX reboot detected (seq %u -> %u)", m_last_heartbeat_seq, hb->seq);
    }
    
    m_last_heartbeat_seq = hb->seq;
    m_last_rx_time_ms = millis();
    m_heartbeats_received++;
    
    LOG_INFO("HEARTBEAT", "Received heartbeat seq=%u (total: %u), TX uptime=%u ms, TX state=%u",
             hb->seq, m_heartbeats_received, hb->uptime_ms, hb->state);
    
    // Update TransmitterManager with time data from heartbeat
    TransmitterManager::updateTimeData(hb->uptime_ms, hb->unix_time, hb->time_source);
    
    // Send ACK
    send_ack(hb->seq, mac);
}

void RxHeartbeatManager::send_ack(uint32_t ack_seq, const uint8_t* mac) {
    heartbeat_ack_t ack;
    ack.type = msg_heartbeat_ack;
    ack.ack_seq = ack_seq;
    ack.uptime_ms = millis();
    ack.state = static_cast<uint8_t>(EspNowConnectionManager::instance().get_state());
    
    // Calculate CRC16 over all fields except checksum
    ack.checksum = calculate_crc16(&ack, sizeof(ack) - sizeof(ack.checksum));
    
    esp_err_t result = esp_now_send(mac, (uint8_t*)&ack, sizeof(ack));
    
    if (result == ESP_OK) {
        m_acks_sent++;
        LOG_DEBUG("HEARTBEAT", "Sent ACK seq=%u, uptime=%u ms", ack.ack_seq, ack.uptime_ms);
    } else {
        LOG_ERROR("HEARTBEAT", "Failed to send ACK seq=%u: %s", ack.ack_seq, esp_err_to_name(result));
    }
}

void RxHeartbeatManager::reset() {
    LOG_INFO("HEARTBEAT", "Resetting heartbeat state");
    m_last_heartbeat_seq = 0;
    m_last_rx_time_ms = millis();  // Reset to current time to prevent false timeout
    m_heartbeats_received = 0;
    m_acks_sent = 0;
}

void RxHeartbeatManager::on_connection_established() {
    // When connection is established, give heartbeat time to arrive
    // before starting timeout checks. Reset the timer.
    LOG_DEBUG("HEARTBEAT", "Connection established - resetting heartbeat timer");
    m_last_rx_time_ms = millis();
    // Note: We keep m_heartbeats_received so tick() knows to check timeout only after first heartbeat
}
