# ESP-NOW Non-Critical Traffic Analysis (TX/RX) — 2026-05-13

## Scope

This review focuses on:

1. What ESP-NOW traffic is present when there is no web traffic and MQTT is gated/offline.
2. Which message classes are currently dominating airtime.
3. Practical options to reduce non-critical background traffic without breaking reconnect reliability.

Primary code paths reviewed:

- `ESPnowtransmitter2/src/espnow/discovery_task.cpp`
- `esp32common/espnow_common_utils/rx_route_registry.cpp`
- `esp32common/espnow_common_utils/espnow_standard_handlers.cpp`
- `esp32common/espnow_common_utils/espnow_tx_scheduler.cpp`
- `ESPnowtransmitter2/src/espnow/tx_reconnect_manager.cpp`
- `ESPnowtransmitter2/src/espnow/heartbeat_manager.cpp`
- `ESPnowtransmitter2/src/espnow/version_beacon_manager.cpp`
- `esp32common/espnow_common_utils/rx_heartbeat_manager.cpp`
- `esp32common/include/esp32common/config/timing_config.h`

---

## Executive Summary

The observed load is real and expected from the current reconnect design:

- The dominant traffic while disconnected is **discovery PROBE/ACK** control traffic, not HTTP/MQTT payload.
- `ESP_ERR_ESPNOW_NO_MEM` warnings with `type=1 len=6` are discovery ACK sends (`msg_ack`), not heartbeat ACK.
- Current discovery cadence is aggressive by design (`probe_interval_ms=200`, channel dwell `2000 ms`, 13-channel sweep, repeated scans), so even with no user activity there is continuous control-plane traffic.

In short: when link is down, the system is *not idle*. It is actively trying to recover link, and that recovery traffic is what is stressing ESP-NOW buffers.

---

## What traffic exists when “nothing is happening”

## 1) Disconnected / reconnecting state (primary source of load)

### Transmitter sends PROBE repeatedly during scan

From current timing:

- `PROBE_INTERVAL_MS = 200 ms`
- `TRANSMIT_DURATION_PER_CHANNEL_MS = 2000 ms`
- Channels scanned: 1..13

Implication per scan pass:

- ~10 probes per channel (`2000/200`)
- ~130 probes for phase-1 full sweep
- Plus optional weighted phase-2 sweep around last known channel

So the reconnect subsystem alone can sustain roughly ~4–5 probe sends/sec over time while disconnected.

### Receiver responds with ACK (throttled but non-zero)

Receiver ACK throttle behavior while disconnected:

- Normal pressure: 450 ms throttle
- Elevated pressure: 1200 ms throttle

So receiver can still emit up to:

- ~2.2 ACK/sec (normal)
- ~0.83 ACK/sec (elevated pressure)

This directly matches the observed `suppressed=<n>` NO_MEM warn pattern (10-second log bucket).

### Why this still appears as “flooding”

Even though ACK is throttled, probe production remains frequent and persistent while reconnect is active. Under RF/channel contention, control queue retries and NO_MEM backoff still produce pressure.

---

## 2) Handshake traffic around reconnect boundary

After probe lock:

- TX sends `msg_connect_confirm`
- RX sends `msg_connect_confirm_ack`

Retry behavior:

- TX confirm timeout loop retries every ~2 s (bounded)
- RX confirm-ack retry interval is 250 ms, up to 20 retries

This is short-lived but can add bursts near reconnect transitions.

---

## 3) Connected idle baseline traffic

Once connected and stable, periodic control traffic still exists:

- Heartbeat: every 10 s (TX -> RX)
- Heartbeat ACK: one per heartbeat (RX -> TX)
- Temperature report: currently sent with each heartbeat cycle (TX -> RX)
- Version beacon: periodic 30 s (plus event-driven immediate sends on MQTT/Ethernet/config state changes)

This is relatively modest compared with disconnected scan traffic.

---

## Message-type mapping and scheduler policy notes

Message type enum:

- `msg_probe` = type 0
- `msg_ack` = type 1

Scheduler policies important to this issue:

- `msg_probe`: min gap 250 ms, retry attempts 3
- `msg_ack`: min gap 150 ms, retry attempts 12
- `msg_heartbeat`: min gap 800 ms, retry attempts 4
- `msg_heartbeat_ack`: min gap 80 ms, retry attempts 8

Critical implementation detail:

- The consecutive NO_MEM escalation counter is specifically tied to ACK send failures in the scheduler path.

---

## Why this appears when web/MQTT are quiet

Because reconnect traffic is independent from HTTP and MQTT:

- MQTT is intentionally gated by ESP-NOW connection state.
- Web request count may be zero.
- Yet reconnect scan still runs and emits probe traffic continuously until link is restored.

Therefore no HTTP/MQTT traffic does **not** imply ESP-NOW radio idleness.

---

## Recommendations to reduce non-critical traffic

## Priority A (low risk, high benefit)

1. **Adaptive probe interval/backoff while disconnected**
   - Keep fast probe only in an initial short acquisition window.
   - Then decay to slower probe intervals (e.g. 200 ms -> 500 ms -> 1000 ms).
   - Preserve immediate reset to fast mode on any positive signal.

2. **Reduce per-channel probe count**
   - Current 2 s dwell with 200 ms probe interval yields ~10 probes/channel.
   - Consider one early probe + one late probe per dwell (or interval >=500 ms) when in long-running reconnect mode.

3. **Pressure-coupled scan rate**
   - When receiver reports/infers elevated NO_MEM pressure, slow transmitter probe cadence automatically.

## Priority B (moderate risk)

4. **Introduce “quiet reconnect mode” after prolonged miss window**
   - After N failed scan cycles, switch to sparse background scan schedule.
   - Exit quiet mode immediately on any ACK/probe activity.

5. **Tighten connect-confirm retry cadence under pressure**
   - Keep reliability, but stretch retry interval when scheduler reports repeated NO_MEM.

6. **Gate optional telemetry during reconnect pressure**
   - Defer non-essential event summary/version/status pushes while not fully settled.

## Priority C (policy tuning)

7. **Re-evaluate ACK throttle values against RF environment**
   - Current 450/1200 ms may still be too chatty for constrained conditions.
   - Increase disconnected ACK throttle in degraded states.

8. **Version beacon periodic interval**
   - If acceptable for UX, increase periodic beacon from 30 s to 60 s when stable and unchanged.

---

## Suggested concrete implementation plan

### Phase 1 (instrumentation + safe tuning)

- Add per-type rolling counters/rates in logs (probe tx, probe rx, ack tx, ack rx, hb tx/rx) every 10 s.
- Add reconnect phase marker in logs (fast-scan vs sparse-scan mode).
- Increase probe interval in sustained reconnect windows.

### Phase 2 (adaptive behavior)

- Implement stateful reconnect cadence profile:
  - `FAST_ACQUIRE` (short)
  - `NORMAL_RECONNECT`
  - `SPARSE_BACKGROUND_RECONNECT`
- Couple profile transitions to NO_MEM/ACK success signals.

### Phase 3 (validation)

- Soak test with no web and MQTT disabled/gated.
- Verify:
  - Significant drop in probe+ack rate during prolonged disconnect
  - No regression in reconnect time when peer returns
  - Reduced `ESP_ERR_ESPNOW_NO_MEM` warnings

---

## Final conclusion

The current behavior is consistent with an aggressive reconnect strategy, not a hidden web/MQTT workload.

The largest lever is discovery cadence control during prolonged disconnects. Reducing probe pressure (while preserving fast initial acquisition) is the highest-impact path to cut non-critical ESP-NOW traffic and reduce NO_MEM pressure.
