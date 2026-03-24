#include "debug_page_script.h"
#include <Arduino.h>

String get_debug_page_styles() {
    return R"rawliteral(
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
}

String get_debug_page_script() {
    return R"rawliteral(
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
}
