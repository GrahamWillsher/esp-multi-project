#include "heartbeat_manager.h"
#include "tx_state_machine.h"
#include "tx_send_guard.h"
#include <espnow_transmitter.h>
#include <esp_now.h>
#include <esp32common/config/timing_config.h>
#include <esp32common/espnow/connection_manager.h>
#include <esp32common/espnow/connection_event.h>
#include <esp32common/espnow/packet_utils.h>
#include <runtime_common_utils/device_temperature.h>
#include "../config/logging_config.h"
#include "../network/time_manager.h"
#include "../../lib/ethernet_utilities/ethernet_utilities.h"

void HeartbeatManager::init() {
    if (m_initialized) {
        LOG_WARN("HEARTBEAT", "Already initialized");
        return;
    }
    
    m_heartbeat_seq = 0;
    m_last_ack_seq = 0;
    m_last_send_time = 0;
    m_temperature_seq = 0;
    m_initialized = true;

    DeviceTemperature::init();
    DeviceTemperature::sample_now();
    
    LOG_INFO("HEARTBEAT", "Heartbeat manager initialized (interval: %u ms)", TimingConfig::HEARTBEAT_INTERVAL_MS);
}

void HeartbeatManager::tick() {
    if (!m_initialized) return;

    // Keepalive is gated only by ESP-NOW link state.
    // Ethernet availability must not suppress heartbeat traffic.
    if (EspNowConnectionManager::instance().get_state() != EspNowConnectionState::CONNECTED) {
        return;  // No receiver connection
    }
    
    uint32_t now = millis();
    
    // Check if it's time to send
    if (now - m_last_send_time >= TimingConfig::HEARTBEAT_INTERVAL_MS) {
        send_heartbeat();
        m_last_send_time = now;
    }
}

bool HeartbeatManager::has_stable_heartbeat() const {
    if (!m_initialized) {
        return false;
    }

    const auto state = TxStateMachine::instance().state();
    if (state != EspNowDeviceState::CONNECTED && state != EspNowDeviceState::ACTIVE) {
        return false;
    }

    return m_last_ack_seq > 0 && !TxStateMachine::instance().heartbeat_timed_out(
        TimingConfig::HEARTBEAT_TIMEOUT_MS);
}

bool HeartbeatManager::can_send_opportunistic_telemetry() const {
    if (!has_stable_heartbeat()) {
        return false;
    }

    return get_unacked_count() <= 1;
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
    hb.uptime_ms      = millis();
    hb.unix_time      = TimeManager::instance().get_unix_time();
    hb.utc_offset_min = get_cached_utc_offset_min();
    hb.time_source    = static_cast<uint8_t>(TimeManager::instance().get_time_source());
    hb.state = static_cast<uint8_t>(TxStateMachine::instance().state());
    hb.rssi = 0;  // TODO: Get last RSSI if available
    hb.flags = is_geolocation_configured() ? HEARTBEAT_FLAG_GEOLOCATION_VALID : 0;
    
    // Calculate CRC32 over all fields except trailing checksum
    hb.checksum = EspnowPacketUtils::calculate_message_crc32_zeroed(&hb);
    
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
        if (can_send_opportunistic_telemetry()) {
            send_temperature_report(peer_mac);
        } else {
            LOG_DEBUG("TEMP", "Deferred TX temperature report until heartbeat path is stable");
        }
    } else {
        LOG_ERROR("HEARTBEAT", "Failed to send heartbeat seq=%u: %s", hb.seq, esp_err_to_name(result));
    }
}

void HeartbeatManager::send_temperature_report(const uint8_t* peer_mac) {
    if (!peer_mac) {
        return;
    }

    DeviceTemperature::sample_now();
    const DeviceTemperature::Reading reading = DeviceTemperature::get_latest();

    temperature_report_t report{};
    report.type = msg_temperature_report;
    report.seq = ++m_temperature_seq;
    report.temperature_centi_c = reading.centi_celsius;
    report.valid = reading.valid ? 1 : 0;
    report.uptime_ms = millis();

    const esp_err_t result = TxSendGuard::send_to_receiver_guarded(
        peer_mac,
        reinterpret_cast<const uint8_t*>(&report),
        sizeof(report),
        "temperature_report"
    );

    if (result == ESP_OK) {
        if (reading.valid) {
            LOG_DEBUG("TEMP", "Sent TX temperature seq=%u value=%.2fC", report.seq,
                      DeviceTemperature::to_celsius(reading.centi_celsius));
        } else {
            LOG_WARN("TEMP", "Sent TX temperature seq=%u with invalid reading", report.seq);
        }
    } else if (result == ESP_ERR_INVALID_STATE || result == ESP_ERR_TIMEOUT) {
        LOG_DEBUG("TEMP", "Skipped TX temperature seq=%u (best-effort deferred: %s)",
                  report.seq, esp_err_to_name(result));
    } else {
        LOG_DEBUG("TEMP", "Skipped TX temperature seq=%u (best-effort send failed: %s)",
                  report.seq, esp_err_to_name(result));
    }
}

void HeartbeatManager::on_heartbeat_ack(const heartbeat_ack_t* ack) {
    if (!ack) return;
    
    // Validate CRC32
    if (!EspnowPacketUtils::verify_message_crc32(ack)) {
        LOG_ERROR("HEARTBEAT", "ACK CRC32 validation failed");
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
    m_temperature_seq = 0;
}
