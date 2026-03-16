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

esp_err_t api_cell_data_sse_handler(httpd_req_t *req) {
    MqttClient::incrementCellDataSubscribers();
    LOG_DEBUG("[SSE]", "SSE client connected (subscribers: %d)", MqttClient::getCellDataSubscriberCount());

    if (HttpSseUtils::begin_sse(req) != ESP_OK || HttpSseUtils::send_retry_hint(req) != ESP_OK) {
        return ESP_FAIL;
    }

    auto sendCellData = [req]() -> bool {
        if (TransmitterManager::hasCellData()) {
            const uint16_t* voltages = TransmitterManager::getCellVoltages();
            const bool* balancing = TransmitterManager::getCellBalancingStatus();
            uint16_t min_voltage = TransmitterManager::getCellMinVoltage();
            uint16_t max_voltage = TransmitterManager::getCellMaxVoltage();
            bool balancing_active = TransmitterManager::isBalancingActive();
            uint16_t cell_count = TransmitterManager::getCellCount();

            String json = "{\"success\":true,\"cells\":[";
            for (uint16_t i = 0; i < cell_count; i++) {
                if (i > 0) json += ",";
                json += String(voltages[i]);
            }
            json += "],\"balancing\":[";
            for (uint16_t i = 0; i < cell_count; i++) {
                if (i > 0) json += ",";
                json += balancing[i] ? "true" : "false";
            }
            json += "],\"cell_min_voltage_mV\":";
            json += String(min_voltage);
            json += ",\"cell_max_voltage_mV\":";
            json += String(max_voltage);
            json += ",\"balancing_active\":";
            json += balancing_active ? "true" : "false";
            json += ",\"mode\":\"";
            json += TransmitterManager::getCellDataSource();
            json += "\"}";

            String event = "data: " + json + "\n\n";
            return httpd_resp_send_chunk(req, event.c_str(), event.length()) == ESP_OK;
        }

        String json = "{\"success\":false,\"mode\":\"unavailable\",\"message\":\"Waiting for transmitter data\"}";
        String event = "data: " + json + "\n\n";
        return httpd_resp_send_chunk(req, event.c_str(), event.length()) == ESP_OK;
    };

    if (!sendCellData()) {
        return ESP_FAIL;
    }

    TickType_t start_time = xTaskGetTickCount();
    const TickType_t max_duration = pdMS_TO_TICKS(300000);
    const TickType_t poll_interval = pdMS_TO_TICKS(500);

    while ((xTaskGetTickCount() - start_time) < max_duration) {
        vTaskDelay(poll_interval);
        if (!sendCellData()) {
            break;
        }
    }

    HttpSseUtils::end_sse(req);
    MqttClient::decrementCellDataSubscribers();
    LOG_DEBUG("[SSE]", "SSE client disconnected (subscribers: %d)", MqttClient::getCellDataSubscriberCount());

    return ESP_OK;
}

esp_err_t api_monitor_sse_handler(httpd_req_t *req) {
    if (HttpSseUtils::begin_sse(req) != ESP_OK || HttpSseUtils::send_retry_hint(req) != ESP_OK) {
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
                    break;
                }

                last_soc = current_soc;
                last_power = current_power;
                last_voltage = current_voltage;
            }
        } else {
            if (HttpSseUtils::send_ping(req) != ESP_OK) {
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
    return ESP_OK;
}
