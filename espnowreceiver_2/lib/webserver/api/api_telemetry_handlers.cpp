#include "api_telemetry_handlers.h"
#include "api_sse_handlers.h"
#include "api_response_utils.h"
#include "webserver_metrics.h"
#include "../webserver.h"
#include "api_field_builders.h"

#include "../utils/transmitter_manager.h"
#include "../utils/cell_data_cache.h"
#include <webserver_common_utils/http_json_utils.h>
#include "../utils/receiver_config_manager.h"
#include "../utils/telemetry_snapshot_utils.h"
#include "../logging.h"

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <firmware_version.h>
#include <firmware_metadata.h>
#include <ArduinoJson.h>
#include <esp_now.h>
#include <esp32common/espnow/common.h>
#include <freertos/queue.h>
#include "../../src/mqtt/mqtt_client.h"

extern bool test_mode_enabled;
extern volatile int g_test_soc;
extern volatile int32_t g_test_power;
extern volatile uint32_t g_test_voltage_mv;

namespace ESPNow {
extern QueueHandle_t queue;
extern volatile uint32_t rx_callback_count;
extern volatile uint32_t rx_queue_drop_count;
extern volatile uint32_t rx_queue_high_watermark;
}

using namespace WebserverMetrics;

esp_err_t api_data_handler(httpd_req_t *req) {
    HttpHandlerTimer handler_timer(HM_DATA);
    StaticJsonDocument<512> doc;
    
    ApiFieldBuilders::addChipInfo(doc);
    ApiFieldBuilders::addWiFiFields(doc);

    String json;
    json.reserve(256);
    serializeJson(doc, json);
    return HttpJsonUtils::send_json(req, json.c_str());
}


esp_err_t api_get_receiver_info_handler(httpd_req_t *req) {
    HttpHandlerTimer handler_timer(HM_GET_RECEIVER_INFO);
    String json = ReceiverConfigManager::getReceiverInfoJson();
    return HttpJsonUtils::send_json(req, json.c_str());
}

esp_err_t api_monitor_handler(httpd_req_t *req) {
    HttpHandlerTimer handler_timer(HM_MONITOR);
    StaticJsonDocument<192> doc;
    const char* mode = test_mode_enabled ? "simulated" : "live";
    uint8_t soc = 0;
    int32_t power = 0;
    uint32_t voltage_mv = 0;

    if (test_mode_enabled) {
        soc = g_test_soc;
        power = g_test_power;
        voltage_mv = g_test_voltage_mv;
    } else {
        TelemetrySnapshotUtils::fill_snapshot_telemetry(soc, power, voltage_mv);
    }

    doc["mode"] = mode;
    doc["soc"] = soc;
    doc["power"] = power;
    doc["voltage_mv"] = voltage_mv;
    doc["voltage_v"] = static_cast<float>(voltage_mv) / 1000.0f;

    String json;
    json.reserve(128);
    serializeJson(doc, json);
    return HttpJsonUtils::send_json(req, json.c_str());
}

esp_err_t api_cell_data_handler(httpd_req_t *req) {
    HttpHandlerTimer handler_timer(HM_CELL_DATA);
    CellDataCache::CellDataSnapshot snapshot;
    if (CellDataCache::get_cell_data_snapshot(snapshot) && snapshot.known) {
        String json = TelemetrySnapshotUtils::serialize_cell_data(snapshot);
        return HttpJsonUtils::send_json(req, json.c_str());
    }

    const char* json = "{\"success\":false,\"mode\":\"unavailable\",\"message\":\"No cell data received from transmitter\"}";
    return HttpJsonUtils::send_json(req, json);
}

esp_err_t api_dashboard_data_handler(httpd_req_t *req) {
    HttpHandlerTimer handler_timer(HM_DASHBOARD_DATA);
    StaticJsonDocument<384> doc;

    JsonObject transmitter = ApiFieldBuilders::addTransmitterObject(doc);
    transmitter["connected"] = TransmitterManager::isTransmitterConnected();
    transmitter["ip"] = TransmitterManager::getIPString();
    transmitter["is_static"] = TransmitterManager::isStaticIP();
    transmitter["mac"] = TransmitterManager::getMACString();
    
    String tx_firmware = "Unknown";
    if (TransmitterManager::hasMetadata()) {
        uint8_t major, minor, patch;
        TransmitterManager::getMetadataVersion(major, minor, patch);
        char version_str[12];
        ApiFieldBuilders::formatVersionString(version_str, sizeof(version_str), major, minor, patch);
        tx_firmware = String(version_str);
    }
    transmitter["firmware"] = tx_firmware;

    JsonObject receiver = ApiFieldBuilders::addReceiverObject(doc);
    receiver["is_static"] = true;

    String json;
    json.reserve(256);
    serializeJson(doc, json);
    return HttpJsonUtils::send_json(req, json.c_str());
}


