# Event Logs Implementation - Quick Reference Guide

## 🎯 What We're Building
A new "Event Logs" card on the receiver dashboard (root page) showing recent system events from the Battery Emulator with:
- Event count by severity level (errors, warnings, info)
- Mini summary card in System Tools section
- One-click access to full event details page

---

## 📊 Event System Overview

| Property | Details |
|----------|---------|
| **Location** | Battery Emulator (transmitter): `src/battery_emulator/devboard/utils/events.h\|cpp` |
| **Storage** | Static global array with ~130 event types max |
| **Lifetime** | Runs for device uptime (events not persistent) |
| **Data Per Event** | timestamp (64-bit), level (enum), state (enum), count (8-bit), data (8-bit) |
| **Access Functions** | `get_event_pointer()`, `get_event_enum_string()`, `get_event_message_string()` |

---

## 🏗️ Architecture (Recommended Approach)

```
Battery Emulator (Transmitter)
    ↓ (static event arrays in memory)
    NEW: /api/get_event_logs HTTP endpoint
    ↓
Receiver Dashboard
    ↓ (HTTP fetch)
    NEW: /api/get_event_logs handler (proxies to transmitter)
    ↓
    NEW: Event Logs Card in System Tools
    ↓ (JavaScript)
    Display: Error count | Warning count | Info count
```

**Why This Approach?**
- ✅ Minimal changes (2 API endpoints, 1 UI card)
- ✅ Uses existing HTTP pattern from receiver
- ✅ Only 1-2 handlers needed (13 available!)
- ✅ No protocol changes (MQTT/ESP-NOW untouched)

---

## 📝 Implementation Summary

### Transmitter Changes
**File:** `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/webserver/api/api_handlers.cpp` (or create new)

**Add Endpoint:**
```cpp
GET /api/get_event_logs?limit=50&level=all

Response JSON:
{
  "success": true,
  "event_count": 5,
  "events": [
    {
      "type": "BATTERY_OVERHEAT",
      "level": "ERROR",
      "timestamp_ms": 1234567890,
      "count": 2,
      "message": "Battery overheated. Shutting down..."
    }
  ]
}
```

### Receiver Changes

#### 1. Add API Handler
**File:** `espnowreciever_2/lib/webserver/api/api_handlers.cpp`

```cpp
// Add to uri_handler array in register_all_api_handlers():
{
    .uri = "/api/get_event_logs",
    .method = HTTP_GET,
    .handler = api_get_event_logs_handler,
    .user_ctx = NULL
},

// Implementation:
static esp_err_t api_get_event_logs_handler(httpd_req_t *req) {
    // Fetch from transmitter HTTP
    String url = "http://" + TransmitterManager::getIPString() + "/api/get_event_logs?limit=50";
    // ... HTTP GET and forward response
}
```

#### 2. Update Webserver Config
**File:** `espnowreciever_2/lib/webserver/webserver.cpp`

```cpp
// Update count:
#define EXPECTED_HANDLER_COUNT 58  // was 57

// Update max handlers (already at 70, no change needed):
.max_uri_handlers = 70,
```

#### 3. Update Dashboard Page
**File:** `espnowreciever_2/lib/webserver/pages/dashboard_page.cpp`

```html
<!-- Add to System Tools section after OTA Update card: -->
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

```javascript
// Add JavaScript function:
function loadEventLogs() {
    fetch('/api/get_event_logs')
        .then(response => response.json())
        .then(data => {
            const statusEl = document.getElementById('eventLogStatus');
            if (data.success && data.events && data.events.length > 0) {
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
                
                statusEl.innerHTML = status.replace(/\n/g, '<br>');
            } else {
                statusEl.textContent = 'No events';
            }
        })
        .catch(() => statusEl.textContent = 'Error');
}

