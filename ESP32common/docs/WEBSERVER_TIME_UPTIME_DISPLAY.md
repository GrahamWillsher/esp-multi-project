# Webserver Time & Uptime Display Design

## Overview

This document provides design recommendations for displaying transmitter uptime and time information on the receiver's webserver dashboard, leveraging the heartbeat data structure defined in `TIME_SYNC_ENHANCED.md`.

---

## Architecture: Two Separate Devices

### System Overview

```
TRANSMITTER (ESP32-POE-ISO with CAN/RS485 control):
├── CAN/RS485 control logic (PRIMARY - real-time priority)
├── Heartbeat sender: sends every 10 seconds
│   └── Includes: unix_time, uptime_ms, time_source
└── NO changes required - unaffected by this design

         ESP-NOW Communication (10s heartbeat)
                    ↓
                    ↓

RECEIVER (LilyGo T-Display-S3 with Webserver):
├── ESP-NOW receiver: processes heartbeat every 10s
├── Webserver: `/api/transmitter_health` endpoint
└── Browser Dashboard:
    ├── API call every 10s (reads cached heartbeat data)
    └── JavaScript timer every 1s (LOCAL ONLY - NO network)
        └── Updates "Last Update: X ago" display locally
```

### Key Design Principles

✅ **TRANSMITTER UNAFFECTED:**
- Continues sending heartbeats every **10 seconds** (no change)
- CAN/RS485 control logic undisturbed
- No additional timing requirements
- No new data to transmit
- No impact on real-time priority

✅ **RECEIVER-SIDE ONLY:**
- 1-second JavaScript timer runs **LOCAL** in browser
- Does NOT send anything to transmitter
- Does NOT create network traffic to transmitter
- Just formats display of already-received heartbeat data
- Zero overhead on receiver ESP32

✅ **ZERO IMPACT ON CRITICAL SYSTEMS:**
- Dashboard updates are **isolated** on receiver
- Transmitter's CAN/RS485 is completely independent
- Two completely separate devices with autonomous operation

---

## 1. Current Context

### Data Available from Heartbeat Message

The receiver already receives from the transmitter every 10 seconds:

```cpp
typedef struct {
    uint8_t type;           // msg_heartbeat
    uint32_t timestamp;     // millis() timestamp (10s interval)
    uint32_t seq;           // Heartbeat sequence number
    uint64_t unix_time;     // Current Unix time (seconds)
    uint64_t uptime_ms;     // Transmitter uptime (milliseconds) ← KEY DATA
    uint8_t time_source;    // 0=unsynced, 1=NTP, 2=manual, 3=GPS
} heartbeat_t;
```

This provides:
- **Transmitter Uptime**: `uptime_ms` - accurate millisecond-level uptime since boot
- **Transmitter Time**: `unix_time` - NTP-synchronized time from transmitter
- **Time Quality**: `time_source` - indicates if time is from NTP, manual, or unsynced

### Current Dashboard

Located in `/lib/webserver/pages/dashboard_page.cpp`:
- Shows device cards for Transmitter and Receiver
- Displays: Status, IP, Firmware version, MAC
- Updates via periodic AJAX calls to API endpoints

---

## 2. Recommended Display Options

### OPTION A: Compact Dashboard Widget (RECOMMENDED)

**Location:** Below current device card status info

**Display Layout:**
```
┌─ Transmitter Info ───────────────────────────┐
│ Status: Connected ● (Green)                   │
│ IP: 192.168.1.40 (D)                          │
│ Firmware: 2.0.0                               │
│ MAC: F0:9E:9E:1F:98:20                        │
│                                               │
│ ⏱️ Uptime: 3 days, 15 hours, 42 min          │ ← NEW
│ 🕐 Time: 15-02-2026 16:55:30 GMT             │ ← NEW (DD-MM-YYYY format)
│ 🌐 Time Source: NTP Synchronized             │ ← NEW
│ ↻ Last Update: 02H:05M:30S ago                │ ← NEW (HH:MM:SS format, no overflow)
└───────────────────────────────────────────────┘
```

