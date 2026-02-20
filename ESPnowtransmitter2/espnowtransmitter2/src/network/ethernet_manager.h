#pragma once
#include <ETH.h>
#include <WiFi.h>
#include <Preferences.h>
#include <vector>
#include <functional>

/**
 * @brief Ethernet connection state machine (9 states)
 * 
 * Tracks the progression from initialization through cable detection to full connectivity.
 * Handles physical cable detection, IP acquisition, and error recovery.
 * 
 * @see ETHERNET_STATE_MACHINE_TECHNICAL_REFERENCE.md for state transition details
 */
enum class EthernetConnectionState : uint8_t {
    UNINITIALIZED = 0,          // Before init() called
    PHY_RESET = 1,              // Hardware PHY layer being reset
    CONFIG_APPLYING = 2,        // Static IP or DHCP being applied
    LINK_ACQUIRING = 3,         // Waiting for physical link UP (cable detection)
    IP_ACQUIRING = 4,           // Waiting for IP assignment
    CONNECTED = 5,              // Fully ready (link + IP + gateway)
    LINK_LOST = 6,              // Cable disconnected (physical removal detected)
    RECOVERING = 7,             // Retry sequence in progress
    ERROR_STATE = 8             // Unrecoverable failure
};

/**
 * @brief Metrics for state machine diagnostics
 */
struct EthernetStateMetrics {
    uint32_t phy_reset_time_ms = 0;
    uint32_t config_apply_time_ms = 0;
    uint32_t link_acquire_time_ms = 0;
    uint32_t ip_acquire_time_ms = 0;
    uint32_t total_initialization_ms = 0;
    uint32_t connection_established_timestamp = 0;
    
    uint32_t state_transitions = 0;
    uint32_t recoveries_attempted = 0;
    uint32_t recoveries_successful = 0;
    uint32_t link_flaps = 0;                // Times cable was plugged/unplugged
    uint32_t connection_restarts = 0;
};

/**
 * @brief Manages Ethernet connectivity with 9-state machine
 * 
 * Singleton class implementing 9-state Ethernet connection state machine.
 * Handles physical cable detection via ARDUINO_EVENT_ETH_CONNECTED/DISCONNECTED.
 * Properly gates dependent services (NTP, MQTT, OTA, Keep-Alive).
 * 
 * @see ETHERNET_STATE_MACHINE_TECHNICAL_REFERENCE.md for implementation details
 * @see SERVICE_INTEGRATION_GUIDE.md for how to use this with other services
 */
class EthernetManager {
public:
    static EthernetManager& instance();
    
    // =========================================================================
    // Core Initialization & Status
    // =========================================================================
    
    /**
     * @brief Initialize Ethernet with state machine
     * 
     * Transitions: UNINITIALIZED → PHY_RESET → CONFIG_APPLYING
     * Registers event handler for cable detection
     * 
     * @return true if init started successfully, false on hardware error
     */
    bool init();
    
    /**
     * @brief Get current connection state
     * @return Current EthernetConnectionState enum value
     */
    EthernetConnectionState get_state() const { return current_state_; }
    
    /**
     * @brief Get human-readable state name
     * @return State as string (e.g., "CONNECTED", "LINK_LOST")
     */
    const char* get_state_string() const;
    
    /**
     * @brief Check if Ethernet is fully ready for network operations
     * 
     * Returns true ONLY in CONNECTED state (link + IP + gateway present).
     * Use this to gate service initialization (NTP, MQTT, OTA).
     * 
     * @return true if in CONNECTED state, false otherwise
     */
    bool is_fully_ready() const { return current_state_ == EthernetConnectionState::CONNECTED; }
    
    /**
     * @brief Check if Ethernet link is physically present
     * 
     * Returns true if physical cable is detected (in states ≥ LINK_ACQUIRING).
     * Does NOT require IP to be assigned.
     * 
     * @return true if link is up (cable present), false otherwise
     */
    bool is_link_present() const { 
        return current_state_ >= EthernetConnectionState::LINK_ACQUIRING &&
               current_state_ != EthernetConnectionState::ERROR_STATE;
    }
    
    /**
     * @brief Legacy compatibility - same as is_fully_ready()
     * @return true if CONNECTED state
     */
    bool is_connected() const { return is_fully_ready(); }
    
    // =========================================================================
    // State Machine Update & Timeouts
    // =========================================================================
    
    /**
     * @brief Update state machine (call from main loop every 1 second)
     * 
     * Checks for timeouts in each state.
     * Handles automatic transitions (e.g., LINK_LOST → RECOVERING).
     * 
     * Call this periodically from loop() to enable timeout detection.
     */
    void update_state_machine();
    
    /**
     * @brief Get milliseconds spent in current state
     * @return Time since state entry in milliseconds
     */
    uint32_t get_state_age_ms() const;
    
    /**
     * @brief Get previous state (for transitions)
     * @return Previous EthernetConnectionState
     */
    EthernetConnectionState get_previous_state() const { return previous_state_; }
    
