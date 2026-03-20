#ifndef TRANSMITTER_NETWORK_QUERY_HELPER_H
#define TRANSMITTER_NETWORK_QUERY_HELPER_H

#include <Arduino.h>
#include <cstdint>

namespace TransmitterNetworkQueryHelper {
    const uint8_t* get_ip();
    const uint8_t* get_gateway();
    const uint8_t* get_subnet();

    const uint8_t* get_static_ip();
    const uint8_t* get_static_gateway();
    const uint8_t* get_static_subnet();
    const uint8_t* get_static_dns_primary();
    const uint8_t* get_static_dns_secondary();

    bool is_ip_known();
    bool is_static_ip();
    uint32_t get_network_config_version();
    void update_network_mode(bool is_static, uint32_t version);
    String get_ip_string();
    String get_url();
}

#endif
