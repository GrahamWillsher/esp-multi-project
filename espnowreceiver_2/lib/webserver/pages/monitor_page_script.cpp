#include "monitor_page_script.h"
#include <Arduino.h>

String get_monitor_page_styles() {
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
        .update-note {
            color: #888;
            font-size: 14px;
            margin-top: 20px;
        }
    )rawliteral";
}

String get_monitor_page_script() {
    return R"rawliteral(
        function updateData() {
            fetch('/api/monitor')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('mode').innerText = 'Mode: ' + (data.mode === 'simulated' ? 'Simulated Data' : 'Live ESP-NOW Data');
                    document.getElementById('soc').innerText = data.soc + ' %';
                    document.getElementById('power').innerText = data.power + ' W';
                    document.getElementById('voltage').innerText = (data.voltage_v || 0).toFixed(1) + ' V';
                })
                .catch(err => console.error('Update failed:', err));
        }
        setInterval(updateData, 1000);
        window.onload = updateData;
    )rawliteral";
}
