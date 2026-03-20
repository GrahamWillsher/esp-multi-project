#include "connection_state_manager.h"
#include "../common.h"
#include "../espnow/battery_data_store.h"
#include "../espnow/rx_state_machine.h"
#include <cstring>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

SemaphoreHandle_t ConnectionStateManager::state_mutex_ = nullptr;
bool ConnectionStateManager::tracked_connection_state_ = false;
uint32_t ConnectionStateManager::connection_start_time_ms_ = 0;
uint32_t ConnectionStateManager::connection_count_ = 0;
uint32_t ConnectionStateManager::last_data_received_ms_ = 0;
ConnectionStateManager::ConnectionChangedCallback ConnectionStateManager::callbacks_[ConnectionStateManager::MAX_CALLBACKS] = {};
size_t ConnectionStateManager::callback_count_ = 0;
static bool legacy_data_received_compat_ = false;

static bool is_connected_state(EspNowDeviceState s) {
    return s == EspNowDeviceState::CONNECTED ||
           s == EspNowDeviceState::ACTIVE ||
           s == EspNowDeviceState::STALE;
}

void ConnectionStateManager::notify_connection_changed(bool connected) {
    ConnectionChangedCallback callbacks_copy[MAX_CALLBACKS] = {};
    size_t count = 0;

    if (state_mutex_ && xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        count = callback_count_;
        for (size_t i = 0; i < count; ++i) {
            callbacks_copy[i] = callbacks_[i];
        }
        xSemaphoreGive(state_mutex_);
    }

    for (size_t i = 0; i < count; ++i) {
        if (callbacks_copy[i]) {
            callbacks_copy[i](connected);
        }
    }
}

void ConnectionStateManager::init() {
    if (!state_mutex_) {
        state_mutex_ = xSemaphoreCreateMutex();
    }
    if (!state_mutex_) {
        LOG_ERROR("CONN_STATE", "Failed to create connection state mutex");
        return;
    }

    tracked_connection_state_ = is_connected_state(RxStateMachine::instance().connection_state());
    if (tracked_connection_state_) {
        connection_start_time_ms_ = millis();
        connection_count_ = 1;
    }
    last_data_received_ms_ = millis();
}

bool ConnectionStateManager::is_transmitter_connected() {
    return is_connected_state(RxStateMachine::instance().connection_state());
}

bool ConnectionStateManager::set_transmitter_connected(bool connected) {
    if (!state_mutex_) return false;
    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return false;

    const bool changed = (tracked_connection_state_ != connected);
    if (changed) {
        tracked_connection_state_ = connected;
        if (connected) {
            connection_start_time_ms_ = millis();
            connection_count_++;
        }
    }

    xSemaphoreGive(state_mutex_);
    if (changed) notify_connection_changed(connected);
    return changed;
}

bool ConnectionStateManager::register_connection_changed_callback(ConnectionChangedCallback callback) {
    if (!callback || !state_mutex_) return false;
    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    if (callback_count_ >= MAX_CALLBACKS) {
        xSemaphoreGive(state_mutex_);
        return false;
    }
    callbacks_[callback_count_++] = callback;
    xSemaphoreGive(state_mutex_);
    return true;
}

bool ConnectionStateManager::poll_connection_change() {
    const bool current = is_transmitter_connected();
    if (!state_mutex_) return false;
    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    const bool changed = (tracked_connection_state_ != current);
    if (changed) {
        tracked_connection_state_ = current;
        if (current) {
            connection_start_time_ms_ = millis();
            connection_count_++;
        }
    }
    xSemaphoreGive(state_mutex_);
    if (changed) notify_connection_changed(current);
    return changed;
}

int ConnectionStateManager::get_wifi_channel() { return ESPNow::wifi_channel; }

bool ConnectionStateManager::set_wifi_channel(int channel) {
    const bool changed = (ESPNow::wifi_channel != channel);
    ESPNow::wifi_channel = channel;
    return changed;
}

bool ConnectionStateManager::get_transmitter_mac(uint8_t* mac_out) {
    if (!mac_out) return false;
    bool is_set = false;
    for (int i = 0; i < 6; ++i) {
        if (ESPNow::transmitter_mac[i] != 0) {
            is_set = true;
            break;
        }
    }
    if (is_set) memcpy(mac_out, ESPNow::transmitter_mac, 6);
    return is_set;
}

bool ConnectionStateManager::set_transmitter_mac(const uint8_t* mac) {
    if (!mac) return false;
    const bool changed = (memcmp(ESPNow::transmitter_mac, mac, 6) != 0);
    if (changed) memcpy(ESPNow::transmitter_mac, mac, 6);
    return changed;
}

