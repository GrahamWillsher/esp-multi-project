/**
 * Receiver State Manager Implementation
 * Thread-safe encapsulation for global volatile variables
 */

#include "receiver_state_manager.h"
#include "../config/logging_config.h"
#include <Arduino.h>

// ============================================================================
// ESPNowState Implementation
// ============================================================================

ESPNowState::ESPNowState()
    : _received_soc(50),
      _received_power(0),
      _received_voltage_mv(0),
      _data_received(false),
      _transmitter_connected(false),
      _wifi_channel(1) {
    _mutex = xSemaphoreCreateMutex();
    if (!_mutex) {
        LOG_ERROR("receiver_state", "Failed to create ESPNowState mutex");
    }
}

ESPNowState::~ESPNowState() {
    if (_mutex) {
        vSemaphoreDelete(_mutex);
    }
}

void ESPNowState::_lock() const {
    if (_mutex) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
    }
}

void ESPNowState::_unlock() const {
    if (_mutex) {
        xSemaphoreGive(_mutex);
    }
}

uint8_t ESPNowState::get_soc() const {
    _lock();
    uint8_t value = _received_soc;
    _unlock();
    return value;
}

void ESPNowState::set_soc(uint8_t soc) {
    _lock();
    _received_soc = soc;
    _unlock();
}

int32_t ESPNowState::get_power() const {
    _lock();
    int32_t value = _received_power;
    _unlock();
    return value;
}

void ESPNowState::set_power(int32_t power) {
    _lock();
    _received_power = power;
    _unlock();
}

uint32_t ESPNowState::get_voltage_mv() const {
    _lock();
    uint32_t value = _received_voltage_mv;
    _unlock();
    return value;
}

void ESPNowState::set_voltage_mv(uint32_t voltage) {
    _lock();
    _received_voltage_mv = voltage;
    _unlock();
}

bool ESPNowState::is_data_received() const {
    _lock();
    bool value = _data_received;
    _unlock();
    return value;
}

void ESPNowState::set_data_received(bool received) {
    _lock();
    _data_received = received;
    _unlock();
}

bool ESPNowState::is_transmitter_connected() const {
    _lock();
    bool value = _transmitter_connected;
    _unlock();
    return value;
}

void ESPNowState::set_transmitter_connected(bool connected) {
    _lock();
    _transmitter_connected = connected;
    _unlock();
}

int ESPNowState::get_wifi_channel() const {
    _lock();
    int value = _wifi_channel;
    _unlock();
    return value;
}

void ESPNowState::set_wifi_channel(int channel) {
    _lock();
    _wifi_channel = channel;
    _unlock();
}

void ESPNowState::update_receiver_data(uint8_t soc, int32_t power, uint32_t voltage_mv, bool data_flag) {
    _lock();
    _received_soc = soc;
    _received_power = power;
    _received_voltage_mv = voltage_mv;
    _data_received = data_flag;
    _unlock();
}

// ============================================================================
// BatteryStatusState Implementation
// ============================================================================

BatteryStatusState::BatteryStatusState()
    : _soc_percent(0.0),
      _voltage_V(0.0),
      _current_A(0.0),
      _temperature_C(0.0),
      _power_W(0),
      _max_charge_power_W(0),
      _max_discharge_power_W(0),
      _bms_status(0),
      _status_received(false) {
    _mutex = xSemaphoreCreateMutex();
    if (!_mutex) {
        LOG_ERROR("receiver_state", "Failed to create BatteryStatusState mutex");
    }
}

BatteryStatusState::~BatteryStatusState() {
    if (_mutex) {
        vSemaphoreDelete(_mutex);
    }
}

void BatteryStatusState::_lock() const {
    if (_mutex) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
    }
}

void BatteryStatusState::_unlock() const {
    if (_mutex) {
        xSemaphoreGive(_mutex);
    }
}

float BatteryStatusState::get_soc_percent() const {
    _lock();
    float value = _soc_percent;
    _unlock();
    return value;
}

void BatteryStatusState::set_soc_percent(float soc) {
    _lock();
    _soc_percent = soc;
    _unlock();
}

float BatteryStatusState::get_voltage_V() const {
    _lock();
    float value = _voltage_V;
    _unlock();
    return value;
}

void BatteryStatusState::set_voltage_V(float voltage) {
    _lock();
    _voltage_V = voltage;
    _unlock();
}

float BatteryStatusState::get_current_A() const {
    _lock();
    float value = _current_A;
    _unlock();
    return value;
}

void BatteryStatusState::set_current_A(float current) {
    _lock();
    _current_A = current;
    _unlock();
}

float BatteryStatusState::get_temperature_C() const {
    _lock();
    float value = _temperature_C;
    _unlock();
    return value;
}

void BatteryStatusState::set_temperature_C(float temp) {
    _lock();
    _temperature_C = temp;
    _unlock();
}

int32_t BatteryStatusState::get_power_W() const {
    _lock();
    int32_t value = _power_W;
    _unlock();
    return value;
}

void BatteryStatusState::set_power_W(int32_t power) {
    _lock();
    _power_W = power;
    _unlock();
}

