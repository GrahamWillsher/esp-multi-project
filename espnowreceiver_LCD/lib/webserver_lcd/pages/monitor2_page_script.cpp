#include "monitor2_page_script.h"
#include <Arduino.h>

const char* get_monitor2_page_styles() {
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

const char* get_monitor2_page_script() {
    return R"rawliteral(
        let pollTimer = null;
        let lastUpdate = Date.now();
        const MONITOR_POLL_BASE_MS = 5000;
        const MONITOR_POLL_MAX_MS = 30000;
        let monitorPollDelayMs = MONITOR_POLL_BASE_MS;

        function jitterDelay(ms) {
            const jitter = 0.10;
            const delta = Math.floor(ms * jitter);
            const min = Math.max(1000, ms - delta);
            const max = ms + delta;
            return Math.floor(Math.random() * (max - min + 1)) + min;
        }

        function retryAfterToMs(response) {
            const retryAfter = response.headers.get('Retry-After');
            const retrySeconds = Number(retryAfter);
            if (!Number.isFinite(retrySeconds) || retrySeconds <= 0) {
                return null;
            }
            return Math.max(MONITOR_POLL_BASE_MS, Math.floor(retrySeconds * 1000));
        }

        function scheduleNextPoll(delayMs) {
            if (pollTimer) {
                clearTimeout(pollTimer);
            }
            pollTimer = setTimeout(pollMonitorData, jitterDelay(delayMs));
        }

        async function pollMonitorData() {
            const connectionEl = document.getElementById('connection');

            try {
                const response = await fetch('/api/monitor', { cache: 'no-store' });

                if (!response.ok) {
                    const retryAfterMs = retryAfterToMs(response);
                    if (retryAfterMs !== null) {
                        monitorPollDelayMs = Math.max(MONITOR_POLL_BASE_MS, retryAfterMs);
                    } else {
                        monitorPollDelayMs = Math.min(monitorPollDelayMs * 2, MONITOR_POLL_MAX_MS);
                    }

                    connectionEl.textContent = `❌ Snapshot unavailable (${response.status})`;
                    connectionEl.className = 'connection-status disconnected';
                    scheduleNextPoll(monitorPollDelayMs);
                    return;
                }

                const data = await response.json();
                document.getElementById('mode').innerText = 'Mode: ' + (data.mode === 'simulated' ? 'Simulated Data' : 'Live Telemetry Data');
                document.getElementById('soc').innerText = data.soc + ' %';
                document.getElementById('power').innerText = data.power + ' W';
                document.getElementById('voltage').innerText = (data.voltage_v || 0).toFixed(1) + ' V';
                lastUpdate = Date.now();

                connectionEl.textContent = '⚡ Connected (5s snapshot polling)';
                connectionEl.className = 'connection-status';

                monitorPollDelayMs = MONITOR_POLL_BASE_MS;
                scheduleNextPoll(monitorPollDelayMs);
            } catch (err) {
                console.error('Monitor polling error:', err);
                monitorPollDelayMs = Math.min(monitorPollDelayMs * 2, MONITOR_POLL_MAX_MS);
                connectionEl.textContent = '❌ Disconnected (Retrying...)';
                connectionEl.className = 'connection-status disconnected';
                scheduleNextPoll(monitorPollDelayMs);
            }
        }

        // Monitor staleness for UX visibility.
        setInterval(function() {
            if (Date.now() - lastUpdate > 30000) {
                document.getElementById('connection').textContent = '⚠ Stale data (>30s)';
                document.getElementById('connection').className = 'connection-status disconnected';
            }
        }, 5000);

        // Start polling on page load.
        window.onload = function() {
            pollMonitorData();
        };

        // Clean up timer on page unload.
        window.onbeforeunload = function() {
            if (pollTimer) {
                clearTimeout(pollTimer);
            }
        };
    )rawliteral";
}
