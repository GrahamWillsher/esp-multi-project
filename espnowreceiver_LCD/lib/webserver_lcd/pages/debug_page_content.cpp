#include "debug_page_content.h"

const char* get_debug_page_content() {
        static const char kDebugPageContent[] =
R"rawliteral(
<h1>Debug Configuration</h1>
<div style='margin-bottom: 20px;'>
        <a href='/' style='display: inline-block; padding: 10px 16px; background: #4CAF50; color: white; text-decoration: none; border-radius: 6px; font-weight: bold;'>
                ← Dashboard
        </a>
</div>

<div class='debug-control'>
        <h3>📊 Transmitter Debug Level Control</h3>
        <p>Control the transmitter debug logging level. Commands are sent on MQTT topic: <code>batt-emu/mqtt-v1/rx/cmd/control/debug_level</code></p>

        <div class='current-level-box'>
                <strong class='current-level-label'>Current Debug Level:</strong>
                <span id='currentLevel' class='current-level-value'>Loading...</span>
        </div>

        <label for='debugLevel'><strong>Select Debug Level:</strong></label><br>
        <select id='debugLevel' name='debugLevel'>
                <option value='0'>EMERG - Emergency (0) - System unusable</option>
                <option value='1'>ALERT - Alert (1) - Immediate action required</option>
                <option value='2'>CRIT - Critical (2) - Critical conditions</option>
                <option value='3'>ERROR - Error (3) - Error conditions</option>
                <option value='4'>WARNING - Warning (4) - Warning conditions</option>
                <option value='5'>NOTICE - Notice (5) - Normal but significant</option>
                <option value='6' selected>INFO - Info (6) - Informational messages</option>
                <option value='7'>DEBUG - Debug (7) - Debug-level messages</option>
        </select><br>

        <button onclick='setDebugLevel()' class='button'>Set Transmitter Debug Level</button>
        <div id='debug-status'></div>
</div>
)rawliteral"
R"rawliteral(
        <div class='debug-control' id='mem-health-card'>
                <h3>🧠 Memory Health</h3>
                                         <p>Heap health is sampled every 30 seconds, or every 1 second during active API/SSE activity.
                                                 Fragmentation is shown as a percentage: lower is better, while values above 70% suggest memory pressure.</p>

                <div class='mem-grid'>
                        <div class='mem-domain' id='mem-internal'>
                                <h4>Internal RAM</h4>
                                <div class='mem-row'><span class='mem-label'>Free:</span>        <span id='int-free'>—</span></div>
                                <div class='mem-row'><span class='mem-label'>Largest block:</span><span id='int-largest'>—</span></div>
                                <div class='mem-row'><span class='mem-label'>Frag estimate:</span><span id='int-frag'>—</span></div>
                        </div>
                        <div class='mem-domain' id='mem-psram'>
                                <h4>PSRAM</h4>
                                <div class='mem-row'><span class='mem-label'>Free:</span>        <span id='ps-free'>—</span></div>
                                <div class='mem-row'><span class='mem-label'>Largest block:</span><span id='ps-largest'>—</span></div>
                                <div class='mem-row'><span class='mem-label'>Frag estimate:</span><span id='ps-frag'>—</span></div>
                        </div>
                </div>

                <div class='mem-meta'>
                        <span id='mem-sample-mode' class='mem-mode-badge'>—</span>
                        <span id='mem-sample-count' style='margin-left:12px;color:#aaa;font-size:13px;'>0 samples</span>
                        <span id='mem-sample-age'   style='margin-left:12px;color:#aaa;font-size:13px;'></span>
                </div>

                <button onclick='loadMemoryHealth()' class='button' style='margin-top:12px;'>↻ Refresh</button>
        </div>
                )rawliteral";

        return kDebugPageContent;
}
