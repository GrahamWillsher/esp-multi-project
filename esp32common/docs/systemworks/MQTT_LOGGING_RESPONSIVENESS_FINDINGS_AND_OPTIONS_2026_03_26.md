# MQTT Logging Responsiveness: Findings and Options (2026-03-26)
> **Updated 2026-03-26**: Initial findings revised after live testing confirmed that setting EMERG debug level
> does NOT improve responsiveness. Root cause analysis updated — the primary cause is architectural
> (SSE handler blocking), not MQTT logging volume. See Section 4a.

---

## 1) Questions being answered

1. Can the receiver `/debug` page log level stop MQTT logging from hurting receiver website responsiveness?
2. Why does setting the level to EMERG not improve responsiveness?
3. What other options are available?
4. Is this actually a real operational concern given the system runs unattended in storage?

---

## 2) Short answer

### 2.1 Does EMERG debug level fix the responsiveness problem?

**No.** Live testing confirmed that setting EMERG makes no measurable difference to webserver responsiveness.

The reason: **the primary root cause is not MQTT debug log traffic.** See Section 4a for full analysis.

### 2.2 Is there another debug flag that helps?

**No.** There is no `/debug` flag or setting that will resolve the underlying issue because the problem
is structural — it is in how the ESP-IDF HTTP server and SSE connections interact with the FreeRTOS task model.

### 2.3 Does `/debug` control receiver-side webserver logging?

**No.** It controls transmitter logger level only. The receiver webserver module already has
`LOG_USE_MQTT=0` override in its shim, so it does not log to MQTT regardless.

---

## 3) Background: what `/debug` does (verified code path)

1. **Receiver `/debug` page** controls **transmitter** MQTT logger minimum level via ESP-NOW.
   - Receiver API routes: `/api/debugLevel` and `/api/setDebugLevel?level=0..7`
   - `send_debug_level_control(level)` builds `msg_debug_control` ESP-NOW packet.

2. **Transmitter receives control** and applies to MqttLogger
   - `handle_debug_control()` calls `MqttLogger::instance().set_level(...)` and saves to NVS.
   - `MqttLogger::log()` drops any message where `level > min_level_`.
   - At `EMERG (0)`, almost all transmitter MQTT debug messages are suppressed.

3. **Current limitation**
   - No explicit "OFF" state — `EMERG(0)` is "almost off" not strictly off.
   - But since EMERG makes no difference to responsiveness, this is a secondary concern.

---

## 4) Why webserver responsiveness is poor — actual root cause

### 4a PRIMARY CAUSE: SSE handler blocks the httpd task (the real culprit)

The receiver webserver uses **ESP-IDF's `esp_http_server`** (`httpd_handle_t`), which runs in a
single internal FreeRTOS task at priority 2. This task is responsible for serving **all** HTTP requests.

When a browser opens a Server-Sent Events (SSE) connection — for example by navigating to
`/cellmonitor` or the battery monitor page — the SSE handler runs inside this same httpd task and
**blocks** it:

```cpp
// From api_sse_handlers.cpp — api_cell_data_sse_handler()
while ((xTaskGetTickCount() - start_time) < max_duration) {      // max 300 seconds!
    const bool changed = SSENotifier::waitForCellDataUpdate(15000); // blocks up to 15s each loop
    ...
}
```

While the httpd task is blocked inside `waitForCellDataUpdate(15000)`, it **cannot serve any other
HTTP request** — including the simple dashboard poll at `/api/dashboard_data`.

**Consequence:**
- If `/cellmonitor` is open in a browser tab (even a background tab), the httpd task is occupied
  indefinitely (up to 5 minutes per SSE session).
- Any subsequent request — page load, API poll, settings change — may wait up to **15 seconds**
  before the httpd task wakes up from its blocking wait.
- This explains the poor responsiveness exactly: the 15 000 ms timeout matches the observed unresponsive periods.
- Setting debug level to EMERG has zero effect on this because MQTT logging is not involved at all.

The SSE monitor handler (`api_monitor_sse_handler`) has the same blocking pattern and same impact.

### 4b SECONDARY CAUSE: Shared lwIP / WiFi stack contention

Both the webserver (TCP port 80) and the receiver MQTT client share the same WiFi connection and
lwIP TCP/IP stack (runs on Core 0). When the MQTT client (`PubSubClient::loop()`) is receiving
messages from the broker, it occupies the lwIP thread with socket I/O. This can add latency to
webserver TCP send/receive operations.

This is a genuine secondary factor. Reducing transmitter MQTT debug traffic (via EMERG or an
explicit OFF mode) would marginally help here but will not fix the 15-second freezes caused by
SSE blocking.

### 4c Task priority summary

