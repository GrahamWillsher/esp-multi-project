# Event Logs Implementation Analysis
## Comprehensive Architecture & Implementation Plan

**Document Version:** 1.0  
**Date:** Analysis for feature/battery-emulator-migration branch  
**Target Component:** Receiver Web Dashboard - Event Logs Card in System Tools  
**Status:** Pre-implementation analysis (ready for coding)

---

## 1. EXECUTIVE SUMMARY

### Objective
Display system event logs from the Battery Emulator on the receiver's root webpage in a new "Event Logs" card within the "System Tools" section.

### Current State
- **Event System Location:** Battery Emulator (transmitter: `src/battery_emulator/devboard/utils/events.*`)
- **Event Storage:** Static global array in battery emulator (~130 event types max)
- **Data Format:** Each event has: timestamp (64-bit), data (8-bit), occurrence count (8-bit), level (enum), state (enum)
- **Message Strings:** Comprehensive descriptions available for each event type (~120 unique events)
- **Display System:** Already exists in esp32common (webserver/events_html.*) for full event page view

### Key Finding
**Events are LOCAL to the transmitter.** They are NOT automatically synced to the receiver via MQTT or ESP-NOW. This means we must either:
1. **Add API endpoint** on transmitter to retrieve event logs via HTTP
2. **Extend MQTT** to publish event data periodically
3. **Add ESP-NOW message type** to sync events from transmitter to receiver

**Recommendation:** Use existing HTTP API pattern on transmitter + JavaScript fetch on receiver dashboard for lightweight implementation.

---

## 2. EVENT SYSTEM ARCHITECTURE

### 2.1 Event Storage (Transmitter)
**File:** `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/devboard/utils/events.h`

**Event Structure:**
```cpp
typedef struct {
  uint64_t timestamp;           // 64-bit milliseconds since boot (millis64)
  uint8_t data;                 // Custom event data (e.g., cell number for voltage errors)
  uint8_t occurences;           // Count of occurrences since startup
  EVENTS_LEVEL_TYPE level;      // INFO, DEBUG, WARNING, ERROR, UPDATE (enum)
  EVENTS_STATE_TYPE state;      // PENDING, INACTIVE, ACTIVE, ACTIVE_LATCHED (enum)
  bool MQTTpublished;           // Flag indicating if MQTT already published this event
} EVENTS_STRUCT_TYPE;
```

**Event Types:** ~130 predefined events including:
- CAN communication failures (battery, charger, inverter, native)
- Battery state issues (empty, full, overheat, undervoltage, overvoltage)
- Contactor issues (welded, open)
- Cell voltage problems (critical/under/over voltage, deviation)
- System resets (power-on, watchdog timeout, panic, brownout)
- Connectivity (WiFi, MQTT, serial)
- OTA updates
- Temperature management
- SOC/SOH monitoring
- And many more...

**Event Levels:**
- `EVENT_LEVEL_INFO` - Information messages
- `EVENT_LEVEL_DEBUG` - Detailed debug info
- `EVENT_LEVEL_WARNING` - Warning conditions
- `EVENT_LEVEL_ERROR` - Error conditions
- `EVENT_LEVEL_UPDATE` - Update/OTA events

### 2.2 Event Functions Available

**Key Retrieval Functions in events.h:**
```cpp
const EVENTS_STRUCT_TYPE* get_event_pointer(EVENTS_ENUM_TYPE event);
const char* get_event_enum_string(EVENTS_ENUM_TYPE event);
String get_event_message_string(EVENTS_ENUM_TYPE event);
const char* get_event_level_string(EVENTS_ENUM_TYPE event);
const char* get_event_level_string(EVENTS_LEVEL_TYPE event_level);
EVENTS_LEVEL_TYPE get_event_level(void);
EMULATOR_STATUS get_emulator_status();
```

**Event Collection Pattern** (from esp32common/webserver/events_html.cpp):
```cpp
std::vector<EventData> order_events;
order_events.clear();

// Collect all events with occurrences > 0
for (int i = 0; i < EVENT_NOF_EVENTS; i++) {
  event_pointer = get_event_pointer((EVENTS_ENUM_TYPE)i);
  if (event_pointer->occurences > 0) {  // Only active/occurred events
    order_events.push_back({(EVENTS_ENUM_TYPE)i, event_pointer});
  }
}

// Sort by timestamp
std::sort(order_events.begin(), order_events.end(), compareEventsByTimestampDesc);
```

