#include "inverter_specs_display_page.h"
#include "inverter_specs_display_page_content.h"
#include "inverter_specs_display_page_script.h"
#include "generic_specs_page.h"
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

    GenericSpecsPage::RenderConfig render_config = {
        .log_tag = "INVERTER_PAGE",
        .specs_section_size = 2048,
        .total_slack_bytes = 768,
        .allocate_specs_section_in_psram = true,
    };
    return GenericSpecsPage::send_formatted_page(req, html_header, html_specs_fmt, html_footer, render_config,
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
}

esp_err_t register_inverter_specs_page(httpd_handle_t server) {
    return GenericSpecsPage::register_page(server, "/inverter_settings.html", inverter_specs_page_handler);
}