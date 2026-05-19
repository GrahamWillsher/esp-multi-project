# Receiver/Transmitter Webpage Failure Full Review (2026-05-08)

## 1) Scope and review method

This review covered both sides of the ESP-NOW link and the HTTP serving path:

- Receiver webserver/page path (`espnowreceiver_LCD`):
  - `lib/webserver_lcd/common/page_generator.cpp`
  - `lib/webserver_lcd/webserver.cpp`
  - `src/main.cpp`
  - `src/espnow/espnow_runtime.cpp`
- Shared connection/radio logic (`ESP32common`):
  - `espnow_common_utils/rx_connection_handler.cpp`
  - `espnow_common_utils/connection_manager.cpp`
  - `espnow_common_utils/channel_manager.cpp`
  - `espnow_common_utils/espnow_standard_handlers.cpp`
- Transmitter reconnect/channel logic (`ESPnowtransmitter2`):
  - `src/espnow/discovery_task.cpp`
  - `src/espnow/tx_reconnect_manager.cpp`

Git history baseline was checked against `main` and recent branch commits (`feature/battery-emulator-migration`), including:

- `da02232` (large reconnect/state-machine redesign)
- `820d3fa` (LCD OTA/APSTA/httpd hardening)
- `6341ddd` (checksum alignment + config ACK path)

---

## 2) Observed symptoms (from runtime logs)

### Receiver

- Repeated connection cycle:
  - `CONNECTING timeout (45000ms) exceeded -> IDLE`
  - immediate auto-reconnect back to `CONNECTING`
- MQTT gate blocked continuously (`state=0`, stale heartbeat age)
- STA still appears up on channel 6 in periodic diagnostic logs
- HTTP page failure:
  - `[error][HTTP_PAGE] send_chunk failed uri=/ stage=common_script_helpers rc=45062`
  - `GET / done rc=-1 dur=21033 ms`

### Transmitter

- Reconnect confirm fails repeatedly:
  - `ESPNOW: Peer channel is not equal to the home channel, send fail!`
  - `connect_confirm send failed: ESP_ERR_ESPNOW_ARG`
- Scan says found on channel 7, but ACK payload reports channel 6:
  - `ACK ... ch=6`
  - `Scan complete: FOUND on ch=7`
  - then channel lock saved as 7 and confirm fails

---

## 3) Primary root cause chain

## RC-1 (Primary): Transmitter reconnect used scan channel instead of receiver-reported channel

In reconnect discovery, the code path accepted ACK on scan channel `ch` and treated `ch` as the found/lock channel, even when ACK payload reported a different authoritative receiver channel (`a->channel`, from receiver `WiFi.channel()`).

Result:

1. Scan loop tuned to 7 receives ACK with `a->channel=6`.
2. Manager locks/saves 7.
3. `connect_confirm` send fails with `ESP_ERR_ESPNOW_ARG` because peer channel != local/home channel.
4. Reconnect loops indefinitely.

This creates persistent probe/ACK churn and elevated radio contention.

## RC-2 (Secondary): Reconnect storm amplified HTTP send fragility

Once reconnect entered a tight loop, receiver had high concurrent ESP-NOW traffic during page rendering. The dashboard embeds large inlined script helpers (`common_script_helpers` stage), and this was where socket send failed.

`rc=45062` corresponds to HTTP response send failure (`ESP_ERR_HTTPD_RESP_SEND`) at chunk send time.

## RC-3 (Contributing): HTTP send path had low tolerance under heavy coexistence pressure

Receiver webserver configuration historically used short send wait timeout (`send_wait_timeout=10s`) and chunk sends without deliberate CPU yield between chunks. Under heavy coexistence pressure this is brittle.

---

## 4) What changed in the redesign and why this surfaced now

The redesign (notably `da02232`) substantially changed reconnect/state-machine orchestration (new reconnect manager, worker scans, event-driven transitions, more diagnostics, tighter loops).

The bug itself was in reconnect channel authority handling, but the visible symptom on user side became "webpage does not load" because:

- reconnect never completed,
- radio stayed busy with repeated discovery/confirm cycles,
- HTTP response send to browser failed mid-page.

So the web failure is a downstream effect of reconnect/channel mismatch plus HTTP path sensitivity under contention.

---

## 5) Detailed findings and risk ranking

### Finding A (Critical): channel authority mismatch in TX reconnect

- Severity: **Critical**
- Impact: prevents reconnection, causes endless retry storm
- Confidence: **High** (directly supported by your logs + code path)

### Finding B (High): peer registered with fixed non-zero channel during confirm phase

- Severity: **High**
- Impact: immediate `ESP_ERR_ESPNOW_ARG` when home channel differs
- Confidence: **High**
- Preferred behavior (already used in receiver probe handling): register peer with channel `0` to follow current WiFi channel.

### Finding C (Medium-High): HTTP path still vulnerable under contention spikes

- Severity: **Medium-High**
- Impact: intermittent page failures/timeouts when RF pressure is high
- Confidence: **High**
- Triggered most often on large helper-script stage.

### Finding D (Medium): Channel manager cache can become stale relative to live WiFi channel

- Severity: **Medium**
- Impact: wrong lock hints or stale lock decisions in edge recovery sequences
- Confidence: **Medium**
- `ChannelManager::get_channel()` currently returns cached state, not live read.

### Finding E (Low-Medium): watchdog recycle interactions can hide root causes

- Severity: **Low-Medium**
- Impact: can mask/compound failures if thresholds are too aggressive
- Confidence: **Medium**