esp_err_t api_version_handler(httpd_req_t *req) {
    HttpHandlerTimer handler_timer(HM_VERSION);
    StaticJsonDocument<1024> doc;

    const bool receiver_metadata_valid = FirmwareMetadata::isValid(FirmwareMetadata::metadata);
    String receiver_device = "unknown";
    String receiver_device_type = "UNKNOWN";
    String receiver_version = "Unknown";
    uint32_t receiver_version_number = 0;
    String receiver_build_date = "";
    String receiver_build_time = "";

    if (receiver_metadata_valid) {
        receiver_device = String(FirmwareMetadata::metadata.env_name);
        receiver_device_type = String(FirmwareMetadata::metadata.device_type);
        receiver_version_number = (uint32_t)FirmwareMetadata::metadata.version_major * 10000 +
                                  (uint32_t)FirmwareMetadata::metadata.version_minor * 100 +
                                  (uint32_t)FirmwareMetadata::metadata.version_patch;
        receiver_version = formatVersion(receiver_version_number);
        receiver_build_date = String(FirmwareMetadata::metadata.build_date);
    }

    String transmitter_version = "Unknown";
    uint32_t transmitter_version_number = 0;
    bool version_compatible = false;
    String transmitter_build_date = "";
    String transmitter_build_time = "";
    bool has_metadata = TransmitterManager::hasMetadata();
    bool metadata_valid = TransmitterManager::isMetadataValid();

    if (has_metadata) {
        uint8_t major, minor, patch;
        TransmitterManager::getMetadataVersion(major, minor, patch);
        transmitter_version_number = major * 10000 + minor * 100 + patch;
        transmitter_version = formatVersion(transmitter_version_number);
        version_compatible = isVersionCompatible(transmitter_version_number);
        transmitter_build_date = String(TransmitterManager::getMetadataBuildDate());
    }

    doc["device"] = receiver_device;
    doc["device_type"] = receiver_device_type;
    doc["version"] = receiver_version;
    doc["version_number"] = receiver_version_number;
    doc["build_date"] = receiver_build_date;
    doc["build_time"] = receiver_build_time;
    doc["metadata_valid"] = receiver_metadata_valid;
    doc["transmitter_version"] = transmitter_version;
    doc["transmitter_version_number"] = transmitter_version_number;
    doc["transmitter_build_date"] = transmitter_build_date;
    doc["transmitter_build_time"] = transmitter_build_time;
    doc["transmitter_compatible"] = version_compatible;
    doc["transmitter_metadata_valid"] = metadata_valid;
    doc["uptime"] = millis() / 1000;
    doc["heap_free"] = ESP.getFreeHeap();
    doc["wifi_channel"] = WiFi.channel();

    String json;
    json.reserve(640);
    serializeJson(doc, json);
    return HttpJsonUtils::send_json(req, json.c_str());
}

esp_err_t api_firmware_info_handler(httpd_req_t *req) {
    HttpHandlerTimer handler_timer(HM_FIRMWARE_INFO);
    StaticJsonDocument<384> doc;

    if (FirmwareMetadata::isValid(FirmwareMetadata::metadata)) {
        char version[16];
        snprintf(version, sizeof(version), "%d.%d.%d",
                 FirmwareMetadata::metadata.version_major,
                 FirmwareMetadata::metadata.version_minor,
                 FirmwareMetadata::metadata.version_patch);

        doc["valid"] = true;
        doc["env"] = FirmwareMetadata::metadata.env_name;
        doc["device"] = FirmwareMetadata::metadata.device_type;
        doc["version"] = version;
        doc["build_date"] = FirmwareMetadata::metadata.build_date;
    } else {
        doc["valid"] = false;
        doc["env"] = "";
        doc["device"] = "";
        doc["version"] = "";
        doc["build_date"] = "";
        doc["message"] = "Embedded firmware metadata unavailable";
    }

    String json;
    serializeJson(doc, json);
    return HttpJsonUtils::send_json(req, json.c_str());
}

