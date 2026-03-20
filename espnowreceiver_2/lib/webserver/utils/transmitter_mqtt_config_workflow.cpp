#include "transmitter_mqtt_config_workflow.h"

#include <Arduino.h>

#include "../logging.h"
#include "transmitter_mqtt_cache.h"
#include "transmitter_write_through.h"

namespace TransmitterMqttConfigWorkflow {

void store_mqtt_config(bool enabled,
                       const uint8_t* server,
                       uint16_t port,
                       const char* username,
                       const char* password,
                       const char* client_id,
                       bool connected,
                       uint32_t version) {
    (void)connected;  // runtime MQTT connection is managed by version beacons

    TransmitterMqttCache::store_config(enabled, server, port, username, password, client_id, version);

    // NOTE: Do NOT update mqtt_connected here - it's runtime status managed by updateRuntimeStatus()
    // The 'connected' parameter in the config message is stale (from when config was saved)
    // Only version beacons have real-time connection status

    const uint8_t* server_ip = TransmitterMqttCache::get_server();
    const uint8_t fallback[4] = {0, 0, 0, 0};
    if (server_ip == nullptr) server_ip = fallback;

    LOG_INFO("[TX_MGR] MQTT config stored: %s, %d.%d.%d.%d:%d, v%u",
             enabled ? "ENABLED" : "DISABLED",
             server_ip[0], server_ip[1], server_ip[2], server_ip[3], port,
             version);

    TransmitterWriteThrough::persist_to_nvs();
}

} // namespace TransmitterMqttConfigWorkflow