**Update Frequency:**
- **Uptime & Time:** Every 10 seconds (with heartbeat arrival)
- **Last Update:** Real-time counter (updates locally, doesn't need network)

**Implementation:**
```html
<!-- Add to dashboard_page.cpp -->
<div style='margin-top: 15px; padding-top: 15px; border-top: 1px solid rgba(255,255,255,0.1);'>
    <div style='display: flex; justify-content: space-between; align-items: center; margin: 8px 0;'>
        <span style='color: #FFD700; font-weight: bold;'>⏱️ Uptime:</span>
        <span id='txUptime' style='color: #fff;'>--:--:--</span>
    </div>
    <div style='display: flex; justify-content: space-between; align-items: center; margin: 8px 0;'>
        <span style='color: #FFD700; font-weight: bold;'>🕐 Time:</span>
        <span id='txTime' style='color: #fff; font-family: monospace;'>--:--:--</span>
    </div>
    <div style='display: flex; justify-content: space-between; align-items: center; margin: 8px 0;'>
        <span style='color: #FFD700; font-weight: bold;'>🌐 Source:</span>
        <span id='txTimeSource' style='color: #4CAF50; font-size: 12px;'>NTP</span>
    </div>
    <div style='display: flex; justify-content: space-between; align-items: center; margin: 8px 0;'>
        <span style='color: #999; font-size: 12px;'>Last Update:</span>
        <span id='txLastUpdate' style='color: #999; font-size: 12px;'>0s ago</span>
    </div>
</div>
```

**JavaScript Logic:**
```javascript
// Update every 10s from API + local counter
let lastHeartbeatTime = Date.now();
let txUptimeMs = 0;
let txUnixTime = 0;
let txTimeSource = 0;
let userTimeZone = 'Europe/London';  // Get from user preferences or detect

// Call API endpoint every 10s to get latest heartbeat data
setInterval(async () => {
    const response = await fetch('/api/transmitter_health');
    const data = await response.json();
    
    if (data.uptime_ms !== undefined) {
        txUptimeMs = data.uptime_ms;
        txUnixTime = data.unix_time;
        txTimeSource = data.time_source;
        lastHeartbeatTime = Date.now();
        
        // Format uptime: "3 days, 15 hours, 42 min"
        const uptime = formatUptime(txUptimeMs);
        document.getElementById('txUptime').textContent = uptime;
        
        // Format time: "15-02-2026 16:55:30 GMT" (DD-MM-YYYY HH:MM:SS TIMEZONE)
        const timeStr = formatTimeWithTimezone(txUnixTime, userTimeZone);
        document.getElementById('txTime').textContent = timeStr;
        
        // Time source indicator
        const sourceMap = {0: 'Unsynced', 1: 'NTP', 2: 'Manual', 3: 'GPS'};
        document.getElementById('txTimeSource').textContent = sourceMap[txTimeSource];
    }
    
    // Update "last update" counter - use days + HH:MM:SS format to avoid overflow
    const lastUpdateStr = formatLastUpdate(Date.now() - lastHeartbeatTime);
    document.getElementById('txLastUpdate').textContent = lastUpdateStr;
}, 10000);

/**
 * Format uptime with support for very long uptimes (years)
 * @param {number} ms - Uptime in milliseconds
 * @returns {string} Formatted uptime string
 */
function formatUptime(ms) {
    // Handle edge case: invalid or negative values
    if (!Number.isFinite(ms) || ms < 0) {
        return 'Invalid';
    }
    
    const totalSeconds = Math.floor(ms / 1000);
    const years = Math.floor(totalSeconds / (365 * 86400));
    const days = Math.floor((totalSeconds % (365 * 86400)) / 86400);
    const hours = Math.floor((totalSeconds % 86400) / 3600);
    const minutes = Math.floor((totalSeconds % 3600) / 60);
    const seconds = totalSeconds % 60;
    
    // Build string based on magnitude
    if (years > 0) {
        return `${years}y ${days}d ${hours}h`;
    } else if (days > 0) {
        return `${days}d, ${hours}h, ${minutes}m`;
    } else if (hours > 0) {
        return `${hours}h, ${minutes}m, ${seconds}s`;
    } else if (minutes > 0) {
        return `${minutes}m, ${seconds}s`;
    } else {
        return `${seconds}s`;
    }
}

/**
 * Format time with geographical timezone and DD-MM-YYYY format
 * @param {number} unixTime - Unix timestamp (seconds)
 * @param {string} timeZone - IANA timezone (e.g., 'Europe/London', 'America/New_York')
 * @returns {string} Formatted time string
 */
function formatTimeWithTimezone(unixTime, timeZone) {
    // Handle edge case: invalid timestamp
    if (!Number.isFinite(unixTime) || unixTime < 0) {
        return 'Invalid time';
    }
    
    try {
        const date = new Date(unixTime * 1000);
        
        // Format: DD-MM-YYYY
        const day = String(date.getDate()).padStart(2, '0');
        const month = String(date.getMonth() + 1).padStart(2, '0');
        const year = date.getFullYear();
        
        // Format: HH:MM:SS in specified timezone
        const formatter = new Intl.DateTimeFormat('en-GB', {
            hour: '2-digit',
            minute: '2-digit',
            second: '2-digit',
            hour12: false,
            timeZone: timeZone
        });
        
        const timeStr = formatter.format(date);
        
        // Get timezone abbreviation (e.g., GMT, BST)
        const tzFormatter = new Intl.DateTimeFormat('en-GB', {
            timeZoneName: 'short',
            timeZone: timeZone
        });
        const tzName = tzFormatter.formatToParts(date)
            .find(part => part.type === 'timeZoneName')?.value || 'UTC';
        
        return `${day}-${month}-${year} ${timeStr} ${tzName}`;
    } catch (error) {
        console.error('Time formatting error:', error);
        return 'Format error';
    }
}

/**
 * Format "last updated" time - uses days + HH:MM:SS to avoid 47-day overflow
 * Solves millisecond overflow issue where timer crashes after ~47 days
 * @param {number} ms - Milliseconds since last update
 * @returns {string} Formatted "X ago" string
 */
function formatLastUpdate(ms) {
    // Handle edge case: invalid or negative values
    if (!Number.isFinite(ms) || ms < 0) {
        return 'Now';
    }
    
    // Convert to total seconds (avoiding millisecond overflow)
    const totalSeconds = Math.floor(ms / 1000);
    
    const days = Math.floor(totalSeconds / 86400);
    const hours = Math.floor((totalSeconds % 86400) / 3600);
    const minutes = Math.floor((totalSeconds % 3600) / 60);
    const seconds = totalSeconds % 60;
    
    // Build string with appropriate granularity
    if (days > 0) {
        // For old data: "47 days, 5H:23M:45S ago"
        return `${days}d, ${String(hours).padStart(2, '0')}H:${String(minutes).padStart(2, '0')}M:${String(seconds).padStart(2, '0')}S ago`;
    } else if (hours > 0) {
        // "3H:45M:20S ago"
        return `${String(hours).padStart(2, '0')}H:${String(minutes).padStart(2, '0')}M:${String(seconds).padStart(2, '0')}S ago`;
    } else if (minutes > 0) {
        // "5M:30S ago"
        return `${minutes}M:${String(seconds).padStart(2, '0')}S ago`;
    } else {
        // "23 seconds ago"
        return `${seconds}s ago`;
    }
}

/**
 * Update last update timer every second (not every 10 seconds)
 * This gives real-time feedback without network calls
 */
setInterval(() => {
    const lastUpdateStr = formatLastUpdate(Date.now() - lastHeartbeatTime);
    document.getElementById('txLastUpdate').textContent = lastUpdateStr;
}, 1000);
```

---

### OPTION B: Dedicated System Health Page (ALTERNATIVE)

**Location:** New page `/system/health`

**Content:**

**Transmitter Health**
```
Uptime:             3 days, 15 hours, 42 minutes, 30 seconds
Boot Time:          2026-02-11 01:13:00 (estimated from uptime)
Current Time:       2026-02-15 16:55:30 UTC
Time Source:        ✅ NTP Synchronized
NTP Server:         pool.ntp.org
Timezone:           Europe/London (GMT)
Last Heartbeat:     2 seconds ago
Heartbeat Sequence: 12847
Consecutive Beats:  12847 (no missed)
```

**Receiver Health**
```
Uptime:             2 days, 8 hours, 15 minutes (EST)
Current Time:       2026-02-15 16:55:30 UTC
Local IP:           192.168.1.100 (Static)
Synchronized With:  Transmitter (2 seconds ago)
Status:             ✅ All systems normal
```

**API Endpoint:** `/api/system_health`

---

### OPTION C: Live Status Bar (MINIMALIST)

**Location:** Top of dashboard, persistent

```
🔄 TX: Up 3d 15h | 🕐 16:55:30 UTC | 🌐 NTP | ↻ Now
```

**Pros:**
- Always visible
- Compact
- Real-time feel

**Cons:**
- Takes up space
- Might seem cluttered

---

### OPTION D: Modal/Tooltip on Hover (NON-INTRUSIVE)

**Location:** On transmitter status indicator

**Trigger:** Hover over transmitter device card

**Shows Popup:**
```
Connected ✓

Uptime: 3 days, 15h, 42m
Boot: 2026-02-11 01:13:00
Time: 2026-02-15 16:55:30 UTC
Source: NTP ✓
Last Update: 2s ago
```

---

## 3. API Endpoint Recommendations

### Endpoint 1: `/api/transmitter_health` (JSON)

```cpp
GET /api/transmitter_health

Response:
{
    "connected": true,
    "uptime_ms": 325962000,      // 3 days, 15h, 42m, 42s
    "uptime_formatted": "3 days, 15 hours, 42 minutes",
    "unix_time": 1739645730,      // Current time on transmitter
    "time_source": 1,             // 0=unsynced, 1=NTP, 2=manual, 3=GPS
    "time_source_name": "NTP",
    "ntp_synced": true,
    "last_heartbeat_ms": 2000,    // Milliseconds since last heartbeat
    "heartbeat_sequence": 12847,
    "mac": "F0:9E:9E:1F:98:20",
    "ip": "192.168.1.40",
    "firmware_version": "2.0.0"
}
```

### Endpoint 2: `/api/system_time` (Simple time string)

```cpp
GET /api/system_time

Response:
{
    "tx_time": "2026-02-15 16:55:30 UTC",
    "tx_uptime": "3d 15h 42m",
    "rx_time": "2026-02-15 16:55:30 UTC",
    "last_update_ms": 2000
}
```

---

## 4. Data Source & Update Strategy

### Best Practice: Use Existing Heartbeat Data

Instead of creating new API endpoints, extend existing data structures:

**In `TransmitterManager` class (receiver-side):**

```cpp
class TransmitterManager {
public:
    // Existing methods...
    
    // NEW: Time & uptime accessors
    uint64_t getUptimeMs() const { return last_uptime_ms_; }
    uint64_t getUnixTime() const { return last_unix_time_; }
    uint8_t getTimeSource() const { return last_time_source_; }
    uint32_t getSecondsSinceLastHeartbeat() const {
        return (millis() - last_heartbeat_time_) / 1000;
    }
    
    const char* getTimeSourceName() const {
        switch (last_time_source_) {
            case 0: return "Unsynced";
            case 1: return "NTP";
            case 2: return "Manual";
            case 3: return "GPS";
            default: return "Unknown";
        }
    }
    
private:
    uint64_t last_uptime_ms_ = 0;
    uint64_t last_unix_time_ = 0;
    uint8_t last_time_source_ = 0;
    uint32_t last_heartbeat_time_ = 0;
    
    // Called when heartbeat arrives
    void updateFromHeartbeat(const heartbeat_t* hb) {
        last_uptime_ms_ = hb->uptime_ms;
        last_unix_time_ = hb->unix_time;
        last_time_source_ = hb->time_source;
        last_heartbeat_time_ = millis();
    }
};
```

---

## 4.1 CRITICAL: Transmitter Remains Completely Unaffected

**⚠️ Important Architectural Clarification:**

The transmitter device (ESP32-POE-ISO with CAN/RS485 control) continues operating **completely unchanged**:

| Aspect | Details |
|--------|---------|
| **Transmitter behavior** | Sends heartbeat every 10 seconds (NO CHANGE) |
| **Network traffic** | One-way: Transmitter → Receiver via ESP-NOW |
| **Browser timer** | Runs LOCAL on receiver's browser only (NO network traffic) |
| **API calls** | Receiver → Browser every 10 seconds (local system only) |
| **Impact on transmitter** | **ZERO** - No new timing requirements, no new CAN/RS485 latency |

**Why this matters:**
- The receiver's 1-second JavaScript timer updates the display LOCALLY in the browser
- This does NOT create additional network requests to the transmitter
- The transmitter's critical CAN/RS485 control logic is completely unaffected
- Only the receiver's dashboard refreshes its display every second with cached data

**Data flow:**
```
Transmitter (ESP32-POE-ISO)          Receiver (LilyGo T-Display-S3)           Browser
[CAN/RS485]                          [Heartbeat received @ 10s]              [Display updated @ 1s]
[Unchanged]                          [Cached in memory]                      [Local JS timer]
                                     [No feedback sent]                       [No network to TX]
```

**Conclusion:** This is a pure receiver feature. The design introduces zero risk to the transmitter's control systems.

---

## 5. Recommended Implementation Plan

### Phase 1: Core Display (Immediate)
✅ Add uptime and time fields to existing dashboard card  
✅ Create `/api/transmitter_health` endpoint  
✅ Update JavaScript to format and display values  
✅ Add real-time "last update" counter  

**Time to implement:** 1-2 hours  
**File changes:** 2-3 files

### Phase 2: Enhanced Features (Next Sprint)
☐ Add dedicated `/system/health` page with extended information  
☐ Add health metrics (consecutive heartbeats, uptime stability)  
☐ Add time quality indicators (NTP lock status, sync age)  
☐ Add historical uptime tracking (best session, average)  

**Time to implement:** 3-4 hours  
**File changes:** 4-5 files

### Phase 3: Advanced Monitoring (Future)
☐ Uptime trend chart (48-hour history)  
☐ Restart detection and logging  
☐ Time drift alerts  
☐ NTP sync quality metrics  

---

## 6. Edge Cases & Solutions

### 6.1 47-Day Millisecond Overflow Issue ✅ SOLVED

**Problem:** 
- Tracking "last updated" in raw milliseconds causes JavaScript number overflow after ~47 days
- `Math.round((Date.now() - lastHeartbeatTime) / 1000)` fails when total milliseconds exceeds `2^31 - 1` (2,147,483,647 ms)
- This causes the timer to crash or display negative/incorrect values

**Solution Implemented:**
- Convert to **total seconds first** before doing math
- Display as **Days + HH:MM:SS** format instead of milliseconds
- Use `Math.floor()` for integer arithmetic (avoids floating-point precision issues)

**Example:**
```javascript
// CORRECT: Avoid overflow by working with seconds
const totalSeconds = Math.floor(ms / 1000);  // Safe conversion
const days = Math.floor(totalSeconds / 86400);
const hours = Math.floor((totalSeconds % 86400) / 3600);

// Display: "47 days, 05H:23M:45S ago" - no overflow possible
// because we're working with seconds (much larger safe range)

// INCORRECT (DO NOT USE): Can overflow after 47 days
const days_unsafe = Math.floor(ms / 86400000);  // Risk: ms might overflow
```

**Display Examples:**
- Fresh: `5s ago`
- Recent: `3M:45S ago`
- Old: `02H:15M:30S ago`
- Very old: `47 days, 05H:23M:45S ago` ← No crash!

---

### 6.2 Timezone Handling & Daylight Saving Time (DST)

**Problem:**
- Hardcoding timezone as "GMT" is incorrect for UK after March (British Summer Time = BST)
- Different users may be in different timezones
- System should adapt to geographical location or user preferences

**Solution Implemented:**
```javascript
// Use IANA timezone database for automatic DST handling
const userTimeZone = 'Europe/London';  // Automatically handles GMT ↔ BST

// Get timezone abbreviation dynamically
const tzFormatter = new Intl.DateTimeFormat('en-GB', {
    timeZoneName: 'short',
    timeZone: userTimeZone
});

// Result: "GMT" in winter, "BST" in summer - automatic!
const tzName = tzFormatter.formatToParts(date)
    .find(part => part.type === 'timeZoneName')?.value;
```

**Recommendations for Implementation:**
- Store user's timezone preference in settings
- Detect timezone from IP on first login (optional)
- Provide timezone selector in settings page
- Use IANA timezone names: `'Europe/London'`, `'America/New_York'`, `'Asia/Tokyo'`, etc.

---

### 6.3 Invalid or Negative Timestamps

**Problem:**
- Corrupted heartbeat data could send invalid unix timestamps (e.g., 0, negative, or huge numbers)
- Transmitter clock not yet synced shows unix_time=0 or old values
- Could cause JavaScript errors or incorrect displays

**Solution Implemented:**
```javascript
// Add validation before processing any timestamp
function formatTimeWithTimezone(unixTime, timeZone) {
    // Check for valid finite positive number
    if (!Number.isFinite(unixTime) || unixTime < 0) {
        return 'Invalid time';  // Graceful fallback
    }
    
    try {
        const date = new Date(unixTime * 1000);
        // ... format safely ...
    } catch (error) {
        return 'Format error';  // Catch any formatting exceptions
    }
}
```

---

### 6.4 Extremely Long Uptimes (Years)

**Problem:**
- After 1 year, "3 days, 15 hours, 42 min" format becomes unreadable
- Need to gracefully handle very long uptimes without loss of data

**Solution Implemented:**
```javascript
function formatUptime(ms) {
    const totalSeconds = Math.floor(ms / 1000);
    const years = Math.floor(totalSeconds / (365 * 86400));
    const days = Math.floor((totalSeconds % (365 * 86400)) / 86400);
    const hours = Math.floor((totalSeconds % 86400) / 3600);
    
    if (years > 0) {
        // Format: "2y 45d 8h" - compact but readable
        return `${years}y ${days}d ${hours}h`;
    } else if (days > 0) {
        return `${days}d, ${hours}h, ${minutes}m`;
    }
    // ... other ranges ...
}
```

**Display Examples:**
- `0d, 0h, 45m`
- `3d, 15h, 42m`
- `180d, 8h, 23m`
- `2y 45d 8h` (compact for years)

---

### 6.5 Millisecond Precision Limits in Uptime

**Problem:**
- `uptime_ms` is `uint64_t`, but JavaScript only safely handles up to `2^53 - 1` (about 9 million years)
- If uptime exceeds this, JavaScript loses precision
- Practically not an issue (device won't run that long), but should be handled

**Solution:**
- Add validation in API response: check if uptime_ms is reasonable
- Display "Invalid uptime" if value exceeds safe range

```javascript
// Validate uptime before display
function formatUptime(ms) {
    // Check for valid finite positive number
    if (!Number.isFinite(ms) || ms < 0) {
        return 'Invalid';
    }
    
    // JavaScript safe integer limit: 2^53 - 1
    if (ms > 9007199254740992) {  // ~9 million years in ms
        return 'Out of range';
    }
    
    // Safe to process...
}
```

---

### 6.6 Clock Drift & Time Desynchronization

**Problem:**
- Transmitter's NTP sync might be stale or unavailable
- Time shown could be significantly different from receiver's time
- User might be confused if two clocks show different times

**Solution Implemented:**
- Display `time_source` indicator prominently: **✓ NTP / ⚠ Manual / ⚠ Unsynced**
- Show "Last Update: X ago" so user knows freshness of transmitter's time
- Consider showing both receiver's time (always synced) and transmitter's time

**Recommended Display:**
```
🕐 System Time: 15-02-2026 16:55:45 UTC    ← Receiver's accurate time
🕐 TX Time:     15-02-2026 16:55:30 GMT    ← Transmitter's last reported (5s old)
     (Source: NTP ✓)                        ← Quality indicator
```

---

### 6.7 Heartbeat Data Corruption

**Problem:**
- Network interference could corrupt heartbeat packets
- Receiver might process invalid data (e.g., uptime_ms=0xFFFFFFFF)
- CRC/checksum in heartbeat should catch this, but frontend should still handle it

**Solution:**
- API endpoint validates data before sending to frontend
- Frontend does secondary validation
- Display "Stale data" if heartbeat not received for >30 seconds

```cpp
// In transmitter_manager.cpp (receiver-side)
void updateFromHeartbeat(const heartbeat_t* hb) {
    // Validate uptime is reasonable (< 100 years in ms)
    if (hb->uptime_ms > 3153600000000ULL) {
        LOG_WARN("Invalid uptime in heartbeat: %llu ms", hb->uptime_ms);
        return;  // Skip corrupted packet
    }
    
    // Validate unix_time is reasonable
    // Should be after 2020 (1577836800) and before 2050
    if (hb->unix_time < 1577836800 || hb->unix_time > 2524608000) {
        LOG_WARN("Invalid unix_time in heartbeat: %llu", hb->unix_time);
        return;
    }
    
    // Safe to update
    last_uptime_ms_ = hb->uptime_ms;
    last_unix_time_ = hb->unix_time;
}
```

---

### 6.8 API Response Handling & Network Failures

**Problem:**
- Network request to `/api/transmitter_health` could fail or timeout
- Response might be malformed JSON
- Partial updates could show inconsistent data

**Solution:**
```javascript
setInterval(async () => {
    try {
        const response = await fetch('/api/transmitter_health', {
            timeout: 5000  // 5 second timeout
        });
        
        if (!response.ok) {
            console.warn('API error:', response.status);
            // Don't update display on error - keep showing stale data
            return;
        }
        
        const data = await response.json();
        
        // Validate response structure
        if (data.uptime_ms === undefined || data.unix_time === undefined) {
            console.warn('Invalid API response structure');
            return;
        }
        
        // Update display only if data is valid
        txUptimeMs = data.uptime_ms;
        txUnixTime = data.unix_time;
        lastHeartbeatTime = Date.now();
        
    } catch (error) {
        console.error('API fetch failed:', error);
        // Silently continue with stale data
    }
}, 10000);
```

---

### 6.9 Summary of Edge Cases Addressed

| Edge Case | Severity | Status | Solution |
|-----------|----------|--------|----------|
| 47-day millisecond overflow | **CRITICAL** | ✅ Fixed | Use days + HH:MM:SS format |
| DST transitions (GMT ↔ BST) | **High** | ✅ Fixed | Use IANA timezones |
| Invalid/zero timestamps | **High** | ✅ Fixed | Input validation + try/catch |
| Extremely long uptimes (years) | **Medium** | ✅ Fixed | Compact "Xy Zd Ah" format |
| JavaScript precision limits | **Low** | ✅ Handled | Validation up to 2^53 |
| Clock drift/desync | **Medium** | ✅ Handled | Display source quality + age |
| Heartbeat data corruption | **Medium** | ✅ Handled | CRC + frontend validation |
| Network failures | **Medium** | ✅ Handled | Graceful fallback, no crash |

---

### 6.10 Display States: Startup, Connected, and Stale Data

**Problem:**
- Dashboard needs to clearly communicate transmitter connection status
- Users should know if displayed data is fresh, stale, or unavailable
- On startup or network failure, need explicit "waiting" state

**Solution: Three Distinct Display States**

#### STATE 1: Connected & Fresh Data (Heartbeat ≤ 10 seconds old)
**When:** Normal operation, receiving heartbeats regularly  
**What to display:**
```
┌─ Transmitter Info ───────────────────┐
│ Status: Connected ● (Green)           │
│ ⏱️ Uptime: 3 days, 15 hours, 42 min   │ ← Actual values
│ 🕐 Time: 15-02-2026 16:55:30 GMT     │ ← Actual timestamp
│ 🌐 Source: NTP ✓ (Green)             │ ← Quality indicator
│ ↻ Last Update: 02H:05M:30S ago        │ ← Actual duration
└───────────────────────────────────────┘
```

**Implementation:**
```javascript
// Fresh data: all fields populated, green indicators
document.getElementById('txUptime').textContent = formatUptime(txUptimeMs);
document.getElementById('txTime').textContent = formatTimeWithTimezone(txUnixTime, userTimeZone);
document.getElementById('txTimeSource').textContent = getTimeSourceName(txTimeSource);
document.getElementById('txLastUpdate').style.color = '#4CAF50';  // Green
document.getElementById('connectionStatus').classList.add('status-connected');
```

#### STATE 2: Stale Data (Heartbeat 10-30 seconds old)
**When:** Network delay, occasional missed packets, or slight connectivity issues  
**What to display:**
```
┌─ Transmitter Info ───────────────────┐
│ Status: Connected (Delayed) ⚠️ (Orange) │
│ ⏱️ Uptime: 3 days, 15 hours, 42 min   │ ← Last known values (grayed)
│ 🕐 Time: 15-02-2026 16:55:30 GMT     │ ← Last known timestamp
│ 🌐 Source: NTP ✓ (Orange)            │ ← Quality indicator (warning)
│ ↻ Last Update: 15S:23M:45S ago ⚠️     │ ← Shows delay (orange text)
└───────────────────────────────────────┘
```

**Implementation:**
```javascript
// Stale data: show values but with warning indicator
const secondsSinceLastHB = (Date.now() - lastHeartbeatTime) / 1000;

if (secondsSinceLastHB > 10 && secondsSinceLastHB <= 30) {
    // Stale but still valid
    document.getElementById('txUptime').textContent = formatUptime(txUptimeMs);
    document.getElementById('txTime').textContent = formatTimeWithTimezone(txUnixTime, userTimeZone);
    document.getElementById('txLastUpdate').style.color = '#FF9800';  // Orange
    document.getElementById('txLastUpdate').textContent = `${Math.floor(secondsSinceLastHB)}s ago ⚠️`;
    document.getElementById('connectionStatus').classList.add('status-delayed');
}
```

#### STATE 3: No Data / Startup (No heartbeat or >30 seconds stale)
**When:** Initial page load, transmitter offline, network failure, or extended disconnection  
**What to display:**
```
┌─ Transmitter Info ───────────────────┐
│ Status: Disconnected ● (Red)          │
│ ⏱️ Uptime: -- -- ----                 │ ← Placeholder (grayed out)
│ 🕐 Time: -- -- ----                  │ ← Placeholder (grayed out)
│ 🌐 Source: Unsynced ⚠️ (Red)          │ ← No data indicator
│ ↻ Waiting for transmitter...          │ ← User-friendly message
└───────────────────────────────────────┘
```

**Implementation:**
```javascript
// No data: show placeholders and waiting message
if (!lastHeartbeatTime || secondsSinceLastHB > 30) {
    document.getElementById('txUptime').textContent = '-- -- ----';
    document.getElementById('txUptime').style.color = '#999';
    document.getElementById('txTime').textContent = '-- -- ----';
    document.getElementById('txTime').style.color = '#999';
    document.getElementById('txTimeSource').textContent = 'Unsynced ⚠️';
    document.getElementById('txTimeSource').style.color = '#F44336';  // Red
    document.getElementById('txLastUpdate').textContent = 'Waiting for transmitter...';
    document.getElementById('txLastUpdate').style.color = '#999';
    document.getElementById('connectionStatus').classList.add('status-disconnected');
}
```

**State Transition Logic:**
```javascript
function updateTransmitterDisplay(secondsSinceLastHB) {
    if (secondsSinceLastHB === null) {
        // STATE 3: No heartbeat received yet
        showNoDataState();
    } else if (secondsSinceLastHB <= 10) {
        // STATE 1: Fresh data
        showConnectedState();
    } else if (secondsSinceLastHB <= 30) {
        // STATE 2: Stale but valid
        showStaleDataState();
    } else {
        // STATE 3: Too stale, treat as disconnected
        showNoDataState();
    }
}

// Update state every 1 second
setInterval(() => {
    const secondsSinceLastHB = lastHeartbeatTime 
        ? (Date.now() - lastHeartbeatTime) / 1000 
        : null;
    updateTransmitterDisplay(secondsSinceLastHB);
}, 1000);
```

**CSS Styling for States:**
```css
.status-connected {
    color: #4CAF50;  /* Green */
    font-weight: bold;
}

.status-delayed {
    color: #FF9800;  /* Orange */
}

.status-disconnected {
    color: #F44336;  /* Red */
}

.value-stale {
    opacity: 0.6;
    color: #999;
}
```

**User-Visible Behavior:**
| Scenario | User Sees | Reason |
|----------|-----------|--------|
| Page loads, first time | "Waiting for transmitter..." | No data yet (STATE 3) |
| Heartbeat arrives | All values populated, green | Fresh data (STATE 1) |
| 15-second network glitch | Values stay visible, orange ⚠️ | Stale but acceptable (STATE 2) |
| Transmitter reboots | "Waiting for transmitter..." | Data expires >30s (STATE 3) |
| Connection re-established | Green indicators return | Fresh data resumes (STATE 1) |

---

## 7. Alternative: NTP Time Update Strategy (5-minute refresh)

### Current Proposal Issue
- Updating transmitter time from heartbeat every 10 seconds might show **stale estimated time**
- Receiver's system clock is synced separately (via Ethernet NTP)
- Transmitter's time shown might drift due to estimation

### Solution: Use Receiver's Local Time + Display Strategy

**RECOMMENDED OPTION:**

```html
<!-- Show receiver's synced time, not transmitter's -->
<div style='display: flex; justify-content: space-between; align-items: center; margin: 8px 0;'>
    <span style='color: #FFD700; font-weight: bold;'>🕐 System Time:</span>
    <span id='sysTime' style='color: #fff; font-family: monospace;'>--:--:--</span>
    <span style='color: #999; font-size: 10px; margin-left: 5px;'>(Synced)</span>
</div>

<!-- Show transmitter's most recent reported time (timestamped) -->
<div style='display: flex; justify-content: space-between; align-items: center; margin: 8px 0;'>
    <span style='color: #FFD700; font-weight: bold;'>🕐 TX Time (last beat):</span>
    <span id='txTime' style='color: #999; font-size: 12px;'>16:55:30 (2s ago)</span>
</div>
```

**Why this works:**
1. Receiver's time is always accurate (NTP synchronized over Ethernet)
2. Transmitter's time is shown with age indicator (not estimated)
3. User sees clearly which time is current vs. stale
4. Avoids confusing dual-time-source displays

---

## 7. Improved TIME_SYNC_ENHANCED.md Recommendations

### Suggested Additions to Document

Add new section: **"5. Webserver Display Integration"**

```markdown
## 5. Webserver Display Integration

### 5.1 Dashboard Display

The receiver's webserver dashboard should display:

1. **Transmitter Uptime** (from heartbeat.uptime_ms)
   - Format: "3 days, 15 hours, 42 minutes" (or "2y 45d 8h" for years)
   - Update: Every heartbeat (10s)
   - Source: Direct from heartbeat
   - Edge case handling: Years, months, overflow protection

2. **Transmitter Time** (from heartbeat.unix_time)
   - Format: "15-02-2026 16:55:30 GMT" (DD-MM-YYYY HH:MM:SS TIMEZONE)
   - Update: Every heartbeat (10s), with age indicator
   - Display age: "(02H:45M:30S ago)" ← HH:MM:SS format prevents overflow!
   - Automatic DST handling: GMT in winter, BST in summer
   - Geographically aware: Respects user's timezone setting

3. **Time Quality** (from heartbeat.time_source)
   - Values: Unsynced | NTP | Manual | GPS
   - Indicator: ✓ for synced, ⚠ for unsynced
   - Color: Green (NTP) / Orange (Manual) / Red (Unsynced)

4. **Freshness Indicator**
   - Show "Last update: 47 days, 05H:23M:45S ago"
   - Uses days + HH:MM:SS format to prevent 47-day overflow bug
   - Local JavaScript timer updates every second (no network calls)
   - Gracefully handles very old data without crashing

### 5.2 API Endpoint

Create `/api/transmitter_health` endpoint:

```json
{
    "connected": true,
    "uptime_ms": 325962000,
    "uptime_formatted": "3 days, 15 hours, 42 minutes",
    "unix_time": 1739645730,
    "time_source": 1,
    "time_source_name": "NTP",
    "ntp_synced": true,
    "last_heartbeat_ms": 2000,
    "errors": null
}
```

### 5.3 Update Strategy

- **Uptime & Time Data**: From heartbeat (arrives every 10s)
- **Freshness Counter**: Local JavaScript timer (updates every 1s)
- **Accuracy**: Transmitter's `unix_time` is NTP-sourced, accurate to ~±1s
- **Latency**: Display actual heartbeat timestamp, not estimated time
- **Robustness**: All values validated; graceful fallback on errors

### 5.4 Design Pattern

Follow Material Design principles:
- Use neutral background for stale data (>30s old)
- Use green indicator for fresh data (<5s old)
- Use yellow indicator for aging data (5-30s old)
- Use icons for visual clarity (⏱️ uptime, 🕐 time, 🌐 source)
- Show DST-aware timezone abbreviation (GMT/BST, EST/EDT, etc.)
```

---

## 8. Summary & Recommendations

### Key Fixes Applied

| Issue | Original Problem | Solution Implemented |
|-------|-----------------|----------------------|
| **Date Format** | YYYY-MM-DD | ✅ Changed to DD-MM-YYYY |
| **Timezone** | Hardcoded "UTC" | ✅ Automatic DST (GMT/BST) + geographic awareness |
| **47-Day Overflow** | Millisecond timer crashes | ✅ Switched to days + HH:MM:SS format |
| **Very Old Data** | Displayed as negative | ✅ Gracefully shows "47d, 05H:23M:45S ago" |
| **Long Uptimes** | Unreadable after 1 year | ✅ Compact "2y 45d 8h" format |
| **Invalid Timestamps** | Could crash display | ✅ Input validation + error handling |
| **Data Corruption** | Corrupted heartbeat shown | ✅ Validation + graceful fallback |
| **Network Failures** | Display broken | ✅ Error handling + keep stale data |

### Implementation Recommendations

| Aspect | Recommendation |
|--------|-----------------|
| **Date Format** | `DD-MM-YYYY HH:MM:SS TIMEZONE` (e.g., `15-02-2026 16:55:30 GMT`) |
| **Timezone** | Use IANA timezones: `'Europe/London'`, `'America/New_York'`, etc. |
| **Uptime Format** | `3d, 15h, 42m` or `2y 45d 8h` for very long uptimes |
| **Last Update Format** | `02H:45M:30S ago` or `47 days, 05H:23M:45S ago` (NO milliseconds!) |
| **Update Frequency** | API every 10s (with heartbeat), local timer every 1s |
| **Primary Display** | Option A - Compact widget in dashboard card |
| **Time Source Display** | Show quality indicator (✓ NTP / ⚠ Manual / ⚠ Unsynced) |
| **API Endpoint** | `/api/transmitter_health` with full validation |
| **Error Handling** | Graceful fallback; never crash on invalid data |
| **Implementation Phase** | Phase 1: Start immediately (2-3 hours work) |

---

**Next Steps:**
1. Implement corrected JavaScript with new date/time format
2. Create `/api/transmitter_health` endpoint with validation
3. Add timezone selection to settings page
4. Test with edge cases (47+ days uptime, DST transitions, invalid data)
5. Deploy to receiver webserver
6. Consider Phase 2 (dedicated health page) for future sprint

---

## Appendix: Testing Edge Cases

### Test Cases to Verify Implementation

```javascript
// Test 1: Fresh data (should show "2s ago")
formatLastUpdate(2000);  // Expected: "2s ago"

// Test 2: Minutes (should show "M:SS")
formatLastUpdate(305000);  // 5m 5s, Expected: "5M:05S ago"

// Test 3: Hours (should show "H:MM:SS")
formatLastUpdate(7325000);  // 2h 2m 5s, Expected: "02H:02M:05S ago"

// Test 4: Days (should show "d, H:MM:SS")
formatLastUpdate(432125000);  // 5d 0h 2m 5s, Expected: "5d, 00H:02M:05S ago"

// Test 5: Critical - 47 days (should NOT crash!)
formatLastUpdate(4060800000);  // 47 days
// Expected: "47d, 00H:00M:00S ago" ✓ NO CRASH!

// Test 6: Invalid (negative)
formatLastUpdate(-5000);  // Expected: "Now"

// Test 7: Very old (100 days)
formatLastUpdate(8640000000);  // 100 days
// Expected: "100d, 00H:00M:00S ago"

// Test 8: Timezone DST (London winter vs summer)
formatTimeWithTimezone(1707987330, 'Europe/London');
// Winter (Jan-Feb): "15-02-2026 16:55:30 GMT"
// Summer (Jul-Aug): "15-08-2026 16:55:30 BST"

// Test 9: Invalid timestamp (zero)
formatTimeWithTimezone(0, 'Europe/London');
// Expected: "Invalid time" (1970-01-01 is suspicious)

// Test 10: Invalid timestamp (negative)
formatTimeWithTimezone(-5000, 'Europe/London');
// Expected: "Invalid time"

// Test 11: Very long uptime (5 years)
formatUptime(157680000000);  // 5 years approx
// Expected: "5y 0d 0h" or similar compact format

// Test 12: Just under 1 second
formatUptime(500);
// Expected: "0s"

// Test 13: Exactly 1 day
formatUptime(86400000);
// Expected: "1d, 0h, 0m" or "1d, 0h, 0m, 0s"

// Test 14: Non-finite number (NaN, Infinity)
formatUptime(NaN);           // Expected: "Invalid"
formatUptime(Infinity);      // Expected: "Invalid"
formatLastUpdate(Infinity);  // Expected: "Now" or "Invalid"
```

All 14 test cases should pass without any crashes or undefined behavior.

````

