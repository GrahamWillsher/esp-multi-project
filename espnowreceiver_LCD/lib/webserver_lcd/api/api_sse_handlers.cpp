#include "api_sse_handlers.h"

#include "api_response_utils.h"
#include "../utils/transmitter_manager.h"
#include "../utils/cell_data_cache.h"
#include "../utils/sse_notifier.h"
#include <webserver_common_utils/http_sse_utils.h>
#include "../utils/telemetry_snapshot_utils.h"
#include "../logging.h"
#include "../../src/mqtt/mqtt_client.h"
#include "../../src/memory/memory_sampler.h"
#include <ArduinoJson.h>
#include <Arduino.h>

namespace {
constexpr uint32_t kSseSessionMaxDurationMs = 300000; // 5 minutes
constexpr uint32_t kCellUpdateWaitMs = 15000;
constexpr uint32_t kMonitorUpdateWaitMs = 500;
constexpr uint32_t kCellSseMaxActiveClients = 2;
constexpr size_t kMonitorEventBufferBytes = 512;
constexpr size_t kSseEventReserveOverheadBytes = 12;
constexpr size_t kSseEventMaxBytes = 1024;
constexpr uint32_t kCellSseRetryMs = 2000;

// MQTT topics for SSE refresh commands
static const char* kMonitorRefreshTopic = "batt-emu/mqtt-v1/rx/cmd/refresh/power";

struct SseMetricsInternal {
    // All fields accessed exclusively under g_sse_metrics_mux (portENTER_CRITICAL).
    // The spinlock provides the required memory barrier — volatile is not needed.
    uint32_t cell_connects = 0;
    uint32_t cell_disconnects = 0;
    uint32_t cell_send_failures = 0;
    uint32_t cell_ping_failures = 0;
    uint32_t cell_active_clients = 0;
    uint32_t cell_last_session_ms = 0;
    uint32_t cell_max_session_ms = 0;

    uint32_t monitor_connects = 0;
    uint32_t monitor_disconnects = 0;
    uint32_t monitor_send_failures = 0;
    uint32_t monitor_ping_failures = 0;
    uint32_t monitor_active_clients = 0;
    uint32_t monitor_last_session_ms = 0;
    uint32_t monitor_max_session_ms = 0;
};

SseMetricsInternal g_sse_metrics;
portMUX_TYPE g_sse_metrics_mux = portMUX_INITIALIZER_UNLOCKED;

void recordCellSessionEnd(uint32_t duration_ms) {
    portENTER_CRITICAL(&g_sse_metrics_mux);
    g_sse_metrics.cell_disconnects++;
    if (g_sse_metrics.cell_active_clients > 0) {
        g_sse_metrics.cell_active_clients--;
    }
    g_sse_metrics.cell_last_session_ms = duration_ms;
    if (duration_ms > g_sse_metrics.cell_max_session_ms) {
        g_sse_metrics.cell_max_session_ms = duration_ms;
    }
    portEXIT_CRITICAL(&g_sse_metrics_mux);
    MemorySampler::burst_clients_release();
}

void recordMonitorSessionEnd(uint32_t duration_ms) {
    portENTER_CRITICAL(&g_sse_metrics_mux);
    g_sse_metrics.monitor_disconnects++;
    if (g_sse_metrics.monitor_active_clients > 0) {
        g_sse_metrics.monitor_active_clients--;
    }
    g_sse_metrics.monitor_last_session_ms = duration_ms;
    if (duration_ms > g_sse_metrics.monitor_max_session_ms) {
        g_sse_metrics.monitor_max_session_ms = duration_ms;
    }
    portEXIT_CRITICAL(&g_sse_metrics_mux);
    MemorySampler::burst_clients_release();
}

}

void get_sse_runtime_metrics(SseRuntimeMetrics& out_metrics) {
    portENTER_CRITICAL(&g_sse_metrics_mux);
    out_metrics.cell_connects = g_sse_metrics.cell_connects;
    out_metrics.cell_disconnects = g_sse_metrics.cell_disconnects;
    out_metrics.cell_send_failures = g_sse_metrics.cell_send_failures;
    out_metrics.cell_ping_failures = g_sse_metrics.cell_ping_failures;
    out_metrics.cell_active_clients = g_sse_metrics.cell_active_clients;
    out_metrics.cell_last_session_ms = g_sse_metrics.cell_last_session_ms;
    out_metrics.cell_max_session_ms = g_sse_metrics.cell_max_session_ms;

    out_metrics.monitor_connects = g_sse_metrics.monitor_connects;
    out_metrics.monitor_disconnects = g_sse_metrics.monitor_disconnects;
    out_metrics.monitor_send_failures = g_sse_metrics.monitor_send_failures;
    out_metrics.monitor_ping_failures = g_sse_metrics.monitor_ping_failures;
    out_metrics.monitor_active_clients = g_sse_metrics.monitor_active_clients;
    out_metrics.monitor_last_session_ms = g_sse_metrics.monitor_last_session_ms;
    out_metrics.monitor_max_session_ms = g_sse_metrics.monitor_max_session_ms;
    portEXIT_CRITICAL(&g_sse_metrics_mux);
}

