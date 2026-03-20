#ifndef TRANSMITTER_NETWORK_STORE_WORKFLOW_H
#define TRANSMITTER_NETWORK_STORE_WORKFLOW_H

#include <stdint.h>

namespace TransmitterNetworkStoreWorkflow {

void store_ip_data(const uint8_t* transmitter_ip,
                   const uint8_t* transmitter_gateway,
                   const uint8_t* transmitter_subnet,
                   bool is_static,
                   uint32_t config_version);

void store_network_config(const uint8_t* curr_ip,
                          const uint8_t* curr_gateway,
                          const uint8_t* curr_subnet,
                          const uint8_t* stat_ip,
                          const uint8_t* stat_gateway,
                          const uint8_t* stat_subnet,
                          const uint8_t* stat_dns1,
                          const uint8_t* stat_dns2,
                          bool is_static,
                          uint32_t config_version);

} // namespace TransmitterNetworkStoreWorkflow

#endif // TRANSMITTER_NETWORK_STORE_WORKFLOW_H