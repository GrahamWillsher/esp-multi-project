#include "api_telemetry_handlers.h"
#include "api_sse_handlers.h"
#include "api_response_utils.h"
#include "webserver_metrics.h"
#include "../webserver.h"
#include "api_field_builders.h"
#include "api_middleware.h"
#include "../../include/common_lcd.h"

#include "../utils/transmitter_event_log_cache.h"
#include "../utils/transmitter_manager.h"
#include "../utils/cell_data_cache.h"
#include <webserver_common_utils/http_json_utils.h>
#include "../utils/receiver_config_manager.h"
#include "../../receiver_config/receiver_config_manager.h"
#include "../utils/telemetry_snapshot_utils.h"
#include "../logging.h"

#include <Arduino.h>
#include <WiFi.h>
#include <firmware_version.h>
#include <firmware_metadata.h>
#include <ArduinoJson.h>
#include <esp32common/contracts/shared_contracts.h>
#include <esp32common/config/event_log_config.h>
#include <runtime_common_utils/device_temperature.h>
#include "../../src/mqtt/control_state_compat.h"
#include "../../src/mqtt/mqtt_client.h"
#include <esp_heap_caps.h>
#include "../../src/memory/memory_sampler.h"

// LCD receiver has no test-mode globals — always live data.

using namespace WebserverMetrics;

namespace {
constexpr uint32_t kSnapshotBudgetSteadyMs = 50;
constexpr uint32_t kSnapshotBudgetDegradedMs = 200;

struct SnapshotBudgetStats {
    uint32_t monitor_last_ms = 0;
    uint32_t monitor_max_ms = 0;
    uint32_t monitor_over_steady_total = 0;
    uint32_t monitor_over_degraded_total = 0;
    uint32_t cell_last_ms = 0;
    uint32_t cell_max_ms = 0;
    uint32_t cell_over_steady_total = 0;
    uint32_t cell_over_degraded_total = 0;
};

SnapshotBudgetStats g_snapshot_budget_stats{};

int parse_int_query_param(httpd_req_t* req, const char* key, int default_value) {
    if (!req || !key) {
        return default_value;
    }

    char query[256] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return default_value;
    }

    char value[24] = {0};
    if (httpd_query_key_value(query, key, value, sizeof(value)) != ESP_OK) {
        return default_value;
    }

    return atoi(value);
}

constexpr size_t kChunkTargetBytes = 768;

