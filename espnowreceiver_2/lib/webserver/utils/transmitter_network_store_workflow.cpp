#include "transmitter_network_store_workflow.h"

#include "transmitter_network_cache.h"
#include "transmitter_write_through.h"

namespace TransmitterNetworkStoreWorkflow {

void store_ip_data(const uint8_t* transmitter_ip,
                   const uint8_t* transmitter_gateway,
                   const uint8_t* transmitter_subnet,
                   bool is_static,
                   uint32_t config_version) {
    if (!TransmitterNetworkCache::store_ip_data(transmitter_ip, transmitter_gateway, transmitter_subnet, is_static, config_version)) {
        return;
    }

    TransmitterWriteThrough::notify_and_persist();
}

void store_network_config(const uint8_t* curr_ip,
                          const uint8_t* curr_gateway,
                          const uint8_t* curr_subnet,
                          const uint8_t* stat_ip,
                          const uint8_t* stat_gateway,
                          const uint8_t* stat_subnet,
                          const uint8_t* stat_dns1,
                          const uint8_t* stat_dns2,
                          bool is_static,
                          uint32_t config_version) {
    if (!TransmitterNetworkCache::store_network_config(curr_ip, curr_gateway, curr_subnet,
                                                       stat_ip, stat_gateway, stat_subnet, stat_dns1, stat_dns2,
                                                       is_static, config_version)) {
        return;
    }

    TransmitterWriteThrough::notify_and_persist();
}

} // namespace TransmitterNetworkStoreWorkflow