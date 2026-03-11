// Deprecated: replaced by RxStateMachine. Intentionally left empty.

#include "connection_state_manager.h"
#include "../common.h"
#include "../espnow/rx_state_machine.h"
#include <cstring>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#if 0

// Static member initialization
SemaphoreHandle_t ConnectionStateManager::state_mutex_ = nullptr;
bool ConnectionStateManager::tracked_connection_state_ = false;
uint32_t ConnectionStateManager::connection_start_time_ms_ = 0;
uint32_t ConnectionStateManager::connection_count_ = 0;
uint32_t ConnectionStateManager::last_data_received_ms_ = 0;
ConnectionStateManager::ConnectionChangedCallback ConnectionStateManager::callbacks_[ConnectionStateManager::MAX_CALLBACKS] = {};
size_t ConnectionStateManager::callback_count_ = 0;
static bool legacy_transmitter_connected_ = false;
static bool legacy_data_received_ = false;

void ConnectionStateManager::notify_connection_changed(bool connected) {
    ConnectionChangedCallback callbacks_copy[MAX_CALLBACKS] = {};
    size_t count = 0;

    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
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
    if (state_mutex_) {
        LOG_WARN("CONN_STATE", "ConnectionStateManager already initialized");
        return;
    }
    
    state_mutex_ = xSemaphoreCreateMutex();
    if (!state_mutex_) {
        LOG_ERROR("CONN_STATE", "Failed to create connection state mutex");
        return;
    }

    tracked_connection_state_ = legacy_transmitter_connected_;
    if (tracked_connection_state_) {
        connection_start_time_ms_ = millis();
        connection_count_ = 1;
    }
    last_data_received_ms_ = millis();
    
    LOG_INFO("CONN_STATE", "ConnectionStateManager initialized");
}

bool ConnectionStateManager::is_transmitter_connected() {
    if (!state_mutex_) {
        LOG_ERROR("CONN_STATE", "ConnectionStateManager not initialized");
        return false;
    }
    
    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_ERROR("CONN_STATE", "Failed to acquire state lock");
        return false;
    }
    
    bool result = legacy_transmitter_connected_;

    // Detect external legacy updates and keep statistics synchronized
    if (result != tracked_connection_state_) {
        tracked_connection_state_ = result;
        if (result) {
            connection_start_time_ms_ = millis();
            connection_count_++;
        }
        xSemaphoreGive(state_mutex_);
        notify_connection_changed(result);
        return result;
    }

    xSemaphoreGive(state_mutex_);
    return result;
}

bool ConnectionStateManager::set_transmitter_connected(bool connected) {
    if (!state_mutex_) {
        LOG_ERROR("CONN_STATE", "ConnectionStateManager not initialized");
        return false;
    }
    
    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_ERROR("CONN_STATE", "Failed to acquire state lock");
        return false;
    }
    
    bool changed = (legacy_transmitter_connected_ != connected);
    if (changed) {
        legacy_transmitter_connected_ = connected;
        tracked_connection_state_ = connected;
        if (connected) {
            connection_start_time_ms_ = millis();
            connection_count_++;
        }
        LOG_INFO("CONN_STATE", "Transmitter connection state changed to: %s", connected ? "CONNECTED" : "DISCONNECTED");
    }
    
    xSemaphoreGive(state_mutex_);

    if (changed) {
        notify_connection_changed(connected);
    }

    return changed;
}

bool ConnectionStateManager::register_connection_changed_callback(ConnectionChangedCallback callback) {
    if (!callback) {
        return false;
    }

    if (!state_mutex_) {
        LOG_ERROR("CONN_STATE", "ConnectionStateManager not initialized");
        return false;
    }

    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_ERROR("CONN_STATE", "Failed to acquire state lock");
        return false;
    }

    if (callback_count_ >= MAX_CALLBACKS) {
        xSemaphoreGive(state_mutex_);
        LOG_WARN("CONN_STATE", "Connection callback list is full");
        return false;
    }

    callbacks_[callback_count_++] = callback;
    xSemaphoreGive(state_mutex_);
    return true;
}

bool ConnectionStateManager::poll_connection_change() {
    if (!state_mutex_) {
        return false;
    }

    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(50)) != pdTRUE) {
        return false;
    }

    bool current = legacy_transmitter_connected_;
    bool changed = (current != tracked_connection_state_);
    if (changed) {
        tracked_connection_state_ = current;
        if (current) {
            connection_start_time_ms_ = millis();
            connection_count_++;
        }
    }

    xSemaphoreGive(state_mutex_);

    if (changed) {
        notify_connection_changed(current);
    }

    return changed;
}

int ConnectionStateManager::get_wifi_channel() {
    if (!state_mutex_) {
        LOG_ERROR("CONN_STATE", "ConnectionStateManager not initialized");
        return 0;
    }
    
    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_ERROR("CONN_STATE", "Failed to acquire state lock");
        return 0;
    }
    
    int result = ESPNow::wifi_channel;
    xSemaphoreGive(state_mutex_);
    return result;
}