---

## 6) Recommended fix plan (ordered)

## Phase 0 — must-do immediately (stability unblock)

1. **Transmitter reconnect channel fix**
   - In discovery ACK handling, propagate `ack_t.channel` back to manager as authoritative found channel.
   - Do not lock to raw scan channel when ACK reports another channel.

2. **Transmitter peer registration for confirm**
   - Re-register confirm peer with channel `0` before confirm sends.
   - Avoid non-zero fixed peer channel during reconnect confirm path.

3. **Flash both sides with latest build pair**
   - Ensure transmitter and receiver are both running builds containing the reconnect fix (mismatched firmware pairs can preserve failure behavior).

## Phase 1 — webserver resilience hardening

4. Receiver `httpd` send tolerance:
   - Use larger `send_wait_timeout` (e.g., 60s) for coexistence scenarios.

5. Chunk pacing:
   - Keep small chunking (1KB is acceptable) and yield briefly between sends to let WiFi/lwIP drain.

6. Keep progress heartbeat updates during chunking:
   - Preserve request-progress updates so liveness watchdog does not misclassify active transfers as stuck.

## Phase 2 — architecture hardening (prevent recurrence)

7. Channel authority contract (document + enforce):
   - `scan_channel` = temporary probe channel.
   - `ack_reported_channel` = receiver home channel authority.
   - `peer channel during reconnect` = 0 until stable connected state.

8. Channel manager live-truth option:
   - Add/enable a `get_live_channel()` path (`esp_wifi_get_channel`) and use it for lock decisions on reconnect transitions.

9. Move large common JS helper from inline HTML to static endpoint/file:
   - Reduces size of initial `/` response and lowers risk of mid-stream failure under RF contention.

10. Add telemetry counters and alert conditions:
   - `confirm_send_arg_failures`
   - `ack_channel_mismatch_count` (scan channel vs ACK channel)
   - `http_send_chunk_failures_by_stage`

---

## 7) Validation matrix (required before closure)

## Test A — reconnect correctness

- Force reconnect while receiver is on router channel 6.
- Pass criteria:
  - TX scan may probe other channels but final lock uses ACK-reported channel.
  - `connect_confirm` succeeds within retry window.
  - No repeated `ESP_ERR_ESPNOW_ARG` spam.

## Test B — webpage stability during reconnect pressure

- Open `/` repeatedly during induced reconnect cycles.
- Pass criteria:
  - No `send_chunk failed ... common_script_helpers` errors.
  - page render times stable and no `rc=-1` responses.

## Test C — long soak

- 4–8 hour run with periodic AP/STA disturbances.
- Pass criteria:
  - no persistent reconnect loops,
  - web UI remains accessible,
  - MQTT gate exits blocked state after reconnect.

## Test D — mixed firmware guard

- Verify behavior with intentionally mismatched TX/RX versions.
- Pass criteria:
  - explicit version-compatibility warning in logs,
  - no silent endless reconnect loop.

---

## 8) Practical conclusion

The immediate failure to present webpages is not only a web layer issue. The dominant failure mechanism is reconnect/channel mismatch in transmitter logic that created persistent radio churn; this then exposed HTTP send fragility on large page chunks.

Stability is expected after applying the reconnect channel-authority fix and peer-channel registration fix, then running the receiver webserver hardening settings as above.

---

## 9) Action checklist

- [ ] Confirm both devices flashed with the same fixed build pair.
- [ ] Verify no `Peer channel is not equal to the home channel` during reconnect.
- [ ] Verify receiver `/` loads without `send_chunk ... rc=45062`.
- [ ] Record 30-minute soak logs with periodic net diagnostics.
- [ ] If any failures persist, capture:
  - TX reconnect state transitions + confirm failures,
  - RX `NET` + `HTTP_PAGE` + `WEBSERVER` + `RX_CONN` logs,
  - webserver telemetry endpoint snapshot.

---

## 10) Implementation execution tracker (with mandatory legacy cleanup)

### Phase 0 — reconnect/channel authority unblock

Status: **In progress (core fixes applied)**

Implemented:

- TX discovery now propagates ACK-reported channel as authoritative result.
- TX reconnect peer registration in confirm path now uses channel `0`.
- TX build validates after changes.

Legacy/redundant cleanup completed in this phase:

- Removed stale cached-channel authority usage by making `ChannelManager::get_channel()` return live WiFi driver channel when available.
- Removed legacy-marker shim comments from LCD receiver ESP-NOW shared-module stub translation units (retained compile-safe empty files until include-path cleanup phase).

Remaining for phase close:

- Runtime validation on hardware: reconnect success without `ESP_ERR_ESPNOW_ARG` loops.

### Phase 1 — receiver HTTP resilience

Status: **In progress (hardening applied)**

Implemented:

- Chunked page send path in `page_generator.cpp`.
- Increased HTTP send timeout and added chunk-yield pacing.
- Request-progress liveness heartbeats to avoid false stuck-recycle.

Legacy/redundant cleanup target before phase close:

- Remove duplicate/unused page-send code branches once soak confirms stable page delivery under reconnect pressure.

### Phase 2 — structural cleanup after validation

Status: **Planned**

Planned legacy/redundant removals:

- Remove local ESP-NOW shim translation units in LCD receiver by switching all local includes to direct `esp32common` headers.
- Remove compatibility comments/bridges superseded by shared runtime modules.
- Final pass to remove dead branches tied to pre-redesign reconnect behavior.
