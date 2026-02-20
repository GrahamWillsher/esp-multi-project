/**
 * @file system_settings.h
 * @brief System-wide configuration with NVS persistence
 * 
 * Manages all Battery Emulator component selections and system parameters.
 * All settings are persisted in NVS (Non-Volatile Storage) for persistence across restarts.
 * 
 * Settings are:
 * - Loaded from NVS on startup (or defaults if not yet configured)
 * - Saved to NVS when changed
 * - Transmitted to receiver via ESP-NOW in every snapshot
 * - Displayed on receiver's UI
 * 
 * This enables complete runtime configuration of:
 * - Which BMS implementation to use (45+ types)
 * - Which inverter protocol to use (15+ types)
 * - Which charger implementation to use (2 types)
 * - Which shunt/current sensor to use (3 types)
 * - All voltage/current/temperature limits
 * 
 * Phase 1 defaults: Pylon BMS only, inverter/charger/shunt disabled
 */

#ifndef _SYSTEM_SETTINGS_H
#define _SYSTEM_SETTINGS_H

#include <cstdint>
#include <nvs.h>
#include <nvs_flash.h>
#include <cstring>

// ============================================================================
// NVS CONFIGURATION
// ============================================================================

#define SYSTEM_NVS_NAMESPACE "battery_sys"
#define NVS_BATTERY_TYPE_KEY "bms_type"
#define NVS_BATTERY_PROFILE_KEY "bat_profile"
#define NVS_INVERTER_TYPE_KEY "inv_type"
#define NVS_CHARGER_TYPE_KEY "chr_type"
#define NVS_SHUNT_TYPE_KEY "shunt_type"
#define NVS_MULTI_BATTERY_KEY "multi_bat"
#define NVS_MAX_VOLTAGE_KEY "max_volt"
#define NVS_MIN_VOLTAGE_KEY "min_volt"
#define NVS_MAX_CURRENT_KEY "max_curr"
#define NVS_MAX_TEMP_KEY "max_temp"
#define NVS_MIN_TEMP_KEY "min_temp"
#define NVS_UPDATE_RATE_KEY "upd_rate"
#define NVS_CONFIG_VERSION_KEY "cfg_ver"

// Configuration version for migrations
#define CONFIG_VERSION 1

// ============================================================================
// FEATURE FLAGS
// ============================================================================

#define ENABLE_STARTUP_DIAGNOSTICS 1
#define ENABLE_DEBUG_LOGGING 1
#define ENABLE_DATALAYER_INTEGRITY_CHECK 1

// ============================================================================
// PHASE 1 DEFAULTS
// ============================================================================

// Primary battery: Pylon (Type 29) - LiFePO4 common baseline
#define DEFAULT_BMS_TYPE 29  // BatteryType::PYLON_BATTERY
#define DEFAULT_BATTERY_PROFILE_TYPE DEFAULT_BMS_TYPE

// Phase 1: Monitoring only - no inverter/charger/shunt
#define DEFAULT_INVERTER_TYPE 0  // InverterType::NONE
#define DEFAULT_CHARGER_TYPE 0   // ChargerType::NONE
#define DEFAULT_SHUNT_TYPE 0     // ShuntTypeEnum::NONE

// Multi-battery not used in Phase 1
#define DEFAULT_MULTI_BATTERY_ENABLED 0

// ============================================================================
// VOLTAGE LIMITS (in millivolts)
// ============================================================================

#define DEFAULT_PACK_MAX_VOLTAGE_MV 500000      // 500V - typical 400V pack with headroom
#define DEFAULT_PACK_MIN_VOLTAGE_MV 300000      // 300V - minimum pack voltage
#define DEFAULT_CELL_MAX_VOLTAGE_MV 4300        // 4.3V - LiFePO4 typical max
#define DEFAULT_CELL_MIN_VOLTAGE_MV 2700        // 2.7V - LiFePO4 typical min

// ============================================================================
// CURRENT LIMITS (in deciAmperes, i.e., units of 0.1A)
// ============================================================================

#define DEFAULT_MAX_CHARGE_CURRENT_DA 300       // 30.0A max charge
#define DEFAULT_MAX_DISCHARGE_CURRENT_DA 300    // 30.0A max discharge

// ============================================================================
// TEMPERATURE LIMITS (in deciCelsius, i.e., units of 0.1°C)
// ============================================================================

#define DEFAULT_MAX_TEMP_DC 550                 // 55.0°C maximum operating temperature
#define DEFAULT_MIN_TEMP_DC -50                 // -5.0°C minimum operating temperature

// ============================================================================
// UPDATE RATES (in milliseconds)
// ============================================================================

