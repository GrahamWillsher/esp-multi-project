#include "battery_specs_display_page_content.h"
#include "../common/spec_page_layout.h"

const WebserverCommonSpecLayout::SpecPageParams& get_battery_specs_page_params() {
    static const WebserverCommonSpecLayout::SpecPageParams kParams = {
        .page_title     = "Battery Specifications",
        .heading        = "&#128267; Battery Specifications",
        .subtitle       = "Battery Emulator Configuration (Real-time from MQTT)",
        .source_topic   = "BE/battery_specs",
        .gradient_start = "#667eea",
        .gradient_end   = "#764ba2",
        .accent_color   = "#667eea",
    };
    return kParams;
}

const char* get_battery_specs_section_fmt() {
    return R"(
        <div class="specs-grid">
            <div class="spec-card">
                <div class="spec-label">Battery Type</div>
                <div class="spec-value" id="batteryTypeValue">%s</div>
                <div class="spec-unit">Interface: <span id="batteryInterfaceValue">Loading...</span></div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Nominal Capacity</div>
                <div class="spec-value" id="nominalCapacityValue">%lu<span class="spec-unit">Wh</span></div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Max Design Voltage</div>
                <div class="spec-value" id="maxDesignVoltageValue">%.1f<span class="spec-unit">V</span></div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Min Design Voltage</div>
                <div class="spec-value" id="minDesignVoltageValue">%.1f<span class="spec-unit">V</span></div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Number of Cells</div>
                <div class="spec-value" id="numberOfCellsValue">%d</div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Max Charge Current</div>
                <div class="spec-value" id="maxChargeCurrentValue">%.1f<span class="spec-unit">A</span></div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Max Discharge Current</div>
                <div class="spec-value" id="maxDischargeCurrentValue">%.1f<span class="spec-unit">A</span></div>
            </div>
            <div class="spec-card">
                <div class="spec-label">Chemistry</div>
                <div class="spec-value" id="chemistryValue">%d</div>
            </div>
        </div>
)";
}
