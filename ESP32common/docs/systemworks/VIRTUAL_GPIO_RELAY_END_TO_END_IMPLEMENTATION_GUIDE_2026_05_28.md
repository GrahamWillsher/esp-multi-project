# Virtual GPIO Relay End-to-End Implementation Guide (MQTT)

**Date:** 2026-05-28  
**Scope:** Full implementation path from transmitter virtual GPIO writes to receiver physical GPIO outputs across all receiver variants in the split architecture.

---

## 1) Purpose

This document is the implementation playbook for relay offload.

- **Transmitter** keeps sequencing authority.
- **Transmitter** uses virtual GPIO IDs (`200`–`203`, optional `204` future).
- **MQTT** carries relay command state (`relay_mask`, `request_id`, `seq`).
- **Receiver** maps relay bits to board-local physical GPIO pins.
- **Receiver UI** (`/receiver/gpiopins`) shows commanded vs actual state.

---

## 2) Canonical Semantic Contract

### 2.1 Virtual GPIO IDs

- `200` → `GPIO_NEGATIVE_CONTACTOR_VIRTUAL` (bit0)
- `201` → `GPIO_PRECHARGE_VIRTUAL` (bit1)
- `202` → `GPIO_POSITIVE_CONTACTOR_VIRTUAL` (bit2)
- `203` → `GPIO_BMS_POWER_VIRTUAL` (bit3)
- `204` → optional future extension (`SECOND_BATTERY_CONTACTORS`)

### 2.2 MQTT Topics

- Command: `batt-emu/mqtt-v1/rx/cmd/control/relay`
- ACK: `batt-emu/mqtt-v1/tx/ack/relay`
- Runtime state: `batt-emu/mqtt-v1/tx/state/runtime/relay`

### 2.3 Delivery Policy

- Command: QoS 1, retained false
- ACK: QoS 1, retained false
- Runtime state: QoS 1, retained true

---

## 3) Transmitter Implementation Steps

## 3.1 Add virtual GPIO constants + helper API

**Files:**
- `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/devboard/hal/hal.h`
- `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/devboard/hal/hal.cpp`

**Required implementation:**
1. Define virtual IDs (`200`–`203`) in a shared header or HAL header.
2. Implement `halDigitalWrite(uint16_t pin, bool level)` wrapper:
   - If pin is virtual: update relay shadow mask, enqueue/publish MQTT relay command.
   - If pin is physical: pass through to `digitalWrite`.
3. Implement matching wrappers for `halPinMode` and `halDigitalRead`:
   - Virtual pins do not touch hardware directly.
   - `halDigitalRead` for virtual pins reads shadow state.

## 3.2 Route contactor control writes through HAL wrapper

**File:**
- `ESPnowtransmitter2/espnowtransmitter2/src/battery_emulator/communication/contactorcontrol/comm_contactorcontrol.cpp`

**Required implementation:**
1. Replace raw relay `digitalWrite(...)` callsites with wrapper (`halDigitalWrite` or equivalent internal abstraction).
2. Keep sequence timing in transmitter logic (precharge, settle delays).
3. Publish relay command only on state change (event-driven).

## 3.3 Build relay command payload

**File candidates:**
- `ESPnowtransmitter2/espnowtransmitter2/src/network/mqtt_manager.cpp`
- or dedicated relay control module under `src/control/*`

**Required implementation:**
1. Maintain `relay_shadow_mask`.
2. On change, build payload:
   - `request_id`, `seq`, `schema`, `mode`, `relay_mask`, `changed_mask`, `require_ack`, `source`, `ts_ms`.
3. Publish to command topic.
4. Start ACK timeout tracking keyed by `request_id`.

## 3.4 Handle ACK + runtime state

**Required implementation:**
1. Subscribe to ACK topic and runtime-state topic.
2. Correlate ACK by `request_id`.
3. Mark pending command complete on successful ACK.
4. Use runtime-state retained message as source-of-truth replay after reconnect.

---

## 4) Receiver Implementation Steps (All Receiver Variants)

## 4.0 Mandatory semantic device mapping (required)

Each receiver variant **must** define a readable semantic mapping from relay bit to physical GPIO.

This mapping is mandatory in both code and docs. It must be readable as semantic relay names (for example: Positive relay, Negative relay, Precharge relay, BMS power), not only raw bit indices.

**Required readable mapping format (per receiver):**

| Semantic Relay Name | Bit | Virtual GPIO ID | Physical GPIO |
|---|---|---|---|
| Negative relay / negative contactor | bit0 | 200 | GPIOxx |
| Precharge relay | bit1 | 201 | GPIOyy |
| Positive relay / positive contactor | bit2 | 202 | GPIOzz |
| BMS power relay | bit3 | 203 | GPIOww |

**Policy:**
1. Semantic relay names stay consistent across all receiver variants.
2. Physical GPIO values are receiver-specific.
3. If a relay semantic is unsupported on a receiver, mark it `NOT_SUPPORTED` and return a clear ACK/error code.

## 4.1 Define board-local mapping table

**Files (per receiver variant):**
- `espnowreceiver_2/include/hal/hardware_config.h`
- `espnowreceiver_LCD/include/.../hardware_config.h` (or equivalent)

**Required implementation:**
1. Define semantic-to-physical mapping table for each board variant.
2. Keep semantic relay names unchanged across variants.
3. Reuse the same mapping table for relay-apply logic and `/receiver/gpiopins` output so UI and hardware state always refer to the same semantic map.

