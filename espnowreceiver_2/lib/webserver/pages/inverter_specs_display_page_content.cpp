#include "inverter_specs_display_page_content.h"
#include "../common/spec_page_layout.h"

const WebserverCommonSpecLayout::SpecPageParams& get_inverter_specs_page_params() {
    static const WebserverCommonSpecLayout::SpecPageParams kParams = {
        .page_title     = "Inverter Specifications",
        .heading        = "&#9889; Inverter Specifications",
        .subtitle       = "Inverter Configuration (Real-time from MQTT)",
        .source_topic   = "transmitter/BE/spec_data_2",
        .gradient_start = "#f093fb",
        .gradient_end   = "#f5576c",
        .accent_color   = "#f5576c",
    };
    return kParams;
}

const char* get_inverter_specs_section_fmt() {
    return R"(
        <div class="specs-grid">
            <div class="spec-card">
                <div class="spec-label">Protocol</div>
                <div class="spec-value" id="inverterProtocolValue" style="font-size: 1.4em;">%s</div>
                <div id="inverterTypeIdValue" style="display:none;">%d</div>
                <div class="spec-unit">Interface: <span id="inverterInterfaceValue">Loading...</span></div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Input Voltage Range</div>
                <div class="spec-value">%.1f - %.1f<span class="spec-unit">V</span></div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Output Voltage</div>
                <div class="spec-value">%.1f<span class="spec-unit">V</span></div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Max Output Power</div>
                <div class="spec-value">%u<span class="spec-unit">W</span></div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Efficiency</div>
                <div class="spec-value">%.1f<span class="spec-unit">%%</span></div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Input Phases</div>
                <div class="spec-value">%u</div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Output Phases</div>
                <div class="spec-value">%u</div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Communication</div>
                <div style="margin-top: 10px;">
                    <div class="feature-badge %s">%s Modbus</div>
                    <div class="feature-badge %s">%s CAN</div>
                </div>
            </div>
        </div>
)";
}