esp_err_t send_json_chunked(httpd_req_t* req, const String& json) {
    if (!req) {
        return ESP_ERR_INVALID_ARG;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    size_t offset = 0;
    while (offset < json.length()) {
        const size_t remaining = json.length() - offset;
        const size_t part = (remaining > kChunkTargetBytes) ? kChunkTargetBytes : remaining;
        const esp_err_t rc = httpd_resp_send_chunk(req, json.c_str() + offset, part);
        if (rc != ESP_OK) {
            return rc;
        }
        offset += part;
    }

    return httpd_resp_send_chunk(req, nullptr, 0);
}

void record_snapshot_latency(bool cell_handler, uint32_t elapsed_ms) {
    if (cell_handler) {
        g_snapshot_budget_stats.cell_last_ms = elapsed_ms;
        if (elapsed_ms > g_snapshot_budget_stats.cell_max_ms) {
            g_snapshot_budget_stats.cell_max_ms = elapsed_ms;
        }
        if (elapsed_ms > kSnapshotBudgetSteadyMs) {
            g_snapshot_budget_stats.cell_over_steady_total++;
        }
        if (elapsed_ms > kSnapshotBudgetDegradedMs) {
            g_snapshot_budget_stats.cell_over_degraded_total++;
            LOG_WARN("API", "/api/cell_data latency budget exceeded: %lu ms",
                     static_cast<unsigned long>(elapsed_ms));
        }
        return;
    }

    g_snapshot_budget_stats.monitor_last_ms = elapsed_ms;
    if (elapsed_ms > g_snapshot_budget_stats.monitor_max_ms) {
        g_snapshot_budget_stats.monitor_max_ms = elapsed_ms;
    }
    if (elapsed_ms > kSnapshotBudgetSteadyMs) {
        g_snapshot_budget_stats.monitor_over_steady_total++;
    }
    if (elapsed_ms > kSnapshotBudgetDegradedMs) {
        g_snapshot_budget_stats.monitor_over_degraded_total++;
        LOG_WARN("API", "/api/monitor latency budget exceeded: %lu ms",
                 static_cast<unsigned long>(elapsed_ms));
    }
}
} // namespace

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
    const uint32_t started_ms = millis();
    StaticJsonDocument<768> doc;
    const uint8_t test_mode = get_last_test_data_mode();
    const bool simulated = (test_mode > 0);
    const char* mode = simulated ? "simulated" : "live";
    uint8_t soc = 0;
    int32_t power = 0;
    uint32_t voltage_mv = 0;
    uint8_t contactor_state = 0;
    uint8_t error_flags = 0;
    uint8_t warning_flags = 0;
    uint32_t sys_uptime_seconds = 0;
    float charger_hv_voltage_V = 0.0f;
    float charger_hv_current_A = 0.0f;
    float charger_lv_voltage_V = 0.0f;
    float charger_lv_current_A = 0.0f;
    uint16_t charger_ac_voltage_V = 0;
    float charger_ac_current_A = 0.0f;
    uint16_t charger_power_W = 0;
    uint8_t charger_status = 0;
    uint16_t inverter_ac_voltage_V = 0;
    uint16_t inverter_ac_frequency_dHz = 0;
    int16_t inverter_ac_current_dA = 0;
    int32_t inverter_power_W = 0;
    uint8_t inverter_status = 0;

    if (simulated) {
        const uint32_t t = millis() / 1000;
        soc = static_cast<uint8_t>(55 + (t % 35));  // 55..89
        power = 900 + static_cast<int32_t>((t % 12) * 85); // 900..1835
        voltage_mv = 50000 + (soc * 20); // simple correlated demo voltage
        charger_hv_voltage_V = static_cast<float>(voltage_mv) / 1000.0f;
        charger_hv_current_A = 4.0f;
        charger_lv_voltage_V = 13.6f;
        charger_lv_current_A = 6.0f;
        charger_ac_voltage_V = 230;
        charger_ac_current_A = 3.5f;
        charger_power_W = 920;
        charger_status = 1;
        inverter_ac_voltage_V = 230;
        inverter_ac_frequency_dHz = 500;
        inverter_ac_current_dA = 45;
        inverter_power_W = power;
        inverter_status = 1;
    } else {
        TelemetrySnapshotUtils::fill_snapshot_telemetry(soc, power, voltage_mv);
        TelemetrySnapshotUtils::fill_system_status(
            contactor_state, error_flags, warning_flags, sys_uptime_seconds);
        TelemetrySnapshotUtils::fill_charger_status(
            charger_hv_voltage_V, charger_hv_current_A,
            charger_lv_voltage_V, charger_lv_current_A,
            charger_ac_voltage_V, charger_ac_current_A,
            charger_power_W, charger_status);
        TelemetrySnapshotUtils::fill_inverter_status(
            inverter_ac_voltage_V, inverter_ac_frequency_dHz,
            inverter_ac_current_dA, inverter_power_W, inverter_status);
    }

    doc["mode"] = mode;
    doc["soc"] = soc;
    doc["power"] = power;
    doc["voltage_mv"] = voltage_mv;
    doc["voltage_v"] = static_cast<float>(voltage_mv) / 1000.0f;
    doc["contactor_state"] = contactor_state;
    doc["error_flags"] = error_flags;
    doc["warning_flags"] = warning_flags;
    doc["tx_uptime_s"] = sys_uptime_seconds;

    JsonObject charger = doc.createNestedObject("charger");
    charger["hv_voltage_v"] = charger_hv_voltage_V;
    charger["hv_current_a"] = charger_hv_current_A;
    charger["lv_voltage_v"] = charger_lv_voltage_V;
    charger["lv_current_a"] = charger_lv_current_A;
    charger["ac_voltage_v"] = charger_ac_voltage_V;
    charger["ac_current_a"] = charger_ac_current_A;
    charger["power_w"] = charger_power_W;
    charger["status"] = charger_status;

    JsonObject inverter = doc.createNestedObject("inverter");
    inverter["ac_voltage_v"] = inverter_ac_voltage_V;
    inverter["ac_frequency_hz"] = static_cast<float>(inverter_ac_frequency_dHz) / 10.0f;
    inverter["ac_current_a"] = static_cast<float>(inverter_ac_current_dA) / 10.0f;
    inverter["power_w"] = inverter_power_W;
    inverter["status"] = inverter_status;

    String json;
    json.reserve(384);
    serializeJson(doc, json);
    const esp_err_t rc = HttpJsonUtils::send_json(req, json.c_str());
    record_snapshot_latency(false, millis() - started_ms);
    return rc;
}

