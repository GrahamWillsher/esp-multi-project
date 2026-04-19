# OTA Failure Root Cause: AP+STA Mode → httpd Task Starvation
**Date:** 2026-04-19  
**Author:** GitHub Copilot  
**Scope:** `espnowreceiver_LCD` — transmitter proxy OTA (`/api/ota_upload`)  
**Status:** Root cause confirmed, fix plan specified, not yet implemented

---

## 1. Executive Summary

Transmitter-proxy OTA from the `espnowreceiver_LCD` webserver reliably fails with
`ERR_CONNECTION_TIMED_OUT` on both the `/api/transmitter_metadata` fetch and the
`/api/ota_upload` POST itself. The root cause is **not** a missing handler, bad routing,
or a network topology problem. It is a **single-threaded httpd task starvation** caused by
`api_ota_upload_handler` making multiple long-timeout outgoing HTTP calls to the transmitter
synchronously on the ESP-IDF httpd worker task, while the device is in **AP+STA fallback
mode** with a stale or unreachable transmitter IP in NVS — causing those calls to hit their
maximum timeouts before returning.

The `espnowreceiver_2` device never triggers this failure in practice because it always
operates in STA-only mode when OTA is attempted — the IP is fresh, the TCP connect
completes in <50 ms, and the httpd task is blocked for only the ~30–60 s OTA transfer window,
which is within the browser's request timeout.

The LCD device enters AP+STA fallback mode whenever STA connection fails at boot. In that
mode the transmitter is typically unreachable, yet `TransmitterManager::isIPKnown()` returns
`true` from the NVS-persisted stale IP, so all blocking retry logic is entered in full.

---

## 2. Architecture Context

### 2.1 httpd server — single worker task

Both `espnowreceiver_LCD` and `espnowreceiver_2` share an identical httpd configuration:

```cpp
httpd_config_t config = HTTPD_DEFAULT_CONFIG();
config.stack_size       = 8192;
config.max_open_sockets = 4;
config.max_uri_handlers = 80;
config.recv_wait_timeout = 10;
config.send_wait_timeout = 10;
config.lru_purge_enable  = true;
```

ESP-IDF's `httpd` component runs all URI handler callbacks on a **single FreeRTOS task**.
There is no thread pool, no async dispatch, no `httpd_req_async_respond()` used anywhere.

**Consequence:** while any handler is executing, every other inbound TCP SYN is accepted
into the socket backlog but the connection is never read. After ~20 s (OS TCP RST timeout)
the browser reports `ERR_CONNECTION_TIMED_OUT` for every concurrent request, including
other endpoints on the same receiver.

### 2.2 WiFi state machine and `g_espnow_transport_enabled`

At boot, `main.cpp::bootstrap_filesystem()` sets:

```cpp
g_espnow_transport_enabled = WiFiSetup::is_sta_connected();
```

This flag gates ESP-NOW radio init and the recovery loop in `loop()`, but it is **never
exposed to the webserver layer**. The webserver starts unconditionally via
`webserver_startup_task` regardless of WiFi mode. There is no mode-aware guard on any OTA
endpoint.

### 2.3 AP+STA fallback mode

When `setup_from_loaded_config()` fails (timeout, wrong password, router offline):

```cpp
// main.cpp
WiFiSetup::start_ap_fallback(/*has_credentials=*/cfg_loaded);
```

`start_ap_fallback(true)` sets `WIFI_AP_STA`, starts the soft-AP on channel 1, calls
`WiFi.begin()` for the STA, and arms the recovery state machine. The webserver then starts
and is accessible via the AP IP `192.168.4.1`.

**Key point:** A user connecting to the AP `ESP32-LCD-Setup` can browse to `/ota`. Nothing
prevents them reaching the transmitter OTA section. The transmitter IP loaded from NVS
is likely stale (the STA connection that produced it has failed). `TransmitterManager::isIPKnown()`
returns `true` regardless.

---

## 3. Failure Mechanics — Step by Step

The user opens `http://192.168.4.1/ota` and selects a transmitter firmware binary.

### Step 1 — Browser POSTs to `/api/ota_upload`

