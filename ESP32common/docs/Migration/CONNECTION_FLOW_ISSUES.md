# ESP-NOW Connection Flow Issues - Analysis & Recommendations

**Date:** February 10, 2026  
**Reported Issues:**
1. Power profile continuously sending with full request messages
2. Version incompatibility warnings between devices

---

## Executive Summary

Two significant issues have been identified in the ESP-NOW connection establishment flow:

🔴 **ISSUE #1:** Receiver sends duplicate/excessive REQUEST_DATA messages on every PROBE  
🔴 **ISSUE #2:** False version incompatibility warnings despite both devices being v2.0.0

Both issues stem from improper state management during the connection handshake. The receiver sends multiple configuration requests **every time** it receives a PROBE message, rather than only on initial connection.

---

## Issue #1: Excessive REQUEST_DATA Messages

### Problem Description

The receiver sends the following messages **every time** it receives a PROBE from the transmitter:

1. `REQUEST_DATA` with `subtype_settings` (static data request)
2. `REQUEST_DATA` with `subtype_power_profile` (power profile stream request)
3. `VERSION_ANNOUNCE` (firmware version info)
4. `CONFIG_REQUEST_FULL` (configuration snapshot request)

**Why This is a Problem:**
- PROBE messages are sent every 5 seconds during discovery
- After connection is established, the discovery task suspends
- **BUT**: When the connection is lost and restored, PROBE messages resume
- Each PROBE triggers all 4 request messages → **Redundant network traffic**
- Power profile stream is continuously restarted → **No clean start/stop**

### Root Cause Analysis

