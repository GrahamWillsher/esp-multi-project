#include "battery_data_store.h"
#include <esp32common/espnow/common.h>

namespace BatteryData {
    // Battery status (real-time)
    volatile float soc_percent = 0.0;
    volatile float voltage_V = 0.0;
    volatile float current_A = 0.0;
    volatile float temperature_C = 0.0;
    volatile int32_t power_W = 0;
    volatile uint16_t max_charge_power_W = 0;
    volatile uint16_t max_discharge_power_W = 0;
    volatile uint8_t bms_status = BMS_OFFLINE;
    volatile bool status_received = false;

    // Battery info (static)
    uint32_t total_capacity_Wh = 0;
    uint32_t reported_capacity_Wh = 0;
    uint16_t max_design_voltage_V = 0;
    uint16_t min_design_voltage_V = 0;
    uint16_t max_cell_voltage_mV = 0;
    uint16_t min_cell_voltage_mV = 0;
    uint8_t number_of_cells = 0;
    uint8_t chemistry = 0;
    bool info_received = false;

    // Charger status (real-time)
    volatile float charger_hv_voltage_V = 0.0;
    volatile float charger_hv_current_A = 0.0;
    volatile float charger_lv_voltage_V = 0.0;
    volatile uint16_t charger_ac_voltage_V = 0;
    volatile uint16_t charger_power_W = 0;
    volatile uint8_t charger_status = 0;
    volatile bool charger_received = false;

    // Inverter status (real-time)
    volatile uint16_t inverter_ac_voltage_V = 0;
    volatile float inverter_ac_frequency_Hz = 0.0;
    volatile float inverter_ac_current_A = 0.0;
    volatile int32_t inverter_power_W = 0;
    volatile uint8_t inverter_status = 0;
    volatile bool inverter_received = false;

    // System status (real-time)
    volatile uint8_t contactor_state = 0;
    volatile uint8_t error_flags = 0;
    volatile uint8_t warning_flags = 0;
    volatile uint32_t uptime_seconds = 0;
    volatile bool system_received = false;
}