#define DEFAULT_ESPNOW_UPDATE_RATE_MS 100       // Snapshot every 100ms to receiver
#define DEFAULT_DISPLAY_REFRESH_RATE_MS 500     // Receiver display update every 500ms
#define DEFAULT_MQTT_PUBLISH_RATE_MS 5000       // MQTT publish every 5 seconds (if enabled)
#define DEFAULT_BMS_PROCESS_RATE_MS 100         // Process BMS data every 100ms

// ============================================================================
// SYSTEM SETTINGS MANAGER CLASS
// ============================================================================

/**
 * @brief System settings manager with NVS persistence
 * 
 * Singleton pattern - use SystemSettings::instance() to access
 * 
 * Usage:
 * ```cpp
 * SystemSettings& settings = SystemSettings::instance();
 * settings.load_from_nvs();  // Load on startup
 * 
 * // Change a setting
 * settings.set_bms_type(29);  // Automatically saves to NVS
 * 
 * // Query settings
 * uint8_t bms = settings.get_bms_type();
 * uint16_t max_v = settings.get_max_voltage_mv();
 * ```
 */
class SystemSettings {
 public:
  static SystemSettings& instance();
  
  // ========== INITIALIZATION ==========
  
  /**
   * @brief Initialize NVS and load settings from storage
   * @return true if successful
   */
  bool init();
  
  /**
   * @brief Load all settings from NVS (or set defaults if not found)
   * @return true if successful
   */
  bool load_from_nvs();
  
  /**
   * @brief Save all current settings to NVS
   * @return true if successful
   */
  bool save_to_nvs();
  
  /**
   * @brief Reset all settings to factory defaults
   * @return true if successful
   */
  bool reset_to_defaults();
  
  // ========== BMS/BATTERY SETTINGS ==========
  
  /**
   * @brief Set primary BMS type and save to NVS
   * @param type Battery type enum value (0-45)
   * @return true if successful
   */
  bool set_bms_type(uint8_t type);
  
  /**
   * @brief Get current primary BMS type
   */
  uint8_t get_bms_type() const { return bms_type_; }

  /**
   * @brief Set battery profile type and save to NVS
   * @param type Battery profile enum value (0-45)
   * @return true if successful
   */
  bool set_battery_profile_type(uint8_t type);

  /**
   * @brief Get current battery profile type
   */
  uint8_t get_battery_profile_type() const { return battery_profile_type_; }
  
  /**
   * @brief Set secondary BMS type (multi-battery support)
   */
  bool set_secondary_bms_type(uint8_t type);
  
  /**
   * @brief Get secondary BMS type
   */
  uint8_t get_secondary_bms_type() const { return secondary_bms_type_; }
  
  /**
   * @brief Enable/disable multi-battery mode
   */
  bool set_multi_battery_enabled(bool enabled);
  
  /**
   * @brief Is multi-battery mode enabled?
   */
  bool is_multi_battery_enabled() const { return multi_battery_enabled_; }
  
  // ========== INVERTER SETTINGS ==========
  
  /**
   * @brief Set inverter type and save to NVS
   * @param type Inverter type enum value (0-21, 0=disabled)
   */
  bool set_inverter_type(uint8_t type);
  
  /**
   * @brief Get current inverter type
   */
  uint8_t get_inverter_type() const { return inverter_type_; }
  
  // ========== CHARGER SETTINGS ==========
  
  /**
   * @brief Set charger type and save to NVS
   * @param type Charger type enum value (0-2, 0=disabled)
   */
  bool set_charger_type(uint8_t type);
  
  /**
   * @brief Get current charger type
   */
  uint8_t get_charger_type() const { return charger_type_; }
  
  // ========== SHUNT SETTINGS ==========
  
  /**
   * @brief Set shunt type and save to NVS
   * @param type Shunt type enum value (0-2, 0=disabled)
   */
  bool set_shunt_type(uint8_t type);
  
  /**
   * @brief Get current shunt type
   */
  uint8_t get_shunt_type() const { return shunt_type_; }
  
  // ========== VOLTAGE LIMITS ==========
  
  /**
   * @brief Set maximum pack voltage (millivolts)
   */
  bool set_max_voltage_mv(uint32_t mv);
  uint32_t get_max_voltage_mv() const { return max_voltage_mv_; }
  
  /**
   * @brief Set minimum pack voltage (millivolts)
   */
  bool set_min_voltage_mv(uint32_t mv);
  uint32_t get_min_voltage_mv() const { return min_voltage_mv_; }
  
  // ========== CURRENT LIMITS (deciAmperes) ==========
  
  /**
   * @brief Set maximum charge current (deciAmperes, 0.1A units)
   */
  bool set_max_charge_current_da(uint16_t da);
  uint16_t get_max_charge_current_da() const { return max_charge_current_da_; }
  