    /**
     * @brief Manually set state and record transition
     * 
     * Automatically handles:
     * - Transition logging
     * - Metrics tracking
     * - Callback triggering
     * 
     * @param new_state State to transition to
     */
    void set_state(EthernetConnectionState new_state);
    
    // =========================================================================
    // Network Information
    // =========================================================================
    
    /**
     * @brief Get local IP address
     * @return IPAddress (0.0.0.0 if not connected)
     */
    IPAddress get_local_ip() const;
    
    /**
     * @brief Get gateway IP address
     * @return IPAddress of default gateway
     */
    IPAddress get_gateway_ip() const;
    
    /**
     * @brief Get subnet mask
     * @return IPAddress subnet mask
     */
    IPAddress get_subnet_mask() const;
    
    /**
     * @brief Get DNS server IP
     * @return IPAddress of DNS server
     */
    IPAddress get_dns_ip() const;
    
    /**
     * @brief Get link speed in Mbps
     * @return Link speed (100, 10, 0 if not connected)
     */
    int get_link_speed() const;
    
    // =========================================================================
    // CamelCase Compatibility Methods (for legacy code)
    // =========================================================================
    
    /** @brief Compatibility wrapper - see get_local_ip() */
    IPAddress getStaticIP() const { return static_ip_; }
    
    /** @brief Compatibility wrapper - see get_gateway_ip() */
    IPAddress getGateway() const { return get_gateway_ip(); }
    
    /** @brief Compatibility wrapper - see get_subnet_mask() */
    IPAddress getSubnetMask() const { return get_subnet_mask(); }
    
    /** @brief Compatibility wrapper - see get_dns_ip() */
    IPAddress getDNSPrimary() const { return get_dns_ip(); }
    
    /** @brief Compatibility wrapper - returns secondary DNS (same as primary) */
    IPAddress getDNSSecondary() const { return get_dns_ip(); }
    
    /** @brief Compatibility wrapper - see is_static_ip() */
    bool isStaticIP() const { return is_static_ip(); }
    
    /** @brief Compatibility wrapper - get config version number */
    uint32_t getNetworkConfigVersion() const { return network_config_version_; }
    
    // =========================================================================
    // Network Configuration Methods (both snake_case and camelCase)
    // =========================================================================
    
    /**
     * @brief Test if static IP is reachable
     * @param ip Static IP to test (4 bytes)
     * @param gateway Gateway IP (4 bytes)
     * @param subnet Subnet mask (4 bytes)
     * @param dns DNS IP (4 bytes)
     * @return true if reachable, false if conflict or unreachable
     */
    bool testStaticIPReachability(const uint8_t ip[4], const uint8_t gateway[4],
                                  const uint8_t subnet[4], const uint8_t dns[4]);
    
    /**
     * @brief Check if an IP address conflicts with existing network
     * @param ip IP address to check (4 bytes)
     * @return true if IP conflict detected, false if safe to use
     */
    bool checkIPConflict(const uint8_t ip[4]);
    
    /**
     * @brief Compatibility wrapper - save network config with camelCase
     * @param use_static True for static IP, false for DHCP
     * @param ip Static IP address (4 bytes)
     * @param gateway Gateway IP (4 bytes)
     * @param subnet Subnet mask (4 bytes)
     * @param dns_primary Primary DNS (4 bytes)
     * @param dns_secondary Secondary DNS (4 bytes)
     * @return true if saved successfully
     */
    bool saveNetworkConfig(bool use_static, const uint8_t ip[4],
                          const uint8_t gateway[4], const uint8_t subnet[4],
                          const uint8_t dns_primary[4], const uint8_t dns_secondary[4]);
    
    // =========================================================================
    // Metrics & Diagnostics
    // =========================================================================
    
    /**
     * @brief Get state machine metrics
     * @return Reference to metrics struct
     */
    const EthernetStateMetrics& get_metrics() const { return metrics_; }
    
    /**
     * @brief Get cable flap count
     * @return Number of times cable was plugged/unplugged
     */
    uint32_t get_link_flap_count() const { return metrics_.link_flaps; }
    
    /**
     * @brief Get recovery attempt count
     * @return How many times recovery was attempted after disconnect
     */
    uint32_t get_recovery_attempts() const { return metrics_.recoveries_attempted; }
    
    // =========================================================================
    // Callbacks for Service Gating
    // =========================================================================
    
    /**
     * @brief Register callback for "Ethernet connected" event
     * 
     * Called when Ethernet transitions to CONNECTED state.
     * Use this to start NTP, MQTT, OTA services.
     * 
     * Signature: void callback()
     * 
     * @param callback Function to call on connection
     */
    void on_connected(std::function<void()> callback) {
        connected_callbacks_.push_back(callback);
    }
    
