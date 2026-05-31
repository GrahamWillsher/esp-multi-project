#include "monitor2_page_content.h"

const char* get_monitor2_page_content() {
    static const char kMonitor2PageContent[] = R"rawliteral(
    <h1>Battery Emulator Receiver</h1>
    <h2>Battery Monitor (SSE - Real-time)</h2>



    <div class='mode-indicator' id='mode'>Mode: Loading...</div>
    <div class='connection-status' id='connection'>⚡ Connecting...</div>

    <div class='info-box'>
        <h3>Battery Status</h3>
        <div class='data-label'>State of Charge</div>
        <div class='data-value' id='soc'>--</div>

        <div class='data-label' style='margin-top: 30px;'>Power</div>
        <div class='data-value' id='power'>--</div>

        <div class='data-label' style='margin-top: 30px;'>Voltage</div>
        <div class='data-value' id='voltage'>--</div>
    </div>

    <p class='update-note'>📡 Real-time updates via Server-Sent Events</p>
)rawliteral";
    return kMonitor2PageContent;
}