uint16_t BatteryStatusState::get_max_charge_power_W() const {
    _lock();
    uint16_t value = _max_charge_power_W;
    _unlock();
    return value;
}

void BatteryStatusState::set_max_charge_power_W(uint16_t power) {
    _lock();
    _max_charge_power_W = power;
    _unlock();
}

uint16_t BatteryStatusState::get_max_discharge_power_W() const {
    _lock();
    uint16_t value = _max_discharge_power_W;
    _unlock();
    return value;
}

void BatteryStatusState::set_max_discharge_power_W(uint16_t power) {
    _lock();
    _max_discharge_power_W = power;
    _unlock();
}

uint8_t BatteryStatusState::get_bms_status() const {
    _lock();
    uint8_t value = _bms_status;
    _unlock();
    return value;
}

void BatteryStatusState::set_bms_status(uint8_t status) {
    _lock();
    _bms_status = status;
    _unlock();
}

bool BatteryStatusState::is_status_received() const {
    _lock();
    bool value = _status_received;
    _unlock();
    return value;
}

void BatteryStatusState::set_status_received(bool received) {
    _lock();
    _status_received = received;
    _unlock();
}

void BatteryStatusState::update_battery_status(float soc, float voltage, float current, float temp,
                                              int32_t power, uint16_t max_charge, uint8_t status) {
    _lock();
    _soc_percent = soc;
    _voltage_V = voltage;
    _current_A = current;
    _temperature_C = temp;
    _power_W = power;
    _max_charge_power_W = max_charge;
    _bms_status = status;
    _status_received = true;
    _unlock();
}

// ============================================================================
// ChargerStatusState Implementation
// ============================================================================

ChargerStatusState::ChargerStatusState()
    : _hv_voltage_V(0.0),
      _hv_current_A(0.0),
      _lv_voltage_V(0.0),
      _ac_voltage_V(0),
      _power_W(0),
      _status(0),
      _received(false) {
    _mutex = xSemaphoreCreateMutex();
    if (!_mutex) {
        LOG_ERROR("receiver_state", "Failed to create ChargerStatusState mutex");
    }
}

ChargerStatusState::~ChargerStatusState() {
    if (_mutex) {
        vSemaphoreDelete(_mutex);
    }
}

void ChargerStatusState::_lock() const {
    if (_mutex) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
    }
}

void ChargerStatusState::_unlock() const {
    if (_mutex) {
        xSemaphoreGive(_mutex);
    }
}

float ChargerStatusState::get_hv_voltage_V() const {
    _lock();
    float value = _hv_voltage_V;
    _unlock();
    return value;
}

void ChargerStatusState::set_hv_voltage_V(float voltage) {
    _lock();
    _hv_voltage_V = voltage;
    _unlock();
}

float ChargerStatusState::get_hv_current_A() const {
    _lock();
    float value = _hv_current_A;
    _unlock();
    return value;
}

void ChargerStatusState::set_hv_current_A(float current) {
    _lock();
    _hv_current_A = current;
    _unlock();
}

float ChargerStatusState::get_lv_voltage_V() const {
    _lock();
    float value = _lv_voltage_V;
    _unlock();
    return value;
}

void ChargerStatusState::set_lv_voltage_V(float voltage) {
    _lock();
    _lv_voltage_V = voltage;
    _unlock();
}

uint16_t ChargerStatusState::get_ac_voltage_V() const {
    _lock();
    uint16_t value = _ac_voltage_V;
    _unlock();
    return value;
}

void ChargerStatusState::set_ac_voltage_V(uint16_t voltage) {
    _lock();
    _ac_voltage_V = voltage;
    _unlock();
}

uint16_t ChargerStatusState::get_power_W() const {
    _lock();
    uint16_t value = _power_W;
    _unlock();
    return value;
}

void ChargerStatusState::set_power_W(uint16_t power) {
    _lock();
    _power_W = power;
    _unlock();
}

uint8_t ChargerStatusState::get_status() const {
    _lock();
    uint8_t value = _status;
    _unlock();
    return value;
}

void ChargerStatusState::set_status(uint8_t status) {
    _lock();
    _status = status;
    _unlock();
}

bool ChargerStatusState::is_received() const {
    _lock();
    bool value = _received;
    _unlock();
    return value;
}

void ChargerStatusState::set_received(bool received) {
    _lock();
    _received = received;
    _unlock();
}

void ChargerStatusState::update_charger_status(float hv_v, float hv_a, float lv_v, uint16_t ac_v,
                                              uint16_t power, uint8_t status) {
    _lock();
    _hv_voltage_V = hv_v;
    _hv_current_A = hv_a;
    _lv_voltage_V = lv_v;
    _ac_voltage_V = ac_v;
    _power_W = power;
    _status = status;
    _received = true;
    _unlock();
}

// ============================================================================
// InverterStatusState Implementation
// ============================================================================

InverterStatusState::InverterStatusState()
    : _ac_voltage_V(0),
      _ac_frequency_Hz(0.0),
      _ac_current_A(0.0),
      _power_W(0),
      _status(0),
      _received(false) {
    _mutex = xSemaphoreCreateMutex();
    if (!_mutex) {
        LOG_ERROR("receiver_state", "Failed to create InverterStatusState mutex");
    }
}

