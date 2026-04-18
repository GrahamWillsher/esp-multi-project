#include "event_logs_page_content.h"
#include <Arduino.h>

String get_event_logs_page_content() {
    return R"rawliteral(
<h3>Event Logs</h3>
<div id='eventStatus' class='event-meta'>Loading...</div>
<div id='eventList' class='event-log'></div>
<div class='event-actions'>
    <button id='clearEventLogsBtn' class='event-clear-btn' type='button' onclick='clearEventLogs()'>Clear Event Logs</button>
</div>
)rawliteral";
}
