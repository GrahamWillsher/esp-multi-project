#include "transmitter_network.h"

#include <Preferences.h>
#include <string.h>

#include "../logging.h"
#include "transmitter_nvs_persistence.h"

namespace {
constexpr const char* kKeyNetCurrIp = "net_curr_ip";
constexpr const char* kKeyNetCurrGw = "net_curr_gw";
constexpr const char* kKeyNetCurrSn = "net_curr_sn";
constexpr const char* kKeyNetStatIp = "net_stat_ip";
constexpr const char* kKeyNetStatGw = "net_stat_gw";
constexpr const char* kKeyNetStatSn = "net_stat_sn";
constexpr const char* kKeyNetDns1 = "net_dns1";
constexpr const char* kKeyNetDns2 = "net_dns2";
constexpr const char* kKeyNetIsStatic = "net_is_static";
constexpr const char* kKeyNetVersion = "net_ver";
constexpr const char* kKeyNetKnown = "net_known";

struct NetworkCache {
    uint8_t current_ip[4] = {0, 0, 0, 0};
    uint8_t current_gateway[4] = {0, 0, 0, 0};
    uint8_t current_subnet[4] = {0, 0, 0, 0};
    uint8_t static_ip[4] = {0, 0, 0, 0};
    uint8_t static_gateway[4] = {0, 0, 0, 0};
    uint8_t static_subnet[4] = {0, 0, 0, 0};
    uint8_t static_dns_primary[4] = {8, 8, 8, 8};
    uint8_t static_dns_secondary[4] = {8, 8, 4, 4};
    bool ip_known = false;
    bool is_static_ip = false;
    uint32_t network_config_version = 0;
};

NetworkCache network_cache;

bool is_zero_ip(const uint8_t* ip) {
    return ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0;
}

void persist_if_requested(bool persist) {
    if (persist) {
        TransmitterNvsPersistence::notifyAndPersist();
    }
}
} // namespace

