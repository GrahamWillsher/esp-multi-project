#include "systeminfo_page.h"
#include "../common/nav_buttons.h"
#include "../common/page_generator.h"
#include <Arduino.h>
#include <WiFi.h>
#include <ESP.h>

// System info page handler - System information page
esp_err_t systeminfo_handler(httpd_req_t *req) {
    String content = R"rawliteral(
    <h1>ESP-NOW Receiver</h1>
    <h2>System Information</h2>
    )rawliteral";
    
    // Add navigation buttons from central registry
    content += "    " + generate_nav_buttons("/systeminfo");
    
    content += R"rawliteral(
    
    <div class='info-box'>
        <h3>Device Details</h3>
        <div class='info-row'>
            <span class='info-label'>Device:</span>
            <span class='info-value'>ESP32 T-Display-S3</span>
        </div>
        <div class='info-row'>
            <span class='info-label'>Chip Model:</span>
            <span class='info-value' id='chipModel'>Loading...</span>
        </div>
        <div class='info-row'>
            <span class='info-label'>Chip Revision:</span>
            <span class='info-value' id='chipRevision'>Loading...</span>
        </div>
        <div class='info-row'>
            <span class='info-label'>eFuse MAC:</span>
            <span class='info-value' id='efuseMac'>Loading...</span>
        </div>
        <div class='info-row'>
            <span class='info-label'>WiFi SSID:</span>
            <span class='info-value' id='ssid'>Loading...</span>
        </div>
        <div class='info-row'>
            <span class='info-label'>IP Address:</span>
            <span class='info-value' id='ip'>Loading...</span>
        </div>
        <div class='info-row'>
            <span class='info-label'>MAC Address:</span>
            <span class='info-value' id='mac'>Loading...</span>
        </div>
        <div class='info-row'>
            <span class='info-label'>WiFi Channel:</span>
            <span class='info-value' id='channel'>Loading...</span>
        </div>
    </div>
    
    <div class='settings-note'>
        📡 Settings are retrieved via ESP-NOW from remote device
    </div>
)rawliteral";

    String script = R"rawliteral(
        // Load system info
        fetch('/api/data')
            .then(response => response.json())
            .then(data => {
                document.getElementById('chipModel').textContent = data.chipModel || 'N/A';
                document.getElementById('chipRevision').textContent = data.chipRevision || 'N/A';
                document.getElementById('efuseMac').textContent = data.efuseMac || 'N/A';
                document.getElementById('ssid').textContent = data.ssid || 'N/A';
                document.getElementById('ip').textContent = data.ip || 'N/A';
                document.getElementById('mac').textContent = data.mac || 'N/A';
                document.getElementById('channel').textContent = data.channel || 'N/A';
            })
            .catch(err => {
                console.error('Failed to load system info:', err);
            });
    )rawliteral";

    String html = generatePage("ESP-NOW Receiver - System Info", content, "", script);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html.c_str(), html.length());
    return ESP_OK;
}

/**
 * @brief Register the systeminfo page handler with the HTTP server
 */
esp_err_t register_systeminfo_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri       = "/systeminfo",
        .method    = HTTP_GET,
        .handler   = systeminfo_handler,
        .user_ctx  = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}
