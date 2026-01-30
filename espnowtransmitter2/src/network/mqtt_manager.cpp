#include "mqtt_manager.h"
#include "ethernet_manager.h"
#include "../config/network_config.h"
#include "../config/logging_config.h"
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
        LOG_INFO("[MQTT] MQTT disabled in configuration");
        return;
    }
    
    LOG_INFO("[MQTT] Initializing MQTT client...");
    client_.setServer(config::mqtt.server, config::mqtt.port);
    client_.setCallback(message_callback);
    client_.setKeepAlive(60);
    client_.setSocketTimeout(10);
    LOG_INFO("[MQTT] MQTT client configured (will connect when Ethernet ready)");
}

bool MqttManager::connect() {
    if (!config::features::MQTT_ENABLED) return false;
    
    if (!EthernetManager::instance().is_connected()) {
        LOG_WARN("[MQTT] Ethernet not connected, skipping MQTT connection");
        return false;
    }
    
    LOG_INFO("[MQTT] Attempting connection to %s:%d...", 
                  config::mqtt.server, config::mqtt.port);
    
    bool success = false;
    if (strlen(config::mqtt.username) > 0) {
        success = client_.connect(config::mqtt.client_id, 
                                 config::mqtt.username, 
                                 config::mqtt.password);
    } else {
        success = client_.connect(config::mqtt.client_id);
    }
    
    if (success) {
        LOG_INFO("[MQTT] Connected to broker");
        connected_ = true;
        
        // Publish connection status
        client_.publish(config::mqtt.topics.status, "online", true);
        
        // Subscribe to OTA topic
        if (client_.subscribe(config::mqtt.topics.ota)) {
            LOG_INFO("[MQTT] Subscribed to OTA topic: %s", config::mqtt.topics.ota);
        } else {
            LOG_ERROR("[MQTT] Failed to subscribe to OTA topic");
        }
    } else {
        LOG_ERROR("[MQTT] Connection failed, rc=%d", client_.state());
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
    
    bool success = client_.publish(config::mqtt.topics.data, payload_buffer_);
    
    if (success) {
        LOG_DEBUG("[MQTT] Published: %s", payload_buffer_);
    } else {
        LOG_ERROR("[MQTT] Publish failed");
    }
    
    return success;
}

bool MqttManager::publish_status(const char* message, bool retained) {
    if (!is_connected()) return false;
    return client_.publish(config::mqtt.topics.status, message, retained);
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
    LOG_INFO("[MQTT] Message arrived [%s]: %s", topic, message);
    
    // Handle OTA commands
    if (strcmp(topic, config::mqtt.topics.ota) == 0) {
        instance().handle_ota_command(message);
    }
}

void MqttManager::handle_ota_command(const char* url) {
    LOG_INFO("[OTA] Received OTA command via MQTT");
    
    // Expected format: "http://receiver_ip/ota_firmware.bin"
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        LOG_ERROR("[OTA] Invalid URL format");
        publish_status("ota_invalid_url", false);
        return;
    }
    
    LOG_INFO("[OTA] Starting OTA update from: %s", url);
    
    // Perform OTA update
    WiFiClient client;
    t_httpUpdate_return ret = httpUpdate.update(client, url);
    
    switch (ret) {
        case HTTP_UPDATE_FAILED:
            LOG_ERROR("[OTA] Update failed. Error (%d): %s", 
                        httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
            publish_status("ota_failed", false);
            break;
            
        case HTTP_UPDATE_NO_UPDATES:
            LOG_INFO("[OTA] No updates available");
            publish_status("ota_no_update", false);
            break;
            
        case HTTP_UPDATE_OK:
            LOG_INFO("[OTA] Update successful! Rebooting...");
            publish_status("ota_success", false);
            delay(1000);
            ESP.restart();
            break;
    }
}
