#include "api_type_selection_handlers.h"

#include "api_request_utils.h"
#include "api_response_utils.h"
#include <webserver_common_utils/http_json_utils.h>

#include <esp32common/logging/logging_config.h>
#include "../../receiver_config/receiver_config_manager.h"
#include "../../src/espnow/espnow_send.h"
#include "../../src/espnow/component_apply_tracker.h"
#include "../../src/espnow/type_catalog_cache.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_system.h>
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

static constexpr size_t kMaxTypeEntries = 128;

static String generate_sorted_type_json(TypeEntry* types, size_t count) {
    if (count > kMaxTypeEntries) {
        return "{\"types\":[]}";
    }

    TypeEntry sorted_copy[kMaxTypeEntries];
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
    return json;
}

static const char* component_apply_state_to_string(ComponentApplyTracker::State state) {
    switch (state) {
        case ComponentApplyTracker::State::idle: return "idle";
        case ComponentApplyTracker::State::pending: return "pending";
        case ComponentApplyTracker::State::persisted: return "persisted";
        case ComponentApplyTracker::State::ready_for_reboot: return "ready_for_reboot";
        case ComponentApplyTracker::State::failed: return "failed";
        case ComponentApplyTracker::State::timed_out: return "timed_out";
        default: return "unknown";
    }
}

static esp_err_t api_component_apply_handler(httpd_req_t *req) {
    char content[256] = {0};
    StaticJsonDocument<256> doc;
    esp_err_t response_error = ESP_OK;
    if (!ApiRequestUtils::read_json_body_or_respond(req, content, sizeof(content), doc, &response_error)) {
        return response_error;
    }

    const int apply_mask = doc["apply_mask"] | 0;
    if (apply_mask <= 0 || apply_mask > 0x0F) {
        return ApiResponseUtils::send_jsonf(req, "{\"success\":false,\"error\":\"apply_mask must be 1-15\"}");
    }

    const uint8_t mask = static_cast<uint8_t>(apply_mask);
    const uint8_t battery_type = static_cast<uint8_t>((doc["battery_type"] | ReceiverNetworkConfig::getBatteryType()));
    const uint8_t inverter_type = static_cast<uint8_t>((doc["inverter_type"] | ReceiverNetworkConfig::getInverterType()));
    const uint8_t battery_interface = static_cast<uint8_t>((doc["battery_interface"] | ReceiverNetworkConfig::getBatteryInterface()));
    const uint8_t inverter_interface = static_cast<uint8_t>((doc["inverter_interface"] | ReceiverNetworkConfig::getInverterInterface()));

    if ((mask & component_apply_battery_interface) && battery_interface > 5) {
        return ApiResponseUtils::send_jsonf(req, "{\"success\":false,\"error\":\"Battery interface must be 0-5\"}");
    }

    if ((mask & component_apply_inverter_interface) && inverter_interface > 5) {
        return ApiResponseUtils::send_jsonf(req, "{\"success\":false,\"error\":\"Inverter interface must be 0-5\"}");
    }

    const uint32_t request_id = static_cast<uint32_t>(esp_random() ^ millis());

    if (!ComponentApplyTracker::start_transaction(request_id, mask, battery_type, inverter_type, battery_interface, inverter_interface)) {
        return ApiResponseUtils::send_jsonf(req, "{\"success\":false,\"error\":\"Failed to start apply transaction\"}");
    }

    if (!send_component_apply_request(request_id, mask, battery_type, inverter_type, battery_interface, inverter_interface)) {
        ComponentApplyTracker::mark_failed(request_id, "Failed to send apply request to transmitter");
        return ApiResponseUtils::send_jsonf(req, "{\"success\":false,\"error\":\"Failed to send apply request to transmitter\"}");
    }

    if (mask & component_apply_battery_type) {
        ReceiverNetworkConfig::setBatteryType(battery_type);
    }
    if (mask & component_apply_inverter_type) {
        ReceiverNetworkConfig::setInverterType(inverter_type);
    }
    if (mask & component_apply_battery_interface) {
        ReceiverNetworkConfig::setBatteryInterface(battery_interface);
    }
    if (mask & component_apply_inverter_interface) {
        ReceiverNetworkConfig::setInverterInterface(inverter_interface);
    }

    return ApiResponseUtils::send_jsonf(req,
                                        "{\"success\":true,\"request_id\":%lu,\"state\":\"pending\",\"message\":\"Apply request dispatched\"}",
                                        static_cast<unsigned long>(request_id));
}

