#include "memoryhealth_page_content.h"

const char* get_memoryhealth_page_content() {
    return R"rawliteral(
    <style>
        .mh-grid { display:grid; grid-template-columns:repeat(2,minmax(0,1fr)); gap:12px; margin:10px 0 14px; }
        .mh-card { background:rgba(255,255,255,.03); border:1px solid rgba(255,255,255,.08); border-radius:8px; padding:12px; }
        .mh-card h4 { margin:0 0 8px 0; color:var(--primary-color); }
        .mh-row { display:flex; justify-content:space-between; gap:10px; padding:2px 0; }
        .mh-label { color:#aaa; }
        .mh-meta { color:#aaa; font-size:13px; display:flex; gap:12px; justify-content:center; flex-wrap:wrap; }
        .mh-actions { margin-top:10px; text-align:center; }
        .mh-badge { display:inline-block; padding:2px 8px; border-radius:999px; font-size:12px; background:rgba(255,255,255,.08); color:#ddd; }
        .mh-badge-burst { background:rgba(255,184,108,.2); color:#ffb86c; }

        .gate-grid { display:grid; grid-template-columns:repeat(4,minmax(0,1fr)); gap:10px; margin-top:10px; }
        .gate-box { background:rgba(255,255,255,.03); border:1px solid rgba(255,255,255,.08); border-radius:8px; padding:10px; }
        .gate-box h4 { margin:0 0 6px 0; color:var(--primary-color); font-size:13px; }
        .gate-value { font-size:22px; font-weight:700; }
        .gate-ok { color:#8be9fd; }
        .gate-warn { color:#ffb86c; }
        .gate-critical { color:#ff6b6b; }
        .gate-note { margin-top:8px; color:#aaa; font-size:13px; }
        .gate-details { margin-top:10px; background:rgba(255,255,255,.02); border:1px solid rgba(255,255,255,.08); border-radius:8px; padding:10px; }
        .gate-row { display:flex; justify-content:space-between; gap:10px; padding:2px 0; font-size:13px; }
        .gate-k { color:#aaa; white-space:nowrap; }
        .gate-v { color:#ddd; text-align:right; word-break:break-word; }
        #gate-history { display:block; max-height:180px; overflow-y:auto; white-space:pre-wrap; text-align:left; }

        @media (max-width:720px) {
            .mh-grid { grid-template-columns:1fr; }
            .gate-grid { grid-template-columns:repeat(2,minmax(0,1fr)); }
        }
    </style>

    <div style='margin-bottom:16px;'><a href='/receiver' class='theme-nav-btn'>← Back to Receiver</a></div>
    <h1 class='theme-key-text'>🧠 Memory Health</h1>

    <div class='theme-panel' style='margin:16px 0;'>
        <h3 class='theme-panel-title' style='margin:0 0 10px 0;'>📊 Memory Status</h3>
        <div class='mh-grid'>
            <div class='mh-card'>
                <h4>Internal RAM</h4>
                <div class='mh-row'><span class='mh-label'>Free:</span><span id='int-free'>—</span></div>
                <div class='mh-row'><span class='mh-label'>Largest block:</span><span id='int-largest'>—</span></div>
                <div class='mh-row' id='int-api-restricted-row' style='display:none;'><span style='color:#ffb86c;font-size:12px;'>⚠ API-restricted / fragmented</span></div>
                <div class='mh-row'><span class='mh-label'>Frag estimate:</span><span id='int-frag'>—</span></div>
                <div class='mh-actions'><span id='mem-sample-mode-int' class='mh-badge'>—</span></div>
            </div>
            <div class='mh-card'>
                <h4>PSRAM</h4>
                <div class='mh-row'><span class='mh-label'>Free:</span><span id='ps-free'>—</span></div>
                <div class='mh-row'><span class='mh-label'>Largest block:</span><span id='ps-largest'>—</span></div>
                <div class='mh-row'><span class='mh-label'>Frag estimate:</span><span id='ps-frag'>—</span></div>
                <div class='mh-actions'><span id='mem-sample-mode-ps' class='mh-badge'>—</span></div>
            </div>
        </div>

        <div class='mh-meta'>
            <span id='mem-sample-count'>0 samples</span>
            <span id='mem-sample-age'></span>
        </div>
        <div class='mh-actions'>
            <button onclick='loadMemoryHealth();loadPressureGateStats();' class='theme-primary-btn'>↻ Refresh</button>
        </div>
    </div>

    <div class='theme-panel' style='margin:16px 0;'>
        <h3 class='theme-panel-title' style='margin:0 0 10px 0;'>🛡️ Pressure Gate</h3>
        <div class='gate-grid'>
            <div class='gate-box'><h4>Total</h4><div id='gate-total' class='gate-value gate-ok'>—</div></div>
            <div class='gate-box'><h4>Constrained</h4><div id='gate-constrained' class='gate-value gate-ok'>—</div></div>
            <div class='gate-box'><h4>Critical</h4><div id='gate-critical' class='gate-value gate-ok'>—</div></div>
            <div class='gate-box'><h4>Mutation</h4><div id='gate-mutation' class='gate-value gate-ok'>—</div></div>
        </div>
        <div id='gate-note' class='gate-note'>Loading pressure-gate counters…</div>
        <div class='gate-details'>
            <div class='gate-row'><span class='gate-k'>Current gate:</span><span id='gate-current-state' class='gate-v'>—</span></div>
            <div class='gate-row'><span class='gate-k'>Latest sample:</span><span id='gate-sampled-state' class='gate-v'>—</span></div>
            <div class='gate-row'><span class='gate-k'>Likely source:</span><span id='gate-likely-source' class='gate-v'>—</span></div>
            <div class='gate-row'><span class='gate-k'>Attribution:</span><span id='gate-likely-reason' class='gate-v'>—</span></div>
            <div class='gate-row'><span class='gate-k'>Last trigger:</span><span id='gate-last-age' class='gate-v'>—</span></div>
            <div class='gate-row'><span class='gate-k'>Endpoint:</span><span id='gate-last-uri' class='gate-v'>—</span></div>
            <div class='gate-row'><span class='gate-k'>Cause:</span><span id='gate-last-cause' class='gate-v'>—</span></div>
            <div class='gate-row'><span class='gate-k'>Code gate:</span><span id='gate-last-gate' class='gate-v'>—</span></div>
            <div class='gate-row'><span class='gate-k'>Heap @ trigger:</span><span id='gate-last-heap' class='gate-v'>—</span></div>
            <div class='gate-row'><span class='gate-k'>History:</span><span id='gate-history' class='gate-v'>—</span></div>
        </div>
    </div>
    )rawliteral";
}
