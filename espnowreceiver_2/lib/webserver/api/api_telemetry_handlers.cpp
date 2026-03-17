#include "api_telemetry_handlers.h"
#include "api_sse_handlers.h"
#include "../webserver.h"

#include "../utils/transmitter_manager.h"
#include "../utils/http_json_utils.h"
#include "../utils/receiver_config_manager.h"
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

extern bool& test_mode_enabled;
extern volatile int& g_test_soc;
extern volatile int32_t& g_test_power;
extern volatile uint32_t& g_test_voltage_mv;

namespace ESPNow {
extern uint8_t received_soc;
extern int32_t received_power;
extern uint32_t received_voltage_mv;
extern QueueHandle_t queue;
extern volatile uint32_t rx_callback_count;
extern volatile uint32_t rx_queue_drop_count;
extern volatile uint32_t rx_queue_high_watermark;
}

namespace {
enum HttpHandlerMetricId : uint8_t {
    HM_DATA = 0,
    HM_GET_RECEIVER_INFO,
    HM_MONITOR,
    HM_GET_DATA_SOURCE,
    HM_CELL_DATA,
    HM_DASHBOARD_DATA,
    HM_TRANSMITTER_IP,
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

struct HttpHandlerMetrics {
    volatile uint32_t calls = 0;
    volatile uint32_t last_ms = 0;
    volatile uint32_t max_ms = 0;
    volatile uint64_t total_ms = 0;
};

HttpHandlerMetrics g_http_handler_metrics[HM_COUNT];
const char* const k_http_handler_names[HM_COUNT] = {
    "api_data",
    "api_get_receiver_info",
    "api_monitor",
    "api_get_data_source",
    "api_cell_data",
    "api_dashboard_data",
    "api_transmitter_ip",
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

struct EventLogProxyMetrics {
    volatile uint32_t total_requests = 0;
    volatile uint32_t total_success = 0;
    volatile uint32_t total_timeouts = 0;
    volatile uint32_t total_http_errors = 0;
    volatile int32_t last_http_code = 0;
    volatile uint32_t last_latency_ms = 0;
    volatile uint64_t total_latency_ms = 0;
};

EventLogProxyMetrics g_event_log_proxy_metrics;
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

class HttpHandlerTimer {
public:
    explicit HttpHandlerTimer(HttpHandlerMetricId id)
        : id_(id), start_ms_(millis()) {}

    ~HttpHandlerTimer() {
        recordHttpHandlerLatency(id_, millis() - start_ms_);
    }

private:
    HttpHandlerMetricId id_;
    uint32_t start_ms_;
};

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
}

esp_err_t api_data_handler(httpd_req_t *req) {
    HttpHandlerTimer handler_timer(HM_DATA);
    char json[1024];

    String ssid = WiFi.SSID();
    String ip = WiFi.localIP().toString();
    String mac = WiFi.macAddress();
    int channel = WiFi.channel();

    String chipModel = ESP.getChipModel();
    uint8_t chipRevision = ESP.getChipRevision();
    uint64_t efuseMac = ESP.getEfuseMac();

    char efuseMacStr[18];
    snprintf(efuseMacStr, sizeof(efuseMacStr),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             (uint8_t)(efuseMac >> 40), (uint8_t)(efuseMac >> 32),
             (uint8_t)(efuseMac >> 24), (uint8_t)(efuseMac >> 16),
             (uint8_t)(efuseMac >> 8), (uint8_t)(efuseMac));

    snprintf(json, sizeof(json),
             "{\"chipModel\":\"%s\",\"chipRevision\":%d,\"efuseMac\":\"%s\"," \
             "\"ssid\":\"%s\",\"ip\":\"%s\",\"mac\":\"%s\",\"channel\":%d}",
             chipModel.c_str(), chipRevision, efuseMacStr,
             ssid.c_str(), ip.c_str(), mac.c_str(), channel);

    return HttpJsonUtils::send_json(req, json);
}

esp_err_t api_get_receiver_info_handler(httpd_req_t *req) {
    HttpHandlerTimer handler_timer(HM_GET_RECEIVER_INFO);
    String json = ReceiverConfigManager::getReceiverInfoJson();
    return HttpJsonUtils::send_json(req, json.c_str());
}

esp_err_t api_monitor_handler(httpd_req_t *req) {
    HttpHandlerTimer handler_timer(HM_MONITOR);
    char json[512];
    const char* mode = test_mode_enabled ? "simulated" : "live";
    uint8_t soc = test_mode_enabled ? g_test_soc : ESPNow::received_soc;
    int32_t power = test_mode_enabled ? g_test_power : ESPNow::received_power;
    uint32_t voltage_mv = test_mode_enabled ? g_test_voltage_mv : ESPNow::received_voltage_mv;

    snprintf(json, sizeof(json),
             "{\"mode\":\"%s\",\"soc\":%d,\"power\":%ld,\"voltage_mv\":%u,\"voltage_v\":%.1f}",
             mode, soc, power, voltage_mv, voltage_mv / 1000.0f);

    return HttpJsonUtils::send_json(req, json);
}

esp_err_t api_get_data_source_handler(httpd_req_t *req) {
    HttpHandlerTimer handler_timer(HM_GET_DATA_SOURCE);
    const char* mode = "live";
    char json[96];
    snprintf(json, sizeof(json), "{\"mode\":\"%s\"}", mode);
    return HttpJsonUtils::send_json(req, json);
}

esp_err_t api_cell_data_handler(httpd_req_t *req) {
    HttpHandlerTimer handler_timer(HM_CELL_DATA);
    TransmitterManager::CellDataSnapshot snapshot;
    if (TransmitterManager::getCellDataSnapshot(snapshot) && snapshot.known) {

        String json = "{\"success\":true,\"mode\":\"live\",\"cells\":[";
            json.reserve(192 + (snapshot.cell_count * 16));
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

        return HttpJsonUtils::send_json(req, json.c_str());
    }

    const char* json = "{\"success\":false,\"mode\":\"unavailable\",\"message\":\"No cell data received from transmitter\"}";
    return HttpJsonUtils::send_json(req, json);
}

esp_err_t api_dashboard_data_handler(httpd_req_t *req) {
    HttpHandlerTimer handler_timer(HM_DASHBOARD_DATA);
    char json[2048];

    bool tx_connected = TransmitterManager::isTransmitterConnected();
    String tx_ip = TransmitterManager::getIPString();
    bool tx_is_static = TransmitterManager::isStaticIP();
    String tx_mac = TransmitterManager::getMACString();
    String tx_firmware = "Unknown";

    if (TransmitterManager::hasMetadata()) {
        uint8_t major, minor, patch;
        TransmitterManager::getMetadataVersion(major, minor, patch);
        char version_str[12];
        snprintf(version_str, sizeof(version_str), "%d.%d.%d", major, minor, patch);
        tx_firmware = String(version_str);
    }

    snprintf(json, sizeof(json),
        "{"
        "\"transmitter\":{"
            "\"connected\":%s,"
            "\"ip\":\"%s\","
            "\"is_static\":%s,"
            "\"mac\":\"%s\","
            "\"firmware\":\"%s\""
        "},"
        "\"receiver\":{"
            "\"is_static\":true"
        "}"
        "}",
        tx_connected ? "true" : "false",
        tx_ip.c_str(),
        tx_is_static ? "true" : "false",
        tx_mac.c_str(),
        tx_firmware.c_str()
    );

    return HttpJsonUtils::send_json(req, json);
}

esp_err_t api_transmitter_ip_handler(httpd_req_t *req) {
    HttpHandlerTimer handler_timer(HM_TRANSMITTER_IP);
    char json[512];

    if (TransmitterManager::isIPKnown()) {
        const uint8_t* ip = TransmitterManager::getIP();
        const uint8_t* gateway = TransmitterManager::getGateway();
        const uint8_t* subnet = TransmitterManager::getSubnet();

        snprintf(json, sizeof(json),
                 "{\"success\":true,\"ip\":\"%d.%d.%d.%d\"," \
                 "\"gateway\":\"%d.%d.%d.%d\",\"subnet\":\"%d.%d.%d.%d\"}",
                 ip[0], ip[1], ip[2], ip[3],
                 gateway[0], gateway[1], gateway[2], gateway[3],
                 subnet[0], subnet[1], subnet[2], subnet[3]);
    } else {
        snprintf(json, sizeof(json), "{\"success\":false,\"message\":\"No IP data received yet\"}");
    }

    return HttpJsonUtils::send_json(req, json);
}

esp_err_t api_version_handler(httpd_req_t *req) {
    HttpHandlerTimer handler_timer(HM_VERSION);
    char json[768];

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

    snprintf(json, sizeof(json),
             "{"
             "\"device\":\"%s\","
             "\"device_type\":\"%s\","
             "\"version\":\"%s\","
             "\"version_number\":%u,"
             "\"build_date\":\"%s\","
             "\"build_time\":\"%s\","
             "\"metadata_valid\":%s,"
             "\"transmitter_version\":\"%s\","
             "\"transmitter_version_number\":%u,"
             "\"transmitter_build_date\":\"%s\","
             "\"transmitter_build_time\":\"%s\","
             "\"transmitter_compatible\":%s,"
             "\"transmitter_metadata_valid\":%s,"
             "\"uptime\":%lu,"
             "\"heap_free\":%u,"
             "\"wifi_channel\":%d"
             "}",
             receiver_device.c_str(),
             receiver_device_type.c_str(),
             receiver_version.c_str(),
             receiver_version_number,
             receiver_build_date.c_str(),
             receiver_build_time.c_str(),
             receiver_metadata_valid ? "true" : "false",
             transmitter_version.c_str(),
             transmitter_version_number,
             transmitter_build_date.c_str(),
             transmitter_build_time.c_str(),
             version_compatible ? "true" : "false",
             metadata_valid ? "true" : "false",
             millis() / 1000,
             ESP.getFreeHeap(),
             WiFi.channel());

    return HttpJsonUtils::send_json(req, json);
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
    char json[512];

    uint64_t uptime_ms = TransmitterManager::getUptimeMs();
    uint64_t unix_time = TransmitterManager::getUnixTime();
    uint8_t time_source = TransmitterManager::getTimeSource();

    snprintf(json, sizeof(json),
        "{\"success\":true,"
        "\"uptime_ms\":%llu,"
        "\"unix_time\":%llu,"
        "\"time_source\":%u,"
        "\"mqtt_connected\":%s,"
        "\"ethernet_connected\":%s}",
        uptime_ms,
        unix_time,
        time_source,
        TransmitterManager::isMqttConnected() ? "true" : "false",
        TransmitterManager::isEthernetConnected() ? "true" : "false"
    );

    return HttpJsonUtils::send_json(req, json);
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
    char json_response[160];
    if (httpCode == -1) {
        snprintf(json_response, sizeof(json_response),
                 "{\"success\":false,\"error\":\"Failed to connect to transmitter\"}");
    } else {
        snprintf(json_response, sizeof(json_response),
                 "{\"success\":false,\"error\":\"Transmitter returned HTTP %d\"}", httpCode);
    }

    return HttpJsonUtils::send_json(req, json_response);
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

    uint32_t proxy_total_requests = 0;
    uint32_t proxy_total_success = 0;
    uint32_t proxy_total_timeouts = 0;
    uint32_t proxy_total_http_errors = 0;
    int32_t proxy_last_http_code = 0;
    uint32_t proxy_last_latency_ms = 0;
    uint64_t proxy_total_latency_ms = 0;

    portENTER_CRITICAL(&g_metrics_mux);
    proxy_total_requests = g_event_log_proxy_metrics.total_requests;
    proxy_total_success = g_event_log_proxy_metrics.total_success;
    proxy_total_timeouts = g_event_log_proxy_metrics.total_timeouts;
    proxy_total_http_errors = g_event_log_proxy_metrics.total_http_errors;
    proxy_last_http_code = g_event_log_proxy_metrics.last_http_code;
    proxy_last_latency_ms = g_event_log_proxy_metrics.last_latency_ms;
    proxy_total_latency_ms = g_event_log_proxy_metrics.total_latency_ms;
    portEXIT_CRITICAL(&g_metrics_mux);

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
    event_logs_proxy["requests"] = proxy_total_requests;
    event_logs_proxy["success"] = proxy_total_success;
    event_logs_proxy["timeouts"] = proxy_total_timeouts;
    event_logs_proxy["http_errors"] = proxy_total_http_errors;
    event_logs_proxy["last_http_code"] = proxy_last_http_code;
    event_logs_proxy["last_latency_ms"] = proxy_last_latency_ms;
    event_logs_proxy["avg_success_latency_ms"] = (proxy_total_success > 0)
                                                     ? static_cast<uint32_t>(proxy_total_latency_ms / proxy_total_success)
                                                     : 0;

    JsonObject http_handlers = doc.createNestedObject("http_handlers");
    for (uint8_t i = 0; i < HM_COUNT; i++) {
        uint32_t calls = 0;
        uint32_t last_ms = 0;
        uint32_t max_ms = 0;
        uint64_t total_ms = 0;

        portENTER_CRITICAL(&g_metrics_mux);
        calls = g_http_handler_metrics[i].calls;
        last_ms = g_http_handler_metrics[i].last_ms;
        max_ms = g_http_handler_metrics[i].max_ms;
        total_ms = g_http_handler_metrics[i].total_ms;
        portEXIT_CRITICAL(&g_metrics_mux);

        JsonObject h = http_handlers.createNestedObject(k_http_handler_names[i]);
        h["calls"] = calls;
        h["last_ms"] = last_ms;
        h["max_ms"] = max_ms;
        h["avg_ms"] = (calls > 0) ? static_cast<uint32_t>(total_ms / calls) : 0;
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
