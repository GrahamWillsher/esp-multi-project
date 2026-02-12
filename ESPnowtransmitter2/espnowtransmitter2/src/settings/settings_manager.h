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
    
    // Version getters
    uint32_t get_battery_settings_version() const { return battery_settings_version_; }
    
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
    
    // Version tracking
    uint32_t battery_settings_version_{0};             // Incremented on any change
    
    bool initialized_{false};
};
