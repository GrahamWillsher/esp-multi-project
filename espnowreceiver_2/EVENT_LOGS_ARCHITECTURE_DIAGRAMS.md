# Event Logs Data Flow & Component Diagram

## System Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         TRANSMITTER (ESP32-PoE2)                        │
│                                                                         │
│  ┌────────────────────────────────────────────────────────────────┐   │
│  │         Battery Emulator (devboard/utils/events.*)            │   │
│  │                                                                │   │
│  │  EVENT_STRUCT_TYPE entries[EVENT_NOF_EVENTS]  (~130 events)  │   │
│  │  ├─ timestamp (64-bit)                                        │   │
│  │  ├─ level (ERROR, WARNING, INFO, DEBUG, UPDATE)              │   │
│  │  ├─ occurences (count)                                        │   │
│  │  ├─ state (ACTIVE, INACTIVE, etc)                            │   │
│  │  └─ message (descriptive string)                             │   │
│  │                                                                │   │
│  │  Functions:                                                   │   │
│  │  • get_event_pointer(event_id)                               │   │
│  │  • get_event_message_string(event_id)                        │   │
│  │  • get_event_level_string(event_id)                          │   │
│  │  • compareEventsByTimestampDesc()                            │   │
│  └────────────────────────────────────────────────────────────────┘   │
│                            ▲                                            │
│                            │ (new endpoint)                            │
│                            │                                            │
│  ┌────────────────────────────────────────────────────────────────┐   │
│  │  [NEW] Webserver API Handler                                 │   │
│  │  GET /api/get_event_logs?limit=50&level=all                 │   │
│  │                                                                │   │
│  │  Implementation:                                              │   │
│  │  1. Collect events with occurences > 0                       │   │
│  │  2. Sort by timestamp (DESC)                                 │   │
│  │  3. Limit to N events (param)                                │   │
│  │  4. Build JSON response with event details                   │   │
│  │  5. Return 200 OK with JSON                                  │   │
│  │                                                                │   │
│  │  Response:                                                    │   │
│  │  {                                                             │   │
│  │    "success": true,                                          │   │
│  │    "event_count": 5,                                         │   │
│  │    "events": [                                               │   │
│  │      {                                                        │   │
│  │        "type": "BATTERY_OVERHEAT",                           │   │
│  │        "level": "ERROR",                                     │   │
│  │        "timestamp_ms": 1234567890,                           │   │
│  │        "count": 2,                                           │   │
│  │        "message": "Battery overheated..."                    │   │
│  │      },                                                       │   │
│  │      ...                                                      │   │
│  │    ]                                                          │   │
│  │  }                                                             │   │
│  └────────────────────────────────────────────────────────────────┘   │
│                            ▲                                            │
└────────────────────────────┼────────────────────────────────────────────┘
                             │
                    HTTP (WiFi 2.4GHz)
                      ~100-300ms latency
                             │
                             │
