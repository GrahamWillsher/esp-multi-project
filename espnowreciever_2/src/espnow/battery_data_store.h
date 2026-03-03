/**
 * Battery Data Store
 * Shared runtime data for battery/charger/inverter/system handlers
 */

#pragma once

#include <Arduino.h>
#include <cstdint>

namespace BatteryData {
    // Battery status (real-time)
    extern volatile float soc_percent;
    extern volatile float voltage_V;
    extern volatile float current_A;
    extern volatile float temperature_C;
    extern volatile int32_t power_W;
    extern volatile uint16_t max_charge_power_W;
    extern volatile uint16_t max_discharge_power_W;
    extern volatile uint8_t bms_status;
    extern volatile bool status_received;

    // Battery info (static)
    extern uint32_t total_capacity_Wh;
    extern uint32_t reported_capacity_Wh;
    extern uint16_t max_design_voltage_V;
    extern uint16_t min_design_voltage_V;
    extern uint16_t max_cell_voltage_mV;
    extern uint16_t min_cell_voltage_mV;
    extern uint8_t number_of_cells;
    extern uint8_t chemistry;
    extern bool info_received;

    // Charger status (real-time)
    extern volatile float charger_hv_voltage_V;
    extern volatile float charger_hv_current_A;
    extern volatile float charger_lv_voltage_V;
    extern volatile uint16_t charger_ac_voltage_V;
    extern volatile uint16_t charger_power_W;
    extern volatile uint8_t charger_status;
    extern volatile bool charger_received;

    // Inverter status (real-time)
    extern volatile uint16_t inverter_ac_voltage_V;
    extern volatile float inverter_ac_frequency_Hz;
    extern volatile float inverter_ac_current_A;
    extern volatile int32_t inverter_power_W;
    extern volatile uint8_t inverter_status;
    extern volatile bool inverter_received;

    // System status (real-time)
    extern volatile uint8_t contactor_state;
    extern volatile uint8_t error_flags;
    extern volatile uint8_t warning_flags;
    extern volatile uint32_t uptime_seconds;
    extern volatile bool system_received;
}
