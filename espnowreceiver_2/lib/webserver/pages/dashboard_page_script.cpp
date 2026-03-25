#include "dashboard_page_script.h"

const char* get_dashboard_page_script() {
    return R"rawliteral(
        // Track last update time for "X seconds ago" display
        let lastUpdateTime = Date.now();
        let lastSeenUptimeMs = 0;  // Track previous uptime value to detect actual updates
        let hasHealthSample = false;
        
        // Time formatting functions
        function formatTimeWithTimezone(unixTime, timeZone = 'GMT') {
            if (!unixTime || unixTime === 0) return '-- -- ----';
            try {
                const date = new Date(unixTime * 1000);
                const formatter = new Intl.DateTimeFormat('en-GB', {
                    year: 'numeric',
                    month: '2-digit',
                    day: '2-digit',
                    hour: '2-digit',
                    minute: '2-digit',
                    second: '2-digit',
                    timeZone: 'UTC'
                });
                const parts = formatter.formatToParts(date);
                const values = {};
                parts.forEach(part => {
                    if (part.type !== 'literal') {
                        values[part.type] = part.value;
                    }
                });
                return `${values.day}-${values.month}-${values.year} ${values.hour}:${values.minute}:${values.second} ${timeZone}`;
            } catch (e) {
                return '-- -- ----';
            }
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

        function applyFormattedDeviceNames() {
            const txEl = document.getElementById('txDeviceName');
            const rxEl = document.getElementById('rxDeviceName');
            if (txEl) txEl.textContent = formatEnvName(txEl.textContent);
            if (rxEl) rxEl.textContent = formatEnvName(rxEl.textContent);
        }

        
        // Update transmitter data every 2 seconds (match transmission rate)
        setInterval(async function() {
            try {
                const response = await fetch('/api/dashboard_data');
                const data = await response.json();
                
                // Update transmitter status (dynamic - can change)
                if (data.transmitter) {
                    const tx = data.transmitter;
                    const statusEl = document.getElementById('txStatus');
                    const txIPEl = document.getElementById('txIP');
                    const txIPModeEl = document.getElementById('txIPMode');
                    const txVersionEl = document.getElementById('txVersion');
                    const txMACEl = document.getElementById('txMAC');
                    
                    if (tx.connected) {
                        statusEl.textContent = 'Connected';
                        statusEl.style.color = '#4CAF50';
                        
                        // Update IP and mode (can change if transmitter reconfigures)
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
                    } else {
                        statusEl.textContent = 'Disconnected';
                        statusEl.style.color = '#ff6b35';
                    }
                }
                
                // Fetch transmitter time data
                try {
                    const timeResponse = await fetch('/api/transmitter_health');
                    const timeData = await timeResponse.json();
                    
                    if (timeData && timeData.uptime_ms !== undefined) {
                        // Update time display
                        document.getElementById('txTime').textContent = formatTimeWithTimezone(timeData.unix_time, 'GMT');
                        document.getElementById('txUptime').textContent = formatUptime(timeData.uptime_ms);
                        
                        // Update time source
                        const sourceEl = document.getElementById('txTimeSource');
                        sourceEl.textContent = getTimeSourceLabel(timeData.time_source);
                        sourceEl.style.color = getTimeSourceColor(timeData.time_source);
                        
                        // Only update "last update" time if uptime_ms has actually changed (new data from transmitter)
                        if (timeData.uptime_ms !== lastSeenUptimeMs) {
                            lastSeenUptimeMs = timeData.uptime_ms;
                            lastUpdateTime = Date.now();
                            hasHealthSample = true;
                            updateTimerDisplay();
                        }
                    }
                } catch (e) {
                    console.debug('Time data not yet available:', e);
                }
            } catch (e) {
                console.error('Failed to update dashboard:', e);
            }
        }, 2000);
        
        // Load event logs from transmitter
        async function loadEventLogs() {
            const statusEl = document.getElementById('eventLogStatus');
            const cardEl = document.getElementById('eventLogCard');
            const linkEl = document.getElementById('eventLogLink');
            statusEl.textContent = 'Loading...';
            statusEl.style.color = '#FFD700';
            
            try {
                const response = await fetch('/api/get_event_logs?limit=100');
                const data = await response.json();
                
                if (data.success && data.event_count !== undefined && data.event_count > 0) {
                    // Count event types if events array exists
                    let errorCount = 0;
                    let warningCount = 0;
                    let infoCount = 0;
                    
                    if (data.events && Array.isArray(data.events)) {
                        data.events.forEach(event => {
                            if (event.level === 3) {  // ERROR
                                errorCount++;
                            } else if (event.level === 4) {  // WARNING
                                warningCount++;
                            } else if (event.level === 6) {  // INFO
                                infoCount++;
                            }
                        });
                    }
                    
                    // Update status display - enable card
                    let statusText = data.event_count + ' events';
                    if (errorCount > 0) {
                        statusText += ` | ${errorCount} errors`;
                    }
                    if (warningCount > 0) {
                        statusText += ` | ${warningCount} warnings`;
                    }
                    
                    statusEl.textContent = statusText;
                    statusEl.style.color = '#4CAF50';
                    cardEl.classList.remove('disabled');
                    linkEl.style.pointerEvents = 'auto';
                    cardEl.style.opacity = '1';
                    
                    // Log event summary
                    console.log('Event Summary:', {
                        total: data.event_count,
                        errors: errorCount,
                        warnings: warningCount,
                        info: infoCount
                    });
                } else {
                    // No data available - disable card and show appropriate message
                    cardEl.classList.add('disabled');
                    linkEl.style.pointerEvents = 'none';
                    cardEl.style.opacity = '0.5';
                    cardEl.style.cursor = 'not-allowed';
                    
                    if (data.success && data.event_count === 0) {
                        statusEl.textContent = 'No events to display';
                        statusEl.style.color = '#888';
                    } else if (data.success === false && data.error && data.error.includes('not connected')) {
                        statusEl.textContent = 'Transmitter offline';
                        statusEl.style.color = '#FFD700';
                    } else {
                        statusEl.textContent = 'Not available';
                        statusEl.style.color = '#888';
                    }
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
        
        // Load event logs on page load
        window.addEventListener('load', function() {
            applyFormattedDeviceNames();
            loadEventLogs();
        });

        // Keep "Updated" counter moving every second between transmitter samples.
        setInterval(updateTimerDisplay, 1000);
        

        setTimeout(async function() {
            try {
                const timeResponse = await fetch('/api/transmitter_health');
                const timeData = await timeResponse.json();
                
                if (timeData && timeData.uptime_ms !== undefined) {
                    document.getElementById('txTime').textContent = formatTimeWithTimezone(timeData.unix_time, 'GMT');
                    document.getElementById('txUptime').textContent = formatUptime(timeData.uptime_ms);
                    const sourceEl = document.getElementById('txTimeSource');
                    sourceEl.textContent = getTimeSourceLabel(timeData.time_source);
                    sourceEl.style.color = getTimeSourceColor(timeData.time_source);
                    lastSeenUptimeMs = timeData.uptime_ms;
                    lastUpdateTime = Date.now();
                    hasHealthSample = true;
                    updateTimerDisplay();
                }
            } catch (e) {
                console.debug('Initial time data fetch failed:', e);
            }
        }, 500);
    )rawliteral";
}