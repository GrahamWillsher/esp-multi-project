#include "monitor2_page.h"
#include "../common/page_generator.h"
#include "../common/nav_buttons.h"

esp_err_t monitor2_handler(httpd_req_t *req) {
    String content = R"rawliteral(
    <h1>ESP-NOW Receiver</h1>
    <h2>Battery Monitor (SSE - Real-time)</h2>
    )rawliteral";
    
    // Add navigation buttons
    content += "    " + generate_nav_buttons("/monitor2");
    
    content += R"rawliteral(
    
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
                    document.getElementById('soc').innerText = data.soc + ' %';
                    document.getElementById('power').innerText = data.power + ' W';
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

esp_err_t register_monitor2_page(httpd_handle_t server) {
    httpd_uri_t uri = {
        .uri = "/transmitter/monitor2",
        .method = HTTP_GET,
        .handler = monitor2_handler,
        .user_ctx = NULL
    };
    return httpd_register_uri_handler(server, &uri);
}