esp_err_t api_cell_data_handler(httpd_req_t *req) {
    HttpHandlerTimer handler_timer(HM_CELL_DATA);
    const uint32_t started_ms = millis();
    esp_err_t rc = ESP_FAIL;
    CellDataCache::CellDataSnapshot snapshot;
    if (CellDataCache::get_cell_data_snapshot(snapshot) && snapshot.known) {
        String json = TelemetrySnapshotUtils::serialize_cell_data(snapshot);
        rc = send_json_chunked(req, json);
    } else {
        const char* json = "{\"success\":false,\"mode\":\"unavailable\",\"message\":\"No cell data received from transmitter\"}";
        rc = HttpJsonUtils::send_json(req, json);
    }

    record_snapshot_latency(true, millis() - started_ms);
    return rc;
}

esp_err_t api_cell_data_page_handler(httpd_req_t *req) {
    HttpHandlerTimer handler_timer(HM_CELL_DATA);

    int offset = parse_int_query_param(req, "offset", 0);
    int limit = parse_int_query_param(req, "limit", 24);
    if (offset < 0) offset = 0;
    if (limit < 1) limit = 1;
    if (limit > 32) limit = 32;

    CellDataCache::CellDataSnapshot snapshot;
    if (!CellDataCache::get_cell_data_snapshot(snapshot) || !snapshot.known) {
        return HttpJsonUtils::send_json(req,
                                        "{\"success\":false,\"mode\":\"unavailable\",\"message\":\"No cell data received from transmitter\"}");
    }

    const int total_cells = static_cast<int>(snapshot.cell_count);
    if (offset > total_cells) {
        offset = total_cells;
    }

    const int end_index = std::min(total_cells, offset + limit);

    DynamicJsonDocument doc(3072);
    doc["success"] = true;
    doc["source"] = "mqtt";
    doc["offset"] = offset;
    doc["limit"] = limit;
    doc["returned"] = end_index - offset;
    doc["total"] = total_cells;
    doc["number_of_cells"] = snapshot.cell_count;
    doc["min_voltage_mV"] = snapshot.min_voltage_mV;
    doc["max_voltage_mV"] = snapshot.max_voltage_mV;
    doc["balancing_active"] = snapshot.balancing_active;
    doc["data_source"] = snapshot.data_source;

    JsonArray cells = doc.createNestedArray("cells");
    for (int index = offset; index < end_index; ++index) {
        JsonObject cell = cells.createNestedObject();
        cell["index"] = index;
        cell["voltage_mV"] = snapshot.voltages_mV[static_cast<size_t>(index)];
        const bool balancing = snapshot.balancing_status[static_cast<size_t>(index)] ? true : false;
        cell["balancing"] = balancing;
    }

    String json;
    serializeJson(doc, json);
    return HttpJsonUtils::send_json(req, json.c_str());
}

