#include "monitor2_page_content.h"
#include <Arduino.h>

String get_monitor2_page_content() {
    String content = R"rawliteral(
    <h1>ESP-NOW Receiver</h1>
    <h2>Battery Monitor (SSE - Real-time)</h2>
    <div style='margin-bottom: 20px;'>
        <a href='/' style='display: inline-block; padding: 10px 16px; background: #4CAF50; color: white; text-decoration: none; border-radius: 6px; font-weight: bold;'>
            ← Dashboard
        </a>
    </div>
    )rawliteral";

    content += R"rawliteral(

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
    return content;
}