    /**
     * @brief Register callback for "Ethernet disconnected" event
     * 
     * Called when Ethernet transitions to LINK_LOST state.
     * Use this to stop NTP, MQTT, OTA services gracefully.
     * 
     * Signature: void callback()
     * 
     * @param callback Function to call on disconnection
     */
    void on_disconnected(std::function<void()> callback) {
        disconnected_callbacks_.push_back(callback);
    }
    
    /**
     * @brief Trigger connected callbacks
     * @internal Called by event_handler when transitioning to CONNECTED
     */
    void trigger_connected_callbacks();
    
    /**
     * @brief Trigger disconnected callbacks
     * @internal Called by event_handler when transitioning to LINK_LOST
     */
    void trigger_disconnected_callbacks();
    
    // =========================================================================
    // Network Configuration (Static IP / DHCP)
    // =========================================================================
    
    /**
     * @brief Check if using static IP
     * @return true if static IP, false if DHCP
     */
    bool is_static_ip() const { return use_static_ip_; }
    
    /**
     * @brief Load network configuration from NVS
     * @return true if config loaded, false if using defaults
     */
    bool load_network_config();
    
    /**
     * @brief Save network configuration to NVS
     * @param use_static True for static IP, false for DHCP
     * @param ip Static IP address (4 bytes)
     * @param gateway Gateway IP (4 bytes)
     * @param subnet Subnet mask (4 bytes)
     * @param dns_primary Primary DNS (4 bytes)
     * @param dns_secondary Secondary DNS (4 bytes)
     * @return true if saved successfully
     */
    bool save_network_config(bool use_static, const uint8_t ip[4],
                            const uint8_t gateway[4], const uint8_t subnet[4],
                            const uint8_t dns_primary[4], const uint8_t dns_secondary[4]);
    
    /**
     * @brief Load network configuration (camelCase wrapper)
     * @return true if config loaded, false if using defaults
     */
    bool loadNetworkConfig();
    
private:
    EthernetManager();
    ~EthernetManager();
    
    // Prevent copying
    EthernetManager(const EthernetManager&) = delete;
    EthernetManager& operator=(const EthernetManager&) = delete;
    
    // State machine internals
    EthernetConnectionState current_state_ = EthernetConnectionState::UNINITIALIZED;
    EthernetConnectionState previous_state_ = EthernetConnectionState::UNINITIALIZED;
    uint32_t state_enter_time_ms_ = 0;
    uint32_t last_link_time_ms_ = 0;
    uint32_t last_ip_time_ms_ = 0;
    
    // Metrics
    EthernetStateMetrics metrics_;
    
    // Timeouts (in milliseconds)
    static constexpr uint32_t PHY_RESET_TIMEOUT_MS = 5000;
    static constexpr uint32_t CONFIG_APPLY_TIMEOUT_MS = 5000;
    static constexpr uint32_t LINK_ACQUIRING_TIMEOUT_MS = 5000;
    static constexpr uint32_t IP_ACQUIRING_TIMEOUT_MS = 30000;
    static constexpr uint32_t RECOVERY_TIMEOUT_MS = 60000;
    
    // Network configuration
    bool use_static_ip_ = false;
    IPAddress static_ip_{0, 0, 0, 0};
    IPAddress static_gateway_{0, 0, 0, 0};
    IPAddress static_subnet_{0, 0, 0, 0};
    IPAddress static_dns_primary_{0, 0, 0, 0};
    IPAddress static_dns_secondary_{0, 0, 0, 0};
    uint32_t network_config_version_ = 0;
    bool network_config_applied_ = false;
    
    // Callbacks
    std::vector<std::function<void()>> connected_callbacks_;
    std::vector<std::function<void()>> disconnected_callbacks_;
    
    // Event handler (static)
    static void event_handler(WiFiEvent_t event);
    
    // Internal state management
    void check_state_timeout();
    void handle_timeout();
    bool apply_network_config();
    
    friend class EspNowTransmitter;
};

/**
 * @brief Convert state enum to human-readable string
 * @param state EthernetConnectionState value
 * @return String representation
 */
inline const char* ethernet_state_to_string(EthernetConnectionState state) {
    switch (state) {
        case EthernetConnectionState::UNINITIALIZED:    return "UNINITIALIZED";
        case EthernetConnectionState::PHY_RESET:        return "PHY_RESET";
        case EthernetConnectionState::CONFIG_APPLYING:  return "CONFIG_APPLYING";
        case EthernetConnectionState::LINK_ACQUIRING:   return "LINK_ACQUIRING";
        case EthernetConnectionState::IP_ACQUIRING:     return "IP_ACQUIRING";
        case EthernetConnectionState::CONNECTED:        return "CONNECTED";
        case EthernetConnectionState::LINK_LOST:        return "LINK_LOST";
        case EthernetConnectionState::RECOVERING:       return "RECOVERING";
        case EthernetConnectionState::ERROR_STATE:      return "ERROR_STATE";
        default:                                         return "UNKNOWN";
    }
}
