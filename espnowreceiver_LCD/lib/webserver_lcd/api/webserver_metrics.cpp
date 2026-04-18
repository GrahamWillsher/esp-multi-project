#include "webserver_metrics.h"

#include <Arduino.h>

namespace WebserverMetrics {
namespace {

struct HttpHandlerMetricsInternal {
    volatile uint32_t calls = 0;
    volatile uint32_t last_ms = 0;
    volatile uint32_t max_ms = 0;
    volatile uint64_t total_ms = 0;
};

struct EventLogProxyMetricsInternal {
    volatile uint32_t total_requests = 0;
    volatile uint32_t total_success = 0;
    volatile uint32_t total_timeouts = 0;
    volatile uint32_t total_http_errors = 0;
    volatile int32_t last_http_code = 0;
    volatile uint32_t last_latency_ms = 0;
    volatile uint64_t total_latency_ms = 0;
};

HttpHandlerMetricsInternal g_http_handler_metrics[HM_COUNT];
const char* const k_http_handler_names[HM_COUNT] = {
    "api_data",
    "api_get_receiver_info",
    "api_monitor",
    "api_cell_data",
    "api_dashboard_data",
    "api_version",
    "api_firmware_info",
    "api_transmitter_metadata",
    "api_transmitter_health",
    "api_static_specs",
    "api_battery_specs",
    "api_inverter_specs",
    "api_get_event_logs",
    "api_system_metrics"
};

EventLogProxyMetricsInternal g_event_log_proxy_metrics;
portMUX_TYPE g_metrics_mux = portMUX_INITIALIZER_UNLOCKED;

void recordHttpHandlerLatency(HttpHandlerMetricId id, uint32_t latency_ms) {
    if (id >= HM_COUNT) {
        return;
    }

    portENTER_CRITICAL(&g_metrics_mux);
    g_http_handler_metrics[id].calls++;
    g_http_handler_metrics[id].last_ms = latency_ms;
    g_http_handler_metrics[id].total_ms += latency_ms;
    if (latency_ms > g_http_handler_metrics[id].max_ms) {
        g_http_handler_metrics[id].max_ms = latency_ms;
    }
    portEXIT_CRITICAL(&g_metrics_mux);
}

} // namespace

HttpHandlerTimer::HttpHandlerTimer(HttpHandlerMetricId id)
    : id_(id), start_ms_(millis()) {}

HttpHandlerTimer::~HttpHandlerTimer() {
    recordHttpHandlerLatency(id_, millis() - start_ms_);
}

void recordEventLogProxyResult(int http_code, uint32_t latency_ms) {
    portENTER_CRITICAL(&g_metrics_mux);
    g_event_log_proxy_metrics.total_requests++;
    g_event_log_proxy_metrics.last_http_code = http_code;
    g_event_log_proxy_metrics.last_latency_ms = latency_ms;

    if (http_code == 200) {
        g_event_log_proxy_metrics.total_success++;
        g_event_log_proxy_metrics.total_latency_ms += latency_ms;
    } else if (http_code == -11) {
        g_event_log_proxy_metrics.total_timeouts++;
    } else {
        g_event_log_proxy_metrics.total_http_errors++;
    }
    portEXIT_CRITICAL(&g_metrics_mux);
}

uint8_t httpHandlerCount() {
    return HM_COUNT;
}

const char* httpHandlerName(HttpHandlerMetricId id) {
    if (id >= HM_COUNT) {
        return "unknown";
    }
    return k_http_handler_names[id];
}

void getHttpHandlerMetrics(HttpHandlerMetricId id, HttpHandlerMetricsSnapshot& out) {
    if (id >= HM_COUNT) {
        out = HttpHandlerMetricsSnapshot{};
        return;
    }

    uint32_t calls = 0;
    uint32_t last_ms = 0;
    uint32_t max_ms = 0;
    uint64_t total_ms = 0;

    portENTER_CRITICAL(&g_metrics_mux);
    calls = g_http_handler_metrics[id].calls;
    last_ms = g_http_handler_metrics[id].last_ms;
    max_ms = g_http_handler_metrics[id].max_ms;
    total_ms = g_http_handler_metrics[id].total_ms;
    portEXIT_CRITICAL(&g_metrics_mux);

    out.calls = calls;
    out.last_ms = last_ms;
    out.max_ms = max_ms;
    out.avg_ms = (calls > 0) ? static_cast<uint32_t>(total_ms / calls) : 0;
}

void getEventLogProxyMetrics(EventLogProxyMetricsSnapshot& out) {
    uint32_t total_success = 0;
    uint64_t total_latency_ms = 0;

    portENTER_CRITICAL(&g_metrics_mux);
    out.total_requests = g_event_log_proxy_metrics.total_requests;
    total_success = g_event_log_proxy_metrics.total_success;
    out.total_success = total_success;
    out.total_timeouts = g_event_log_proxy_metrics.total_timeouts;
    out.total_http_errors = g_event_log_proxy_metrics.total_http_errors;
    out.last_http_code = g_event_log_proxy_metrics.last_http_code;
    out.last_latency_ms = g_event_log_proxy_metrics.last_latency_ms;
    total_latency_ms = g_event_log_proxy_metrics.total_latency_ms;
    portEXIT_CRITICAL(&g_metrics_mux);

    out.avg_success_latency_ms = (total_success > 0)
                                     ? static_cast<uint32_t>(total_latency_ms / total_success)
                                     : 0;
}

} // namespace WebserverMetrics
