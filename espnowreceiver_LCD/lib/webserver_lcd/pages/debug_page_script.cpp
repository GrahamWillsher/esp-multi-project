#include "debug_page_script.h"
#include <Arduino.h>

const char* get_debug_page_styles() {
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


        .mem-grid {
            display: flex;
            gap: 20px;
            flex-wrap: wrap;
            margin: 12px 0;
        }

        .mem-domain {
            flex: 1 1 200px;
            background-color: #1e1e1e;
            border-left: 4px solid #8BE9FD;
            padding: 12px 16px;
            border-radius: 4px;
        }

        .mem-domain h4 {
            margin: 0 0 10px 0;
            color: #8BE9FD;
        }

        .mem-row {
            display: flex;
            justify-content: space-between;
            padding: 3px 0;
            font-size: 14px;
        }

        .mem-label {
            color: #aaa;
        }

        .mem-warn {
            color: #FFB86C;
            font-weight: bold;
        }

        .mem-critical {
            color: #FF5555;
            font-weight: bold;
        }

        .mem-ok {
            color: #50FA7B;
        }

        .mem-meta {
            margin-top: 10px;
            display: flex;
            align-items: center;
            flex-wrap: wrap;
        }

        .mem-mode-badge {
            display: inline-block;
            padding: 3px 10px;
            border-radius: 12px;
            font-size: 12px;
            font-weight: bold;
            background-color: #44475a;
            color: #f8f8f2;
        }

        .mem-mode-burst {
            background-color: #FFB86C;
            color: #282a36;
        }

        )rawliteral";
        }

const char* get_debug_page_script() {
    return R"rawliteral(
const levelNames = ['EMERG', 'ALERT', 'CRIT', 'ERROR', 'WARNING', 'NOTICE', 'INFO', 'DEBUG'];

const DEBUG_UI_POLICY = {
    statusHideMs: 5000,
    mem: {
        fragWarn: 0.50,
        fragCritical: 0.70,
        intLargestWarn: 32768,
        intLargestCritical: 32768,
        psLargestWarn: 131072,
        burstAgeSeconds: 2
    }
};

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
            }, DEBUG_UI_POLICY.statusHideMs);
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

function fmtBytes(n) {
    if (n === undefined || n === null) return '--';
    if (n >= 1048576) return (n / 1048576).toFixed(2) + ' MB';
    if (n >= 1024)    return (n / 1024).toFixed(1) + ' KB';
    return n + ' B';
}

function fragClass(f) {
    if (f === undefined || f === null) return '';
    if (f > DEBUG_UI_POLICY.mem.fragCritical) return 'mem-critical';
    if (f > DEBUG_UI_POLICY.mem.fragWarn) return 'mem-warn';
    return 'mem-ok';
}

function setMemField(id, text, cssClass) {
    const el = document.getElementById(id);
    if (!el) return;
    el.textContent = text;
    el.className = cssClass || '';
}

function formatAgeSeconds(totalSeconds) {
    if (totalSeconds === null || totalSeconds === undefined || totalSeconds < 0) return '';

    var s = Math.floor(totalSeconds);
    var d = Math.floor(s / 86400);
    s = s % 86400;
    var h = Math.floor(s / 3600);
    s = s % 3600;
    var m = Math.floor(s / 60);
    s = s % 60;

    if (d > 0) return d + 'd ' + h + 'h ' + m + 'm';
    if (h > 0) return h + 'h ' + m + 'm ' + s + 's';
    if (m > 0) return m + 'm ' + s + 's';
    return s + 's';
}

function getFragEstimate(rawFrag, freeBytes, largestBytes) {
    var raw = Number(rawFrag);
    if (Number.isFinite(raw) && raw >= 0) {
        return raw;
    }

    var freeN = Number(freeBytes);
    var largestN = Number(largestBytes);
    if (Number.isFinite(freeN) && Number.isFinite(largestN) && freeN > 0) {
        var computed = 1 - (largestN / freeN);
        if (Number.isFinite(computed) && computed >= 0) {
            return computed;
        }
    }

    return null;
}

function loadMemoryHealth() {
    fetch('/api/memory_samples')
        .then(r => r.json())
        .then(data => {
            if (!data.success || !data.samples || data.samples.length === 0) {
                setMemField('int-free',    'No data yet', '');
                setMemField('int-largest', '--', '');
                setMemField('int-frag',    '--', '');
                setMemField('ps-free',     'No data yet', '');
                setMemField('ps-largest',  '--', '');
                setMemField('ps-frag',     '--', '');
                document.getElementById('mem-sample-count').textContent = '0 samples';
                return;
            }
            var latest = data.samples[data.samples.length - 1];
            var ageS = null;
            if (data.now_ms !== undefined && latest.ts_ms !== undefined) {
                // device timestamps are uint32_t millis(); handle wrap every ~49.7 days
                var UINT32_MOD = 4294967296;
                var delta = data.now_ms - latest.ts_ms;
                if (delta < 0) {
                    delta += UINT32_MOD;
                }
                if (delta >= 0) {
                    ageS = Math.round(delta / 1000);
                }
            }
            var intFrag = getFragEstimate(latest.int_frag_est, latest.int_free, latest.int_largest);
            var psFrag = getFragEstimate(latest.ps_frag_est, latest.ps_free, latest.ps_largest);
            setMemField('int-free',    fmtBytes(latest.int_free),
                        latest.int_largest < DEBUG_UI_POLICY.mem.intLargestWarn ? 'mem-warn' : 'mem-ok');
            setMemField('int-largest', fmtBytes(latest.int_largest),
                        latest.int_largest < DEBUG_UI_POLICY.mem.intLargestCritical ? 'mem-critical' : 'mem-ok');
            setMemField('int-frag',    intFrag !== null
                            ? (intFrag * 100).toFixed(1) + '%' : '--',
                        fragClass(intFrag));
            setMemField('ps-free',    fmtBytes(latest.ps_free), 'mem-ok');
            setMemField('ps-largest', fmtBytes(latest.ps_largest),
                        latest.ps_largest < DEBUG_UI_POLICY.mem.psLargestWarn ? 'mem-warn' : 'mem-ok');
            setMemField('ps-frag',    psFrag !== null
                            ? (psFrag * 100).toFixed(1) + '%' : '--',
                        fragClass(psFrag));
            document.getElementById('mem-sample-count').textContent =
                data.count + ' sample' + (data.count !== 1 ? 's' : '');
            if (ageS !== null && ageS >= 0) {
                document.getElementById('mem-sample-age').textContent = 'latest ~' + formatAgeSeconds(ageS) + ' ago';
            } else {
                document.getElementById('mem-sample-age').textContent = '';
            }
            var badge = document.getElementById('mem-sample-mode');
            if (ageS !== null && ageS < DEBUG_UI_POLICY.mem.burstAgeSeconds) {
                badge.textContent = 'burst';
                badge.className = 'mem-mode-badge mem-mode-burst';
            } else {
                badge.textContent = 'baseline';
                badge.className = 'mem-mode-badge';
            }
        })
        .catch(function() {
            setMemField('int-free', 'Error loading', 'mem-critical');
            setMemField('ps-free',  'Error loading', 'mem-critical');
        });
}

window.addEventListener('load', function() {
    loadCurrentDebugLevel();
    loadMemoryHealth();
});
)rawliteral";
}
