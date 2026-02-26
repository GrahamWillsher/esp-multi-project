#pragma once

#include <Arduino.h>
#include <Preferences.h>

/**
 * @brief Test Data Configuration Manager
 * 
 * Manages runtime configuration for test data generation with NVS persistence.
 * Replaces compile-time flags with dynamic runtime control accessible via HTTP API.
 * 
 * Phase 2 Implementation: Comprehensive test data control
 */

namespace TestDataConfig {

/**
 * @brief Test data generation modes
 */
enum class Mode : uint8_t {
    OFF = 0,              // No test data (use real CAN data only)
    SOC_POWER_ONLY = 1,   // Generate SOC/power only (ESP-NOW, no cells)
    FULL_BATTERY_DATA = 2 // Generate SOC/power + cells (ESP-NOW + MQTT)
};

/**
 * @brief Battery source selection for test data
 */
enum class BatterySource : uint8_t {
    SELECTED_BATTERY = 0, // Use current battery selection (Nissan Leaf, Tesla, etc.)
    CUSTOM_COUNT = 1      // Use custom cell count
};

/**
 * @brief SOC animation profile
 */
enum class SocProfile : uint8_t {
    TRIANGLE = 0,  // 20-80% oscillation (default)
    CONSTANT = 1,  // Fixed value
    RANDOM_WALK = 2 // Random drift
};

/**
 * @brief Power animation profile
 */
enum class PowerProfile : uint8_t {
    SINE = 0,      // -4000W to +4000W sine wave (default)
    STEP = 1,      // Step changes
    RANDOM = 2     // Random values
};

/**
 * @brief Complete test data configuration
 */
struct Config {
    Mode mode;
    BatterySource battery_source;
    uint16_t custom_cell_count;  // Only used if battery_source == CUSTOM_COUNT
    SocProfile soc_profile;
    PowerProfile power_profile;
    
    // Default configuration
    Config() 
        : mode(Mode::OFF)
        , battery_source(BatterySource::SELECTED_BATTERY)
        , custom_cell_count(96)
        , soc_profile(SocProfile::TRIANGLE)
        , power_profile(PowerProfile::SINE)
    {}
};

/**
 * @brief Initialize test data configuration system
 * 
 * Loads configuration from NVS or uses defaults if not found.
 * Must be called during system startup.
 * 
 * @return true if initialized successfully
 */
bool init();

/**
 * @brief Get current test data configuration
 * 
 * @return Current configuration
 */
const Config& get_config();

/**
 * @brief Update test data configuration
 * 
 * @param config New configuration to apply
 * @param persist If true, save to NVS for persistence across reboots
 * @return true if configuration updated successfully
 */
bool set_config(const Config& config, bool persist = true);

/**
 * @brief Apply current configuration to test data generator
 * 
 * Reinitializes TestDataGenerator with current settings.
 * Call after set_config() or battery type changes.
 * 
 * @return true if applied successfully
 */
bool apply_config();

/**
 * @brief Reset configuration to defaults
 * 
 * @param persist If true, save to NVS
 * @return true if reset successfully
 */
bool reset_to_defaults(bool persist = true);

/**
 * @brief Check if test data is currently enabled
 * 
 * @return true if mode != OFF
 */
bool is_enabled();

/**
 * @brief Check if cell data generation is enabled
 * 
 * @return true if mode == FULL_BATTERY_DATA
 */
bool should_generate_cells();

/**
 * @brief Get effective cell count for test data
 * 
 * Returns either battery's configured cell count or custom count.
 * 
 * @return Number of cells to use
 */
uint16_t get_effective_cell_count();

/**
 * @brief Convert mode enum to string
 */
const char* mode_to_string(Mode mode);

/**
 * @brief Convert string to mode enum
 * 
 * @param str Mode string ("OFF", "SOC_POWER_ONLY", "FULL_BATTERY_DATA")
 * @return Corresponding mode, or Mode::OFF if invalid
 */
Mode string_to_mode(const char* str);

/**
 * @brief Get configuration as JSON object
 * 
 * @param buffer Output buffer for JSON string
 * @param buffer_size Size of output buffer
 * @return true if JSON generated successfully
 */
bool get_config_json(char* buffer, size_t buffer_size);

/**
 * @brief Set configuration from JSON object
 * 
 * @param json JSON string with configuration
 * @param persist If true, save to NVS
 * @return true if configuration parsed and applied successfully
 */
bool set_config_from_json(const char* json, bool persist = true);

} // namespace TestDataConfig
