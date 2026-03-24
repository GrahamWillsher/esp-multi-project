#include "event_logs_page_content.h"
#include "../common/nav_buttons.h"
#include <Arduino.h>

String get_event_logs_page_content() {
    String content = R"rawliteral(
<h3>Event Logs</h3>
<div id='eventStatus' class='event-meta'>Loading...</div>
<div id='eventList' class='event-log'></div>
)rawliteral";
    content += generate_nav_buttons();
    return content;
}
