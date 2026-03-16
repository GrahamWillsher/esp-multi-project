#include "api_type_selection_handlers.h"

#include <esp32common/logging/logging_config.h>
#include "../../receiver_config/receiver_config_manager.h"
#include "../../src/espnow/espnow_send.h"
#include "../../src/espnow/type_catalog_cache.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <cstring>

namespace {

struct TypeEntry {
    uint8_t id;
    const char* name;

    bool operator<(const TypeEntry& other) const {
        return strcasecmp(name, other.name) < 0;
    }
};

static TypeEntry battery_interfaces[] = {
    {0, "Modbus"},
    {1, "RS485"},
    {2, "CAN (Native)"},
    {3, "CAN-FD (Native)"},
    {4, "CAN (MCP2515 add-on)"},
    {5, "CAN-FD (MCP2518 add-on)"}
};

static String generate_sorted_type_json(TypeEntry* types, size_t count) {
    TypeEntry* sorted_copy = new TypeEntry[count];
    if (!sorted_copy) {
        return "{\"types\":[]}";
    }

    memcpy(sorted_copy, types, count * sizeof(TypeEntry));
    std::sort(sorted_copy, sorted_copy + count);

    DynamicJsonDocument doc(64 + (count * 32));
    JsonArray json_types = doc.createNestedArray("types");
    for (size_t i = 0; i < count; i++) {
        JsonObject entry = json_types.createNestedObject();
        entry["id"] = sorted_copy[i].id;
        entry["name"] = sorted_copy[i].name;
    }

    String json;
    json.reserve(32 + (count * 32));
    serializeJson(doc, json);

    delete[] sorted_copy;
    return json;
}

static esp_err_t api_get_battery_types_handler(httpd_req_t *req) {
    // Heap-allocate: 128 * 49 bytes = 6 KB, too large for the httpd task stack
    auto* cache_entries = new TypeCatalogCache::TypeEntry[128];
    if (!cache_entries) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }
    size_t count = TypeCatalogCache::copy_battery_entries(cache_entries, 128);

    if (count == 0) {
        delete[] cache_entries;
        send_battery_types_request();
        const char* loading = "{\"types\":[],\"loading\":true}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, loading, strlen(loading));
        return ESP_OK;
    }

    // response_entries.name pointers point into cache_entries — keep cache_entries alive
    // until after generate_sorted_type_json has built the String
    auto* response_entries = new TypeEntry[count];
    if (!response_entries) {
        delete[] cache_entries;
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }
    for (size_t i = 0; i < count; ++i) {
        response_entries[i].id = cache_entries[i].id;
        response_entries[i].name = cache_entries[i].name;
    }

    String json_response = generate_sorted_type_json(response_entries, count);
    delete[] response_entries;
    delete[] cache_entries;  // safe: JSON string has already copied the name data
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_response.c_str(), json_response.length());
    return ESP_OK;
}

static esp_err_t api_get_inverter_types_handler(httpd_req_t *req) {
    // Heap-allocate: 128 * 49 bytes = 6 KB, too large for the httpd task stack
    auto* cache_entries = new TypeCatalogCache::TypeEntry[128];
    if (!cache_entries) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }
    size_t count = TypeCatalogCache::copy_inverter_entries(cache_entries, 128);

    if (count == 0) {
        delete[] cache_entries;
        send_inverter_types_request();
        const char* loading = "{\"types\":[],\"loading\":true}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, loading, strlen(loading));
        return ESP_OK;
    }

    // response_entries.name pointers point into cache_entries — keep cache_entries alive
    // until after generate_sorted_type_json has built the String
    auto* response_entries = new TypeEntry[count];
    if (!response_entries) {
        delete[] cache_entries;
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }
    for (size_t i = 0; i < count; ++i) {
        response_entries[i].id = cache_entries[i].id;
        response_entries[i].name = cache_entries[i].name;
    }

    String json_response = generate_sorted_type_json(response_entries, count);
    delete[] response_entries;
    delete[] cache_entries;  // safe: JSON string has already copied the name data
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_response.c_str(), json_response.length());
    return ESP_OK;
}

