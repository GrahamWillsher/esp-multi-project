#include "api_sse_handlers.h"

#include "../utils/transmitter_manager.h"
#include "../utils/sse_notifier.h"
#include "../utils/http_sse_utils.h"
#include "../logging.h"
#include "../../src/mqtt/mqtt_client.h"
#include "../../src/espnow/espnow_send.h"
#include "../page_definitions.h"

#include <Arduino.h>
#include <esp_now.h>
#include <esp32common/espnow/common.h>

namespace ESPNow {
extern uint8_t received_soc;
extern int32_t received_power;
extern uint32_t received_voltage_mv;
}

namespace {
struct SseMetricsInternal {
    volatile uint32_t cell_connects = 0;
    volatile uint32_t cell_disconnects = 0;
    volatile uint32_t cell_send_failures = 0;
    volatile uint32_t cell_ping_failures = 0;
    volatile uint32_t cell_active_clients = 0;
    volatile uint32_t cell_last_session_ms = 0;
    volatile uint32_t cell_max_session_ms = 0;

    volatile uint32_t monitor_connects = 0;
    volatile uint32_t monitor_disconnects = 0;
    volatile uint32_t monitor_send_failures = 0;
    volatile uint32_t monitor_ping_failures = 0;
    volatile uint32_t monitor_active_clients = 0;
    volatile uint32_t monitor_last_session_ms = 0;
    volatile uint32_t monitor_max_session_ms = 0;
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
    g_sse_metrics.cell_connects++;
    g_sse_metrics.cell_active_clients++;
    portEXIT_CRITICAL(&g_sse_metrics_mux);

    MqttClient::incrementCellDataSubscribers();
    LOG_DEBUG("[SSE]", "SSE client connected (subscribers: %d)", MqttClient::getCellDataSubscriberCount());

    if (HttpSseUtils::begin_sse(req) != ESP_OK || HttpSseUtils::send_retry_hint(req) != ESP_OK) {
        MqttClient::decrementCellDataSubscribers();
        recordCellSessionEnd(millis() - session_start_ms);
        return ESP_FAIL;
    }

    auto sendCellData = [req]() -> bool {
        TransmitterManager::CellDataSnapshot snapshot;
        if (TransmitterManager::getCellDataSnapshot(snapshot) && snapshot.known) {

            String json = "{\"success\":true,\"cells\":[";
            json.reserve(180 + (snapshot.cell_count * 16));
            for (uint16_t i = 0; i < snapshot.cell_count; i++) {
                if (i > 0) json += ",";
                json += String(snapshot.voltages_mV[i]);
            }
            json += "],\"balancing\":[";
            for (uint16_t i = 0; i < snapshot.cell_count; i++) {
                if (i > 0) json += ",";
                json += snapshot.balancing_status[i] ? "true" : "false";
            }
            json += "],\"cell_min_voltage_mV\":";
            json += String(snapshot.min_voltage_mV);
            json += ",\"cell_max_voltage_mV\":";
            json += String(snapshot.max_voltage_mV);
            json += ",\"balancing_active\":";
            json += snapshot.balancing_active ? "true" : "false";
            json += ",\"mode\":\"";
            json += snapshot.data_source;
            json += "\"}";

            String event = "data: " + json + "\n\n";
            event.reserve(json.length() + 12);
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
    const TickType_t max_duration = pdMS_TO_TICKS(300000);

    while ((xTaskGetTickCount() - start_time) < max_duration) {
        const bool changed = SSENotifier::waitForCellDataUpdate(15000);
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
    LOG_DEBUG("[SSE]", "SSE client disconnected (subscribers: %d)", MqttClient::getCellDataSubscriberCount());

    return ESP_OK;
}

esp_err_t api_monitor_sse_handler(httpd_req_t *req) {
    const uint32_t session_start_ms = millis();

    portENTER_CRITICAL(&g_sse_metrics_mux);
    g_sse_metrics.monitor_connects++;
    g_sse_metrics.monitor_active_clients++;
    portEXIT_CRITICAL(&g_sse_metrics_mux);

    if (HttpSseUtils::begin_sse(req) != ESP_OK || HttpSseUtils::send_retry_hint(req) != ESP_OK) {
        recordMonitorSessionEnd(millis() - session_start_ms);
        return ESP_FAIL;
    }

    msg_subtype data_subtype = get_subtype_for_uri("/monitor2");

    if (TransmitterManager::isMACKnown()) {
        request_data_t req_msg = { msg_request_data, data_subtype };
        esp_err_t result = esp_now_send(TransmitterManager::getMAC(), (const uint8_t*)&req_msg, sizeof(req_msg));
        if (result == ESP_OK) {
            LOG_DEBUG("SSE: Sent REQUEST_DATA (subtype=%d) to transmitter", data_subtype);
        }
    }

    uint8_t last_soc = 255;
    int32_t last_power = INT32_MAX;
    uint32_t last_voltage = 0;

    char event_data[512];
    uint8_t current_soc = ESPNow::received_soc;
    int32_t current_power = ESPNow::received_power;
    uint32_t current_voltage = ESPNow::received_voltage_mv;

    snprintf(event_data, sizeof(event_data),
             "data: {\"soc\":%d,\"power\":%ld,\"voltage_mv\":%u,\"voltage_v\":%.1f}\n\n",
             current_soc, current_power, current_voltage, current_voltage / 1000.0f);

    if (httpd_resp_send_chunk(req, event_data, strlen(event_data)) != ESP_OK) {
        portENTER_CRITICAL(&g_sse_metrics_mux);
        g_sse_metrics.monitor_send_failures++;
        portEXIT_CRITICAL(&g_sse_metrics_mux);
        recordMonitorSessionEnd(millis() - session_start_ms);
        return ESP_FAIL;
    }

    last_soc = current_soc;
    last_power = current_power;
    last_voltage = current_voltage;

    TickType_t start_time = xTaskGetTickCount();
    const TickType_t max_duration = pdMS_TO_TICKS(300000);

    while ((xTaskGetTickCount() - start_time) < max_duration) {
        EventBits_t bits = xEventGroupWaitBits(
            SSENotifier::getEventGroup(),
            (1 << 0),
            pdTRUE,
            pdFALSE,
            pdMS_TO_TICKS(500)
        );

        if (bits & (1 << 0)) {
            current_soc = ESPNow::received_soc;
            current_power = ESPNow::received_power;
            current_voltage = ESPNow::received_voltage_mv;

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

    if (TransmitterManager::isMACKnown()) {
        abort_data_t abort_msg = { msg_abort_data, data_subtype };
        esp_now_send(TransmitterManager::getMAC(), (const uint8_t*)&abort_msg, sizeof(abort_msg));
        LOG_DEBUG("SSE: Sent ABORT_DATA (subtype=%d) to transmitter", data_subtype);
    }

    HttpSseUtils::end_sse(req);
    recordMonitorSessionEnd(millis() - session_start_ms);
    return ESP_OK;
}