| Task | Priority | Core | Notes |
|------|----------|------|-------|
| WiFi driver | 23 | 0 | Fixed by IDF |
| lwIP tcpip_thread | 18 | 0 | Fixed by IDF |
| httpd (webserver) | 2 | any | Blocked by SSE handlers |
| ESPNow Worker | 2 | 1 | Competes equally with httpd |
| DisplayRenderer | 1 | 1 | Lower than httpd |
| LedRenderer | 1 | 1 | Lower than httpd |
| MqttClient | 0 | 1 | Lowest; non-blocking in practice |

---

## 5) Operational context — is this actually a problem?

The user's own observation is key: **"the system will just sit in a storage area running; you will
only check in on it when things are not working correctly."**

This reframes the severity significantly:

- **Normal unattended operation**: no browser open → no SSE connection → no blocking → httpd task
  is free → webserver will respond quickly on the rare occasion someone checks in.
- **Active monitoring session**: browser open with `/cellmonitor` → SSE connection active →
  blocking occurs → other pages load slowly during that session.
- **Diagnostic check-in**: opening `/` dashboard first → fast, because the dashboard uses JS
  polling (not SSE). Only open `/cellmonitor` when cell-level data is specifically needed.

**Practical conclusion**: the responsiveness problem is essentially self-inflicted by keeping active
SSE monitoring tabs open, and it largely disappears in unattended operational mode. For periodic
check-ins, just use `/` first and avoid leaving cell monitor open in the background.

---

## 6) Options

### Option A (immediate workaround — zero code change)

**Close browser tabs after each monitoring session.**

- The SSE session auto-terminates after 300 seconds (5 minutes) if left open.
- Closing the tab immediately frees the httpd task.
- Dashboard `/` uses polling not SSE, and is always fast when no SSE is active.
- **This is the correct operational approach for a system checked in on rarely.**

---

### Option B (simple low-risk fix): Shorten SSE wait timeout

Reduce the blocking duration from 15 000 ms to 500–1 000 ms so the httpd task wakes up far more
often and can interleave with other requests between SSE poll cycles.

```cpp
// api_sse_handlers.cpp — change in both SSE handlers:
const bool changed = SSENotifier::waitForCellDataUpdate(500);  // was 15000
const TickType_t max_duration = pdMS_TO_TICKS(60000);          // was 300000
```

This does not eliminate blocking but reduces the worst-case httpd freeze from 15 s to 0.5 s.
Very low risk, simple change.

---

### Option C (architectural fix): Per-connection task for SSE

Refactor SSE handlers to spin off a dedicated FreeRTOS task per SSE session, freeing the httpd
task immediately after sending response headers.

```cpp
esp_err_t api_cell_data_sse_handler(httpd_req_t *req) {
    HttpSseUtils::begin_sse(req);               // Send headers — unblocks httpd task
    xTaskCreate(sse_cell_data_task, "SSE_cell", 4096, req, 1, nullptr);
    return ESP_OK;                               // httpd task is now free
}
```

Requires careful httpd socket lifecycle management (keep socket alive during task run) but is the
architecturally correct solution. Medium effort.

---

### Option D (add explicit MQTT OFF mode in transmitter)

Even though EMERG already suppresses almost all debug MQTT traffic, add a true `OFF (8)` state
that hard-disables MqttLogger enqueue entirely. Addresses the secondary lwIP contention factor and
provides a clean operational "mute" switch.

Changes required:
- Receiver `/debug` UI: add `<option value='8'>OFF - Disable MQTT Logging</option>`
- Protocol: accept `level == 8` in transmitter `handle_debug_control()`
- `MqttLogger::log()`: add early return when level is OFF
- `api_debug_handlers.cpp`: extend validation from `level > 7` to `level > 8`

Does not fix 15-second SSE freezes, but worthwhile as a clean diagnostic mute.

---

### Option E (traffic shaping): Rate-limit transmitter MQTT logs

Per-tag rate limiting, duplicate suppression, periodic summary lines. Reduces secondary lwIP
contention further. Complementary to Option D.

---

## 7) Recommended rollout

| Phase | Action | Benefit | Effort |
|-------|--------|---------|--------|
| **0 — now** | Close browser tabs after use; use `/` for check-ins | Resolves operational concern immediately | Zero |
| **1 — next** | Implement Option B (shorten SSE wait to 500 ms) | Worst-case httpd freeze drops from 15 s to 0.5 s | Very low |
| **2 — hardening** | Implement Option C (per-task SSE) | Fully eliminates SSE-induced blocking | Medium |
| **3 — optional** | Implement Option D (MQTT OFF mode) | Clean explicit transmitter MQTT mute for diagnostics | Low |

---

## 8) Final summary

| Question | Answer |
|----------|--------|
| Does EMERG debug level fix responsiveness? | **No** |
| Is there another `/debug` flag that helps? | **No** |
| What is the actual root cause? | SSE handlers block the single httpd task for up to 15 s |
| Is it a real operational problem? | **Not in practice** — system runs unattended, no browser open |
| What is the quick operational fix? | Close browser tabs when done; use `/` for check-ins |
| What is the engineering fix? | Shorten SSE wait timeout (Option B) or per-task SSE (Option C) |
