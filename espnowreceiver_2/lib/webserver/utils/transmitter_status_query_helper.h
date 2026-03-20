#ifndef TRANSMITTER_STATUS_QUERY_HELPER_H
#define TRANSMITTER_STATUS_QUERY_HELPER_H

#include <Arduino.h>
#include <cstdint>

namespace TransmitterStatusQueryHelper {
    // Runtime status queries
    bool is_ethernet_connected();
    unsigned long get_last_beacon_time();
    bool was_last_send_successful();
    
    // Time/uptime data queries
    uint64_t get_uptime_ms();
    uint64_t get_unix_time();
    uint8_t get_time_source();
}

#endif