esp_err_t api_dashboard_data_handler(httpd_req_t *req) {
    HttpHandlerTimer handler_timer(HM_DASHBOARD_DATA);
    // Size increased to accommodate merged transmitter_health fields (uptime_ms,
    // unix_time, utc_offset_min, time_source, geolocation_valid, mqtt_connected).
    // /api/transmitter_health is no longer polled separately by the dashboard.
    StaticJsonDocument<768> doc;

    const DeviceTemperature::Reading receiver_temperature = DeviceTemperature::get_latest();
    const auto transmitter_temperature = TransmitterManager::getTemperatureReport();
    const auto battery_temperature = TransmitterManager::getBatteryTemperatureReport();

    JsonObject transmitter = ApiFieldBuilders::addTransmitterObject(doc);
    transmitter["connected"] = TransmitterManager::isTransmitterConnected();
    transmitter["ethernet_connected"] = TransmitterManager::isEthernetConnected();
    transmitter["ip"] = TransmitterManager::getIPString();
    transmitter["is_static"] = TransmitterManager::isStaticIP();
    transmitter["mac"] = TransmitterManager::getMACString();

    // Health fields merged from /api/transmitter_health to eliminate the second
    // periodic request per dashboard poll cycle (2 requests/cycle -> 1 request/cycle).
    transmitter["uptime_ms"]         = TransmitterManager::getUptimeMs();
    transmitter["unix_time"]         = TransmitterManager::getUnixTime();
    transmitter["utc_offset_min"]    = TransmitterManager::getUtcOffsetMin();
    transmitter["time_source"]       = TransmitterManager::getTimeSource();
    transmitter["geolocation_valid"] = TransmitterManager::isGeolocationValid();
    transmitter["mqtt_connected"]    = TransmitterManager::isMqttConnected();

    String tx_firmware = "Unknown";
    String tx_name = "Unknown";
    if (TransmitterManager::hasMetadata()) {
        uint8_t major, minor, patch;
        TransmitterManager::getMetadataVersion(major, minor, patch);
        char version_str[12];
        ApiFieldBuilders::formatVersionString(version_str, sizeof(version_str), major, minor, patch);
        tx_firmware = String(version_str);
        const char* env = TransmitterManager::getMetadataEnv();
        const char* device = TransmitterManager::getMetadataDevice();
        if (env && env[0] != '\0') {
            tx_name = String(env);
        } else if (device && device[0] != '\0') {
            tx_name = String(device);
        }
    }
    transmitter["name"] = tx_name;
    transmitter["firmware"] = tx_firmware;
    if (transmitter_temperature.known && transmitter_temperature.valid) {
        transmitter["temperature_c"] = DeviceTemperature::to_celsius(transmitter_temperature.temperature_centi_c);
    } else {
        transmitter["temperature_c"] = nullptr;
    }
    if (battery_temperature.known && battery_temperature.valid) {
        transmitter["battery_temp_c"] = DeviceTemperature::to_celsius(battery_temperature.temperature_centi_c);
    } else {
        transmitter["battery_temp_c"] = nullptr;
    }

    JsonObject receiver = ApiFieldBuilders::addReceiverObject(doc);
    receiver["is_static"] = ReceiverNetworkConfig::useStaticIP();
    if (receiver_temperature.valid) {
        receiver["temperature_c"] = DeviceTemperature::to_celsius(receiver_temperature.centi_celsius);
    } else {
        receiver["temperature_c"] = nullptr;
    }

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
    doc["metadata_valid"] = receiver_metadata_valid;
    doc["transmitter_version"] = transmitter_version;
    doc["transmitter_version_number"] = transmitter_version_number;
    doc["transmitter_build_date"] = transmitter_build_date;
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

    // Expose WiFi mode so the OTA page can gate transmitter OTA in AP/config mode.
    const wifi_mode_t wifi_mode = WiFi.getMode();
    doc["sta_connected"] = (wifi_mode == WIFI_MODE_STA) && (WiFi.status() == WL_CONNECTED);
    const char* wifi_mode_str =
        (wifi_mode == WIFI_MODE_STA)   ? "STA"   :
        (wifi_mode == WIFI_MODE_AP)    ? "AP"    :
        (wifi_mode == WIFI_MODE_APSTA) ? "APSTA" : "NULL";
    doc["wifi_mode"] = wifi_mode_str;

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
    StaticJsonDocument<320> doc;

    doc["success"]            = true;
    doc["uptime_ms"]          = TransmitterManager::getUptimeMs();
    doc["unix_time"]          = TransmitterManager::getUnixTime();
    doc["utc_offset_min"]     = TransmitterManager::getUtcOffsetMin();
    doc["time_source"]        = TransmitterManager::getTimeSource();
    doc["geolocation_valid"]  = TransmitterManager::isGeolocationValid();
    doc["mqtt_connected"]     = TransmitterManager::isMqttConnected();
    doc["ethernet_connected"] = TransmitterManager::isEthernetConnected();

    const auto transmitter_temperature = TransmitterManager::getTemperatureReport();
    if (transmitter_temperature.known && transmitter_temperature.valid) {
        doc["temperature_c"] = DeviceTemperature::to_celsius(transmitter_temperature.temperature_centi_c);
    } else {
        doc["temperature_c"] = nullptr;
    }

    const auto battery_temperature = TransmitterManager::getBatteryTemperatureReport();
    if (battery_temperature.known && battery_temperature.valid) {
        doc["battery_temp_c"] = DeviceTemperature::to_celsius(battery_temperature.temperature_centi_c);
    } else {
        doc["battery_temp_c"] = nullptr;
    }

    String json;
    json.reserve(192);
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
    int limit = parse_int_query_param(req, "limit", 50);
    if (limit < 1) limit = 1;
    if (limit > 50) limit = 50;

    std::vector<TransmitterManager::EventLogEntry> logs;
    uint32_t last_update_ms = 0;
    TransmitterManager::getEventLogsSnapshot(logs, &last_update_ms);

    const int max_events = (limit < (int)logs.size()) ? limit : (int)logs.size();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    // Send header
    char header[256];
    const int header_len = snprintf(header, sizeof(header),
                                    "{\"success\":true,\"event_count\":%d,"
                                    "\"source\":\"mqtt\",\"last_update_ms\":%u,\"events\":[",
                                    max_events, last_update_ms);
    if (header_len <= 0 || httpd_resp_send_chunk(req, header, header_len) != ESP_OK) {
        return ESP_FAIL;
    }

    // Send per-event chunks
    for (int i = 0; i < max_events; i++) {
        if (i > 0) {
            if (httpd_resp_send_chunk(req, ",", 1) != ESP_OK) {
                return ESP_FAIL;
            }
        }

        const auto& entry = logs[i];
        StaticJsonDocument<512> evt_doc;
        evt_doc["timestamp_ms"] = entry.timestamp_ms;
        evt_doc["event_unix_ms"] = entry.event_unix_ms;
        evt_doc["event_utc_offset_min"] = entry.event_utc_offset_min;
        evt_doc["level"] = entry.level;
        evt_doc["data"] = entry.data;
        evt_doc["count"] = entry.count;
        evt_doc["is_new"] = entry.is_new;
        evt_doc["type"] = entry.type;
        evt_doc["message"] = entry.message;

        char evt_json[640];
        const size_t evt_len = serializeJson(evt_doc, evt_json, sizeof(evt_json));
        if (evt_len == 0 || httpd_resp_send_chunk(req, evt_json, evt_len) != ESP_OK) {
            return ESP_FAIL;
        }
    }

    // Send footer and finalize
    if (httpd_resp_send_chunk(req, "]}", 2) != ESP_OK) { return ESP_FAIL; }
    if (httpd_resp_send_chunk(req, nullptr, 0) != ESP_OK) { return ESP_FAIL; }
    return ESP_OK;
}

