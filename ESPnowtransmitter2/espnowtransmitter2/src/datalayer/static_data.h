#ifndef STATIC_DATA_H
#define STATIC_DATA_H

#include <Arduino.h>

/**
 * @brief Static configuration data that doesn't change during runtime
 * This data is published once on startup to MQTT BE/spec_data topics
 * and can be requested by receiver via ESP-NOW
 */

namespace StaticData {

/**
 * @brief Battery type specifications
 */
struct BatterySpecs {
    const char* battery_type = "TEST_DUMMY";           // e.g., "NISSAN_LEAF", "TESLA_MODEL_3", "BMW_I3"
    const char* battery_chemistry = "NCA";             // e.g., "NCA", "NMC", "LFP", "LTO"
    uint32_t nominal_capacity_wh = 30000;              // Nominal capacity in Wh
    uint16_t max_design_voltage_dv = 5000;             // Max voltage in decivolts (500.0V)
    uint16_t min_design_voltage_dv = 2500;             // Min voltage in decivolts (250.0V)
    uint16_t max_cell_voltage_mv = 4300;               // Max cell voltage in millivolts
    uint16_t min_cell_voltage_mv = 2700;               // Min cell voltage in millivolts
    uint16_t max_cell_deviation_mv = 500;              // Max allowed cell voltage deviation
    uint8_t number_of_cells = 96;                      // Total number of cells
    uint8_t number_of_modules = 24;                    // Number of modules
    uint16_t usable_capacity_wh = 28000;               // Usable capacity (after buffer)
    bool supports_balancing = true;                     // Whether BMS supports cell balancing
    bool supports_heating = false;                      // Whether pack has heating capability
    bool supports_cooling = false;                      // Whether pack has cooling capability
};

/**
 * @brief Inverter protocol and limits
 */
struct InverterSpecs {
    const char* inverter_protocol = "PYLON_CAN";       // e.g., "PYLON_CAN", "SMA_CAN", "SOLAX_CAN"
    const char* inverter_manufacturer = "Pylon";       // Manufacturer name
    uint32_t max_charge_power_w = 5000;                // Max charge power in watts
    uint32_t max_discharge_power_w = 5000;             // Max discharge power in watts
    uint16_t max_charge_current_da = 100;              // Max charge current in deciamps
    uint16_t max_discharge_current_da = 100;           // Max discharge current in deciamps
    uint16_t nominal_voltage_dv = 4800;                // Nominal voltage in decivolts
    uint16_t ac_voltage_v = 230;                       // AC voltage (for grid-tie inverters)
    uint16_t ac_frequency_hz = 50;                     // AC frequency
    bool supports_modbus = false;                       // Whether inverter has Modbus interface
    bool supports_can = true;                           // Whether inverter has CAN interface
};

/**
 * @brief Charger specifications
 */
struct ChargerSpecs {
    const char* charger_type = "CHADEMO";              // e.g., "CHADEMO", "CCS", "AC_ONBOARD"
    const char* charger_manufacturer = "None";         // Manufacturer name
    uint32_t max_charge_power_w = 50000;               // Max charge power in watts
    uint16_t max_charge_current_da = 1250;             // Max charge current in deciamps (125A)
    uint16_t max_charge_voltage_dv = 4200;             // Max charge voltage in decivolts
    uint16_t min_charge_voltage_dv = 3000;             // Min charge voltage in decivolts
    bool supports_dc_charging = true;                   // DC fast charging support
    bool supports_ac_charging = false;                  // AC charging support
    bool supports_bidirectional = false;                // V2G/V2H capability
};

/**
 * @brief System hardware and capabilities
 */
struct SystemSpecs {
    const char* hardware_model = "ESP32-POE2";         // Hardware platform
    const char* can_interface = "MCP2515_SPI";         // CAN controller type
    const char* firmware_version = "2.0.0";            // Current firmware version
    const char* build_date = __DATE__;                 // Compilation date
    const char* build_time = __TIME__;                 // Compilation time
    uint32_t can_bitrate = 500000;                     // CAN bus speed in bps
    bool has_contactor_control = false;                 // Main contactor control
    bool has_precharge_control = false;                 // Precharge circuit control
    bool has_charger_control = false;                   // Charger enable/disable
    bool has_heating_control = false;                   // Battery heating control
    bool has_cooling_control = false;                   // Battery cooling control
    bool has_sd_logging = false;                        // SD card data logging
    bool has_ethernet = true;                           // Ethernet connectivity
    bool has_wifi = true;                               // WiFi connectivity (ESP-NOW only)
    uint8_t number_of_can_buses = 1;                   // Number of CAN interfaces
};

// Global instances (defined in static_data.cpp)
extern BatterySpecs battery_specs;
extern InverterSpecs inverter_specs;
extern ChargerSpecs charger_specs;
extern SystemSpecs system_specs;

/**
 * @brief Initialize static data from NVS or defaults
 */
void init();

/**
 * @brief Update battery specs based on selected battery type and current datalayer values
 * @param battery_type Battery profile type enum value
 */
void update_battery_specs(uint8_t battery_type);

/**
 * @brief Update inverter specs based on selected inverter type
 * @param inverter_type Inverter protocol enum value
 */
void update_inverter_specs(uint8_t inverter_type);

/**
 * @brief Serialize battery specs to JSON string
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @return Number of bytes written
 */
size_t serialize_battery_specs(char* buffer, size_t buffer_size);

/**
 * @brief Serialize cell data to JSON string (voltages and balancing status)
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @return Number of bytes written
 */
size_t serialize_cell_data(char* buffer, size_t buffer_size);

/**
 * @brief Serialize inverter specs to JSON string
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @return Number of bytes written
 */
size_t serialize_inverter_specs(char* buffer, size_t buffer_size);

/**
 * @brief Serialize charger specs to JSON string
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @return Number of bytes written
 */
size_t serialize_charger_specs(char* buffer, size_t buffer_size);

/**
 * @brief Serialize system specs to JSON string
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @return Number of bytes written
 */
size_t serialize_system_specs(char* buffer, size_t buffer_size);

/**
 * @brief Serialize all specs into combined JSON (for BE/spec_data topic)
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @return Number of bytes written
 */
size_t serialize_all_specs(char* buffer, size_t buffer_size);

} // namespace StaticData

#endif // STATIC_DATA_H