InverterStatusState::~InverterStatusState() {
    if (_mutex) {
        vSemaphoreDelete(_mutex);
    }
}

void InverterStatusState::_lock() const {
    if (_mutex) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
    }
}

void InverterStatusState::_unlock() const {
    if (_mutex) {
        xSemaphoreGive(_mutex);
    }
}

uint16_t InverterStatusState::get_ac_voltage_V() const {
    _lock();
    uint16_t value = _ac_voltage_V;
    _unlock();
    return value;
}

void InverterStatusState::set_ac_voltage_V(uint16_t voltage) {
    _lock();
    _ac_voltage_V = voltage;
    _unlock();
}

float InverterStatusState::get_ac_frequency_Hz() const {
    _lock();
    float value = _ac_frequency_Hz;
    _unlock();
    return value;
}

void InverterStatusState::set_ac_frequency_Hz(float frequency) {
    _lock();
    _ac_frequency_Hz = frequency;
    _unlock();
}

float InverterStatusState::get_ac_current_A() const {
    _lock();
    float value = _ac_current_A;
    _unlock();
    return value;
}

void InverterStatusState::set_ac_current_A(float current) {
    _lock();
    _ac_current_A = current;
    _unlock();
}

int32_t InverterStatusState::get_power_W() const {
    _lock();
    int32_t value = _power_W;
    _unlock();
    return value;
}

void InverterStatusState::set_power_W(int32_t power) {
    _lock();
    _power_W = power;
    _unlock();
}

uint8_t InverterStatusState::get_status() const {
    _lock();
    uint8_t value = _status;
    _unlock();
    return value;
}

void InverterStatusState::set_status(uint8_t status) {
    _lock();
    _status = status;
    _unlock();
}

bool InverterStatusState::is_received() const {
    _lock();
    bool value = _received;
    _unlock();
    return value;
}

void InverterStatusState::set_received(bool received) {
    _lock();
    _received = received;
    _unlock();
}

void InverterStatusState::update_inverter_status(uint16_t ac_v, float ac_f, float ac_a,
                                                int32_t power, uint8_t status) {
    _lock();
    _ac_voltage_V = ac_v;
    _ac_frequency_Hz = ac_f;
    _ac_current_A = ac_a;
    _power_W = power;
    _status = status;
    _received = true;
    _unlock();
}

// ============================================================================
// SystemStatusState Implementation
// ============================================================================

SystemStatusState::SystemStatusState()
    : _contactor_state(0),
      _error_flags(0),
      _warning_flags(0),
      _uptime_seconds(0),
      _received(false) {
    _mutex = xSemaphoreCreateMutex();
    if (!_mutex) {
        LOG_ERROR("receiver_state", "Failed to create SystemStatusState mutex");
    }
}

SystemStatusState::~SystemStatusState() {
    if (_mutex) {
        vSemaphoreDelete(_mutex);
    }
}

void SystemStatusState::_lock() const {
    if (_mutex) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
    }
}

void SystemStatusState::_unlock() const {
    if (_mutex) {
        xSemaphoreGive(_mutex);
    }
}

uint8_t SystemStatusState::get_contactor_state() const {
    _lock();
    uint8_t value = _contactor_state;
    _unlock();
    return value;
}

void SystemStatusState::set_contactor_state(uint8_t state) {
    _lock();
    _contactor_state = state;
    _unlock();
}

uint8_t SystemStatusState::get_error_flags() const {
    _lock();
    uint8_t value = _error_flags;
    _unlock();
    return value;
}

void SystemStatusState::set_error_flags(uint8_t flags) {
    _lock();
    _error_flags = flags;
    _unlock();
}

uint8_t SystemStatusState::get_warning_flags() const {
    _lock();
    uint8_t value = _warning_flags;
    _unlock();
    return value;
}

void SystemStatusState::set_warning_flags(uint8_t flags) {
    _lock();
    _warning_flags = flags;
    _unlock();
}

uint32_t SystemStatusState::get_uptime_seconds() const {
    _lock();
    uint32_t value = _uptime_seconds;
    _unlock();
    return value;
}

void SystemStatusState::set_uptime_seconds(uint32_t uptime) {
    _lock();
    _uptime_seconds = uptime;
    _unlock();
}

bool SystemStatusState::is_received() const {
    _lock();
    bool value = _received;
    _unlock();
    return value;
}

void SystemStatusState::set_received(bool received) {
    _lock();
    _received = received;
    _unlock();
}

void SystemStatusState::update_system_status(uint8_t contactor, uint8_t errors, uint8_t warnings, uint32_t uptime) {
    _lock();
    _contactor_state = contactor;
    _error_flags = errors;
    _warning_flags = warnings;
    _uptime_seconds = uptime;
    _received = true;
    _unlock();
}

// ============================================================================
// Global Instances
// ============================================================================

ESPNowState g_espnow_state;
BatteryStatusState g_battery_status;
ChargerStatusState g_charger_status;
InverterStatusState g_inverter_status;
SystemStatusState g_system_status;