esp_err_t api_event_logs_page_handler(httpd_req_t *req) {
    HttpHandlerTimer handler_timer(HM_GET_EVENT_LOGS);

    int offset = parse_int_query_param(req, "offset", 0);
    int limit = parse_int_query_param(req, "limit", 25);
    if (offset < 0) offset = 0;
    if (limit < 1) limit = 1;
    if (limit > 25) limit = 25;

    std::vector<TransmitterManager::EventLogEntry> logs;
    uint32_t last_update_ms = 0;
    TransmitterManager::getEventLogsSnapshot(logs, &last_update_ms);

    const int total = static_cast<int>(logs.size());
    if (offset > total) {
        offset = total;
    }
    const int end_index = std::min(total, offset + limit);
    const int returned = end_index - offset;

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    // Send header with pagination metadata
    char header[320];
    const int header_len = snprintf(header, sizeof(header),
                                    "{\"success\":true,\"source\":\"mqtt\","
                                    "\"offset\":%d,\"limit\":%d,\"returned\":%d,"
                                    "\"total\":%d,\"last_update_ms\":%u,\"events\":[",
                                    offset, limit, returned, total, last_update_ms);
    if (header_len <= 0 || httpd_resp_send_chunk(req, header, header_len) != ESP_OK) {
        return ESP_FAIL;
    }

    // Send per-event chunks
    for (int index = offset; index < end_index; index++) {
        if (index > offset) {
            if (httpd_resp_send_chunk(req, ",", 1) != ESP_OK) {
                return ESP_FAIL;
            }
        }

        const auto& entry = logs[static_cast<size_t>(index)];
        StaticJsonDocument<512> evt_doc;
        evt_doc["timestamp_ms"] = entry.timestamp_ms;
        evt_doc["event_unix_ms"] = entry.event_unix_ms;
        evt_doc["event_utc_offset_min"] = entry.event_utc_offset_min;
        evt_doc["level"] = entry.level;
        evt_doc["data"] = entry.data;
        evt_doc["count"] = entry.count;
        evt_doc["is_new"] = entry.is_new;
        evt_doc["type"] = entry.type;
        evt_doc["message"] = entry.message;

        char evt_json[640];
        const size_t evt_len = serializeJson(evt_doc, evt_json, sizeof(evt_json));
        if (evt_len == 0 || httpd_resp_send_chunk(req, evt_json, evt_len) != ESP_OK) {
            return ESP_FAIL;
        }
    }

    // Send footer and finalize
    if (httpd_resp_send_chunk(req, "]}", 2) != ESP_OK) { return ESP_FAIL; }
    if (httpd_resp_send_chunk(req, nullptr, 0) != ESP_OK) { return ESP_FAIL; }
    return ESP_OK;
}

