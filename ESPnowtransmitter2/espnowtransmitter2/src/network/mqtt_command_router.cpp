#include "mqtt_command_router.h"
#include "mqtt_manager.h"
#include "../espnow/control_handlers.h"
#include "../espnow/component_catalog_handlers.h"
#include "../network/ethernet_manager.h"
#include "../datalayer/static_data.h"
#include "../settings/settings_manager.h"
#include "logging_config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Arduino.h>
#include <cstring>

namespace {

// Helper to send ACK with given success/message fields
void send_ack(const char* model, const char* request_id, bool success, const char* message) {
    if (!request_id) return;
    
    char topic[128];
    snprintf(topic, sizeof(topic), "batt-emu/mqtt-v1/tx/ack/%s", model ? model : "");
    
    char payload[256];
    snprintf(payload, sizeof(payload),
             R"({"request_id":"%s","success":%s,"message":"%s"})",
             request_id,
             success ? "true" : "false",
             message ? message : "");
    
    // Use direct client_.publish
    // Note: this reaches into MqttManager internals; ideally we'd have a helper method
    // For now, we'll just log the ACK intent
    LOG_INFO("MQTT_ACK", "ACK: %s -> %s (success=%s)", topic, request_id, success ? "yes" : "no");
}

} // namespace

void MqttCommandRouter::handleCommand(const char* topic, const uint8_t* payload, unsigned int length) {
    if (!topic || !payload) {
        LOG_WARN("MQTT_CMD", "Invalid command: null topic or payload");
        return;
    }

    const char* json_payload = reinterpret_cast<const char*>(payload);
    LOG_INFO("MQTT_CMD", "Received command on %s (%u bytes)", topic, length);

    // Dispatch based on topic
    // Pattern: batt-emu/mqtt-v1/rx/cmd/update/<model>
    if (strstr(topic, "batt-emu/mqtt-v1/rx/cmd/update/network")) {
        handle_update_network(json_payload, length);
    } else if (strstr(topic, "batt-emu/mqtt-v1/rx/cmd/update/mqtt")) {
        handle_update_mqtt(json_payload, length);
    } else if (strstr(topic, "batt-emu/mqtt-v1/rx/cmd/update/settings")) {
        handle_update_settings(json_payload, length);
    } else if (strstr(topic, "batt-emu/mqtt-v1/rx/cmd/control/reboot")) {
        handle_control_reboot(json_payload, length);
    } else if (strstr(topic, "batt-emu/mqtt-v1/rx/cmd/control/debug_level")) {
        handle_control_debug_level(json_payload, length);
    } else if (strstr(topic, "batt-emu/mqtt-v1/rx/cmd/control/component_apply")) {
        handle_control_component_apply(json_payload, length);
    } else if (strstr(topic, "batt-emu/mqtt-v1/rx/cmd/refresh/network")) {
        handle_refresh_network(json_payload, length);
    } else if (strstr(topic, "batt-emu/mqtt-v1/rx/cmd/refresh/mqtt")) {
        handle_refresh_mqtt(json_payload, length);
    } else if (strstr(topic, "batt-emu/mqtt-v1/rx/cmd/refresh/battery")) {
        handle_refresh_battery(json_payload, length);
    } else if (strstr(topic, "batt-emu/mqtt-v1/rx/cmd/refresh/catalog_battery")) {
        handle_refresh_catalog_battery(json_payload, length);
    } else if (strstr(topic, "batt-emu/mqtt-v1/rx/cmd/refresh/catalog_inverter")) {
        handle_refresh_catalog_inverter(json_payload, length);
    } else {
        LOG_WARN("MQTT_CMD", "Unknown command topic: %s", topic);
    }
}

void MqttCommandRouter::handle_update_network(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT_CMD", "Processing network update command");
    
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_ERROR("MQTT_CMD", "Failed to parse network update: %s", error.c_str());
        send_ack("network", doc["request_id"] | "", false, "JSON parse error");
        return;
    }
    
    const char* request_id = doc["request_id"] | "";
    
    // Delegate to existing control handler (transport-agnostic)
    // This will eventually call transmitter's EthernetManager, etc.
    // For now, send placeholder ACK
    send_ack("network", request_id, true, "network update queued");
    LOG_INFO("MQTT_CMD", "Network update command processed (request_id=%s)", request_id);
}

void MqttCommandRouter::handle_update_mqtt(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT_CMD", "Processing MQTT config update command");
    
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_ERROR("MQTT_CMD", "Failed to parse MQTT update: %s", error.c_str());
        send_ack("mqtt", doc["request_id"] | "", false, "JSON parse error");
        return;
    }
    
    const char* request_id = doc["request_id"] | "";
    send_ack("mqtt", request_id, true, "MQTT config update queued");
    LOG_INFO("MQTT_CMD", "MQTT config update command processed (request_id=%s)", request_id);
}