┌────────────────────────────┼────────────────────────────────────────────┐
│                            │    RECEIVER (ESP32-S3)                     │
│                            ▼                                             │
│  ┌────────────────────────────────────────────────────────────────┐   │
│  │  [NEW] API Handler (api_handlers.cpp)                         │   │
│  │  GET /api/get_event_logs                                     │   │
│  │                                                                │   │
│  │  Implementation:                                              │   │
│  │  1. Check if transmitter connected                           │   │
│  │  2. Build HTTP URL to transmitter endpoint                   │   │
│  │  3. HTTPClient.GET(url)                                      │   │
│  │  4. If success, forward response                             │   │
│  │  5. If error, return error JSON                              │   │
│  │                                                                │   │
│  │  Registered in uri_handlers[] array                          │   │
│  └────────────────────────────────────────────────────────────────┘   │
│                            ▲                                            │
│                            │ HTTP from browser                         │
│                            │                                            │
│  ┌────────────────────────────────────────────────────────────────┐   │
│  │  [NEW] Dashboard Card (dashboard_page.cpp)                   │   │
│  │                                                                │   │
│  │  ┌─────────────────────────────────┐                         │   │
│  │  │    📋 Event Logs                │                         │   │
│  │  │  ❌ 1 error                     │                         │   │
│  │  │  ⚠️  2 warnings                 │                         │   │
│  │  │  ℹ️  5 info                     │                         │   │
│  │  │                                 │  (clickable)            │   │
│  │  │  [System Tools Grid - Row 3]    │                         │   │
│  │  └─────────────────────────────────┘                         │   │
│  │                                                                │   │
│  │  JavaScript (on page load):                                  │   │
│  │  function loadEventLogs() {                                  │   │
│  │    fetch('/api/get_event_logs')                             │   │
│  │    .then(r => r.json())                                     │   │
│  │    .then(data => {                                           │   │
│  │      // Parse event levels                                  │   │
│  │      // Count errors, warnings, info                        │   │
│  │      // Update #eventLogStatus element                      │   │
│  │    })                                                         │   │
│  │  }                                                             │   │
│  │                                                                │   │
│  │  window.addEventListener('load', loadEventLogs);            │   │
│  └────────────────────────────────────────────────────────────────┘   │
│                            ▲                                            │
│                            │ (optional) Link to                        │
│                            │ full page at /event_logs                  │
│                            │                                            │
│  ┌────────────────────────────────────────────────────────────────┐   │
│  │  [FUTURE] Full Event Logs Page (event_logs_page.cpp)         │   │
│  │  GET /event_logs                                             │   │
│  │                                                                │   │
│  │  Features:                                                    │   │
│  │  • Full table view of events                                 │   │
│  │  • Filtering by level/type                                   │   │
│  │  • Sorting options                                           │   │
│  │  • Clear events button                                       │   │
│  │  • (Later) Export to CSV                                     │   │
│  │                                                                │   │
│  │  Table Columns:                                              │   │
│  │  • Event Type          | BATTERY_OVERHEAT                    │   │
│  │  • Severity            | ERROR                               │   │
│  │  • Last Occurred       | 5 minutes ago                       │   │
│  │  • Occurrence Count    | 2                                   │   │
│  │  • Message             | Battery overheated...               │   │
│  └────────────────────────────────────────────────────────────────┘   │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Request/Response Flow

### Flow 1: Browser Loads Receiver Dashboard

```
┌────────────┐                                    ┌──────────┐
│  Browser   │                                    │ Receiver │
└────────────┘                                    └──────────┘
     │                                                  │
     │───── GET / ─────────────────────────────────────>│
     │                                                  │
     │ <────── HTML dashboard_page ──────────────────│
     │                                                  │
     │ [Dashboard HTML loads, JavaScript runs]         │
     │ window.addEventListener('load', loadEventLogs)  │
     │                                                  │
     │───── fetch('/api/get_event_logs') ────────────>│
     │                                                  │
     │            [Receiver Handler]                   │
     │            HTTPClient.GET(transmitter_ip:80     │
     │                   /api/get_event_logs)          │
     │                                                  │
     │            [HTTP to Transmitter]                │
     │       ┌────────────────────────────────────┐   │
     │       │   Transmitter event system collects  │   │
     │       │   events, builds JSON, returns 200  │   │
     │       └────────────────────────────────────┘   │
     │                                                  │
     │       [Transmitter Response: JSON]               │
     │                                                  │
     │ <──── JSON {success:true,events:[...]} ───────│
     │                                                  │
     │ [JavaScript processes JSON]                     │
     │ Counts errors/warnings/info by level            │
     │ Updates #eventLogStatus innerHTML               │
     │                                                  │
     │ [Dashboard shows event summary]                 │
     │                                                  │
```

### Flow 2: User Clicks Event Logs Card

```
┌────────────┐                                    ┌──────────┐
│  Browser   │                                    │ Receiver │
└────────────┘                                    └──────────┘
     │                                                  │
     │───── click on card ─────────────────────────────>│
     │ (href='/event_logs')                            │
     │                                                  │
     │───── GET /event_logs ──────────────────────────>│
     │                                                  │
     │ <────── Full Event Logs Page HTML ────────────│
     │                                                  │
     │ [Displays table with all events]                │
     │                                                  │
```

---

## Handler Count Impact

