#include "mqtt_manager.h"
#include "ethernet_manager.h"
#include "../config/network_config.h"
#include "../config/logging_config.h"
#include "../datalayer/static_data.h"
#include "../battery_emulator/devboard/utils/events.h"
#include <Arduino.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <vector>

MqttManager::MqttManager() : client_(eth_client_) {
}

MqttManager& MqttManager::instance() {
    static MqttManager instance;
    return instance;
}

void MqttManager::init() {
    if (!config::features::MQTT_ENABLED) {
        LOG_INFO("MQTT", "MQTT disabled in configuration");
        return;
    }
    
    LOG_INFO("MQTT", "Initializing MQTT client...");
    // Set buffer size to accommodate cells + event logs (cell_data can be ~6KB)
    client_.setBufferSize(6144);
    client_.setServer(config::get_mqtt_config().server, config::get_mqtt_config().port);
    client_.setCallback(message_callback);
    client_.setKeepAlive(60);
    client_.setSocketTimeout(10);
    LOG_INFO("MQTT", "MQTT client configured (will connect when Ethernet ready)");
}

bool MqttManager::connect() {
    if (!config::features::MQTT_ENABLED) return false;
    
    if (!EthernetManager::instance().is_connected()) {
        LOG_WARN("MQTT", "Ethernet not connected, skipping MQTT connection");
        return false;
    }
    
    LOG_INFO("MQTT", "Attempting connection to %s:%d...", 
                  config::get_mqtt_config().server, config::get_mqtt_config().port);
    
    bool success = false;
    if (strlen(config::get_mqtt_config().username) > 0) {
        success = client_.connect(config::get_mqtt_config().client_id, 
                                 config::get_mqtt_config().username, 
                                 config::get_mqtt_config().password);
    } else {
        success = client_.connect(config::get_mqtt_config().client_id);
    }
    
    if (success) {
        LOG_INFO("MQTT", "Connected to broker");
        connected_ = true;
        
        // Publish connection status
        client_.publish(config::get_mqtt_config().topics.status, "online", true);
        
        // Subscribe to OTA topic
        if (client_.subscribe(config::get_mqtt_config().topics.ota)) {
            LOG_INFO("MQTT", "Subscribed to OTA topic: %s", config::get_mqtt_config().topics.ota);
        } else {
            LOG_ERROR("MQTT", "Failed to subscribe to OTA topic");
        }
    } else {
        LOG_ERROR("MQTT", "Connection failed, rc=%d", client_.state());
        connected_ = false;
    }
    
    return success;
}

bool MqttManager::publish_data(int soc, long power, const char* timestamp, bool eth_connected) {
    if (!is_connected()) return false;
    
    snprintf(payload_buffer_, sizeof(payload_buffer_),
             R"({"soc":%d,"power":%ld,"timestamp":%lu,"time":"%s","eth_connected":%s})",
             soc, power, millis(), timestamp,
             eth_connected ? "true" : "false");
    
    bool success = client_.publish(config::get_mqtt_config().topics.data, payload_buffer_);
    
    if (success) {
        LOG_DEBUG("MQTT", "Published: %s", payload_buffer_);
    } else {
        LOG_ERROR("MQTT", "Publish failed");
    }
    
    return success;
}

void MqttManager::disconnect() {
    if (connected_) {
        LOG_INFO("MQTT", "Disconnecting from broker...");
        client_.publish(config::get_mqtt_config().topics.status, "offline", true);
        client_.disconnect();
        connected_ = false;
        // Give time for disconnect to complete
        delay(100);
        LOG_INFO("MQTT", "Disconnected gracefully");
    }
}

bool MqttManager::publish_status(const char* message, bool retained) {
    if (!is_connected()) return false;
    return client_.publish(config::get_mqtt_config().topics.status, message, retained);
}

bool MqttManager::publish_static_specs() {
    if (!is_connected()) return false;
    
    // Allocate buffer in PSRAM to avoid stack overflow
    char* buffer = (char*)ps_malloc(2048);
    if (!buffer) {
        LOG_ERROR("MQTT", "Failed to allocate PSRAM for static specs");
        return false;
    }
    
    size_t len = StaticData::serialize_all_specs(buffer, 2048);
    
    if (len > 0) {
        bool success = client_.publish("transmitter/BE/spec_data", buffer, true); // Retained
        if (success) {
            LOG_INFO("MQTT", "Published static specs (%u bytes)", len);
        } else {
            LOG_ERROR("MQTT", "Failed to publish static specs");
        }
        free(buffer);
        return success;
    }
    
    LOG_ERROR("MQTT", "Failed to serialize static specs");
    free(buffer);
    return false;
}