// Call on page load:
window.addEventListener('load', loadEventLogs);
```

---

## 🎨 Visual Design

**System Tools Grid (Updated):**
```
┌─────────────┬──────────────┬─────────────────┐
│    🐛       │     📤       │       📋        │
│   Debug     │    OTA       │   Event Logs    │
│  Logging    │   Update     │                 │
│             │              │ ❌ 1 error      │
│             │              │ ⚠️ 2 warnings   │
└─────────────┴──────────────┴─────────────────┘
```

**Color Scheme:**
- Frame: Orange (#FF9800)
- Hover: Darker orange rgba(255,152,0,0.2)
- Text: Orange for title, gray for count
- Icon: 📋 (clipboard)

---

## 🔌 Handler Count Status

| Metric | Count |
|--------|-------|
| Current handlers | 57 |
| Max handlers | 70 |
| **New handlers needed** | **1** (/api/get_event_logs) |
| **Total after** | **58** |
| **Headroom remaining** | **12** |

✅ **Plenty of capacity!**

---

## 📋 File Checklist

### Files to Create/Modify

- [ ] **Transmitter:** `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/webserver/api/api_handlers.cpp`
  - Add `/api/get_event_logs` endpoint
  - Register handler in array
  
- [ ] **Receiver:** `espnowreciever_2/lib/webserver/api/api_handlers.cpp`
  - Add `/api/get_event_logs` proxy handler
  - Register handler in array
  
- [ ] **Receiver:** `espnowreciever_2/lib/webserver/webserver.cpp`
  - Update EXPECTED_HANDLER_COUNT from 57 to 58
  
- [ ] **Receiver:** `espnowreciever_2/lib/webserver/pages/dashboard_page.cpp`
  - Add Event Logs card HTML
  - Add loadEventLogs() JavaScript function
  - Call function on page load

---

## 🧪 Testing Checklist

- [ ] Transmitter API returns valid JSON with events
- [ ] Receiver can fetch from transmitter HTTP without error
- [ ] Dashboard page loads without crashes
- [ ] Event card shows event count summary
- [ ] Clicking card works (navigate to event details page)
- [ ] Graceful error when transmitter unreachable
- [ ] Handler count stays within limits (58/70)
- [ ] No memory leaks (verify heap free)

---

## 🚀 Implementation Order

1. **Transmitter API** (if not already exist)
   - Create `/api/get_event_logs` endpoint
   - Test standalone with curl
   - ~1 hour

2. **Receiver API Handler**
   - Create proxy endpoint in api_handlers.cpp
   - Register in webserver.cpp
   - Update EXPECTED_HANDLER_COUNT
   - ~30 minutes

3. **Dashboard Integration**
   - Add card HTML to dashboard_page.cpp
   - Implement loadEventLogs() JavaScript
   - Test on receiver dashboard
   - ~30 minutes

4. **Testing & Debugging**
   - Compile and upload both devices
   - Test with connected transmitter
   - Test error cases
   - ~1 hour

**Total Estimated Time:** 3-4 hours

---

## 🔗 Reference Files

**Event System (Transmitter):**
- Header: `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/devboard/utils/events.h`
- Implementation: `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/devboard/utils/events.cpp`

**Existing Display Implementation (Reference):**
- `esp32common/webserver/events_html.h|cpp` - Full event page (reference pattern)

**API Handler Patterns (Reference):**
- `espnowreciever_2/lib/webserver/api/api_handlers.cpp` - Battery type API (reference pattern)

**Dashboard Pattern (Reference):**
- `espnowreciever_2/lib/webserver/pages/dashboard_page.cpp` - System Tools section exists

---

## 📊 Event Types Summary

| Category | Examples | Count |
|----------|----------|-------|
| **CAN Communication** | Battery missing, Charger missing, Inverter missing | 5+ |
| **Battery State** | Empty, Full, Overheat, Overvoltage, Undervoltage | 10+ |
| **Contactor Issues** | Welded, Open, Fault | 3+ |
| **Cell Monitoring** | Under/Over voltage, Deviation, Critical | 5+ |
| **System Resets** | Power-on, SW, Watchdog, Panic, Brownout | 10+ |
| **Connectivity** | WiFi connect/disconnect, MQTT connect/disconnect | 4 |
| **OTA/Updates** | Update started, Timeout | 2 |
| **Other** | Task overrun, Thermal runaway, Serial errors, etc. | 90+ |

**Total:** ~130 event types defined

---

## ⚠️ Known Limitations

1. **Not Persistent** - Events only exist for device uptime (no SD/NVS storage)
2. **Limited to Active Events** - Only events with occurences > 0 are displayed
3. **No Filtering on Transmitter API** - Receiver must handle client-side filtering if needed
4. **Polling-Based** - Dashboard refreshes manually (no real-time push)
5. **100-300ms Latency** - HTTP fetch across WiFi

**Mitigations:**
- Events reset on reboot (user accepts this for MVP)
- Pagination handles large event lists
- Optional Phase 2 can add real-time via SSE
- Dashboard can implement 30+ second refresh interval

---

## 🎁 Future Enhancements (Phase 2+)

- Real-time event notifications via SSE
- Event filtering by type/level
- Event clearing functionality
- Export events to CSV
- Historical event database (NVS/SD)
- Event trend analysis

---

## 📞 Questions/Issues

**Q: Where are events stored?**  
A: Static arrays in transmitter battery emulator. Not synced to receiver.

**Q: Can events be cleared?**  
A: Yes, via `reset_all_events()` function. Can add clearing endpoint in Phase 2.

**Q: Why not use MQTT?**  
A: Simpler to implement with existing HTTP pattern. MQTT can be added later for real-time.

**Q: How many events can be stored?**  
A: ~130 event types, each with occurrence count. Limited by memory.

**Q: What happens when device reboots?**  
A: All event counts reset to 0. New events counted from restart.

---

**Ready to implement! See full analysis document for detailed technical specifications.**