### Before Implementation
```
Total Handlers: 57/70

Pages (16):
  • dashboard_page
  • battery_specs_display_page
  • inverter_specs_display_page
  • battery_settings_page
  • inverter_settings_page
  • ... (11 more pages)

API Handlers (41):
  • /api/get_battery_types
  • /api/set_battery_type
  • /api/get_inverter_types
  • /api/set_inverter_type
  • /api/get_battery_interfaces
  • /api/get_selected_interfaces
  • /api/set_battery_interface
  • /api/get_inverter_interfaces
  • /api/get_inverter_interface
  • /api/set_inverter_interface
  • /api/monitor
  • /api/cell_data
  • /api/dashboard_data
  • ... (28 more)

Max Capacity: 70
Headroom: 13
```

### After Event Logs Implementation
```
Total Handlers: 58/70

Pages (16): [unchanged]

API Handlers (42):
  • [all previous 41]
  • /api/get_event_logs [NEW]

Max Capacity: 70
Headroom: 12

✅ Plenty of capacity remaining!
```

---

## Data Structure Details

### Event Data Flow

```
Battery Emulator (Memory)
    ↓
    EVENTS_STRUCT_TYPE entries[EVENT_NOF_EVENTS]
    ├─ entries[0]: EVENT_CANMCP2517FD_INIT_FAILURE
    │  ├─ timestamp: 1234567890
    │  ├─ level: WARNING
    │  ├─ occurences: 1
    │  └─ message: "CAN-FD initialization failed..."
    │
    ├─ entries[1]: EVENT_CANMCP2515_INIT_FAILURE
    │  ├─ timestamp: 0 (never occurred)
    │  ├─ level: WARNING
    │  ├─ occurences: 0
    │  └─ [skipped in display]
    │
    ├─ entries[24]: EVENT_BATTERY_OVERHEAT
    │  ├─ timestamp: 1234567950
    │  ├─ level: ERROR
    │  ├─ occurences: 2
    │  └─ message: "Battery overheated. Shutting down..."
    │
    └─ entries[129]: EVENT_GPIO_CONFLICT
       ├─ timestamp: 1234567900
       ├─ level: WARNING
       ├─ occurences: 1
       └─ message: "GPIO Pin Conflict: ..."

↓ [filter occurences > 0]

EventData vector: [24 active events selected]
    ├─ (EVENT_CANMCP2517FD_INIT_FAILURE, ptr)
    ├─ (EVENT_BATTERY_OVERHEAT, ptr)
    ├─ (EVENT_GPIO_CONFLICT, ptr)
    └─ [21 more]

↓ [sort by timestamp DESC]

Sorted events:
    ├─ EVENT_BATTERY_OVERHEAT (timestamp: 1234567950)
    ├─ EVENT_CANMCP2517FD_INIT_FAILURE (timestamp: 1234567890)
    └─ EVENT_GPIO_CONFLICT (timestamp: 1234567900)

↓ [JSON serialization]

JSON Response: {
  "success": true,
  "event_count": 24,
  "events": [
    {
      "type": "BATTERY_OVERHEAT",
      "level": "ERROR",
      "timestamp_ms": 1234567950,
      "count": 2,
      "message": "Battery overheated..."
    },
    {
      "type": "GPIO_CONFLICT",
      "level": "WARNING",
      "timestamp_ms": 1234567900,
      "count": 1,
      "message": "GPIO Pin Conflict..."
    },
    {
      "type": "CANMCP2517FD_INIT_FAILURE",
      "level": "WARNING",
      "timestamp_ms": 1234567890,
      "count": 1,
      "message": "CAN-FD initialization failed..."
    }
  ]
}

↓ [HTTP response to receiver]

↓ [Receiver fetches from transmitter]

↓ [JavaScript processes on browser]

Dashboard Display:
┌──────────────────────┐
│ 📋 Event Logs        │
│ ❌ 1 error           │
│ ⚠️  2 warnings       │
│ ℹ️  0 info           │
└──────────────────────┘
```

---

## File Dependencies

### Transmitter Dependencies
```
events.cpp
├─ events.h (event structures, functions)
├─ datalayer.h (battery emulator data)
├─ millis64.h (timestamp conversion)
├─ logging.h (debug output)
└─ Arduino.h (String class)

api_handlers.cpp
├─ events.h (event functions)
├─ ArduinoJson.h (JSON building)
├─ HTTPClient.h (if forwarding)
└─ webserver_common.h (handler registration)
```

