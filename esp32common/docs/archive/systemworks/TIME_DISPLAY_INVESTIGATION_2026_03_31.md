# Time Display Investigation — 2026-03-31

**Subject:** Receiver dashboard "Transmitter Time & Uptime" card showing `---`  
**Symptom:** Transmitter log proves correct BST time (`2026-03-31 07:38:43 BST`); receiver displays `---`  
**Status:** In investigation — fix options documented below  

---

## 1. The Transmitter Log (Evidence of Correct Operation)

```
[0d 00h 00m 06s] [info][TZ_LOOKUP] Provider success: ip-api.com -> Europe/London
[0d 00h 00m 06s] [info][TZ_CONFIG] Timezone configured: Europe/London -> GMT0BST,M3.5.0/1,M10.5.0
[0d 00h 00m 06s] [notice][TZ]       Configured: Europe/London (GMT0BST,M3.5.0/1,M10.5.0)
[0d 00h 00m 06s] [info][NTP_UTILS]  Syncing time from NTP...
[31-03-2026 07:38:43] [0d 00h 00m 06s] [info][NTP_UTILS] Time set: 2026-03-31 07:38:43 BST
```

The transmitter is functioning correctly. It:
1. Queries `ip-api.com` for geolocation → gets `Europe/London`
2. Maps that to POSIX rule `GMT0BST,M3.5.0/1,M10.5.0` (correct BST DST rule)
3. Calls `setenv("TZ", ...)` + `tzset()` and marks `timezone_configured = true`
4. Syncs NTP — the system clock now ticks in local wall-clock time (BST = UTC+1)
5. `get_formatted_time()` correctly formats this as `31/03/2026 07:38:43 BST`

The transmitter is **not** the problem. The problem is in how that information reaches the receiver.

---

## 2. What Time Data Is Actually Sent Over ESP-NOW

### 2.1 The `heartbeat_t` Struct (the only ongoing ESP-NOW time carrier)

Defined in `esp32common/espnow_transmitter/espnow_common.h`:

```cpp
typedef struct __attribute__((packed)) {
    uint8_t  type;          // msg_heartbeat
    uint32_t seq;           // Monotonic sequence number
    uint64_t uptime_ms;     // Sender uptime in milliseconds
    uint64_t unix_time;     // Unix timestamp in SECONDS (NTP-synchronized)
    uint8_t  time_source;   // 0=unsynced, 1=NTP, 2=manual, 3=GPS
    uint8_t  state;         // Connection state enum
    uint8_t  rssi;          // Last RX RSSI (0 if N/A)
    uint8_t  flags;         // Status flags
    uint32_t checksum;      // CRC32 checksum
} heartbeat_t;
```

**Critical finding:** The `unix_time` field carries raw Unix epoch seconds. This is **UTC by definition** — epoch time has no timezone embedded in it. There is **no `utc_offset` field**, **no `timezone_name`**, and **no pre-formatted `local_time` string** in the heartbeat.

Sent every **10 seconds** (configured via `TimingConfig::HEARTBEAT_INTERVAL_MS`).

### 2.2 What the Receiver Stores from the Heartbeat

`espnowreceiver_2/lib/webserver/utils/transmitter_state.cpp`:

```cpp
struct RuntimeStatus {
    bool      ethernet_connected = false;
    unsigned long last_beacon_time_ms = 0;
    bool      last_espnow_send_success = true;
    uint64_t  uptime_ms = 0;       // from heartbeat
    uint64_t  unix_time = 0;       // from heartbeat — raw epoch, UTC
    uint8_t   time_source = 0;     // from heartbeat
};
```

The receiver caches the three time-related fields. It has **no local time string and no timezone offset**. This is the complete extent of time information available to the receiver from the wireless channel.

### 2.3 Populated in Heartbeat Manager (Transmitter Side)

`ESPnowtransmitter2/.../src/espnow/heartbeat_manager.cpp`:

```cpp
hb.uptime_ms   = millis();
hb.unix_time   = TimeManager::instance().get_unix_time();
hb.time_source = static_cast<uint8_t>(TimeManager::instance().get_time_source());
```

`TimeManager::get_unix_time()` returns `time(nullptr)` — which on a POSIX system is **always UTC epoch seconds**, regardless of the `TZ` environment variable. The timezone only affects formatting functions like `localtime_r()` — not the raw epoch value. So the heartbeat's `unix_time` is always correct UTC epoch, which is good, but it carries zero timezone information.

---

## 3. The Full End-to-End Time Path

```
TRANSMITTER                                    RECEIVER
──────────────────────────────────────────     ─────────────────────────────────────────
 ethernet_utilities.cpp                           heartbeat_manager.cpp (receiver side)
   ├─ ip-api.com → "Europe/London"                  └─ on_heartbeat_rx(hb)
   ├─ setenv("TZ", "GMT0BST,...")                         └─ TransmitterState::update_time_data(
   ├─ tzset()                                                   hb.uptime_ms,
   └─ timezone_configured = true                                hb.unix_time,    ← UTC epoch
                                                                hb.time_source)
 TimeManager::get_unix_time()                           
   └─ returns time(nullptr)  ← UTC epoch        transmitter_state.cpp
                                                   └─ stores: uptime_ms, unix_time, time_source
 heartbeat_manager.cpp                                    NO timezone, NO local_time
   └─ hb.unix_time = UTC epoch  ──ESP-NOW──►
   
                                                BROWSER polls /api/transmitter_health
                                                ─────────────────────────────────────
 OtaManager::health_handler()                  api_transmitter_health_handler()
   └─ /api/health response:                      ├─ Gets uptime_ms, unix_time, time_source
       ├─ unix_time (UTC epoch)                  │    from TransmitterState (heartbeat cache)
       ├─ time_source                            │
       └─ local_time: "31/03/2026 07:38:43 BST" │  IF transmitter IP is known:
            ↑ uses localtime_r() + TZ env var   │    ├─ HTTP GET /api/health (2500ms timeout)
                                                 │    └─ extracts local_time from JSON
    ◄──────────────── HTTP over Ethernet ────────┤
                                                 │  IF HTTP succeeds: local_time populated
                                                 │  IF HTTP fails:    local_time = ""
                                                 │
                                                 └─ Returns JSON to browser:
                                                      ├─ uptime_ms
                                                      ├─ unix_time
                                                      ├─ time_source
                                                      └─ local_time  ← "" if proxy failed

DASHBOARD JS
─────────────
 resolveTransmitterTimeDisplay(timeData.local_time)
   ├─ if local_time is falsy or "Time not synced" → returns "---"
   └─ otherwise → returns the string verbatim
```