The httpd worker task picks up the POST and enters `api_ota_upload_handler`.

### Step 2 — Content-type and IP checks pass (false positive)

```cpp
if (!TransmitterManager::isIPKnown()) {           // ← returns true (stale NVS IP)
    return ApiResponseUtils::send_error_message(...);
}
```

The guard passes. Execution continues into the blocking section.

### Step 3 — ESP-NOW OTA_START signal (best-effort, irrelevant here)

```cpp
if (TransmitterManager::isMACKnown()) {
    esp_now_send(...);
    vTaskDelay(pdMS_TO_TICKS(200));              // +200 ms
}
```

ESP-NOW is disabled (`g_espnow_transport_enabled = false`), but the radio is not
initialised so `esp_now_send` fails silently. The 200 ms delay still runs.

### Step 4 — `acquire_ota_session_challenge()` — up to 111 800 ms

This function contains an outer retry loop:

```
OTA_CHALLENGE_FETCH_ATTEMPTS = 6 iterations
  Each iteration:
    try_get_prearmed_ota_challenge()
      → HTTPClient GET  <stale-ip>/api/ota_status
        timeout = OTA_CHALLENGE_HTTP_TIMEOUT_MS = 9 000 ms
        attempts = OTA_PREARM_STATUS_FETCH_ATTEMPTS = 2
        → 2 × 9 000 ms = 18 000 ms
    try_arm_ota_challenge()
      → HTTPClient POST <stale-ip>/api/ota_arm
        timeout = OTA_CHALLENGE_HTTP_TIMEOUT_MS = 9 000 ms
        → 9 000 ms
    retry delay = OTA_CHALLENGE_FETCH_RETRY_DELAY_MS = 300 ms

  Per iteration worst case = 18 000 + 9 000 + 300 = 27 300 ms
  6 iterations = 163 800 ms ≈ 164 s
```

All six iterations hit full timeout because the transmitter TCP port 80 is unreachable
from an AP+STA device where the STA interface has no route to the transmitter.

### Step 5 — Handler returns error (too late)

`acquire_ota_session_challenge()` finally returns `false`. The handler sends a JSON error
response and exits. But **164 seconds have elapsed** on the httpd task.

### Step 6 — Concurrent requests already dead

Chrome's default fetch timeout is ~120 s for same-origin requests. Both
`fetchTransmitterMetadata` (polling `/api/transmitter_metadata` every 5 s) and the upload
POST itself have already received `ERR_CONNECTION_TIMED_OUT` long before step 5.

### Worst-case timeline (stale IP, no transmitter)

| Phase | Duration |
|---|---|
| `vTaskDelay` for OTA_START settle | 200 ms |
| `acquire_ota_session_challenge` (6 × 27 300 ms) | 163 800 ms ≈ **164 s** |
| `WiFiClient.connect()` if challenge somehow obtained | up to 60 000 ms |
| `await_and_parse_ota_response()` | up to 70 000 ms |
| **Total worst case** | **≈ 294 s** |

Chrome request timeout: **~120 s** — exceeded after phase 2.

---

## 4. Why `espnowreceiver_2` Does Not Exhibit This

`_2` and LCD share identical handler code, identical httpd config, and no dedicated FreeRTOS
task. The reason `_2` works is purely **operational**:

| Condition | `espnowreceiver_2` | `espnowreceiver_LCD` |
|---|---|---|
| WiFi mode at OTA time | Always `WIFI_STA` (no AP fallback) | May be `WIFI_AP_STA` |
| Transmitter reachable? | Yes — on same LAN, ESP-NOW active | No — STA failed, stale NVS IP |
| TCP connect to transmitter | < 50 ms | Hits 60 s timeout |
| Challenge acquisition | ~100 ms (1 attempt) | ~164 s (6 × full timeout) |
| httpd blocked | ~30–60 s (safe) | ~164 s (exceeds browser timeout) |

If `_2`'s transmitter also went offline before an OTA attempt, it would exhibit the
exact same failure. The bug latently exists in both codebases; only the LCD variant's
AP fallback mode routinely creates the conditions to trigger it.

---

## 5. Contributing Factors

