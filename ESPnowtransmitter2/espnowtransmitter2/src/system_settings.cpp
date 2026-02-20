/**
 * @file system_settings.cpp
 * @brief Implementation of system settings with NVS persistence
 */

#include "system_settings.h"
#include "config/logging_config.h"
#include <mqtt_logger.h>
#include <esp_err.h>

// ========== SINGLETON ==========

SystemSettings& SystemSettings::instance() {
  static SystemSettings instance;
  return instance;
}

// ========== INITIALIZATION ==========

bool SystemSettings::init() {
  LOG_INFO("SETTINGS", "Initializing NVS...");
  
  // Initialize NVS
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    // NVS partition was truncated - erase and retry
    LOG_WARN("SETTINGS", "NVS partition needs erasing, reinitializing...");
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  
  if (err != ESP_OK) {
    LOG_ERROR("SETTINGS", "NVS init failed: %d", err);
    return false;
  }
  
  // Open NVS handle
  err = nvs_open(SYSTEM_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle_);
  if (err != ESP_OK) {
    LOG_ERROR("SETTINGS", "Failed to open NVS namespace: %d", err);
    return false;
  }
  
  LOG_INFO("SETTINGS", "✓ NVS initialized");
  
  // Load settings from NVS (or defaults if first boot)
  return load_from_nvs();
}

bool SystemSettings::load_from_nvs() {
  LOG_INFO("SETTINGS", "Loading settings from NVS...");
  
  // Read configuration version first
  uint8_t stored_version = 0;
  nvs_read_u8(NVS_CONFIG_VERSION_KEY, stored_version, CONFIG_VERSION);
  
  if (stored_version != CONFIG_VERSION) {
    LOG_WARN("SETTINGS", "Config version mismatch (stored=%d, current=%d), using defaults",
             stored_version, CONFIG_VERSION);
    reset_to_defaults();
    return true;
  }
  
  // Load component types
  nvs_read_u8(NVS_BATTERY_TYPE_KEY, bms_type_, DEFAULT_BMS_TYPE);
  nvs_read_u8(NVS_BATTERY_PROFILE_KEY, battery_profile_type_, DEFAULT_BATTERY_PROFILE_TYPE);
  nvs_read_u8(NVS_INVERTER_TYPE_KEY, inverter_type_, DEFAULT_INVERTER_TYPE);
  nvs_read_u8(NVS_CHARGER_TYPE_KEY, charger_type_, DEFAULT_CHARGER_TYPE);
  nvs_read_u8(NVS_SHUNT_TYPE_KEY, shunt_type_, DEFAULT_SHUNT_TYPE);
  
  // Load multi-battery flag
  uint8_t multi_bat_u8 = DEFAULT_MULTI_BATTERY_ENABLED;
  nvs_read_u8(NVS_MULTI_BATTERY_KEY, multi_bat_u8, DEFAULT_MULTI_BATTERY_ENABLED);
  multi_battery_enabled_ = (multi_bat_u8 != 0);
  
  // Load voltage limits
  nvs_read_u32(NVS_MAX_VOLTAGE_KEY, max_voltage_mv_, DEFAULT_PACK_MAX_VOLTAGE_MV);
  nvs_read_u32(NVS_MIN_VOLTAGE_KEY, min_voltage_mv_, DEFAULT_PACK_MIN_VOLTAGE_MV);
  
  // Load current limits
  nvs_read_u16(NVS_MAX_CURRENT_KEY, max_charge_current_da_, DEFAULT_MAX_CHARGE_CURRENT_DA);
  max_discharge_current_da_ = max_charge_current_da_;  // Use same for both in Phase 1
  
  // Load temperature limits
  nvs_read_i16(NVS_MAX_TEMP_KEY, max_temp_dc_, DEFAULT_MAX_TEMP_DC);
  nvs_read_i16(NVS_MIN_TEMP_KEY, min_temp_dc_, DEFAULT_MIN_TEMP_DC);
  
  // Load update rate
  nvs_read_u16(NVS_UPDATE_RATE_KEY, espnow_update_rate_ms_, DEFAULT_ESPNOW_UPDATE_RATE_MS);
  
  LOG_INFO("SETTINGS", "✓ Settings loaded from NVS");
  print_settings();
  
  return true;
}

