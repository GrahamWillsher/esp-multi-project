#include "dashboard_page_script.h"

const char* get_dashboard_page_script() {
    return R"rawliteral(
        // Track last update time for "X seconds ago" display
        let lastUpdateTime = Date.now();
        let lastSeenUptimeMs = 0;  // Track previous uptime value to detect actual updates
        let hasHealthSample = false;
        
        // Transmitter time is the single source of truth.
        // Converts the transmitter's UTC epoch + UTC offset (both from ESP-NOW heartbeat)
        // to a local wall-clock time string. Returns '---' if the clock is not yet synced.
        function resolveTransmitterTimeDisplay(timeData) {
            if (!timeData || !timeData.time_source || timeData.time_source === 0) return '---';
            const unix = timeData.unix_time;
            if (!unix) return '---';
            const offsetMin = timeData.utc_offset_min || 0;
            const localMs = (unix + offsetMin * 60) * 1000;
            const d = new Date(localMs);
            const day  = String(d.getUTCDate()).padStart(2, '0');
            const mon  = String(d.getUTCMonth() + 1).padStart(2, '0');
            const year = d.getUTCFullYear();
            const hh   = String(d.getUTCHours()).padStart(2, '0');
            const mm   = String(d.getUTCMinutes()).padStart(2, '0');
            const ss   = String(d.getUTCSeconds()).padStart(2, '0');
            return `${day}/${mon}/${year} ${hh}:${mm}:${ss}`;
        }
        
        function formatUptime(ms) {
            if (!ms || ms === 0) return '-- -- ----';
            const totalSeconds = Math.floor(ms / 1000);
            const days = Math.floor(totalSeconds / 86400);
            const hours = Math.floor((totalSeconds % 86400) / 3600);
            const minutes = Math.floor((totalSeconds % 3600) / 60);
            const seconds = totalSeconds % 60;
            
            if (days > 0) {
                return `${days}d ${String(hours).padStart(2, '0')}:${String(minutes).padStart(2, '0')}:${String(seconds).padStart(2, '0')}`;
            } else {
                return `${String(hours).padStart(2, '0')}:${String(minutes).padStart(2, '0')}:${String(seconds).padStart(2, '0')}`;
            }
        }
        
        function formatLastUpdate(ms) {
            if (!Number.isFinite(ms) || ms < 0) {
                return 'Now';
            }
            const totalSeconds = Math.floor(ms / 1000);
            const days = Math.floor(totalSeconds / 86400);
            const hours = Math.floor((totalSeconds % 86400) / 3600);
            const minutes = Math.floor((totalSeconds % 3600) / 60);
            const seconds = totalSeconds % 60;
            
            if (days > 0) {
                return `${days}d, ${String(hours).padStart(2, '0')}H:${String(minutes).padStart(2, '0')}M:${String(seconds).padStart(2, '0')}S ago`;
            } else if (hours > 0) {
                return `${String(hours).padStart(2, '0')}H:${String(minutes).padStart(2, '0')}M:${String(seconds).padStart(2, '0')}S ago`;
            } else if (minutes > 0) {
                return `${minutes}M:${String(seconds).padStart(2, '0')}S ago`;
            } else {
                return `${seconds}s ago`;
            }
        }
        
        function updateTimerDisplay() {
            if (!hasHealthSample) {
                return;
            }

            const msSinceUpdate = Date.now() - lastUpdateTime;
            const secondsSinceUpdate = Math.floor(msSinceUpdate / 1000);
            const lastUpdateStr = formatLastUpdate(msSinceUpdate);
            
            const lastUpdateEl = document.getElementById('txLastUpdate');
            lastUpdateEl.textContent = lastUpdateStr;
            
            // Change color based on staleness
            if (secondsSinceUpdate < 2) {
                lastUpdateEl.style.color = '#4CAF50';  // Green - fresh
            } else if (secondsSinceUpdate < 5) {
                lastUpdateEl.style.color = '#FFD700';  // Yellow - slightly stale
            } else if (secondsSinceUpdate < 10) {
                lastUpdateEl.style.color = '#FF9800';  // Orange - getting stale
            } else {
                lastUpdateEl.style.color = '#ff6b35';  // Red - very stale
            }
        }
        
        function getTimeSourceLabel(source) {
            switch(source) {
                case 0: return 'Unsynced';
                case 1: return 'NTP';
                case 2: return 'Manual';
                case 3: return 'GPS';
                default: return 'Unknown';
            }
        }
        
        function getTimeSourceColor(source) {
            switch(source) {
                case 0: return '#ff6b35';  // Red - unsynced
                case 1: return '#4CAF50';  // Green - NTP
                case 2: return '#FF9800';  // Orange - Manual
                case 3: return '#2196F3';  // Blue - GPS
                default: return '#999';
            }
        }

        function formatEnvName(env) {
            if (!env) return 'Unknown Device';
            const spaced = String(env).replace(/[-_]+/g, ' ').trim();
            return spaced.replace(/\b\w/g, c => c.toUpperCase());
        }

        function formatCountLabel(count, singular, plural) {
            return `${count} ${count === 1 ? singular : plural}`;
        }

        function sleep(ms) {
            return new Promise(resolve => setTimeout(resolve, ms));
        }

        function formatTemperature(value) {
            const numeric = Number(value);
            if (!Number.isFinite(numeric)) {
                return '🌡️ --.-°C';
            }
            return `🌡️ ${numeric.toFixed(1)}°C`;
        }

        function applyFormattedDeviceNames() {
            const txEl = document.getElementById('txDeviceName');
            const rxEl = document.getElementById('rxDeviceName');
            if (txEl) txEl.textContent = formatEnvName(txEl.textContent);
            if (rxEl) rxEl.textContent = formatEnvName(rxEl.textContent);
        }

        
        // Dashboard polling uses a single consolidated endpoint every 1 second.
        setInterval(async function() {
            try {
                const response = await fetch('/api/dashboard_data');
                const data = await response.json();
                
                // Update transmitter status (dynamic - can change)
                if (data.transmitter) {
                    const tx = data.transmitter;
                    const statusEl = document.getElementById('txStatus');
                    const statusDotEl = document.getElementById('txStatusDot');
                    const txIPEl = document.getElementById('txIP');
                    const txIPModeEl = document.getElementById('txIPMode');
                    const txVersionEl = document.getElementById('txVersion');
                    const txMACEl = document.getElementById('txMAC');
                    const txTemperatureEl = document.getElementById('txTemperature');
                    const ethernetConnected = !!tx.ethernet_connected;

                    if (txTemperatureEl) {
                        txTemperatureEl.textContent = formatTemperature(tx.temperature_c);
                        txTemperatureEl.style.color = Number.isFinite(Number(tx.temperature_c)) ? '#fff' : '#888';
                    }

                    // Update cached transmitter identity/network details independently of link state
                    if (tx.ip && tx.ip !== 'Unknown' && tx.ip !== '0.0.0.0') {
                        txIPEl.textContent = tx.ip;
                        txIPModeEl.textContent = tx.is_static ? ' (S)' : ' (D)';
                    } else if (tx.ip === '0.0.0.0') {
                        txIPEl.textContent = 'Not available';
                        txIPModeEl.textContent = '';
                    }
                    if (tx.firmware && tx.firmware !== 'Unknown') {
                        txVersionEl.textContent = tx.firmware;
                    }
                    if (tx.mac && tx.mac !== 'Unknown') {
                        txMACEl.textContent = tx.mac;
                    }
                    
                    if (ethernetConnected) {
                        statusEl.textContent = 'Connected';
                        statusEl.style.color = '#4CAF50';
                        if (statusDotEl) {
                            statusDotEl.style.background = '#4CAF50';
                        }
                    } else {
                        statusEl.textContent = 'Disconnected';
                        statusEl.style.color = '#ff6b35';
                        if (statusDotEl) {
                            statusDotEl.style.background = '#ff6b35';
                        }
                    }
                }

                if (data.receiver) {
                    const rx = data.receiver;
                    const rxIPModeEl = document.getElementById('rxIPMode');
                    const rxTemperatureEl = document.getElementById('rxTemperature');
                    if (rxIPModeEl && typeof rx.is_static === 'boolean') {
                        rxIPModeEl.textContent = rx.is_static ? ' (S)' : ' (D)';
                    }
                    if (rxTemperatureEl) {
                        rxTemperatureEl.textContent = formatTemperature(rx.temperature_c);
                        rxTemperatureEl.style.color = Number.isFinite(Number(rx.temperature_c)) ? '#fff' : '#888';
                    }
                }
                
                // Health fields are included in /api/dashboard_data response.
                const healthData = data.transmitter;
                if (healthData && healthData.uptime_ms !== undefined) {
                    document.getElementById('txTime').textContent =
                        resolveTransmitterTimeDisplay(healthData);
                    document.getElementById('txUptime').textContent = formatUptime(healthData.uptime_ms);

                    const sourceEl = document.getElementById('txTimeSource');
                    sourceEl.textContent = getTimeSourceLabel(healthData.time_source);
                    sourceEl.style.color = getTimeSourceColor(healthData.time_source);

                    const geoEl = document.getElementById('txGeoStatus');
                    if (geoEl) {
                        if (healthData.geolocation_valid) {
                            geoEl.textContent = '🌍 Geolocation confirmed';
                            geoEl.style.color = '#4CAF50';
                            geoEl.title = 'Timezone has been confirmed by geolocation service.';
                        } else {
                            geoEl.textContent = '⚠ Default (UTC) - geolocation pending';
                            geoEl.style.color = '#FF9800';
                            geoEl.title = 'Transmitter is currently using fallback timezone. DST transitions are unavailable until geolocation succeeds.';
                        }
                    }

                    if (healthData.uptime_ms !== lastSeenUptimeMs) {
                        lastSeenUptimeMs = healthData.uptime_ms;
                        lastUpdateTime = Date.now();
                        hasHealthSample = true;
                        updateTimerDisplay();
                    }
                }
            } catch (e) {
                console.error('Failed to update dashboard:', e);
            }
        }, 1000);
        
        // Load event logs from transmitter
        async function loadEventLogs() {
            const statusEl = document.getElementById('eventLogStatus');
            const cardEl = document.getElementById('eventLogCard');
            const linkEl = document.getElementById('eventLogLink');
            statusEl.textContent = 'Loading...';
            statusEl.style.color = '#FFD700';
            
            try {
                // Dashboard card uses cached ESP-NOW summary counters pushed from transmitter.
                let summary = await fetch('/api/get_event_log_summary').then(r => r.json());

                if (summary && summary.success) {
                    const total = Number(summary.total_historical || 0);
                    const errors = Number(summary.error_historical || 0);
                    const newTotal = Number(summary.new_since_last_report_total || 0);

                    let statusText = formatCountLabel(total, 'event', 'events');
                    if (errors > 0) {
                        statusText += ` | ${formatCountLabel(errors, 'error', 'errors')}`;
                    }
                    if (newTotal > 0) {
                        statusText += ` | ${formatCountLabel(newTotal, 'new', 'new')}`;
                    }

                    statusEl.textContent = statusText;
                    statusEl.style.color = (errors > 0)
                        ? '#ff6b35'
                        : (total > 0 ? '#4CAF50' : '#888');
                    cardEl.classList.remove('disabled');
                    linkEl.style.pointerEvents = 'auto';
                    cardEl.style.opacity = '1';
                } else {
                    cardEl.classList.add('disabled');
                    linkEl.style.pointerEvents = 'none';
                    cardEl.style.opacity = '0.5';
                    cardEl.style.cursor = 'not-allowed';
                    statusEl.textContent = 'Waiting for summary';
                    statusEl.style.color = '#888';
                }
            } catch (e) {
                // Connection error - disable card
                cardEl.classList.add('disabled');
                linkEl.style.pointerEvents = 'none';
                cardEl.style.opacity = '0.5';
                cardEl.style.cursor = 'not-allowed';
                statusEl.textContent = 'Connection error';
                statusEl.style.color = '#ff6b35';
                console.error('Event logs fetch failed:', e);
            }
        }
        
        // Load event logs once on page load for card status.
        // Detailed logs are fetched on demand when the Event Logs page is opened.
        window.addEventListener('load', function() {
            applyFormattedDeviceNames();
            loadEventLogs();
        });

        // Refresh event log summary when tab becomes active again.
        document.addEventListener('visibilitychange', function() {
            if (!document.hidden) {
                loadEventLogs();
            }
        });

        // Keep "Updated" counter moving every second between transmitter samples.
        setInterval(updateTimerDisplay, 1000);
        
    )rawliteral";
}