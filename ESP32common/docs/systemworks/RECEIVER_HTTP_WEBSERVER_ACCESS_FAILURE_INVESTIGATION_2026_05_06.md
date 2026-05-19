# Receiver HTTP/Webserver Access Failure Investigation (2026-05-06)

## 1) Problem statement

User-reported symptom:
- Receiver web UI is not reachable at `http://192.168.1.59`.
- Request was for a full investigation of why receiver web access is failing and what should be changed.

This report focuses on the active receiver implementations:
- `espnowreceiver_LCD`
- `espnowreceiver_2`

---

## 2) What was investigated

### Code paths reviewed

1. Webserver startup and socket configuration
- [espnowreceiver_LCD/lib/webserver_lcd/webserver.cpp](espnowreceiver_LCD/lib/webserver_lcd/webserver.cpp#L60)
- [espnowreceiver_2/lib/webserver/webserver.cpp](espnowreceiver_2/lib/webserver/webserver.cpp#L60)

2. WiFi/AP fallback behavior and expected reachable IP
- [espnowreceiver_LCD/src/main.cpp](espnowreceiver_LCD/src/main.cpp#L101)
- [espnowreceiver_LCD/src/config/wifi_setup.cpp](espnowreceiver_LCD/src/config/wifi_setup.cpp#L105)

3. Route registration for `/` and API endpoints
- [espnowreceiver_LCD/lib/webserver_lcd/page_registration_factory.cpp](espnowreceiver_LCD/lib/webserver_lcd/page_registration_factory.cpp#L22)
- [espnowreceiver_LCD/lib/webserver_lcd/pages/dashboard_page.cpp](espnowreceiver_LCD/lib/webserver_lcd/pages/dashboard_page.cpp#L86)
- [espnowreceiver_LCD/lib/webserver_lcd/api/api_handlers.cpp](espnowreceiver_LCD/lib/webserver_lcd/api/api_handlers.cpp#L31)

4. Long-lived SSE behavior and connection duration
- [espnowreceiver_LCD/lib/webserver_lcd/api/api_sse_handlers.cpp](espnowreceiver_LCD/lib/webserver_lcd/api/api_sse_handlers.cpp#L19)
- [espnowreceiver_LCD/lib/webserver_lcd/api/api_sse_handlers.cpp](espnowreceiver_LCD/lib/webserver_lcd/api/api_sse_handlers.cpp#L100)
- [espnowreceiver_LCD/lib/webserver_lcd/api/api_sse_handlers.cpp](espnowreceiver_LCD/lib/webserver_lcd/api/api_sse_handlers.cpp#L175)

5. Recent HTTP/radio coexistence changes that can affect accessibility
- [espnowreceiver_LCD/lib/webserver_lcd/webserver.cpp](espnowreceiver_LCD/lib/webserver_lcd/webserver.cpp#L119)
- [espnowreceiver_LCD/lib/webserver_lcd/api/api_middleware.cpp](espnowreceiver_LCD/lib/webserver_lcd/api/api_middleware.cpp#L17)
- [espnowreceiver_LCD/lib/webserver_lcd/pages/dashboard_page_script.cpp](espnowreceiver_LCD/lib/webserver_lcd/pages/dashboard_page_script.cpp#L12)

---

## 3) Key findings

## F1 — Route registration is not the primary failure

Root page `/` is explicitly registered via dashboard page:
- [espnowreceiver_LCD/lib/webserver_lcd/page_registration_factory.cpp](espnowreceiver_LCD/lib/webserver_lcd/page_registration_factory.cpp#L22)
- [espnowreceiver_LCD/lib/webserver_lcd/pages/dashboard_page.cpp](espnowreceiver_LCD/lib/webserver_lcd/pages/dashboard_page.cpp#L86)

Conclusion: this is not a missing-route bug.

## F2 — A network-mode mismatch can make `192.168.1.59` invalid

LCD receiver supports AP fallback and AP+STA recovery:
- AP fallback starts at `192.168.4.1`: [espnowreceiver_LCD/src/config/wifi_setup.cpp](espnowreceiver_LCD/src/config/wifi_setup.cpp#L20)
- AP mode entered when STA config/connect fails: [espnowreceiver_LCD/src/main.cpp](espnowreceiver_LCD/src/main.cpp#L108)

If receiver is in AP/APSTA fallback, browsing `192.168.1.59` will fail even though webserver may be running correctly on AP IP (`192.168.4.1`).

Conclusion: one failure class is wrong target IP for current network mode.

## F3 — High-confidence regression: LCD `max_open_sockets` reduced to 2

Current LCD config:
- [espnowreceiver_LCD/lib/webserver_lcd/webserver.cpp](espnowreceiver_LCD/lib/webserver_lcd/webserver.cpp#L119)

Receiver_2 still uses 4:
- [espnowreceiver_2/lib/webserver/webserver.cpp](espnowreceiver_2/lib/webserver/webserver.cpp#L117)

This reduction is a material behavior change and is the most likely direct cause of “web page won’t open” under normal multi-connection browser conditions and/or existing long-lived streams.

## F4 — Two long-lived SSE endpoints can consume both sockets

SSE handlers are long-lived (up to 5 minutes):
- [espnowreceiver_LCD/lib/webserver_lcd/api/api_sse_handlers.cpp](espnowreceiver_LCD/lib/webserver_lcd/api/api_sse_handlers.cpp#L19)

Endpoints:
- `/api/cell_stream`: [espnowreceiver_LCD/lib/webserver_lcd/api/api_handlers.cpp](espnowreceiver_LCD/lib/webserver_lcd/api/api_handlers.cpp#L32)
- `/api/monitor_sse`: [espnowreceiver_LCD/lib/webserver_lcd/api/api_handlers.cpp](espnowreceiver_LCD/lib/webserver_lcd/api/api_handlers.cpp#L31)

If two SSE sessions are active (even from stale tabs/devices), all available sockets can be occupied when `max_open_sockets=2`, leaving no capacity for new page loads.

Conclusion: socket starvation is a high-probability access blocker.

## F5 — Middleware throttling is unlikely to block `/`

Recent middleware pressure logic targets telemetry/API requests and mutations, not page registration itself:
- [espnowreceiver_LCD/lib/webserver_lcd/api/api_middleware.cpp](espnowreceiver_LCD/lib/webserver_lcd/api/api_middleware.cpp#L78)

This may delay API freshness but does not explain inability to open the initial HTML page.

Conclusion: not the main cause of “can’t open web page” symptom.

## F6 — Runtime STA-loss blind spot (high impact)

During boot, LCD receiver can enter AP fallback if STA connect fails. However, when boot succeeds in STA mode and ESP-NOW is later active, a subsequent STA drop (for example during channel lock/reconnect pressure) did not automatically start AP fallback at runtime.

Implication:
- ESP-NOW can remain active (`alive ... espnow=connected`),
- but STA/LAN IP can be down,
- leaving `192.168.1.59` unreachable with no automatic management-path recovery.

This matches the observed symptom pattern: ESP-NOW logs continue, but web UI appears “gone”.

---

## 4) Most likely root cause (ranked)

1. **Primary likely cause:** socket budget too low (`max_open_sockets=2`) on `espnowreceiver_LCD`, causing connection starvation under normal browser + SSE behavior.
2. **Secondary likely cause:** receiver currently in AP/APSTA fallback, so `192.168.1.59` is not the active serving address.
3. **Additional high-impact cause:** runtime STA-loss without AP recovery handoff (management path drops after boot).
4. **Less likely:** route registration or middleware hard-blocking of `/` (not supported by code evidence).

---

## 5) Recommended solution

## S1 (Immediate) — Restore practical socket capacity

For LCD receiver, increase HTTP socket count from 2 to at least 4 (prefer 6 for operational headroom), in:
- [espnowreceiver_LCD/lib/webserver_lcd/webserver.cpp](espnowreceiver_LCD/lib/webserver_lcd/webserver.cpp#L119)

Rationale:
- keeps coexistence controls (adaptive polling + pressure throttling)
- removes brittle “2 sockets only” bottleneck
- aligns with receiver_2 baseline behavior

## S2 (Immediate) — Operationally validate active serving IP before browser test

At runtime confirm receiver mode/IP:
- STA mode: use `WiFi.localIP()`
- AP/APSTA fallback: use `192.168.4.1`

Related code:
- [espnowreceiver_LCD/src/config/wifi_setup.cpp](espnowreceiver_LCD/src/config/wifi_setup.cpp#L105)
- [espnowreceiver_LCD/lib/webserver_lcd/webserver.cpp](espnowreceiver_LCD/lib/webserver_lcd/webserver.cpp#L183)

## S3 (Short-term hardening) — Add SSE admission guard

Add a small guard to reject new SSE when active SSE count reaches a threshold (e.g. 1 per stream class) with `503` + retry hint. This prevents silent starvation and reserves capacity for normal page/API requests.

Metrics already exist and can drive this:
- [espnowreceiver_LCD/lib/webserver_lcd/api/api_sse_handlers.cpp](espnowreceiver_LCD/lib/webserver_lcd/api/api_sse_handlers.cpp#L34)

## S3b (Implemented) — Runtime STA-loss watchdog with AP+STA fallback handoff

Implemented in [espnowreceiver_LCD/src/main.cpp](espnowreceiver_LCD/src/main.cpp):
- Detect STA loss while ESP-NOW runtime is active
- Wait grace window (15s) to avoid reacting to transient blips
- Start AP+STA fallback automatically (`ESP32-LCD-Setup`, `192.168.4.1`) when STA remains down
- Continue STA background recovery and reboot after stable reconnection
- Emit periodic network diagnostics (`mode`, `sta/ap up/down`, STA IP, AP IP, channel)

This preserves a deterministic management path even if LAN-side STA connectivity collapses during ESP-NOW pressure/reconnect cycles.

## S4 (Keep) — Retain pressure-aware HTTP telemetry controls

Keep these already-added coexistence protections (they are not the web-access blocker):
- adaptive dashboard polling: [espnowreceiver_LCD/lib/webserver_lcd/pages/dashboard_page_script.cpp](espnowreceiver_LCD/lib/webserver_lcd/pages/dashboard_page_script.cpp#L169)
- telemetry pressure throttling + `Retry-After`: [espnowreceiver_LCD/lib/webserver_lcd/api/api_middleware.cpp](espnowreceiver_LCD/lib/webserver_lcd/api/api_middleware.cpp#L108)
- observability endpoint: [espnowreceiver_LCD/lib/webserver_lcd/api/api_handlers.cpp](espnowreceiver_LCD/lib/webserver_lcd/api/api_handlers.cpp#L57)

---

## 6) Validation plan

1. Build and flash receiver with `max_open_sockets >= 4`.
2. Verify access to:
   - `/` (dashboard)
   - `/api/system_metrics`
   - `/api/http_pressure_stats`
3. Open/close monitor pages and ensure new browser sessions still load `/` reliably.
4. Check socket headroom telemetry:
   - `webserver.active_long_lived_streams`
   - `webserver.socket_utilization_estimate`
   in [espnowreceiver_LCD/lib/webserver_lcd/api/api_telemetry_handlers.cpp](espnowreceiver_LCD/lib/webserver_lcd/api/api_telemetry_handlers.cpp#L578)
5. Confirm no regression in ESP-NOW stability during moderate browser usage.

Pass criteria:
- Page consistently opens at correct active IP.
- No “stuck inaccessible webserver” behavior while SSE is active.
- Coexistence protections still function (`X-Radio-Pressure`, throttling counters).

---

## 7) Final conclusion

The receiver webserver code is present and route wiring is correct. The strongest code-backed explanation for current access failure is **socket starvation introduced by reducing LCD `max_open_sockets` to 2**, combined with long-lived SSE usage and normal browser connection patterns. A parallel operational factor is **AP fallback IP mismatch** (`192.168.4.1` vs `192.168.1.59`).

**Recommended corrective action:** raise `max_open_sockets` back to a practical value (4–6), keep current pressure-throttling logic, and validate against active network mode/IP during access tests.