namespace TransmitterNetwork {

void load_from_prefs(void* prefs_ptr) {
    if (prefs_ptr == nullptr) {
        return;
    }

    Preferences& prefs = *static_cast<Preferences*>(prefs_ptr);
    prefs.getBytes(kKeyNetCurrIp, network_cache.current_ip, sizeof(network_cache.current_ip));
    prefs.getBytes(kKeyNetCurrGw, network_cache.current_gateway, sizeof(network_cache.current_gateway));
    prefs.getBytes(kKeyNetCurrSn, network_cache.current_subnet, sizeof(network_cache.current_subnet));
    prefs.getBytes(kKeyNetStatIp, network_cache.static_ip, sizeof(network_cache.static_ip));
    prefs.getBytes(kKeyNetStatGw, network_cache.static_gateway, sizeof(network_cache.static_gateway));
    prefs.getBytes(kKeyNetStatSn, network_cache.static_subnet, sizeof(network_cache.static_subnet));
    prefs.getBytes(kKeyNetDns1, network_cache.static_dns_primary, sizeof(network_cache.static_dns_primary));
    prefs.getBytes(kKeyNetDns2, network_cache.static_dns_secondary, sizeof(network_cache.static_dns_secondary));
    network_cache.is_static_ip = prefs.getBool(kKeyNetIsStatic, false);
    network_cache.network_config_version = prefs.getUInt(kKeyNetVersion, 0);
    network_cache.ip_known = prefs.getBool(kKeyNetKnown, false);
}

void save_to_prefs(void* prefs_ptr) {
    if (prefs_ptr == nullptr) {
        return;
    }

    Preferences& prefs = *static_cast<Preferences*>(prefs_ptr);
    prefs.putBytes(kKeyNetCurrIp, network_cache.current_ip, sizeof(network_cache.current_ip));
    prefs.putBytes(kKeyNetCurrGw, network_cache.current_gateway, sizeof(network_cache.current_gateway));
    prefs.putBytes(kKeyNetCurrSn, network_cache.current_subnet, sizeof(network_cache.current_subnet));
    prefs.putBytes(kKeyNetStatIp, network_cache.static_ip, sizeof(network_cache.static_ip));
    prefs.putBytes(kKeyNetStatGw, network_cache.static_gateway, sizeof(network_cache.static_gateway));
    prefs.putBytes(kKeyNetStatSn, network_cache.static_subnet, sizeof(network_cache.static_subnet));
    prefs.putBytes(kKeyNetDns1, network_cache.static_dns_primary, sizeof(network_cache.static_dns_primary));
    prefs.putBytes(kKeyNetDns2, network_cache.static_dns_secondary, sizeof(network_cache.static_dns_secondary));
    prefs.putBool(kKeyNetIsStatic, network_cache.is_static_ip);
    prefs.putUInt(kKeyNetVersion, network_cache.network_config_version);
    prefs.putBool(kKeyNetKnown, network_cache.ip_known);
}

bool store_ip_data(const uint8_t* transmitter_ip,
                   const uint8_t* transmitter_gateway,
                   const uint8_t* transmitter_subnet,
                   bool is_static,
                   uint32_t config_version,
                   bool persist) {
    if (transmitter_ip == nullptr || transmitter_gateway == nullptr || transmitter_subnet == nullptr) {
        return false;
    }

    if (is_zero_ip(transmitter_ip)) {
        network_cache.ip_known = false;
        LOG_WARN("[NET_CACHE] Received empty IP data - transmitter Ethernet not connected yet");
        return false;
    }

    memcpy(network_cache.current_ip, transmitter_ip, 4);
    memcpy(network_cache.current_gateway, transmitter_gateway, 4);
    memcpy(network_cache.current_subnet, transmitter_subnet, 4);
    network_cache.ip_known = true;
    network_cache.is_static_ip = is_static;
    network_cache.network_config_version = config_version;

    char ip_str[kIpStringLength] = {0};
    if (!get_ip_string(ip_str, sizeof(ip_str))) {
        strncpy(ip_str, "0.0.0.0", sizeof(ip_str) - 1);
        ip_str[sizeof(ip_str) - 1] = '\0';
    }

    LOG_INFO("[NET_CACHE] IP data: %s (%s), Gateway: %d.%d.%d.%d, Subnet: %d.%d.%d.%d, Version: %u",
             ip_str,
             network_cache.is_static_ip ? "Static" : "DHCP",
             network_cache.current_gateway[0], network_cache.current_gateway[1], network_cache.current_gateway[2], network_cache.current_gateway[3],
             network_cache.current_subnet[0], network_cache.current_subnet[1], network_cache.current_subnet[2], network_cache.current_subnet[3],
             network_cache.network_config_version);

    persist_if_requested(persist);
    return true;
}

bool store_network_config(const uint8_t* curr_ip,
                          const uint8_t* curr_gateway,
                          const uint8_t* curr_subnet,
                          const uint8_t* stat_ip,
                          const uint8_t* stat_gateway,
                          const uint8_t* stat_subnet,
                          const uint8_t* stat_dns1,
                          const uint8_t* stat_dns2,
                          bool is_static,
                          uint32_t config_version,
                          bool persist) {
    if (curr_ip == nullptr || curr_gateway == nullptr || curr_subnet == nullptr) {
        return false;
    }

    if (is_zero_ip(curr_ip)) {
        network_cache.ip_known = false;
        LOG_WARN("[NET_CACHE] Received empty current IP - transmitter Ethernet not connected yet");
        return false;
    }

    memcpy(network_cache.current_ip, curr_ip, 4);
    memcpy(network_cache.current_gateway, curr_gateway, 4);
    memcpy(network_cache.current_subnet, curr_subnet, 4);

    if (stat_ip) memcpy(network_cache.static_ip, stat_ip, 4);
    if (stat_gateway) memcpy(network_cache.static_gateway, stat_gateway, 4);
    if (stat_subnet) memcpy(network_cache.static_subnet, stat_subnet, 4);
    if (stat_dns1) memcpy(network_cache.static_dns_primary, stat_dns1, 4);
    if (stat_dns2) memcpy(network_cache.static_dns_secondary, stat_dns2, 4);

    network_cache.ip_known = true;
    network_cache.is_static_ip = is_static;
    network_cache.network_config_version = config_version;

    LOG_INFO("[NET_CACHE] Network config stored");
    LOG_INFO("[NET_CACHE] Current: %d.%d.%d.%d (%s)",
             network_cache.current_ip[0], network_cache.current_ip[1], network_cache.current_ip[2], network_cache.current_ip[3],
             network_cache.is_static_ip ? "Static" : "DHCP");
    LOG_INFO("[NET_CACHE] Static saved: %d.%d.%d.%d / %d.%d.%d.%d / %d.%d.%d.%d",
             network_cache.static_ip[0], network_cache.static_ip[1], network_cache.static_ip[2], network_cache.static_ip[3],
             network_cache.static_gateway[0], network_cache.static_gateway[1], network_cache.static_gateway[2], network_cache.static_gateway[3],
             network_cache.static_subnet[0], network_cache.static_subnet[1], network_cache.static_subnet[2], network_cache.static_subnet[3]);
    LOG_INFO("[NET_CACHE] DNS: %d.%d.%d.%d / %d.%d.%d.%d, Version: %u",
             network_cache.static_dns_primary[0], network_cache.static_dns_primary[1], network_cache.static_dns_primary[2], network_cache.static_dns_primary[3],
             network_cache.static_dns_secondary[0], network_cache.static_dns_secondary[1], network_cache.static_dns_secondary[2], network_cache.static_dns_secondary[3],
             network_cache.network_config_version);

    persist_if_requested(persist);
    return true;
}

const uint8_t* get_ip() {
    return network_cache.ip_known ? network_cache.current_ip : nullptr;
}

const uint8_t* get_gateway() {
    return network_cache.ip_known ? network_cache.current_gateway : nullptr;
}

const uint8_t* get_subnet() {
    return network_cache.ip_known ? network_cache.current_subnet : nullptr;
}

const uint8_t* get_static_ip() {
    return network_cache.static_ip;
}

const uint8_t* get_static_gateway() {
    return network_cache.static_gateway;
}

const uint8_t* get_static_subnet() {
    return network_cache.static_subnet;
}

const uint8_t* get_static_dns_primary() {
    return network_cache.static_dns_primary;
}

const uint8_t* get_static_dns_secondary() {
    return network_cache.static_dns_secondary;
}

bool is_ip_known() {
    return network_cache.ip_known;
}

bool is_static_ip() {
    return network_cache.is_static_ip;
}

uint32_t get_network_config_version() {
    return network_cache.network_config_version;
}

void update_network_mode(bool is_static, uint32_t version) {
    network_cache.is_static_ip = is_static;
    network_cache.network_config_version = version;
    LOG_INFO("[NET_CACHE] Network mode updated: %s (version %u)", is_static ? "Static" : "DHCP", version);
}

bool format_ip(const uint8_t* ip, char* out, size_t out_len) {
    if (out == nullptr || out_len == 0) {
        return false;
    }

    if (ip == nullptr) {
        static constexpr const char* kUnknownIp = "0.0.0.0";
        if (out_len <= strlen(kUnknownIp)) {
            out[0] = '\0';
            return false;
        }
        strncpy(out, kUnknownIp, out_len - 1);
        out[out_len - 1] = '\0';
        return true;
    }

    int written = snprintf(out,
                           out_len,
                           "%u.%u.%u.%u",
                           ip[0],
                           ip[1],
                           ip[2],
                           ip[3]);
    if (written < 0 || static_cast<size_t>(written) >= out_len) {
        out[0] = '\0';
        return false;
    }

    return true;
}

bool get_ip_string(char* out, size_t out_len) {
    if (!network_cache.ip_known) {
        return format_ip(nullptr, out, out_len);
    }
    return format_ip(network_cache.current_ip, out, out_len);
}

bool get_url(char* out, size_t out_len) {
    if (out == nullptr || out_len == 0) {
        return false;
    }

    if (!network_cache.ip_known) {
        out[0] = '\0';
        return true;
    }

    char ip_str[kIpStringLength] = {0};
    if (!get_ip_string(ip_str, sizeof(ip_str))) {
        out[0] = '\0';
        return false;
    }

    int written = snprintf(out, out_len, "http://%s", ip_str);
    if (written < 0 || static_cast<size_t>(written) >= out_len) {
        out[0] = '\0';
        return false;
    }

    return true;
}

String get_ip_string() {
    char str[kIpStringLength] = {0};
    if (!get_ip_string(str, sizeof(str))) {
        return String("0.0.0.0");
    }
    return String(str);
}

String get_url() {
    char str[kUrlStringLength] = {0};
    if (!get_url(str, sizeof(str))) {
        return String("");
    }
    return String(str);
}

} // namespace TransmitterNetwork