### 3.1 Two Completely Different Transport Channels

| Channel | Protocol | Frequency | What it carries | Timezone info |
|---------|----------|-----------|-----------------|---------------|
| ESP-NOW heartbeat | Wireless | Every 10s | `unix_time` (epoch), `uptime_ms`, `time_source` | ❌ None |
| HTTP proxy to `/api/health` | Ethernet/WiFi | On-demand (per browser poll) | `local_time` string, `unix_time`, `time_source` | ✅ Yes (pre-formatted) |

The receiver dashboard depends on the **HTTP proxy path** to get displayable local time. If that path fails, the display falls back to `---`.

---

## 4. Root Cause of `---` on the Dashboard

The dashboard currently shows `---` when `timeData.local_time` is empty or absent. There are two independent reasons this can happen:

### 4.1 The HTTP Proxy Can Fail Silently

The proxy in `api_transmitter_health_handler` has **no cache**. If the single HTTP request to the transmitter's `/api/health` fails — for any reason — `local_time` is set to `""` and the dashboard shows `---`.

**Failure conditions:**

| Condition | Effect |
|-----------|--------|
| Transmitter IP not yet known by receiver | Proxy skipped entirely → `local_time = ""` |
| Transmitter HTTP server busy / backlogged | Timeout (2500ms) → `local_time = ""` |
| Transmitter restarting / OTA in progress | Connection refused → `local_time = ""` |
| Browser poll rate faster than proxy TTL | Intermittent `---` flicker |
| Transmitter not flashed with new `/api/health` | Response has no `local_time` key → `local_time = ""` |

### 4.2 Firmware May Not Be Deployed

At the time of this writing, the last known upload attempts failed (COM6 not available). If either the transmitter or receiver is running firmware **without** the changes described in this document, the chain breaks:

- **Transmitter without new `health_handler`**: `/api/health` response has no `local_time` field → proxy gets nothing
- **Receiver without new `api_transmitter_health_handler`**: no proxy is attempted, `local_time` never appears in API response

### 4.3 Structural Design Problem

The HTTP proxy path is **fragile by design**: it is a synchronous, uncached, blocking HTTP request injected into every browser poll cycle. This means:

- Time accuracy on the dashboard is directly coupled to Ethernet health and transmitter HTTP availability
- A single failed poll causes an immediate regression to `---`
- There is no graceful degradation to the epoch data that IS reliably received via ESP-NOW

---

## 5. Previous Bugs Identified and Fixed

These bugs are **already resolved in the codebase** (lines confirm the fixes exist):

### Bug 1: `timezone_configured` Never Set by Geolocation Path

**File:** `ethernet_utilities.cpp`  
**Original behaviour:** `configure_timezone_from_location_internal()` called `setenv("TZ", posix_tz, 1)` and `tzset()` but never set `timezone_configured = true`. On the next call to `get_ntp_time()`, the check `if (!timezone_configured)` was still true, causing it to overwrite the geolocation timezone with the default (`UTC0`).

**Sequence that caused 1-hour offset:**
```
t=6s: ip-api.com → GMT0BST → setenv("TZ","GMT0BST,...") ← CORRECT
t=6s: get_ntp_time() → timezone_configured is false → setenv("TZ","UTC0") ← OVERWRITES BST!
t=6s: NTP sync → time set but TZ is now UTC → timestamps 1 hour behind
```

**Fix applied (line 716):**
```cpp
timezone_configured = true;   // ← added inside configure_timezone_from_location_internal()
```

**Current behaviour (confirmed by logs):** Timezone stays as `GMT0BST,M3.5.0/1,M10.5.0` and NTP sync correctly shows BST.

### Bug 2: Dashboard Always Rendered UTC

**File:** `dashboard_page_script.cpp`  
**Original behaviour:** Fallback path called `formatTimeWithTimezone(timeData.unix_time, 'UTC')` — the `'UTC'` string was hardcoded, so even with correct epoch time, the displayed time was always UTC regardless of the transmitter's actual timezone.

**Fix applied:** Removed all epoch-based fallback rendering. Dashboard now exclusively uses `timeData.local_time` from the transmitter.

---

## 6. UTC Offset Calculation Frequency Analysis

### 6.1 How Often Does the Heartbeat Fire?

From `esp32common/include/esp32common/config/timing_config.h`:

```cpp
constexpr HeartbeatTiming HEARTBEAT{
    10000,   // interval_ms  ← heartbeat fires every 10 seconds
    30000,   // timeout_ms
    35000,   // tx_timeout_ms
    30000,   // espnow_connecting_timeout_ms
    1000,    // ack_timeout_ms
};
constexpr uint32_t HEARTBEAT_INTERVAL_MS = HEARTBEAT.interval_ms;  // 10,000 ms
```

