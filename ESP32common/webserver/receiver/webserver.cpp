#include "webserver.h"
#include <esp_netif.h>
#include <ESP.h>
#include <esp_now.h>
#include <espnow_common.h>

// ESP-IDF HTTP Server handle
httpd_handle_t server = NULL;

// External variables from main.cpp
extern volatile uint8_t g_received_soc;
extern volatile int32_t g_received_power;
extern volatile int g_test_soc;
extern volatile int32_t g_test_power;
extern bool test_mode_enabled;

// Transmitter MAC address (set when first data is received)
static uint8_t transmitter_mac[6] = {0};
static bool transmitter_mac_known = false;

// FreeRTOS Event Group for SSE data update notifications
static EventGroupHandle_t sse_event_group = NULL;
#define SSE_DATA_UPDATED_BIT (1 << 0)

// External settings processor function
extern String settings_processor(const String& var);

// ═══════════════════════════════════════════════════════════════════════
// HTML TEMPLATE HELPERS
// ═══════════════════════════════════════════════════════════════════════

// Common CSS styles for all pages
const char* COMMON_STYLES = R"rawliteral(
    html { font-family: Arial, Helvetica, sans-serif; display: inline-block; text-align: center; }
    body { max-width: 800px; margin: 0px auto; padding: 20px; background-color: #303841; color: white; }
    h1 { color: white; }
    h2 { color: #FFD700; margin-top: 5px; }
    h3 { color: white; margin-top: 20px; }
    .button {
        background-color: #505E67;
        border: none;
        color: white;
        padding: 12px 24px;
        text-decoration: none;
        font-size: 16px;
        margin: 10px;
        cursor: pointer;
        border-radius: 10px;
        display: inline-block;
    }
    .button:hover { background-color: #3A4A52; }
    .info-box {
        background-color: #3a4b54;
        padding: 20px;
        border-radius: 20px;
        margin: 15px 0;
        box-shadow: 0 2px 5px rgba(0, 0, 0, 0.2);
    }
    .info-box h3 {
        color: #fff;
        margin-top: 0;
        margin-bottom: 15px;
        padding-bottom: 8px;
        border-bottom: 1px solid #4d5f69;
    }
    .info-row {
        display: flex;
        justify-content: space-between;
        padding: 8px 0;
        border-bottom: 1px solid #505E67;
    }
    .info-row:last-child { border-bottom: none; }
    .info-label { font-weight: bold; color: #FFD700; }
    .info-value { color: white; }
    .settings-card {
        background-color: #3a4b54;
        padding: 15px 20px;
        margin-bottom: 20px;
        border-radius: 20px;
        box-shadow: 0 2px 5px rgba(0, 0, 0, 0.2);
        text-align: left;
    }
    .settings-card h3 {
        color: #fff;
        margin-top: 0;
        margin-bottom: 15px;
        padding-bottom: 8px;
        border-bottom: 1px solid #4d5f69;
    }
    .settings-row {
        display: grid;
        grid-template-columns: 1fr 1.5fr;
        gap: 10px;
        align-items: center;
        padding: 8px 0;
    }
    label { font-weight: bold; color: #FFD700; }
    input, select {
        max-width: 250px;
        padding: 8px;
        border-radius: 5px;
        border: none;
    }
    .ip-row {
        display: flex;
        align-items: center;
        gap: 6px;
    }
    .octet {
        width: 44px;
        text-align: right;
        margin: 0;
    }
    .dot {
        display: inline-block;
        width: 8px;
        text-align: center;
    }
    .note {
        background-color: #ff9800;
        color: #000;
        padding: 15px;
        border-radius: 10px;
        margin: 20px 0;
        font-weight: bold;
    }
    .settings-note {
        background-color: #ff9800;
        color: #000;
        padding: 15px;
        border-radius: 10px;
        margin: 20px 0;
        font-weight: bold;
    }
)rawliteral";

// Generate standard HTML page with common template
String generatePage(const String& title, const String& content, const String& extraStyles = "", const String& script = "") {
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='utf-8'>";
    html += "<title>" + title + "</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>" + String(COMMON_STYLES) + extraStyles + "</style>";
    if (script.length() > 0) {
        html += "<script>" + script + "</script>";
    }
    html += "</head><body>";
    html += content;
    html += "</body></html>";
    return html;
}

// ═══════════════════════════════════════════════════════════════════════
// HANDLERS
// ═══════════════════════════════════════════════════════════════════════

// Landing page handler - Settings configuration page
static esp_err_t root_handler(httpd_req_t *req) {
    String content = R"rawliteral(
    <h1>ESP-NOW Receiver Settings</h1>
    <a href='/settings' class='button'>System Info</a>
    <a href='/monitor' class='button'>Battery Monitor (Polling)</a>
    <a href='/monitor2' class='button'>Battery Monitor (SSE)</a>
    
    <div class='note'>
        📡 Settings will be retrieved from remote device via ESP-NOW
    </div>
    
    <div class='settings-card'>
        <h3>Network Configuration</h3>
        <div class='settings-row'>
            <label>Hostname:</label>
            <input type='text' value='%HOSTNAME%' disabled />
        </div>
        <div class='settings-row'>
            <label>SSID:</label>
            <input type='text' value='%SSID%' disabled />
        </div>
        <div class='settings-row'>
            <label>WiFi Channel:</label>
            <input type='text' value='%WIFICHANNEL%' disabled />
        </div>
        <div class='settings-row'>
            <label>WiFi AP Enabled:</label>
            <input type='checkbox' %WIFIAPENABLED% disabled />
        </div>
        <div class='settings-row'>
            <label>AP Name:</label>
            <input type='text' value='%APNAME%' disabled />
        </div>
        <div class='settings-row'>
            <label>Static IP:</label>
            <input type='checkbox' %STATICIP% disabled />
        </div>
        <div class='settings-row'>
            <label>Local IP:</label>
            <div class='ip-row'>
                <input class='octet' type='text' value='%LOCALIP1%' disabled />
                <span class='dot'>.</span>
                <input class='octet' type='text' value='%LOCALIP2%' disabled />
                <span class='dot'>.</span>
                <input class='octet' type='text' value='%LOCALIP3%' disabled />
                <span class='dot'>.</span>
                <input class='octet' type='text' value='%LOCALIP4%' disabled />
            </div>
        </div>
        <div class='settings-row'>
            <label>Gateway:</label>
            <div class='ip-row'>
                <input class='octet' type='text' value='%GATEWAY1%' disabled />
                <span class='dot'>.</span>
                <input class='octet' type='text' value='%GATEWAY2%' disabled />
                <span class='dot'>.</span>
                <input class='octet' type='text' value='%GATEWAY3%' disabled />
                <span class='dot'>.</span>
                <input class='octet' type='text' value='%GATEWAY4%' disabled />
            </div>
        </div>
        <div class='settings-row'>
            <label>Subnet:</label>
            <div class='ip-row'>
                <input class='octet' type='text' value='%SUBNET1%' disabled />
                <span class='dot'>.</span>
                <input class='octet' type='text' value='%SUBNET2%' disabled />
                <span class='dot'>.</span>
                <input class='octet' type='text' value='%SUBNET3%' disabled />
                <span class='dot'>.</span>
                <input class='octet' type='text' value='%SUBNET4%' disabled />
            </div>
        </div>
    </div>
    
    <div class='settings-card'>
        <h3>MQTT Configuration</h3>
        <div class='settings-row'>
            <label>MQTT Server:</label>
            <input type='text' value='%MQTTSERVER%' disabled />
        </div>
        <div class='settings-row'>
            <label>MQTT Port:</label>
            <input type='text' value='%MQTTPORT%' disabled />
        </div>
        <div class='settings-row'>
            <label>MQTT User:</label>
            <input type='text' value='%MQTTUSER%' disabled />
        </div>
        <div class='settings-row'>
            <label>MQTT Password:</label>
            <input type='password' value='%MQTTPASSWORD%' disabled />
        </div>
        <div class='settings-row'>
            <label>MQTT Topic:</label>
            <input type='text' value='%MQTTTOPIC%' disabled />
        </div>
        <div class='settings-row'>
            <label>MQTT Timeout:</label>
            <input type='text' value='%MQTTTIMEOUT% ms' disabled />
        </div>
        <div class='settings-row'>
            <label>MQTT Object ID Prefix:</label>
            <input type='text' value='%MQTTOBJIDPREFIX%' disabled />
        </div>
        <div class='settings-row'>
            <label>MQTT Device Name:</label>
            <input type='text' value='%MQTTDEVICENAME%' disabled />
        </div>
        <div class='settings-row'>
            <label>Home Assistant Device ID:</label>
            <input type='text' value='%HADEVICEID%' disabled />
        </div>
    </div>
    
    <div class='settings-card'>
        <h3>Battery Configuration</h3>
        <div class='settings-row'>
            <label>Battery Type:</label>
            <input type='text' value='ESP-NOW Remote' disabled />
        </div>
        <div class='settings-row'>
            <label>Double Battery:</label>
            <input type='checkbox' %DBLBTR% disabled />
        </div>
        <div class='settings-row'>
            <label>Battery Max Voltage:</label>
            <input type='text' value='%BATTPVMAX% V' disabled />
        </div>
        <div class='settings-row'>
            <label>Battery Min Voltage:</label>
            <input type='text' value='%BATTPVMIN% V' disabled />
        </div>
        <div class='settings-row'>
            <label>Cell Max Voltage:</label>
            <input type='text' value='%BATTCVMAX% mV' disabled />
        </div>
        <div class='settings-row'>
            <label>Cell Min Voltage:</label>
            <input type='text' value='%BATTCVMIN% mV' disabled />
        </div>
        <div class='settings-row'>
            <label>Use Estimated SOC:</label>
            <input type='checkbox' %SOCESTIMATED% disabled />
        </div>
    </div>
    
    <div class='settings-card'>
        <h3>Power Settings</h3>
        <div class='settings-row'>
            <label>Charge Power:</label>
            <input type='text' value='%CHGPOWER% W' disabled />
        </div>
        <div class='settings-row'>
            <label>Discharge Power:</label>
            <input type='text' value='%DCHGPOWER% W' disabled />
        </div>
        <div class='settings-row'>
            <label>Max Pre-charge Time:</label>
            <input type='text' value='%MAXPRETIME% ms' disabled />
        </div>
        <div class='settings-row'>
            <label>Pre-charge Duration:</label>
            <input type='text' value='%PRECHGMS% ms' disabled />
        </div>
    </div>
    
    <div class='settings-card'>
        <h3>Inverter Configuration</h3>
        <div class='settings-row'>
            <label>Inverter Cells:</label>
            <input type='text' value='%INVCELLS%' disabled />
        </div>
        <div class='settings-row'>
            <label>Inverter Modules:</label>
            <input type='text' value='%INVMODULES%' disabled />
        </div>
        <div class='settings-row'>
            <label>Cells Per Module:</label>
            <input type='text' value='%INVCELLSPER%' disabled />
        </div>
        <div class='settings-row'>
            <label>Voltage Level:</label>
            <input type='text' value='%INVVLEVEL% V' disabled />
        </div>
        <div class='settings-row'>
            <label>Capacity:</label>
            <input type='text' value='%INVCAPACITY% Ah' disabled />
        </div>
        <div class='settings-row'>
            <label>Battery Type:</label>
            <input type='text' value='%INVBTYPE%' disabled />
        </div>
    </div>
    
    <div class='settings-card'>
        <h3>CAN Configuration</h3>
        <div class='settings-row'>
            <label>CAN Frequency:</label>
            <input type='text' value='%CANFREQ% kHz' disabled />
        </div>
        <div class='settings-row'>
            <label>CAN FD Frequency:</label>
            <input type='text' value='%CANFDFREQ% MHz' disabled />
        </div>
        <div class='settings-row'>
            <label>Sofar Inverter ID:</label>
            <input type='text' value='%SOFAR_ID%' disabled />
        </div>
        <div class='settings-row'>
            <label>Pylon Send Interval:</label>
            <input type='text' value='%PYLONSEND% ms' disabled />
        </div>
    </div>
    
    <div class='settings-card'>
        <h3>Contactor Control</h3>
        <div class='settings-row'>
            <label>Contactor Control:</label>
            <input type='checkbox' %CNTCTRL% disabled />
        </div>
        <div class='settings-row'>
            <label>NC Contactor:</label>
            <input type='checkbox' %NCCONTACTOR% disabled />
        </div>
        <div class='settings-row'>
            <label>PWM Frequency:</label>
            <input type='text' value='%PWMFREQ% Hz' disabled />
        </div>
    </div>
    
    <p style='color: #666; font-size: 14px; margin-top: 30px;'>
        Settings are read-only. Configure values on the remote transmitter device.
    </p>
)rawliteral";

    // Process placeholders
    int start_pos = 0;
    while ((start_pos = content.indexOf("%", start_pos)) != -1) {
        int end_pos = content.indexOf("%", start_pos + 1);
        if (end_pos == -1) break;
        
        String placeholder = content.substring(start_pos + 1, end_pos);
        String value = settings_processor(placeholder);
        
        content = content.substring(0, start_pos) + value + content.substring(end_pos + 1);
        start_pos += value.length();
    }

    String html = generatePage("ESP-NOW Receiver - Settings", content);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html.c_str(), html.length());
    return ESP_OK;
}

// Battery monitor page handler - Live SOC and Power display
static esp_err_t monitor_handler(httpd_req_t *req) {
    String content = R"rawliteral(
    <h1>ESP-NOW Receiver</h1>
    <h2>Battery Monitor</h2>
    
    <a href='/' class='button'>Settings</a>
    <a href='/settings' class='button'>System Info</a>
    
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
                    document.getElementById('soc').innerText = data.soc + '%';
                    document.getElementById('power').innerText = data.power + 'W';
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

// Settings page handler - System information page
static esp_err_t settings_handler(httpd_req_t *req) {
    String content = R"rawliteral(
    <h1>ESP-NOW Receiver</h1>
    <h2>System Information</h2>
    
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
    
    <div class='info-box'>
        <h3>Navigation</h3>
        <a href='/' class='button'>Settings</a>
        <a href='/monitor' class='button'>Battery Monitor</a>
    </div>
)rawliteral";

    String script = R"rawliteral(
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

// API data endpoint handler - System information for landing page
static esp_err_t api_data_handler(httpd_req_t *req) {
    char json[768];
    
    // WiFi information
    String ssid = WiFi.SSID();
    String ip = WiFi.localIP().toString();
    String mac = WiFi.macAddress();
    int channel = WiFi.channel();
    
    // Chip information
    String chipModel = ESP.getChipModel();
    uint8_t chipRevision = ESP.getChipRevision();
    uint64_t efuseMac = ESP.getEfuseMac();
    
    // Format eFuse MAC as hex string (6 bytes)
    char efuseMacStr[18];
    snprintf(efuseMacStr, sizeof(efuseMacStr), 
             "%02X:%02X:%02X:%02X:%02X:%02X",
             (uint8_t)(efuseMac >> 40), (uint8_t)(efuseMac >> 32),
             (uint8_t)(efuseMac >> 24), (uint8_t)(efuseMac >> 16),
             (uint8_t)(efuseMac >> 8), (uint8_t)(efuseMac));
    
    snprintf(json, sizeof(json), 
             "{\"chipModel\":\"%s\",\"chipRevision\":%d,\"efuseMac\":\"%s\","
             "\"ssid\":\"%s\",\"ip\":\"%s\",\"mac\":\"%s\",\"channel\":%d}",
             chipModel.c_str(), chipRevision, efuseMacStr,
             ssid.c_str(), ip.c_str(), mac.c_str(), channel);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

// API monitor endpoint handler - Battery monitor data (SOC/Power)
static esp_err_t api_monitor_handler(httpd_req_t *req) {
    char json[256];
    const char* mode = test_mode_enabled ? "test" : "real";
    uint8_t soc = test_mode_enabled ? g_test_soc : g_received_soc;
    int32_t power = test_mode_enabled ? g_test_power : g_received_power;
    
    snprintf(json, sizeof(json), 
             "{\"mode\":\"%s\",\"soc\":%d,\"power\":%ld}",
             mode, soc, power);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

// SSE endpoint handler - Server-Sent Events for real-time battery monitor updates
// This handler blocks waiting for event notifications and only sends when data actually changes
static esp_err_t api_monitor_sse_handler(httpd_req_t *req) {
    // Set headers for SSE
    httpd_resp_set_type(req, "text/event-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Connection", "keep-alive");
    
    // Send REQUEST_DATA message to transmitter to start sending
    if (transmitter_mac_known) {
        request_data_t req_msg = { msg_request_data };
        esp_err_t result = esp_now_send(transmitter_mac, (const uint8_t*)&req_msg, sizeof(req_msg));
        if (result == ESP_OK) {
            Serial.println("[SSE] Sent REQUEST_DATA to transmitter");
        } else {
            Serial.printf("[SSE] Failed to send REQUEST_DATA: %s\n", esp_err_to_name(result));
        }
    } else {
        Serial.println("[SSE] Warning: Transmitter MAC unknown, cannot send REQUEST_DATA");
    }
    
    // Track last sent values to avoid duplicate sends
    uint8_t last_soc = 255;  // Invalid initial value to force first send
    int32_t last_power = INT32_MAX;
    bool last_mode = false;
    
    // Send initial data immediately
    char event_data[512];
    const char* mode = test_mode_enabled ? "test" : "real";
    uint8_t current_soc = test_mode_enabled ? g_test_soc : g_received_soc;
    int32_t current_power = test_mode_enabled ? g_test_power : g_received_power;
    
    snprintf(event_data, sizeof(event_data),
             "data: {\"mode\":\"%s\",\"soc\":%d,\"power\":%ld}\n\n",
             mode, current_soc, current_power);
    
    if (httpd_resp_send_chunk(req, event_data, strlen(event_data)) != ESP_OK) {
        return ESP_FAIL;
    }
    
    last_soc = current_soc;
    last_power = current_power;
    last_mode = test_mode_enabled;
    
    // Event-driven loop: Wait for notifications that data has changed
    // Runs for maximum 5 minutes to prevent indefinite connections
    TickType_t start_time = xTaskGetTickCount();
    const TickType_t max_duration = pdMS_TO_TICKS(300000);  // 5 minutes
    
    while ((xTaskGetTickCount() - start_time) < max_duration) {
        // Block waiting for data update notification OR 3-second timeout
        EventBits_t bits = xEventGroupWaitBits(
            sse_event_group,
            SSE_DATA_UPDATED_BIT,
            pdTRUE,  // Clear bit on exit
            pdFALSE, // Wait for any bit (only one bit defined)
            pdMS_TO_TICKS(3000)  // 3-second timeout
        );
        
        // Check if data was updated
        if (bits & SSE_DATA_UPDATED_BIT) {
            // Get current values
            current_soc = test_mode_enabled ? g_test_soc : g_received_soc;
            current_power = test_mode_enabled ? g_test_power : g_received_power;
            
            // Only send if values actually changed (avoid spurious notifications)
            if (current_soc != last_soc || current_power != last_power || test_mode_enabled != last_mode) {
                mode = test_mode_enabled ? "test" : "real";
                
                snprintf(event_data, sizeof(event_data),
                         "data: {\"mode\":\"%s\",\"soc\":%d,\"power\":%ld}\n\n",
                         mode, current_soc, current_power);
                
                if (httpd_resp_send_chunk(req, event_data, strlen(event_data)) != ESP_OK) {
                    break;  // Connection closed by client
                }
                
                last_soc = current_soc;
                last_power = current_power;
                last_soc = current_soc;
                last_power = current_power;
                last_mode = test_mode_enabled;
            }
        } else {
            // Timeout - send a comment (ignored by browser) to detect if connection is closed
            // This allows us to detect disconnects within 3 seconds without constant wake-ups
            const char* ping = ": ping\n\n";
            if (httpd_resp_send_chunk(req, ping, strlen(ping)) != ESP_OK) {
                break;  // Connection closed - detected on send attempt
            }
        }
    }
    
    // Send ABORT_DATA message to transmitter to stop sending
    if (transmitter_mac_known) {
        abort_data_t abort_msg = { msg_abort_data };
        esp_err_t result = esp_now_send(transmitter_mac, (const uint8_t*)&abort_msg, sizeof(abort_msg));
        if (result == ESP_OK) {
            Serial.println("[SSE] Sent ABORT_DATA to transmitter");
        } else {
            Serial.printf("[SSE] Failed to send ABORT_DATA: %s\n", esp_err_to_name(result));
        }
    }
    
    // Close the connection
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

// Battery monitor page handler v2 - Using Server-Sent Events for real-time updates
static esp_err_t monitor_handler_2(httpd_req_t *req) {
    String content = R"rawliteral(
    <h1>ESP-NOW Receiver</h1>
    <h2>Battery Monitor (SSE - Real-time)</h2>
    
    <a href='/' class='button'>Settings</a>
    <a href='/settings' class='button'>System Info</a>
    <a href='/monitor' class='button'>Polling Mode</a>
    
    <div class='mode-indicator' id='mode'>Mode: Loading...</div>
    <div class='connection-status' id='connection'>⚡ Connecting...</div>
    
    <div class='info-box'>
        <h3>Battery Status</h3>
        <div class='data-label'>State of Charge</div>
        <div class='data-value' id='soc'>--</div>
        
        <div class='data-label' style='margin-top: 30px;'>Power</div>
        <div class='data-value' id='power'>--</div>
    </div>
    
    <p class='update-note'>📡 Real-time updates via Server-Sent Events</p>
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
        .connection-status {
            color: #4CAF50;
            font-size: 14px;
            font-weight: bold;
            padding: 8px;
            background-color: #2d3741;
            border-radius: 8px;
            margin: 10px 0;
        }
        .connection-status.disconnected {
            color: #f44336;
        }
        .update-note {
            color: #888;
            font-size: 14px;
            margin-top: 20px;
        }
    )rawliteral";

    String script = R"rawliteral(
        let eventSource = null;
        let reconnectTimer = null;
        let lastUpdate = Date.now();
        
        function connectSSE() {
            // Close existing connection if any
            if (eventSource) {
                eventSource.close();
            }
            
            // Clear reconnect timer
            if (reconnectTimer) {
                clearTimeout(reconnectTimer);
                reconnectTimer = null;
            }
            
            // Create new EventSource connection
            eventSource = new EventSource('/api/monitor_sse');
            
            eventSource.onopen = function() {
                console.log('SSE connection opened');
                document.getElementById('connection').textContent = '⚡ Connected (Real-time)';
                document.getElementById('connection').className = 'connection-status';
            };
            
            eventSource.onmessage = function(event) {
                try {
                    const data = JSON.parse(event.data);
                    document.getElementById('mode').innerText = 'Mode: ' + (data.mode === 'test' ? 'Test Data' : 'Real ESP-NOW Data');
                    document.getElementById('soc').innerText = data.soc + '%';
                    document.getElementById('power').innerText = data.power + 'W';
                    lastUpdate = Date.now();
                } catch (err) {
                    console.error('Failed to parse SSE data:', err);
                }
            };
            
            eventSource.onerror = function(err) {
                console.error('SSE error:', err);
                document.getElementById('connection').textContent = '❌ Disconnected (Reconnecting...)';
                document.getElementById('connection').className = 'connection-status disconnected';
                
                // Close and reconnect after 3 seconds
                eventSource.close();
                reconnectTimer = setTimeout(connectSSE, 3000);
            };
        }
        
        // Monitor connection health - reconnect if no updates for 30 seconds
        setInterval(function() {
            if (Date.now() - lastUpdate > 30000) {
                console.log('No updates received for 30s, reconnecting...');
                connectSSE();
            }
        }, 5000);
        
        // Start SSE connection on page load
        window.onload = function() {
            connectSSE();
        };
        
        // Clean up on page unload
        window.onbeforeunload = function() {
            if (eventSource) {
                eventSource.close();
            }
            if (reconnectTimer) {
                clearTimeout(reconnectTimer);
            }
        };
    )rawliteral";

    String html = generatePage("ESP-NOW Receiver - Battery Monitor (SSE)", content, extraStyles, script);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html.c_str(), html.length());
    return ESP_OK;
}

// 404 Not Found handler
static esp_err_t notfound_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "404 Not Found");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "Endpoint not found");
    return ESP_OK;
}

