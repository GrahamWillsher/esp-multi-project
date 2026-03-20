#include "debug_page.h"
#include "../common/page_generator.h"
#include "../common/nav_buttons.h"
#include <Arduino.h>

// Debug page handler
static esp_err_t debug_page_handler(httpd_req_t *req) {
        String content = R"rawliteral(
<div class='debug-control'>
        <h3>📊 Transmitter Debug Level Control</h3>
        <p>Control the debug logging level of the ESP-NOW transmitter. Messages are published to MQTT topic: <code>espnow/transmitter/debug/{level}</code></p>

        <div class='current-level-box'>
                <strong class='current-level-label'>Current Debug Level:</strong>
                <span id='currentLevel' class='current-level-value'>Loading...</span>
        </div>

        <label for='debugLevel'><strong>Select Debug Level:</strong></label><br>
        <select id='debugLevel' name='debugLevel'>
                <option value='0'>EMERG - Emergency (0) - System unusable</option>
                <option value='1'>ALERT - Alert (1) - Immediate action required</option>
                <option value='2'>CRIT - Critical (2) - Critical conditions</option>
                <option value='3'>ERROR - Error (3) - Error conditions</option>
                <option value='4'>WARNING - Warning (4) - Warning conditions</option>
                <option value='5'>NOTICE - Notice (5) - Normal but significant</option>
                <option value='6' selected>INFO - Info (6) - Informational messages</option>
                <option value='7'>DEBUG - Debug (7) - Debug-level messages</option>
        </select><br>

        <button onclick='setDebugLevel()' class='button'>Set Transmitter Debug Level</button>
        <div id='debug-status'></div>
</div>
)rawliteral";

        content += generate_nav_buttons("/debug");

        const String extra_styles = R"rawliteral(
.debug-control {
        background-color: #2C3539;
        padding: 20px;
        border-radius: 8px;
        margin-bottom: 20px;
}

.debug-control h3 {
        margin-top: 0;
        color: #50FA7B;
}

.debug-control select {
        padding: 10px;
        margin: 10px 5px;
        font-size: 16px;
        border-radius: 4px;
}

.debug-control button {
        padding: 12px 24px;
        margin: 10px 5px;
        font-size: 16px;
}

.current-level-box {
        background-color: #1e1e1e;
        padding: 12px;
        border-left: 4px solid #50FA7B;
        margin-bottom: 15px;
        border-radius: 4px;
}

.current-level-label {
        color: #50FA7B;
}

.current-level-value {
        margin-left: 10px;
        color: #fff;
        font-size: 18px;
        font-weight: bold;
}

#debug-status {
        margin-top: 15px;
        padding: 12px;
        border-radius: 4px;
        display: none;
}

.status-success {
        background-color: #28a745;
        color: white;
        display: block;
}

.status-error {
        background-color: #dc3545;
        color: white;
        display: block;
}

.status-info {
        background-color: #17a2b8;
        color: white;
        display: block;
}
)rawliteral";

        const String script = R"rawliteral(
const levelNames = ['EMERG', 'ALERT', 'CRIT', 'ERROR', 'WARNING', 'NOTICE', 'INFO', 'DEBUG'];

function setDebugLevel() {
    const level = document.getElementById('debugLevel').value;
    const statusDiv = document.getElementById('debug-status');

    statusDiv.textContent = 'Sending debug level ' + level + ' (' + levelNames[level] + ') to transmitter...';
    statusDiv.className = 'status-info';

    fetch('/api/setDebugLevel?level=' + level)
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                statusDiv.textContent = '✓ ' + data.message;
                statusDiv.className = 'status-success';
                loadCurrentDebugLevel();
            } else {
                statusDiv.textContent = '✗ ' + data.message;
                statusDiv.className = 'status-error';
            }
            setTimeout(() => {
                statusDiv.style.display = 'none';
            }, 5000);
        })
        .catch(error => {
            statusDiv.textContent = '✗ Error: ' + error;
            statusDiv.className = 'status-error';
        });
}

function loadCurrentDebugLevel() {
    fetch('/api/debugLevel')
        .then(response => response.json())
        .then(data => {
            if (data.level !== undefined) {
                const levelNum = data.level;
                const levelName = levelNames[levelNum] || 'UNKNOWN';
                document.getElementById('currentLevel').textContent = levelName + ' (' + levelNum + ')';
                document.getElementById('debugLevel').value = levelNum;
            } else {
                document.getElementById('currentLevel').textContent = 'Unknown';
            }
        })
        .catch(() => {
            document.getElementById('currentLevel').textContent = 'Unable to load';
        });
}

window.addEventListener('load', loadCurrentDebugLevel);
)rawliteral";

        String page = renderPage("Debug Logging Control", content, PageRenderOptions(extra_styles, script));
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, page.c_str(), page.length());
    return ESP_OK;
}

// Register debug page
esp_err_t register_debug_page(httpd_handle_t server) {
    httpd_uri_t debug_uri = {
        .uri = "/debug",
        .method = HTTP_GET,
        .handler = debug_page_handler,
        .user_ctx = NULL
    };
    return httpd_register_uri_handler(server, &debug_uri);
}
