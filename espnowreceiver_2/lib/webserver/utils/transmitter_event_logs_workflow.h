#ifndef TRANSMITTER_EVENT_LOGS_WORKFLOW_H
#define TRANSMITTER_EVENT_LOGS_WORKFLOW_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include "transmitter_event_log_types.h"

namespace TransmitterEventLogsWorkflow {
    using EventLogEntry = TransmitterEventLogTypes::EventLogEntry;
    
    // Storage
    void store_event_logs(const JsonObject& logs);
    
    // Queries
    bool has_event_logs();
    void get_event_logs_snapshot(std::vector<EventLogEntry>& out_logs, uint32_t* out_last_update_ms = nullptr);
    uint32_t get_event_log_count();
    uint32_t get_event_logs_last_update_ms();
}

#endif