static esp_err_t api_component_apply_status_handler(httpd_req_t *req) {
    uint32_t requested_id = 0;
    bool has_requested_id = false;

    char query[96] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char value[32] = {0};
        if (httpd_query_key_value(query, "request_id", value, sizeof(value)) == ESP_OK) {
            requested_id = static_cast<uint32_t>(strtoul(value, nullptr, 10));
            has_requested_id = (requested_id != 0);
        }
    }

    ComponentApplyTracker::Snapshot snapshot = ComponentApplyTracker::get_snapshot();

    const bool request_mismatch = has_requested_id && snapshot.request_id != requested_id;
    const bool in_progress = (!request_mismatch) &&
                             (snapshot.state == ComponentApplyTracker::State::pending ||
                              snapshot.state == ComponentApplyTracker::State::persisted);
    const bool last_success = (!request_mismatch) && snapshot.success;

    if (request_mismatch) {
        LOG_WARN("COMP_APPLY_STATUS",
                 "Request mismatch: requested=%lu snapshot=%lu state=%s",
                 static_cast<unsigned long>(requested_id),
                 static_cast<unsigned long>(snapshot.request_id),
                 component_apply_state_to_string(snapshot.state));
    }

    StaticJsonDocument<512> doc;
    doc["success"] = true;
    doc["in_progress"] = in_progress;
    doc["last_success"] = last_success;
    doc["request_match"] = !request_mismatch;

    if (request_mismatch) {
        doc["request_id"] = requested_id;
        doc["state"] = "pending";
        doc["ready_for_reboot"] = false;
        doc["message"] = "Waiting for matching apply transaction";

        String json;
        json.reserve(192);
        serializeJson(doc, json);

        return ApiResponseUtils::send_json_no_cache(req, json.c_str());
    }

    doc["request_id"] = snapshot.request_id;
    doc["state"] = component_apply_state_to_string(snapshot.state);
    doc["started_ms"] = snapshot.started_ms;
    doc["updated_ms"] = snapshot.updated_ms;
    doc["apply_mask"] = snapshot.apply_mask;
    doc["persisted_mask"] = snapshot.persisted_mask;
    const bool persisted_complete = (snapshot.persisted_mask == snapshot.apply_mask);
    const bool reboot_gate_ready = snapshot.success &&
                                   snapshot.reboot_required &&
                                   snapshot.ready_for_reboot &&
                                   persisted_complete;
    doc["persisted_complete"] = persisted_complete;
    doc["ack_success"] = snapshot.success;
    doc["reboot_required"] = snapshot.reboot_required;
    doc["ack_ready_for_reboot"] = snapshot.ready_for_reboot;
    doc["ready_for_reboot"] = reboot_gate_ready;
    doc["battery_type"] = snapshot.battery_type;
    doc["inverter_type"] = snapshot.inverter_type;
    doc["battery_interface"] = snapshot.battery_interface;
    doc["inverter_interface"] = snapshot.inverter_interface;
    doc["settings_version"] = snapshot.settings_version;
    doc["last_error"] = (snapshot.state == ComponentApplyTracker::State::failed ||
                           snapshot.state == ComponentApplyTracker::State::timed_out)
                              ? snapshot.message
                              : "";
    doc["message"] = snapshot.message;

    String json;
    json.reserve(384);
    serializeJson(doc, json);

    return ApiResponseUtils::send_json_no_cache(req, json.c_str());
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
        return HttpJsonUtils::send_json(req, "{\"types\":[],\"loading\":true}");
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
    return HttpJsonUtils::send_json(req, json_response.c_str());
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
        return HttpJsonUtils::send_json(req, "{\"types\":[],\"loading\":true}");
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
    return HttpJsonUtils::send_json(req, json_response.c_str());
}

static esp_err_t api_get_selected_types_handler(httpd_req_t *req) {
    uint8_t battery_type = ReceiverNetworkConfig::getBatteryType();
    uint8_t inverter_type = ReceiverNetworkConfig::getInverterType();

    return ApiResponseUtils::send_jsonf(req,
                                        "{\"battery_type\":%d,\"inverter_type\":%d}",
                                        battery_type,
                                        inverter_type);
}

static esp_err_t api_get_battery_interfaces_handler(httpd_req_t *req) {
    String json_response = generate_sorted_type_json(battery_interfaces, sizeof(battery_interfaces) / sizeof(TypeEntry));
    return HttpJsonUtils::send_json(req, json_response.c_str());
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
        return HttpJsonUtils::send_json(req, "{\"types\":[],\"loading\":true}");
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

    return HttpJsonUtils::send_json(req, json_response.c_str());
}

static esp_err_t api_get_selected_interfaces_handler(httpd_req_t *req) {
    uint8_t battery_interface = ReceiverNetworkConfig::getBatteryInterface();
    uint8_t inverter_interface = ReceiverNetworkConfig::getInverterInterface();

    return ApiResponseUtils::send_jsonf(req,
                                        "{\"battery_interface\":%d,\"inverter_interface\":%d}",
                                        battery_interface,
                                        inverter_interface);
}

} // namespace

int register_type_selection_api_handlers(httpd_handle_t server) {
    int count = 0;

    static const httpd_uri_t handlers[] = {
        {.uri = "/api/get_battery_types", .method = HTTP_GET, .handler = api_get_battery_types_handler, .user_ctx = NULL},
        {.uri = "/api/get_inverter_types", .method = HTTP_GET, .handler = api_get_inverter_types_handler, .user_ctx = NULL},
        {.uri = "/api/get_selected_types", .method = HTTP_GET, .handler = api_get_selected_types_handler, .user_ctx = NULL},
        {.uri = "/api/get_battery_interfaces", .method = HTTP_GET, .handler = api_get_battery_interfaces_handler, .user_ctx = NULL},
        {.uri = "/api/get_inverter_interfaces", .method = HTTP_GET, .handler = api_get_inverter_interfaces_handler, .user_ctx = NULL},
        {.uri = "/api/get_selected_interfaces", .method = HTTP_GET, .handler = api_get_selected_interfaces_handler, .user_ctx = NULL},
        {.uri = "/api/component_apply", .method = HTTP_POST, .handler = api_component_apply_handler, .user_ctx = NULL},
        {.uri = "/api/component_apply_status", .method = HTTP_GET, .handler = api_component_apply_status_handler, .user_ctx = NULL}
    };

    for (size_t i = 0; i < sizeof(handlers) / sizeof(httpd_uri_t); ++i) {
        if (httpd_register_uri_handler(server, &handlers[i]) == ESP_OK) {
            count++;
        }
    }

    return count;
}

int expected_type_selection_api_handlers() {
    return 8;
}