### F1 — No STA-mode guard on the OTA endpoint (server side)

`api_ota_upload_handler` has no check for current WiFi mode. It proceeds with full
blocking logic whenever `isIPKnown()` is true, regardless of whether the STA interface
has a route to the transmitter.

### F2 — `g_espnow_transport_enabled` is not accessible to webserver handlers

The only runtime signal that accurately encodes "we are in AP-fallback/no-STA mode" is
`g_espnow_transport_enabled` in `main.cpp`. It is declared as a non-`extern` file-scope
variable and is invisible to the `lib/webserver_lcd` layer. No equivalent query exists
in the webserver.

### F3 — `TransmitterManager::isIPKnown()` is not a liveness check

It returns `true` as soon as any IP is loaded from NVS, with no concept of IP staleness
or current reachability. A device that has never connected since a router change, or that
rebooted into AP fallback, will still report `isIPKnown() = true`.

### F4 — No transmitter reachability pre-flight

There is no fast TCP probe (e.g. 2 s connect attempt to port 80) before entering the full
challenge acquisition loop. A single-attempt 2 s probe would immediately discard the
stale IP case and return a useful error to the user without blocking the httpd task.

### F5 — Challenge loop retry constants assume reliable network

`OTA_CHALLENGE_FETCH_ATTEMPTS = 6` and `OTA_CHALLENGE_HTTP_TIMEOUT_MS = 9000 ms` were
designed for a connected STA device with occasional WiFi congestion. In an AP+STA
fallback scenario they produce a 164 s httpd blockade rather than failing fast.

### F6 — OTA page renders transmitter upload section unconditionally

`ota_page_content.cpp` always renders both the Receiver and Transmitter upload sections.
There is no server-side WiFi mode check that would disable or warn about the Transmitter
section when the device is in AP mode. The user is never told "transmitter OTA unavailable
in AP mode".

### F7 — `fetchTransmitterMetadata` polls every 5 s unconditionally

The JS polls `/api/transmitter_metadata` every 5 s. In AP mode this always returns
`{ status: "waiting" }` (no ESP-NOW transport), but the page does not use this to gate
the upload button. It displays "Waiting... ○" but leaves the upload button active.

---

## 6. Fix Plan

The fixes are layered: F1 is the mandatory server-side safety gate; F2–F4 are defence-in-depth
that also improve the `_2` variant; F5 hardens the timeout constants; F6–F7 fix the UX.

### Fix 1 (MANDATORY) — Server-side STA-mode guard in `api_ota_upload_handler`

Add a WiFi mode check at the very top of `api_ota_upload_handler`, before any outgoing
network call. Return an immediate 400 error if the device is not in STA mode.

**File:** `espnowreceiver_LCD/lib/webserver_lcd/api/api_control_handlers.cpp`

```cpp
// At the top of api_ota_upload_handler, after the content-type check and BEFORE isIPKnown():
{
    const wifi_mode_t wifi_mode = WiFi.getMode();
    if (wifi_mode != WIFI_MODE_STA) {
        LOG_WARN("OTA", "Transmitter OTA rejected: device not in STA mode (mode=%d)", (int)wifi_mode);
        return ApiResponseUtils::send_error_message(req,
            "Transmitter OTA unavailable: device must be in STA mode (currently in AP/config mode). "
            "Connect to your WiFi network first.");
    }
}
```

This is a zero-cost guard for the normal (STA) case, and immediately short-circuits
the 164 s blocking path for the AP+STA case. It also future-proofs against any other
scenario where WiFi mode is not STA.

**Risk:** None. Adding an early-return error before any network call is safe.

### Fix 2 (HIGH PRIORITY) — Transmitter TCP reachability pre-flight

After the `isIPKnown()` check and before `acquire_ota_session_challenge()`, perform a
fast TCP connect probe with a 2 s timeout. If the probe fails, return immediately.

**File:** `espnowreceiver_LCD/lib/webserver_lcd/api/api_control_handlers.cpp`

