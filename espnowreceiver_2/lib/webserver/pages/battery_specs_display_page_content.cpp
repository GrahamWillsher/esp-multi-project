#include "battery_specs_display_page_content.h"
#include "../common/spec_page_layout.h"

String get_battery_specs_page_html_header() {
    return build_spec_page_html_header("Battery Specifications",
                                       "&#128267; Battery Specifications",
                                       "Battery Emulator Configuration (Real-time from MQTT)",
                                       "BE/battery_specs",
                                       "#667eea",
                                       "#764ba2",
                                       "#667eea");
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
