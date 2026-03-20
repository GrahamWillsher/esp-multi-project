#ifndef TRANSMITTER_MQTT_CONFIG_WORKFLOW_H
#define TRANSMITTER_MQTT_CONFIG_WORKFLOW_H

#include <stdint.h>

namespace TransmitterMqttConfigWorkflow {

void store_mqtt_config(bool enabled,
                       const uint8_t* server,
                       uint16_t port,
                       const char* username,
                       const char* password,
                       const char* client_id,
                       bool connected,
                       uint32_t version);

} // namespace TransmitterMqttConfigWorkflow

#endif // TRANSMITTER_MQTT_CONFIG_WORKFLOW_H