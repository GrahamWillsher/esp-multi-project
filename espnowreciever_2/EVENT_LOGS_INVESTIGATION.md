# Event Logs & Battery Emulator Issue Investigation Report

## Issues Identified

### 1. Missing `/api/get_event_logs` Handler Implementation

**Location**: [lib/webserver/api/api_handlers.cpp](lib/webserver/api/api_handlers.cpp#L2287)

**Problem**: 
The endpoint `/api/get_event_logs` is **registered** in the handlers list (line 2287) but the handler function `api_get_event_logs_handler` is **NOT IMPLEMENTED** anywhere in the codebase.

**Current State**:
```cpp
// Line 2287 in api_handlers.cpp
{.uri = "/api/get_event_logs", .method = HTTP_GET, .handler = api_get_event_logs_handler, .user_ctx = NULL},
```

**What Happens**:
- The dashboard page tries to call this endpoint on page load (line 539 of [dashboard_page.cpp](lib/webserver/pages/dashboard_page.cpp#L539))
- JavaScript function `loadEventLogs()` executes: `fetch('/api/get_event_logs?limit=100')`
- The handler doesn't exist → returns 404 or undefined error
- Status shows: "Connection error" or "Failed to load logs"

**Evidence**: 
Dashboard JavaScript tries to load event logs:
```javascript
async function loadEventLogs() {
    const response = await fetch('/api/get_event_logs?limit=100');
    const data = await response.json();
    
    if (data.success && data.events) {
        // ... process events
    } else {
        statusEl.textContent = data.error || 'Failed to load logs';
    }
}
```

---

### 2. "Battery Emulator Not Enabled" Message

**Root Cause**: Not part of receiver project - **This is from the transmitter, not receiver**

The receiver is the **ESP-NOW display device** (LilyGO T-Display-S3) showing data from the transmitter. The message about "battery emulator not enabled" likely comes from:
- The Battery Emulator device (separate hardware) when transmitter isn't connected to it
- Or part of the transmitter UI that transmits status to receiver

This is **NOT a receiver web UI issue**.

---

### 3. Event Logs Feature Status

**What Should Happen**:
1. Dashboard page loads
2. `loadEventLogs()` JavaScript function calls `/api/get_event_logs`
3. API returns JSON with event log data from transmitter
4. Status card shows: "X events | Y errors | Z warnings"
5. Clicking the card could navigate to full event logs view

**What Actually Happens**:
1. Dashboard loads  
2. `loadEventLogs()` executes
3. `/api/get_event_logs` returns 404 or error
4. Status shows "Connection error"
5. No link to event logs page

---

## Solution Required

### Missing Handler Implementation

Create the `api_get_event_logs_handler` function in `api_handlers.cpp`:

```cpp
static esp_err_t api_get_event_logs_handler(httpd_req_t *req) {
    // Get 'limit' parameter from query string (default 100)
    char buf[256];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char limit_str[10];
        if (httpd_query_key_value(buf, "limit", limit_str, sizeof(limit_str)) == ESP_OK) {
            // Parse limit
        }
    }
    
    // Request event logs from transmitter via ESP-NOW
    // Package into JSON response with:
    // - event_count (total events)
    // - events array with fields: timestamp, level, message
    // - success flag
    
    JsonDocument doc;
    doc["success"] = true;
    doc["event_count"] = 0;
    doc["events"] = JsonArray();
    
    String response;
    serializeJson(doc, response);
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response.c_str(), response.length());
    return ESP_OK;
}
```

---

## Architecture Notes

### Receiver Web UI Structure
```
espnowreciever_2/lib/webserver/
├── pages/
│   ├── dashboard_page.cpp  ← Event Logs card HERE
│   ├── monitor_page.cpp
│   └── ...
├── api/
│   └── api_handlers.cpp    ← Handler registration HERE
└── utils/
```

### Data Flow for Event Logs
```
1. Dashboard Page (HTML/JS)
   └── Calls /api/get_event_logs
       └── Handler (NOT IMPLEMENTED)
           └── Should fetch from Transmitter Manager
               └── Should request from transmitter via ESP-NOW/MQTT
                   └── Transmitter event logger system
```

---

## Related Configuration

- **Dashboard Event Logs Card**: Lines 305-310 of [dashboard_page.cpp](lib/webserver/pages/dashboard_page.cpp#L305)
- **JavaScript Loader**: Lines 539-569 of [dashboard_page.cpp](lib/webserver/pages/dashboard_page.cpp#L539)
- **Status Display Element**: ID `eventLogStatus` - updated on page load
- **Expected API Response Format**:
  ```json
  {
    "success": true,
    "event_count": 42,
    "events": [
      {
        "timestamp": 1234567890,
        "level": 3,
        "message": "CAN bus offline"
      },
      ...
    ]
  }
  ```

---

## Summary

**Main Issue**: The `/api/get_event_logs` endpoint handler function is **declared but not implemented**.

**Secondary**: "Battery Emulator not enabled" is a **transmitter system message**, not a receiver UI issue.

**Fix Required**: Implement `api_get_event_logs_handler()` to retrieve event logs from the transmitter and return them as JSON.