bool SystemSettings::save_to_nvs() {
  LOG_DEBUG("SETTINGS", "Saving settings to NVS...");
  
  // Save configuration version
  if (!nvs_write_u8(NVS_CONFIG_VERSION_KEY, config_version_)) return false;
  
  // Save component types
  if (!nvs_write_u8(NVS_BATTERY_TYPE_KEY, bms_type_)) return false;
  if (!nvs_write_u8(NVS_BATTERY_PROFILE_KEY, battery_profile_type_)) return false;
  if (!nvs_write_u8(NVS_INVERTER_TYPE_KEY, inverter_type_)) return false;
  if (!nvs_write_u8(NVS_CHARGER_TYPE_KEY, charger_type_)) return false;
  if (!nvs_write_u8(NVS_SHUNT_TYPE_KEY, shunt_type_)) return false;
  
  // Save multi-battery flag
  uint8_t multi_bat_u8 = multi_battery_enabled_ ? 1 : 0;
  if (!nvs_write_u8(NVS_MULTI_BATTERY_KEY, multi_bat_u8)) return false;
  
  // Save voltage limits
  if (!nvs_write_u32(NVS_MAX_VOLTAGE_KEY, max_voltage_mv_)) return false;
  if (!nvs_write_u32(NVS_MIN_VOLTAGE_KEY, min_voltage_mv_)) return false;
  
  // Save current limits
  if (!nvs_write_u16(NVS_MAX_CURRENT_KEY, max_charge_current_da_)) return false;
  
  // Save temperature limits
  if (!nvs_write_i16(NVS_MAX_TEMP_KEY, max_temp_dc_)) return false;
  if (!nvs_write_i16(NVS_MIN_TEMP_KEY, min_temp_dc_)) return false;
  
  // Save update rate
  if (!nvs_write_u16(NVS_UPDATE_RATE_KEY, espnow_update_rate_ms_)) return false;
  
  // Commit changes
  esp_err_t err = nvs_commit(nvs_handle_);
  if (err != ESP_OK) {
    LOG_ERROR("SETTINGS", "NVS commit failed: %d", err);
    return false;
  }
  
  LOG_DEBUG("SETTINGS", "✓ Settings saved to NVS");
  return true;
}

bool SystemSettings::reset_to_defaults() {
  LOG_INFO("SETTINGS", "Resetting to factory defaults...");
  
  bms_type_ = DEFAULT_BMS_TYPE;
  battery_profile_type_ = DEFAULT_BATTERY_PROFILE_TYPE;
  secondary_bms_type_ = 0;  // NONE
  inverter_type_ = DEFAULT_INVERTER_TYPE;
  charger_type_ = DEFAULT_CHARGER_TYPE;
  shunt_type_ = DEFAULT_SHUNT_TYPE;
  multi_battery_enabled_ = DEFAULT_MULTI_BATTERY_ENABLED;
  
  max_voltage_mv_ = DEFAULT_PACK_MAX_VOLTAGE_MV;
  min_voltage_mv_ = DEFAULT_PACK_MIN_VOLTAGE_MV;
  max_charge_current_da_ = DEFAULT_MAX_CHARGE_CURRENT_DA;
  max_discharge_current_da_ = DEFAULT_MAX_DISCHARGE_CURRENT_DA;
  max_temp_dc_ = DEFAULT_MAX_TEMP_DC;
  min_temp_dc_ = DEFAULT_MIN_TEMP_DC;
  
  espnow_update_rate_ms_ = DEFAULT_ESPNOW_UPDATE_RATE_MS;
  display_refresh_rate_ms_ = DEFAULT_DISPLAY_REFRESH_RATE_MS;
  mqtt_publish_rate_ms_ = DEFAULT_MQTT_PUBLISH_RATE_MS;
  bms_process_rate_ms_ = DEFAULT_BMS_PROCESS_RATE_MS;
  
  config_version_ = CONFIG_VERSION;
  
  LOG_INFO("SETTINGS", "✓ Defaults restored");
  return save_to_nvs();
}

// ========== SETTERS WITH NVS PERSISTENCE ==========

bool SystemSettings::set_bms_type(uint8_t type) {
  if (type > 45) {
    LOG_ERROR("SETTINGS", "Invalid BMS type: %d", type);
    return false;
  }
  bms_type_ = type;
  LOG_INFO("SETTINGS", "BMS type changed to: %d", type);
  return save_to_nvs();
}

bool SystemSettings::set_battery_profile_type(uint8_t type) {
  if (type > 45) {
    LOG_ERROR("SETTINGS", "Invalid battery profile type: %d", type);
    return false;
  }
  battery_profile_type_ = type;
  LOG_INFO("SETTINGS", "Battery profile type changed to: %d", type);
  return save_to_nvs();
}

bool SystemSettings::set_secondary_bms_type(uint8_t type) {
  if (type > 45) {
    LOG_ERROR("SETTINGS", "Invalid secondary BMS type: %d", type);
    return false;
  }
  secondary_bms_type_ = type;
  LOG_INFO("SETTINGS", "Secondary BMS type changed to: %d", type);
  return save_to_nvs();
}