```cpp
// After isIPKnown() check:
{
    const uint8_t* ip = TransmitterManager::getIP();
    IPAddress probe_ip(ip[0], ip[1], ip[2], ip[3]);
    WiFiClient probe;
    probe.setTimeout(2000);
    if (!probe.connect(probe_ip, 80)) {
        probe.stop();
        LOG_WARN("OTA", "Transmitter not reachable at %s:80 — aborting OTA", probe_ip.toString().c_str());
        return ApiResponseUtils::send_error_message(req,
            "Transmitter not reachable. Check that it is powered on and connected to the same network.");
    }
    probe.stop();
}
```

This caps the worst-case blocking time from 164 s to **2 s** in the unreachable-transmitter
case, and is entirely additive to Fix 1.

**Risk:** Low. Adds 2 s in the failure path (vastly better than 164 s). In the success
path adds ~50 ms (one extra TCP connect, immediately closed).

### Fix 3 (HIGH PRIORITY) — Reduce challenge loop timeout constants

Even with Fixes 1 and 2, the constants should be tightened for the case where the
pre-flight passes but the challenge calls subsequently fail (e.g. transmitter restarted
between the probe and the arm call).

**File:** `espnowreceiver_LCD/lib/webserver_lcd/api/api_control_handlers.cpp`

| Constant | Current | Proposed | Rationale |
|---|---|---|---|
| `OTA_CHALLENGE_HTTP_TIMEOUT_MS` | 9 000 ms | 3 000 ms | LAN RTT is always < 200 ms; 3 s is generous for congestion |
| `OTA_CHALLENGE_FETCH_ATTEMPTS` | 6 | 2 | 2 attempts covers transient OTA arm race; 6 is designed for WAN |
| `OTA_TX_SOCKET_TIMEOUT_MS` | 60 000 ms | 15 000 ms | LAN transfers never stall for 60 s; saves httpd if stream hangs |

New worst case with Fix 3 alone (no Fixes 1 or 2): `2 × (2 × 3000 + 3000) = 18 000 ms`.
New worst case with Fixes 1 + 2 + 3: **2 000 ms** (probe timeout).

Apply the same constants to `espnowreceiver_2` for defence-in-depth. Both variants share
identical code paths.

### Fix 4 (MEDIUM PRIORITY) — Expose WiFi mode to webserver via `api_receiver_status`

Add a `wifi_mode` or `sta_connected` field to an existing API endpoint (e.g.
`/api/firmware_info` or a new `/api/receiver_status`) so the OTA page JS can query it on
load and gate the Transmitter upload section accordingly.

**File:** `espnowreceiver_LCD/lib/webserver_lcd/api/api_telemetry_handlers.cpp`

Suggested addition to `api_firmware_info_handler` response JSON:

```json
{
  "valid": true,
  "env": "...",
  "device": "RECEIVER",
  "version": "2.0.0",
  "build_date": "...",
  "sta_connected": true,        // ← new field
  "wifi_mode": "STA"            // ← new field: "STA" | "AP" | "APSTA" | "NULL"
}
```

**Risk:** None (additive JSON field, backward-compatible).

### Fix 5 (MEDIUM PRIORITY) — OTA page: disable Transmitter section in AP mode

Using the `sta_connected` field from Fix 4, the JS should:
1. On page load, fetch `/api/firmware_info`.
2. If `sta_connected === false`, replace the Transmitter OTA section with a warning banner:
   > ⚠️ Transmitter OTA is unavailable in AP/config mode.
   > Connect to your WiFi network to enable transmitter firmware updates.
3. Disable the transmitter upload button (grey it out, ignore clicks).
4. Continue polling; if `sta_connected` becomes `true` (device recovered and rebooted),
   reload the page.

This prevents the user from triggering the failing path at all — consistent with the
server-side guard in Fix 1.

**Files:**
- `espnowreceiver_LCD/lib/webserver_lcd/pages/ota_page_script.cpp` (JS guard logic)
- `espnowreceiver_LCD/lib/webserver_lcd/pages/ota_page_content.cpp` (optional: server-side
  conditional render via `WiFi.getMode()` in the page handler)

### Fix 6 (LOW PRIORITY) — `fetchTransmitterMetadata` should gate upload button