**Current LilyGo non-touch example mapping:**
- bit0 → GPIO10
- bit1 → GPIO16
- bit2 → GPIO21
- bit3 → GPIO43

## 4.2 Implement relay command subscriber

**File candidates (per receiver):**
- `src/mqtt/mqtt_client.cpp`
- or equivalent command handler module

**Required implementation:**
1. Subscribe to command topic.
2. Parse/validate payload (`schema`, `relay_mask`, `request_id`, `seq`).
3. Deduplicate by `request_id`.
4. Apply all relay bits atomically to mapped GPIO outputs.
5. Publish ACK and runtime state.

## 4.3 Implement `/receiver/gpiopins` state model

**Required implementation:**
1. Keep latest transmitted state (`relay_mask`, `request_id`, timestamp).
2. Poll or sample actual GPIO readback (`digitalRead`) for mapped relay pins.
3. Compute mismatch status per relay.
4. Expose via `/api/receiver/relaystatus`.
5. Render on `/receiver/gpiopins` with match/mismatch indicators.

---

## 5) Payload Schemas (Implementation Baseline)

## 5.1 Command

```json
{
  "request_id": "relay-20260528-0000001",
  "seq": 1,
  "schema": 1,
  "mode": "set",
  "relay_mask": 5,
  "changed_mask": 4,
  "require_ack": true,
  "source": "virtual_gpio",
  "ts_ms": 0
}
```

## 5.2 ACK

```json
{
  "request_id": "relay-20260528-0000001",
  "seq": 1,
  "success": true,
  "code": "OK",
  "applied_mask": 5,
  "ts_ms": 0
}
```

## 5.3 Runtime state

```json
{
  "schema": 1,
  "applied_mask": 5,
  "relay_states": {
    "negative": true,
    "precharge": false,
    "positive": true,
    "bms_power": false
  },
  "last_request_id": "relay-20260528-0000001",
  "ts_ms": 0
}
```

---

## 6) Acceptance Criteria

Implementation is complete only when all checks pass:

1. Transmitter contactor flow uses virtual GPIO wrappers (no direct relay GPIO writes in transmitter sequence path).
2. Relay command publishes only on state transition.
3. Receiver applies mapped physical GPIO outputs correctly for each bit.
4. ACK correlation works with `request_id` and timeout handling.
5. Runtime-state retained replay works after reconnect.
6. `/receiver/gpiopins` shows transmitted state, actual GPIO state, and mismatch status.
7. Duplicate command (`request_id`) is idempotent.
8. Boot default safe state = all relays OFF (`LOW`) unless explicit command says otherwise.

---

## 6.1 Initial implementation scope (trace-first)

Yes — this is possible in this implementation.

For the initial phase, implement only the trace path needed to verify correct switching from transmitter virtual GPIO to `/receiver/gpiopins`:

1. Transmitter virtual GPIO writes (`200`–`203`) produce `relay_mask` publish events.
2. Receiver stores commanded state (`relay_mask`, `request_id`, timestamp).
3. Receiver applies mapped physical GPIO outputs.
4. Receiver reads actual mapped GPIO levels (`digitalRead`).
5. `/receiver/gpiopins` displays commanded vs actual for each semantic relay.
6. API/page reports per-relay `OK`/`MISMATCH`.

**Minimum trace-first fields required on `/receiver/gpiopins`:**
- Semantic relay name (Negative / Precharge / Positive / BMS Power)
- Bit index and virtual GPIO ID
- Commanded state (from MQTT payload)
- Mapped physical GPIO pin
- Actual GPIO readback (`HIGH/LOW`)
- Match status (`OK` or `MISMATCH`)

Advanced hardening (deep timeout analytics, long-soak metrics, extension bit `204`) can be completed after this trace-first milestone is passing.

---

## 7) Test Matrix (Minimum)

1. **Happy path**: `0b0000 -> 0b0011 -> 0b0101 -> 0b0000` and verify ACK/state each step.
2. **Duplicate command**: send same `request_id` twice; ensure single apply + dedup code.
3. **Out-of-order seq**: ensure receiver applies by request identity and reports clearly.
4. **Reconnect replay**: restart broker/client and verify retained runtime state recovery.
5. **Mismatch detection**: force GPIO mismatch (test harness/stub) and confirm UI flag.
6. **Timeout path**: drop ACK and verify transmitter timeout handling/logging.

---

## 8) Known Outliers and Policy

1. **Virtual 204** is future extension only; keep out of 4-relay core flow until hardware exists.
2. **Board-specific pin differences** are expected; semantic mapping must remain stable.
3. **ACK is transport/apply acknowledgment**, not physical contact proof; use runtime readback/telemetry for physical truth.

---

## 9) Implementation Order (Recommended)

1. Transmitter HAL wrapper + shadow mask.
2. Transmitter command publish + ACK tracking.
3. Receiver command handler + atomic GPIO apply.
4. Receiver ACK + runtime-state publish.
5. `/receiver/gpiopins` API + page.
6. End-to-end tests and soak validation.

---

## 10) Related Documents

- `esp32common/docs/systemworks/LILYGO_T_DISPLAY_S3_RELAY_GPIO_AVAILABILITY.md`
- `ESPnowtransmitter2/espnowtransmitter2/PROJECT_ARCHITECTURE_MASTER.md`
- `esp32common/docs/systemworks/MQTT_ONLY_TRANSPORT_FEASIBILITY_2026_05_14.md`