bool ConnectionStateManager::set_wifi_channel(int channel) {
    if (!state_mutex_) {
        LOG_ERROR("CONN_STATE", "ConnectionStateManager not initialized");
        return false;
    }
    
    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_ERROR("CONN_STATE", "Failed to acquire state lock");
        return false;
    }
    
    bool changed = (ESPNow::wifi_channel != channel);
    if (changed) {
        ESPNow::wifi_channel = channel;
        LOG_DEBUG("CONN_STATE", "WiFi channel changed to: %d", channel);
    }
    
    xSemaphoreGive(state_mutex_);
    return changed;
}

bool ConnectionStateManager::get_transmitter_mac(uint8_t* mac_out) {
    if (!mac_out) {
        LOG_ERROR("CONN_STATE", "MAC output buffer is nullptr");
        return false;
    }
    
    if (!state_mutex_) {
        LOG_ERROR("CONN_STATE", "ConnectionStateManager not initialized");
        return false;
    }
    
    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_ERROR("CONN_STATE", "Failed to acquire state lock");
        return false;
    }
    
    // Check if MAC is set (not all zeros)
    bool is_set = false;
    for (int i = 0; i < 6; i++) {
        if (ESPNow::transmitter_mac[i] != 0) {
            is_set = true;
            break;
        }
    }
    
    if (is_set) {
        memcpy(mac_out, ESPNow::transmitter_mac, 6);
    }
    
    xSemaphoreGive(state_mutex_);
    return is_set;
}

