#include "inverter_specs_display_page.h"
#include "inverter_specs_display_page_content.h"
#include "inverter_specs_display_page_script.h"
#include "../common/spec_page_layout.h"
#include "../utils/transmitter_manager.h"
#include "../page_definitions.h"
#include "../logging.h"
#include <ArduinoJson.h>

/**
 * @brief Inverter Specs Display Page
 *
 * Displays static inverter configuration received from transmitter via MQTT.
 * Source: transmitter/BE/spec_data_2 MQTT topic
 */
esp_err_t inverter_specs_page_handler(httpd_req_t *req) {
    // Parse JSON from TransmitterManager
    String specs_json = TransmitterManager::getInverterSpecsJson();

    DynamicJsonDocument doc(512);
    String inverter_protocol = "Unknown";
    int inverter_type_id = -1;
    uint16_t min_input_voltage_dv = 0;
    uint16_t max_input_voltage_dv = 0;
    uint16_t nominal_output_voltage_dv = 0;
    uint16_t max_output_power_w = 0;
    uint8_t supports_modbus = 0;
    uint8_t supports_can = 0;
    uint16_t efficiency_percent = 0;
    uint8_t input_phases = 0;
    uint8_t output_phases = 0;

    if (specs_json.length() > 0) {
        DeserializationError error = deserializeJson(doc, specs_json);
        if (!error) {
            inverter_type_id        = doc["inverter_type_id"] | -1;
            inverter_protocol       = doc["inverter_protocol_name"].as<String>();
            if (inverter_protocol.length() == 0) {
                inverter_protocol = doc["inverter_protocol"].as<String>();
            }
            min_input_voltage_dv    = doc["min_input_voltage_dv"] | 1800;
            max_input_voltage_dv    = doc["max_input_voltage_dv"] | 5500;
            nominal_output_voltage_dv = doc["nominal_output_voltage_dv"] | 2300;
            max_output_power_w      = doc["max_output_power_w"] | 10000;
            supports_modbus         = doc["supports_modbus"] | 0;
            supports_can            = doc["supports_can"] | 0;
            efficiency_percent      = doc["efficiency_percent"] | 950;
            input_phases            = doc["input_phases"] | 3;
            output_phases           = doc["output_phases"] | 3;
        }
    }

    if (inverter_protocol.length() == 0) {
        inverter_protocol = "Unknown";
    }

    // Retrieve page sections from module functions
    String html_header           = get_inverter_specs_page_html_header();
    const char* html_specs_fmt   = get_inverter_specs_section_fmt();
    String html_footer           = build_spec_page_html_footer(
        get_inverter_specs_page_nav_links_html(),
        get_inverter_specs_page_inline_script());

    // Allocate response buffer in PSRAM
    size_t specs_section_max = 2048;
    size_t total_size = html_header.length() + specs_section_max + html_footer.length() + 768;

    char* response = (char*)ps_malloc(total_size);
    if (!response) {
        LOG_ERROR("INVERTER_PAGE", "Failed to allocate %d bytes in PSRAM", total_size);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }

    char* specs_section = (char*)ps_malloc(2048);
    if (!specs_section) {
        free(response);
        LOG_ERROR("INVERTER_PAGE", "Failed to allocate specs buffer in PSRAM");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }

    snprintf(specs_section, 2048, html_specs_fmt,
             inverter_protocol.c_str(),
             inverter_type_id,
             min_input_voltage_dv / 10.0f,
             max_input_voltage_dv / 10.0f,
             nominal_output_voltage_dv / 10.0f,
             max_output_power_w,
             efficiency_percent / 10.0f,
             input_phases,
             output_phases,
             supports_modbus ? "enabled" : "disabled",
             supports_modbus ? "&#10003;" : "&#10007;",
             supports_can ? "enabled" : "disabled",
             supports_can ? "&#10003;" : "&#10007;");

    size_t offset = 0;
    offset += snprintf(response + offset, total_size - offset, "%s", html_header.c_str());
    offset += snprintf(response + offset, total_size - offset, "%s", specs_section);
    offset += snprintf(response + offset, total_size - offset, "%s", html_footer.c_str());

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, response, strlen(response));

    free(specs_section);
    free(response);
    LOG_INFO("INVERTER_PAGE", "Inverter specs page served (%d bytes)", offset);

    return ESP_OK;
}

esp_err_t register_inverter_specs_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri       = "/inverter_settings.html",
        .method    = HTTP_GET,
        .handler   = inverter_specs_page_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}