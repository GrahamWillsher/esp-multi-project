/**
 * Battery Emulator Data Handlers - Phase 1
 * 
 * Processes battery/charger/inverter/system messages and updates global state
 */

#include "battery_handlers.h"
#include <Arduino.h>

// Battery data globals (will be used by web UI)
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

bool validate_checksum(const void* data, size_t len) {
    const uint8_t* bytes = (const uint8_t*)data;
    uint16_t calculated_sum = 0;
    
    // Sum all bytes except last 2 (checksum field)
    for (size_t i = 0; i < len - 2; i++) {
        calculated_sum += bytes[i];
    }
    
    // Extract checksum from message (last 2 bytes, little-endian)
    uint16_t message_checksum = bytes[len-2] | (bytes[len-1] << 8);
    
    return (calculated_sum == message_checksum);
}

void handle_battery_status(const espnow_queue_msg_t* msg) {
    const battery_status_msg_t* data = (battery_status_msg_t*)msg->data;
    
    // Validate checksum
    if (!validate_checksum(data, sizeof(*data))) {
        Serial.println("[BATTERY] Invalid checksum - message rejected");
        return;
    }
    
    // Update global state
    BatteryData::soc_percent = data->soc_percent_100 / 100.0;
    BatteryData::voltage_V = data->voltage_mV / 1000.0;
    BatteryData::current_A = data->current_mA / 1000.0;
    BatteryData::temperature_C = data->temperature_dC / 10.0;
    BatteryData::power_W = data->power_W;
    BatteryData::max_charge_power_W = data->max_charge_power_W;
    BatteryData::max_discharge_power_W = data->max_discharge_power_W;
    BatteryData::bms_status = data->bms_status;
    BatteryData::status_received = true;
    
    Serial.printf("[BATTERY] Status: SOC=%.2f%%, V=%.2fV, I=%.2fA, T=%.1fC, P=%dW, BMS=%d\n",
                 BatteryData::soc_percent, BatteryData::voltage_V, BatteryData::current_A,
                 BatteryData::temperature_C, BatteryData::power_W, BatteryData::bms_status);
    
    // TODO Phase 2: Notify SSE clients
    // notify_sse_battery_updated();
    
    // TODO Phase 3: Update TFT display
    // update_battery_display();
}

void handle_battery_info(const espnow_queue_msg_t* msg) {
    const battery_info_msg_t* data = (battery_info_msg_t*)msg->data;
    
    // Validate checksum
    if (!validate_checksum(data, sizeof(*data))) {
        Serial.println("[BATTERY] Info: Invalid checksum - message rejected");
        return;
    }
    
    // Update global state (static data)
    BatteryData::total_capacity_Wh = data->total_capacity_Wh;
    BatteryData::reported_capacity_Wh = data->reported_capacity_Wh;
    BatteryData::max_design_voltage_V = data->max_design_voltage_dV / 10;
    BatteryData::min_design_voltage_V = data->min_design_voltage_dV / 10;
    BatteryData::max_cell_voltage_mV = data->max_cell_voltage_mV;
    BatteryData::min_cell_voltage_mV = data->min_cell_voltage_mV;
    BatteryData::number_of_cells = data->number_of_cells;
    BatteryData::chemistry = data->chemistry;
    BatteryData::info_received = true;
    
    const char* chemistry_str[] = {"NCA", "NMC", "LFP", "LTO"};
    Serial.printf("[BATTERY] Info: %dWh capacity, %d cells, %s chemistry, V: %d-%dV\n",
                 BatteryData::total_capacity_Wh, BatteryData::number_of_cells,
                 chemistry_str[data->chemistry], BatteryData::min_design_voltage_V,
                 BatteryData::max_design_voltage_V);
}

void handle_charger_status(const espnow_queue_msg_t* msg) {
    const charger_status_msg_t* data = (charger_status_msg_t*)msg->data;
    
    // Validate checksum
    if (!validate_checksum(data, sizeof(*data))) {
        Serial.println("[CHARGER] Invalid checksum - message rejected");
        return;
    }
    
    // Update global state
    BatteryData::charger_hv_voltage_V = data->hv_voltage_dV / 10.0;
    BatteryData::charger_hv_current_A = data->hv_current_dA / 10.0;
    BatteryData::charger_lv_voltage_V = data->lv_voltage_dV / 10.0;
    BatteryData::charger_ac_voltage_V = data->ac_voltage_V;
    BatteryData::charger_power_W = data->power_W;
    BatteryData::charger_status = data->charger_status;
    BatteryData::charger_received = true;
    
    Serial.printf("[CHARGER] Status=%d, HV=%.1fV/%.1fA, AC=%dV, P=%dW\n",
                 BatteryData::charger_status, BatteryData::charger_hv_voltage_V,
                 BatteryData::charger_hv_current_A, BatteryData::charger_ac_voltage_V,
                 BatteryData::charger_power_W);
    
    // TODO Phase 2: Notify SSE clients
}

void handle_inverter_status(const espnow_queue_msg_t* msg) {
    const inverter_status_msg_t* data = (inverter_status_msg_t*)msg->data;
    
    // Validate checksum
    if (!validate_checksum(data, sizeof(*data))) {
        Serial.println("[INVERTER] Invalid checksum - message rejected");
        return;
    }
    
    // Update global state
    BatteryData::inverter_ac_voltage_V = data->ac_voltage_V;
    BatteryData::inverter_ac_frequency_Hz = data->ac_frequency_dHz / 10.0;
    BatteryData::inverter_ac_current_A = data->ac_current_dA / 10.0;
    BatteryData::inverter_power_W = data->power_W;
    BatteryData::inverter_status = data->inverter_status;
    BatteryData::inverter_received = true;
    
    Serial.printf("[INVERTER] Status=%d, AC=%dV/%.1fA@%.1fHz, P=%dW\n",
                 BatteryData::inverter_status, BatteryData::inverter_ac_voltage_V,
                 BatteryData::inverter_ac_current_A, BatteryData::inverter_ac_frequency_Hz,
                 BatteryData::inverter_power_W);
    
    // TODO Phase 2: Notify SSE clients
}

void handle_system_status(const espnow_queue_msg_t* msg) {
    const system_status_msg_t* data = (system_status_msg_t*)msg->data;
    
    // Validate checksum
    if (!validate_checksum(data, sizeof(*data))) {
        Serial.println("[SYSTEM] Invalid checksum - message rejected");
        return;
    }
    
    // Update global state
    BatteryData::contactor_state = data->contactor_state;
    BatteryData::error_flags = data->error_flags;
    BatteryData::warning_flags = data->warning_flags;
    BatteryData::uptime_seconds = data->uptime_seconds;
    BatteryData::system_received = true;
    
    Serial.printf("[SYSTEM] Contactors=0x%02X, Errors=0x%02X, Warnings=0x%02X, Uptime=%us\n",
                 BatteryData::contactor_state, BatteryData::error_flags,
                 BatteryData::warning_flags, BatteryData::uptime_seconds);
    
    // TODO Phase 2: Notify SSE clients
}
