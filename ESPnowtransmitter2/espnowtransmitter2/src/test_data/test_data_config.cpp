#include "test_data_config.h"
#include "../config/logging_config.h"
#include "../datalayer/datalayer.h"
#include "../battery_emulator/test_data_generator.h"
#include <ArduinoJson.h>

namespace TestDataConfig {

static const char* TAG = "TEST_DATA_CONFIG";
static const char* NVS_NAMESPACE = "test_data";
static const char* NVS_KEY_MODE = "mode";
static const char* NVS_KEY_BATTERY_SRC = "bat_src";
static const char* NVS_KEY_CUSTOM_COUNT = "custom_cnt";
static const char* NVS_KEY_SOC_PROFILE = "soc_prof";
static const char* NVS_KEY_POWER_PROFILE = "pwr_prof";

static Config current_config;
static Preferences prefs;
static bool initialized = false;

bool init() {
    if (initialized) {
        LOG_WARN(TAG, "Already initialized");
        return true;
    }
    
    LOG_INFO(TAG, "Initializing test data configuration system...");
    
    // Try to load from NVS
    if (prefs.begin(NVS_NAMESPACE, false)) {  // false = read/write mode
        current_config.mode = static_cast<Mode>(prefs.getUChar(NVS_KEY_MODE, 
                                                  static_cast<uint8_t>(Mode::OFF)));
        current_config.battery_source = static_cast<BatterySource>(
            prefs.getUChar(NVS_KEY_BATTERY_SRC, 
                          static_cast<uint8_t>(BatterySource::SELECTED_BATTERY)));
        current_config.custom_cell_count = prefs.getUShort(NVS_KEY_CUSTOM_COUNT, 96);
        current_config.soc_profile = static_cast<SocProfile>(
            prefs.getUChar(NVS_KEY_SOC_PROFILE, 
                          static_cast<uint8_t>(SocProfile::TRIANGLE)));
        current_config.power_profile = static_cast<PowerProfile>(
            prefs.getUChar(NVS_KEY_POWER_PROFILE, 
                          static_cast<uint8_t>(PowerProfile::SINE)));
        prefs.end();
        
        LOG_INFO(TAG, "✓ Configuration loaded from NVS");
        LOG_INFO(TAG, "  Mode: %s", mode_to_string(current_config.mode));
        LOG_INFO(TAG, "  Battery source: %s", 
                current_config.battery_source == BatterySource::SELECTED_BATTERY 
                ? "Selected Battery" : "Custom Count");
        if (current_config.battery_source == BatterySource::CUSTOM_COUNT) {
            LOG_INFO(TAG, "  Custom cell count: %u", current_config.custom_cell_count);
        }
    } else {
        LOG_INFO(TAG, "No saved configuration, using defaults");
        LOG_INFO(TAG, "  Mode: OFF (no test data)");
    }
    
    initialized = true;
    return true;
}

const Config& get_config() {
    return current_config;
}

bool set_config(const Config& config, bool persist) {
    if (!initialized) {
        LOG_ERROR(TAG, "Not initialized, call init() first");
        return false;
    }
    
    // Validate configuration
    if (config.mode > Mode::FULL_BATTERY_DATA) {
        LOG_ERROR(TAG, "Invalid mode: %u", static_cast<uint8_t>(config.mode));
        return false;
    }
    
    if (config.battery_source == BatterySource::CUSTOM_COUNT) {
        if (config.custom_cell_count < 1 || config.custom_cell_count > 200) {
            LOG_ERROR(TAG, "Invalid custom cell count: %u (must be 1-200)", 
                     config.custom_cell_count);
            return false;
        }
    }
    
    // Update current configuration
    current_config = config;
    
    LOG_INFO(TAG, "Configuration updated:");
    LOG_INFO(TAG, "  Mode: %s", mode_to_string(current_config.mode));
    LOG_INFO(TAG, "  Battery source: %s", 
            current_config.battery_source == BatterySource::SELECTED_BATTERY 
            ? "Selected Battery" : "Custom Count");
    if (current_config.battery_source == BatterySource::CUSTOM_COUNT) {
        LOG_INFO(TAG, "  Custom cell count: %u", current_config.custom_cell_count);
    }
    LOG_INFO(TAG, "  SOC profile: %u", static_cast<uint8_t>(current_config.soc_profile));
    LOG_INFO(TAG, "  Power profile: %u", static_cast<uint8_t>(current_config.power_profile));
    
    // Persist to NVS if requested
    if (persist) {
        if (prefs.begin(NVS_NAMESPACE, false)) {
            prefs.putUChar(NVS_KEY_MODE, static_cast<uint8_t>(current_config.mode));
            prefs.putUChar(NVS_KEY_BATTERY_SRC, static_cast<uint8_t>(current_config.battery_source));
            prefs.putUShort(NVS_KEY_CUSTOM_COUNT, current_config.custom_cell_count);
            prefs.putUChar(NVS_KEY_SOC_PROFILE, static_cast<uint8_t>(current_config.soc_profile));
            prefs.putUChar(NVS_KEY_POWER_PROFILE, static_cast<uint8_t>(current_config.power_profile));
            prefs.end();
            LOG_INFO(TAG, "✓ Configuration saved to NVS");
        } else {
            LOG_ERROR(TAG, "Failed to open NVS for writing");
            return false;
        }
    }
    
    return true;
}

bool apply_config() {
    if (!initialized) {
        LOG_ERROR(TAG, "Not initialized, call init() first");
        return false;
    }
    
    LOG_INFO(TAG, "Applying test data configuration...");
    
    // Apply based on mode
    switch (current_config.mode) {
        case Mode::OFF:
            LOG_INFO(TAG, "Test data generation disabled");
            TestDataGenerator::set_enabled(false);
            break;
            
        case Mode::SOC_POWER_ONLY:
            LOG_INFO(TAG, "Enabling SOC/power generation only (no cells)");
            TestDataGenerator::set_enabled(true);
            TestDataGenerator::set_cell_generation_enabled(false);
            TestDataGenerator::update();  // Reinitialize
            break;
            
        case Mode::FULL_BATTERY_DATA:
            LOG_INFO(TAG, "Enabling full battery data generation (SOC/power + cells)");
            TestDataGenerator::set_enabled(true);
            TestDataGenerator::set_cell_generation_enabled(true);
            TestDataGenerator::update();  // Reinitialize
            break;
    }
    
    uint16_t cell_count = get_effective_cell_count();
    LOG_INFO(TAG, "✓ Configuration applied, effective cell count: %u", cell_count);
    
    return true;
}

bool reset_to_defaults(bool persist) {
    LOG_INFO(TAG, "Resetting to default configuration...");
    Config defaults;
    return set_config(defaults, persist);
}

bool is_enabled() {
    return current_config.mode != Mode::OFF;
}

bool should_generate_cells() {
    return current_config.mode == Mode::FULL_BATTERY_DATA;
}

uint16_t get_effective_cell_count() {
    if (current_config.battery_source == BatterySource::CUSTOM_COUNT) {
        return current_config.custom_cell_count;
    } else {
        // Use battery's configured cell count
        return datalayer.battery.info.number_of_cells;
    }
}

const char* mode_to_string(Mode mode) {
    switch (mode) {
        case Mode::OFF: return "OFF";
        case Mode::SOC_POWER_ONLY: return "SOC_POWER_ONLY";
        case Mode::FULL_BATTERY_DATA: return "FULL_BATTERY_DATA";
        default: return "UNKNOWN";
    }
}

Mode string_to_mode(const char* str) {
    if (strcmp(str, "OFF") == 0) return Mode::OFF;
    if (strcmp(str, "SOC_POWER_ONLY") == 0) return Mode::SOC_POWER_ONLY;
    if (strcmp(str, "FULL_BATTERY_DATA") == 0) return Mode::FULL_BATTERY_DATA;
    return Mode::OFF;  // Default to OFF if invalid
}

bool get_config_json(char* buffer, size_t buffer_size) {
    StaticJsonDocument<512> doc;
    
    doc["mode"] = mode_to_string(current_config.mode);
    doc["battery_source"] = (current_config.battery_source == BatterySource::SELECTED_BATTERY) 
                            ? "SELECTED_BATTERY" : "CUSTOM_COUNT";
    doc["custom_cell_count"] = current_config.custom_cell_count;
    doc["effective_cell_count"] = get_effective_cell_count();
    doc["soc_profile"] = static_cast<uint8_t>(current_config.soc_profile);
    doc["power_profile"] = static_cast<uint8_t>(current_config.power_profile);
    
    // Add data generation status
    JsonObject data_gen = doc.createNestedObject("data_generated");
    data_gen["soc"] = is_enabled();
    data_gen["power"] = is_enabled();
    data_gen["cells"] = should_generate_cells();
    
    // Add transport info
    JsonObject transport = doc.createNestedObject("transport");
    transport["soc_power_via"] = "ESP_NOW";
    transport["cells_via"] = "MQTT";
    
    size_t len = serializeJson(doc, buffer, buffer_size);
    return len > 0 && len < buffer_size;
}

bool set_config_from_json(const char* json, bool persist) {
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, json);
    
    if (error) {
        LOG_ERROR(TAG, "JSON parse error: %s", error.c_str());
        return false;
    }
    
    Config new_config = current_config;  // Start with current config
    
    // Parse mode
    if (doc.containsKey("mode")) {
        const char* mode_str = doc["mode"];
        new_config.mode = string_to_mode(mode_str);
    }
    
    // Parse battery source
    if (doc.containsKey("battery_source")) {
        const char* src = doc["battery_source"];
        new_config.battery_source = (strcmp(src, "CUSTOM_COUNT") == 0) 
                                    ? BatterySource::CUSTOM_COUNT 
                                    : BatterySource::SELECTED_BATTERY;
    }
    
    // Parse custom cell count
    if (doc.containsKey("custom_cell_count")) {
        new_config.custom_cell_count = doc["custom_cell_count"];
    }
    
    // Parse profiles
    if (doc.containsKey("soc_profile")) {
        new_config.soc_profile = static_cast<SocProfile>(doc["soc_profile"].as<uint8_t>());
    }
    
    if (doc.containsKey("power_profile")) {
        new_config.power_profile = static_cast<PowerProfile>(doc["power_profile"].as<uint8_t>());
    }
    
    return set_config(new_config, persist);
}

} // namespace TestDataConfig
