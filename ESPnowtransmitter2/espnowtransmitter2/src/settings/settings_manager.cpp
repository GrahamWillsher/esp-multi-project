#include "settings_manager.h"
#include "../config/logging_config.h"
#include "../datalayer/datalayer.h"
#include "../battery_emulator/communication/can/comm_can.h"
#include "../battery_emulator/communication/contactorcontrol/comm_contactorcontrol.h"
#include "../battery_emulator/communication/precharge_control/precharge_control.h"
// Companion translation units:
//   settings_persistence.cpp  – NVS blob save/load (all categories)
//   settings_field_setters.cpp – per-field value validation and dispatch
//   settings_espnow.cpp       – handle_settings_update / send_settings_ack / send_settings_changed_notification

SettingsManager& SettingsManager::instance() {
    static SettingsManager instance;
    return instance;
}

SettingsManager::SettingsManager() {
}

void SettingsManager::apply_runtime_static_settings() {
    contactor_control_enabled = contactor_control_enabled_;
    contactor_control_inverted_logic = contactor_nc_mode_;
    precharge_time_ms = power_precharge_duration_ms_;
    pwm_contactor_control = contactor_pwm_control_enabled_;
    pwm_frequency = contactor_pwm_frequency_hz_;
    pwm_hold_duty = contactor_pwm_hold_duty_;
    periodic_bms_reset = contactor_periodic_bms_reset_;
    bms_first_align_enabled = contactor_bms_first_align_enabled_;
    bms_first_align_target_minutes = contactor_bms_first_align_target_minutes_;

    use_canfd_as_can = can_use_canfd_as_classic_;

    precharge_control_enabled = power_external_precharge_enabled_;
    precharge_inverter_normally_open_contactor = power_no_inverter_disconnect_contactor_;
    precharge_max_precharge_time_before_fault = power_max_precharge_ms_;
}

SettingsManager::ValidationResult SettingsManager::validate_battery_settings() const {
    if (battery_capacity_wh_ < 1000 || battery_capacity_wh_ > 1000000) {
        ValidationResult result;
        result.is_valid = false;
        result.error_message = "Battery capacity out of range";
        return result;
    }
    if (battery_min_voltage_mv_ >= battery_max_voltage_mv_) {
        ValidationResult result;
        result.is_valid = false;
        result.error_message = "Battery min voltage must be less than max voltage";
        return result;
    }
    if (battery_cell_min_voltage_mV_ >= battery_cell_max_voltage_mV_) {
        ValidationResult result;
        result.is_valid = false;
        result.error_message = "Cell min voltage must be less than cell max voltage";
        return result;
    }
    if (battery_soc_low_limit_ > battery_soc_high_limit_) {
        ValidationResult result;
        result.is_valid = false;
        result.error_message = "SOC low limit must be less than or equal to SOC high limit";
        return result;
    }
    if (battery_led_mode_ > 2) {
        ValidationResult result;
        result.is_valid = false;
        result.error_message = "LED mode out of range";
        return result;
    }
    return ValidationResult{};
}

SettingsManager::ValidationResult SettingsManager::validate_power_settings() const {
    if (power_charge_w_ > 50000 || power_discharge_w_ > 50000) {
        ValidationResult result;
        result.is_valid = false;
        result.error_message = "Power settings out of allowed range";
        return result;
    }
    if (power_precharge_duration_ms_ > power_max_precharge_ms_) {
        ValidationResult result;
        result.is_valid = false;
        result.error_message = "Precharge duration cannot exceed max precharge duration";
        return result;
    }
    if (power_equipment_stop_type_ > 2) {
        ValidationResult result;
        result.is_valid = false;
        result.error_message = "Equipment stop type out of range (0-2)";
        return result;
    }
    return ValidationResult{};
}

SettingsManager::ValidationResult SettingsManager::validate_inverter_settings() const {
    if (inverter_cells_ > 32 || inverter_modules_ > 32 || inverter_cells_per_module_ > 32) {
        ValidationResult result;
        result.is_valid = false;
        result.error_message = "Inverter cell/module settings out of range";
        return result;
    }
    return ValidationResult{};
}

SettingsManager::ValidationResult SettingsManager::validate_can_settings() const {
    if (can_frequency_khz_ == 0 || can_frequency_khz_ > 1000) {
        ValidationResult result;
        result.is_valid = false;
        result.error_message = "CAN frequency out of range";
        return result;
    }
    if (can_fd_frequency_mhz_ == 0 || can_fd_frequency_mhz_ > 100) {
        ValidationResult result;
        result.is_valid = false;
        result.error_message = "CAN-FD frequency out of range";
        return result;
    }
    return ValidationResult{};
}