### 6.2 How Often Does NTP Sync Run?

From `ethernet_utilities.h`:

```cpp
#define NTP_SYNC_INTERVAL_MS (30 * 60 * 1000)  // 30 minutes
```

The NTP skip guard in `get_ntp_time()` uses this — if time was synced less than 30 minutes ago, the function returns immediately without hitting the NTP server. The timezone geolocation refresh interval also uses this same value (`kTimezoneRefreshIntervalMs = NTP_SYNC_INTERVAL_MS`).

From `timing_config.h`, the background task's outer NTP check uses:
```cpp
constexpr TimeSyncTiming TIME_SYNC{ 3600000 };  // 1 hour between background task NTP checks
constexpr uint32_t NTP_RESYNC_INTERVAL_MS = TIME_SYNC.ntp_resync_interval_ms;
```

So in practice: NTP background task checks every **1 hour**, NTP `get_ntp_time()` short-circuits if synced within **30 minutes**.

### 6.3 How Often Does the UTC Offset Actually Change?

The UTC offset is governed entirely by the `TZ` environment variable. It changes only when:

| Event | Frequency | Trigger |
|-------|-----------|---------|
| First NTP sync sets default TZ | Once per boot | `get_ntp_time()` first call |
| Geolocation updates TZ | Once per boot (retried on failure) | `configure_timezone_from_location_internal()` |
| **DST spring-forward (UK)** | **Once per year** | Last Sunday of March at 01:00 UTC |
| **DST fall-back (UK)** | **Once per year** | Last Sunday of October at 01:00 UTC |

The offset is therefore **stable for approximately 6 months at a time**. A DST transition only matters if the device is running continuously across a midnight boundary in late March or late October.

### 6.4 Does `utc_offset_min` Need Recalculating Every Heartbeat?

**No.** Recalculating it per heartbeat (every 10 seconds) is:
- **180× more frequent** than the NTP sync guard (30 min)
- **360× more frequent** than the NTP background task (1 hr)
- **~1,576,800× more frequent** than the offset actually changes (twice a year)

The computation (`localtime_r` + `gmtime_r` + `mktime`) is cheap — a few microseconds — so it would not cause any measurable CPU or timing impact. But it is architecturally untidy and unnecessary.

### 6.5 Correct Design: Cached Value, Updated on TZ Change

The UTC offset should be **cached as a module-level variable** in `ethernet_utilities.cpp` and **updated only when the `TZ` environment changes** — which happens in exactly two places already:

1. **`get_ntp_time()`** — when it sets the default TZ on first call (line ~557–562)
2. **`configure_timezone_from_location_internal()`** — after geolocation succeeds and calls `setenv("TZ", posix_tz, 1) + tzset()` (line ~712–716)

This is identical to how `detected_timezone_abbreviation` is already cached in the same file and updated by `refresh_detected_timezone_abbreviation_from_system_time()` — called from both of the same two places.

**Implementation sketch:**

```cpp
// ethernet_utilities.cpp — add alongside other cached state
static int16_t cached_utc_offset_min = 0;

static void refresh_cached_utc_offset() {
    time_t now = time(nullptr);
    if (now <= 0) return;                  // clock not set yet
    struct tm local_tm{}, gm_tm{};
    localtime_r(&now, &local_tm);          // wall-clock time in current TZ
    gmtime_r(&now, &gm_tm);               // UTC breakdown
    const time_t local_t = mktime(&local_tm);
    const time_t gm_t    = mktime(&gm_tm);
    cached_utc_offset_min = static_cast<int16_t>(
        static_cast<int32_t>(difftime(local_t, gm_t)) / 60
    );
}

int16_t get_cached_utc_offset_min() {
    return cached_utc_offset_min;
}
```

Called from both `setenv/tzset` sites (already marked with `timezone_configured = true` and `refresh_detected_timezone_abbreviation_from_system_time()`).

In `heartbeat_manager.cpp`, the assignment becomes a **single integer read**:

```cpp
hb.utc_offset_min = get_cached_utc_offset_min();   // zero-cost: reads a static int16
```

No computation, no syscalls, no overhead per heartbeat.

### 6.6 Handling DST Transitions at Runtime

Since the offset is cached and refreshed only on NTP sync, what happens during a DST transition while the device is running?

- The `TZ` POSIX rule (`GMT0BST,M3.5.0/1,M10.5.0`) already encodes the exact DST transition schedule. When the system clock crosses the transition time, `localtime_r()` automatically returns the new offset — **the `TZ` string does not need to change**.
- Therefore `refresh_cached_utc_offset()` will return the new offset on its next call after the transition time passes.
- Since NTP resyncs every 30 minutes to 1 hour, the cached offset will be corrected within **at most 1 hour** of a DST transition.
- Given that DST transitions happen at 01:00 UTC (02:00 BST → 03:00 BST in spring; 02:00 BST → 01:00 GMT in autumn), a 1-hour lag is well within acceptable tolerance. The user already confirmed sub-hour accuracy is not a concern.

**Alternatively**, `refresh_cached_utc_offset()` can also be called from the background task's periodic NTP check path, ensuring the cached value is refreshed on every NTP sync cycle regardless.

---

## 7. Options for Resolution

### Option A: Add UTC Offset to the ESP-NOW Heartbeat Struct ⭐ RECOMMENDED

Extend `heartbeat_t` with a `utc_offset_minutes` field (int16, signed, 2 bytes):

```cpp
typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint32_t seq;
    uint64_t uptime_ms;
    uint64_t unix_time;        // UTC epoch seconds (unchanged)
    int16_t  utc_offset_min;   // NEW: UTC offset in minutes (e.g. BST = +60, EST = -300)
    uint8_t  time_source;
    uint8_t  state;
    uint8_t  rssi;
    uint8_t  flags;
    uint32_t checksum;
} heartbeat_t;
```

