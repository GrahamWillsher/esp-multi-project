#include "heartbeat_manager.h"
#include "tx_state_machine.h"
#include "tx_send_guard.h"
#include <espnow_transmitter.h>
#include <connection_manager.h>
#include <connection_event.h>
#include "../config/logging_config.h"
#include "../network/time_manager.h"
#include "../network/ethernet_manager.h"

void HeartbeatManager::init() {
    if (m_initialized) {
        LOG_WARN("HEARTBEAT", "Already initialized");
        return;
    }
    
    m_heartbeat_seq = 0;
    m_last_ack_seq = 0;
    m_last_send_time = 0;
    m_initialized = true;
    
    LOG_INFO("HEARTBEAT", "Heartbeat manager initialized (interval: %u ms)", HEARTBEAT_INTERVAL_MS);
}

void HeartbeatManager::tick() {
    if (!m_initialized) return;
    
    // ✅ NEW: Check Ethernet first - must have cable + IP
    if (!EthernetManager::instance().is_fully_ready()) {
        return;  // Cable not present or IP not assigned
    }
    
    // ✅ EXISTING: Check ESP-NOW connection
    if (EspNowConnectionManager::instance().get_state() != EspNowConnectionState::CONNECTED) {
        return;  // No receiver connection
    }
    
    uint32_t now = millis();
    
    // Check if it's time to send
    if (now - m_last_send_time >= HEARTBEAT_INTERVAL_MS) {
        send_heartbeat();
        m_last_send_time = now;
    }
}

void HeartbeatManager::send_heartbeat() {
    // Get peer MAC from connection manager
    const uint8_t* peer_mac = EspNowConnectionManager::instance().get_peer_mac();
    
    if (!peer_mac) {
        LOG_WARN("HEARTBEAT", "Cannot send heartbeat - no peer MAC available");
        return;
    }
    
    // Check if it's the broadcast address (shouldn't happen if we're CONNECTED)
    bool is_broadcast = true;
    for (int i = 0; i < 6; i++) {
        if (peer_mac[i] != 0xFF) {
            is_broadcast = false;
            break;
        }
    }
    
    if (is_broadcast) {
        LOG_WARN("HEARTBEAT", "Cannot send heartbeat - peer MAC is broadcast address");
        return;
    }
    
    heartbeat_t hb;
    hb.type = msg_heartbeat;
    hb.seq = ++m_heartbeat_seq;
    hb.uptime_ms = millis();
    hb.unix_time = TimeManager::instance().get_unix_time();
    hb.time_source = static_cast<uint8_t>(TimeManager::instance().get_time_source());
    hb.state = static_cast<uint8_t>(TxStateMachine::instance().state());
    hb.rssi = 0;  // TODO: Get last RSSI if available
    hb.flags = 0;
    
    // Calculate CRC16 over all fields except checksum
    hb.checksum = calculate_crc16(&hb, sizeof(hb) - sizeof(hb.checksum));
    
    esp_err_t result = TxSendGuard::send_to_receiver_guarded(
        peer_mac,
        (const uint8_t*)&hb,
        sizeof(hb),
        "heartbeat"
    );
    
    if (result == ESP_OK) {
        LOG_DEBUG("HEARTBEAT", "Sent heartbeat seq=%u, uptime=%llu ms to %02X:%02X:%02X:%02X:%02X:%02X", 
                  hb.seq, (unsigned long long)hb.uptime_ms,
                  peer_mac[0], peer_mac[1], peer_mac[2], peer_mac[3], peer_mac[4], peer_mac[5]);
    } else {
        LOG_ERROR("HEARTBEAT", "Failed to send heartbeat seq=%u: %s", hb.seq, esp_err_to_name(result));
    }
}

void HeartbeatManager::on_heartbeat_ack(const heartbeat_ack_t* ack) {
    if (!ack) return;
    
    // Validate CRC
    if (!validate_crc16(ack, sizeof(*ack))) {
        LOG_ERROR("HEARTBEAT", "ACK CRC validation failed");
        return;
    }
    
    // Update last ack sequence (only if newer)
    if (ack->ack_seq > m_last_ack_seq) {
        uint32_t prev_ack = m_last_ack_seq;
        m_last_ack_seq = ack->ack_seq;
        TxStateMachine::instance().on_heartbeat_ack();
        EspNowConnectionManager::instance().on_heartbeat_received();
        
        LOG_DEBUG("HEARTBEAT", "Received ACK seq=%u (prev=%u), RX uptime=%u ms, RX state=%u",
                  ack->ack_seq, prev_ack, ack->uptime_ms, ack->state);
    } else {
        LOG_WARN("HEARTBEAT", "Received old/duplicate ACK seq=%u (current=%u)", 
                 ack->ack_seq, m_last_ack_seq);
    }
}

void HeartbeatManager::reset() {
    LOG_INFO("HEARTBEAT", "Resetting heartbeat state");
    m_heartbeat_seq = 0;
    m_last_ack_seq = 0;
    m_last_send_time = 0;
}
