#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <espnow_common.h>
#include <version_utils.h>

/**
 * @brief Settings Manager - Handles settings storage and ESP-NOW updates
 * 
 * Manages all persistent settings in NVS, handles settings update messages
 * from the receiver, and sends acknowledgments.
 * 
 * Settings are organized by category (battery, charger, inverter, system, etc.)
 * and stored in separate NVS namespaces for organization.
 */
class SettingsManager {
public:
    static SettingsManager& instance();
    
    /**
     * @brief Initialize settings manager and load from NVS
     * @return true if settings loaded successfully
     */
    bool init();
    
    /**
     * @brief Handle settings update message from receiver
     * @param msg ESP-NOW message containing settings update
     */
    void handle_settings_update(const espnow_queue_msg_t& msg);
    
    /**
     * @brief Save a battery setting to NVS
     * @param field_id Battery setting field ID
     * @param value_uint32 Integer value (if applicable)
     * @param value_float Float value (if applicable)
     * @param value_string String value (if applicable)
     * @return true if saved successfully
     */
    bool save_battery_setting(uint8_t field_id, uint32_t value_uint32, float value_float, const char* value_string);
    
    /**
     * @brief Load all settings from NVS
     * @return true if loaded successfully
     */
    bool load_all_settings();
    
    /**
     * @brief Restore all settings to factory defaults
     */
    void restore_defaults();
    
    // Battery settings getters
    uint32_t get_battery_capacity_wh() const { return battery_capacity_wh_; }
    uint32_t get_battery_max_voltage_mv() const { return battery_max_voltage_mv_; }
    uint32_t get_battery_min_voltage_mv() const { return battery_min_voltage_mv_; }
    float get_battery_max_charge_current_a() const { return battery_max_charge_current_a_; }
    float get_battery_max_discharge_current_a() const { return battery_max_discharge_current_a_; }
    uint8_t get_battery_soc_high_limit() const { return battery_soc_high_limit_; }
    uint8_t get_battery_soc_low_limit() const { return battery_soc_low_limit_; }
    uint8_t get_battery_cell_count() const { return battery_cell_count_; }
    uint8_t get_battery_chemistry() const { return battery_chemistry_; }
    bool get_battery_double_enabled() const { return battery_double_enabled_; }
    uint16_t get_battery_pack_max_voltage_dV() const { return battery_pack_max_voltage_dV_; }
    uint16_t get_battery_pack_min_voltage_dV() const { return battery_pack_min_voltage_dV_; }
    uint16_t get_battery_cell_max_voltage_mV() const { return battery_cell_max_voltage_mV_; }
    uint16_t get_battery_cell_min_voltage_mV() const { return battery_cell_min_voltage_mV_; }
    bool get_battery_soc_estimated() const { return battery_soc_estimated_; }

    // Power settings getters
    uint16_t get_power_charge_w() const { return power_charge_w_; }
    uint16_t get_power_discharge_w() const { return power_discharge_w_; }
    uint16_t get_power_max_precharge_ms() const { return power_max_precharge_ms_; }
    uint16_t get_power_precharge_duration_ms() const { return power_precharge_duration_ms_; }

    // Inverter settings getters
    uint8_t get_inverter_cells() const { return inverter_cells_; }
    uint8_t get_inverter_modules() const { return inverter_modules_; }
    uint8_t get_inverter_cells_per_module() const { return inverter_cells_per_module_; }
    uint16_t get_inverter_voltage_level() const { return inverter_voltage_level_; }
    uint16_t get_inverter_capacity_ah() const { return inverter_capacity_ah_; }
    uint8_t get_inverter_battery_type() const { return inverter_battery_type_; }

    // CAN settings getters
    uint16_t get_can_frequency_khz() const { return can_frequency_khz_; }
    uint16_t get_can_fd_frequency_mhz() const { return can_fd_frequency_mhz_; }
    uint16_t get_can_sofar_id() const { return can_sofar_id_; }
    uint16_t get_can_pylon_send_interval_ms() const { return can_pylon_send_interval_ms_; }

    // Contactor settings getters
    bool get_contactor_control_enabled() const { return contactor_control_enabled_; }
    bool get_contactor_nc_mode() const { return contactor_nc_mode_; }
    uint16_t get_contactor_pwm_frequency_hz() const { return contactor_pwm_frequency_hz_; }
    
    // Version getters
    uint32_t get_battery_settings_version() const { return battery_settings_version_; }
    uint32_t get_power_settings_version() const { return power_settings_version_; }
    uint32_t get_inverter_settings_version() const { return inverter_settings_version_; }
    uint32_t get_can_settings_version() const { return can_settings_version_; }
    uint32_t get_contactor_settings_version() const { return contactor_settings_version_; }
    
