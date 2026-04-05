# Time Transitions Snapshot — Send Policy Analysis
**Date:** 2026-03-31  
**Scope:** Transmitter (`ESPnowtransmitter2`) → Receiver (`espnowreceiver_2`)  
**Struct:** `time_transitions_snapshot_t` (95 bytes packed, CRC32-validated)

---

## 1. Current Behaviour

### 1.1 What triggers a resend today

`send_time_transitions_snapshot_if_needed()` is called from **two sites** in
`version_beacon_manager.cpp`:

| Call site | When |
|---|---|
| `VersionBeaconManager::init()` | Once at transmitter boot (Phase 8 of startup) |
| `VersionBeaconManager::update()` | Every iteration of `loop()` (no timer gate here) |

`send_time_transitions_snapshot_if_needed()` itself applies a two-condition gate before sending:

```cpp
const bool same_revision = time_snapshot_sent_ &&
    (snapshot.revision == last_sent_time_snapshot_revision_);
if (same_revision &&
    ((now_ms - last_time_snapshot_send_ms_) < TIME_SNAPSHOT_RESEND_INTERVAL_MS)) {
    return false;   // skip
}
```

`TIME_SNAPSHOT_RESEND_INTERVAL_MS = 30 000` (30 s).

**Effective send triggers today:**

| Trigger | Condition |
|---|---|
| First send ever | `!time_snapshot_sent_` — fires immediately when snapshot is valid and receiver is connected |
| Revision change | `snapshot.revision != last_sent_time_snapshot_revision_` — fires immediately on next `update()` call |
| Periodic keep-alive | Same revision, but 30 s has elapsed since last send — fires unconditionally |

### 1.2 What triggers a snapshot revision change (transmitter side)

`refresh_time_transition_snapshot_cache()` in `ethernet_utilities.cpp` is called from:

| Call site | Frequency | Notes |
|---|---|---|
| `get_ntp_time()` — initial timezone setup path | Once, on first NTP sync | Sets default POSIX TZ, then calls refresh |
| `get_ntp_time()` — every successful NTP response | Every 30 minutes | Recomputes transitions from current `time()` |
| `configure_timezone_from_location_internal()` | Every 30 min (after geo success), every 30 s (while retrying) | Called from the `ethernet_utilities_task` background task |

Inside `refresh_time_transition_snapshot_cache()`, a content-equal comparison is performed. The revision increments **only if** one of the following actually changed:

- `geolocation_valid` flag
- `current_utc_offset_min`
- `timezone_name`
- `timezone_abbrev`
- `transition_count`
- Any individual `transition_unix_utc`, `offset_before_min`, or `offset_after_min`

In normal steady-state operation (correct timezone, no DST event imminent) all these values are
stable. A revision change happens in practice only on:

1. First geolocation success (geolocation_valid: false → true)
2. DST transition (~2× per year in affected locales)
3. Physical location change (timezone name changes)
4. Transmitter reboot (resets revision counter to 0, then immediately to 1 after first snapshot)

---

## 2. Why the 30-Second Resend Exists

There is **no explicit "send on reconnect" trigger** anywhere in the codebase.

When the ESP-NOW link is re-established (receiver reboots, channel changes, brief loss), the
`CONNECTING → CONNECTED` state transition in `tx_connection_handler.cpp` does:

```cpp
} else if (new_state == EspNowConnectionState::CONNECTED) {
    TxSendGuard::notify_connection_state(true);
    ChannelManager::instance().lock_channel(channel, "TX_CONN");
    HeartbeatManager::instance().reset();
    TxStateMachine::instance().on_connected(channel);
    // ← No VersionBeaconManager call here
}
```

`VersionBeaconManager::send_version_beacon(true)` **is** called on reconnect (implicitly, because
`init()` already ran and `update()` is polled). But `send_time_transitions_snapshot_if_needed()` will
only fire if either:
- The revision changed while disconnected (unlikely unless a DST event occurred), or  
- 30 seconds have elapsed since the last send (the periodic resend).

**The 30-second periodic resend is therefore acting as the reconnection-recovery mechanism** — a
receiver that just reconnected will get the snapshot within 30 seconds, not immediately.

---

## 3. Problems with the Current Approach

### P1 — Unnecessary airtime for stable data
The snapshot is 95 bytes. Sent every 30 seconds regardless of whether the data has changed or the
receiver is freshly connected. Over a 24-hour period that is 2,880 sends × 95 bytes =
**~274 KB of identical data** transmitted over ESP-NOW for no reason.

### P2 — Delayed delivery on reconnection
A receiver that reconnects at t=0 may wait up to 30 seconds before receiving the snapshot. During
that window the `/debug` page will show placeholder dashes and "⚠ Transmitter transition data not
received yet" even though the transmitter has perfectly good data ready.

### P3 — No notification path from ethernet_utilities → VersionBeaconManager
When `refresh_time_transition_snapshot_cache()` detects a real content change (revision++)
it has no way to tell `VersionBeaconManager` about it immediately. The only discovery path is
`send_time_transitions_snapshot_if_needed()` polling `get_time_transition_snapshot()` on the next
`update()` call — which happens every `loop()` iteration, so lag is minimal (~10 ms). This is
acceptable as-is; flagging for completeness only.