### 2.3 Current Event Display Page

**File:** `esp32common/webserver/events_html.cpp` (existing implementation)
- Displays ALL events with occurrences > 0
- Columns: Event Type | Severity | Last Event Time | Count | Data | Message
- Includes clear events button
- Uses JavaScript to convert timestamp delta to human-readable time
- Visited via `/events` endpoint on transmitter or battery emulator

---

## 3. RECEIVER ARCHITECTURE

### 3.1 Root Page Location
**File:** `espnowreciever_2/lib/webserver/pages/dashboard_page.cpp`

**System Tools Section (Current):**
- Located at ~line 300 in dashboard_page.cpp
- Contains 2 grid items: Debug Logging, OTA Update
- Uses orange color scheme (#FF9800)
- Grid layout: `grid-template-columns: 1fr 1fr`

### 3.2 Data Flow Pattern on Receiver
1. **Receiver webserver** hosts pages and API endpoints
2. **API endpoints** (api_handlers.cpp) fetch data from internal caches or transmitter
3. **JavaScript in pages** uses `fetch()` to call API endpoints
4. **TransmitterManager** caches transmitter data (MQTT, ESP-NOW, HTTP)
5. **JSON responses** sent from API handlers

### 3.3 Example API Pattern (Battery Types)
```cpp
// API endpoint: /api/get_battery_types
static esp_err_t api_get_battery_types_handler(httpd_req_t *req) {
    String json = "[";
    // Build JSON array
    for (int i = 0; i < battery_type_count; i++) {
        if (i > 0) json += ",";
        json += "{\"id\":" + String(battery_types[i].id) + 
                ",\"name\":\"" + String(battery_types[i].name) + "\"}";
    }
    json += "]";
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json.c_str(), json.length());
    return ESP_OK;
}

// JavaScript on page:
function loadBatteryTypes() {
    fetch('/api/get_battery_types')
        .then(response => response.json())
        .then(data => {
            // Populate UI with data
        });
}
```

### 3.4 Handler Count Status
- **Current Usage:** 57/70 handlers registered
- **Available Headroom:** 13 handlers
- **Required for Event Logs:** 1-2 new handlers
  - `/api/get_event_logs` (main endpoint)
  - (Optional) `/api/clear_event_logs` (clearing events)

---

## 4. IMPLEMENTATION APPROACH OPTIONS

### Option A: ✅ RECOMMENDED - Transmitter HTTP API + Receiver Dashboard Card
**Pros:**
- Minimal changes to receiver (just add 1 page and 1 API endpoint)
- Reuses existing event system in transmitter
- No changes to ESP-NOW or MQTT protocols
- Handler count headroom sufficient (1 handler needed)
- Uses established receiver API pattern

**Cons:**
- Requires transmitter to have HTTP server with events endpoint
- May add 100-200ms latency per fetch

**Implementation Steps:**
1. Add `/api/get_event_logs` endpoint on transmitter
2. Add API handler on receiver to fetch from transmitter or cache locally
3. Add JavaScript card on dashboard
4. Format and display in System Tools section

### Option B: MQTT Event Stream
**Pros:**
- Real-time updates, no polling needed
- Events published as they occur

**Cons:**
- Requires MQTT topic definition and structure changes
- More MQTT traffic
- Receiver must cache events from continuous stream
- More complex implementation

**Status:** Not recommended for current phase

### Option C: ESP-NOW Event Sync Message
**Pros:**
- Direct communication, no HTTP overhead
- Encrypted if ESP-NOW encryption enabled

**Cons:**
- New message type definition
- Event buffer serialization complexity
- Receiver must manage event cache

**Status:** Not recommended for current phase

---

## 5. RECOMMENDED IMPLEMENTATION DESIGN

### 5.1 Architecture Diagram
```
Transmitter (Battery Emulator)
    ↓
    Battery Emulator Events System
    (~/130 events, stored in static arrays)
    ↓
    NEW: /api/get_event_logs HTTP endpoint
    (returns top 50 recent events as JSON)
    ↓
    ↓
Receiver Dashboard
    ↓
    NEW: /api/get_event_logs endpoint (proxy to transmitter)
    ↓
    NEW: Event Logs Card in System Tools
    (JavaScript fetches & displays events)
```

### 5.2 Data Flow

**When receiver dashboard loads:**
1. JavaScript `loadEventLogs()` called on page load
2. Fetches `/api/get_event_logs` on receiver
3. Receiver API handler fetches from transmitter HTTP (or caches)
4. Returns JSON array of recent events
5. JavaScript populates card with formatted event list

**Event Log JSON Response Format:**
```json
{
  "success": true,
  "event_count": 5,
  "total_events": 150,
  "events": [
    {
      "type": "EVENT_BATTERY_OVERHEAT",
      "type_display": "Battery Overheat",
      "level": "ERROR",
      "timestamp_ms": 1234567890,
      "time_display": "5 minutes ago",
      "count": 3,
      "message": "Battery overheated. Shutting down to prevent thermal runaway!"
    },
    {
      "type": "EVENT_CAN_BATTERY_MISSING",
      "type_display": "CAN Battery Missing",
      "level": "WARNING",
      "timestamp_ms": 1234567800,
      "time_display": "10 minutes ago",
      "count": 1,
      "message": "Battery not sending messages via CAN for the last 60 seconds. Check wiring!"
    }
  ]
}
```

### 5.3 Event Logs Card Design

**Location:** Dashboard root page, System Tools section (after OTA Update)

**HTML Structure:**
```html
<a href='/event_logs' style='text-decoration: none;'>
  <div style='padding: 15px; background: rgba(255,152,0,0.1); border: 2px solid #FF9800; 
              border-radius: 8px; text-align: center; cursor: pointer; 
              transition: background 0.2s;'
       onmouseover='this.style.background="rgba(255,152,0,0.2)"'
       onmouseout='this.style.background="rgba(255,152,0,0.1)"'>
    <span style='font-size: 24px;'>📋</span>
    <div style='margin-top: 10px; color: #FF9800; font-weight: bold;'>Event Logs</div>
    <div id='eventCount' style='font-size: 12px; color: #888; margin-top: 5px;'>
      Loading events...
    </div>
  </div>
</a>
```

**Display Options:**
1. **Mini card on dashboard** (current design above)
   - Shows event count and status summary
   - Clicking opens full event logs page

2. **Full page view** (`/event_logs`)
   - Detailed table with all events
   - Filtering by level/type
   - Sorting options
   - Clear events button

### 5.4 Display Styling

**Event Levels - Color Coding:**
- ERROR: Red (#f44336)
- WARNING: Orange (#ff9800)
- INFO: Blue (#2196F3)
- DEBUG: Gray (#9E9E9E)
- UPDATE: Green (#4CAF50)

**Example Mini Card Display:**
```
📋 Event Logs
─────────────
⚠️ 3 warnings
❌ 1 error
ℹ️ 5 info
```

---

## 6. IMPLEMENTATION STEPS

### Phase 1: Transmitter HTTP API (if not already present)

**File to Create/Modify:** `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/webserver/api/api_handlers.cpp` (or similar)

**Endpoint:** `GET /api/get_event_logs?limit=50&level=all`

**Parameters:**
- `limit` (optional, default=50): Number of recent events to return
- `level` (optional, default=all): Filter by level (INFO, WARNING, ERROR, DEBUG, UPDATE, all)

**Implementation:**
```cpp
static esp_err_t api_get_event_logs_handler(httpd_req_t *req) {
    // Parse query parameters
    char buf[128];
    httpd_req_get_url_query_str(req, buf, sizeof(buf));
    
    // Extract limit and level parameters
    int limit = 50;  // default
    const char* level_filter = "all";
    
    // Collect events with limit
    std::vector<EventData> recent_events;
    for (int i = 0; i < EVENT_NOF_EVENTS && recent_events.size() < limit; i++) {
        const EVENTS_STRUCT_TYPE* event_ptr = get_event_pointer((EVENTS_ENUM_TYPE)i);
        if (event_ptr->occurences > 0) {
            recent_events.push_back({(EVENTS_ENUM_TYPE)i, event_ptr});
        }
    }
    
    // Sort by timestamp descending (newest first)
    std::sort(recent_events.begin(), recent_events.end(), 
              compareEventsByTimestampDesc);
    
    // Build JSON response
    String json = "{\"success\":true,\"event_count\":" + String(recent_events.size()) + ",\"events\":[";
    
    for (int i = 0; i < recent_events.size(); i++) {
        if (i > 0) json += ",";
        
        const EventData& event = recent_events[i];
        json += "{\"type\":\"" + String(get_event_enum_string(event.event_handle)) + "\"";
        json += ",\"level\":\"" + String(get_event_level_string(event.event_handle)) + "\"";
        json += ",\"timestamp_ms\":" + String(event.event_pointer->timestamp);
        json += ",\"count\":" + String(event.event_pointer->occurences);
        json += ",\"message\":\"" + get_event_message_string(event.event_handle) + "\"";
        json += "}";
    }
    
    json += "]}";
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json.c_str(), json.length());
    return ESP_OK;
}
```

### Phase 2: Receiver API Endpoint

**File:** `espnowreciever_2/lib/webserver/api/api_handlers.cpp`

**Endpoint:** `GET /api/get_event_logs`

**Implementation:**
```cpp
static esp_err_t api_get_event_logs_handler(httpd_req_t *req) {
    if (!TransmitterManager::isMACKnown()) {
        const char* json = "{\"success\":false,\"error\":\"Transmitter not connected\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
        return ESP_OK;
    }
    
    // Fetch from transmitter HTTP API
    String transmitter_ip = TransmitterManager::getIPString();
    String url = "http://" + transmitter_ip + "/api/get_event_logs?limit=50";
    
    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
        String response = http.getString();
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, response.c_str(), response.length());
    } else {
        const char* json = "{\"success\":false,\"error\":\"Failed to fetch from transmitter\"}";
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, strlen(json));
    }
    
    http.end();
    return ESP_OK;
}
```

### Phase 3: Dashboard Page Updates

**File:** `espnowreciever_2/lib/webserver/pages/dashboard_page.cpp`

**Changes:**
1. Add new card to System Tools grid (make it 3 columns or 2x2)
2. Add event count display element with ID
3. Add JavaScript function `loadEventLogs()`

**HTML Addition (in System Tools section):**
```html
<a href='/event_logs' style='text-decoration: none;'>
  <div style='padding: 15px; background: rgba(255,152,0,0.1); border: 2px solid #FF9800; 
              border-radius: 8px; text-align: center; cursor: pointer; transition: background 0.2s;'
       onmouseover='this.style.background="rgba(255,152,0,0.2)"'
       onmouseout='this.style.background="rgba(255,152,0,0.1)"'>
    <span style='font-size: 24px;'>📋</span>
    <div style='margin-top: 10px; color: #FF9800; font-weight: bold;'>Event Logs</div>
    <div id='eventLogStatus' style='font-size: 12px; color: #888; margin-top: 5px;'>
      Loading...
    </div>
  </div>
</a>
```

**JavaScript Addition:**
```javascript
function loadEventLogs() {
    fetch('/api/get_event_logs')
        .then(response => response.json())
        .then(data => {
            const statusEl = document.getElementById('eventLogStatus');
            if (data.success && data.events && data.events.length > 0) {
                // Count by level
                let errorCount = 0, warningCount = 0, infoCount = 0;
                data.events.forEach(event => {
                    if (event.level === 'ERROR') errorCount++;
                    else if (event.level === 'WARNING') warningCount++;
                    else if (event.level === 'INFO') infoCount++;
                });
                
                let status = '';
                if (errorCount > 0) status += `❌ ${errorCount} error\n`;
                if (warningCount > 0) status += `⚠️ ${warningCount} warnings\n`;
                if (infoCount > 0) status += `ℹ️ ${infoCount} info`;
                
                statusEl.innerHTML = status || 'No recent events';
            } else {
                statusEl.textContent = data.error || 'No events available';
            }
        })
        .catch(error => {
            document.getElementById('eventLogStatus').textContent = 'Error loading';
        });
}

// Call on page load
window.addEventListener('load', loadEventLogs);
```

### Phase 4: Full Event Logs Page (Optional)

**File to Create:** `espnowreciever_2/lib/webserver/pages/event_logs_page.cpp`

**Route:** `/event_logs`

**Features:**
- Full table view of events
- Filtering by level
- Sorting by timestamp
- Clear all events button
- Auto-refresh capability

---

## 7. RESOURCE REQUIREMENTS

### Handler Count Impact
- **Transmitter:** 1 new handler `/api/get_event_logs`
- **Receiver:** 1 new handler `/api/get_event_logs`
- **Current Receiver Usage:** 57/70
- **After Addition:** 58/70 (12 handlers headroom remaining)

### Memory Impact
- JSON response for 50 events: ~15-20KB
- Event vector during processing: ~2KB (std::vector<EventData> = 50 × 16 bytes)
- Minimal receiver-side caching needed

### Network Impact
- API call latency: ~100-300ms (WiFi HTTP)
- Response size: ~15-20KB for 50 events
- Recommended polling interval: 30-60 seconds (manual refresh)

---

## 8. ALTERNATE DISPLAY STRATEGIES

### Strategy 1: Mini Summary Card (Recommended)
- Shows event count by level (errors, warnings, info)
- One-click access to full page
- Minimal memory footprint
- Non-blocking updates

### Strategy 2: Full Inline Table
- Entire event table on dashboard
- Shows details (time, message, occurrence count)
- More information visible
- Requires larger dashboard
- May exceed handler buffer capacity

### Strategy 3: Notification-Style
- Toast notifications for new errors
- Badge count on card
- Real-time SSE updates
- More complex implementation

**Recommendation:** Strategy 1 (Mini Summary) for Phase 1, expand to Strategy 2 later if needed.

---

## 9. INTEGRATION WITH EXISTING PATTERNS

### Pattern 1: Battery Interface Display (Analogous)
**File:** `battery_settings_page.cpp`

**Similar Implementation:**
```cpp
// Get interface display element
<div id="selectedBatteryInterface" style="...">Loading...</div>

// JavaScript fetch
fetch('/api/get_selected_interfaces')
    .then(r => r.json())
    .then(d => document.getElementById('selectedBatteryInterface')
              .textContent = d.battery_interface);
```

**Event Logs Will Follow Same Pattern:**
- API endpoint on receiver proxy
- JavaScript fetch from dashboard
- Dynamic DOM population
- Clean separation of concerns

### Pattern 2: API Handler Registration
**Location:** `webserver.cpp` - uri_handler array

**Addition Required:**
```cpp
{
    .uri = "/api/get_event_logs",
    .method = HTTP_GET,
    .handler = api_get_event_logs_handler,
    .user_ctx = NULL
}
```

---

## 10. DEPENDENCIES & PREREQUISITES

### Required Includes
```cpp
#include <algorithm>      // For std::sort
#include <vector>         // For std::vector<EventData>
#include <HTTPClient.h>   // For receiver to fetch from transmitter
```

### External References
```cpp
// From battery emulator:
extern EVENTS_STRUCT_TYPE* get_event_pointer(EVENTS_ENUM_TYPE event);
extern const char* get_event_enum_string(EVENTS_ENUM_TYPE event);
extern String get_event_message_string(EVENTS_ENUM_TYPE event);
extern compareEventsByTimestampDesc;

// From receiver:
extern TransmitterManager;
extern HTTPClient;
```

### Build Configuration
- No PlatformIO dependency changes
- No extra library requirements
- Existing esp32 SDK supports all required functions

---

## 11. TESTING STRATEGY

### Unit Tests (Transmitter)
1. Verify event collection works with 50-event limit
2. Test JSON serialization doesn't overflow buffer
3. Test query parameter parsing (limit, level)

### Integration Tests (Receiver)
1. Verify receiver can fetch from transmitter HTTP
2. Test with disconnected transmitter (error handling)
3. Test dashboard page loads without crashes

### End-to-End Tests
1. Trigger known event on transmitter (e.g., set dummy event)
2. Verify event appears on receiver dashboard within 30 seconds
3. Click card to navigate to full page
4. Verify all event details render correctly

---

## 12. POTENTIAL ISSUES & MITIGATION

| Issue | Impact | Mitigation |
|-------|--------|-----------|
| Transmitter HTTP unreachable | API fails silently | Show "Transmitter unavailable" message |
| Event JSON too large (>20KB) | Buffer overflow | Implement pagination, limit to 25 events |
| Frequent API polling | Network load | Use 30+ second refresh interval, add debounce |
| Invalid event types | Display error | Validate event enum before JSON serialization |
| Timestamp overflow (64-bit) | Display incorrect time | Use millis64() consistently, handle overflow |
| Missing event descriptions | Incomplete message | Provide fallback message for unmapped events |

---

## 13. FUTURE ENHANCEMENTS

### Phase 2 (Optional)
- Real-time event notifications via SSE
- Event filtering by type
- Event clearing functionality
- Event export to CSV
- Search/grep event messages

### Phase 3 (Optional)
- Event persistence to NVS/SD card
- Historical event log database
- Event trend analysis
- Predictive alerts
- MQTT event stream integration

---

## 14. IMPLEMENTATION CHECKLIST

### Transmitter Changes
- [ ] Create or verify `/api/get_event_logs` endpoint exists
- [ ] Test endpoint returns valid JSON
- [ ] Verify limit parameter works
- [ ] Check response time (<200ms)

### Receiver API Changes
- [ ] Add `/api/get_event_logs` handler to api_handlers.cpp
- [ ] Register handler in webserver.cpp uri_handler array
- [ ] Update EXPECTED_HANDLER_COUNT (57 → 58)
- [ ] Test handler returns transmitter data

### Dashboard Page Changes
- [ ] Update System Tools grid to accommodate Event Logs card
- [ ] Add Event Logs card HTML
- [ ] Implement `loadEventLogs()` JavaScript function
- [ ] Add call to loadEventLogs on page load
- [ ] Style card with orange theme (#FF9800)

### Testing
- [ ] Compile without errors (transmitter + receiver)
- [ ] Verify no regression on existing endpoints
- [ ] Test with connected transmitter
- [ ] Test with disconnected transmitter
- [ ] Verify dashboard loads quickly
- [ ] Check memory usage (heap free)

---

## 15. CODE LOCATIONS REFERENCE

### Transmitter
- Event system: `src/battery_emulator/devboard/utils/events.h|cpp`
- Existing event display: `src/battery_emulator/webserver/events_html.cpp` (reference implementation)
- Battery emulator main: `src/main.cpp`

### Receiver
- Dashboard page: `lib/webserver/pages/dashboard_page.cpp`
- API handlers: `lib/webserver/api/api_handlers.cpp`
- Webserver init: `lib/webserver/webserver.cpp`
- Transmitter manager: `lib/webserver/utils/transmitter_manager.h|cpp`

### Common Library
- Existing event page: `esp32common/webserver/events_html.h|cpp`
- Reference pattern: `esp32common/webserver/pages/*_page.cpp`

---

## 16. CONCLUSION

The Event Logs implementation is **straightforward and low-risk**:

✅ **Event system is mature** - already used in battery emulator  
✅ **Display code exists** - can reference events_html.cpp  
✅ **API pattern established** - receiver dashboard already uses HTTP to transmitter  
✅ **Handler headroom available** - only 1-2 handlers needed (13 available)  
✅ **Minimal changes required** - ~200 LOC total across both projects  

**Estimated Implementation Time:** 2-4 hours  
**Estimated Testing Time:** 1-2 hours  
**Total Effort:** Half day to full implementation with testing  

**Recommendation:** Proceed with Phase 1 implementation (transmitter API + receiver dashboard card). Full event logs page can be added in Phase 2 if needed.

---

## APPENDICES

### Appendix A: Event Types Quick Reference

**System Events (Priority):**
- CAN failures (battery, charger, inverter)
- Thermal events (overheat, thermal runaway)
- Battery state (empty, full, overvoltage, undervoltage)

**User Events:**
- Contactor status (welded, open)
- Cell monitoring (under/over voltage, deviation)

**Operational Events:**
- WiFi/MQTT connectivity
- OTA updates
- System resets
- Task overruns

**Total Count:** ~130 events defined in EVENTS_ENUM_TYPE macro

### Appendix B: JSON Response Examples

**Success Response (with events):**
```json
{
  "success": true,
  "event_count": 3,
  "events": [
    {
      "type": "BATTERY_OVERHEAT",
      "level": "ERROR",
      "timestamp_ms": 1234567890,
      "count": 2,
      "message": "Battery overheated..."
    }
  ]
}
```

**Error Response (transmitter unreachable):**
```json
{
  "success": false,
  "error": "Transmitter not connected"
}
```

**Empty Response (no events):**
```json
{
  "success": true,
  "event_count": 0,
  "events": []
}
```

---

**Document End**  
*For questions or updates, refer to Battery Emulator events.h documentation and receiver API patterns in api_handlers.cpp*
