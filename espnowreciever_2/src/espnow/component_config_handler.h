/**
 * @file component_config_handler.h
 * @brief Receives and stores component configuration from transmitter
 * 
 * Handles msg_component_config messages containing BMS type, inverter type,
 * charger type, and shunt type selections.
 * 
 * Responsibilities:
 * - Receive component config messages via ESP-NOW
 * - Store configuration in NVS for persistence
 * - Provide configuration to display layer
 * - Track configuration version for change detection
 */

#pragma once

#include <cstdint>
#include <nvs.h>
#include <espnow_common.h>  // Include actual message structure definitions

/**
 * @brief Component configuration data storage
 */
struct ComponentConfig {
  uint8_t bms_type = 29;                    // Default: Pylon
  uint8_t secondary_bms_type = 0;           // Default: None
  uint8_t inverter_type = 0;                // Default: None
  uint8_t charger_type = 0;                 // Default: None
  uint8_t shunt_type = 0;                   // Default: None
  bool multi_battery_enabled = false;       // Default: Disabled
  uint32_t config_version = 0;              // Version tracking
  uint32_t last_update_ms = 0;              // Timestamp of last update
};

class ComponentConfigHandler {
 public:
  static ComponentConfigHandler& instance();
  
  /**
   * @brief Initialize NVS and load stored configuration
   * @return true if successful
   */
  bool init();
  
  /**
   * @brief Handle incoming component config message
   * @param msg Pointer to component_config_msg_t
   * @return true if processed successfully
   */
  bool handle_message(const uint8_t* data, size_t len);
  
  /**
   * @brief Get current component configuration
   */
  const ComponentConfig& get_config() const { return config_; }
  
  /**
   * @brief Get human-readable BMS type name
   */
  const char* get_bms_name(uint8_t type) const;
  
  /**
   * @brief Get human-readable inverter type name
   */
  const char* get_inverter_name(uint8_t type) const;
  
  /**
   * @brief Get human-readable charger type name
   */
  const char* get_charger_name(uint8_t type) const;
  
  /**
   * @brief Get human-readable shunt type name
   */
  const char* get_shunt_name(uint8_t type) const;
  
  /**
   * @brief Has configuration been received?
   */
  bool is_config_received() const { return config_received_; }
  
  /**
   * @brief Print current configuration to log
   */
  void print_config();
  
 private:
  ComponentConfigHandler() = default;
  ~ComponentConfigHandler();
  
  // Load from NVS
  bool load_from_nvs();
  
  // Save to NVS
  bool save_to_nvs();
  
  // Validate checksum
  bool validate_checksum(const uint8_t* data, size_t len) const;
  
  // NVS handle
  nvs_handle_t nvs_handle_ = 0;
  
  // Current configuration
  ComponentConfig config_;
  
  // Flag indicating if we've received config from transmitter
  bool config_received_ = false;
};