esp_err_t api_transmitter_metadata_handler(httpd_req_t *req) {
    HttpHandlerTimer handler_timer(HM_TRANSMITTER_METADATA);
    StaticJsonDocument<512> doc;

    if (TransmitterManager::hasMetadata()) {
        uint8_t major, minor, patch;
        TransmitterManager::getMetadataVersion(major, minor, patch);
        bool valid = TransmitterManager::isMetadataValid();
        char version[16];
        snprintf(version, sizeof(version), "%d.%d.%d", major, minor, patch);

        doc["status"] = "received";
        doc["valid"] = valid;
        doc["env"] = TransmitterManager::getMetadataEnv();
        doc["device"] = TransmitterManager::getMetadataDevice();
        doc["version"] = version;
        doc["build_date"] = TransmitterManager::getMetadataBuildDate();
    } else {
        doc["status"] = "waiting";
        doc["valid"] = false;
        doc["message"] = "No metadata received from transmitter yet";
    }

    String json;
    serializeJson(doc, json);
    return HttpJsonUtils::send_json(req, json.c_str());
}

esp_err_t api_transmitter_health_handler(httpd_req_t *req) {
    HttpHandlerTimer handler_timer(HM_TRANSMITTER_HEALTH);
    StaticJsonDocument<256> doc;

    uint64_t uptime_ms = TransmitterManager::getUptimeMs();
    uint64_t unix_time = TransmitterManager::getUnixTime();
    uint8_t time_source = TransmitterManager::getTimeSource();

    doc["success"] = true;
    doc["uptime_ms"] = uptime_ms;
    doc["unix_time"] = unix_time;
    doc["time_source"] = time_source;
    doc["mqtt_connected"] = TransmitterManager::isMqttConnected();
    doc["ethernet_connected"] = TransmitterManager::isEthernetConnected();

    String json;
    json.reserve(160);
    serializeJson(doc, json);
    return HttpJsonUtils::send_json(req, json.c_str());
}

esp_err_t api_static_specs_handler(httpd_req_t *req) {
    HttpHandlerTimer handler_timer(HM_STATIC_SPECS);
    if (!TransmitterManager::hasStaticSpecs()) {
        return HttpJsonUtils::send_json(req, "{\"success\":false,\"error\":\"Static specs not available\"}");
    }

    String specs_json = TransmitterManager::getStaticSpecsJson();
    return HttpJsonUtils::send_json(req, specs_json.c_str());
}

esp_err_t api_battery_specs_handler(httpd_req_t *req) {
    HttpHandlerTimer handler_timer(HM_BATTERY_SPECS);
    String specs_json = TransmitterManager::getBatterySpecsJson();
    if (specs_json.length() == 0) {
        return HttpJsonUtils::send_json(req, "{\"success\":false,\"error\":\"Battery specs not available\"}");
    }

    return HttpJsonUtils::send_json(req, specs_json.c_str());
}

esp_err_t api_inverter_specs_handler(httpd_req_t *req) {
    HttpHandlerTimer handler_timer(HM_INVERTER_SPECS);
    String specs_json = TransmitterManager::getInverterSpecsJson();
    if (specs_json.length() == 0) {
        return HttpJsonUtils::send_json(req, "{\"success\":false,\"error\":\"Inverter specs not available\"}");
    }

    return HttpJsonUtils::send_json(req, specs_json.c_str());
}

