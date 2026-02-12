#pragma once

#include <Arduino.h>
#include <espnow_common.h>

/**
 * @brief Manages periodic version beacons for cache synchronization
 * 
 * Sends lightweight version beacons (~20 bytes) every 15 seconds containing:
 * - Configuration version numbers (MQTT, Network, Battery, Power Profile)
 * - Runtime status (MQTT connected, Ethernet link status)
 * 
 * Receiver compares beacon versions with its cache and requests updated
 * config sections only when versions don't match. This minimizes bandwidth
 * while ensuring receiver always has current configuration data.
 * 
 * Event-driven updates sent immediately when:
 * - MQTT connection state changes
 * - Ethernet link state changes
 * - Any configuration version changes (config saved)
 */
class VersionBeaconManager {
public:
    static VersionBeaconManager& instance();
    
    /**
     * @brief Initialize the version beacon manager
     * Sends initial beacon immediately
     */
    void init();
    
    /**
     * @brief Notify that MQTT connection state changed
     * Triggers immediate beacon transmission
     */
    void notify_mqtt_connected(bool connected);
    
    /**
     * @brief Notify that Ethernet link state changed
     * Triggers immediate beacon transmission
     */
    void notify_ethernet_changed(bool connected);
    
    /**
     * @brief Notify that a configuration version changed
     * Triggers immediate beacon transmission
     * @param section The config section that changed
     */
    void notify_config_version_changed(config_section_t section);
    
    /**
     * @brief Periodic update - call from main loop
     * Sends periodic heartbeat beacon every 15 seconds
     */
    void update();
    
    /**
     * @brief Handle config section request from receiver
     * @param request The config request message
     * @param sender_mac MAC address of the receiver requesting config
     */
    void handle_config_request(const config_section_request_t* request, const uint8_t* sender_mac);
    
    /**
     * @brief Send version beacon to receiver (can be called directly when needed)
     * @param force If true, send even if no runtime state changed
     */
    void send_version_beacon(bool force = false);
    
private:
    VersionBeaconManager() = default;
    
    // Send specific config section in response to request
    void send_config_section(config_section_t section, const uint8_t* receiver_mac);
    
    // Check if runtime state has changed since last beacon
    bool has_runtime_state_changed();
    
    // Get current version number for a config section
    uint32_t get_config_version(config_section_t section);
    
    // Current runtime state
    bool mqtt_connected_{false};
    bool ethernet_connected_{false};
    
    // Previous runtime state (for change detection)
    bool prev_mqtt_connected_{false};
    bool prev_ethernet_connected_{false};
    
    // Timing
    uint32_t last_beacon_ms_{0};
    static constexpr uint32_t PERIODIC_INTERVAL_MS = 15000;  // 15 seconds
    static constexpr uint32_t MIN_BEACON_INTERVAL_MS = 1000; // Rate limit (1s minimum)
};
