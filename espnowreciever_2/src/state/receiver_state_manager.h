/**
 * Receiver State Manager
 * Thread-safe encapsulation for global volatile variables
 * 
 * Provides atomic access to receiver-specific state through safe accessor classes
 * eliminating raw volatile variable access and improving thread safety.
 */

#pragma once

#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/**
 * ESP-NOW Connection State Manager
 * Thread-safe accessors for ESP-NOW communication state
 */
class ESPNowState {
public:
    ESPNowState();
    ~ESPNowState();

    // Receiver data accessors
    uint8_t get_soc() const;
    void set_soc(uint8_t soc);

    int32_t get_power() const;
    void set_power(int32_t power);

    uint32_t get_voltage_mv() const;
    void set_voltage_mv(uint32_t voltage);

    bool is_data_received() const;
    void set_data_received(bool received);

    // Connection state accessors
    bool is_transmitter_connected() const;
    void set_transmitter_connected(bool connected);

    int get_wifi_channel() const;
    void set_wifi_channel(int channel);

    // Batch update for received data (reduces lock contention)
    void update_receiver_data(uint8_t soc, int32_t power, uint32_t voltage_mv, bool data_flag);

private:
    // Underlying volatile storage
    volatile uint8_t _received_soc;
    volatile int32_t _received_power;
    volatile uint32_t _received_voltage_mv;
    volatile bool _data_received;
    volatile bool _transmitter_connected;
    volatile int _wifi_channel;

    // Thread safety
    mutable SemaphoreHandle_t _mutex;

    // Internal lock management
    void _lock() const;
    void _unlock() const;
};

/**
 * Battery Status State Manager
 * Thread-safe accessors for battery real-time data
 */
class BatteryStatusState {
public:
    BatteryStatusState();
    ~BatteryStatusState();

    // Battery status accessors
    float get_soc_percent() const;
    void set_soc_percent(float soc);

    float get_voltage_V() const;
    void set_voltage_V(float voltage);

    float get_current_A() const;
    void set_current_A(float current);

    float get_temperature_C() const;
    void set_temperature_C(float temp);

    int32_t get_power_W() const;
    void set_power_W(int32_t power);

    uint16_t get_max_charge_power_W() const;
    void set_max_charge_power_W(uint16_t power);

    uint16_t get_max_discharge_power_W() const;
    void set_max_discharge_power_W(uint16_t power);

    uint8_t get_bms_status() const;
    void set_bms_status(uint8_t status);

    bool is_status_received() const;
    void set_status_received(bool received);

    // Batch update for battery status (reduces lock contention)
    void update_battery_status(float soc, float voltage, float current, float temp,
                              int32_t power, uint16_t max_charge, uint8_t status);

private:
    // Underlying volatile storage
    volatile float _soc_percent;
    volatile float _voltage_V;
    volatile float _current_A;
    volatile float _temperature_C;
    volatile int32_t _power_W;
    volatile uint16_t _max_charge_power_W;
    volatile uint16_t _max_discharge_power_W;
    volatile uint8_t _bms_status;
    volatile bool _status_received;

    // Thread safety
    mutable SemaphoreHandle_t _mutex;

    // Internal lock management
    void _lock() const;
    void _unlock() const;
};

/**
 * Charger Status State Manager
 * Thread-safe accessors for charger real-time data
 */
class ChargerStatusState {
public:
    ChargerStatusState();
    ~ChargerStatusState();

    // Charger voltage/current accessors
    float get_hv_voltage_V() const;
    void set_hv_voltage_V(float voltage);

    float get_hv_current_A() const;
    void set_hv_current_A(float current);

    float get_lv_voltage_V() const;
    void set_lv_voltage_V(float voltage);

    uint16_t get_ac_voltage_V() const;
    void set_ac_voltage_V(uint16_t voltage);

    uint16_t get_power_W() const;
    void set_power_W(uint16_t power);

    uint8_t get_status() const;
    void set_status(uint8_t status);

    bool is_received() const;
    void set_received(bool received);

    // Batch update for charger status
    void update_charger_status(float hv_v, float hv_a, float lv_v, uint16_t ac_v,
                              uint16_t power, uint8_t status);

private:
    // Underlying volatile storage
    volatile float _hv_voltage_V;
    volatile float _hv_current_A;
    volatile float _lv_voltage_V;
    volatile uint16_t _ac_voltage_V;
    volatile uint16_t _power_W;
    volatile uint8_t _status;
    volatile bool _received;

    // Thread safety
    mutable SemaphoreHandle_t _mutex;

    // Internal lock management
    void _lock() const;
    void _unlock() const;
};

/**
 * Inverter Status State Manager
 * Thread-safe accessors for inverter real-time data
 */
class InverterStatusState {
public:
    InverterStatusState();
    ~InverterStatusState();

    // Inverter AC accessors
    uint16_t get_ac_voltage_V() const;
    void set_ac_voltage_V(uint16_t voltage);

    float get_ac_frequency_Hz() const;
    void set_ac_frequency_Hz(float frequency);

    float get_ac_current_A() const;
    void set_ac_current_A(float current);

    int32_t get_power_W() const;
    void set_power_W(int32_t power);

    uint8_t get_status() const;
    void set_status(uint8_t status);

    bool is_received() const;
    void set_received(bool received);

    // Batch update for inverter status
    void update_inverter_status(uint16_t ac_v, float ac_f, float ac_a,
                               int32_t power, uint8_t status);

private:
    // Underlying volatile storage
    volatile uint16_t _ac_voltage_V;
    volatile float _ac_frequency_Hz;
    volatile float _ac_current_A;
    volatile int32_t _power_W;
    volatile uint8_t _status;
    volatile bool _received;

    // Thread safety
    mutable SemaphoreHandle_t _mutex;

    // Internal lock management
    void _lock() const;
    void _unlock() const;
};

/**
 * System Status State Manager
 * Thread-safe accessors for system state
 */
class SystemStatusState {
public:
    SystemStatusState();
    ~SystemStatusState();

    // System state accessors
    uint8_t get_contactor_state() const;
    void set_contactor_state(uint8_t state);

    uint8_t get_error_flags() const;
    void set_error_flags(uint8_t flags);

    uint8_t get_warning_flags() const;
    void set_warning_flags(uint8_t flags);

    uint32_t get_uptime_seconds() const;
    void set_uptime_seconds(uint32_t uptime);

    bool is_received() const;
    void set_received(bool received);

    // Batch update for system status
    void update_system_status(uint8_t contactor, uint8_t errors, uint8_t warnings, uint32_t uptime);

private:
    // Underlying volatile storage
    volatile uint8_t _contactor_state;
    volatile uint8_t _error_flags;
    volatile uint8_t _warning_flags;
    volatile uint32_t _uptime_seconds;
    volatile bool _received;

    // Thread safety
    mutable SemaphoreHandle_t _mutex;

    // Internal lock management
    void _lock() const;
    void _unlock() const;
};

// Global instances
extern ESPNowState g_espnow_state;
extern BatteryStatusState g_battery_status;
extern ChargerStatusState g_charger_status;
extern InverterStatusState g_inverter_status;
extern SystemStatusState g_system_status;
