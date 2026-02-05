#pragma once

#include "config_structures.h"
#include <Arduino.h>

/**
 * @brief Configuration manager class
 * 
 * Manages configuration data, version tracking, and provides
 * utilities for updating individual fields.
 */
class ConfigManager {
public:
    ConfigManager();
    
    // Get full configuration snapshot
    const FullConfigSnapshot& getFullConfig() const { return config_; }
    
    // Set full configuration (used when receiving snapshot)
    void setFullConfig(const FullConfigSnapshot& config);
    
    // Update a specific field
    bool updateField(ConfigSection section, uint8_t field_id, 
                    const void* value, uint8_t value_length);
    
    // Get version information
    uint16_t getGlobalVersion() const { return config_.version.global_version; }
    uint16_t getSectionVersion(ConfigSection section) const;
    
    // Increment versions (called when config changes)
    void incrementGlobalVersion();
    void incrementSectionVersion(ConfigSection section);
    
    // Calculate and update checksum
    void updateChecksum();
    
    // Validate checksum
    bool validateChecksum() const;
    
    // Get specific configuration sections (const references)
    const MqttConfig& getMqttConfig() const { return config_.mqtt; }
    const NetworkConfig& getNetworkConfig() const { return config_.network; }
    const BatteryConfig& getBatteryConfig() const { return config_.battery; }
    const PowerConfig& getPowerConfig() const { return config_.power; }
    const InverterConfig& getInverterConfig() const { return config_.inverter; }
    const CanConfig& getCanConfig() const { return config_.can; }
    const ContactorConfig& getContactorConfig() const { return config_.contactor; }
    const SystemConfig& getSystemConfig() const { return config_.system; }
    
    // Get writable references (for direct modification)
    MqttConfig& getMqttConfigRef() { return config_.mqtt; }
    NetworkConfig& getNetworkConfigRef() { return config_.network; }
    BatteryConfig& getBatteryConfigRef() { return config_.battery; }
    PowerConfig& getPowerConfigRef() { return config_.power; }
    InverterConfig& getInverterConfigRef() { return config_.inverter; }
    CanConfig& getCanConfigRef() { return config_.can; }
    ContactorConfig& getContactorConfigRef() { return config_.contactor; }
    SystemConfig& getSystemConfigRef() { return config_.system; }
    
private:
    FullConfigSnapshot config_;
    
    // Helper to update field in a specific section
    bool updateMqttField(uint8_t field_id, const void* value, uint8_t value_length);
    bool updateNetworkField(uint8_t field_id, const void* value, uint8_t value_length);
    bool updateBatteryField(uint8_t field_id, const void* value, uint8_t value_length);
    bool updatePowerField(uint8_t field_id, const void* value, uint8_t value_length);
    bool updateInverterField(uint8_t field_id, const void* value, uint8_t value_length);
    bool updateCanField(uint8_t field_id, const void* value, uint8_t value_length);
    bool updateContactorField(uint8_t field_id, const void* value, uint8_t value_length);
    bool updateSystemField(uint8_t field_id, const void* value, uint8_t value_length);
};