    // Initialization status
    bool is_initialized() const { return initialized_; }
    
private:
    SettingsManager();
    ~SettingsManager() = default;
    
    // Prevent copying
    SettingsManager(const SettingsManager&) = delete;
    SettingsManager& operator=(const SettingsManager&) = delete;
    
    /**
     * @brief Send settings update acknowledgment
     * @param mac Receiver MAC address
     * @param category Settings category
     * @param field_id Field ID
     * @param success Success status
     * @param new_version New version number
     * @param error_msg Error message (if any)
     */
    void send_settings_ack(const uint8_t* mac, uint8_t category, uint8_t field_id, bool success, uint32_t new_version, const char* error_msg);
    
    /**
     * @brief Send settings changed notification to receiver
     * @param category Settings category that changed
     * @param new_version New version number
     */
    void send_settings_changed_notification(uint8_t category, uint32_t new_version);
    
    /**
     * @brief Increment battery settings version and save to NVS
     */
    void increment_battery_version();
    
    /**
     * @brief Load battery settings from NVS
     * @return true if loaded successfully
     */
    bool load_battery_settings();
    
    /**
     * @brief Save battery settings to NVS
     * @return true if saved successfully
     */
    bool save_battery_settings();

    bool save_power_setting(uint8_t field_id, uint32_t value_uint32);
    bool save_inverter_setting(uint8_t field_id, uint32_t value_uint32);
    bool save_can_setting(uint8_t field_id, uint32_t value_uint32);
    bool save_contactor_setting(uint8_t field_id, uint32_t value_uint32);

    bool load_power_settings();
    bool load_inverter_settings();
    bool load_can_settings();
    bool load_contactor_settings();

    bool save_power_settings();
    bool save_inverter_settings();
    bool save_can_settings();
    bool save_contactor_settings();

    void increment_power_version();
    void increment_inverter_version();
    void increment_can_version();
    void increment_contactor_version();
    
    // Battery settings storage
    uint32_t battery_capacity_wh_{30000};              // 30kWh default
    uint32_t battery_max_voltage_mv_{58000};           // 58V default
    uint32_t battery_min_voltage_mv_{46000};           // 46V default
    float battery_max_charge_current_a_{100.0f};       // 100A default
    float battery_max_discharge_current_a_{100.0f};    // 100A default
    uint8_t battery_soc_high_limit_{95};               // 95% default
    uint8_t battery_soc_low_limit_{20};                // 20% default
    uint8_t battery_cell_count_{16};                   // 16S default
    uint8_t battery_chemistry_{2};                     // LFP default
    bool battery_double_enabled_{false};               // Double battery disabled
    uint16_t battery_pack_max_voltage_dV_{580};        // 58.0V default
    uint16_t battery_pack_min_voltage_dV_{460};        // 46.0V default
    uint16_t battery_cell_max_voltage_mV_{4200};       // 4.2V default
    uint16_t battery_cell_min_voltage_mV_{3000};       // 3.0V default
    bool battery_soc_estimated_{false};                // SOC estimation disabled

    // Power settings storage
    uint16_t power_charge_w_{3000};                    // 3kW default
    uint16_t power_discharge_w_{3000};                 // 3kW default
    uint16_t power_max_precharge_ms_{15000};           // 15s default
    uint16_t power_precharge_duration_ms_{100};        // 100ms default

    // Inverter settings storage
    uint8_t inverter_cells_{0};
    uint8_t inverter_modules_{0};
    uint8_t inverter_cells_per_module_{0};
    uint16_t inverter_voltage_level_{0};
    uint16_t inverter_capacity_ah_{0};
    uint8_t inverter_battery_type_{0};

    // CAN settings storage
    uint16_t can_frequency_khz_{8};
    uint16_t can_fd_frequency_mhz_{40};
    uint16_t can_sofar_id_{0};
    uint16_t can_pylon_send_interval_ms_{0};

    // Contactor settings storage
    bool contactor_control_enabled_{false};
    bool contactor_nc_mode_{false};
    uint16_t contactor_pwm_frequency_hz_{20000};
    
    // Version tracking
    uint32_t battery_settings_version_{0};             // Incremented on any change
    uint32_t power_settings_version_{0};
    uint32_t inverter_settings_version_{0};
    uint32_t can_settings_version_{0};
    uint32_t contactor_settings_version_{0};
    
    bool initialized_{false};
};
