#include "event_logs_page_script.h"
#include <Arduino.h>

const char* get_event_logs_page_styles() {
    return R"rawliteral(
.event-log { margin-top: 12px; }
.event-meta { color: #aaa; font-size: 12px; }
.event-error { color: #ff6b35; }

.events-table-wrap {
    max-height: 570px;
    overflow-x: auto;
    overflow-y: auto;
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
    position: sticky;
    top: 0;
    z-index: 1;
    background: #2e3d45;
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
const CLEAR_COUNTDOWN_SECONDS = 5;
const SNAPSHOT_WAIT_TIMEOUT_MS = 5000;
const SNAPSHOT_WAIT_POLL_MS = 500;
const SNAPSHOT_RETRY_ATTEMPTS = 5;
const SNAPSHOT_RETRY_DELAY_MS = 1000;
const KEEPALIVE_INTERVAL_MS = 45000;
const CHUNK_SIZE = 25;

let snapshotSubscriptionClosed = false;
let clearCountdownTimer = null;
let clearCountdownRemaining = 0;
let keepaliveTimer = null;

function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

function formatCountLabel(count, singular, plural) {
    return `${count} ${count === 1 ? singular : plural}`;
}

function parseDdMmYyyyHhMmSs(value) {
    if (typeof value !== 'string') {
        return NaN;
    }

    const m = value.trim().match(/^(\d{2})\/(\d{2})\/(\d{4})\s+(\d{2}):(\d{2}):(\d{2})$/);
    if (!m) {
        return NaN;
    }

    const day = Number(m[1]);
    const mon = Number(m[2]);
    const year = Number(m[3]);
    const hh = Number(m[4]);
    const mm = Number(m[5]);
    const ss = Number(m[6]);

    const d = new Date(year, mon - 1, day, hh, mm, ss);
    const ms = d.getTime();
    return Number.isFinite(ms) ? ms : NaN;
}

function extractRawTimestampMs(evt) {
    // Accept multiple transmitter schema variants.
    const numericCandidates = [
        evt.timestamp,
        evt.timestamp_ms,
        evt.last_event_ms,
        evt.last_timestamp_ms,
        evt.last_seen_ms
    ];

    for (const candidate of numericCandidates) {
        const value = Number(candidate);
        if (Number.isFinite(value) && value > 0) {
            return value;
        }
    }

    // Some firmware variants may provide a preformatted wall-clock string.
    const stringCandidates = [
        evt.last_event,
        evt.last_seen,
        evt.timestamp_text,
        evt.timestamp_str
    ];

    for (const candidate of stringCandidates) {
        if (typeof candidate !== 'string' || !candidate.trim()) {
            continue;
        }

        const parsedLocal = parseDdMmYyyyHhMmSs(candidate);
        if (Number.isFinite(parsedLocal)) {
            return { wallClockMs: parsedLocal };
        }

        const parsedNative = Date.parse(candidate);
        if (Number.isFinite(parsedNative)) {
            return { wallClockMs: parsedNative };
        }
    }

    return NaN;
}

function formatTimestamp(evt, txHealth) {
    const eventUnixMs = Number(evt.event_unix_ms);
    const eventUtcOffsetMin = Number(evt.event_utc_offset_min);

    // Preferred path: event carries its own UTC instant + event-time UTC offset.
    if (Number.isFinite(eventUnixMs) && eventUnixMs > 0 && Number.isFinite(eventUtcOffsetMin)) {
        const localMs = eventUnixMs + (eventUtcOffsetMin * 60 * 1000);
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

    const extracted = extractRawTimestampMs(evt);

    if (typeof extracted === 'object' && extracted !== null && Number.isFinite(extracted.wallClockMs)) {
        const d = new Date(extracted.wallClockMs);
        const pad = (n) => String(n).padStart(2, '0');
        const day  = pad(d.getDate());
        const mon  = pad(d.getMonth() + 1);
        const year = d.getFullYear();
        const hh   = pad(d.getHours());
        const mm   = pad(d.getMinutes());
        const ss   = pad(d.getSeconds());
        return `${day}/${mon}/${year} ${hh}:${mm}:${ss}`;
    }

    const rawTs = Number(extracted);

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

async function getTransmitterUptimeMs() {
    // Use /api/dashboard_data (consolidated endpoint) — transmitter health fields
    // are merged there; avoids a second parallel fetch to /api/transmitter_health.
    try {
        const res = await fetch('/api/dashboard_data');
        const data = await res.json();
        if (data && data.transmitter) {
            const tx = data.transmitter;
            return {
                uptimeMs: tx.uptime_ms !== undefined ? Number(tx.uptime_ms) : NaN,
                unixTimeMs: tx.unix_time !== undefined ? Number(tx.unix_time) * 1000 : NaN,
                utcOffsetMin: tx.utc_offset_min !== undefined ? Number(tx.utc_offset_min) : 0
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
        let wrap = null;
        let tbody = null;
        let offset = 0;
        let total = 0;
        let loaded = 0;
        let lastSource = '';

        while (true) {
            const res = await fetch('/api/event_logs_page?offset=' + offset + '&limit=' + CHUNK_SIZE);
            const data = await res.json();
            if (!data.success) {
                status.textContent = data.error || 'Event logs unavailable';
                status.className = 'event-meta event-error';
                return loaded > 0 ? loaded : -1;
            }

            total = Number(data.total || 0);
            lastSource = data.source || '';
            const events = data.events || [];

            if (events.length > 0 && !wrap) {
                wrap = document.createElement('div');
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
                tbody = document.createElement('tbody');
                table.appendChild(tbody);
                wrap.appendChild(table);
                list.appendChild(wrap);
            }

            events.forEach(evt => {
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

            loaded += events.length;
            offset += events.length;

            if (events.length === CHUNK_SIZE && offset < total) {
                status.textContent = 'Loading... (' + loaded + '/' + total + ' events)';
            } else {
                break;
            }
        }

        const source = lastSource ? (' | source: ' + lastSource) : '';
        status.textContent = formatCountLabel(loaded, 'event', 'events') + source;
        status.className = 'event-meta';
        return loaded;
    } catch (e) {
        status.textContent = 'Failed to load events';
        status.className = 'event-meta event-error';
        return -1;
    }
}

async function waitForSnapshotCompletion() {
    const deadline = Date.now() + SNAPSHOT_WAIT_TIMEOUT_MS;

    while (Date.now() < deadline) {
        try {
            const res = await fetch('/api/event_logs/snapshot_status');
            const data = await res.json();
            if (data && data.success && data.snapshot_complete) {
                return true;
            }
        } catch (e) {
            // Ignore transient status endpoint failures and continue timeout path.
        }

        await sleep(SNAPSHOT_WAIT_POLL_MS);
    }

    return false;
}

function closeSnapshotSubscription() {
    if (snapshotSubscriptionClosed) {
        return;
    }
    snapshotSubscriptionClosed = true;
    fetch('/api/event_logs/unsubscribe', {method: 'POST', keepalive: true});
}

async function clearEventLogs() {
    const clearBtn = document.getElementById('clearEventLogsBtn');
    const status = document.getElementById('eventStatus');

    if (clearCountdownTimer) {
        clearBtn.disabled = false;
        clearBtn.style.backgroundColor = '#dc3545';
        clearBtn.textContent = 'Clear Event Logs';
        clearCountdownRemaining = 0;
        clearTimeout(clearCountdownTimer);
        clearCountdownTimer = null;
        status.textContent = 'Clear cancelled';
        status.className = 'event-meta';
        return;
    }

    clearBtn.disabled = false;
    clearBtn.style.backgroundColor = '#ff9800';
    clearCountdownRemaining = CLEAR_COUNTDOWN_SECONDS;

    const tick = async () => {
        clearBtn.textContent = 'Clear in ' + clearCountdownRemaining + 's... (click to cancel)';

        if (clearCountdownRemaining <= 0) {
            clearCountdownTimer = null;
            clearBtn.disabled = true;
            clearBtn.style.backgroundColor = '#ff9800';
            clearBtn.textContent = 'Sending...';

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
                clearBtn.textContent = '\u2713 Clear command sent';
                clearBtn.style.backgroundColor = '#28a745';
                status.textContent = 'Events cleared \u2014 returning to dashboard\u2026';
                status.className = 'event-meta';
                closeSnapshotSubscription();
                setTimeout(() => { window.location.href = '/'; }, 3000);
            } catch (e) {
                status.textContent = 'Failed to clear event logs';
                status.className = 'event-meta event-error';
            } finally {
                if (!navigating) {
                    clearBtn.disabled = false;
                    clearBtn.textContent = 'Clear Event Logs';
                    clearBtn.style.backgroundColor = '#dc3545';
                }
            }
            return;
        }

        clearCountdownRemaining--;
        clearCountdownTimer = setTimeout(tick, 1000);
    };

    tick();
}

window.addEventListener('load', () => {
    snapshotSubscriptionClosed = false;
    fetch('/api/event_logs/subscribe', {method: 'POST'});
    keepaliveTimer = setInterval(() => {
        if (!snapshotSubscriptionClosed) {
            fetch('/api/event_logs/keepalive', {method: 'POST'});
        }
    }, KEEPALIVE_INTERVAL_MS);

    (async () => {
        const status = document.getElementById('eventStatus');
        status.textContent = 'Requesting snapshot...';
        const completed = await waitForSnapshotCompletion();
        if (!completed) {
            status.textContent = 'Snapshot timeout - showing latest available data';
            status.className = 'event-meta';
        }

        let eventCount = await loadEvents();

        // If snapshot metadata has not completed yet and we have no rows, allow a few
        // short follow-up fetches while keeping subscription active.
        if (!completed && eventCount === 0) {
            for (let attempt = 0; attempt < SNAPSHOT_RETRY_ATTEMPTS; attempt++) {
                await sleep(SNAPSHOT_RETRY_DELAY_MS);
                const retryCompleted = await waitForSnapshotCompletion();
                eventCount = await loadEvents();
                if (retryCompleted || eventCount > 0) {
                    break;
                }
            }
        }

        // Keep subscription active for the lifetime of the /systemtools/events page.
        // Unsubscribe only on unload/navigation.
    })();
});

window.addEventListener('beforeunload', () => {
    if (keepaliveTimer) {
        clearInterval(keepaliveTimer);
        keepaliveTimer = null;
    }
    if (clearCountdownTimer) {
        clearTimeout(clearCountdownTimer);
        clearCountdownTimer = null;
    }
    if (snapshotSubscriptionClosed) {
        return;
    }
    if (navigator.sendBeacon) {
        const blob = new Blob(['{}'], {type: 'application/json'});
        navigator.sendBeacon('/api/event_logs/unsubscribe', blob);
    } else {
        fetch('/api/event_logs/unsubscribe', {method: 'POST', keepalive: true});
    }
});
)rawliteral";
}