esp_err_t api_get_event_log_summary_handler(httpd_req_t *req) {
    // Transmitter-driven model: return cached summary pushed over MQTT.
    // Do not request on every HTTP poll from dashboard.

    const auto summary = TransmitterManager::getEventLogSummary();

    DynamicJsonDocument doc(256);
    doc["success"] = summary.known;
    doc["source"] = "mqtt";
    doc["seq"] = summary.seq;
    doc["total_historical"] = summary.total_historical;
    doc["error_historical"] = summary.error_historical;
    doc["new_since_last_report_total"] = summary.new_since_last_report_total;
    doc["new_since_last_report_error"] = summary.new_since_last_report_error;
    doc["uptime_ms"] = summary.uptime_ms;
    doc["last_update_ms"] = summary.last_update_ms;

    if (!summary.known) {
        doc["message"] = "Waiting for summary";
    }

    String json;
    serializeJson(doc, json);
    return HttpJsonUtils::send_json(req, json.c_str());
}

esp_err_t api_clear_event_logs_handler(httpd_req_t *req) {
    HttpHandlerTimer handler_timer(HM_GET_EVENT_LOGS);

    const auto ack_before = TransmitterManager::getEventLogClearAck();
    const auto summary_before = TransmitterManager::getEventLogSummary();

    // Event log transport is ESP-NOW + MQTT only: send clear command to transmitter.
    const bool tx_send_ok = send_event_logs_clear_request();

    // Keep receiver cache consistent with clear command.
    TransmitterManager::clearEventLogs();

    bool transmitter_clear_confirmed = false;
    if (tx_send_ok) {
        const uint32_t start_ms = millis();
        while ((millis() - start_ms) < config::event_logs::CLEAR_CONFIRM_TIMEOUT_MS) {
            const auto ack_now = TransmitterManager::getEventLogClearAck();
            if (ack_now.known &&
                ack_now.last_update_ms > ack_before.last_update_ms &&
                ack_now.status == TransmitterManager::kEventLogsClearAckSuccess) {
                transmitter_clear_confirmed = true;
                break;
            }

            const auto summary_now = TransmitterManager::getEventLogSummary();
            if (summary_now.known &&
                summary_now.last_update_ms > summary_before.last_update_ms &&
                summary_now.total_historical == 0) {
                transmitter_clear_confirmed = true;
                break;
            }

            delay(50);
        }
    }

    StaticJsonDocument<320> doc;
    doc["success"] = tx_send_ok;
    doc["receiver_cache_cleared"] = true;
    doc["transmitter_clear_requested"] = tx_send_ok;
    doc["transmitter_clear_confirmed"] = transmitter_clear_confirmed;
    doc["message"] = tx_send_ok
        ? (transmitter_clear_confirmed
            ? "Event logs cleared on receiver and confirmed by transmitter"
            : "Receiver logs cleared; transmitter clear requested (confirmation pending)")
        : "Receiver logs cleared; failed to send transmitter clear request";

    return ApiResponseUtils::send_json_doc(req, doc);
}