bool ConnectionStateManager::lock(uint32_t timeout_ms) {
    if (!state_mutex_) return false;
    TickType_t ticks = (timeout_ms == portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(state_mutex_, ticks) == pdTRUE;
}

void ConnectionStateManager::unlock() {
    if (state_mutex_) xSemaphoreGive(state_mutex_);
}

uint8_t ConnectionStateManager::get_soc() {
    BatteryData::TelemetrySnapshot snapshot;
    if (BatteryData::read_snapshot(snapshot) && snapshot.battery_status.received) {
        int soc = static_cast<int>(snapshot.soc_percent + 0.5f);
        if (soc < 0) soc = 0;
        if (soc > 100) soc = 100;
        return static_cast<uint8_t>(soc);
    }
    return 0;
}

bool ConnectionStateManager::set_soc(uint8_t soc) {
    const bool changed = (get_soc() != soc);
    BatteryData::update_basic_telemetry(soc, get_power(), get_voltage_mv());
    last_data_received_ms_ = millis();
    legacy_data_received_compat_ = true;
    return changed;
}

int32_t ConnectionStateManager::get_power() {
    BatteryData::TelemetrySnapshot snapshot;
    if (BatteryData::read_snapshot(snapshot) && snapshot.battery_status.received) {
        return snapshot.power_W;
    }
    return 0;
}

bool ConnectionStateManager::set_power(int32_t power) {
    const bool changed = (get_power() != power);
    BatteryData::update_basic_telemetry(get_soc(), power, get_voltage_mv());
    last_data_received_ms_ = millis();
    legacy_data_received_compat_ = true;
    return changed;
}

uint32_t ConnectionStateManager::get_voltage_mv() {
    BatteryData::TelemetrySnapshot snapshot;
    if (BatteryData::read_snapshot(snapshot) && snapshot.battery_status.received) {
        return static_cast<uint32_t>(snapshot.voltage_V * 1000.0f);
    }
    return 0;
}

bool ConnectionStateManager::set_voltage_mv(uint32_t voltage_mv) {
    const bool changed = (get_voltage_mv() != voltage_mv);
    BatteryData::update_basic_telemetry(get_soc(), get_power(), voltage_mv);
    last_data_received_ms_ = millis();
    legacy_data_received_compat_ = true;
    return changed;
}

bool ConnectionStateManager::has_data_received() {
    BatteryData::TelemetrySnapshot snapshot;
    if (BatteryData::read_snapshot(snapshot)) {
        return snapshot.battery_status.received ||
               legacy_data_received_compat_ ||
               RxStateMachine::instance().message_state() == RxStateMachine::MessageState::VALID;
    }
    return legacy_data_received_compat_ ||
           RxStateMachine::instance().message_state() == RxStateMachine::MessageState::VALID;
}

bool ConnectionStateManager::set_data_received(bool received) {
    const bool changed = (legacy_data_received_compat_ != received);
    legacy_data_received_compat_ = received;
    if (received) last_data_received_ms_ = millis();
    return changed;
}

void ConnectionStateManager::update_received_data(uint8_t soc, int32_t power, uint32_t voltage_mv, bool* out_soc_changed, bool* out_power_changed) {
    const bool soc_changed = (get_soc() != soc);
    const bool power_changed = (get_power() != power);

    BatteryData::update_basic_telemetry(soc, power, voltage_mv);
    legacy_data_received_compat_ = true;
    last_data_received_ms_ = millis();

    if (out_soc_changed) *out_soc_changed = soc_changed;
    if (out_power_changed) *out_power_changed = power_changed;
}

uint32_t ConnectionStateManager::last_data_received_ms() { return last_data_received_ms_; }

bool ConnectionStateManager::is_data_stale(uint32_t timeout_ms) {
    const uint32_t now = millis();
    return (last_data_received_ms_ == 0) || ((now - last_data_received_ms_) > timeout_ms);
}

uint32_t ConnectionStateManager::get_connection_uptime_ms() {
    if (!tracked_connection_state_ || connection_start_time_ms_ == 0) return 0;
    return millis() - connection_start_time_ms_;
}

uint32_t ConnectionStateManager::get_connection_count() { return connection_count_; }

void ConnectionStateManager::get_all_state(bool& out_connected, int& out_channel, uint8_t* out_mac) {
    if (!out_mac) return;
    out_connected = is_transmitter_connected();
    out_channel = ESPNow::wifi_channel;
    memcpy(out_mac, ESPNow::transmitter_mac, 6);
}
