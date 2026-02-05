#include "monitor_page.h"
#include "../common/page_generator.h"
#include "../common/nav_buttons.h"
#include <Arduino.h>

/**
 * @brief Handler for the battery monitor page (non-SSE version)
 * 
 * This page displays battery SOC and Power with 1-second auto-refresh via polling.
 * Unlike monitor2_page which uses SSE, this uses interval-based fetch requests.
 */
static esp_err_t monitor_handler(httpd_req_t *req) {
    String content = R"rawliteral(
    <h1>ESP-NOW Receiver</h1>
    <h2>Battery Monitor</h2>
    )rawliteral";
    
    // Add navigation buttons from central registry
    content += "    " + generate_nav_buttons("/monitor");
    
    content += R"rawliteral(
    
    <div class='mode-indicator' id='mode'>Mode: Loading...</div>
    
    <div class='info-box'>
        <h3>Battery Status</h3>
        <div class='data-label'>State of Charge</div>
        <div class='data-value' id='soc'>--</div>
        
        <div class='data-label' style='margin-top: 30px;'>Power</div>
        <div class='data-value' id='power'>--</div>
    </div>
    
    <p class='update-note'>📊 Auto-update every 1 second</p>
)rawliteral";

    String extraStyles = R"rawliteral(
        .info-box { text-align: center; }
        .data-value { 
            font-size: 48px; 
            font-weight: bold;
            color: #fff;
            margin: 10px 0;
        }
        .data-label { 
            font-size: 20px; 
            color: #FFD700;
            margin-bottom: 5px;
        }
        .mode-indicator { 
            color: #ff9800; 
            font-size: 16px;
            font-weight: bold;
            padding: 10px;
            background-color: #3a4b54;
            border-radius: 10px;
            margin: 15px 0;
        }
        .update-note {
            color: #888;
            font-size: 14px;
            margin-top: 20px;
        }
    )rawliteral";

    String script = R"rawliteral(
        function updateData() {
            fetch('/api/monitor')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('mode').innerText = 'Mode: ' + (data.mode === 'test' ? 'Test Data' : 'Real ESP-NOW Data');
                    document.getElementById('soc').innerText = data.soc + ' %';
                    document.getElementById('power').innerText = data.power + ' W';
                })
                .catch(err => console.error('Update failed:', err));
        }
        setInterval(updateData, 1000);
        window.onload = updateData;
    )rawliteral";

    String html = generatePage("ESP-NOW Receiver - Battery Monitor", content, extraStyles, script);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html.c_str(), html.length());
    return ESP_OK;
}

/**
 * @brief Register the monitor page handler with the HTTP server
 */
esp_err_t register_monitor_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri       = "/monitor",
        .method    = HTTP_GET,
        .handler   = monitor_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}
