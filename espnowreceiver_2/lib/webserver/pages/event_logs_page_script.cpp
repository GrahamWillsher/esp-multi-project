#include "event_logs_page_script.h"
#include <Arduino.h>

const char* get_event_logs_page_styles() {
    return R"rawliteral(
.event-log { margin-top: 12px; }
.event-meta { color: #aaa; font-size: 12px; }
.event-error { color: #ff6b35; }

.events-table-wrap {
    overflow-x: auto;
    border-radius: 8px;
    border: 1px solid rgba(255,255,255,0.12);
}

.events-table {
    width: 100%;
    border-collapse: collapse;
    font-size: 14px;
    background: rgba(0,0,0,0.22);
}

.events-table th,
.events-table td {
    padding: 10px 12px;
    border-bottom: 1px solid rgba(255,255,255,0.08);
    text-align: left;
    vertical-align: top;
}

.events-table th {
    background: rgba(255,255,255,0.08);
    color: #f1f1f1;
    font-weight: 600;
}

.events-table tr:nth-child(even) {
    background-color: #455a64;
}

.events-table tr:nth-child(odd) {
    background-color: #394b52;
}

.evt-level {
    font-weight: 700;
    border-radius: 10px;
    padding: 3px 8px;
    display: inline-block;
    font-size: 12px;
}

.evt-error { background: rgba(220,53,69,0.22); color: #ff8a96; }
.evt-warning { background: rgba(255,193,7,0.20); color: #ffe08a; }
.evt-info { background: rgba(23,162,184,0.20); color: #9be9f5; }
.evt-new-marker {
    color: #ffd54f;
    font-weight: 700;
    margin-left: 6px;
}

.event-actions { margin-top: 18px; display: flex; justify-content: center; }
.event-clear-btn {
    padding: 12px 24px;
    margin: 10px 5px;
    font-size: 16px;
    border-radius: 4px;
    border: none;
    background: #dc3545;
    color: #fff;
    cursor: pointer;
}
.event-clear-btn:hover { background: #b02a37; }
.event-clear-btn:disabled { opacity: 0.55; cursor: not-allowed; }
.event-clear-btn.armed { background: #ff9800; }

.status-success,
.status-error,
.status-info {
    margin-top: 12px;
    padding: 12px;
    border-radius: 4px;
    display: none;
}

.status-success { background-color: #28a745; color: white; }
.status-error { background-color: #dc3545; color: white; }
.status-info { background-color: #17a2b8; color: white; }
)rawliteral";
}

const char* get_event_logs_page_script() {
    return R"rawliteral(
const EVENT_LOGS_UI_POLICY = {
    clearArmTimeoutMs: 6000
};

let clearArmed = false;
let clearArmTimer = null;

function formatCountLabel(count, singular, plural) {
    return `${count} ${count === 1 ? singular : plural}`;
}

function formatTimestamp(evt, txHealth) {
    const rawTs = (evt.timestamp !== undefined && evt.timestamp !== null)
        ? Number(evt.timestamp)
        : ((evt.timestamp_ms !== undefined && evt.timestamp_ms !== null) ? Number(evt.timestamp_ms) : NaN);

    if (!Number.isFinite(rawTs)) {
        return 'N/A';
    }

    if (txHealth && Number.isFinite(txHealth.unixTimeMs) && Number.isFinite(txHealth.uptimeMs) && txHealth.uptimeMs >= rawTs) {
        // Calculate age of event in milliseconds
        const ageMs = txHealth.uptimeMs - rawTs;
        // Calculate UTC epoch of when event occurred
        const eventUnixSecondsSinceEpoch = (txHealth.unixTimeMs / 1000) - (ageMs / 1000);
        // Apply transmitter's UTC offset (same mechanics as dashboard resolveTransmitterTimeDisplay)
        const offsetMin = txHealth.utcOffsetMin || 0;
        const localMs = (eventUnixSecondsSinceEpoch + offsetMin * 60) * 1000;
        // Format using UTC getters (offset is already baked in)
        const d = new Date(localMs);
        const pad = (n) => String(n).padStart(2, '0');
        const day  = pad(d.getUTCDate());
        const mon  = pad(d.getUTCMonth() + 1);
        const year = d.getUTCFullYear();
        const hh   = pad(d.getUTCHours());
        const mm   = pad(d.getUTCMinutes());
        const ss   = pad(d.getUTCSeconds());
        return `${day}/${mon}/${year} ${hh}:${mm}:${ss}`;
    }

    if (!txHealth || !Number.isFinite(txHealth.uptimeMs) || txHealth.uptimeMs < rawTs) {
        return 'N/A';
    }

    const ageMs = txHealth.uptimeMs - rawTs;
    const approxWallClock = new Date(Date.now() - ageMs);
    return approxWallClock.toLocaleString();
}

function normalizeLevel(evt) {
    if (typeof evt.level === 'number') {
        if (evt.level === 3) return { label: 'ERROR', cls: 'evt-error' };
        if (evt.level === 4) return { label: 'WARNING', cls: 'evt-warning' };
        return { label: 'INFO', cls: 'evt-info' };
    }

    const levelText = String(evt.level || '').toUpperCase();
    if (levelText.includes('ERROR')) return { label: 'ERROR', cls: 'evt-error' };
    if (levelText.includes('WARN')) return { label: 'WARNING', cls: 'evt-warning' };
    return { label: levelText || 'INFO', cls: 'evt-info' };
}

function armClearButton(btn) {
    clearArmed = true;
    btn.classList.add('armed');
    btn.textContent = 'Confirm Clear Event Logs';

    if (clearArmTimer) {
        clearTimeout(clearArmTimer);
    }

    clearArmTimer = setTimeout(() => {
        clearArmed = false;
        btn.classList.remove('armed');
        btn.textContent = 'Clear Event Logs';
    }, EVENT_LOGS_UI_POLICY.clearArmTimeoutMs);
}

function disarmClearButton(btn) {
    clearArmed = false;
    if (clearArmTimer) {
        clearTimeout(clearArmTimer);
        clearArmTimer = null;
    }
    btn.classList.remove('armed');
    btn.textContent = 'Clear Event Logs';
}

async function getTransmitterUptimeMs() {
    try {
        const res = await fetch('/api/transmitter_health');
        const data = await res.json();
        if (data && data.success) {
            return {
                uptimeMs: data.uptime_ms !== undefined ? Number(data.uptime_ms) : NaN,
                unixTimeMs: data.unix_time !== undefined ? Number(data.unix_time) * 1000 : NaN,
                utcOffsetMin: data.utc_offset_min !== undefined ? Number(data.utc_offset_min) : 0
            };
        }
    } catch (e) {
        // Ignore and fall back below
    }
    return {
        uptimeMs: NaN,
        unixTimeMs: NaN,
        utcOffsetMin: 0
    };
}

async function loadEvents() {
    const status = document.getElementById('eventStatus');
    const list = document.getElementById('eventList');
    status.textContent = 'Loading...';
    list.innerHTML = '';
    try {
        const txHealth = await getTransmitterUptimeMs();
        // Force authoritative transmitter snapshot for /events so this page
        // reflects transmitter-side logs (not receiver cache-only view).
        const res = await fetch('/api/get_event_logs?limit=500&source=transmitter');
        const data = await res.json();
        if (!data.success) {
            status.textContent = data.error || 'Event logs unavailable';
            status.className = 'event-meta event-error';
            return;
        }
        const source = data.source ? (' | source: ' + data.source) : '';
        const eventCount = Number(data.event_count || 0);
        status.textContent = formatCountLabel(eventCount, 'event', 'events') + source;
        status.className = 'event-meta';
        if (data.events && data.events.length) {
            const wrap = document.createElement('div');
            wrap.className = 'events-table-wrap';
            const table = document.createElement('table');
            table.className = 'events-table';
            table.innerHTML = '<thead><tr>' +
                              '<th style="width:17%;">Event Type</th>' +
                              '<th style="width:12%;">Severity</th>' +
                              '<th style="width:18%;">Last Event</th>' +
                              '<th style="width:8%;">Count</th>' +
                              '<th style="width:10%;">Data</th>' +
                              '<th>Message</th>' +
                              '</tr></thead>';

            const tbody = document.createElement('tbody');
            data.events.forEach(evt => {
                const row = document.createElement('tr');
                const msg = (evt.message !== undefined && evt.message !== null && evt.message !== '') ? evt.message : 'Event';
                const dataVal = (evt.data !== undefined && evt.data !== null) ? evt.data : '-';
                const ts = formatTimestamp(evt, txHealth);
                const level = normalizeLevel(evt);
                const eventType = (evt.event !== undefined && evt.event !== null && evt.event !== '')
                    ? evt.event
                    : ((evt.type !== undefined && evt.type !== null && evt.type !== '')
                        ? evt.type
                        : ((evt.event_type !== undefined && evt.event_type !== null && evt.event_type !== '')
                            ? evt.event_type
                            : '-'));
                const count = (evt.count !== undefined && evt.count !== null) ? evt.count : 1;
                const isNew = !!evt.is_new;
                const eventTypeDisplay = isNew
                    ? (eventType + '<span class="evt-new-marker">*</span>')
                    : eventType;

                row.innerHTML = '<td>' + eventTypeDisplay + '</td>' +
                                '<td><span class="evt-level ' + level.cls + '">' + level.label + '</span></td>' +
                                '<td>' + ts + '</td>' +
                                '<td>' + count + '</td>' +
                                '<td>' + dataVal + '</td>' +
                                '<td>' + msg + '</td>';
                tbody.appendChild(row);
            });

            table.appendChild(tbody);
            wrap.appendChild(table);
            list.appendChild(wrap);
        } else if (eventCount === 0) {
            status.textContent = formatCountLabel(0, 'event', 'events');
        }
    } catch (e) {
        status.textContent = 'Failed to load events';
        status.className = 'event-meta event-error';
    }
}

async function clearEventLogs() {
    const clearBtn = document.getElementById('clearEventLogsBtn');
    const status = document.getElementById('eventStatus');

    if (!clearArmed) {
        armClearButton(clearBtn);
        return;
    }

    disarmClearButton(clearBtn);
    clearBtn.disabled = true;

    let navigating = false;
    try {
        const res = await fetch('/api/clear_event_logs', { method: 'POST' });
        const data = await res.json();
        if (!data.success) {
            status.textContent = data.error || 'Failed to clear event logs';
            status.className = 'event-meta event-error';
            return;
        }

        navigating = true;
        status.textContent = 'Events cleared \u2014 returning to dashboard\u2026';
        status.className = 'event-meta';
        setTimeout(() => { window.location.href = '/'; }, 3000);
    } catch (e) {
        status.textContent = 'Failed to clear event logs';
        status.className = 'event-meta event-error';
    } finally {
        if (!navigating) {
            clearBtn.disabled = false;
            disarmClearButton(clearBtn);
        }
    }
}

window.addEventListener('load', () => {
    fetch('/api/event_logs/subscribe', {method: 'POST'});
    loadEvents();
});

window.addEventListener('beforeunload', () => {
    if (navigator.sendBeacon) {
        const blob = new Blob(['{}'], {type: 'application/json'});
        navigator.sendBeacon('/api/event_logs/unsubscribe', blob);
    } else {
        fetch('/api/event_logs/unsubscribe', {method: 'POST', keepalive: true});
    }
});
)rawliteral";
}
