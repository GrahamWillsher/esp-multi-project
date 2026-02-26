#include "static_data.h"
#include "../config/logging_config.h"
#include "../battery_emulator/battery/Battery.h"
#include "../battery_emulator/inverter/InverterProtocol.h"
#include "../battery_emulator/datalayer/datalayer.h"
#include "../test_data/test_data_config.h"
#include <Arduino.h>
#include <ArduinoJson.h>

namespace StaticData {

// Global instances with default values
BatterySpecs battery_specs;
InverterSpecs inverter_specs;
ChargerSpecs charger_specs;
SystemSpecs system_specs;

void init() {
    LOG_INFO("STATIC_DATA", "Initializing static configuration data");
    
    // TODO: Load from NVS if user has customized these values
    // For now, using compile-time defaults
    
    LOG_INFO("STATIC_DATA", "Battery: %s (%s), %u Wh, %u cells",
             battery_specs.battery_type,
             battery_specs.battery_chemistry,
             battery_specs.nominal_capacity_wh,
             battery_specs.number_of_cells);
    
    LOG_INFO("STATIC_DATA", "Inverter: %s (%s), %u W charge, %u W discharge",
             inverter_specs.inverter_protocol,
             inverter_specs.inverter_manufacturer,
             inverter_specs.max_charge_power_w,
             inverter_specs.max_discharge_power_w);
    
    LOG_INFO("STATIC_DATA", "Charger: %s, %u W max",
             charger_specs.charger_type,
             charger_specs.max_charge_power_w);
    
    LOG_INFO("STATIC_DATA", "System: %s, CAN: %s @ %u bps",
             system_specs.hardware_model,
             system_specs.can_interface,
             system_specs.can_bitrate);
}

void update_battery_specs(uint8_t battery_type) {
    const char* battery_name = name_for_battery_type(static_cast<BatteryType>(battery_type));
    if (battery_name != nullptr) {
        battery_specs.battery_type = battery_name;
    }

#if CONFIG_CAN_ENABLED
    const char* chemistry_name = name_for_chemistry(datalayer.battery.info.chemistry);
    if (chemistry_name != nullptr) {
        battery_specs.battery_chemistry = chemistry_name;
    }
    if (datalayer.battery.info.total_capacity_Wh != 0) {
        battery_specs.nominal_capacity_wh = datalayer.battery.info.total_capacity_Wh;
    }
    if (datalayer.battery.info.reported_total_capacity_Wh != 0) {
        battery_specs.usable_capacity_wh = datalayer.battery.info.reported_total_capacity_Wh;
    }
    if (datalayer.battery.info.max_design_voltage_dV != 0) {
        battery_specs.max_design_voltage_dv = datalayer.battery.info.max_design_voltage_dV;
    }
    if (datalayer.battery.info.min_design_voltage_dV != 0) {
        battery_specs.min_design_voltage_dv = datalayer.battery.info.min_design_voltage_dV;
    }
    if (datalayer.battery.info.max_cell_voltage_mV != 0) {
        battery_specs.max_cell_voltage_mv = datalayer.battery.info.max_cell_voltage_mV;
    }
    if (datalayer.battery.info.min_cell_voltage_mV != 0) {
        battery_specs.min_cell_voltage_mv = datalayer.battery.info.min_cell_voltage_mV;
    }
    if (datalayer.battery.info.max_cell_voltage_deviation_mV != 0) {
        battery_specs.max_cell_deviation_mv = datalayer.battery.info.max_cell_voltage_deviation_mV;
    }
    // Always update number_of_cells from datalayer (set by battery setup())
    // Nissan Leaf and other batteries set this to 96 in their setup() function
    // This ALWAYS takes precedence over the default value in battery_specs
    uint8_t old_count = battery_specs.number_of_cells;
    if (datalayer.battery.info.number_of_cells != 0) {
        battery_specs.number_of_cells = datalayer.battery.info.number_of_cells;
        if (old_count != battery_specs.number_of_cells) {
            LOG_INFO("STATIC_DATA", "Updated number_of_cells from datalayer: %u -> %u",
                     old_count, battery_specs.number_of_cells);
        }
    } else {
        // If battery hasn't set it yet, use the default from battery specs
        LOG_WARN("STATIC_DATA", "Battery number_of_cells not set yet, keeping default: %u", battery_specs.number_of_cells);
    }
#endif

    LOG_INFO("STATIC_DATA", "Updated battery specs: %s (%s), %u Wh, %u cells",
             battery_specs.battery_type,
             battery_specs.battery_chemistry,
             battery_specs.nominal_capacity_wh,
             battery_specs.number_of_cells);
}

void update_inverter_specs(uint8_t inverter_type) {
    const char* inverter_name = name_for_inverter_type(static_cast<InverterProtocolType>(inverter_type));
    if (inverter_name != nullptr) {
        inverter_specs.inverter_protocol = inverter_name;
    }
    LOG_INFO("STATIC_DATA", "Updated inverter specs: %s",
             inverter_specs.inverter_protocol);
}

const BatterySpecs& get_battery_specs() {
    return battery_specs;
}

size_t serialize_battery_specs(char* buffer, size_t buffer_size) {
    // Use PSRAM for JSON document to avoid stack overflow
    DynamicJsonDocument doc(512);
    
    doc["battery_type"] = battery_specs.battery_type;
    doc["battery_chemistry"] = battery_specs.battery_chemistry;
    doc["nominal_capacity_wh"] = battery_specs.nominal_capacity_wh;
    doc["usable_capacity_wh"] = battery_specs.usable_capacity_wh;
    doc["max_design_voltage"] = battery_specs.max_design_voltage_dv / 10.0;
    doc["min_design_voltage"] = battery_specs.min_design_voltage_dv / 10.0;
    doc["max_cell_voltage"] = battery_specs.max_cell_voltage_mv / 1000.0;
    doc["min_cell_voltage"] = battery_specs.min_cell_voltage_mv / 1000.0;
    doc["max_cell_deviation"] = battery_specs.max_cell_deviation_mv / 1000.0;
    doc["number_of_cells"] = battery_specs.number_of_cells;
    doc["number_of_modules"] = battery_specs.number_of_modules;
    doc["supports_balancing"] = battery_specs.supports_balancing;
    doc["supports_heating"] = battery_specs.supports_heating;
    doc["supports_cooling"] = battery_specs.supports_cooling;
    
    return serializeJson(doc, buffer, buffer_size);
}

size_t serialize_cell_data(char* buffer, size_t buffer_size) {
    // Use PSRAM for JSON document (needs 6KB for 96 cells + balancing + metadata)
    DynamicJsonDocument doc(6144);
    
    // Get cell count from datalayer
    uint16_t cell_count = datalayer.battery.info.number_of_cells;
    
    // DEBUG: Log what we're getting
    Serial.printf("[SERIALIZE_DEBUG] cell_count from datalayer: %u\n", cell_count);
    Serial.printf("[SERIALIZE_DEBUG] cell_voltages_mV[0]: %u\n", datalayer.battery.status.cell_voltages_mV[0]);
    Serial.printf("[SERIALIZE_DEBUG] cell_voltages_mV[95]: %u\n", datalayer.battery.status.cell_voltages_mV[95]);
    Serial.printf("[SERIALIZE_DEBUG] cell_voltages_mV[107]: %u\n", datalayer.battery.status.cell_voltages_mV[107]);
    
    // Check if we have valid cell data (at least one non-zero voltage)
    bool has_real_data = false;
    for (uint16_t i = 0; i < cell_count && i < MAX_AMOUNT_CELLS; i++) {
        if (datalayer.battery.status.cell_voltages_mV[i] > 0) {
            has_real_data = true;
            break;
        }
    }
    
    Serial.printf("[SERIALIZE_DEBUG] has_real_data: %s\n", has_real_data ? "true" : "false");
    
    // Check if test mode is enabled - this controls data path selection
    bool test_mode_active = TestDataConfig::is_enabled();
    
    // ═══════════════════════════════════════════════════════════════════════
    // PATH 1: TEST MODE - Always generate dummy data when test mode is ON
    // ═══════════════════════════════════════════════════════════════════════
    if (test_mode_active && cell_count > 0) {
        const char* data_source = "dummy";
        Serial.printf("[SERIALIZE_DEBUG] Test mode ACTIVE - generating dummy data\n");
        // Generate realistic dummy voltages (3750-3900 mV for ~3.85V average)
        static uint16_t dummy_voltages[MAX_AMOUNT_CELLS] = {0};
        static bool dummy_balancing[MAX_AMOUNT_CELLS] = {false};
        static unsigned long last_dummy_update = 0;
        unsigned long now = millis();
        
        // Update dummy data every 5 seconds
        if (now - last_dummy_update > 5000 || last_dummy_update == 0) {
            last_dummy_update = now;
            
            for (uint16_t i = 0; i < cell_count && i < MAX_AMOUNT_CELLS; i++) {
                // Generate voltage with slight variation (3750-3900 mV)
                dummy_voltages[i] = 3750 + (rand() % 150);
                
                // Randomly activate balancing on 10% of cells
                dummy_balancing[i] = (rand() % 10) == 0;
            }
            
            // Make a few cells stand out as min/max
            if (cell_count > 0) {
                dummy_voltages[0] = 3740;  // Slightly lower (will be min)
                dummy_voltages[cell_count - 1] = 3920;  // Slightly higher (will be max)
            }
        }
        
        doc["number_of_cells"] = cell_count;
        
        // Add dummy voltages
        JsonArray voltages = doc.createNestedArray("cell_voltages_mV");
        for (uint16_t i = 0; i < cell_count && i < MAX_AMOUNT_CELLS; i++) {
            voltages.add(dummy_voltages[i]);
        }
        
        // Add dummy balancing status
        JsonArray balancing = doc.createNestedArray("cell_balancing_status");
        for (uint16_t i = 0; i < cell_count && i < MAX_AMOUNT_CELLS; i++) {
            balancing.add(dummy_balancing[i]);
        }
        
        // Find min/max from dummy data
        uint16_t min_voltage = 5000;
        uint16_t max_voltage = 0;
        bool balancing_active = false;
        
        for (uint16_t i = 0; i < cell_count && i < MAX_AMOUNT_CELLS; i++) {
            if (dummy_voltages[i] < min_voltage) min_voltage = dummy_voltages[i];
            if (dummy_voltages[i] > max_voltage) max_voltage = dummy_voltages[i];
            if (dummy_balancing[i]) balancing_active = true;
        }
        
        doc["cell_min_voltage_mV"] = min_voltage;
        doc["cell_max_voltage_mV"] = max_voltage;
        doc["balancing_active"] = balancing_active;
        doc["data_source"] = data_source;
        
        return serializeJson(doc, buffer, buffer_size);
    }
    
    // ═══════════════════════════════════════════════════════════════════════
    // PATH 2: REAL DATA - Use actual datalayer values
    // ═══════════════════════════════════════════════════════════════════════
    
    Serial.printf("[SERIALIZE_DEBUG] Test mode OFF - using real data\n");
    
    // Determine data source tag based on data availability and freshness
    const char* data_source = "live";
    
    if (!has_real_data) {
        // No valid cell data available
        data_source = "live_simulated";
        Serial.printf("[SERIALIZE_DEBUG] No real data available - tagged as live_simulated\n");
    } else {
        // Check CAN data freshness using CAN_battery_still_alive counter
        // Counter starts at 60 and decrements every second when no CAN messages received
        const uint8_t CAN_STALE_THRESHOLD = 55;  // Consider stale if < 55 (5+ seconds old)
        
        if (datalayer.battery.status.CAN_battery_still_alive < CAN_STALE_THRESHOLD) {
            // CAN data is stale - this is simulated
            data_source = "live_simulated";
            Serial.printf("[SERIALIZE_DEBUG] CAN data stale (counter=%u) - tagged as live_simulated\n",
                         datalayer.battery.status.CAN_battery_still_alive);
        } else {
            // Fresh CAN data
            data_source = "live";
            Serial.printf("[SERIALIZE_DEBUG] CAN data fresh (counter=%u) - tagged as live\n",
                         datalayer.battery.status.CAN_battery_still_alive);
        }
    }

    doc["number_of_cells"] = cell_count;
    
    // Add cell voltages array (only include valid cells)
    JsonArray voltages = doc.createNestedArray("cell_voltages_mV");
    for (uint16_t i = 0; i < cell_count && i < MAX_AMOUNT_CELLS; i++) {
        voltages.add(datalayer.battery.status.cell_voltages_mV[i]);
    }
    
    // Add cell balancing status array
    JsonArray balancing = doc.createNestedArray("cell_balancing_status");
    for (uint16_t i = 0; i < cell_count && i < MAX_AMOUNT_CELLS; i++) {
        balancing.add(datalayer.battery.status.cell_balancing_status[i]);
    }
    
    // Find min/max cell voltages
    uint16_t min_voltage = 5000;  // Start high
    uint16_t max_voltage = 0;     // Start low
    for (uint16_t i = 0; i < cell_count && i < MAX_AMOUNT_CELLS; i++) {
        uint16_t voltage = datalayer.battery.status.cell_voltages_mV[i];
        if (voltage > 0) {  // Only consider non-zero voltages
            if (voltage < min_voltage) min_voltage = voltage;
            if (voltage > max_voltage) max_voltage = voltage;
        }
    }
    
    doc["cell_min_voltage_mV"] = (min_voltage < 5000) ? min_voltage : 0;
    doc["cell_max_voltage_mV"] = max_voltage;
    
    // Check if any balancing is active
    bool balancing_active = false;
    for (uint16_t i = 0; i < cell_count && i < MAX_AMOUNT_CELLS; i++) {
        if (datalayer.battery.status.cell_balancing_status[i]) {
            balancing_active = true;
            break;
        }
    }
    doc["balancing_active"] = balancing_active;
    doc["data_source"] = data_source;
    
    // Debug: Log what we're serializing
    Serial.printf("[SERIALIZE_DEBUG] About to serialize with data_source='%s'\n", data_source);
    Serial.printf("[SERIALIZE_DEBUG] JSON document capacity: %u bytes, memory usage: %u bytes\n", 
                 doc.capacity(), doc.memoryUsage());
    
    size_t result = serializeJson(doc, buffer, buffer_size);
    
    // Debug: Check for truncation
    if (result >= buffer_size - 1) {
        Serial.printf("[SERIALIZE_WARNING] JSON truncated! Result=%u, Buffer=%u\n", result, buffer_size);
    }
    
    // Debug: Log the serialized JSON (first/last 200 chars)
    Serial.printf("[SERIALIZE_DEBUG] Serialized %u bytes (first 200): %.200s\n", result, buffer);
    if (result > 200) {
        Serial.printf("[SERIALIZE_DEBUG] Last 100 chars: %s\n", buffer + result - 100);
    }
    
    return result;
}

size_t serialize_inverter_specs(char* buffer, size_t buffer_size) {
    // Use PSRAM for JSON document to avoid stack overflow
    DynamicJsonDocument doc(512);
    
    doc["inverter_protocol"] = inverter_specs.inverter_protocol;
    doc["inverter_manufacturer"] = inverter_specs.inverter_manufacturer;
    doc["max_charge_power_w"] = inverter_specs.max_charge_power_w;
    doc["max_discharge_power_w"] = inverter_specs.max_discharge_power_w;
    doc["max_charge_current"] = inverter_specs.max_charge_current_da / 10.0;
    doc["max_discharge_current"] = inverter_specs.max_discharge_current_da / 10.0;
    doc["nominal_voltage"] = inverter_specs.nominal_voltage_dv / 10.0;
    doc["ac_voltage"] = inverter_specs.ac_voltage_v;
    doc["ac_frequency"] = inverter_specs.ac_frequency_hz;
    doc["supports_modbus"] = inverter_specs.supports_modbus;
    doc["supports_can"] = inverter_specs.supports_can;
    
    return serializeJson(doc, buffer, buffer_size);
}

size_t serialize_charger_specs(char* buffer, size_t buffer_size) {
    // Use PSRAM for JSON document to avoid stack overflow
    DynamicJsonDocument doc(512);
    
    doc["charger_type"] = charger_specs.charger_type;
    doc["charger_manufacturer"] = charger_specs.charger_manufacturer;
    doc["max_charge_power_w"] = charger_specs.max_charge_power_w;
    doc["max_charge_current"] = charger_specs.max_charge_current_da / 10.0;
    doc["max_charge_voltage"] = charger_specs.max_charge_voltage_dv / 10.0;
    doc["min_charge_voltage"] = charger_specs.min_charge_voltage_dv / 10.0;
    doc["supports_dc_charging"] = charger_specs.supports_dc_charging;
    doc["supports_ac_charging"] = charger_specs.supports_ac_charging;
    doc["supports_bidirectional"] = charger_specs.supports_bidirectional;
    
    return serializeJson(doc, buffer, buffer_size);
}

size_t serialize_system_specs(char* buffer, size_t buffer_size) {
    // Use PSRAM for JSON document to avoid stack overflow
    DynamicJsonDocument doc(512);
    
    doc["hardware_model"] = system_specs.hardware_model;
    doc["can_interface"] = system_specs.can_interface;
    doc["firmware_version"] = system_specs.firmware_version;
    doc["build_date"] = system_specs.build_date;
    doc["build_time"] = system_specs.build_time;
    doc["can_bitrate"] = system_specs.can_bitrate;
    doc["has_contactor_control"] = system_specs.has_contactor_control;
    doc["has_precharge_control"] = system_specs.has_precharge_control;
    doc["has_charger_control"] = system_specs.has_charger_control;
    doc["has_heating_control"] = system_specs.has_heating_control;
    doc["has_cooling_control"] = system_specs.has_cooling_control;
    doc["has_sd_logging"] = system_specs.has_sd_logging;
    doc["has_ethernet"] = system_specs.has_ethernet;
    doc["has_wifi"] = system_specs.has_wifi;
    doc["number_of_can_buses"] = system_specs.number_of_can_buses;
    
    return serializeJson(doc, buffer, buffer_size);
}

size_t serialize_all_specs(char* buffer, size_t buffer_size) {
    // Use PSRAM for large JSON document (2KB would overflow stack)
    DynamicJsonDocument doc(2048);
    
    // Battery specs
    JsonObject battery = doc.createNestedObject("battery");
    battery["type"] = battery_specs.battery_type;
    battery["chemistry"] = battery_specs.battery_chemistry;
    battery["nominal_capacity_wh"] = battery_specs.nominal_capacity_wh;
    battery["usable_capacity_wh"] = battery_specs.usable_capacity_wh;
    battery["max_voltage"] = battery_specs.max_design_voltage_dv / 10.0;
    battery["min_voltage"] = battery_specs.min_design_voltage_dv / 10.0;
    battery["number_of_cells"] = battery_specs.number_of_cells;
    
    // Inverter specs
    JsonObject inverter = doc.createNestedObject("inverter");
    inverter["protocol"] = inverter_specs.inverter_protocol;
    inverter["manufacturer"] = inverter_specs.inverter_manufacturer;
    inverter["max_charge_power_w"] = inverter_specs.max_charge_power_w;
    inverter["max_discharge_power_w"] = inverter_specs.max_discharge_power_w;
    
    // Charger specs
    JsonObject charger = doc.createNestedObject("charger");
    charger["type"] = charger_specs.charger_type;
    charger["max_charge_power_w"] = charger_specs.max_charge_power_w;
    
    // System specs
    JsonObject system = doc.createNestedObject("system");
    system["hardware"] = system_specs.hardware_model;
    system["firmware_version"] = system_specs.firmware_version;
    system["can_bitrate"] = system_specs.can_bitrate;
    
    return serializeJson(doc, buffer, buffer_size);
}

} // namespace StaticData
