#include "espnow/rx_heartbeat_manager.h"

#include <esp_now.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp32common/espnow/packet_utils.h>
#include <esp32common/config/timing_config.h>
#include <runtime_common_utils/device_temperature.h>

#include "espnow/rx_connection_handler.h"
#include "espnow/rx_state_machine.h"
#include "logging_config.h"
#include "../../lib/webserver_lcd/utils/transmitter_manager.h"

// Defined in webserver runtime module.
void notify_sse_data_updated();

RxHeartbeatManager& RxHeartbeatManager::instance() {
    static RxHeartbeatManager inst;
    return inst;
}

namespace {
constexpr uint8_t kAckNoMemRetryAttempts = 6;
constexpr uint32_t kAckNoMemRetryDelayMs = 8;
constexpr uint32_t kAckWarnRateLimitMs = 5000;
uint32_t g_ack_no_mem_drops_since_log = 0;
uint32_t g_ack_last_warn_ms = 0;
}

void RxHeartbeatManager::init() {
    if (m_initialized) {
        return;
    }

    m_last_heartbeat_seq = 0;
    m_last_rx_time_ms = millis();
    m_heartbeats_received = 0;
    m_acks_sent = 0;
    m_initialized = true;
    g_ack_no_mem_drops_since_log = 0;
    g_ack_last_warn_ms = millis();

    DeviceTemperature::init();
    DeviceTemperature::sample_now();

    LOG_INFO("HEARTBEAT", "RX heartbeat manager initialized");
}

void RxHeartbeatManager::tick() {
    if (!m_initialized) {
        return;
    }

    DeviceTemperature::tick(TimingConfig::HEARTBEAT.interval_ms);
}

void RxHeartbeatManager::on_heartbeat(const heartbeat_t* hb, const uint8_t* mac) {
    if (!hb || !mac) {
        return;
    }

    if (!EspnowPacketUtils::verify_message_crc32(hb)) {
        LOG_WARN("HEARTBEAT", "CRC32 validation failed for seq=%lu", static_cast<unsigned long>(hb->seq));
        return;
    }

    if (hb->seq < m_last_heartbeat_seq) {
        LOG_WARN("HEARTBEAT", "TX reboot detected (seq %lu -> %lu)",
                 static_cast<unsigned long>(m_last_heartbeat_seq),
                 static_cast<unsigned long>(hb->seq));
        ReceiverConnectionHandler::instance().on_transmitter_reboot_detected();
    }

    m_last_heartbeat_seq = hb->seq;
    m_last_rx_time_ms = millis();
    m_heartbeats_received++;

    TransmitterManager::updateTimeData(hb->uptime_ms, hb->unix_time, hb->utc_offset_min, hb->time_source);
    TransmitterManager::updateHeartbeatFlags(hb->flags);
    notify_sse_data_updated();

    send_ack(hb->seq, mac);
}

void RxHeartbeatManager::send_ack(uint32_t ack_seq, const uint8_t* mac) {
    heartbeat_ack_t ack{};
    ack.type = msg_heartbeat_ack;
    ack.ack_seq = ack_seq;
    ack.uptime_ms = millis();
    ack.state = static_cast<uint8_t>(RxStateMachine::instance().connection_state());
    ack.checksum = EspnowPacketUtils::calculate_message_crc32_zeroed(&ack);

    esp_err_t result = ESP_FAIL;
    for (uint8_t attempt = 0; attempt < kAckNoMemRetryAttempts; ++attempt) {
        result = esp_now_send(mac, reinterpret_cast<const uint8_t*>(&ack), sizeof(ack));
        if (result == ESP_OK) {
            m_acks_sent++;
            return;
        }

        // ESP-NOW tx queue can be transiently full under burst traffic.
        // Retry a few times with a short backoff before logging a warning.
        if (result == ESP_ERR_ESPNOW_NO_MEM && (attempt + 1U) < kAckNoMemRetryAttempts) {
            // Exponential-ish backoff to let the ESPNOW tx queue drain under WiFi/OTA load.
            const uint32_t backoff_ms = kAckNoMemRetryDelayMs * (attempt + 1U);
            vTaskDelay(pdMS_TO_TICKS(backoff_ms));
            continue;
        }

        break;
    }

    if (result == ESP_ERR_ESPNOW_NO_MEM) {
        ++g_ack_no_mem_drops_since_log;
        const uint32_t now = millis();
        if ((now - g_ack_last_warn_ms) >= kAckWarnRateLimitMs) {
            LOG_WARN("HEARTBEAT", "ACK tx queue saturated (NO_MEM): dropped=%lu in last %lums, latest seq=%lu",
                     static_cast<unsigned long>(g_ack_no_mem_drops_since_log),
                     static_cast<unsigned long>(now - g_ack_last_warn_ms),
                     static_cast<unsigned long>(ack_seq));
            g_ack_last_warn_ms = now;
            g_ack_no_mem_drops_since_log = 0;
        }
        return;
    }

    LOG_WARN("HEARTBEAT", "Failed to send ACK seq=%lu after %u attempt(s): %s",
             static_cast<unsigned long>(ack_seq),
             static_cast<unsigned>(kAckNoMemRetryAttempts),
             esp_err_to_name(result));
}

void RxHeartbeatManager::reset() {
    m_last_heartbeat_seq = 0;
    m_last_rx_time_ms = millis();
    m_heartbeats_received = 0;
    m_acks_sent = 0;
}

void RxHeartbeatManager::on_connection_established() {
    m_last_rx_time_ms = millis();
}
