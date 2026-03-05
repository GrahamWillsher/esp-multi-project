#include "api_type_selection_handlers.h"

#include <logging_config.h>
#include "../../receiver_config/receiver_config_manager.h"
#include "../../src/espnow/espnow_send.h"
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

static TypeEntry battery_types[] = {
    {0, "None"},
    {2, "BMW i3"},
    {3, "BMW iX"},
    {4, "Bolt/Ampera"},
    {5, "BYD Atto 3"},
    {6, "Cellpower BMS"},
    {7, "CHAdeMO"},
    {8, "CMFA EV"},
    {9, "Foxess"},
    {10, "Geely Geometry C"},
    {11, "Orion BMS"},
    {12, "Sono"},
    {13, "ECMP"},
    {14, "i-MiEV/C-Zero"},
    {15, "Jaguar I-PACE"},
    {16, "Kia E-GMP"},
    {17, "Kia/Hyundai 64"},
    {18, "Kia/Hyundai Hybrid"},
    {19, "VW MEB"},
    {20, "MG 5"},
    {21, "Nissan Leaf"},
    {22, "Pylon"},
    {23, "Daly BMS"},
    {24, "RJXZS BMS"},
    {25, "Range Rover PHEV"},
    {26, "Renault Kangoo"},
    {27, "Renault Twizy"},
    {28, "Renault Zoe Gen1"},
    {29, "Renault Zoe Gen2"},
    {30, "Santa Fe PHEV"},
    {31, "SimpBMS"},
    {32, "Tesla Model 3/Y"},
    {33, "Tesla Model S/X"},
    {34, "Test/Fake"},
    {35, "Volvo SPA"},
    {36, "Volvo SPA Hybrid"},
    {37, "MG HS PHEV"},
    {38, "Samsung SDI LV"},
    {39, "Hyundai Ioniq 28"},
    {40, "Kia 64FD"},
    {41, "Relion LV"},
    {42, "Rivian"},
    {43, "BMW PHEV"},
    {44, "Ford Mach-E"},
    {45, "CMP Smart"},
    {46, "Maxus EV80"}
};

static TypeEntry inverter_types[] = {
    {0, "None"},
    {1, "Afore battery over CAN"},
    {2, "BYD Battery-Box Premium HVS over CAN Bus"},
    {3, "BYD 11kWh HVM battery over Modbus RTU"},
    {4, "Ferroamp Pylon battery over CAN bus"},
    {5, "FoxESS compatible HV2600/ECS4100 battery"},
    {6, "Growatt High Voltage protocol via CAN"},
    {7, "Growatt Low Voltage (48V) protocol via CAN"},
    {8, "Growatt WIT compatible battery via CAN"},
    {9, "BYD battery via Kostal RS485"},
    {10, "Pylontech HV battery over CAN bus"},
    {11, "Pylontech LV battery over CAN bus"},
    {12, "Schneider V2 SE BMS CAN"},
    {13, "SMA compatible BYD H"},
    {14, "SMA compatible BYD Battery-Box HVS"},
    {15, "SMA Low Voltage (48V) protocol via CAN"},
    {16, "SMA Tripower CAN"},
    {17, "Sofar BMS (Extended) via CAN, Battery ID"},
    {18, "SolaX Triple Power LFP over CAN bus"},
    {19, "Solxpow compatible battery"},
    {20, "Sol-Ark LV protocol over CAN bus"},
    {21, "Sungrow SBRXXX emulation over CAN bus"}
};

static TypeEntry battery_interfaces[] = {
    {0, "Modbus"},
    {1, "RS485"},
    {2, "CAN (Native)"},
    {3, "CAN-FD (Native)"},
    {4, "CAN (MCP2515 add-on)"},
    {5, "CAN-FD (MCP2518 add-on)"}
};

static TypeEntry inverter_interfaces[] = {
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

    String json = "{\"types\":[";
    for (size_t i = 0; i < count; i++) {
        if (i > 0) json += ",";
        json += "{\"id\":" + String(sorted_copy[i].id) + ",\"name\":\"" + sorted_copy[i].name + "\"}";
    }
    json += "]}";

    delete[] sorted_copy;
    return json;
}

static esp_err_t api_get_battery_types_handler(httpd_req_t *req) {
    String json_response = generate_sorted_type_json(battery_types, sizeof(battery_types) / sizeof(TypeEntry));
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_response.c_str(), json_response.length());
    return ESP_OK;
}

static esp_err_t api_get_inverter_types_handler(httpd_req_t *req) {
    String json_response = generate_sorted_type_json(inverter_types, sizeof(inverter_types) / sizeof(TypeEntry));
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
    String json_response = generate_sorted_type_json(inverter_interfaces, sizeof(inverter_interfaces) / sizeof(TypeEntry));
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
