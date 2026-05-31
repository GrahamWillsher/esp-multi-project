#include "memoryhealth_page_script.h"

const char* get_memoryhealth_page_script() {
    return R"rawliteral(
        const POLICY = {
            intLargestWarn: 32768,
            intLargestRestricted: 16384,
            intLargestCritical: 8192,
            psLargestWarn: 131072,
            fragWarn: 0.50,
            fragCritical: 0.70,
            freshIssueSeconds: 60
        };

        let gDeviceBaseMs = 0;
        let gWallBaseMs = 0;
        let gHistorySig = '';
        let gHistoryText = '';

        function n(v, d) {
            var x = Number(v);
            return Number.isFinite(x) ? x : d;
        }

        function txt(id, value, cls) {
            var el = document.getElementById(id);
            if (!el) return;
            el.textContent = value;
            if (cls !== undefined) el.className = cls;
        }

        function fmtBytes(v) {
            var b = n(v, NaN);
            if (!Number.isFinite(b)) return '--';
            if (b >= 1048576) return (b / 1048576).toFixed(2) + ' MB';
            if (b >= 1024) return (b / 1024).toFixed(1) + ' KB';
            return String(Math.round(b)) + ' B';
        }

        function fmtAge(totalS) {
            if (totalS === null || totalS === undefined || totalS < 0) return 'unknown';
            var s = Math.floor(totalS);
            var d = Math.floor(s / 86400); s %= 86400;
            var h = Math.floor(s / 3600); s %= 3600;
            var m = Math.floor(s / 60); s %= 60;
            if (d > 0) return d + 'd ' + h + 'h ' + m + 'm';
            if (h > 0) return h + 'h ' + m + 'm ' + s + 's';
            if (m > 0) return m + 'm ' + s + 's';
            return s + 's';
        }

        function deltaSeconds(nowMs, tsMs) {
            var now = n(nowMs, NaN);
            var ts = n(tsMs, NaN);
            if (!Number.isFinite(now) || !Number.isFinite(ts) || ts <= 0) return null;
            var d = now - ts;
            if (d < 0) d += 4294967296;
            if (d < 0) return null;
            return Math.floor(d / 1000);
        }

        function setAnchor(nowMs) {
            var now = n(nowMs, 0);
            if (gDeviceBaseMs === 0 && now > 0) {
                gDeviceBaseMs = now;
                gWallBaseMs = Date.now();
            }
        }

        function wallTimeFromDeviceMs(tsMs) {
            if (gDeviceBaseMs === 0 || gWallBaseMs === 0) return null;
            var wall = gWallBaseMs + (n(tsMs, 0) - gDeviceBaseMs);
            var d = new Date(wall);
            return String(d.getHours()).padStart(2, '0') + ':' +
                   String(d.getMinutes()).padStart(2, '0') + ':' +
                   String(d.getSeconds()).padStart(2, '0');
        }

        function fragEstimate(raw, freeB, largestB) {
            var f = n(raw, NaN);
            if (Number.isFinite(f) && f >= 0) return f;
            var free = n(freeB, NaN);
            var largest = n(largestB, NaN);
            if (!Number.isFinite(free) || !Number.isFinite(largest) || free <= 0) return null;
            var e = 1 - (largest / free);
            return Number.isFinite(e) && e >= 0 ? e : null;
        }

        function fragClass(f) {
            if (f === null || f === undefined) return '';
            if (f > POLICY.fragCritical) return 'mem-critical';
            if (f > POLICY.fragWarn) return 'mem-warn';
            return 'mem-ok';
        }

        function largestClass(v) {
            var nV = n(v, 0);
            if (nV < POLICY.intLargestCritical) return 'mem-critical';
            if (nV < POLICY.intLargestRestricted) return 'mem-warn';
            if (nV < POLICY.intLargestWarn) return 'mem-warn';
            return 'mem-ok';
        }

        function updateModeBadges(mode, burst) {
            var ids = ['mem-sample-mode-int', 'mem-sample-mode-ps'];
            for (var i = 0; i < ids.length; i++) {
                var el = document.getElementById(ids[i]);
                if (!el) continue;
                el.textContent = mode;
                el.className = burst ? 'mh-badge mh-badge-burst' : 'mh-badge';
            }
        }

        function loadMemoryHealth() {
            fetch('/api/memory_events')
                .then(function(r) { return r.json(); })
                .then(function(data) {
                    if (!data || !data.success || !Array.isArray(data.events) || data.events.length === 0) {
                        txt('int-free', 'No data');
                        txt('int-largest', '--');
                        txt('int-frag', '--');
                        txt('ps-free', 'No data');
                        txt('ps-largest', '--');
                        txt('ps-frag', '--');
                        txt('mem-sample-count', '0 events');
                        txt('mem-sample-age', '');
                        txt('gate-sampled-state', 'No memory events yet');
                        updateModeBadges('event', false);
                        return;
                    }

                    var latest = data.events[data.events.length - 1];
                    var ageS = deltaSeconds(n(data.now_ms, 0), n(latest.ts_ms, 0));
                    var intFrag = fragEstimate(latest.int_frag_est, latest.int_free, latest.int_largest);
                    var psFrag = fragEstimate(latest.ps_frag_est, latest.ps_free, latest.ps_largest);

                    txt('int-free', fmtBytes(latest.int_free), n(latest.int_largest, 0) < POLICY.intLargestWarn ? 'mem-warn' : 'mem-ok');
                    txt('int-largest', fmtBytes(latest.int_largest), largestClass(latest.int_largest));
                    txt('int-frag', intFrag !== null ? (intFrag * 100).toFixed(1) + '%' : '--', fragClass(intFrag));

                    txt('ps-free', fmtBytes(latest.ps_free), 'mem-ok');
                    txt('ps-largest', fmtBytes(latest.ps_largest), n(latest.ps_largest, 0) < POLICY.psLargestWarn ? 'mem-warn' : 'mem-ok');
                    txt('ps-frag', psFrag !== null ? (psFrag * 100).toFixed(1) + '%' : '--', fragClass(psFrag));

                    var restrictedRow = document.getElementById('int-api-restricted-row');
                    if (restrictedRow) {
                        var showRestricted = n(latest.int_largest, 0) < POLICY.intLargestRestricted && (ageS === null || ageS <= POLICY.freshIssueSeconds);
                        restrictedRow.style.display = showRestricted ? '' : 'none';
                    }

                    var count = n(data.count, data.events.length);
                    txt('mem-sample-count', count + ' event' + (count === 1 ? '' : 's'));
                    txt('mem-sample-age', ageS !== null ? ('latest event ~' + fmtAge(ageS) + ' ago') : '');

                    var isProbeEvent = String(latest.event_type || '').indexOf('probe_') === 0;
                    updateModeBadges(isProbeEvent ? 'probe-event' : 'sampler-event', false);
                    var level = String(latest.threshold_level || 'none');
                    var reason = String(latest.reason || 'n/a');
                    txt('gate-sampled-state', 'Last event: ' + level + ' (' + reason + ')' + (ageS !== null ? ' - ' + fmtAge(ageS) + ' ago' : ''));
                })
                .catch(function() {
                    txt('int-free', 'Error loading', 'mem-critical');
                    txt('ps-free', 'Error loading', 'mem-critical');
                    txt('gate-sampled-state', 'Memory sample load failed');
                });
        }

        function historyText(history) {
            if (!Array.isArray(history) || history.length === 0) {
                return 'No restricted probe history yet';
            }

            var sig = String(history.length) + ':' + String(history[0].ts_ms || 0) + ':' + String(history[history.length - 1].ts_ms || 0);
            if (sig === gHistorySig && gHistoryText) return gHistoryText;

            var out = history.map(function(h, i) {
                var t = wallTimeFromDeviceMs(h.ts_ms) || 'unknown time';
                var item = String(h.item || 'unknown');
                var phase = String(h.phase || 'unknown');
                var cause = String(h.cause || 'unknown');
                  var free = fmtBytes(h.free_heap);
                  var largest = fmtBytes(h.largest_8bit);
                var frag = n(h.internal_frag_est, 0);
                return (i + 1) + '. ' + item + ' @ ' + phase + ' (@' + t + ')\n' +
                       '   cause=' + cause + '\n' +
                      '   heap: free=' + free + ', largest=' + largest + ' (' + (frag * 100).toFixed(1) + '% frag)';
            }).join('\n\n');

            gHistorySig = sig;
            gHistoryText = out;
            return out;
        }

        function gateValueClass(v, warn, critical) {
            var x = n(v, 0);
            if (x >= critical) return 'gate-value gate-critical';
            if (x >= warn) return 'gate-value gate-warn';
            return 'gate-value gate-ok';
        }

        function gateAgeText(nowMs, tsMs) {
            var ageS = deltaSeconds(nowMs, tsMs);
            if (ageS === null) return 'No pressure event recorded yet';
            if (ageS < 1) return 'just now';
            return fmtAge(ageS) + ' ago';
        }

        function loadPressureGateStats() {
            fetch('/api/pressure_gate_stats?offset=0&limit=16')
                .then(function(r) { return r.json(); })
                .then(function(data) {
                    if (!data || !data.success) {
                        txt('gate-note', (data && data.message) ? data.message : 'Pressure-gate stats unavailable');
                        txt('gate-total', '--', 'gate-value gate-warn');
                        txt('gate-constrained', '--', 'gate-value gate-warn');
                        txt('gate-critical', '--', 'gate-value gate-warn');
                        txt('gate-mutation', '--', 'gate-value gate-warn');
                        txt('gate-history', 'Unavailable');
                        return;
                    }

                    var nowMs = n(data.now_ms, 0);
                    setAnchor(nowMs);

                    var total = n(data.read_throttled_total, 0);
                    var constrained = n(data.read_throttled_constrained, 0);
                    var critical = n(data.read_throttled_critical, 0);
                    var mutation = n(data.mutation_blocked_critical, 0);

                    txt('gate-total', String(total), gateValueClass(total, 1, 999999));
                    txt('gate-constrained', String(constrained), gateValueClass(constrained, 1, 999999));
                    txt('gate-critical', String(critical), gateValueClass(critical, 1, 1));
                    txt('gate-mutation', String(mutation), gateValueClass(mutation, 1, 1));

                    var restricted = !!data.currently_restricted;
                    var criticalNow = !!data.currently_critical;
                    var causeNow = String(data.current_restriction_cause || 'none');

                    if (restricted) {
                        txt('gate-current-state', criticalNow ? 'Restricted now (CRITICAL)' : 'Restricted now');
                        txt('gate-note', 'Gate currently restricted (' + causeNow + ').');
                    } else {
                        txt('gate-current-state', 'Open now');
                        txt('gate-note', (total + constrained + critical + mutation) > 0
                            ? 'Gate open now; historical restrictions shown below.'
                            : 'No pressure-gate rejections recorded yet.');
                    }

                    var probeItem = String(data.last_probe_item || '—');
                    var probePhase = String(data.last_probe_phase || '—');
                    txt('gate-likely-source', probeItem !== '—' ? ('Feature: ' + probeItem) : String(data.likely_source_section || '—'));
                    txt('gate-likely-reason', probePhase !== '—' ? ('Phase: ' + probePhase) : String(data.likely_source_reason || '—'));

                    var lastTs = n(data.last_rejection_ms, 0) || n(data.last_probe_ms, 0);
                    txt('gate-last-age', gateAgeText(nowMs, lastTs));
                    txt('gate-last-uri', String(data.last_probe_uri || data.last_rejection_uri || '—'));
                    txt('gate-last-cause', String(data.last_probe_breadcrumb || data.last_rejection_cause || '—'));
                    txt('gate-last-gate', probePhase !== '—' ? ('Phase: ' + probePhase) : String(data.last_rejection_gate || '—'));

                    var pFree = n(data.last_probe_internal_free, 0);
                    var pLargest = n(data.last_probe_internal_largest, 0);
                    var pFrag = n(data.last_probe_internal_frag_est, 0);
                    if (pLargest > 0) {
                        txt('gate-last-heap', fmtBytes(pFree) + ' free, ' + fmtBytes(pLargest) + ' largest (' + (pFrag * 100).toFixed(1) + '% frag)');
                    } else {
                        txt('gate-last-heap', fmtBytes(n(data.current_free_heap, 0)) + ' free (8-bit), ' + fmtBytes(n(data.current_largest_8bit, 0)) + ' largest (8-bit)');
                    }

                    var history = Array.isArray(data.probe_history) ? data.probe_history : [];
                    txt('gate-history', historyText(history));
                })
                .catch(function() {
                    txt('gate-note', 'Error loading pressure-gate counters.');
                    txt('gate-total', '--', 'gate-value gate-critical');
                    txt('gate-constrained', '--', 'gate-value gate-critical');
                    txt('gate-critical', '--', 'gate-value gate-critical');
                    txt('gate-mutation', '--', 'gate-value gate-critical');
                    txt('gate-current-state', 'Unavailable');
                    txt('gate-likely-source', 'Unavailable');
                    txt('gate-likely-reason', 'Unavailable');
                    txt('gate-last-age', 'Unavailable');
                    txt('gate-last-uri', 'Unavailable');
                    txt('gate-last-cause', 'Unavailable');
                    txt('gate-last-gate', 'Unavailable');
                    txt('gate-last-heap', 'Unavailable');
                    txt('gate-history', 'Unavailable');
                });
        }

        window.addEventListener('load', function() {
            loadMemoryHealth();
            loadPressureGateStats();
        });
    )rawliteral";
}