The receiver already stores `unix_time`. With the UTC offset, it can convert to local time in JavaScript:

```javascript
const localEpochMs = (timeData.unix_time + timeData.utc_offset_min * 60) * 1000;
const d = new Date(localEpochMs);
// format d as local time
```

**Transmitter populates it from `ethernet_utilities`:**

```cpp
// In heartbeat_manager.cpp:
hb.utc_offset_min = static_cast<int16_t>(get_utc_offset_seconds() / 60);
```

Where `get_utc_offset_seconds()` reads the current TZ-adjusted offset using `mktime`/`gmtime`.

**Pros:**
- ✅ Reliable — delivered on every ESP-NOW heartbeat, no HTTP required
- ✅ Offline-capable — works even if Ethernet is down on either side
- ✅ Lightweight — only 2 bytes added to the struct
- ✅ Receiver can always display correct local time
- ✅ Struct is backwards-compatible if you only add to the end... though since it's packed, care needed with existing receivers
- ✅ No added latency to browser polls
- ✅ Graceful degradation: if `time_source == 0` (unsynced), receiver shows `---`; if synced, shows correctly computed local time

**Cons:**
- ⚠ Requires re-flashing both transmitter and receiver (struct change breaks wire compatibility)
- ⚠ DST transitions happen at the instant the transmitter sends the new offset; there's a ~10 second lag window

---

### Option B: Cache the Last Known `local_time` in the Receiver (Quick Fix)

Add a persistent `local_time` string cache to `TransmitterState`. Update it whenever the HTTP proxy succeeds. Serve the cached value on subsequent polls even if the proxy fails.

```cpp
// In transmitter_state.h / .cpp
static char cached_local_time[48];
static unsigned long cached_local_time_age_ms;

void update_local_time_cache(const char* local_time);
const char* get_cached_local_time();
```

The proxy code becomes:
```cpp
if (proxied_local_time) {
    TransmitterState::update_local_time_cache(doc["local_time"]);
} else {
    // Use cache if recent enough
    const char* cached = TransmitterState::get_cached_local_time();
    if (cached && strlen(cached)) doc["local_time"] = cached;
}
```

**Pros:**
- ✅ No struct changes — no re-flash of transmitter needed (just receiver)
- ✅ Tolerates brief HTTP failures
- ✅ Browser always sees something sensible

