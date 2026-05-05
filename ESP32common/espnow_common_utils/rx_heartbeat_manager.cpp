#include "rx_heartbeat_manager.h"

#include <esp_now.h>
#include <esp32common/config/timing_config.h>
#include <esp32common/espnow/connection_manager.h>
#include <esp32common/espnow/packet_utils.h>
#include <esp32common/espnow/tx_scheduler.h>
#include <logging_config.h>
#include <runtime_common_utils/device_temperature.h>

namespace {

void call_on_transmitter_reboot_detected(const RxHeartbeatManagerHooks& hooks) {
    if (hooks.on_transmitter_reboot_detected != nullptr) {
        hooks.on_transmitter_reboot_detected(hooks.context);
    }
}

void call_on_heartbeat_payload(const RxHeartbeatManagerHooks& hooks,
                               const heartbeat_t* hb,
                               const uint8_t* mac) {
    if (hooks.on_heartbeat_payload != nullptr) {
        hooks.on_heartbeat_payload(hooks.context, hb, mac);
    }
}

void call_on_heartbeat_ack_enqueue_failure(const RxHeartbeatManagerHooks& hooks,
                                           esp_err_t err) {
    if (hooks.on_heartbeat_ack_enqueue_failure != nullptr) {
        hooks.on_heartbeat_ack_enqueue_failure(hooks.context, err);
    }
}

uint8_t get_connection_state(const RxHeartbeatManagerHooks& hooks) {
    if (hooks.get_connection_state != nullptr) {
        return hooks.get_connection_state(hooks.context);
    }

    return 0;
}

uint32_t get_link_activity_time_ms(const RxHeartbeatManagerHooks& hooks) {
    if (hooks.get_link_activity_time_ms != nullptr) {
        return hooks.get_link_activity_time_ms(hooks.context);
    }

    return 0;
}

}  // namespace

RxHeartbeatManager& RxHeartbeatManager::instance() {
    static RxHeartbeatManager inst;
    return inst;
}

void RxHeartbeatManager::configure_hooks(const RxHeartbeatManagerHooks& hooks) {
    hooks_ = hooks;
}

void RxHeartbeatManager::configure(const RxHeartbeatManagerConfig& config) {
    config_ = config;
    if (config_.temperature_tick_interval_ms == 0U) {
        config_.temperature_tick_interval_ms = TimingConfig::HEARTBEAT.interval_ms;
    }
}

void RxHeartbeatManager::init() {
    if (m_initialized) {
        return;
    }

    m_last_heartbeat_seq = 0;
    m_last_rx_time_ms = millis();
    m_heartbeats_received = 0;
    m_acks_sent = 0;
    m_ack_enqueue_stats = {};
    m_initialized = true;

    if (config_.temperature_tick_interval_ms == 0U) {
        config_.temperature_tick_interval_ms = TimingConfig::HEARTBEAT.interval_ms;
    }

    DeviceTemperature::init();
    DeviceTemperature::sample_now();

    LOG_INFO("HEARTBEAT", "RX heartbeat manager initialized");
}

void RxHeartbeatManager::tick() {
    if (!m_initialized) {
        return;
    }

    DeviceTemperature::tick(config_.temperature_tick_interval_ms);

    if (config_.use_link_activity_as_keepalive) {
        const uint32_t last_activity_ms = get_link_activity_time_ms(hooks_);
        if (last_activity_ms > m_last_rx_time_ms) {
            m_last_rx_time_ms = last_activity_ms;
        }
    }
}

void RxHeartbeatManager::on_heartbeat(const heartbeat_t* hb, const uint8_t* mac) {
    if (hb == nullptr || mac == nullptr) {
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
        call_on_transmitter_reboot_detected(hooks_);
    }

    m_last_heartbeat_seq = hb->seq;
    m_last_rx_time_ms = millis();
    m_heartbeats_received++;

    EspNowConnectionManager::instance().on_heartbeat_received();
    call_on_heartbeat_payload(hooks_, hb, mac);

    send_ack(hb->seq, mac);
}

void RxHeartbeatManager::send_ack(uint32_t ack_seq, const uint8_t* mac) {
    heartbeat_ack_t ack{};
    ack.type = msg_heartbeat_ack;
    ack.ack_seq = ack_seq;
    ack.uptime_ms = millis();
    ack.state = get_connection_state(hooks_);
    ack.checksum = EspnowPacketUtils::calculate_message_crc32_zeroed(&ack);

    m_ack_enqueue_stats.enqueue_attempts++;
    const esp_err_t queued_result = EspnowTxScheduler::send(mac, &ack, sizeof(ack), "HEARTBEAT_ACK");
    if (queued_result == ESP_OK) {
        m_acks_sent++;
        m_ack_enqueue_stats.enqueue_success++;
        LOG_DEBUG("HEARTBEAT", "Queued ACK seq=%lu",
                  static_cast<unsigned long>(ack_seq));
        return;
    }

    if (queued_result == ESP_ERR_ESPNOW_NO_MEM) {
        m_ack_enqueue_stats.enqueue_no_mem_failures++;
    } else {
        m_ack_enqueue_stats.enqueue_other_failures++;
    }
    call_on_heartbeat_ack_enqueue_failure(hooks_, queued_result);

    LOG_WARN("HEARTBEAT", "Failed to queue ACK seq=%lu: %s",
             static_cast<unsigned long>(ack_seq),
             esp_err_to_name(queued_result));
}

void RxHeartbeatManager::reset() {
    m_last_heartbeat_seq = 0;
    m_last_rx_time_ms = millis();
    m_heartbeats_received = 0;
    m_acks_sent = 0;
    m_ack_enqueue_stats = {};
}

void RxHeartbeatManager::on_connection_established() {
    m_last_rx_time_ms = millis();
}

bool RxHeartbeatManager::read_ack_enqueue_stats(RxHeartbeatAckEnqueueStats& out_stats) const {
    out_stats = m_ack_enqueue_stats;
    return true;
}

void RxHeartbeatManager::reset_ack_enqueue_stats() {
    m_ack_enqueue_stats = {};
}