**Location:** [espnow_tasks.cpp](c:\Users\GrahamWillsher\ESP32Projects\espnowreciever_2\src\espnow\espnow_tasks.cpp#L62-L104)

```cpp
// Called every time a PROBE is received (transmitter announcing itself)
probe_config.on_probe_received = [](const uint8_t* mac, uint32_t seq) {
    LOG_DEBUG("PROBE received (seq=%u) - requesting config update", seq);
    
    // Store transmitter MAC
    memcpy(ESPNow::transmitter_mac, mac, 6);
    TransmitterManager::registerMAC(mac);
    
    // Request full configuration snapshot from transmitter
    // (transmitter may have rebooted with new config)
    ReceiverConfigManager::instance().requestFullSnapshot(mac);
    
    // Request static data (IP address, settings, etc.)
    request_data_t static_req = { msg_request_data, subtype_settings };
    esp_err_t static_result = esp_now_send(mac, (const uint8_t*)&static_req, sizeof(static_req));
    // ...
    
    // Send REQUEST_DATA to ensure power profile stream is active
    request_data_t req_msg = { msg_request_data, subtype_power_profile };
    esp_err_t result = esp_now_send(mac, (const uint8_t*)&req_msg, sizeof(req_msg));
    // ...
    
    // Send version information
    version_announce_t announce;
    announce.type = msg_version_announce;
    announce.firmware_version = FW_VERSION_NUMBER;
    // ...
    result = esp_now_send(mac, (const uint8_t*)&announce, sizeof(announce));
};
```

**The Problem:**
- `on_probe_received` callback fires **EVERY TIME** a PROBE is received
- This is by design (as documented in the callback)
- However, the code sends **all initialization messages** on every PROBE
- This is inefficient and causes state thrashing

**Expected Behavior:**
- Initialization messages should be sent **ONCE** on first connection
- Subsequent PROBEs during active connection should be **ignored**
- Only send requests again when **reconnecting after timeout**

### Impact Assessment

| Scenario | Current Behavior | Expected Behavior |
|----------|------------------|-------------------|
| **Initial connection** | ✅ 4 requests sent | ✅ 4 requests sent |
| **Active connection** | ✅ Discovery suspended, no PROBEs | ✅ No PROBEs, no requests |
| **Connection lost** | ✅ Discovery resumes | ✅ Discovery resumes |
| **Reconnection** | ❌ 4 requests sent on **EVERY PROBE** (5s intervals) | ✅ 4 requests sent **ONCE** on first PROBE after timeout |
| **Transmitter reboot** | ❌ 4 requests sent on **EVERY PROBE** | ✅ 4 requests sent **ONCE** on reconnection |

**Traffic Analysis (Reconnection Scenario):**
```
T0:   Connection lost (timeout)
T1:   Discovery resumes, sends PROBE
T6:   PROBE sent → 4 requests triggered
T11:  PROBE sent → 4 requests triggered  ❌ REDUNDANT
T16:  PROBE sent → 4 requests triggered  ❌ REDUNDANT
T21:  PROBE sent → 4 requests triggered  ❌ REDUNDANT
... (continues until ACK received)
```

**Waste:** 12-16 redundant messages during typical 15-20 second reconnection

---

## Issue #2: False Version Incompatibility Warnings

### Problem Description

Both transmitter and receiver are configured with identical firmware versions:
- **Transmitter:** v2.0.0 (FW_VERSION_NUMBER = 20000)
- **Receiver:** v2.0.0 (FW_VERSION_NUMBER = 20000)

Yet the transmitter logs:
```
LOG_WARN("Version incompatible: transmitter v2.0.0, receiver v2.0.0");
```

### Root Cause Analysis

**Location:** [message_handler.cpp](c:\Users\GrahamWillsher\ESP32Projects\ESPnowtransmitter2\espnowtransmitter2\src\espnow\message_handler.cpp#L189-L192)

```cpp
router.register_route(msg_version_announce,
    [](const espnow_queue_msg_t* msg, void* ctx) {
        if (msg->len >= (int)sizeof(version_announce_t)) {
            const version_announce_t* announce = reinterpret_cast<const version_announce_t*>(msg->data);
            uint8_t rx_major = (announce->firmware_version / 10000);
            uint8_t rx_minor = (announce->firmware_version / 100) % 100;
            uint8_t rx_patch = announce->firmware_version % 100;
            
            LOG_INFO("Receiver version: v%d.%d.%d", rx_major, rx_minor, rx_patch);
            
            if (!isVersionCompatible(announce->firmware_version)) {
                LOG_WARN("Version incompatible: transmitter v%d.%d.%d, receiver v%d.%d.%d",
                         FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH,
                         rx_major, rx_minor, rx_patch);
            }
        }
    },
    0xFF, this);
```

**The Version Compatibility Function:**  
[firmware_version.h](c:\users\grahamwillsher\esp32projects\esp32common\firmware_version.h#L61-L68)

```cpp
inline bool isVersionCompatible(uint32_t otherVersion) {
    // Define compatibility range
    constexpr uint32_t MIN_COMPATIBLE = 10000;  // v1.0.0
    constexpr uint32_t MAX_COMPATIBLE = 19999;  // v1.99.99
    
    // Check if other version is within compatible range
    return (otherVersion >= MIN_COMPATIBLE && otherVersion <= MAX_COMPATIBLE);
}
```

**The Bug:**
- Compatibility range: **10000 to 19999** (v1.0.0 to v1.99.99)
- Receiver version: **20000** (v2.0.0)
- **20000 > 19999** → FALSE → **Version incompatible warning!**

**This is a hardcoded compatibility range that was never updated for v2.x firmware!**

### Configuration Evidence

**Transmitter platformio.ini:**
```ini
-D FW_VERSION_MAJOR=2
-D FW_VERSION_MINOR=0
-D FW_VERSION_PATCH=0
```
→ FW_VERSION_NUMBER = (2 × 10000) + (0 × 100) + 0 = **20000**

**Receiver platformio.ini:**
```ini
-D FW_VERSION_MAJOR=2
-D FW_VERSION_MINOR=0
-D FW_VERSION_PATCH=0
```
→ FW_VERSION_NUMBER = (2 × 10000) + (0 × 100) + 0 = **20000**

**Both devices are v2.0.0, but the compatibility check only accepts v1.x.x!**

---

## Recommended Fixes

### Fix #1: Conditional Request Logic (High Priority)

**Change:** Only send initialization messages on **first connection** or **reconnection after timeout**, not on every PROBE.

**Implementation:**

Add state tracking to distinguish first connection from subsequent PROBEs:

```cpp
// Add to setup_message_routes():
static bool initialization_sent = false;

probe_config.on_probe_received = [](const uint8_t* mac, uint32_t seq) {
    LOG_DEBUG("PROBE received (seq=%u)", seq);
    
    // Store transmitter MAC
    memcpy(ESPNow::transmitter_mac, mac, 6);
    TransmitterManager::registerMAC(mac);
    
    // Check if this is a new connection (first PROBE after being disconnected)
    static bool last_connected_state = false;
    bool is_reconnection = !last_connected_state && !initialization_sent;
    last_connected_state = true;
    
    // Only send initialization messages on first connection or reconnection
    if (is_reconnection || !initialization_sent) {
        LOG_INFO("PROBE received - sending initialization requests (reconnection=%s)", 
                 is_reconnection ? "true" : "false");
        
        // Request full configuration snapshot
        ReceiverConfigManager::instance().requestFullSnapshot(mac);
        
        // Request static data (IP address, settings, etc.)
        request_data_t static_req = { msg_request_data, subtype_settings };
        esp_now_send(mac, (const uint8_t*)&static_req, sizeof(static_req));
        
        // Send REQUEST_DATA to ensure power profile stream is active
        request_data_t req_msg = { msg_request_data, subtype_power_profile };
        esp_now_send(mac, (const uint8_t*)&req_msg, sizeof(req_msg));
        
        // Send version information (once per connection)
        version_announce_t announce;
        announce.type = msg_version_announce;
        announce.firmware_version = FW_VERSION_NUMBER;
        announce.protocol_version = PROTOCOL_VERSION;
        // ... (fill in rest of announce)
        esp_now_send(mac, (const uint8_t*)&announce, sizeof(announce));
        
        initialization_sent = true;
    } else {
        LOG_TRACE("PROBE received - already initialized, ignoring");
    }
};

// Reset flag when connection is lost
// Add this to timeout watchdog (line ~427):
if (millis() - transmitter_state.last_rx_time_ms > CONNECTION_TIMEOUT_MS) {
    transmitter_state.is_connected = false;
    ESPNow::transmitter_connected = false;
    initialization_sent = false;  // ✅ Reset for next connection
    LOG_WARN("[WATCHDOG] Transmitter connection lost (timeout: %u ms)", CONNECTION_TIMEOUT_MS);
    // ... (existing restart discovery code)
}
```

**Benefits:**
- ✅ Eliminates redundant request messages during reconnection
- ✅ Reduces network traffic by ~75% during recovery
- ✅ Cleaner connection state machine
- ✅ Power profile stream starts/stops cleanly

**File to Modify:**
- [espnow_tasks.cpp](c:\Users\GrahamWillsher\ESP32Projects\espnowreciever_2\src\espnow\espnow_tasks.cpp)

---

### Fix #2A: Update Hardcoded Compatibility Range (Quick Fix)

**Change:** Update the hardcoded compatibility range to support v2.x firmware.

**Implementation:**

```cpp
// File: firmware_version.h (line 61)

inline bool isVersionCompatible(uint32_t otherVersion) {
    // Define compatibility range
    constexpr uint32_t MIN_COMPATIBLE = 20000;  // v2.0.0  ← CHANGED
    constexpr uint32_t MAX_COMPATIBLE = 29999;  // v2.99.99  ← CHANGED
    
    // Check if other version is within compatible range
    return (otherVersion >= MIN_COMPATIBLE && otherVersion <= MAX_COMPATIBLE);
}
```

**Pros:**
- ✅ Quick fix (1 line change)
- ✅ Immediately resolves false warnings

**Cons:**
- ❌ Same problem will occur when upgrading to v3.x
- ❌ Not maintainable long-term

**File to Modify:**
- [firmware_version.h](c:\users\grahamwillsher\esp32projects\esp32common\firmware_version.h)

---

### Fix #2B: Dynamic Compatibility Range (Recommended)

**Change:** Calculate compatibility range dynamically based on current major version.

**Implementation:**

```cpp
// File: firmware_version.h (line 61)

inline bool isVersionCompatible(uint32_t otherVersion) {
    // Calculate compatibility range based on current major version
    uint16_t my_major = FW_VERSION_MAJOR;
    uint32_t min_compatible = my_major * 10000;        // Same major version minimum
    uint32_t max_compatible = (my_major + 1) * 10000 - 1;  // Up to next major version
    
    // Check if other version is within compatible range
    // Example: v2.0.0 accepts v2.0.0 to v2.99.99
    return (otherVersion >= min_compatible && otherVersion <= max_compatible);
}
```

**Alternative (Configurable Tolerance):**

```cpp
inline bool isVersionCompatible(uint32_t otherVersion, bool allow_different_major = false) {
    uint16_t my_major = FW_VERSION_MAJOR;
    uint16_t my_minor = FW_VERSION_MINOR;
    
    uint16_t other_major = otherVersion / 10000;
    uint16_t other_minor = (otherVersion / 100) % 100;
    
    if (!allow_different_major) {
        // Strict: Require same major version
        if (my_major != other_major) return false;
        
        // Allow any minor/patch version within same major
        return true;
    } else {
        // Lenient: Allow ±1 major version
        int major_diff = abs((int)my_major - (int)other_major);
        return major_diff <= 1;
    }
}
```

**Pros:**
- ✅ Automatically adapts to version upgrades
- ✅ No manual updates needed for v3.x, v4.x, etc.
- ✅ Follows semantic versioning principles
- ✅ Maintainable long-term

**Cons:**
- 🟡 Slightly more complex logic

**File to Modify:**
- [firmware_version.h](c:\users\grahamwillsher\esp32projects\esp32common\firmware_version.h)

**Recommendation:** Use **Fix #2B (Dynamic)** for long-term maintainability.

---

### Fix #3: Add Connection State Context to Callbacks (Enhancement)

**Change:** Provide connection state context to callbacks to avoid static variables.

**Implementation:**

```cpp
// Modify ProbeHandlerConfig to include state tracking
struct ProbeHandlerConfig {
    ConnectionCallback on_connection;
    ProbeReceivedCallback on_probe_received;
    bool send_ack_response;
    volatile bool* connection_flag;
    uint8_t* peer_mac_storage;
    
    // NEW: Add state tracking
    bool* initialization_sent_flag;  // Track if initialization messages sent
};

// Usage:
static bool initialization_sent = false;
probe_config.initialization_sent_flag = &initialization_sent;

probe_config.on_probe_received = [](const uint8_t* mac, uint32_t seq) {
    // Access via config instead of static
    if (!*probe_config.initialization_sent_flag) {
        // Send initialization messages
        *probe_config.initialization_sent_flag = true;
    }
};
```

**Benefits:**
- ✅ Cleaner design (no static variables)
- ✅ Better testability
- ✅ More reusable

**Files to Modify:**
- [espnow_standard_handlers.h](c:\users\grahamwillsher\esp32projects\esp32common\espnow_common_utils\espnow_standard_handlers.h)
- [espnow_tasks.cpp](c:\Users\GrahamWillsher\ESP32Projects\espnowreciever_2\src\espnow\espnow_tasks.cpp)

---

## Implementation Priority

| Fix | Priority | Effort | Impact | Order |
|-----|----------|--------|--------|-------|
| **Fix #1:** Conditional request logic | 🔴 HIGH | Medium (2-3 hours) | Eliminates redundant traffic | **1** |
| **Fix #2B:** Dynamic compatibility | 🔴 HIGH | Low (30 min) | Resolves false warnings permanently | **2** |
| **Fix #3:** State context enhancement | 🟢 MEDIUM | Medium (2 hours) | Code quality improvement | **3** |

**Total Effort:** 1 day for critical fixes

---

## Testing Plan

### Test Case 1: Initial Connection
1. Boot transmitter
2. Boot receiver
3. **Verify:** Only 1 set of initialization messages sent
4. **Verify:** No version incompatibility warnings

### Test Case 2: Reconnection After Timeout
1. Connect both devices
2. Power cycle transmitter
3. Wait for timeout (10s)
4. Transmitter reboots
5. **Verify:** Only 1 set of initialization messages sent on first PROBE
6. **Verify:** No subsequent messages on follow-up PROBEs
7. **Verify:** No version incompatibility warnings

### Test Case 3: Rapid Reconnection
1. Connect both devices
2. Power cycle transmitter 3 times (30s intervals)
3. **Verify:** Each reconnection sends exactly 1 set of messages
4. **Verify:** No message accumulation
5. **Verify:** No version warnings

### Test Case 4: Version Compatibility
1. Deploy v2.0.0 on both devices
2. **Verify:** No incompatibility warnings
3. Deploy v2.5.0 on transmitter, v2.8.0 on receiver
4. **Verify:** No incompatibility warnings (same major version)
5. Deploy v3.0.0 on transmitter, v2.0.0 on receiver
6. **Verify:** Incompatibility warning logged (different major version)

---

## Summary

### Current Issues
1. **Redundant REQUEST_DATA messages** - Sent on every PROBE instead of once per connection
2. **False version incompatibility warnings** - Hardcoded range doesn't include v2.x

### Root Causes
1. `on_probe_received` callback has no state tracking to distinguish first PROBE from subsequent ones
2. `isVersionCompatible()` has hardcoded range `10000-19999` (v1.x only)

### Recommended Actions
1. **Add initialization state tracking** to send requests only once per connection
2. **Replace hardcoded version range** with dynamic major version matching
3. **Test reconnection scenarios** thoroughly

### Expected Outcomes
- ✅ 75% reduction in reconnection traffic
- ✅ Clean power profile stream lifecycle
- ✅ No false version warnings
- ✅ Future-proof version compatibility

---

## Files Requiring Modification

1. [espnow_tasks.cpp](c:\Users\GrahamWillsher\ESP32Projects\espnowreciever_2\src\espnow\espnow_tasks.cpp) - Fix #1
2. [firmware_version.h](c:\users\grahamwillsher\esp32projects\esp32common\firmware_version.h) - Fix #2B
3. *(Optional)* [espnow_standard_handlers.h](c:\users\grahamwillsher\esp32projects\esp32common\espnow_common_utils\espnow_standard_handlers.h) - Fix #3

---

**Status:** Ready for implementation