### Receiver Dependencies
```
api_handlers.cpp
├─ transmitter_manager.h (transmitter IP/MAC)
├─ HTTPClient.h (fetch from transmitter)
├─ ArduinoJson.h (JSON response)
├─ Arduino.h (String class)
└─ webserver_common.h (handler registration)

dashboard_page.cpp
├─ page_generator.h (common page structure)
├─ transmitter_manager.h (status info)
├─ firmware_version.h (version info)
└─ Arduino.h (String class)

webserver.cpp
├─ api_handlers.h (handler declarations)
├─ page_definitions.h (page handlers)
└─ esp_http_server.h (handler registration)
```

---

## Memory Usage Estimate

### Transmitter
```
Event Array (static):
  ~130 events × ~20 bytes each = ~2.6 KB (permanent)

Temporary (during API call):
  EventData vector for 50 events: ~1 KB (freed after response)
  JSON string buffer: ~20 KB (freed after response)

Total Peak RAM: ~25 KB
Total Persistent: ~2.6 KB
```

### Receiver
```
Cache (optional, if implemented):
  Last event response: ~20 KB

API response buffer:
  ~20 KB for building response (temporary)

Total Peak RAM: ~40 KB
Total if cached: ~60 KB
```

---

## Timing Estimates

```
Transmitter Processing:
  Event collection loop: ~1-5 ms
  Sorting algorithm: ~5-10 ms
  JSON serialization: ~10-20 ms
  Total: ~20-35 ms

Network Latency:
  Receiver to Transmitter HTTP: ~100-300 ms
  (WiFi 2.4GHz, typical 100-500 feet)

Receiver Processing:
  HTTP request creation: ~5 ms
  HTTP response parsing: ~10-20 ms
  JSON response to browser: ~1-5 ms
  Total: ~20-30 ms

Browser Processing:
  HTML parsing: ~10-50 ms
  JavaScript execution: ~5-10 ms
  DOM updates: ~5-10 ms
  Total: ~20-70 ms

TOTAL ROUND-TRIP: ~160-435 ms
Typical: ~250 ms (acceptable for dashboard)
```

---

## Error Cases & Handling

```
Case 1: Transmitter Unreachable
  ├─ Receiver API tries HTTP GET
  ├─ HTTPClient timeout (5 second default)
  ├─ Returns error JSON: {"success":false,"error":"..."}
  ├─ Dashboard shows: "Transmitter unavailable"
  └─ No crash, graceful degradation

Case 2: Transmitter API Missing
  ├─ Receiver gets 404 response
  ├─ Catches exception, returns error
  ├─ Dashboard shows: "API not available"
  └─ Suggest firmware update

Case 3: JSON Parse Error
  ├─ Browser JavaScript catches parsing error
  ├─ Shows: "Error loading events"
  ├─ Logs to console (dev tools)
  └─ Can retry on click

Case 4: Malformed Event Data
  ├─ Invalid event enum index
  ├─ Skipped during collection (occurences == 0)
  ├─ Never reaches JSON serialization
  └─ Safe filtering prevents exposure
```

---

## Performance Characteristics

| Operation | Time | Impact |
|-----------|------|--------|
| Dashboard load (first) | ~400ms | Initial API call + HTTP |
| Dashboard auto-refresh | ~250ms | Polling every 30-60s |
| Full event page load | ~800ms | Load page + fetch + render |
| Event filtering | <10ms | Client-side JavaScript |
| Add new event (transmitter) | <1ms | Static array write |

---

## Backward Compatibility

```
✅ Fully backward compatible:
  • No changes to existing API endpoints
  • No protocol changes (MQTT/ESP-NOW untouched)
  • No changes to webserver.h public API
  • No changes to event system behavior
  • Dashboard still works if API missing (graceful degrade)

Migration path:
  1. Deploy transmitter API first (optional if testing only)
  2. Deploy receiver changes
  3. No reconfiguration needed
  4. Works with existing transmitter firmware (if has HTTP server)
```

---

**Diagram End**

For implementation details, see EVENT_LOGS_IMPLEMENTATION_ANALYSIS.md