esp_err_t api_cell_data_sse_handler(httpd_req_t *req) {
    const uint32_t session_start_ms = millis();

    portENTER_CRITICAL(&g_sse_metrics_mux);
    if (g_sse_metrics.cell_active_clients >= kCellSseMaxActiveClients) {
        const uint32_t active_clients = g_sse_metrics.cell_active_clients;
        portEXIT_CRITICAL(&g_sse_metrics_mux);
        LOG_WARN("SSE", "Rejected cell-data SSE client: active=%u limit=%u",
                 static_cast<unsigned>(active_clients),
                 static_cast<unsigned>(kCellSseMaxActiveClients));
        httpd_resp_set_hdr(req, "Retry-After", "2");
        StaticJsonDocument<192> doc;
        doc["success"] = false;
        doc["retry_ms"] = kCellSseRetryMs;
        doc["message"] = "Too many active cell-data streams; retry shortly";
        httpd_resp_set_status(req, "429 Too Many Requests");
        return ApiResponseUtils::send_json_doc(req, doc);
    }
    g_sse_metrics.cell_connects++;
    g_sse_metrics.cell_active_clients++;
    portEXIT_CRITICAL(&g_sse_metrics_mux);
    MemorySampler::burst_clients_add();

    MqttClient::incrementCellDataSubscribers();
    LOG_DEBUG("SSE", "SSE client connected (subscribers: %d)", MqttClient::getCellDataSubscriberCount());

    if (HttpSseUtils::begin_sse(req) != ESP_OK || HttpSseUtils::send_retry_hint(req) != ESP_OK) {
        MqttClient::decrementCellDataSubscribers();
        recordCellSessionEnd(millis() - session_start_ms);
        return ESP_FAIL;
    }

    auto sendCellData = [req]() -> bool {
        CellDataCache::CellDataSnapshot snapshot;
        if (CellDataCache::get_cell_data_snapshot(snapshot) && snapshot.known) {
            String json = TelemetrySnapshotUtils::serialize_cell_data(snapshot);
            String event;
            if ((json.length() + kSseEventReserveOverheadBytes) <= kSseEventMaxBytes) {
                event = "data: " + json + "\n\n";
                event.reserve(json.length() + kSseEventReserveOverheadBytes);
            } else {
                String summary = String("{\"success\":true,\"mode\":\"summary\",\"cell_count\":") +
                                 String(snapshot.cell_count) +
                                 String(",\"message\":\"SSE payload capped; use /api/cell_data_page for details\"}");
                event = "data: " + summary + "\n\n";
            }

            if (event.length() > kSseEventMaxBytes) {
                event = "data: {\"success\":false,\"mode\":\"summary\",\"message\":\"SSE payload capped\"}\n\n";
            }

            const bool ok = (httpd_resp_send_chunk(req, event.c_str(), event.length()) == ESP_OK);
            if (!ok) {
                portENTER_CRITICAL(&g_sse_metrics_mux);
                g_sse_metrics.cell_send_failures++;
                portEXIT_CRITICAL(&g_sse_metrics_mux);
            }
            return ok;
        }

        String json = "{\"success\":false,\"mode\":\"unavailable\",\"message\":\"Waiting for transmitter data\"}";
        String event = "data: " + json + "\n\n";
        const bool ok = (httpd_resp_send_chunk(req, event.c_str(), event.length()) == ESP_OK);
        if (!ok) {
            portENTER_CRITICAL(&g_sse_metrics_mux);
            g_sse_metrics.cell_send_failures++;
            portEXIT_CRITICAL(&g_sse_metrics_mux);
        }
        return ok;
    };

    if (!sendCellData()) {
        recordCellSessionEnd(millis() - session_start_ms);
        MqttClient::decrementCellDataSubscribers();
        return ESP_FAIL;
    }

    TickType_t start_time = xTaskGetTickCount();
    const TickType_t max_duration = pdMS_TO_TICKS(kSseSessionMaxDurationMs);

    while ((xTaskGetTickCount() - start_time) < max_duration) {
        const bool changed = SSENotifier::waitForCellDataUpdate(kCellUpdateWaitMs);
        if (changed) {
            if (!sendCellData()) {
                break;
            }
        } else if (!HttpSseUtils::send_ping(req)) {
            portENTER_CRITICAL(&g_sse_metrics_mux);
            g_sse_metrics.cell_ping_failures++;
            portEXIT_CRITICAL(&g_sse_metrics_mux);
            break;
        }
    }

    HttpSseUtils::end_sse(req);
    MqttClient::decrementCellDataSubscribers();
    recordCellSessionEnd(millis() - session_start_ms);
    LOG_DEBUG("SSE", "SSE client disconnected (subscribers: %d)", MqttClient::getCellDataSubscriberCount());

    return ESP_OK;
}