esp_err_t api_get_event_logs_handler(httpd_req_t *req) {
    HttpHandlerTimer handler_timer(HM_GET_EVENT_LOGS);
    char query[256] = {0};
    int limit = 50;

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char param[32];
        if (httpd_query_key_value(query, "limit", param, sizeof(param)) == ESP_OK) {
            limit = atoi(param);
            if (limit < 1) limit = 1;
            if (limit > 500) limit = 500;
        }
    }

    if (TransmitterManager::hasEventLogs()) {
        std::vector<TransmitterManager::EventLogEntry> logs;
        uint32_t last_update_ms = 0;
        TransmitterManager::getEventLogsSnapshot(logs, &last_update_ms);
        DynamicJsonDocument doc(4096);
        doc["success"] = true;
        doc["event_count"] = static_cast<uint32_t>(logs.size());
        doc["source"] = "mqtt";
        doc["last_update_ms"] = last_update_ms;
        JsonArray events = doc.createNestedArray("events");

        const int max_events = (limit < (int)logs.size()) ? limit : (int)logs.size();
        for (int i = 0; i < max_events; i++) {
            const auto& entry = logs[i];
            JsonObject evt = events.createNestedObject();
            evt["timestamp"] = entry.timestamp;
            evt["level"] = entry.level;
            evt["data"] = entry.data;
            evt["message"] = entry.message;
        }

        String json;
        serializeJson(doc, json);
        return HttpJsonUtils::send_json(req, json.c_str());
    }

    if (!TransmitterManager::isIPKnown()) {
        return HttpJsonUtils::send_json(req, "{\"success\":false,\"error\":\"Transmitter not connected\"}");
    }

    String transmitter_url = TransmitterManager::getURL() + "/api/get_event_logs?limit=" + String(limit);
    HTTPClient http;
    http.begin(transmitter_url);
    http.setTimeout(5000);
    const uint32_t start_ms = millis();
    int httpCode = http.GET();
    const uint32_t latency_ms = millis() - start_ms;
    recordEventLogProxyResult(httpCode, latency_ms);

    if (httpCode == 200) {
        String response = http.getString();
        http.end();
        return HttpJsonUtils::send_json(req, response.c_str());
    }

    http.end();
    StaticJsonDocument<128> doc;
    doc["success"] = false;
    if (httpCode == -1) {
        doc["error"] = "Failed to connect to transmitter";
    } else {
        String error_msg = "Transmitter returned HTTP " + String(httpCode);
        doc["error"] = error_msg;
    }

    return ApiResponseUtils::send_json_doc(req, doc);
}

