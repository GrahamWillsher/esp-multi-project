#include "debug_page.h"
#include "../common/page_generator.h"
#include "../common/nav_buttons.h"
#include <Arduino.h>

// Debug page processor
static String debug_page_processor() {
    String html = "";
    
    // Page-specific styles
    html += "<style>";
    html += ".debug-control { background-color: #2C3539; padding: 20px; border-radius: 8px; margin-bottom: 20px; }";
    html += ".debug-control h3 { margin-top: 0; color: #50FA7B; }";
    html += ".debug-control select { padding: 10px; margin: 10px 5px; font-size: 16px; border-radius: 4px; }";
    html += ".debug-control button { padding: 12px 24px; margin: 10px 5px; font-size: 16px; }";
    html += "#debug-status { margin-top: 15px; padding: 12px; border-radius: 4px; display: none; }";
    html += ".status-success { background-color: #28a745; color: white; display: block; }";
    html += ".status-error { background-color: #dc3545; color: white; display: block; }";
    html += ".status-info { background-color: #17a2b8; color: white; display: block; }";
    html += "</style>";
        
        // Debug Level Control Section
        html += "<div class='debug-control'>";
        html += "<h3>📊 Transmitter Debug Level Control</h3>";
        html += "<p>Control the debug logging level of the ESP-NOW transmitter. Messages are published to MQTT topic: <code>espnow/transmitter/debug/{level}</code></p>";
        html += "<label for='debugLevel'><strong>Select Debug Level:</strong></label><br>";
        html += "<select id='debugLevel' name='debugLevel'>";
        html += "<option value='0'>EMERG - Emergency (0) - System unusable</option>";
        html += "<option value='1'>ALERT - Alert (1) - Immediate action required</option>";
        html += "<option value='2'>CRIT - Critical (2) - Critical conditions</option>";
        html += "<option value='3'>ERROR - Error (3) - Error conditions</option>";
        html += "<option value='4'>WARNING - Warning (4) - Warning conditions</option>";
        html += "<option value='5'>NOTICE - Notice (5) - Normal but significant</option>";
        html += "<option value='6' selected>INFO - Info (6) - Informational messages</option>";
        html += "<option value='7'>DEBUG - Debug (7) - Debug-level messages</option>";
        html += "</select><br>";
        html += "<button onclick='setDebugLevel()' class='button'>Set Transmitter Debug Level</button>";
        html += "<div id='debug-status'></div>";
        html += "</div>";
        
        // Information section
        html += "<div class='debug-control'>";
        html += "<h3>ℹ️ Debug System Information</h3>";
        html += "<p><strong>Current System:</strong> Battery Emulator Receiver</p>";
        html += "<p><strong>Debug Target:</strong> ESP-NOW Transmitter (Olimex ESP32-POE-ISO)</p>";
        html += "<p><strong>Communication:</strong> ESP-NOW wireless protocol</p>";
        html += "<p><strong>MQTT Broker:</strong> Subscribe to <code>espnow/transmitter/debug/#</code> to see all debug messages</p>";
        html += "<p><strong>Level Storage:</strong> Debug level is saved to NVS on transmitter and persists across reboots</p>";
        html += "</div>";
        
        // JavaScript for AJAX control
        html += "<script>";
        html += "function setDebugLevel() {";
        html += "  var level = document.getElementById('debugLevel').value;";
        html += "  var statusDiv = document.getElementById('debug-status');";
        html += "  var levelNames = ['EMERG', 'ALERT', 'CRIT', 'ERROR', 'WARNING', 'NOTICE', 'INFO', 'DEBUG'];";
        html += "  statusDiv.textContent = 'Sending debug level ' + level + ' (' + levelNames[level] + ') to transmitter...';";
        html += "  statusDiv.className = 'status-info';";
    html += "  fetch('/api/setDebugLevel?level=' + level)";
    html += "    .then(response => response.json())";
    html += "    .then(data => {";
    html += "      if (data.success) {";
    html += "        statusDiv.textContent = '✓ ' + data.message;";
    html += "        statusDiv.className = 'status-success';";
    html += "      } else {";
    html += "        statusDiv.textContent = '✗ ' + data.message;";
    html += "        statusDiv.className = 'status-error';";
    html += "      }";
    html += "      setTimeout(() => { statusDiv.style.display = 'none'; }, 5000);";
    html += "    })";
    html += "    .catch(error => {";
    html += "      statusDiv.textContent = '✗ Error: ' + error;";
    html += "      statusDiv.className = 'status-error';";
    html += "    });";
    html += "}";
    html += "</script>";
    
    // Navigation buttons
    html += generate_nav_buttons();
    
    return html;
}

// Debug page handler
static esp_err_t debug_page_handler(httpd_req_t *req) {
    String content = debug_page_processor();
    String page = generatePage("Debug Logging Control", content);
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