esp_err_t api_system_metrics_handler(httpd_req_t *req) {
    HttpHandlerTimer handler_timer(HM_SYSTEM_METRICS);
    DynamicJsonDocument doc(2560);

    const uint32_t callback_count = RuntimeState::rx_callback_count.load();
    const uint32_t drop_count = RuntimeState::rx_queue_drop_count.load();
    const uint32_t high_watermark = RuntimeState::rx_queue_high_watermark.load();
    const uint32_t queue_depth = 0;
    const uint32_t queue_size = 0;

    doc["success"] = true;
    doc["uptime_s"] = millis() / 1000;

    JsonObject heap = doc.createNestedObject("heap");
    heap["free"] = ESP.getFreeHeap();
    heap["min_free"] = ESP.getMinFreeHeap();
    heap["max_alloc"] = ESP.getMaxAllocHeap();

    const uint32_t int_free    = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const uint32_t int_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    JsonObject heap_internal = doc.createNestedObject("heap_internal");
    heap_internal["free"] = int_free;
    heap_internal["largest_block"] = int_largest;
    heap_internal["frag_est"] = (int_free > 0)
                                    ? (1.0f - static_cast<float>(int_largest) / static_cast<float>(int_free))
                                    : 0.0f;

    const uint32_t ps_free    = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const uint32_t ps_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    JsonObject heap_psram = doc.createNestedObject("heap_psram");
    heap_psram["free"] = ps_free;
    heap_psram["largest_block"] = ps_largest;
    heap_psram["frag_est"] = (ps_free > 0)
                                  ? (1.0f - static_cast<float>(ps_largest) / static_cast<float>(ps_free))
                                  : 0.0f;

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
    webserver["request_total"] = webserver_metrics.request_total;
    webserver["request_failures"] = webserver_metrics.request_failures;
    webserver["last_request_complete_ms"] = webserver_metrics.last_request_complete_ms;
    webserver["last_request_duration_ms"] = webserver_metrics.last_request_duration_ms;
    webserver["max_request_duration_ms"] = webserver_metrics.max_request_duration_ms;
    webserver["active_requests"] = webserver_metrics.active_requests;
    webserver["recycle_count"] = webserver_metrics.recycle_count;

    JsonObject transmitter = doc.createNestedObject("transmitter");
    transmitter["connected"] = TransmitterManager::isTransmitterConnected();
    transmitter["ip_known"] = TransmitterManager::isIPKnown();
    transmitter["metadata"] = TransmitterManager::hasMetadata();

    const ApiMiddleware::PressureGateStats pressure_stats = ApiMiddleware::get_pressure_gate_stats();
    JsonObject http_pressure = doc.createNestedObject("http_pressure_gate");
    http_pressure["read_throttled_total"] = pressure_stats.read_throttled_total;
    http_pressure["read_throttled_constrained"] = pressure_stats.read_throttled_constrained;
    http_pressure["read_throttled_critical"] = pressure_stats.read_throttled_critical;
    http_pressure["mutation_blocked_critical"] = pressure_stats.mutation_blocked_critical;

    String json;
    json.reserve(1024);
    serializeJson(doc, json);
    return HttpJsonUtils::send_json(req, json.c_str());
}

