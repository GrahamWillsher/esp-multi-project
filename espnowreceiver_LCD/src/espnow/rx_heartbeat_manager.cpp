#include "espnow/rx_heartbeat_manager.h"

#include <esp_now.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp32common/espnow/packet_utils.h>
#include <esp32common/config/timing_config.h>
#include <esp32common/espnow/tx_scheduler.h>
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

    const esp_err_t result = EspnowTxScheduler::send(mac, &ack, sizeof(ack), "HEARTBEAT_ACK");
    if (result == ESP_OK) {
        m_acks_sent++;
        return;
    }

    LOG_WARN("HEARTBEAT", "Failed to queue ACK seq=%lu: %s",
             static_cast<unsigned long>(ack_seq),
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
