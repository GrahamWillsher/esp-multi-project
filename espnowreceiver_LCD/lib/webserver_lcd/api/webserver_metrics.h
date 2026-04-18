#ifndef WEBSERVER_METRICS_H
#define WEBSERVER_METRICS_H

#include <cstdint>

namespace WebserverMetrics {

enum HttpHandlerMetricId : uint8_t {
    HM_DATA = 0,
    HM_GET_RECEIVER_INFO,
    HM_MONITOR,
    HM_CELL_DATA,
    HM_DASHBOARD_DATA,
    HM_VERSION,
    HM_FIRMWARE_INFO,
    HM_TRANSMITTER_METADATA,
    HM_TRANSMITTER_HEALTH,
    HM_STATIC_SPECS,
    HM_BATTERY_SPECS,
    HM_INVERTER_SPECS,
    HM_GET_EVENT_LOGS,
    HM_SYSTEM_METRICS,
    HM_COUNT
};

struct HttpHandlerMetricsSnapshot {
    uint32_t calls = 0;
    uint32_t last_ms = 0;
    uint32_t max_ms = 0;
    uint32_t avg_ms = 0;
};

struct EventLogProxyMetricsSnapshot {
    uint32_t total_requests = 0;
    uint32_t total_success = 0;
    uint32_t total_timeouts = 0;
    uint32_t total_http_errors = 0;
    int32_t last_http_code = 0;
    uint32_t last_latency_ms = 0;
    uint32_t avg_success_latency_ms = 0;
};

class HttpHandlerTimer {
public:
    explicit HttpHandlerTimer(HttpHandlerMetricId id);
    ~HttpHandlerTimer();

private:
    HttpHandlerMetricId id_;
    uint32_t start_ms_;
};

void recordEventLogProxyResult(int http_code, uint32_t latency_ms);

uint8_t httpHandlerCount();
const char* httpHandlerName(HttpHandlerMetricId id);
void getHttpHandlerMetrics(HttpHandlerMetricId id, HttpHandlerMetricsSnapshot& out);
void getEventLogProxyMetrics(EventLogProxyMetricsSnapshot& out);

} // namespace WebserverMetrics

#endif // WEBSERVER_METRICS_H
