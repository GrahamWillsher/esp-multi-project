#include "battery_specs_display_page.h"
#include "battery_specs_display_page_content.h"
#include "battery_specs_display_page_script.h"
#include "generic_specs_page.h"
#include "../common/spec_page_layout.h"
#include "../utils/transmitter_manager.h"
#include "../page_definitions.h"
#include "../logging.h"
#include <ArduinoJson.h>

/**
 * @brief Battery Specs Display Page
 *
 * Displays static battery configuration received from transmitter via MQTT.
 * Source: BE/battery_specs MQTT topic
 */
esp_err_t battery_specs_page_handler(httpd_req_t *req) {
    String specs_json = TransmitterManager::getBatterySpecsJson();

    DynamicJsonDocument doc(512);
    String battery_type = "Unknown";
    uint32_t nominal_capacity_wh = 0;
    uint16_t max_design_voltage_dv = 0;
    uint8_t number_of_cells = 0;
    uint32_t min_design_voltage_dv = 0;
    float max_charge_current_a = 0;
    float max_discharge_current_a = 0;
    uint8_t battery_chemistry = 0;

    if (specs_json.length() > 0) {
        DeserializationError error = deserializeJson(doc, specs_json);
        if (!error) {
            battery_type = doc["battery_type"].as<String>();
            if (battery_type.length() == 0) battery_type = "Unknown";

            nominal_capacity_wh = doc["nominal_capacity_wh"] | 30000;
            float max_voltage_v  = doc["max_design_voltage"] | 500.0f;
            max_design_voltage_dv = (uint16_t)(max_voltage_v * 10);
            number_of_cells = doc["number_of_cells"] | 96;
            float min_voltage_v  = doc["min_design_voltage"] | 270.0f;
            min_design_voltage_dv = (uint32_t)(min_voltage_v * 10);
            max_charge_current_a    = 120.0f;
            max_discharge_current_a = 120.0f;
            battery_chemistry = doc["battery_chemistry"] | 0;
        }
    }

    // Retrieve page sections from module functions
    String html_header         = get_battery_specs_page_html_header();
    const char* html_specs_fmt = get_battery_specs_section_fmt();
    String html_footer         = build_spec_page_html_footer(
        get_battery_specs_page_nav_links_html(),
        get_battery_specs_page_inline_script());

    // Allocate response buffer in PSRAM
    size_t specs_section_max = 2048;
    size_t total_size = html_header.length() + specs_section_max + html_footer.length() + 256;

    char* response = (char*)ps_malloc(total_size);
    if (!response) {
        LOG_ERROR("BATTERY_PAGE", "Failed to allocate %d bytes in PSRAM", total_size);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }

    char specs_section[2048];
    snprintf(specs_section, sizeof(specs_section), html_specs_fmt,
             battery_type.c_str(),
             nominal_capacity_wh,
             max_design_voltage_dv / 10.0f,
             min_design_voltage_dv / 10.0f,
             number_of_cells,
             max_charge_current_a / 10.0f,
             max_discharge_current_a / 10.0f,
             battery_chemistry);

    size_t offset = 0;
    offset += snprintf(response + offset, total_size - offset, "%s", html_header.c_str());
    offset += snprintf(response + offset, total_size - offset, "%s", specs_section);
    offset += snprintf(response + offset, total_size - offset, "%s", html_footer.c_str());

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, response, strlen(response));

    free(response);
    LOG_INFO("BATTERY_PAGE", "Battery specs page served (%d bytes)", offset);

    return ESP_OK;
}

esp_err_t register_battery_specs_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri       = "/battery_settings.html",
        .method    = HTTP_GET,
        .handler   = battery_specs_page_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}