esp_err_t api_monitor_sse_handler(httpd_req_t *req) {
    const uint32_t session_start_ms = millis();

    portENTER_CRITICAL(&g_sse_metrics_mux);
    g_sse_metrics.monitor_connects++;
    g_sse_metrics.monitor_active_clients++;
    portEXIT_CRITICAL(&g_sse_metrics_mux);
    MemorySampler::burst_clients_add();
    SSENotifier::monitorClientConnected();

    if (HttpSseUtils::begin_sse(req) != ESP_OK || HttpSseUtils::send_retry_hint(req) != ESP_OK) {
        SSENotifier::monitorClientDisconnected();
        recordMonitorSessionEnd(millis() - session_start_ms);
        return ESP_FAIL;
    }

    bool mqtt_refresh_sent = false;
    if (MqttClient::isEnabled() && MqttClient::isConnected()) {
        StaticJsonDocument<128> refresh_cmd;
        refresh_cmd["request_id"] = esp_random();
        refresh_cmd["stream"] = "power";

        char mqtt_payload[256];
        if (serializeJson(refresh_cmd, mqtt_payload, sizeof(mqtt_payload)) > 0) {
            if (MqttClient::publishJson(kMonitorRefreshTopic, mqtt_payload)) {
                mqtt_refresh_sent = true;
                LOG_DEBUG("SSE", "Published MQTT monitor refresh command");
            }
        }
    }
    if (!mqtt_refresh_sent) {
        LOG_WARN("SSE", "Monitor refresh command not sent: MQTT unavailable");
    }

    uint8_t last_soc = 255;
    int32_t last_power = INT32_MAX;
    uint32_t last_voltage = 0;

    char event_data[kMonitorEventBufferBytes];
    uint8_t current_soc = 0;
    int32_t current_power = 0;
    uint32_t current_voltage = 0;
    TelemetrySnapshotUtils::fill_snapshot_telemetry(current_soc, current_power, current_voltage);

    snprintf(event_data, sizeof(event_data),
             "data: {\"soc\":%d,\"power\":%ld,\"voltage_mv\":%u,\"voltage_v\":%.1f}\n\n",
             current_soc, current_power, current_voltage, current_voltage / 1000.0f);

    if (httpd_resp_send_chunk(req, event_data, strlen(event_data)) != ESP_OK) {
        portENTER_CRITICAL(&g_sse_metrics_mux);
        g_sse_metrics.monitor_send_failures++;
        portEXIT_CRITICAL(&g_sse_metrics_mux);
        SSENotifier::monitorClientDisconnected();
        recordMonitorSessionEnd(millis() - session_start_ms);
        return ESP_FAIL;
    }

    last_soc = current_soc;
    last_power = current_power;
    last_voltage = current_voltage;

    TickType_t start_time = xTaskGetTickCount();
    const TickType_t max_duration = pdMS_TO_TICKS(kSseSessionMaxDurationMs);

    while ((xTaskGetTickCount() - start_time) < max_duration) {
        if (SSENotifier::waitForUpdate(kMonitorUpdateWaitMs)) {
            TelemetrySnapshotUtils::fill_snapshot_telemetry(current_soc, current_power, current_voltage);

            if (current_soc != last_soc || current_power != last_power || current_voltage != last_voltage) {
                snprintf(event_data, sizeof(event_data),
                         "data: {\"soc\":%d,\"power\":%ld,\"voltage_mv\":%u,\"voltage_v\":%.1f}\n\n",
                         current_soc, current_power, current_voltage, current_voltage / 1000.0f);

                if (httpd_resp_send_chunk(req, event_data, strlen(event_data)) != ESP_OK) {
                    portENTER_CRITICAL(&g_sse_metrics_mux);
                    g_sse_metrics.monitor_send_failures++;
                    portEXIT_CRITICAL(&g_sse_metrics_mux);
                    break;
                }

                last_soc = current_soc;
                last_power = current_power;
                last_voltage = current_voltage;
            }
        } else {
            if (HttpSseUtils::send_ping(req) != ESP_OK) {
                portENTER_CRITICAL(&g_sse_metrics_mux);
                g_sse_metrics.monitor_ping_failures++;
                portEXIT_CRITICAL(&g_sse_metrics_mux);
                break;
            }
        }
    }

    HttpSseUtils::end_sse(req);
    SSENotifier::monitorClientDisconnected();
    recordMonitorSessionEnd(millis() - session_start_ms);
    return ESP_OK;
}