bool MqttManager::publish_battery_specs() {
    if (!is_connected()) return false;
    
    // Allocate buffer in PSRAM
    char* buffer = (char*)ps_malloc(512);
    if (!buffer) {
        LOG_ERROR("MQTT", "Failed to allocate PSRAM for battery specs");
        return false;
    }
    
    size_t len = StaticData::serialize_battery_specs(buffer, 512);
    
    bool success = false;
    if (len > 0) {
        success = client_.publish("transmitter/BE/battery_specs", buffer, true); // Retained
        if (success) {
            LOG_DEBUG("MQTT", "Published battery specs (%u bytes)", len);
        }
    }
    
    free(buffer);
    return success;
}

bool MqttManager::publish_cell_data() {
    if (!is_connected()) {
        return false;
    }
    
    // Allocate buffer in PSRAM (needs 6KB for 96 cells + balancing + metadata)
    char* buffer = (char*)ps_malloc(6144);
    if (!buffer) {
        LOG_ERROR("MQTT", "Failed to allocate PSRAM for cell data");
        return false;
    }
    
    size_t len = StaticData::serialize_cell_data(buffer, 6144);
    
    bool success = false;
    if (len > 0) {
        success = client_.publish("transmitter/BE/cell_data", buffer, true); // Retained
        if (success) {
            LOG_DEBUG("MQTT", "Published cell data (%u bytes)", len);
        }
    } else {
        Serial.println("[MQTT_DEBUG] serialize returned 0 bytes, not publishing");
    }
    
    free(buffer);
    return success;
}

bool MqttManager::publish_inverter_specs() {
    if (!is_connected()) return false;
    
    // Allocate buffer in PSRAM
    char* buffer = (char*)ps_malloc(512);
    if (!buffer) {
        LOG_ERROR("MQTT", "Failed to allocate PSRAM for inverter specs");
        return false;
    }
    
    size_t len = StaticData::serialize_inverter_specs(buffer, 512);
    
    bool success = false;
    if (len > 0) {
        success = client_.publish("transmitter/BE/spec_data_2", buffer, true); // Retained
        if (success) {
            LOG_DEBUG("MQTT", "Published inverter specs (%u bytes)", len);
        }
    }
    
    free(buffer);
    return success;
}

static uint8_t map_event_level(EVENTS_LEVEL_TYPE level) {
    switch (level) {
        case EVENT_LEVEL_ERROR:   return 3;
        case EVENT_LEVEL_WARNING: return 4;
        case EVENT_LEVEL_INFO:    return 6;
        case EVENT_LEVEL_DEBUG:   return 7;
        case EVENT_LEVEL_UPDATE:  return 5;
        default:                  return 6;
    }
}

