#include "transmitter_runtime_status_update.h"

#include <Arduino.h>

#include "../logging.h"
#include "transmitter_mqtt_cache.h"
#include "transmitter_status_cache.h"

namespace TransmitterRuntimeStatusUpdate {

void update_runtime_status(bool mqtt_conn, bool eth_conn) {
    bool mqtt_changed = TransmitterMqttCache::update_runtime_connection(mqtt_conn);
    bool eth_changed = (TransmitterStatusCache::is_ethernet_connected() != eth_conn);

    TransmitterStatusCache::update_runtime_status(eth_conn);

    if (mqtt_changed || eth_changed) {
        LOG_INFO("[TX_MGR] Runtime status updated: MQTT=%s, ETH=%s",
                 mqtt_conn ? "CONNECTED" : "DISCONNECTED",
                 eth_conn ? "CONNECTED" : "DISCONNECTED");
    }
}

} // namespace TransmitterRuntimeStatusUpdate