**Cons:**
- ❌ Still depends on HTTP proxy for initial value and refresh
- ❌ Stale cached time drifts once cache is a few seconds old (you'd display "07:38:43 BST" indefinitely if HTTP keeps failing)
- ❌ Cache invalidation: when does stale become misleading?
- ❌ Root structural problem remains — epoch time is available every 10s but unused

---

### Option C: Pre-Formatted Local Time String in Heartbeat

Add a `char local_time[24]` field directly to `heartbeat_t`, containing a compact pre-formatted string (e.g. `"2026-03-31T07:38:43+01"`).

**Pros:**
- ✅ Dashboard needs zero arithmetic — just display the string
- ✅ Timezone display name can be embedded too

**Cons:**
- ❌ 24 bytes added to every heartbeat — significant given ESP-NOW 250-byte max payload
- ❌ Wire format change requires simultaneous reflash of both devices
- ❌ String formatting on transmitter side on every heartbeat (minor CPU cost)
- ❌ String encoding/locale issues if dashboard ever needs to parse it

---

### Option D: Keep HTTP Proxy — Fix It Properly (No Struct Change)

Improve the existing proxy with:
1. Background periodic refresh (separate task, 30s interval)
2. Cached result served immediately from memory
3. Fall back to epoch → UTC conversion with explicit UTC label only when cache is very stale (> 5 min)

**Pros:**
- ✅ No struct change — works with current heartbeat
- ✅ No receiver reflash for time data (just proxy logic)

**Cons:**
- ❌ Background HTTP task adds complexity and memory
- ❌ Ethernet dependency remains — no Ethernet = stale or unknown time
- ❌ Does nothing for the offline/ESP-NOW-only scenario

---

## 7. Recommendation

**Implement Option A (UTC offset in heartbeat) as the primary fix.**  
**Implement Option B (receiver-side cache) as a temporary bridge** until both devices are reflashed.

### Why Option A is the Right Long-Term Approach

The transmitter knows its UTC offset the moment NTP syncs and geolocation completes. That offset is small (int16, 2 bytes), stable (changes only at DST transitions), and essential for any consumer of the time data. It belongs in the heartbeat because **the heartbeat is the reliable, always-on, mesh-agnostic channel between the two devices**. HTTP depends on Ethernet, WiFi routing, and both webservers being up. The ESP-NOW heartbeat has none of those dependencies.

The BST problem only surfaced because the receiver had to guess the timezone from a raw epoch timestamp — a guess it was making incorrectly. Sending the offset eliminates the guess entirely.

### Minimal Impact Wire Change

The heartbeat struct change is backward-incompatible (packed struct, not versioned). However, the system already has a version exchange mechanism (`msg_version_announce`, `msg_version_response`). A version bump enforces both devices are updated before time display is trusted.

---

## 8. Proposed Implementation Plan

### Step 1: Add `utc_offset_min` to `heartbeat_t` in esp32common

```cpp
// esp32common/espnow_transmitter/espnow_common.h
typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint32_t seq;
    uint64_t uptime_ms;
    uint64_t unix_time;
    int16_t  utc_offset_min;   // ← ADD HERE (after unix_time, before time_source)
    uint8_t  time_source;
    uint8_t  state;
    uint8_t  rssi;
    uint8_t  flags;
    uint32_t checksum;
} heartbeat_t;
```

### Step 2: Add Cached Offset to `ethernet_utilities` (Not Recomputed Per Heartbeat)

The offset is **cached**, updated only when `TZ` changes — on NTP default-TZ setup and on geolocation success. This avoids any computation in the heartbeat send path.

```cpp
// ethernet_utilities.h
int16_t get_cached_utc_offset_min();  // returns cached TZ offset in whole minutes

// ethernet_utilities.cpp  — add alongside other static state vars
static int16_t cached_utc_offset_min = 0;

static void refresh_cached_utc_offset() {
    time_t now = time(nullptr);
    if (now <= 0) return;
    struct tm local_tm{}, gm_tm{};
    localtime_r(&now, &local_tm);
    gmtime_r(&now, &gm_tm);
    const time_t local_t = mktime(&local_tm);
    const time_t gm_t    = mktime(&gm_tm);
    cached_utc_offset_min = static_cast<int16_t>(
        static_cast<int32_t>(difftime(local_t, gm_t)) / 60
    );
}

int16_t get_cached_utc_offset_min() {
    return cached_utc_offset_min;
}
```

Call `refresh_cached_utc_offset()` in the two existing `setenv/tzset` sites (alongside the existing `refresh_detected_timezone_abbreviation_from_system_time()` calls).

**Refresh frequency matches NTP sync: every 30 minutes to 1 hour.** DST transitions are covered because the POSIX TZ rule already encodes the transition schedule — `localtime_r` returns the new offset automatically once the wall-clock crosses the transition boundary.

### Step 3: Populate `utc_offset_min` in Transmitter `heartbeat_manager.cpp`

```cpp
hb.utc_offset_min = get_cached_utc_offset_min();   // single integer read — zero cost per heartbeat
```

### Step 4: Store `utc_offset_min` in Receiver `TransmitterState`

```cpp
// transmitter_state.h
void update_time_data(uint64_t uptime_ms, uint64_t unix_time,
                      int16_t utc_offset_min, uint8_t time_source);
int16_t get_utc_offset_min();

// transmitter_state.cpp
struct RuntimeStatus {
    ...
    int16_t utc_offset_min = 0;   // ← ADD
};
```

### Step 5: Update Receiver API to Include `utc_offset_min`

```cpp
// api_telemetry_handlers.cpp - api_transmitter_health_handler
doc["utc_offset_min"] = TransmitterManager::getUtcOffsetMin();
```

### Step 6: Update Dashboard JS

```javascript
function resolveTransmitterTimeDisplay(timeData) {
    const source = timeData.time_source;
    if (!source || source === 0) return '---';  // unsynced

    const unix = timeData.unix_time;
    const offset = timeData.utc_offset_min || 0;
    if (!unix) return '---';

    // Convert UTC epoch to local wall-clock time using the transmitter's offset
    const localMs = (unix + offset * 60) * 1000;
    const d = new Date(localMs);

    const day   = String(d.getUTCDate()).padStart(2, '0');
    const mon   = String(d.getUTCMonth() + 1).padStart(2, '0');
    const year  = d.getUTCFullYear();
    const hh    = String(d.getUTCHours()).padStart(2, '0');
    const mm    = String(d.getUTCMinutes()).padStart(2, '0');
    const ss    = String(d.getUTCSeconds()).padStart(2, '0');
    const sign  = offset >= 0 ? '+' : '-';
    const absH  = String(Math.floor(Math.abs(offset) / 60)).padStart(2, '0');
    const absM  = String(Math.abs(offset) % 60).padStart(2, '0');

    return `${day}/${mon}/${year} ${hh}:${mm}:${ss} UTC${sign}${absH}:${absM}`;
}
```

> Note: If `local_time` string is still proxied from HTTP, it can be preferred over the computed value — but the computed value is the reliable fallback.

---

## 9. Interim Fix (Until Reflash Is Possible)

If the current firmware cannot be reflashed immediately, an interim mitigation is to add a **time cache** to the receiver so that once the HTTP proxy succeeds even once, the cached `local_time` string is reused for subsequent polls. This prevents the `---` flicker and means the time will be correct after the first successful proxy fetch, even if subsequent fetches fail.

See Option B above for implementation sketch. This can be done in `api_telemetry_handlers.cpp` alone without any struct or heartbeat changes.

---

## 10. Files to Change (Summary)

| File | Change | Affects |
|------|--------|---------|
| `esp32common/espnow_transmitter/espnow_common.h` | Add `utc_offset_min` (int16) to `heartbeat_t` | Both devices |
| `ESPnowtransmitter2/.../lib/ethernet_utilities/ethernet_utilities.h/.cpp` | Add `cached_utc_offset_min` static var + `refresh_cached_utc_offset()` + `get_cached_utc_offset_min()`; call refresh in both `setenv/tzset` sites | Transmitter |
| `ESPnowtransmitter2/.../src/espnow/heartbeat_manager.cpp` | Set `hb.utc_offset_min = get_cached_utc_offset_min()` — single integer read, zero per-heartbeat cost | Transmitter |
| `espnowreceiver_2/.../lib/webserver/utils/transmitter_state.h/.cpp` | Store `utc_offset_min` from heartbeat | Receiver |
| `espnowreceiver_2/.../lib/webserver/api/api_telemetry_handlers.cpp` | Expose `utc_offset_min` in JSON; add time cache | Receiver |
| `espnowreceiver_2/.../lib/webserver/pages/dashboard_page_script.cpp` | Replace `local_time` string display with epoch + offset calculation | Receiver |

The `local_time` HTTP proxy can remain as an **optional enhancement** (for a richer display including timezone abbreviation like "BST"), but should no longer be the **sole** source of time data.

---

## 11. Geolocation Status Indicator — When DST Data Is Unavailable

### 11.1 The Problem

When the transmitter boots and geolocation has **not yet been confirmed**, the timezone
defaults to **UTC** (`EthernetConfig::NTP::DEFAULT_POSIX_TZ = "UTC0"`). This has two
consequences:

1. **`utc_offset_min` in the heartbeat is `0`** — the dashboard displays UTC time, which is
   wrong for any region with a non-zero offset (e.g. UK in BST = UTC+1 is shown 1 hour behind).
2. **The `time_transitions_snapshot_t` shows `timezone_name = "UTC"` with `transition_count = 0`** —
   UTC has no DST, so the transitions card appears to show no upcoming changes. There is no
   distinction between "no DST in this timezone" and "timezone not yet confirmed."

Neither of these shows any indication to the user that the data is provisional.

### 11.2 The Authoritative State Variable

In `ethernet_utilities.cpp`, the private static variable:

```cpp
// Line 40 — set to false at module init
static bool timezone_auto_detected = false;
```

tracks whether geolocation has ever succeeded. It is:

- **`false`** at boot — until `configure_timezone_from_location_internal()` succeeds
- **`true`** only after all three geolocation providers have been tried and one succeeds
  (line 857: `timezone_auto_detected = true;`)
- **Never reverted** to `false` once confirmed (the TZ rule may be refreshed, but it stays confirmed)

A separate flag `timezone_configured` becomes `true` on first NTP call (when the default UTC
timezone is applied), but this does **not** indicate geolocation — only that `setenv("TZ", ...)` has
been called at least once.

| Flag | Set when | Means |
|------|----------|-------|
| `timezone_configured` | First NTP call (default TZ applied) | TZ env var has been set to something |
| `timezone_auto_detected` | Geolocation provider returns success | TZ is confirmed to match device location |

**The receiver currently has no awareness of `timezone_auto_detected` at all.** It only receives
`utc_offset_min` and `time_source` via the heartbeat, and the timezone snapshot via the event-driven
message. Neither carries a geolocation flag.

### 11.3 Timing of Geolocation

| Phase | What happens | Typical duration |
|-------|-------------|-----------------|
| Boot | Network connection established | 1–3 s |
| Boot | `get_ntp_time()` sets default TZ (`UTC0`) | Immediate |
| Boot | `configure_timezone_from_location_internal()` called | 1–6 s (HTTP to provider) |
| If providers fail | Retry every `kTimezoneRetryDelayMs = 30 s` | Repeated until success |
| Once confirmed | Refresh every `kTimezoneRefreshIntervalMs = 30 min` | Background task |

**Implication:** On a device with normal internet access, `timezone_auto_detected` transitions to
`true` within seconds of the Ethernet link coming up. On a device without internet (or with all
three geolocation providers blocked), it stays `false` indefinitely — and the time display will
remain at UTC offset for the lifetime of that boot.

### 11.4 Recommended Transport — Two Independent Layers

#### Layer 1: Use the existing `heartbeat_t.flags` field (no struct size change)

The `heartbeat_t` struct already has a `uint8_t flags` field that is currently always set to `0`:

```cpp
// espnow_common.h — heartbeat_t (unchanged — flags field already exists)
uint8_t  flags;         // Status flags (e.g., low_batt, degraded)
```

In `heartbeat_manager.cpp`:
```cpp
hb.flags = 0;   // ← currently hardcoded zero
```

**No wire format change is needed.** Define the bit and start using it:

```cpp
// esp32common/espnow_transmitter/espnow_common.h — add with heartbeat_t definition

// Heartbeat flags bit definitions
namespace HeartbeatFlags {
    constexpr uint8_t GEO_VALID = 0x01;  // bit 0: geolocation confirmed (timezone_auto_detected == true)
    // bits 1–7 reserved for future use
}
```

In `ethernet_utilities.h/.cpp`, add an accessor for the private `timezone_auto_detected` variable:

```cpp
// ethernet_utilities.h — new declaration
/**
 * @brief Returns true if the timezone has been confirmed by geolocation service.
 * Returns false if the device is using the default fallback timezone (UTC).
 */
bool is_geolocation_configured();

// ethernet_utilities.cpp — implementation
bool is_geolocation_configured() {
    return timezone_auto_detected;
}
```

In `heartbeat_manager.cpp`, populate the flags field:

```cpp
hb.flags = is_geolocation_configured() ? HeartbeatFlags::GEO_VALID : 0;
```

**Why this is the best channel for the dashboard:** The heartbeat arrives every 10 seconds. The
receiver is immediately aware of geolocation status on every heartbeat cycle, with no latency beyond
one heartbeat interval.

#### Layer 2: Add `flags` field to `time_transitions_snapshot_t` (minor struct change)

The debug page needs to know geolocation status independently (it fetches from a different API than
the dashboard). Adding a `flags` byte to the snapshot struct captures geo status at the time of
snapshot generation:

```cpp
// esp32common/espnow_transmitter/espnow_common.h — update time_transitions_snapshot_t

// Time-snapshot flags bit definitions
namespace TimeSnapshotFlags {
    constexpr uint8_t GEO_VALID = 0x01;  // bit 0: geolocation confirmed at snapshot time
}

typedef struct __attribute__((packed)) {
    uint8_t type;                       // msg_time_transitions_snapshot
    uint8_t flags;                      // ← NEW: TimeSnapshotFlags bits
    uint8_t transition_count;           // 0..TIME_TRANSITION_MAX
    uint16_t revision;                  // Monotonic revision (increments on content change)
    uint32_t generated_unix_utc;        // UTC epoch when this snapshot was generated
    char timezone_name[40];             // e.g. "Europe/London" / "UTC"
    char timezone_abbrev[8];            // e.g. "BST" / "UTC"
    int16_t current_utc_offset_min;     // Current UTC offset in minutes
    time_transition_t transitions[TIME_TRANSITION_MAX];
    uint32_t checksum;                  // CRC32 over all fields except this checksum
} time_transitions_snapshot_t;
```

> **Note on struct layout:** Inserting `flags` as the second byte (after `type`) is clean because
> it occupies the alignment gap that would otherwise be wasted padding. The total size increases by
> 1 byte. Since both transmitter and receiver are always reflashed together for protocol changes, this
> is safe.

In `ethernet_utilities.cpp`, set it in `refresh_time_transition_snapshot_cache()`:

```cpp
next.flags = timezone_auto_detected ? TimeSnapshotFlags::GEO_VALID : 0;
```

Also update the `TimeTransitionSnapshot` local struct in `ethernet_utilities.h` to add a matching
`bool geolocation_valid` field for easy use within the transmitter code:

```cpp
struct TimeTransitionSnapshot {
    bool valid = false;
    bool geolocation_valid = false;   // ← NEW: mirrors timezone_auto_detected at snapshot time
    uint16_t revision = 0;
    ...
};
```

### 11.5 Receiver Changes — Storing and Exposing Geo Status

#### Receiver TransmitterState / TransmitterManager changes

```cpp
// transmitter_state.h — add to RuntimeStatus struct
uint8_t heartbeat_flags = 0;   // raw flags byte from heartbeat

// transmitter_state.h — new function declarations
void update_heartbeat_flags(uint8_t flags);
uint8_t get_heartbeat_flags();
bool is_geolocation_valid();   // returns (heartbeat_flags & HeartbeatFlags::GEO_VALID) != 0

// transmitter_manager.h — new static wrappers
static uint8_t getHeartbeatFlags();
static bool isGeolocationValid();
```

`update_heartbeat_flags()` is called from the heartbeat receive path (alongside the existing
`update_time_data()` call in `heartbeat_manager.cpp` on the receiver side).

#### `/api/transmitter_health` changes

```cpp
// api_telemetry_handlers.cpp — inside api_transmitter_health_handler
doc["geolocation_valid"] = TransmitterManager::isGeolocationValid();
```

This makes it available to the dashboard JS, which already polls this endpoint every 2 seconds.

#### `/api/transmitter_time_transitions` changes

```cpp
// api_debug_handlers.cpp — inside api_transmitter_time_transitions_handler
doc["geolocation_valid"] = (snapshot.flags & TimeSnapshotFlags::GEO_VALID) != 0;
```

### 11.6 Dashboard Display — Transmitter Time & Uptime Section (`/` page)

The "Transmitter Time & Uptime" section currently shows:

| Row | Element ID | Content |
|-----|------------|---------|
| Local Time | `txTime` | `dd/mm/yyyy HH:MM:SS UTC±HH:MM` |
| Uptime | `txUptime` | `Xd HH:MM:SS` |
| Time Source | `txTimeSource` | `NTP` / `Unsynced` |
| Last Update | `txLastUpdate` | `Xs ago` |

**Add a new row beneath Time Source:**

```html
<!-- dashboard_page_content.cpp — add inside the time section card -->
<div class="tx-detail-row">
    <span class="tx-detail-label">Timezone Source:</span>
    <span id="txGeoStatus">Waiting…</span>
</div>
```

**Dashboard JS update (inside both `fetch('/api/transmitter_health')` handlers):**

```javascript
// After updating txTimeSource:
const geoEl = document.getElementById('txGeoStatus');
if (geoEl) {
    if (timeData.geolocation_valid) {
        geoEl.textContent = '🌍 Geolocation confirmed';
        geoEl.style.color = '#4CAF50';   // green
        geoEl.title = 'Timezone has been confirmed by IP geolocation service.';
    } else {
        geoEl.textContent = '⚠ Default (UTC) — geolocation not yet confirmed';
        geoEl.style.color = '#FF9800';   // amber
        geoEl.title = 'Transmitter timezone defaults to UTC until geolocation succeeds. '
                    + 'Displayed time may not match your local time.';
    }
}
```

**Visual summary:**

| State | `txGeoStatus` element | Colour |
|-------|-----------------------|--------|
| Geolocation not yet confirmed | `⚠ Default (UTC) — geolocation not yet confirmed` | Amber `#FF9800` |
| Geolocation confirmed | `🌍 Geolocation confirmed` | Green `#4CAF50` |
| Data not yet received (initial) | `Waiting…` | Default |

**Important:** The time display itself (`txTime`) does **not** need to change — it correctly shows
the UTC offset received from the heartbeat, which is `0` when on default UTC. The amber indicator
is purely informational, alerting the user that the displayed time is in UTC and may not match their
local time.

### 11.7 Debug Page Display — Timezone & Upcoming Time Changes Card (`/debug`)

The `tz-transitions-card` currently shows the timezone name, offset, snapshot revision, next
change, and the full transitions table — but has no geo status indicator.

#### HTML addition (`debug_page_content.cpp`)

Add a `tz-geo-status` div immediately inside the card, before the `tz-summary` div:

```html
<div class='debug-control' id='tz-transitions-card'>
    <h3>🕒 Timezone &amp; Upcoming Time Changes</h3>
    <!-- NEW: geolocation status banner — populated by loadTimeTransitions() -->
    <div id='tz-geo-status'></div>
    <div class='tz-summary'>
    ...
```

#### CSS additions (`debug_page_script.cpp`)

```css
.tz-geo-warning {
    background: rgba(255, 152, 0, 0.12);
    border-left: 3px solid #FF9800;
    color: #FF9800;
    padding: 8px 12px;
    border-radius: 4px;
    margin-bottom: 12px;
    font-size: 0.88em;
    line-height: 1.5;
}
.tz-geo-ok {
    background: rgba(76, 175, 80, 0.10);
    border-left: 3px solid #4CAF50;
    color: #4CAF50;
    padding: 6px 12px;
    border-radius: 4px;
    margin-bottom: 12px;
    font-size: 0.88em;
}
```

#### JS update inside `loadTimeTransitions()` (`debug_page_script.cpp`)

After populating timezone name/abbrev/offset, add:

```javascript
const geoStatusEl = document.getElementById('tz-geo-status');
if (geoStatusEl) {
    if (data.geolocation_valid) {
        geoStatusEl.className = 'tz-geo-ok';
        geoStatusEl.textContent = '✓ Timezone confirmed via geolocation service';
    } else {
        geoStatusEl.className = 'tz-geo-warning';
        geoStatusEl.innerHTML =
            '⚠ <strong>Timezone not yet confirmed by geolocation.</strong> '
          + 'The transmitter is using the default timezone (UTC). '
          + 'DST transition data will not be available until geolocation succeeds. '
          + 'Displayed times are UTC and may not match your local time. '
          + 'The transmitter retries geolocation every 30 seconds until successful.';
    }
}
```

**Visual summary for debug page:**

| State | Banner style | Message |
|-------|-------------|---------|
| `geolocation_valid = false` | Amber left-border warning box | Explains UTC fallback, retry schedule |
| `geolocation_valid = true` | Green left-border OK box | Confirms geolocation source |

### 11.8 Files to Change (Summary)

| File | Change | Impact |
|------|--------|--------|
| `esp32common/espnow_transmitter/espnow_common.h` | Add `HeartbeatFlags::GEO_VALID = 0x01` constant; add `uint8_t flags` field to `time_transitions_snapshot_t`; add `TimeSnapshotFlags::GEO_VALID = 0x01` | Both devices |
| `ESPnowtransmitter2/.../ethernet_utilities.h` | Add `bool is_geolocation_configured()` declaration; add `geolocation_valid` to `TimeTransitionSnapshot` struct | Transmitter |
| `ESPnowtransmitter2/.../ethernet_utilities.cpp` | Implement `is_geolocation_configured()`; set `next.flags` in `refresh_time_transition_snapshot_cache()` | Transmitter |
| `ESPnowtransmitter2/.../heartbeat_manager.cpp` | Set `hb.flags = is_geolocation_configured() ? HeartbeatFlags::GEO_VALID : 0` | Transmitter |
| `espnowreceiver_2/.../transmitter_state.h/.cpp` | Store `heartbeat_flags`; add `update_heartbeat_flags()`, `get_heartbeat_flags()`, `is_geolocation_valid()` | Receiver |
| `espnowreceiver_2/.../transmitter_manager.h/.cpp` | Add `getHeartbeatFlags()`, `isGeolocationValid()` wrappers | Receiver |
| `espnowreceiver_2/.../espnow_tasks.cpp` | Call `TransmitterManager::updateHeartbeatFlags(hb->flags)` from heartbeat handler | Receiver |
| `espnowreceiver_2/.../api_telemetry_handlers.cpp` | Add `"geolocation_valid"` field to `/api/transmitter_health` response | Receiver |
| `espnowreceiver_2/.../api_debug_handlers.cpp` | Add `"geolocation_valid"` field to `/api/transmitter_time_transitions` response | Receiver |
| `espnowreceiver_2/.../dashboard_page_content.cpp` | Add `txGeoStatus` row in Time & Uptime section | Receiver UI |
| `espnowreceiver_2/.../dashboard_page_script.cpp` | Render geo status badge from `timeData.geolocation_valid` | Receiver UI |
| `espnowreceiver_2/.../debug_page_content.cpp` | Add `tz-geo-status` div at top of tz-transitions-card | Receiver UI |
| `espnowreceiver_2/.../debug_page_script.cpp` | Render geo status banner in `loadTimeTransitions()` | Receiver UI |

### 11.9 Why Not Just Hide the Transitions Section Until Geo Is Confirmed?

Hiding the card is tempting but counterproductive:

- The user needs to know **why** the section appears empty — hiding it silently is more confusing.
- The transitions card is also useful for confirming that geolocation *did* work (showing the
  correct timezone and future transitions).
- The amber warning banner with the retry explanation gives the user actionable information
  ("waiting, will update automatically") rather than a mystery.

### 11.10 Interaction with `time_source` Field

The `time_source` field in the heartbeat (`0=unsynced, 1=NTP`) is independent of `geolocation_valid`:

| `time_source` | `geolocation_valid` | Meaning |
|---------------|---------------------|---------|
| `0` (Unsynced) | `false` | No time at all — display `---` |
| `1` (NTP) | `false` | Time is valid UTC; offset is 0; local timezone not confirmed |
| `1` (NTP) | `true` | Time is valid and in confirmed local timezone ✓ |

The existing `getTimeSourceLabel()` / `getTimeSourceColor()` functions handle the `time_source`
display. The new `txGeoStatus` element is a separate, independent indicator that answers a different
question: not *is the clock set* but *is the timezone confirmed*.

---

*Document written: 2026-03-31*  
*Author: GitHub Copilot — based on code audit of `esp32common`, `ESPnowtransmitter2`, `espnowreceiver_2`*  
*Section 11 added: 2026-03-31 — Geolocation status indicator design*
