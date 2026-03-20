#include "transmitter_network_query_helper.h"
#include "transmitter_network_cache.h"

namespace TransmitterNetworkQueryHelper {
    const uint8_t* get_ip() {
        return TransmitterNetworkCache::get_ip();
    }

    const uint8_t* get_gateway() {
        return TransmitterNetworkCache::get_gateway();
    }

    const uint8_t* get_subnet() {
        return TransmitterNetworkCache::get_subnet();
    }

    const uint8_t* get_static_ip() {
        return TransmitterNetworkCache::get_static_ip();
    }

    const uint8_t* get_static_gateway() {
        return TransmitterNetworkCache::get_static_gateway();
    }

    const uint8_t* get_static_subnet() {
        return TransmitterNetworkCache::get_static_subnet();
    }

    const uint8_t* get_static_dns_primary() {
        return TransmitterNetworkCache::get_static_dns_primary();
    }

    const uint8_t* get_static_dns_secondary() {
        return TransmitterNetworkCache::get_static_dns_secondary();
    }

    bool is_ip_known() {
        return TransmitterNetworkCache::is_ip_known();
    }

    bool is_static_ip() {
        return TransmitterNetworkCache::is_static_ip();
    }

    uint32_t get_network_config_version() {
        return TransmitterNetworkCache::get_network_config_version();
    }

    void update_network_mode(bool is_static, uint32_t version) {
        TransmitterNetworkCache::update_network_mode(is_static, version);
    }

    String get_ip_string() {
        return TransmitterNetworkCache::get_ip_string();
    }

    String get_url() {
        return TransmitterNetworkCache::get_url();
    }
}