bool SystemSettings::set_multi_battery_enabled(bool enabled) {
  multi_battery_enabled_ = enabled;
  LOG_INFO("SETTINGS", "Multi-battery mode: %s", enabled ? "ENABLED" : "DISABLED");
  return save_to_nvs();
}

bool SystemSettings::set_inverter_type(uint8_t type) {
  if (type > 21) {
    LOG_ERROR("SETTINGS", "Invalid inverter type: %d", type);
    return false;
  }
  inverter_type_ = type;
  LOG_INFO("SETTINGS", "Inverter type changed to: %d", type);
  return save_to_nvs();
}

bool SystemSettings::set_charger_type(uint8_t type) {
  if (type > 2) {
    LOG_ERROR("SETTINGS", "Invalid charger type: %d", type);
    return false;
  }
  charger_type_ = type;
  LOG_INFO("SETTINGS", "Charger type changed to: %d", type);
  return save_to_nvs();
}

bool SystemSettings::set_shunt_type(uint8_t type) {
  if (type > 2) {
    LOG_ERROR("SETTINGS", "Invalid shunt type: %d", type);
    return false;
  }
  shunt_type_ = type;
  LOG_INFO("SETTINGS", "Shunt type changed to: %d", type);
  return save_to_nvs();
}

bool SystemSettings::set_max_voltage_mv(uint32_t mv) {
  max_voltage_mv_ = mv;
  LOG_INFO("SETTINGS", "Max voltage changed to: %lu mV", mv);
  return save_to_nvs();
}

bool SystemSettings::set_min_voltage_mv(uint32_t mv) {
  min_voltage_mv_ = mv;
  LOG_INFO("SETTINGS", "Min voltage changed to: %lu mV", mv);
  return save_to_nvs();
}

bool SystemSettings::set_max_charge_current_da(uint16_t da) {
  max_charge_current_da_ = da;
  LOG_INFO("SETTINGS", "Max charge current changed to: %u dA", da);
  return save_to_nvs();
}

bool SystemSettings::set_max_discharge_current_da(uint16_t da) {
  max_discharge_current_da_ = da;
  LOG_INFO("SETTINGS", "Max discharge current changed to: %u dA", da);
  return save_to_nvs();
}

bool SystemSettings::set_max_temp_dc(int16_t dc) {
  max_temp_dc_ = dc;
  LOG_INFO("SETTINGS", "Max temperature changed to: %d dC", dc);
  return save_to_nvs();
}

bool SystemSettings::set_min_temp_dc(int16_t dc) {
  min_temp_dc_ = dc;
  LOG_INFO("SETTINGS", "Min temperature changed to: %d dC", dc);
  return save_to_nvs();
}

bool SystemSettings::set_espnow_update_rate_ms(uint16_t ms) {
  espnow_update_rate_ms_ = ms;
  LOG_INFO("SETTINGS", "ESP-NOW update rate changed to: %u ms", ms);
  return save_to_nvs();
}

// ========== DIAGNOSTICS ==========

void SystemSettings::print_settings() {
  LOG_INFO("SETTINGS", "=== Current System Settings ===");
  LOG_INFO("SETTINGS", "Config Version: %d", config_version_);
  LOG_INFO("SETTINGS", "");
  LOG_INFO("SETTINGS", "Components:");
  LOG_INFO("SETTINGS", "  Primary BMS Type: %d", bms_type_);
  LOG_INFO("SETTINGS", "  Battery Profile Type: %d", battery_profile_type_);
  LOG_INFO("SETTINGS", "  Secondary BMS Type: %d", secondary_bms_type_);
  LOG_INFO("SETTINGS", "  Multi-battery: %s", multi_battery_enabled_ ? "ENABLED" : "DISABLED");
  LOG_INFO("SETTINGS", "  Inverter Type: %d", inverter_type_);
  LOG_INFO("SETTINGS", "  Charger Type: %d", charger_type_);
  LOG_INFO("SETTINGS", "  Shunt Type: %d", shunt_type_);
  LOG_INFO("SETTINGS", "");
  LOG_INFO("SETTINGS", "Voltage Limits:");
  LOG_INFO("SETTINGS", "  Max Pack: %lu mV (%.1f V)", max_voltage_mv_, max_voltage_mv_ / 1000.0f);
  LOG_INFO("SETTINGS", "  Min Pack: %lu mV (%.1f V)", min_voltage_mv_, min_voltage_mv_ / 1000.0f);
  LOG_INFO("SETTINGS", "");
  LOG_INFO("SETTINGS", "Current Limits:");
  LOG_INFO("SETTINGS", "  Max Charge: %u dA (%.1f A)", max_charge_current_da_, max_charge_current_da_ / 10.0f);
  LOG_INFO("SETTINGS", "  Max Discharge: %u dA (%.1f A)", max_discharge_current_da_, max_discharge_current_da_ / 10.0f);
  LOG_INFO("SETTINGS", "");
  LOG_INFO("SETTINGS", "Temperature Limits:");
  LOG_INFO("SETTINGS", "  Max: %d dC (%.1f °C)", max_temp_dc_, max_temp_dc_ / 10.0f);
  LOG_INFO("SETTINGS", "  Min: %d dC (%.1f °C)", min_temp_dc_, min_temp_dc_ / 10.0f);
  LOG_INFO("SETTINGS", "");
  LOG_INFO("SETTINGS", "Update Rates:");
  LOG_INFO("SETTINGS", "  ESP-NOW: %u ms", espnow_update_rate_ms_);
  LOG_INFO("SETTINGS", "  Display: %u ms", display_refresh_rate_ms_);
  LOG_INFO("SETTINGS", "  MQTT: %u ms", mqtt_publish_rate_ms_);
  LOG_INFO("SETTINGS", "  BMS Process: %u ms", bms_process_rate_ms_);
  LOG_INFO("SETTINGS", "==============================");
}

