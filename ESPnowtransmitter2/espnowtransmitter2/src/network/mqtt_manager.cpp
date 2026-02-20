#include "mqtt_manager.h"
#include "ethernet_manager.h"
#include "../config/network_config.h"
#include "../config/logging_config.h"
#include "../datalayer/static_data.h"
#include <Arduino.h>
#include <HTTPUpdate.h>

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
    // Set buffer size to accommodate 710-byte cell data payload + MQTT overhead
    client_.setBufferSize(1024);
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
        bool success = client_.publish("BE/spec_data", buffer, true); // Retained
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
        success = client_.publish("BE/battery_specs", buffer, true); // Retained
        if (success) {
            LOG_DEBUG("MQTT", "Published battery specs (%u bytes)", len);
        }
    }
    
    free(buffer);
    return success;
}

bool MqttManager::publish_cell_data() {
    Serial.println("[MQTT_DEBUG] publish_cell_data() called");
    
    if (!is_connected()) {
        Serial.println("[MQTT_DEBUG] Not connected, skipping cell data publish");
        return false;
    }
    
    Serial.println("[MQTT_DEBUG] Allocating PSRAM buffer for cell data");
    // Allocate buffer in PSRAM (needs 2KB+ for 96 cells)
    char* buffer = (char*)ps_malloc(2048);
    if (!buffer) {
        LOG_ERROR("MQTT", "Failed to allocate PSRAM for cell data");
        Serial.println("[MQTT_DEBUG] PSRAM allocation FAILED");
        return false;
    }
    
    Serial.println("[MQTT_DEBUG] Calling serialize_cell_data()");
    size_t len = StaticData::serialize_cell_data(buffer, 2048);
    Serial.printf("[MQTT_DEBUG] serialize_cell_data() returned %u bytes\n", len);
    
    if (len > 0) {
        Serial.printf("[MQTT_DEBUG] First 200 chars of JSON: %.200s\n", buffer);
    }
    
    bool success = false;
    if (len > 0) {
        Serial.println("[MQTT_DEBUG] Publishing to BE/cell_data...");
        success = client_.publish("BE/cell_data", buffer, true); // Retained
        if (success) {
            LOG_DEBUG("MQTT", "Published cell data (%u bytes)", len);
            Serial.println("[MQTT_DEBUG] ✓ Publish successful!");
        } else {
            Serial.println("[MQTT_DEBUG] ✗ Publish FAILED!");
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
        success = client_.publish("BE/spec_data_2", buffer, true); // Retained
        if (success) {
            LOG_DEBUG("MQTT", "Published inverter specs (%u bytes)", len);
        }
    }
    
    free(buffer);
    return success;
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
            delay(1000);
            ESP.restart();
            break;
    }
}

// External C linkage function for MqttConfigManager to query connection status
// This avoids circular header dependencies between lib/mqtt_manager and src/network
extern "C" bool mqtt_manager_is_connected() {
    return MqttManager::instance().is_connected();
}
