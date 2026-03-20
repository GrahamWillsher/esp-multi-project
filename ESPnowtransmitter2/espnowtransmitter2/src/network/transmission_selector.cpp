#include "transmission_selector.h"
#include "mqtt_manager.h"
#include "../espnow/message_handler.h"
#include "../config/logging_config.h"
#include <Arduino.h>
#include <espnow_transmitter.h>

namespace TransmissionSelector {

// Configuration
static TransmissionMode current_mode = TransmissionMode::SMART;
static constexpr size_t ESPNOW_PAYLOAD_THRESHOLD = 230;  // Safe limit (250 - 20 byte margin)

// Statistics
static struct {
    uint32_t espnow_count = 0;
    uint32_t mqtt_count = 0;
    uint32_t redundant_count = 0;
    uint32_t espnow_total_latency_ms = 0;
    uint32_t mqtt_total_latency_ms = 0;
    TransmissionResult last_result = {false, false, 0, "INIT"};
} stats;

void init(TransmissionMode mode) {
    current_mode = mode;
    memset(&stats, 0, sizeof(stats));
    
    const char* mode_str = "UNKNOWN";
    switch (mode) {
        case TransmissionMode::ESPNOW_ONLY: mode_str = "ESPNOW_ONLY"; break;
        case TransmissionMode::MQTT_ONLY: mode_str = "MQTT_ONLY"; break;
        case TransmissionMode::SMART: mode_str = "SMART"; break;
        case TransmissionMode::REDUNDANT: mode_str = "REDUNDANT"; break;
    }
    
    LOG_INFO("TRANSMISSION_SELECTOR", "Initialized with mode: %s", mode_str);
}

void set_mode(TransmissionMode mode) {
    current_mode = mode;
    
    const char* mode_str = "UNKNOWN";
    switch (mode) {
        case TransmissionMode::ESPNOW_ONLY: mode_str = "ESPNOW_ONLY"; break;
        case TransmissionMode::MQTT_ONLY: mode_str = "MQTT_ONLY"; break;
        case TransmissionMode::SMART: mode_str = "SMART"; break;
        case TransmissionMode::REDUNDANT: mode_str = "REDUNDANT"; break;
    }
    
    LOG_INFO("TRANSMISSION_SELECTOR", "Transmission mode changed to: %s", mode_str);
}

TransmissionMode get_mode() {
    return current_mode;
}

bool should_use_espnow(size_t payload_size) {
    return payload_size <= ESPNOW_PAYLOAD_THRESHOLD;
}

bool are_both_methods_available() {
    bool espnow_available = EspnowMessageHandler::instance().is_transmission_active();
    bool mqtt_available = MqttManager::instance().is_connected();
    return espnow_available && mqtt_available;
}

TransmissionResult transmit_specs(const JsonObject& json_doc, const char* topic) {
    TransmissionResult result = {false, false, 0, "SPECS"};
    unsigned long start_time = millis();
    
    // Serialize to temporary buffer to measure size
    char temp_buffer[512];
    size_t len = serializeJson(json_doc, temp_buffer, sizeof(temp_buffer));
    result.payload_size = len;
    
    bool topic_for_logging = false;
    if (topic == nullptr) {
        topic = "specs";
        topic_for_logging = true;
    }
    
    LOG_DEBUG("TRANSMISSION_SELECTOR", "Transmitting %s (%u bytes, threshold:%u)", 
              topic, len, ESPNOW_PAYLOAD_THRESHOLD);
    
    // Route based on current mode
    switch (current_mode) {
        case TransmissionMode::ESPNOW_ONLY:
            // Route planning only: actual payload transmission is handled elsewhere
            if (EspnowMessageHandler::instance().is_transmission_active()) {
                result.espnow_sent = true;
                result.method = "ESP-NOW_ROUTE";
                stats.espnow_count++;
                LOG_DEBUG("TRANSMISSION_SELECTOR", "✓ Specs routed to ESP-NOW (%s)", topic);
            } else {
                result.method = "FAILED";
                LOG_WARN("TRANSMISSION_SELECTOR", "✗ No ESP-NOW route available (%s)", topic);
            }
            break;
            
        case TransmissionMode::MQTT_ONLY:
            if (MqttManager::instance().is_connected()) {
                // Route planning only: specs publish is executed by specific MQTT publish functions
                result.mqtt_sent = true;
                result.method = "MQTT_ROUTE";
                stats.mqtt_count++;
                LOG_DEBUG("TRANSMISSION_SELECTOR", "✓ Specs routed to MQTT (%s)", topic);
            } else {
                result.method = "BUFFERED";
                LOG_WARN("TRANSMISSION_SELECTOR", "✗ MQTT route unavailable (%s)", topic);
            }
            break;
            
        case TransmissionMode::SMART:
            // Smart routing: small specs → ESP-NOW ready, large specs → MQTT
            if (should_use_espnow(len) && EspnowMessageHandler::instance().is_transmission_active()) {
                result.espnow_sent = true;
                result.method = "ESP-NOW_ROUTE";
                stats.espnow_count++;
                stats.espnow_total_latency_ms += (millis() - start_time);
                LOG_DEBUG("TRANSMISSION_SELECTOR", "✓ Specs routed to ESP-NOW (SMART) (%s)", topic);
            } else if (MqttManager::instance().is_connected()) {
                result.mqtt_sent = true;
                result.method = "MQTT_ROUTE";
                stats.mqtt_count++;
                stats.mqtt_total_latency_ms += (millis() - start_time);
                LOG_DEBUG("TRANSMISSION_SELECTOR", "✓ Specs routed to MQTT (SMART) (%s)", topic);
            } else {
                result.method = "BUFFERED";
                LOG_WARN("TRANSMISSION_SELECTOR", "✗ No route available (%s)", topic);
            }
            break;
            
        case TransmissionMode::REDUNDANT:
            // Send via both methods
            if (EspnowMessageHandler::instance().is_transmission_active()) {
                result.espnow_sent = true;
                if (result.espnow_sent) stats.espnow_count++;
            }
            if (MqttManager::instance().is_connected()) {
                result.mqtt_sent = true;
                stats.mqtt_count++;
            }
            if (result.espnow_sent && result.mqtt_sent) {
                stats.redundant_count++;
                result.method = "BOTH_ROUTE";
                LOG_DEBUG("TRANSMISSION_SELECTOR", "✓ Specs routed via BOTH (REDUNDANT) (%s)", topic);
            } else if (result.espnow_sent) {
                result.method = "ESP-NOW_ROUTE";
            } else if (result.mqtt_sent) {
                result.method = "MQTT_ROUTE";
            } else {
                result.method = "FAILED";
                LOG_WARN("TRANSMISSION_SELECTOR", "✗ REDUNDANT route selection failed (%s)", topic);
            }
            stats.espnow_total_latency_ms += (millis() - start_time);
            stats.mqtt_total_latency_ms += (millis() - start_time);
            break;
    }
    
    stats.last_result = result;
    return result;
}

TransmissionResult transmit_dynamic_data(int soc, long power, const char* timestamp) {
    TransmissionResult result = {false, false, 0, "DYNAMIC"};
    unsigned long start_time = millis();
    
    // Dynamic data is always small, should fit easily in JSON for ESP-NOW
    // Payload: {"soc":100,"power":-1234,"ts":"2025-02-23T12:34:56Z"} ≈ 40-60 bytes
    
    LOG_TRACE("TRANSMISSION_SELECTOR", "Transmitting dynamic data: SOC=%d%%, Power=%ldW", soc, power);
    
    // Route planning only: dynamic data prefers ESP-NOW when available
    if (EspnowMessageHandler::instance().is_transmission_active()) {
        result.espnow_sent = true;
        result.method = "ESP-NOW_ROUTE";
        stats.espnow_count++;
        stats.espnow_total_latency_ms += (millis() - start_time);
        result.payload_size = 60;  // Approximate size
        LOG_TRACE("TRANSMISSION_SELECTOR", "✓ Dynamic data routed to ESP-NOW");
    } else if (MqttManager::instance().is_connected()) {
        // Fallback route to MQTT if ESP-NOW unavailable
        result.mqtt_sent = true;
        result.method = "MQTT_ROUTE";
        stats.mqtt_count++;
        stats.mqtt_total_latency_ms += (millis() - start_time);
        result.payload_size = 60;
        LOG_DEBUG("TRANSMISSION_SELECTOR", "Dynamic data routed to MQTT fallback");
    } else {
        result.method = "FAILED";
        LOG_WARN("TRANSMISSION_SELECTOR", "✗ No route available for dynamic data");
    }
    
    stats.last_result = result;
    return result;
}

TransmissionResult transmit_cell_data(const JsonObject& json_doc) {
    TransmissionResult result = {false, false, 0, "CELL_DATA"};
    unsigned long start_time = millis();
    
    // Serialize to temporary buffer to measure size
    char temp_buffer[2048];
    size_t len = serializeJson(json_doc, temp_buffer, sizeof(temp_buffer));
    result.payload_size = len;
    
    LOG_DEBUG("TRANSMISSION_SELECTOR", "Transmitting cell data (%u bytes, threshold:%u)", 
              len, ESPNOW_PAYLOAD_THRESHOLD);
    
    // Cell data is typically 711 bytes - exceeds ESP-NOW limit, must use MQTT
    if (len > ESPNOW_PAYLOAD_THRESHOLD) {
        LOG_DEBUG("TRANSMISSION_SELECTOR", "Cell data exceeds ESP-NOW limit, routing to MQTT");
        
        if (MqttManager::instance().is_connected()) {
            result.mqtt_sent = true;
            result.method = "MQTT_ROUTE";
            stats.mqtt_count++;
            stats.mqtt_total_latency_ms += (millis() - start_time);
            LOG_DEBUG("TRANSMISSION_SELECTOR", "✓ Cell data routed to MQTT (%u bytes)", len);
        } else {
            // TODO: In production, implement buffering for MQTT reconnection
            result.method = "BUFFERED";
            LOG_WARN("TRANSMISSION_SELECTOR", "✗ MQTT route unavailable, cell data buffered");
        }
    } else {
        // Small cell payload (shouldn't happen in practice) - use smart routing
        switch (current_mode) {
            case TransmissionMode::ESPNOW_ONLY:
                result.espnow_sent = EspnowMessageHandler::instance().is_transmission_active();
                result.method = result.espnow_sent ? "ESP-NOW_ROUTE" : "FAILED";
                if (result.espnow_sent) stats.espnow_count++;
                break;
                
            case TransmissionMode::MQTT_ONLY:
                result.mqtt_sent = MqttManager::instance().is_connected();
                result.method = result.mqtt_sent ? "MQTT_ROUTE" : "FAILED";
                if (result.mqtt_sent) stats.mqtt_count++;
                break;
                
            case TransmissionMode::SMART:
            case TransmissionMode::REDUNDANT:
            default:
                // Prefer MQTT for cell data (even if small) to preserve ESP-NOW bandwidth
                if (MqttManager::instance().is_connected()) {
                    result.mqtt_sent = true;
                    result.method = "MQTT_ROUTE";
                    stats.mqtt_count++;
                } else if (EspnowMessageHandler::instance().is_transmission_active()) {
                    result.espnow_sent = true;
                    result.method = "ESP-NOW_ROUTE";
                    if (result.espnow_sent) stats.espnow_count++;
                } else {
                    result.method = "FAILED";
                }
                break;
        }
        stats.espnow_total_latency_ms += (millis() - start_time);
        stats.mqtt_total_latency_ms += (millis() - start_time);
    }
    
    stats.last_result = result;
    return result;
}

void get_statistics(uint32_t& espnow_count, uint32_t& mqtt_count, uint32_t& redundant_count,
                   float& avg_espnow_latency_ms, float& avg_mqtt_latency_ms) {
    espnow_count = stats.espnow_count;
    mqtt_count = stats.mqtt_count;
    redundant_count = stats.redundant_count;
    
    avg_espnow_latency_ms = (stats.espnow_count > 0) 
        ? (float)stats.espnow_total_latency_ms / stats.espnow_count 
        : 0.0f;
    avg_mqtt_latency_ms = (stats.mqtt_count > 0) 
        ? (float)stats.mqtt_total_latency_ms / stats.mqtt_count 
        : 0.0f;
}

void reset_statistics() {
    memset(&stats, 0, sizeof(stats));
    LOG_INFO("TRANSMISSION_SELECTOR", "Statistics reset");
}

TransmissionResult get_last_result() {
    return stats.last_result;
}

} // namespace TransmissionSelector
