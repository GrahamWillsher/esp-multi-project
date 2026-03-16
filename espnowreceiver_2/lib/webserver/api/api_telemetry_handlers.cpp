#include "api_telemetry_handlers.h"

#include "../utils/transmitter_manager.h"
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

extern bool& test_mode_enabled;
extern volatile int& g_test_soc;
extern volatile int32_t& g_test_power;
extern volatile uint32_t& g_test_voltage_mv;

namespace ESPNow {
extern uint8_t received_soc;
extern int32_t received_power;
extern uint32_t received_voltage_mv;
}

esp_err_t api_data_handler(httpd_req_t *req) {
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

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

esp_err_t api_get_receiver_info_handler(httpd_req_t *req) {
    String json = ReceiverConfigManager::getReceiverInfoJson();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json.c_str(), json.length());
    return ESP_OK;
}

esp_err_t api_monitor_handler(httpd_req_t *req) {
    char json[512];
    const char* mode = test_mode_enabled ? "simulated" : "live";
    uint8_t soc = test_mode_enabled ? g_test_soc : ESPNow::received_soc;
    int32_t power = test_mode_enabled ? g_test_power : ESPNow::received_power;
    uint32_t voltage_mv = test_mode_enabled ? g_test_voltage_mv : ESPNow::received_voltage_mv;

    snprintf(json, sizeof(json),
             "{\"mode\":\"%s\",\"soc\":%d,\"power\":%ld,\"voltage_mv\":%u,\"voltage_v\":%.1f}",
             mode, soc, power, voltage_mv, voltage_mv / 1000.0f);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

esp_err_t api_get_data_source_handler(httpd_req_t *req) {
    const char* mode = "live";
    char json[96];
    snprintf(json, sizeof(json), "{\"mode\":\"%s\"}", mode);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

esp_err_t api_cell_data_handler(httpd_req_t *req) {
    if (TransmitterManager::hasCellData()) {
        uint16_t cell_count = TransmitterManager::getCellCount();
        const uint16_t* voltages = TransmitterManager::getCellVoltages();
        const bool* balancing = TransmitterManager::getCellBalancingStatus();
        uint16_t min_voltage = TransmitterManager::getCellMinVoltage();
        uint16_t max_voltage = TransmitterManager::getCellMaxVoltage();
        bool balancing_active = TransmitterManager::isBalancingActive();

        String json = "{\"success\":true,\"mode\":\"live\",\"cells\":[";
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

        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json.c_str(), json.length());
        return ESP_OK;
    }

    const char* json = "{\"success\":false,\"mode\":\"unavailable\",\"message\":\"No cell data received from transmitter\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

esp_err_t api_dashboard_data_handler(httpd_req_t *req) {
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

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

esp_err_t api_transmitter_ip_handler(httpd_req_t *req) {
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

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

esp_err_t api_version_handler(httpd_req_t *req) {
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

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

esp_err_t api_firmware_info_handler(httpd_req_t *req) {
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
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json.c_str(), json.length());
    return ESP_OK;
}

esp_err_t api_transmitter_metadata_handler(httpd_req_t *req) {
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
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json.c_str(), json.length());
    return ESP_OK;
}

esp_err_t api_transmitter_health_handler(httpd_req_t *req) {
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

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

esp_err_t api_static_specs_handler(httpd_req_t *req) {
    if (!TransmitterManager::hasStaticSpecs()) {
        const char* json = "{\"success\":false,\"error\":\"Static specs not available\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }

    String specs_json = TransmitterManager::getStaticSpecsJson();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, specs_json.c_str(), specs_json.length());
    return ESP_OK;
}

esp_err_t api_battery_specs_handler(httpd_req_t *req) {
    String specs_json = TransmitterManager::getBatterySpecsJson();
    if (specs_json.length() == 0) {
        const char* json = "{\"success\":false,\"error\":\"Battery specs not available\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, specs_json.c_str(), specs_json.length());
    return ESP_OK;
}

esp_err_t api_inverter_specs_handler(httpd_req_t *req) {
    String specs_json = TransmitterManager::getInverterSpecsJson();
    if (specs_json.length() == 0) {
        const char* json = "{\"success\":false,\"error\":\"Inverter specs not available\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, specs_json.c_str(), specs_json.length());
    return ESP_OK;
}

esp_err_t api_get_event_logs_handler(httpd_req_t *req) {
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
        const auto& logs = TransmitterManager::getEventLogs();
        DynamicJsonDocument doc(4096);
        doc["success"] = true;
        doc["event_count"] = TransmitterManager::getEventLogCount();
        doc["source"] = "mqtt";
        doc["last_update_ms"] = TransmitterManager::getEventLogsLastUpdateMs();
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
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json.c_str(), json.length());
        return ESP_OK;
    }

    if (!TransmitterManager::isIPKnown()) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"success\":false,\"error\":\"Transmitter not connected\"}");
        return ESP_OK;
    }

    String transmitter_url = TransmitterManager::getURL() + "/api/get_event_logs?limit=" + String(limit);
    HTTPClient http;
    http.begin(transmitter_url);
    http.setTimeout(5000);
    int httpCode = http.GET();

    if (httpCode == 200) {
        String response = http.getString();
        http.end();
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, response.c_str(), response.length());
        return ESP_OK;
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

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_response, strlen(json_response));
    return ESP_OK;
}