static esp_err_t api_get_selected_types_handler(httpd_req_t *req) {
    char json[128];
    uint8_t battery_type = ReceiverNetworkConfig::getBatteryType();
    uint8_t inverter_type = ReceiverNetworkConfig::getInverterType();

    snprintf(json, sizeof(json),
             "{\"battery_type\":%d,\"inverter_type\":%d}",
             battery_type, inverter_type);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

static esp_err_t api_set_battery_type_handler(httpd_req_t *req) {
    char content[100] = {0};
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);

    if (ret <= 0) {
        const char* json = "{\"success\":false,\"error\":\"No data received\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }

    StaticJsonDocument<128> doc;
    DeserializationError error = deserializeJson(doc, content);
    if (error || !doc.containsKey("type")) {
        const char* json = "{\"success\":false,\"error\":\"Invalid JSON format\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }

    int type = doc["type"];
    if (type < 0 || type > 255) {
        const char* json = "{\"success\":false,\"error\":\"Type must be 0-255\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }

    ReceiverNetworkConfig::setBatteryType((uint8_t)type);

    if (send_component_type_selection((uint8_t)type, ReceiverNetworkConfig::getInverterType())) {
        LOG_INFO("API", "Battery type %d sent to transmitter via ESP-NOW", type);
    } else {
        LOG_WARN("API", "Could not send battery type to transmitter (may be offline)");
    }

    const char* json = "{\"success\":true,\"message\":\"Battery type updated\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

static esp_err_t api_set_inverter_type_handler(httpd_req_t *req) {
    char content[100] = {0};
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);

    if (ret <= 0) {
        const char* json = "{\"success\":false,\"error\":\"No data received\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }

    StaticJsonDocument<128> doc;
    DeserializationError error = deserializeJson(doc, content);
    if (error || !doc.containsKey("type")) {
        const char* json = "{\"success\":false,\"error\":\"Invalid JSON format\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }

    int type = doc["type"];
    if (type < 0 || type > 255) {
        const char* json = "{\"success\":false,\"error\":\"Type must be 0-255\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }

    ReceiverNetworkConfig::setInverterType((uint8_t)type);

    if (send_component_type_selection(ReceiverNetworkConfig::getBatteryType(), (uint8_t)type)) {
        LOG_INFO("API", "Inverter type %d sent to transmitter via ESP-NOW", type);
    } else {
        LOG_WARN("API", "Could not send inverter type to transmitter (may be offline)");
    }

    const char* json = "{\"success\":true,\"message\":\"Inverter type updated\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

static esp_err_t api_get_battery_interfaces_handler(httpd_req_t *req) {
    String json_response = generate_sorted_type_json(battery_interfaces, sizeof(battery_interfaces) / sizeof(TypeEntry));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_response.c_str(), json_response.length());
    return ESP_OK;
}

static esp_err_t api_get_inverter_interfaces_handler(httpd_req_t *req) {
    auto* cache_entries = new TypeCatalogCache::TypeEntry[128];
    if (!cache_entries) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    size_t count = TypeCatalogCache::copy_inverter_interface_entries(cache_entries, 128);

    if (count == 0) {
        delete[] cache_entries;
        send_inverter_interfaces_request();
        const char* loading = "{\"types\":[],\"loading\":true}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, loading, strlen(loading));
        return ESP_OK;
    }

    auto* response_entries = new TypeEntry[count];
    if (!response_entries) {
        delete[] cache_entries;
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    for (size_t i = 0; i < count; ++i) {
        response_entries[i].id = cache_entries[i].id;
        response_entries[i].name = cache_entries[i].name;
    }

    String json_response = generate_sorted_type_json(response_entries, count);
    delete[] response_entries;
    delete[] cache_entries;

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_response.c_str(), json_response.length());
    return ESP_OK;
}

static esp_err_t api_get_selected_interfaces_handler(httpd_req_t *req) {
    char json[128];
    uint8_t battery_interface = ReceiverNetworkConfig::getBatteryInterface();
    uint8_t inverter_interface = ReceiverNetworkConfig::getInverterInterface();

    snprintf(json, sizeof(json),
             "{\"battery_interface\":%d,\"inverter_interface\":%d}",
             battery_interface, inverter_interface);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

static esp_err_t api_set_battery_interface_handler(httpd_req_t *req) {
    char content[100] = {0};
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);

    if (ret <= 0) {
        const char* json = "{\"success\":false,\"error\":\"No data received\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }

    StaticJsonDocument<128> doc;
    DeserializationError error = deserializeJson(doc, content);
    if (error || !doc.containsKey("interface")) {
        const char* json = "{\"success\":false,\"error\":\"Invalid JSON format\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }

    int interface = doc["interface"];
    if (interface < 0 || interface > 5) {
        const char* json = "{\"success\":false,\"error\":\"Interface must be 0-5\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }

    ReceiverNetworkConfig::setBatteryInterface((uint8_t)interface);

    if (send_component_interface_selection(ReceiverNetworkConfig::getBatteryInterface(), ReceiverNetworkConfig::getInverterInterface())) {
        LOG_INFO("API", "Battery interface %d sent to transmitter via ESP-NOW", interface);
    } else {
        LOG_WARN("API", "Could not send battery interface to transmitter (may be offline)");
    }

    const char* json = "{\"success\":true,\"message\":\"Battery interface updated\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

static esp_err_t api_set_inverter_interface_handler(httpd_req_t *req) {
    char content[100] = {0};
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);

    if (ret <= 0) {
        const char* json = "{\"success\":false,\"error\":\"No data received\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }

    StaticJsonDocument<128> doc;
    DeserializationError error = deserializeJson(doc, content);
    if (error || !doc.containsKey("interface")) {
        const char* json = "{\"success\":false,\"error\":\"Invalid JSON format\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }

    int interface = doc["interface"];
    if (interface < 0 || interface > 5) {
        const char* json = "{\"success\":false,\"error\":\"Interface must be 0-5\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }

    ReceiverNetworkConfig::setInverterInterface((uint8_t)interface);

    if (send_component_interface_selection(ReceiverNetworkConfig::getBatteryInterface(), (uint8_t)interface)) {
        LOG_INFO("API", "Inverter interface %d sent to transmitter via ESP-NOW", interface);
    } else {
        LOG_WARN("API", "Could not send inverter interface to transmitter (may be offline)");
    }

    const char* json = "{\"success\":true,\"message\":\"Inverter interface updated\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

} // namespace

int register_type_selection_api_handlers(httpd_handle_t server) {
    int count = 0;

    httpd_uri_t handlers[] = {
        {.uri = "/api/get_battery_types", .method = HTTP_GET, .handler = api_get_battery_types_handler, .user_ctx = NULL},
        {.uri = "/api/get_inverter_types", .method = HTTP_GET, .handler = api_get_inverter_types_handler, .user_ctx = NULL},
        {.uri = "/api/get_selected_types", .method = HTTP_GET, .handler = api_get_selected_types_handler, .user_ctx = NULL},
        {.uri = "/api/set_battery_type", .method = HTTP_POST, .handler = api_set_battery_type_handler, .user_ctx = NULL},
        {.uri = "/api/set_inverter_type", .method = HTTP_POST, .handler = api_set_inverter_type_handler, .user_ctx = NULL},
        {.uri = "/api/get_battery_interfaces", .method = HTTP_GET, .handler = api_get_battery_interfaces_handler, .user_ctx = NULL},
        {.uri = "/api/get_inverter_interfaces", .method = HTTP_GET, .handler = api_get_inverter_interfaces_handler, .user_ctx = NULL},
        {.uri = "/api/get_selected_interfaces", .method = HTTP_GET, .handler = api_get_selected_interfaces_handler, .user_ctx = NULL},
        {.uri = "/api/set_battery_interface", .method = HTTP_POST, .handler = api_set_battery_interface_handler, .user_ctx = NULL},
        {.uri = "/api/set_inverter_interface", .method = HTTP_POST, .handler = api_set_inverter_interface_handler, .user_ctx = NULL}
    };

    for (size_t i = 0; i < sizeof(handlers) / sizeof(httpd_uri_t); ++i) {
        if (httpd_register_uri_handler(server, &handlers[i]) == ESP_OK) {
            count++;
        }
    }

    return count;
}