esp_err_t api_http_pressure_stats_handler(httpd_req_t *req) {
    HttpHandlerTimer handler_timer(HM_SYSTEM_METRICS);

    DynamicJsonDocument doc(384);
    const ApiMiddleware::PressureGateStats stats = ApiMiddleware::get_pressure_gate_stats();

    doc["success"] = true;
    doc["uptime_s"] = millis() / 1000;
    doc["read_throttled_total"] = stats.read_throttled_total;
    doc["read_throttled_constrained"] = stats.read_throttled_constrained;
    doc["read_throttled_critical"] = stats.read_throttled_critical;
    doc["mutation_blocked_critical"] = stats.mutation_blocked_critical;

    JsonObject budget = doc.createNestedObject("snapshot_latency_budget_ms");
    budget["steady"] = kSnapshotBudgetSteadyMs;
    budget["degraded"] = kSnapshotBudgetDegradedMs;

    JsonObject monitor = doc.createNestedObject("monitor");
    monitor["last_ms"] = g_snapshot_budget_stats.monitor_last_ms;
    monitor["max_ms"] = g_snapshot_budget_stats.monitor_max_ms;
    monitor["over_steady_total"] = g_snapshot_budget_stats.monitor_over_steady_total;
    monitor["over_degraded_total"] = g_snapshot_budget_stats.monitor_over_degraded_total;

    JsonObject cell = doc.createNestedObject("cell_data");
    cell["last_ms"] = g_snapshot_budget_stats.cell_last_ms;
    cell["max_ms"] = g_snapshot_budget_stats.cell_max_ms;
    cell["over_steady_total"] = g_snapshot_budget_stats.cell_over_steady_total;
    cell["over_degraded_total"] = g_snapshot_budget_stats.cell_over_degraded_total;

    String json;
    json.reserve(256);
    serializeJson(doc, json);
    return HttpJsonUtils::send_json(req, json.c_str());
}

esp_err_t api_memory_samples_handler(httpd_req_t *req) {
    HttpHandlerTimer handler_timer(HM_SYSTEM_METRICS);

    MemorySampler::memory_sample_t samples[MemorySampler::SAMPLE_RING_SIZE];
    const size_t count = MemorySampler::copy_samples(samples, MemorySampler::SAMPLE_RING_SIZE);
    const uint32_t now_ms = millis();

    // Each sample ~120 bytes of JSON; 20 samples + envelope fits in 2560 bytes.
    DynamicJsonDocument doc(2560);
    doc["success"] = true;
    doc["count"] = count;
    doc["now_ms"] = now_ms;

    JsonArray arr = doc.createNestedArray("samples");
    for (size_t i = 0; i < count; i++) {
        const MemorySampler::memory_sample_t& s = samples[i];
        JsonObject obj = arr.createNestedObject();
        obj["ts_ms"] = s.timestamp_ms;
        obj["int_free"] = s.internal_free;
        obj["int_largest"] = s.internal_largest;
        obj["int_frag_est"] = s.internal_frag_est;
        obj["ps_free"] = s.psram_free;
        obj["ps_largest"] = s.psram_largest;
        obj["ps_frag_est"] = s.psram_frag_est;
    }

    String json;
    json.reserve(512);
    serializeJson(doc, json);
    return HttpJsonUtils::send_json(req, json.c_str());
}