// ═══════════════════════════════════════════════════════════════════════
// INITIALIZATION
// ═══════════════════════════════════════════════════════════════════════

void init_webserver() {
    Serial.println("[WEBSERVER] Initializing ESP-IDF http_server...");
    
    // Check if server already running
    if (server != NULL) {
        Serial.println("[WEBSERVER] Server already running, skipping");
        return;
    }
    
    // Create event group for SSE notifications (if not already created)
    if (sse_event_group == NULL) {
        sse_event_group = xEventGroupCreate();
        if (sse_event_group == NULL) {
            Serial.println("[WEBSERVER] ERROR: Failed to create SSE event group");
            return;
        }
        Serial.println("[WEBSERVER] SSE event group created");
    }
    
    // Ensure network stack initialized
    static bool netif_initialized = false;
    if (!netif_initialized) {
        esp_err_t ret = esp_netif_init();
        if (ret == ESP_OK || ret == ESP_ERR_INVALID_STATE) {
            Serial.println("[WEBSERVER] Network interface initialized");
            netif_initialized = true;
        } else {
            Serial.printf("[WEBSERVER] ERROR: esp_netif_init failed: %s\n", esp_err_to_name(ret));
            return;
        }
    }
    
    // Verify WiFi is connected
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WEBSERVER] ERROR: WiFi not connected");
        return;
    }
    
    // Configure HTTP server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.task_priority = tskIDLE_PRIORITY + 2;
    config.stack_size = 6144;
    config.max_open_sockets = 4;
    config.max_uri_handlers = 8;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.server_port = 80;
    config.lru_purge_enable = true;
    
    // Start HTTP server
    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        Serial.printf("[WEBSERVER] ERROR: Failed to start: %s\n", esp_err_to_name(ret));
        return;
    }
    
    Serial.printf("[WEBSERVER] Server started successfully\n");
    Serial.printf("[WEBSERVER] Accessible at: http://%s\n", WiFi.localIP().toString().c_str());
    
    // Register URI handlers
    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &root);
    
    httpd_uri_t monitor = {
        .uri = "/monitor",
        .method = HTTP_GET,
        .handler = monitor_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &monitor);
    
    httpd_uri_t settings = {
        .uri = "/settings",
        .method = HTTP_GET,
        .handler = settings_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &settings);
    
    httpd_uri_t api_data = {
        .uri = "/api/data",
        .method = HTTP_GET,
        .handler = api_data_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_data);
    
    httpd_uri_t api_monitor = {
        .uri = "/api/monitor",
        .method = HTTP_GET,
        .handler = api_monitor_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_monitor);
    
    httpd_uri_t api_monitor_sse = {
        .uri = "/api/monitor_sse",
        .method = HTTP_GET,
        .handler = api_monitor_sse_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &api_monitor_sse);
    
    httpd_uri_t monitor2 = {
        .uri = "/monitor2",
        .method = HTTP_GET,
        .handler = monitor_handler_2,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &monitor2);
    
    // 404 handler (must be last)
    httpd_uri_t notfound = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = notfound_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &notfound);
    
    Serial.println("[WEBSERVER] All handlers registered");
}

