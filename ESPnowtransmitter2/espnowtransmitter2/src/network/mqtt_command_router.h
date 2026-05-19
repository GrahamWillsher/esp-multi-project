#ifndef MQTT_COMMAND_ROUTER_H
#define MQTT_COMMAND_ROUTER_H

#include <Arduino.h>
#include <ArduinoJson.h>

/**
 * @brief Routes and handles incoming MQTT commands from receiver
 *
 * Subscribes to `batt-emu/mqtt-v1/rx/cmd/#` topics and dispatches
 * to appropriate handlers (network, mqtt, settings updates, control actions).
 *
 * Publishes ACKs to `batt-emu/mqtt-v1/tx/ack/#` topics.
 */
class MqttCommandRouter {
public:
    /**
     * @brief Process an incoming MQTT command message
     * @param topic Full topic path (e.g., "batt-emu/mqtt-v1/rx/cmd/update/network")
     * @param payload JSON command payload
     * @param length Payload length in bytes
     */
    static void handleCommand(const char* topic, const uint8_t* payload, unsigned int length);

private:
    // Command topic parsers
    static void handle_update_network(const char* json_payload, size_t length);
    static void handle_update_mqtt(const char* json_payload, size_t length);
    static void handle_update_settings(const char* json_payload, size_t length);
    static void handle_control_reboot(const char* json_payload, size_t length);
    static void handle_control_debug_level(const char* json_payload, size_t length);
    static void handle_control_component_apply(const char* json_payload, size_t length);
    static void handle_refresh_network(const char* json_payload, size_t length);
    static void handle_refresh_mqtt(const char* json_payload, size_t length);
    static void handle_refresh_battery(const char* json_payload, size_t length);
    static void handle_refresh_catalog_battery(const char* json_payload, size_t length);
    static void handle_refresh_catalog_inverter(const char* json_payload, size_t length);
};

#endif // MQTT_COMMAND_ROUTER_H
