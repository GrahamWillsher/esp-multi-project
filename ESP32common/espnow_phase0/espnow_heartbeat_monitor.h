#ifndef ESPNOW_HEARTBEAT_MONITOR_H
#define ESPNOW_HEARTBEAT_MONITOR_H

#include <cstdint>

/**
 * @class EspNowHeartbeatMonitor
 * 
 * Monitors ESP-NOW heartbeat for connection health detection.
 * Triggers connection loss detection after 5-second timeout without heartbeat.
 * 
 * ARCHITECTURE:
 * - Detects connection loss: No heartbeat for 5 seconds = dead connection
 * - Triggers recovery: Returns connection_lost() = true to initiate reconnection
 * - Tracks metrics: Last heartbeat time, timeout count, connection duration
 * - Graceful degradation: Works with missed heartbeats in lossy links
 * 
 * USAGE FLOW:
 *   1. Constructor: Initialize with timeout threshold (default 5000 ms)
 *   2. on_heartbeat_received(): Call when heartbeat packet arrives
 *   3. on_connection_success(): Call after successful ESP-NOW connect
 *   4. connection_lost(): Check if timeout exceeded - drives reconnection
 *   5. reset(): Clear state for fresh monitoring
 * 
 * EXAMPLE:
 *   EspNowHeartbeatMonitor monitor(5000);  // 5 second timeout
 *   
 *   // In connection handler:
 *   monitor.on_connection_success();
 *   
 *   // In main loop:
 *   if (monitor.connection_lost()) {
 *       // Trigger reconnection logic
 *   }
 *   
 *   // When heartbeat arrives:
 *   monitor.on_heartbeat_received();
 * 
 * HEARTBEAT PACKET STRUCTURE:
 * - Sender: Transmitter device
 * - Frequency: Every 5 seconds in normal operation
 * - Payload: Simple marker (1 byte) to avoid parse overhead
 * - Handler: Receiver connection manager processes and updates monitor
 * 
 * TIMEOUT SCENARIOS:
 * 1. Normal operation: Heartbeat every 5s, monitor resets each time
 * 2. Receiver reboot: Transmitter continues heartbeating, receiver detects after 5s
 * 3. Transmitter network loss: All ESP-NOW transmission fails, timeout triggers
 * 4. Lossy link: Some heartbeats lost but recovery happens before 5s threshold
 * 5. Radio interference: Brief interruptions don't trigger (< 5s duration)
 * 
 * METRICS PROVIDED:
 * - ms_since_last_heartbeat(): Time elapsed since last packet
 * - timeout_count(): Total times connection was lost and recovered
 * - connection_duration_seconds(): How long current connection has been stable
 */
class EspNowHeartbeatMonitor {
public:
    // 5 second timeout - matches typical ESP-NOW stability requirements
    static constexpr uint32_t DEFAULT_TIMEOUT_MS = 5000;
    
    /**
     * Initialize heartbeat monitor with configurable timeout.
     * @param timeout_ms Milliseconds before declaring connection lost (default 5000ms)
     */
    explicit EspNowHeartbeatMonitor(uint32_t timeout_ms = DEFAULT_TIMEOUT_MS);
    
    /**
     * Called when heartbeat packet is received from peer.
     * Resets the timeout counter.
     */
    void on_heartbeat_received();
    
    /**
     * Called when ESP-NOW connection is successfully established.
     * Initializes heartbeat monitoring window.
     */
    void on_connection_success();
    
    /**
     * Check if connection should be considered lost.
     * Returns true if no heartbeat received within timeout window.
     * 
     * @return true if heartbeat timeout exceeded, false otherwise
     */
    bool connection_lost() const;
    
    /**
     * Get milliseconds since last heartbeat received.
     * @return Elapsed time in milliseconds
     */
    uint32_t ms_since_last_heartbeat() const;
    
    /**
     * Get number of times connection was lost and recovered.
     * Useful for diagnostics and logging.
     * @return Total timeout count
     */
    uint32_t timeout_count() const { return timeout_count_; }
    
    /**
     * Get duration of current stable connection in seconds.
     * Resets to 0 when connection is lost.
     * @return Connection duration in seconds
     */
    uint32_t connection_duration_seconds() const;
    
    /**
     * Reset heartbeat monitor to initial state.
     * Called during initialization or when restarting connection.
     */
    void reset();
    
    /**
     * Get current timeout threshold in milliseconds.
     * @return Timeout value
     */
    uint32_t get_timeout_ms() const { return timeout_ms_; }

private:
    uint32_t timeout_ms_;              // Timeout threshold in milliseconds
    uint32_t last_heartbeat_time_;     // millis() when last heartbeat received
    uint32_t connection_start_time_;   // millis() when connection established
    uint32_t timeout_count_;           // Count of connection loss events
    bool connection_established_;      // Whether connection is currently valid
};

#endif // ESPNOW_HEARTBEAT_MONITOR_H
