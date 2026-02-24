#ifndef TRANSMISSION_SELECTOR_H
#define TRANSMISSION_SELECTOR_H

#include <Arduino.h>
#include <ArduinoJson.h>

/**
 * @brief Intelligent transmission method selector for dual ESP-NOW/MQTT support
 * 
 * Phase 2 Implementation: Smart routing based on payload size and network availability
 * 
 * Routing Strategy:
 * - Small payloads (<250B): Use ESP-NOW (fast, local, reliable for battery data)
 * - Large payloads (≥250B): Use MQTT (handles big cell arrays, 711B typical)
 * - Redundancy mode: Send both simultaneously (when enabled)
 * - Failover: If MQTT down, buffer for ESP-NOW; if ESP-NOW down, send full payload to MQTT
 * 
 * Transmission Methods:
 * 1. ESPNOW_ONLY: Pure ESP-NOW (legacy, backward compatible)
 * 2. MQTT_ONLY: Pure MQTT (for display-only receivers)
 * 3. SMART (default): Route based on payload size
 * 4. REDUNDANT: Send via both methods (doubles overhead but maximum reliability)
 * 
 * Payload Examples:
 * - spec_data (static battery info): 180-250B → ESP-NOW
 * - spec_data_2 (inverter info): 150-200B → ESP-NOW  
 * - battery_specs (spec array): 120-150B → ESP-NOW
 * - cell_data (96 cells + balancing): 711B → MQTT (too large for ESP-NOW)
 * - dynamic_data (SOC/power update): 40-60B → ESP-NOW
 */

namespace TransmissionSelector {

/**
 * @brief Transmission method options
 */
enum class TransmissionMode : uint8_t {
    ESPNOW_ONLY,    // 0: ESP-NOW only (backward compatible)
    MQTT_ONLY,      // 1: MQTT only (display receiver)
    SMART,          // 2: Intelligent routing (default) - chooses based on size
    REDUNDANT       // 3: Both ESP-NOW and MQTT simultaneously
};

/**
 * @brief Transmission result for status tracking
 */
struct TransmissionResult {
    bool espnow_sent;    // ESP-NOW transmission success
    bool mqtt_sent;      // MQTT transmission success
    size_t payload_size; // Original payload size in bytes
    const char* method;  // Method used: "ESP-NOW", "MQTT", "BOTH", "BUFFERED", "FAILED"
};

/**
 * @brief Initialize transmission selector with configured mode
 * 
 * Must be called during system startup after both ESP-NOW and MQTT are initialized
 */
void init(TransmissionMode mode = TransmissionMode::SMART);

/**
 * @brief Set transmission mode at runtime
 * 
 * Allows switching between transmission methods without reboot
 * Useful for dynamic network condition adaptation
 */
void set_mode(TransmissionMode mode);

/**
 * @brief Get current transmission mode
 */
TransmissionMode get_mode();

/**
 * @brief Intelligent transmission for battery spec data (static, small)
 * 
 * Typical payload: 150-250B (spec_data or spec_data_2)
 * Expected route: ESP-NOW (direct) or MQTT (if ESP-NOW unavailable)
 * 
 * @param json_doc: ArduinoJson document with spec data
 * @param topic: Optional MQTT topic name for logging ("spec_data", "spec_data_2", "battery_specs")
 * @return TransmissionResult with success status and method used
 */
TransmissionResult transmit_specs(const JsonObject& json_doc, const char* topic = nullptr);

/**
 * @brief Intelligent transmission for dynamic data (small, frequent)
 * 
 * Typical payload: 40-60B (SOC, power, timestamp)
 * Expected route: ESP-NOW (always, very small)
 * 
 * @param soc: State of charge percentage (0-100)
 * @param power: Power in watts (signed, negative for discharge)
 * @param timestamp: ISO8601 timestamp string
 * @return TransmissionResult with success status
 */
TransmissionResult transmit_dynamic_data(int soc, long power, const char* timestamp);

/**
 * @brief Intelligent transmission for cell data (large, periodic)
 * 
 * Typical payload: 711B (96 cell voltages + balancing array)
 * Expected route: MQTT (too large for ESP-NOW 250B limit)
 * Falls back to buffering if MQTT temporarily unavailable
 * 
 * @param json_doc: ArduinoJson document with cell data
 * @return TransmissionResult with success status and method used
 */
TransmissionResult transmit_cell_data(const JsonObject& json_doc);

/**
 * @brief Query if a payload should use ESP-NOW (based on size threshold)
 * 
 * Threshold: 250 bytes (ESP-NOW max in practice)
 * Safety margin: 230B (leaves 20B for protocol overhead)
 * 
 * @param payload_size: Size of payload in bytes
 * @return true if payload is small enough for ESP-NOW, false if should use MQTT
 */
bool should_use_espnow(size_t payload_size);

/**
 * @brief Check if both transmission methods are currently available
 * 
 * @return true if both ESP-NOW and MQTT are ready, false if one or both unavailable
 */
bool are_both_methods_available();

/**
 * @brief Get statistics about transmission method usage
 * 
 * @param espnow_count: [out] Total ESP-NOW transmissions
 * @param mqtt_count: [out] Total MQTT transmissions
 * @param redundant_count: [out] Messages sent via both methods
 * @param avg_espnow_latency_ms: [out] Average ESP-NOW transmission time
 * @param avg_mqtt_latency_ms: [out] Average MQTT transmission time
 */
void get_statistics(uint32_t& espnow_count, uint32_t& mqtt_count, uint32_t& redundant_count,
                   float& avg_espnow_latency_ms, float& avg_mqtt_latency_ms);

/**
 * @brief Reset transmission statistics
 */
void reset_statistics();

/**
 * @brief Get last transmission result (for debugging)
 * 
 * @return Most recent TransmissionResult from any transmit_* function
 */
TransmissionResult get_last_result();

} // namespace TransmissionSelector

#endif // TRANSMISSION_SELECTOR_H
