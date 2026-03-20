#ifndef TRANSMITTER_TIME_STATUS_UPDATE_WORKFLOW_H
#define TRANSMITTER_TIME_STATUS_UPDATE_WORKFLOW_H

#include <stdint.h>

namespace TransmitterTimeStatusUpdateWorkflow {

void update_time_data(uint64_t new_uptime_ms, uint64_t new_unix_time, uint8_t new_time_source);
void update_send_status(bool success);

} // namespace TransmitterTimeStatusUpdateWorkflow

#endif // TRANSMITTER_TIME_STATUS_UPDATE_WORKFLOW_H
