#pragma once
#include <cstdint>

/**
 * @brief ESP-NOW Connection State Machine
 * 
 * Unified state machine for both transmitter and receiver to track
 * ESP-NOW connection lifecycle. Ensures both devices handle connection
 * loss, discovery, and reconnection in a coordinated manner.
 * 
 * State Transitions:
 * UNINITIALIZED → INITIALIZING → IDLE → DISCOVERY_INITIATING → DISCOVERY_IN_PROGRESS → CONNECTED
 *                                              ↓
 *                                         DISCOVERY_TIMEOUT → ERROR_STATE
 * 
 * CONNECTED → PEER_NOT_RESPONDING → RECONNECTING → RECONNECT_BACKOFF → DISCOVERY_INITIATING
 *                                        ↓
 *                                   CONNECTION_LOST → ERROR_STATE
 * 
 * Any state → DISCONNECTING → DISCONNECTED (intentional disconnect)
 */
enum class EspNowConnectionState : uint8_t {
    // Initialization Phase
    UNINITIALIZED = 0,         // Before init() called
    INITIALIZING = 1,          // WiFi/radio starting up
    
    // Discovery Phase
    IDLE = 2,                  // Ready but not discovering
    DISCOVERY_INITIATING = 3,  // Starting discovery process
    DISCOVERY_IN_PROGRESS = 4, // Actively scanning/probing
    DISCOVERY_TIMEOUT = 5,     // Discovery took too long
    
    // Connection Established
    CONNECTED = 6,             // Peer found, link established
    SYNCING_CACHE = 7,         // Connected, syncing data
    
    // Connection Issues
    PEER_NOT_RESPONDING = 8,   // Was connected, no heartbeat
    RECONNECTING = 9,          // Trying to re-establish
    RECONNECT_BACKOFF = 10,    // Waiting before retry
    
    // Error States
    DISCOVERY_FAILED = 11,     // Gave up on discovery
    CONNECTION_LOST = 12,      // Was connected, now offline
    ERROR_UNRECOVERABLE = 13,  // Fatal error
    
    // Shutdown
    DISCONNECTING = 14,        // Intentional disconnect
    DISCONNECTED = 15          // Offline and idle
};

/**
 * @brief Connection health metrics for diagnostics
 */
struct EspNowConnectionMetrics {
    uint32_t total_successful_connections = 0;
    uint32_t total_failed_discovery_attempts = 0;
    uint32_t total_connection_losses = 0;
    uint32_t successful_reconnections = 0;
    uint32_t total_uptime_ms = 0;
    uint32_t total_downtime_ms = 0;
    uint32_t current_connection_duration_ms = 0;
    uint32_t heartbeat_failures = 0;
    uint32_t discovery_retries = 0;
    uint32_t last_state_change_time_ms = 0;
    
    /**
     * @brief Calculate connection reliability (0-100%)
     * @return Percentage of time connected vs total time
     */
    uint8_t get_reliability_percent() const {
        if (total_uptime_ms + total_downtime_ms == 0) return 0;
        return (total_uptime_ms * 100) / (total_uptime_ms + total_downtime_ms);
    }
};

/**
 * @brief Convert state enum to human-readable string
 * @param state The connection state
 * @return String representation of state
 */
const char* espnow_state_to_string(EspNowConnectionState state);

/**
 * @brief Convert string to state enum
 * @param state_str String representation of state
 * @return Connection state enum value
 */
EspNowConnectionState espnow_string_to_state(const char* state_str);