### P4 — The periodic resend comment is misleading
The code comment says `"Event-driven, low-churn snapshot: send once after boot and on revision
changes."` but the 30-second timer means it is also sending periodically, contradicting the
description.

---

## 4. Correct Send Policy

The snapshot should be sent on exactly three events, and **never** on a wall-clock timer:

| Event | Mechanism |
|---|---|
| **ESP-NOW connected** (including reconnection) | Explicit call from `CONNECTING → CONNECTED` state callback in `tx_connection_handler.cpp` |
| **Snapshot revision incremented** (DST flip, geolocation first success, location change) | Detected on next `update()` poll — already works today; no change needed |
| **Boot** (transmitter first powers up) | Already handled: `VersionBeaconManager::init()` calls `send_time_transitions_snapshot_if_needed()` |

A deep safety-net resend at a long interval (e.g., 1 hour or 6 hours) is optional but should be
considered only as a last-resort guard against a missed initial send — not as the primary delivery
mechanism.

---

## 5. Recommendations

### R1 — Add an explicit "send on reconnect" trigger  *(HIGH PRIORITY)*
In `tx_connection_handler.cpp`, inside the `new_state == EspNowConnectionState::CONNECTED` branch:

```cpp
} else if (new_state == EspNowConnectionState::CONNECTED) {
    TxSendGuard::notify_connection_state(true);
    ChannelManager::instance().lock_channel(channel, "TX_CONN");
    HeartbeatManager::instance().reset();
    TxStateMachine::instance().on_connected(channel);

    // ← ADD THIS:
    // Force-send the snapshot to the newly connected receiver.
    // Ensures receiver gets DST data immediately regardless of revision state.
    VersionBeaconManager::instance().force_send_time_snapshot();
}
```

This requires a new public method on `VersionBeaconManager` (or a flag to bypass the same-revision
guard). Simplest implementation — add a `force_send_time_snapshot()` method that clears
`time_snapshot_sent_` (or sets `last_time_snapshot_send_ms_ = 0`) and calls
`send_time_transitions_snapshot_if_needed()` directly.

### R2 — Remove (or greatly increase) the 30-second periodic resend  *(HIGH PRIORITY, depends on R1)*
Once R1 is in place, the periodic resend has no correctness role.

**Option A — Remove it entirely (clean):**  
Delete the `TIME_SNAPSHOT_RESEND_INTERVAL_MS` gate; only send on boot and revision change.  
Requires trusting that R1 covers the reconnect case (which it does).

**Option B — Deep safety net at 1–6 hours (conservative):**  
Change `TIME_SNAPSHOT_RESEND_INTERVAL_MS = 60 * 60 * 1000` (1 hour).  
Provides a final fallback against any missed send while eliminating the noise.  
Recommended if R1 has not yet been deployed to production hardware.

### R3 — Update the comment in `version_beacon_manager.h`  *(LOW PRIORITY)*
The `update()` docstring says "Sends periodic heartbeat beacon every 30 seconds" but makes no
mention of the snapshot. The class-level comment also says nothing about snapshot resend. Update
both to accurately reflect post-R1/R2 behaviour.

### R4 — Consider a `notify_snapshot_changed()` callback from ethernet_utilities  *(LOW PRIORITY)*
Currently `refresh_time_transition_snapshot_cache()` detects a real content change but has no path
to notify `VersionBeaconManager` synchronously. Today the lag before `update()` polls and detects
the revision change is one `loop()` tick (~10 ms), which is fine. However, making this explicit
would remove the temporal coupling between the two modules. Not urgent; flag for a future cleanup.

---

## 6. Implementation Plan

**Phase 1 — Add `force_send_time_snapshot()` to `VersionBeaconManager`** (`.h` + `.cpp`)
- Public method; clears `last_time_snapshot_send_ms_` and calls
  `send_time_transitions_snapshot_if_needed()`.

**Phase 2 — Call it from `tx_connection_handler.cpp`** on `CONNECTED` state entry.

**Phase 3 — Change `TIME_SNAPSHOT_RESEND_INTERVAL_MS` to 1 hour** (or remove it), update
comment, rebuild and upload transmitter.

**Phase 4 — Validate on hardware:**
- Reboot receiver while transmitter is running → snapshot must arrive within ~1 s of ESP-NOW
  reconnection (not 30 s).
- Simulate DST by manually calling `refresh_time_transition_snapshot_cache()` with changed TZ data
  → snapshot must be re-sent immediately on next `update()` cycle.

**Estimated code change:** ~25 lines across 3 files (`version_beacon_manager.h`,
`version_beacon_manager.cpp`, `tx_connection_handler.cpp`).

---

## 7. Files Affected

| File | Change |
|---|---|
| `ESPnowtransmitter2/.../src/espnow/version_beacon_manager.h` | Add `force_send_time_snapshot()` public declaration; update `TIME_SNAPSHOT_RESEND_INTERVAL_MS` or remove |
| `ESPnowtransmitter2/.../src/espnow/version_beacon_manager.cpp` | Implement `force_send_time_snapshot()`; update periodic-resend gate |
| `ESPnowtransmitter2/.../src/espnow/tx_connection_handler.cpp` | Add `VersionBeaconManager::instance().force_send_time_snapshot()` call in `CONNECTED` state entry |

No receiver changes required.