SettingsManager::ValidationResult SettingsManager::validate_contactor_settings() const {
    if (contactor_pwm_frequency_hz_ < 100 || contactor_pwm_frequency_hz_ > 50000) {
        ValidationResult result;
        result.is_valid = false;
        result.error_message = "Contactor PWM frequency out of range";
        return result;
    }
    if (contactor_pwm_hold_duty_ < 1 || contactor_pwm_hold_duty_ > 1023) {
        ValidationResult result;
        result.is_valid = false;
        result.error_message = "Contactor PWM hold duty out of range (1-1023)";
        return result;
    }
    if (contactor_bms_first_align_target_minutes_ > 1439) {
        ValidationResult result;
        result.is_valid = false;
        result.error_message = "BMS first-align target minutes out of range (0-1439)";
        return result;
    }
    return ValidationResult{};
}

SettingsManager::ValidationResult SettingsManager::validate_all_settings() const {
    ValidationResult result = validate_battery_settings();
    if (!result.is_valid) return result;

    result = validate_power_settings();
    if (!result.is_valid) return result;

    result = validate_inverter_settings();
    if (!result.is_valid) return result;

    result = validate_can_settings();
    if (!result.is_valid) return result;

    return validate_contactor_settings();
}

bool SettingsManager::init() {
    if (initialized_) {
        LOG_WARN("SETTINGS", "Already initialized");
        return true;
    }
    
    LOG_INFO("SETTINGS", "Initializing settings manager...");
    
    // Load all settings from NVS
    // Note: load_battery_settings() returns false on first boot (namespace doesn't exist)
    // but still loads default values. We only call restore_defaults() explicitly if needed
    // to initialize the NVS namespace.
    bool loaded = load_all_settings();
    if (!loaded) {
        // First boot - NVS namespace doesn't exist yet
        // Settings already have default values from load function
        // Just save them to NVS to create the namespace
        LOG_INFO("SETTINGS", "First boot - initializing NVS with defaults");
        save_battery_settings();
        save_power_settings();
        save_inverter_settings();
        save_can_settings();
        save_contactor_settings();
    }
    
    initialized_ = true;

    // Keep runtime LED mode aligned with authoritative managed settings.
    datalayer.battery.status.led_mode = static_cast<led_mode_enum>(battery_led_mode_);
    apply_runtime_static_settings();

    LOG_INFO("SETTINGS", "Settings manager initialized");
    return true;
}

bool SettingsManager::load_all_settings() {
    bool success = true;
    
    // Load battery settings
    if (!load_battery_settings()) {
        LOG_WARN("SETTINGS", "Failed to load battery settings");
        success = false;
    }
    
    if (!load_power_settings()) {
        LOG_WARN("SETTINGS", "Failed to load power settings");
        success = false;
    }

    if (!load_inverter_settings()) {
        LOG_WARN("SETTINGS", "Failed to load inverter settings");
        success = false;
    }

    if (!load_can_settings()) {
        LOG_WARN("SETTINGS", "Failed to load CAN settings");
        success = false;
    }

    if (!load_contactor_settings()) {
        LOG_WARN("SETTINGS", "Failed to load contactor settings");
        success = false;
    }

    last_validation_ = validate_all_settings();
    if (!last_validation_.is_valid) {
        LOG_ERROR("SETTINGS", "Settings validation failed after load: %s", last_validation_.error_message.c_str());
        success = false;
    }
    
    return success;
}

void SettingsManager::increment_power_version() {
    VersionUtils::increment_version(power_settings_version_);
}

void SettingsManager::increment_inverter_version() {
    VersionUtils::increment_version(inverter_settings_version_);
}

void SettingsManager::increment_can_version() {
    VersionUtils::increment_version(can_settings_version_);
}

void SettingsManager::increment_contactor_version() {
    VersionUtils::increment_version(contactor_settings_version_);
}

void SettingsManager::restore_defaults() {
    LOG_INFO("SETTINGS", "Restoring factory defaults...");
    
    // Battery defaults
    battery_capacity_wh_ = 30000;
    battery_max_voltage_mv_ = 58000;
    battery_min_voltage_mv_ = 46000;
    battery_max_charge_current_a_ = 100.0f;
    battery_max_discharge_current_a_ = 100.0f;
    battery_soc_high_limit_ = 95;
    battery_soc_low_limit_ = 20;
    battery_cell_count_ = 16;
    battery_chemistry_ = 2; // LFP
    battery_double_enabled_ = false;
    battery_pack_max_voltage_dV_ = 580;
    battery_pack_min_voltage_dV_ = 460;
    battery_cell_max_voltage_mV_ = 4200;
    battery_cell_min_voltage_mV_ = 3000;
    battery_soc_estimated_ = false;
    battery_led_mode_ = 0;
    datalayer.battery.status.led_mode = static_cast<led_mode_enum>(battery_led_mode_);
    
    // Save defaults to NVS
    save_battery_settings();
    
    LOG_INFO("SETTINGS", "Factory defaults restored");
}
void SettingsManager::increment_battery_version() {
    VersionUtils::increment_version(battery_settings_version_);
    LOG_INFO("SETTINGS", "Battery settings version incremented to %u", battery_settings_version_);
}