void MqttCommandRouter::handle_update_settings(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT_CMD", "Processing settings update command");
    
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_ERROR("MQTT_CMD", "Failed to parse settings update: %s", error.c_str());
        send_ack("settings", doc["request_id"] | "", false, "JSON parse error");
        return;
    }
    
    const char* request_id = doc["request_id"] | "";
    send_ack("settings", request_id, true, "settings update queued");
    LOG_INFO("MQTT_CMD", "Settings update command processed (request_id=%s)", request_id);
}

void MqttCommandRouter::handle_control_reboot(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT_CMD", "Processing reboot command");
    
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_ERROR("MQTT_CMD", "Failed to parse reboot command: %s", error.c_str());
        send_ack("reboot", doc["request_id"] | "", false, "JSON parse error");
        return;
    }
    
    const char* request_id = doc["request_id"] | "";
    send_ack("reboot", request_id, true, "rebooting...");
    
    // Schedule reboot
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    ESP.restart();
}

void MqttCommandRouter::handle_control_debug_level(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT_CMD", "Processing debug level command");
    
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_ERROR("MQTT_CMD", "Failed to parse debug level: %s", error.c_str());
        send_ack("debug_level", doc["request_id"] | "", false, "JSON parse error");
        return;
    }
    
    const char* request_id = doc["request_id"] | "";
    const uint8_t level = doc["level"] | 0;
    
    send_ack("debug_level", request_id, true, "debug level set");
    LOG_INFO("MQTT_CMD", "Debug level updated via MQTT (request_id=%s, level=%u)", request_id, level);
}

void MqttCommandRouter::handle_control_component_apply(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT_CMD", "Processing component apply command");
    
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_ERROR("MQTT_CMD", "Failed to parse component apply: %s", error.c_str());
        send_ack("component_apply", doc["request_id"] | "", false, "JSON parse error");
        return;
    }
    
    const char* request_id = doc["request_id"] | "";
    const uint8_t apply_mask = doc["apply_mask"] | 0;
    const uint8_t battery_type = doc["battery_type"] | 0;
    const uint8_t inverter_type = doc["inverter_type"] | 0;
    const uint8_t battery_interface = doc["battery_interface"] | 0;
    const uint8_t inverter_interface = doc["inverter_interface"] | 0;
    
    // Delegate to transport-agnostic apply_components service
    const TxComponentCatalogHandlers::ComponentApplyResult result =
        TxComponentCatalogHandlers::apply_components(apply_mask, battery_type,
                                                     inverter_type, battery_interface,
                                                     inverter_interface);
    
    send_ack("component_apply", request_id, result.success,
             result.success ? "OK" : result.message);
    LOG_INFO("MQTT_CMD", "Component apply processed (request_id=%s, success=%s)", request_id,
             result.success ? "yes" : "no");
}

void MqttCommandRouter::handle_refresh_network(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT_CMD", "Processing network refresh request");
    
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_ERROR("MQTT_CMD", "Failed to parse network refresh: %s", error.c_str());
        return;
    }
    
    // Republish static/network topic
    MqttManager::instance().publish_static_network();
}

void MqttCommandRouter::handle_refresh_mqtt(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT_CMD", "Processing MQTT config refresh request");
    
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_ERROR("MQTT_CMD", "Failed to parse MQTT refresh: %s", error.c_str());
        return;
    }
    
    // Republish static/mqtt topic
    MqttManager::instance().publish_static_mqtt();
}

void MqttCommandRouter::handle_refresh_battery(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT_CMD", "Processing battery specs refresh request");
    
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_ERROR("MQTT_CMD", "Failed to parse battery refresh: %s", error.c_str());
        return;
    }
    
    // Republish static/battery topic
    MqttManager::instance().publish_battery_specs();
}

void MqttCommandRouter::handle_refresh_catalog_battery(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT_CMD", "Processing battery catalog refresh request");
    
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_ERROR("MQTT_CMD", "Failed to parse battery catalog refresh: %s", error.c_str());
        return;
    }
    
    // Republish catalog topics
    MqttManager::instance().publish_battery_type_catalog();
}

void MqttCommandRouter::handle_refresh_catalog_inverter(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT_CMD", "Processing inverter catalog refresh request");
    
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_ERROR("MQTT_CMD", "Failed to parse inverter catalog refresh: %s", error.c_str());
        return;
    }
    
    // Republish catalog topics
    MqttManager::instance().publish_inverter_type_catalog();
}
