#include "transmitter_time_status_update_workflow.h"

#include "transmitter_status_cache.h"

namespace TransmitterTimeStatusUpdateWorkflow {

void update_time_data(uint64_t new_uptime_ms, uint64_t new_unix_time, uint8_t new_time_source) {
    TransmitterStatusCache::update_time_data(new_uptime_ms, new_unix_time, new_time_source);
}

void update_send_status(bool success) {
    TransmitterStatusCache::update_send_status(success);
}

} // namespace TransmitterTimeStatusUpdateWorkflow
