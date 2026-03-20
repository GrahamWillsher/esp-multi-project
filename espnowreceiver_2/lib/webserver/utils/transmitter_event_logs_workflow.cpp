#include "transmitter_event_logs_workflow.h"
#include "transmitter_event_log_cache.h"

namespace TransmitterEventLogsWorkflow {
    void store_event_logs(const JsonObject& logs) {
        TransmitterEventLogCache::store_event_logs(logs);
    }
    
    bool has_event_logs() {
        return TransmitterEventLogCache::has_event_logs();
    }
    
    void get_event_logs_snapshot(std::vector<EventLogEntry>& out_logs, uint32_t* out_last_update_ms) {
        TransmitterEventLogCache::get_event_logs_snapshot(out_logs, out_last_update_ms);
    }
    
    uint32_t get_event_log_count() {
        return TransmitterEventLogCache::get_event_log_count();
    }
    
    uint32_t get_event_logs_last_update_ms() {
        return TransmitterEventLogCache::get_event_logs_last_update_ms();
    }
}
