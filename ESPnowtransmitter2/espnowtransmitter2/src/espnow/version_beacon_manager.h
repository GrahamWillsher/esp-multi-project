#pragma once

#include <Arduino.h>
#include <esp32common/espnow/common.h>

/**
 * @brief Manages version beacon publication.
 *
 * Version beacons (~107 bytes) are sent every 30 seconds and immediately on
 * runtime-state or config-version changes so the receiver can request stale
 * sections on demand.
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
     * @brief Periodic update - call from main loop.
     * Sends version beacon every 30 seconds and checks for snapshot revision
     * changes (revision change triggers immediate re-send).
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
     * @return true if beacon was sent successfully, false if send was skipped/failed
     */
    bool send_version_beacon(bool force = false);

private:
    VersionBeaconManager() = default;

    bool can_send_beacon_now() const;
    
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
    bool pending_beacon_{false};
    static constexpr uint32_t PERIODIC_INTERVAL_MS = 30000;  // 30 seconds
    static constexpr uint32_t MIN_BEACON_INTERVAL_MS = 1000; // Rate limit (1s minimum)

};
