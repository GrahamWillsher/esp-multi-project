#include "monitor_page_content.h"

const char* get_monitor_page_content() {
    static const char kMonitorPageContent[] = R"rawliteral(
    <h1>Battery Emulator Receiver</h1>
    <h2>Battery Monitor</h2>


    <div class='mode-indicator' id='mode'>Mode: Loading...</div>

    <div class='info-box'>
        <h3>Battery Status</h3>
        <div class='data-label'>State of Charge</div>
        <div class='data-value' id='soc'>--</div>

        <div class='data-label' style='margin-top: 30px;'>Power</div>
        <div class='data-value' id='power'>--</div>

        <div class='data-label' style='margin-top: 30px;'>Voltage</div>
        <div class='data-value' id='voltage'>--</div>
    </div>

    <div class='info-box' style='margin-top: 30px;'>
        <h3>System Runtime</h3>
        <div class='data-label'>Contactor State</div>
        <div class='data-value' id='contactor' style='font-size: 32px;'>--</div>

        <div class='data-label' style='margin-top: 20px;'>Charger</div>
        <div class='data-value' id='charger' style='font-size: 28px;'>--</div>

        <div class='data-label' style='margin-top: 20px;'>Inverter</div>
        <div class='data-value' id='inverter' style='font-size: 28px;'>--</div>
    </div>

    <p class='update-note'>📊 Snapshot polling: 5s baseline (adaptive backoff under load)</p>
)rawliteral";
    return kMonitorPageContent;
}