void stop_webserver() {
    if (server != NULL) {
        httpd_stop(server);
        server = NULL;
        Serial.println("[WEBSERVER] Server stopped");
    }
}

// Notify SSE clients that battery monitor data has been updated
// Call this from ESP-NOW worker task or test data generator when data changes
void notify_sse_data_updated() {
    if (sse_event_group != NULL) {
        xEventGroupSetBits(sse_event_group, SSE_DATA_UPDATED_BIT);
    }
}

// Register the transmitter MAC address for sending control messages
void register_transmitter_mac(const uint8_t* mac) {
    if (mac != NULL) {
        memcpy(transmitter_mac, mac, 6);
        transmitter_mac_known = true;
        Serial.printf("[WEBSERVER] Transmitter MAC registered: %02X:%02X:%02X:%02X:%02X:%02X\n",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        
        // Add transmitter as ESP-NOW peer so we can send control messages to it
        if (!esp_now_is_peer_exist(mac)) {
            esp_now_peer_info_t peer_info = {};
            memcpy(peer_info.peer_addr, mac, 6);
            peer_info.channel = 0;  // Use current channel
            peer_info.encrypt = false;
            peer_info.ifidx = WIFI_IF_STA;
            
            esp_err_t result = esp_now_add_peer(&peer_info);
            if (result == ESP_OK) {
                Serial.println("[WEBSERVER] Transmitter added as ESP-NOW peer");
            } else {
                Serial.printf("[WEBSERVER] ERROR: Failed to add transmitter as peer: %s\n", esp_err_to_name(result));
            }
        } else {
            Serial.println("[WEBSERVER] Transmitter already exists as peer");
        }
    }
}
