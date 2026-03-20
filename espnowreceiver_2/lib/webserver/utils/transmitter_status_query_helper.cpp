#include "transmitter_status_query_helper.h"
#include "transmitter_status_cache.h"

namespace TransmitterStatusQueryHelper {
    bool is_ethernet_connected() {
        return TransmitterStatusCache::is_ethernet_connected();
    }
    
    unsigned long get_last_beacon_time() {
        return TransmitterStatusCache::get_last_beacon_time();
    }
    
    bool was_last_send_successful() {
        return TransmitterStatusCache::was_last_send_successful();
    }
    
    uint64_t get_uptime_ms() {
        return TransmitterStatusCache::get_uptime_ms();
    }
    
    uint64_t get_unix_time() {
        return TransmitterStatusCache::get_unix_time();
    }
    
    uint8_t get_time_source() {
        return TransmitterStatusCache::get_time_source();
    }
}
