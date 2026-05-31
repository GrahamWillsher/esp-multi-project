#include "debug_page_content.h"

const char* get_debug_page_content() {
        static const char kDebugPageContent[] =
R"rawliteral(
<h1>Debug Configuration</h1>

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
)rawliteral";

        return kDebugPageContent;
}