bool MqttManager::publish_event_logs() {
    if (!is_connected()) return false;
    
    // Only publish if there are active subscribers
    if (event_log_subscribers_ <= 0) {
        LOG_DEBUG("MQTT", "No event log subscribers, skipping publish");
        return true;
    }

    std::vector<EventData> ordered;
    ordered.reserve(EVENT_NOF_EVENTS);

    // Collect only events that have been set and NOT yet published (delta mode)
    for (int i = 0; i < EVENT_NOF_EVENTS; i++) {
        const EVENTS_STRUCT_TYPE* event_ptr = get_event_pointer((EVENTS_ENUM_TYPE)i);
        if (event_ptr && event_ptr->occurences > 0 && !event_ptr->MQTTpublished) {
            ordered.push_back({(EVENTS_ENUM_TYPE)i, event_ptr});
        }
    }

    // If no unpublished events, skip publishing
    if (ordered.empty()) {
        LOG_DEBUG("MQTT", "No unpublished events, skipping publish");
        return true;
    }

    std::sort(ordered.begin(), ordered.end(), compareEventsByTimestampDesc);

    const size_t total_events = ordered.size();
    const size_t max_events = (total_events > 100) ? 100 : total_events;

    // Allocate JSON document in PSRAM
    DynamicJsonDocument doc(6144);
    doc["event_count"] = static_cast<uint32_t>(total_events);
    JsonArray events = doc.createNestedArray("events");

    for (size_t i = 0; i < max_events; i++) {
        const auto& item = ordered[i];
        const EVENTS_STRUCT_TYPE* evt = item.event_pointer;
        JsonObject obj = events.createNestedObject();
        obj["timestamp"] = static_cast<uint64_t>(evt->timestamp);
        obj["level"] = map_event_level(evt->level);
        obj["data"] = evt->data;
        obj["message"] = get_event_message_string(item.event_handle);
        obj["event"] = get_event_enum_string(item.event_handle);
    }

    // Allocate buffer in PSRAM for serialization
    char* buffer = (char*)ps_malloc(6144);
    if (!buffer) {
        LOG_ERROR("MQTT", "Failed to allocate PSRAM for event logs");
        return false;
    }

    size_t len = serializeJson(doc, buffer, 6144);
    bool success = false;
    if (len > 0) {
        success = client_.publish("transmitter/BE/event_logs", buffer, true);
        if (success) {
            LOG_DEBUG("MQTT", "Published %u changed event(s) (%u bytes)", (unsigned)max_events, len);
            
            // Mark all published events as published
            for (const auto& item : ordered) {
                set_event_MQTTpublished(item.event_handle);
            }
        } else {
            LOG_ERROR("MQTT", "Failed to publish event logs");
        }
    } else {
        LOG_ERROR("MQTT", "Failed to serialize event logs");
    }

    free(buffer);
    return success;
}

void MqttManager::increment_event_log_subscribers() {
    event_log_subscribers_++;
    LOG_INFO("MQTT", "Event log subscriber count: %d", event_log_subscribers_);
}

void MqttManager::decrement_event_log_subscribers() {
    if (event_log_subscribers_ > 0) {
        event_log_subscribers_--;
        LOG_INFO("MQTT", "Event log subscriber count: %d", event_log_subscribers_);
    }
}

void MqttManager::loop() {
    if (client_.connected()) {
        connected_ = true;
        client_.loop();
    } else {
        connected_ = false;
    }
}

void MqttManager::message_callback(char* topic, byte* payload, unsigned int length) {
    // Null-terminate payload
    char message[256];
    if (length >= sizeof(message)) length = sizeof(message) - 1;
    memcpy(message, payload, length);
    message[length] = '\0';
    LOG_INFO("MQTT", "Message arrived [%s]: %s", topic, message);
    
    // Handle OTA commands
    if (strcmp(topic, config::get_mqtt_config().topics.ota) == 0) {
        instance().handle_ota_command(message);
    }
}

void MqttManager::handle_ota_command(const char* url) {
    LOG_INFO("OTA", "Received OTA command via MQTT");
    
    // Expected format: "http://receiver_ip/ota_firmware.bin"
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        LOG_ERROR("OTA", "Invalid URL format");
        publish_status("ota_invalid_url", false);
        return;
    }
    
    LOG_INFO("OTA", "Starting OTA update from: %s", url);
    
    // Perform OTA update
    WiFiClient client;
    t_httpUpdate_return ret = httpUpdate.update(client, url);
    
    switch (ret) {
        case HTTP_UPDATE_FAILED:
            LOG_ERROR("OTA", "Update failed. Error (%d): %s", 
                        httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
            publish_status("ota_failed", false);
            break;
            
        case HTTP_UPDATE_NO_UPDATES:
            LOG_INFO("OTA", "No updates available");
            publish_status("ota_no_update", false);
            break;
            
        case HTTP_UPDATE_OK:
            LOG_INFO("OTA", "Update successful! Rebooting...");
            publish_status("ota_success", false);
            delay(500);
            // Disconnect MQTT gracefully before reboot
            disconnect();
            delay(500);
            ESP.restart();
            break;
    }
}

// External C linkage function for MqttConfigManager to query connection status
// This avoids circular header dependencies between lib/mqtt_manager and src/network
extern "C" bool mqtt_manager_is_connected() {
    return MqttManager::instance().is_connected();
}