  /**
   * @brief Set maximum discharge current (deciAmperes, 0.1A units)
   */
  bool set_max_discharge_current_da(uint16_t da);
  uint16_t get_max_discharge_current_da() const { return max_discharge_current_da_; }
  
  // ========== TEMPERATURE LIMITS (deciCelsius) ==========
  
  /**
   * @brief Set maximum temperature (deciCelsius, 0.1°C units)
   */
  bool set_max_temp_dc(int16_t dc);
  int16_t get_max_temp_dc() const { return max_temp_dc_; }
  
  /**
   * @brief Set minimum temperature (deciCelsius, 0.1°C units)
   */
  bool set_min_temp_dc(int16_t dc);
  int16_t get_min_temp_dc() const { return min_temp_dc_; }
  
  // ========== UPDATE RATES ==========
  
  /**
   * @brief Set ESP-NOW snapshot rate (milliseconds)
   */
  bool set_espnow_update_rate_ms(uint16_t ms);
  uint16_t get_espnow_update_rate_ms() const { return espnow_update_rate_ms_; }
  
  /**
   * @brief Get display refresh rate
   */
  uint16_t get_display_refresh_rate_ms() const { return display_refresh_rate_ms_; }
  
  /**
   * @brief Get MQTT publish rate
   */
  uint16_t get_mqtt_publish_rate_ms() const { return mqtt_publish_rate_ms_; }
  
  // ========== DIAGNOSTICS ==========
  
  /**
   * @brief Print all current settings to log
   */
  void print_settings();
  
  /**
   * @brief Get configuration version
   */
  uint8_t get_config_version() const { return config_version_; }
  
 private:
  SystemSettings() = default;
  ~SystemSettings() = default;
  
  // NVS handle
  nvs_handle_t nvs_handle_ = 0;
  
  // Configuration version for migrations
  uint8_t config_version_ = CONFIG_VERSION;
  
  // Component types
  uint8_t bms_type_ = DEFAULT_BMS_TYPE;
  uint8_t battery_profile_type_ = DEFAULT_BATTERY_PROFILE_TYPE;
  uint8_t secondary_bms_type_ = 0;  // NONE
  uint8_t inverter_type_ = DEFAULT_INVERTER_TYPE;
  uint8_t charger_type_ = DEFAULT_CHARGER_TYPE;
  uint8_t shunt_type_ = DEFAULT_SHUNT_TYPE;
  
  bool multi_battery_enabled_ = DEFAULT_MULTI_BATTERY_ENABLED;
  
  // Voltage limits
  uint32_t max_voltage_mv_ = DEFAULT_PACK_MAX_VOLTAGE_MV;
  uint32_t min_voltage_mv_ = DEFAULT_PACK_MIN_VOLTAGE_MV;
  
  // Current limits (deciAmperes)
  uint16_t max_charge_current_da_ = DEFAULT_MAX_CHARGE_CURRENT_DA;
  uint16_t max_discharge_current_da_ = DEFAULT_MAX_DISCHARGE_CURRENT_DA;
  
  // Temperature limits (deciCelsius)
  int16_t max_temp_dc_ = DEFAULT_MAX_TEMP_DC;
  int16_t min_temp_dc_ = DEFAULT_MIN_TEMP_DC;
  
  // Update rates
  uint16_t espnow_update_rate_ms_ = DEFAULT_ESPNOW_UPDATE_RATE_MS;
  uint16_t display_refresh_rate_ms_ = DEFAULT_DISPLAY_REFRESH_RATE_MS;
  uint16_t mqtt_publish_rate_ms_ = DEFAULT_MQTT_PUBLISH_RATE_MS;
  uint16_t bms_process_rate_ms_ = DEFAULT_BMS_PROCESS_RATE_MS;
  
  // Helper: Write single setting to NVS
  bool nvs_write_u8(const char* key, uint8_t value);
  bool nvs_write_u16(const char* key, uint16_t value);
  bool nvs_write_u32(const char* key, uint32_t value);
  bool nvs_write_i16(const char* key, int16_t value);
  
  // Helper: Read single setting from NVS
  bool nvs_read_u8(const char* key, uint8_t& value, uint8_t default_val);
  bool nvs_read_u16(const char* key, uint16_t& value, uint16_t default_val);
  bool nvs_read_u32(const char* key, uint32_t& value, uint32_t default_val);
  bool nvs_read_i16(const char* key, int16_t& value, int16_t default_val);
};

// Global convenience reference
#define gSystemSettings SystemSettings::instance()

#endif  // _SYSTEM_SETTINGS_H