esp_err_t api_system_metrics_handler(httpd_req_t *req) {
    HttpHandlerTimer handler_timer(HM_SYSTEM_METRICS);
    DynamicJsonDocument doc(2048);

    const uint32_t callback_count = ESPNow::rx_callback_count;
    const uint32_t drop_count = ESPNow::rx_queue_drop_count;
    const uint32_t high_watermark = ESPNow::rx_queue_high_watermark;
    const uint32_t queue_depth = (ESPNow::queue != nullptr) ? static_cast<uint32_t>(uxQueueMessagesWaiting(ESPNow::queue)) : 0;
    const uint32_t queue_size = (ESPNow::queue != nullptr)
                                    ? static_cast<uint32_t>(uxQueueMessagesWaiting(ESPNow::queue) + uxQueueSpacesAvailable(ESPNow::queue))
                                    : 0;

    EventLogProxyMetricsSnapshot proxy_metrics{};
    getEventLogProxyMetrics(proxy_metrics);

    doc["success"] = true;
    doc["uptime_s"] = millis() / 1000;

    JsonObject heap = doc.createNestedObject("heap");
    heap["free"] = ESP.getFreeHeap();
    heap["min_free"] = ESP.getMinFreeHeap();
    heap["max_alloc"] = ESP.getMaxAllocHeap();

    JsonObject wifi = doc.createNestedObject("wifi");
    wifi["connected"] = WiFi.isConnected();
    wifi["rssi"] = WiFi.RSSI();
    wifi["channel"] = WiFi.channel();
    wifi["ip"] = WiFi.localIP().toString();

    JsonObject espnow = doc.createNestedObject("espnow_rx_queue");
    espnow["size"] = queue_size;
    espnow["depth"] = queue_depth;
    espnow["callbacks"] = callback_count;
    espnow["drops"] = drop_count;
    espnow["high_watermark"] = high_watermark;
    espnow["drop_rate"] = (callback_count > 0)
                              ? static_cast<float>(drop_count) / static_cast<float>(callback_count)
                              : 0.0f;

    JsonObject sse = doc.createNestedObject("sse");
    sse["cell_stream_clients"] = MqttClient::getCellDataSubscriberCount();
    sse["event_log_clients"] = MqttClient::getEventLogSubscriberCount();
    sse["cell_stream_state"] = MqttClient::getCellDataSubscriptionState();

    SseRuntimeMetrics sse_runtime{};
    get_sse_runtime_metrics(sse_runtime);
    const uint32_t active_long_lived_streams = sse_runtime.cell_active_clients + sse_runtime.monitor_active_clients;
    JsonObject sse_runtime_json = sse.createNestedObject("runtime");
    sse_runtime_json["cell_connects"] = sse_runtime.cell_connects;
    sse_runtime_json["cell_disconnects"] = sse_runtime.cell_disconnects;
    sse_runtime_json["cell_send_failures"] = sse_runtime.cell_send_failures;
    sse_runtime_json["cell_ping_failures"] = sse_runtime.cell_ping_failures;
    sse_runtime_json["cell_active_clients"] = sse_runtime.cell_active_clients;
    sse_runtime_json["cell_last_session_ms"] = sse_runtime.cell_last_session_ms;
    sse_runtime_json["cell_max_session_ms"] = sse_runtime.cell_max_session_ms;
    sse_runtime_json["monitor_connects"] = sse_runtime.monitor_connects;
    sse_runtime_json["monitor_disconnects"] = sse_runtime.monitor_disconnects;
    sse_runtime_json["monitor_send_failures"] = sse_runtime.monitor_send_failures;
    sse_runtime_json["monitor_ping_failures"] = sse_runtime.monitor_ping_failures;
    sse_runtime_json["monitor_active_clients"] = sse_runtime.monitor_active_clients;
    sse_runtime_json["monitor_last_session_ms"] = sse_runtime.monitor_last_session_ms;
    sse_runtime_json["monitor_max_session_ms"] = sse_runtime.monitor_max_session_ms;

    JsonObject event_logs_proxy = doc.createNestedObject("event_logs_proxy");
    event_logs_proxy["requests"] = proxy_metrics.total_requests;
    event_logs_proxy["success"] = proxy_metrics.total_success;
    event_logs_proxy["timeouts"] = proxy_metrics.total_timeouts;
    event_logs_proxy["http_errors"] = proxy_metrics.total_http_errors;
    event_logs_proxy["last_http_code"] = proxy_metrics.last_http_code;
    event_logs_proxy["last_latency_ms"] = proxy_metrics.last_latency_ms;
    event_logs_proxy["avg_success_latency_ms"] = proxy_metrics.avg_success_latency_ms;

    JsonObject http_handlers = doc.createNestedObject("http_handlers");
    for (uint8_t i = 0; i < httpHandlerCount(); i++) {
        HttpHandlerMetricsSnapshot handler_metrics{};
        const HttpHandlerMetricId metric_id = static_cast<HttpHandlerMetricId>(i);
        getHttpHandlerMetrics(metric_id, handler_metrics);

        JsonObject h = http_handlers.createNestedObject(httpHandlerName(metric_id));
        h["calls"] = handler_metrics.calls;
        h["last_ms"] = handler_metrics.last_ms;
        h["max_ms"] = handler_metrics.max_ms;
        h["avg_ms"] = handler_metrics.avg_ms;
    }

    WebserverRuntimeMetrics webserver_metrics{};
    get_webserver_runtime_metrics(webserver_metrics);
    JsonObject webserver = doc.createNestedObject("webserver");
    webserver["running"] = webserver_metrics.running;
    webserver["server_port"] = webserver_metrics.server_port;
    webserver["max_open_sockets"] = webserver_metrics.max_open_sockets;
    webserver["max_uri_handlers"] = webserver_metrics.max_uri_handlers;
    webserver["registered_handlers"] = webserver_metrics.registered_handlers;
    webserver["expected_handlers"] = webserver_metrics.expected_handlers;
    webserver["handler_registration_ratio"] = (webserver_metrics.expected_handlers > 0)
                                                ? static_cast<float>(webserver_metrics.registered_handlers) / static_cast<float>(webserver_metrics.expected_handlers)
                                                : 0.0f;
    webserver["task_stack_size"] = webserver_metrics.task_stack_size;
    webserver["task_priority"] = webserver_metrics.task_priority;
    webserver["recv_wait_timeout_s"] = webserver_metrics.recv_wait_timeout_s;
    webserver["send_wait_timeout_s"] = webserver_metrics.send_wait_timeout_s;
    webserver["lru_purge_enabled"] = webserver_metrics.lru_purge_enabled;
    webserver["active_long_lived_streams"] = active_long_lived_streams;
    webserver["socket_utilization_estimate"] = (webserver_metrics.max_open_sockets > 0)
                                              ? static_cast<float>(active_long_lived_streams) / static_cast<float>(webserver_metrics.max_open_sockets)
                                              : 0.0f;
    webserver["init_attempts"] = webserver_metrics.init_attempts;
    webserver["init_successes"] = webserver_metrics.init_successes;
    webserver["init_failures"] = webserver_metrics.init_failures;

    JsonObject transmitter = doc.createNestedObject("transmitter");
    transmitter["connected"] = TransmitterManager::isTransmitterConnected();
    transmitter["ip_known"] = TransmitterManager::isIPKnown();
    transmitter["metadata"] = TransmitterManager::hasMetadata();

    String json;
    json.reserve(1024);
    serializeJson(doc, json);
    return HttpJsonUtils::send_json(req, json.c_str());
}