bool ConnectionStateManager::set_transmitter_mac(const uint8_t* mac) {
    if (!mac) {
        LOG_ERROR("CONN_STATE", "MAC input is nullptr");
        return false;
    }
    
    if (!state_mutex_) {
        LOG_ERROR("CONN_STATE", "ConnectionStateManager not initialized");
        return false;
    }
    
    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_ERROR("CONN_STATE", "Failed to acquire state lock");
        return false;
    }
    
    bool changed = (memcmp(ESPNow::transmitter_mac, mac, 6) != 0);
    if (changed) {
        memcpy(ESPNow::transmitter_mac, mac, 6);
        LOG_INFO("CONN_STATE", "Transmitter MAC set to: %02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    
    xSemaphoreGive(state_mutex_);
    return changed;
}

bool ConnectionStateManager::lock(uint32_t timeout_ms) {
    if (!state_mutex_) {
        LOG_ERROR("CONN_STATE", "ConnectionStateManager not initialized");
        return false;
    }
    
    TickType_t ticks = (timeout_ms == portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(state_mutex_, ticks) == pdTRUE;
}

void ConnectionStateManager::unlock() {
    if (!state_mutex_) {
        LOG_ERROR("CONN_STATE", "ConnectionStateManager not initialized");
        return;
    }
    
    xSemaphoreGive(state_mutex_);
}

uint8_t ConnectionStateManager::get_soc() {
    if (!state_mutex_) return ESPNow::received_soc;
    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return ESPNow::received_soc;
    uint8_t value = ESPNow::received_soc;
    xSemaphoreGive(state_mutex_);
    return value;
}

bool ConnectionStateManager::set_soc(uint8_t soc) {
    if (!state_mutex_) return false;
    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool changed = (ESPNow::received_soc != soc);
    ESPNow::received_soc = soc;
    last_data_received_ms_ = millis();
    xSemaphoreGive(state_mutex_);
    return changed;
}

int32_t ConnectionStateManager::get_power() {
    if (!state_mutex_) return ESPNow::received_power;
    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return ESPNow::received_power;
    int32_t value = ESPNow::received_power;
    xSemaphoreGive(state_mutex_);
    return value;
}

bool ConnectionStateManager::set_power(int32_t power) {
    if (!state_mutex_) return false;
    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool changed = (ESPNow::received_power != power);
    ESPNow::received_power = power;
    last_data_received_ms_ = millis();
    xSemaphoreGive(state_mutex_);
    return changed;
}

uint32_t ConnectionStateManager::get_voltage_mv() {
    if (!state_mutex_) return ESPNow::received_voltage_mv;
    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return ESPNow::received_voltage_mv;
    uint32_t value = ESPNow::received_voltage_mv;
    xSemaphoreGive(state_mutex_);
    return value;
}

bool ConnectionStateManager::set_voltage_mv(uint32_t voltage_mv) {
    if (!state_mutex_) return false;
    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool changed = (ESPNow::received_voltage_mv != voltage_mv);
    ESPNow::received_voltage_mv = voltage_mv;
    last_data_received_ms_ = millis();
    xSemaphoreGive(state_mutex_);
    return changed;
}

bool ConnectionStateManager::has_data_received() {
    if (!state_mutex_) return legacy_data_received_;
    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return legacy_data_received_;
    bool value = legacy_data_received_;
    xSemaphoreGive(state_mutex_);
    return value;
}

bool ConnectionStateManager::set_data_received(bool received) {
    if (!state_mutex_) return false;
    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    bool changed = (legacy_data_received_ != received);
    legacy_data_received_ = received;
    if (received) {
        last_data_received_ms_ = millis();
    }
    xSemaphoreGive(state_mutex_);
    return changed;
}

void ConnectionStateManager::update_received_data(uint8_t soc, int32_t power, uint32_t voltage_mv, bool* out_soc_changed, bool* out_power_changed) {
    if (out_soc_changed) *out_soc_changed = false;
    if (out_power_changed) *out_power_changed = false;

    if (!state_mutex_) {
        ESPNow::received_soc = soc;
        ESPNow::received_power = power;
        ESPNow::received_voltage_mv = voltage_mv;
        legacy_data_received_ = true;
        return;
    }

    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }

    const bool soc_changed = (ESPNow::received_soc != soc);
    const bool power_changed = (ESPNow::received_power != power);

    ESPNow::received_soc = soc;
    ESPNow::received_power = power;
    ESPNow::received_voltage_mv = voltage_mv;
    legacy_data_received_ = true;
    last_data_received_ms_ = millis();

    if (out_soc_changed) *out_soc_changed = soc_changed;
    if (out_power_changed) *out_power_changed = power_changed;

    xSemaphoreGive(state_mutex_);
}

uint32_t ConnectionStateManager::last_data_received_ms() {
    if (!state_mutex_) return 0;
    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    uint32_t value = last_data_received_ms_;
    xSemaphoreGive(state_mutex_);
    return value;
}

bool ConnectionStateManager::is_data_stale(uint32_t timeout_ms) {
    if (!state_mutex_) return true;
    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return true;
    const uint32_t now = millis();
    const bool stale = (last_data_received_ms_ == 0) || ((now - last_data_received_ms_) > timeout_ms);
    xSemaphoreGive(state_mutex_);
    return stale;
}

uint32_t ConnectionStateManager::get_connection_uptime_ms() {
    if (!state_mutex_) return 0;
    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return 0;

    uint32_t uptime = 0;
    if (legacy_transmitter_connected_ && connection_start_time_ms_ != 0) {
        uptime = millis() - connection_start_time_ms_;
    }

    xSemaphoreGive(state_mutex_);
    return uptime;
}

uint32_t ConnectionStateManager::get_connection_count() {
    if (!state_mutex_) return 0;
    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return 0;
    const uint32_t count = connection_count_;
    xSemaphoreGive(state_mutex_);
    return count;
}

void ConnectionStateManager::get_all_state(bool& out_connected, int& out_channel, uint8_t* out_mac) {
    if (!out_mac) {
        LOG_ERROR("CONN_STATE", "MAC output buffer is nullptr");
        return;
    }
    
    if (!state_mutex_) {
        LOG_ERROR("CONN_STATE", "ConnectionStateManager not initialized");
        return;
    }
    
    if (xSemaphoreTake(state_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_ERROR("CONN_STATE", "Failed to acquire state lock");
        return;
    }
    
    out_connected = legacy_transmitter_connected_;
    out_channel = ESPNow::wifi_channel;
    memcpy(out_mac, ESPNow::transmitter_mac, 6);
    
    xSemaphoreGive(state_mutex_);
}

#endif

// Compatibility-only lightweight implementation (deprecated API).
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

uint8_t ConnectionStateManager::get_soc() { return ESPNow::received_soc; }

bool ConnectionStateManager::set_soc(uint8_t soc) {
    const bool changed = (ESPNow::received_soc != soc);
    ESPNow::received_soc = soc;
    last_data_received_ms_ = millis();
    legacy_data_received_compat_ = true;
    return changed;
}

int32_t ConnectionStateManager::get_power() { return ESPNow::received_power; }

bool ConnectionStateManager::set_power(int32_t power) {
    const bool changed = (ESPNow::received_power != power);
    ESPNow::received_power = power;
    last_data_received_ms_ = millis();
    legacy_data_received_compat_ = true;
    return changed;
}

uint32_t ConnectionStateManager::get_voltage_mv() { return ESPNow::received_voltage_mv; }

bool ConnectionStateManager::set_voltage_mv(uint32_t voltage_mv) {
    const bool changed = (ESPNow::received_voltage_mv != voltage_mv);
    ESPNow::received_voltage_mv = voltage_mv;
    last_data_received_ms_ = millis();
    legacy_data_received_compat_ = true;
    return changed;
}

bool ConnectionStateManager::has_data_received() {
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
    const bool soc_changed = (ESPNow::received_soc != soc);
    const bool power_changed = (ESPNow::received_power != power);

    ESPNow::received_soc = soc;
    ESPNow::received_power = power;
    ESPNow::received_voltage_mv = voltage_mv;
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
