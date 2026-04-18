#pragma once

#include <Arduino.h>
#include <cstdint>
#include <esp32common/espnow/common.h>

namespace BatteryData {

struct SectionState {
    bool received = false;
    bool stale = true;
    uint32_t last_update_ms = 0;
    uint32_t version = 0;
};

struct TelemetrySnapshot {
    // Battery status (real-time)
    float soc_percent = 0.0f;
    float voltage_V = 0.0f;
    float current_A = 0.0f;
    float temperature_C = 0.0f;
    int32_t power_W = 0;
    uint16_t max_charge_power_W = 0;
    uint16_t max_discharge_power_W = 0;
    uint8_t bms_status = BMS_OFFLINE;

    // Battery info/settings
    uint32_t total_capacity_Wh = 0;
    uint32_t reported_capacity_Wh = 0;
    uint16_t max_design_voltage_V = 0;
    uint16_t min_design_voltage_V = 0;
    uint16_t max_cell_voltage_mV = 0;
    uint16_t min_cell_voltage_mV = 0;
    uint8_t number_of_cells = 0;
    uint8_t chemistry = 0;

    // Charger status
    float charger_hv_voltage_V = 0.0f;
    float charger_hv_current_A = 0.0f;
    float charger_lv_voltage_V = 0.0f;
    uint16_t charger_ac_voltage_V = 0;
    uint16_t charger_power_W = 0;
    uint8_t charger_status = 0;

    // Inverter status
    uint16_t inverter_ac_voltage_V = 0;
    float inverter_ac_frequency_Hz = 0.0f;
    float inverter_ac_current_A = 0.0f;
    int32_t inverter_power_W = 0;
    uint8_t inverter_status = 0;

    // System status
    uint8_t contactor_state = 0;
    uint8_t error_flags = 0;
    uint8_t warning_flags = 0;
    uint32_t uptime_seconds = 0;

    // Component config
    uint8_t bms_type = 0;
    uint8_t secondary_bms_type = 0;
    uint8_t battery_type = 0;
    uint8_t inverter_type = 0;
    uint8_t charger_type = 0;
    uint8_t shunt_type = 0;
    uint8_t multi_battery_enabled = 0;
    uint32_t component_config_version = 0;

    // Per-section metadata
    SectionState battery_status;
    SectionState battery_info;
    SectionState charger_status_meta;
    SectionState inverter_status_meta;
    SectionState system_status_meta;
    SectionState component_config_meta;
};

void update_battery_status(const battery_status_msg_t& data);
void update_basic_telemetry(uint8_t soc_percent, int32_t power_w, uint32_t voltage_mv = 0);
void update_battery_info(const battery_info_msg_t& data);
void update_battery_info(const battery_settings_full_msg_t& data, uint32_t settings_version);
void update_charger_status(const charger_status_msg_t& data);
void update_inverter_status(const inverter_status_msg_t& data);
void update_system_status(const system_status_msg_t& data);
void update_component_config(const component_config_msg_t& data);

bool read_snapshot(TelemetrySnapshot& out_snapshot);
uint32_t snapshot_sequence();
void refresh_staleness(uint32_t now_ms = 0);

}  // namespace BatteryData