The existing JS already polls `/api/transmitter_metadata`. The result already signals
`{ status: "waiting" }` when ESP-NOW is disabled (AP mode). This signal should be used
to disable the Transmitter upload button until `status === "received"`.

This is defence-in-depth behind Fixes 4+5 and requires only a JS change in
`ota_page_script.cpp`.

---

## 7. Implementation Sequence

Apply in strict order; validate build after each step.

| Step | Fix | Files | Risk | Effort |
|---|---|---|---|---|
| 1 | Fix 1: STA mode guard in handler | `api_control_handlers.cpp` | None | 5 min |
| 2 | Fix 3: Tighten timeout constants | `api_control_handlers.cpp` (both projects) | None | 5 min |
| 3 | Fix 2: TCP reachability pre-flight | `api_control_handlers.cpp` | Low | 15 min |
| 4 | Fix 4: Expose `sta_connected` in API | `api_telemetry_handlers.cpp` | None | 10 min |
| 5 | Fix 5: OTA page JS AP-mode guard | `ota_page_script.cpp` | Low | 20 min |
| 6 | Fix 6: Gate upload btn on TX metadata | `ota_page_script.cpp` | None | 10 min |
| 7 | Build + flash + live test | — | — | 30 min |

Steps 1–3 eliminate the root cause (httpd starvation). Steps 4–6 add correct UX.

---

## 8. Testing Checklist

After implementing all fixes:

### 8.1 AP-mode OTA rejection (Fixes 1–3)
- [ ] Boot device with invalid WiFi credentials → confirm AP fallback mode
- [ ] Connect browser to `http://192.168.4.1/ota`
- [ ] Select a transmitter `.bin` and click Upload
- [ ] Confirm **immediate** `400` error: "Transmitter OTA unavailable: device must be in STA mode"
- [ ] Confirm `/api/transmitter_metadata` continues to respond normally (httpd not blocked)
- [ ] Confirm total round-trip time < 500 ms

### 8.2 AP-mode UX gate (Fixes 4–5)
- [ ] OTA page in AP mode shows warning banner in Transmitter section
- [ ] Transmitter upload button is disabled/greyed out
- [ ] Receiver self-OTA section is still functional (AP mode is fine for receiver self-update)

### 8.3 STA-mode OTA still works (regression)
- [ ] Boot with valid credentials → STA connected
- [ ] Transmitter is online
- [ ] Transmitter OTA completes successfully end-to-end
- [ ] Confirm timeout reductions (Fix 3) do not break the happy path

### 8.4 Stale IP in STA mode (Fixes 2–3)
- [ ] Device in STA mode, transmitter powered off (stale IP in NVS)
- [ ] Attempt transmitter OTA
- [ ] Confirm rejection in ≤ 2 s (pre-flight probe fails), not 164 s
- [ ] Confirm useful error message: "Transmitter not reachable"

---

## 9. Files Changed

| File | Change |
|---|---|
| `espnowreceiver_LCD/lib/webserver_lcd/api/api_control_handlers.cpp` | Fix 1 (STA guard), Fix 2 (TCP probe), Fix 3 (timeout constants) |
| `espnowreceiver_LCD/lib/webserver_lcd/api/api_telemetry_handlers.cpp` | Fix 4 (`sta_connected` in `/api/firmware_info`) |
| `espnowreceiver_LCD/lib/webserver_lcd/pages/ota_page_script.cpp` | Fix 5 (AP-mode banner + gate), Fix 6 (upload button gate) |
| `espnowreceiver_2/lib/webserver/api/api_control_handlers.cpp` | Fix 3 timeout constants (defence-in-depth parity) |

---

## 10. Not In Scope

- Async OTA handler via `httpd_req_async_respond()` (Phase D in the long-term plan) — this
  would fully decouple OTA from the httpd task but is a significant refactor. Fixes 1–3
  solve the practical problem completely for the current architecture.
- MQTT-based OTA notification — not used by LCD receiver.
- Receiver self-OTA (`/api/ota_upload_receiver`) — this has no outgoing TCP calls; it writes
  directly to the ESP32 flash via `Update`. It is not affected by any of the above and
  continues to work in both STA and AP mode.
