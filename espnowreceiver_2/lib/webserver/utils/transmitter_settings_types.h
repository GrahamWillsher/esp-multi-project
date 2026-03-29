#ifndef TRANSMITTER_SETTINGS_TYPES_H
#define TRANSMITTER_SETTINGS_TYPES_H

#include <Arduino.h>

// Battery settings structure (matches transmitter)
struct BatterySettings {
    uint32_t capacity_wh;
    uint32_t max_voltage_mv;
    uint32_t min_voltage_mv;
    float max_charge_current_a;
    float max_discharge_current_a;
    uint8_t soc_high_limit;
    uint8_t soc_low_limit;
    uint8_t cell_count;
    uint8_t chemistry;
    uint32_t version;  // Version tracking for synchronization
};

struct BatteryEmulatorSettings {
    bool double_battery;
    uint16_t pack_max_voltage_dV;
    uint16_t pack_min_voltage_dV;
    uint16_t cell_max_voltage_mV;
    uint16_t cell_min_voltage_mV;
    bool soc_estimated;
    uint8_t led_mode;  // 0=Classic, 1=Energy Flow, 2=Heartbeat
};

struct PowerSettings {
    uint16_t charge_w;
    uint16_t discharge_w;
    uint16_t max_precharge_ms;
    uint16_t precharge_duration_ms;
    uint8_t  equipment_stop_type;  // 0=Not connected, 1=Latching, 2=Momentary
    bool external_precharge_enabled;
    bool no_inverter_disconnect_contactor;
};

struct InverterSettings {
    uint8_t cells;
    uint8_t modules;
    uint8_t cells_per_module;
    uint16_t voltage_level;
    uint16_t capacity_ah;
    uint8_t battery_type;
};

struct CanSettings {
    uint16_t frequency_khz;
    uint16_t fd_frequency_mhz;
    uint16_t sofar_id;
    uint16_t pylon_send_interval_ms;
    bool use_canfd_as_classic;
};

struct ContactorSettings {
    bool control_enabled;
    bool nc_contactor;
    uint16_t pwm_frequency_hz;
    bool pwm_control_enabled;
    uint16_t pwm_hold_duty;  // PWM hold duty cycle (1-1023)
    bool periodic_bms_reset;
    bool bms_first_align_enabled;
    uint16_t bms_first_align_target_minutes;
};

#endif // TRANSMITTER_SETTINGS_TYPES_H
