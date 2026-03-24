#include "monitor2_page_script.h"
#include <Arduino.h>

String get_monitor2_page_styles() {
    return R"rawliteral(
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
}

String get_monitor2_page_script() {
    return R"rawliteral(
        let eventSource = null;
        let reconnectTimer = null;
        let lastUpdate = Date.now();
        let reconnectDelayMs = 1000;
        const reconnectDelayMaxMs = 30000;

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
                reconnectDelayMs = 1000;
            };

            eventSource.onmessage = function(event) {
                try {
                    const data = JSON.parse(event.data);
                    document.getElementById('mode').innerText = 'Mode: ' + (data.mode === 'simulated' ? 'Simulated Data' : 'Live ESP-NOW Data');
                    document.getElementById('soc').innerText = data.soc + ' %';
                    document.getElementById('power').innerText = data.power + ' W';
                    document.getElementById('voltage').innerText = (data.voltage_v || 0).toFixed(1) + ' V';
                    lastUpdate = Date.now();
                } catch (err) {
                    console.error('Failed to parse SSE data:', err);
                }
            };

            eventSource.onerror = function(err) {
                console.error('SSE error:', err);
                document.getElementById('connection').textContent = '❌ Disconnected (Reconnecting...)';
                document.getElementById('connection').className = 'connection-status disconnected';

                // Close and reconnect with exponential backoff
                eventSource.close();
                const waitMs = reconnectDelayMs;
                reconnectTimer = setTimeout(connectSSE, waitMs);
                reconnectDelayMs = Math.min(Math.floor(reconnectDelayMs * 1.5), reconnectDelayMaxMs);
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
}