// ========== NVS HELPERS ==========

bool SystemSettings::nvs_write_u8(const char* key, uint8_t value) {
  esp_err_t err = nvs_set_u8(nvs_handle_, key, value);
  if (err != ESP_OK) {
    LOG_ERROR("SETTINGS", "Failed to write u8 key '%s': %d", key, err);
    return false;
  }
  return true;
}

bool SystemSettings::nvs_write_u16(const char* key, uint16_t value) {
  esp_err_t err = nvs_set_u16(nvs_handle_, key, value);
  if (err != ESP_OK) {
    LOG_ERROR("SETTINGS", "Failed to write u16 key '%s': %d", key, err);
    return false;
  }
  return true;
}

bool SystemSettings::nvs_write_u32(const char* key, uint32_t value) {
  esp_err_t err = nvs_set_u32(nvs_handle_, key, value);
  if (err != ESP_OK) {
    LOG_ERROR("SETTINGS", "Failed to write u32 key '%s': %d", key, err);
    return false;
  }
  return true;
}

bool SystemSettings::nvs_write_i16(const char* key, int16_t value) {
  esp_err_t err = nvs_set_i16(nvs_handle_, key, value);
  if (err != ESP_OK) {
    LOG_ERROR("SETTINGS", "Failed to write i16 key '%s': %d", key, err);
    return false;
  }
  return true;
}

bool SystemSettings::nvs_read_u8(const char* key, uint8_t& value, uint8_t default_val) {
  esp_err_t err = nvs_get_u8(nvs_handle_, key, &value);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    LOG_DEBUG("SETTINGS", "Key '%s' not found, using default: %d", key, default_val);
    value = default_val;
    return true;
  }
  if (err != ESP_OK) {
    LOG_ERROR("SETTINGS", "Failed to read u8 key '%s': %d", key, err);
    value = default_val;
    return false;
  }
  return true;
}

bool SystemSettings::nvs_read_u16(const char* key, uint16_t& value, uint16_t default_val) {
  esp_err_t err = nvs_get_u16(nvs_handle_, key, &value);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    LOG_DEBUG("SETTINGS", "Key '%s' not found, using default: %d", key, default_val);
    value = default_val;
    return true;
  }
  if (err != ESP_OK) {
    LOG_ERROR("SETTINGS", "Failed to read u16 key '%s': %d", key, err);
    value = default_val;
    return false;
  }
  return true;
}

bool SystemSettings::nvs_read_u32(const char* key, uint32_t& value, uint32_t default_val) {
  esp_err_t err = nvs_get_u32(nvs_handle_, key, &value);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    LOG_DEBUG("SETTINGS", "Key '%s' not found, using default: %lu", key, default_val);
    value = default_val;
    return true;
  }
  if (err != ESP_OK) {
    LOG_ERROR("SETTINGS", "Failed to read u32 key '%s': %d", key, err);
    value = default_val;
    return false;
  }
  return true;
}

bool SystemSettings::nvs_read_i16(const char* key, int16_t& value, int16_t default_val) {
  esp_err_t err = nvs_get_i16(nvs_handle_, key, &value);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    LOG_DEBUG("SETTINGS", "Key '%s' not found, using default: %d", key, default_val);
    value = default_val;
    return true;
  }
  if (err != ESP_OK) {
    LOG_ERROR("SETTINGS", "Failed to read i16 key '%s': %d", key, err);
    value = default_val;
    return false;
  }
  return true;
}
