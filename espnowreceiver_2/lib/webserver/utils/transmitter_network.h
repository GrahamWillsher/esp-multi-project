#ifndef TRANSMITTER_NETWORK_H
#define TRANSMITTER_NETWORK_H

#include <Arduino.h>
#include <cstdint>

namespace TransmitterNetwork {

void load_from_prefs(void* prefs_ptr);
void save_to_prefs(void* prefs_ptr);

bool store_ip_data(const uint8_t* transmitter_ip,
                   const uint8_t* transmitter_gateway,
                   const uint8_t* transmitter_subnet,
                   bool is_static,
                   uint32_t config_version,
                   bool persist = true);

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
                          bool persist = true);

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

} // namespace TransmitterNetwork

#endif // TRANSMITTER_NETWORK_H
