# Component Reboot Orchestration Review

**Date:** 2026-03-16  
**Scope:** Receiver-initiated transmitter reboot flow for `/transmitter/battery` and `/transmitter/inverter`  
**Goal:** Make all transmitter reboots explicitly initiated by the receiver, but only after the transmitter has safely persisted the requested changes.

---

## Executive Summary

The current OTA flow is the correct architectural model:

1. The receiver sends the OTA payload.
2. The transmitter validates and stages the update.
3. The transmitter reports `ready_for_reboot`.
4. The receiver sends exactly one reboot command.

The battery/inverter flows do **not** currently follow that model.

### What is wrong today

- The receiver web UI currently treats local API success as if the transmitter has safely applied and saved the change.
- The transmitter component selection handlers historically rebooted immediately after saving.
- The receiver pages then also sent an explicit reboot command after the save flow completed.
- Result: **double reboot risk**, plus no reliable proof that the transmitter actually saved the requested configuration before reboot.

### Core conclusion

**The reboot owner should be the receiver, but the authority to allow reboot must come from the transmitter after successful persistence.**

That means battery/inverter should adopt the same control pattern as OTA:

- **Receiver owns the reboot command**
- **Transmitter owns the “safe to reboot now” confirmation**

The safest and cleanest implementation is:

- send a **component-change transaction** from the receiver,
- persist and verify it on the transmitter,
- send back a **persisted ACK / ready-for-reboot ACK**,
- let the receiver UI poll local status,
- only then send a single reboot command.

---

## Current Code Findings

## 1) OTA already implements the right handshake

### Receiver side
`espnowreceiver_2/lib/webserver/pages/ota_page.cpp`

The receiver uploads OTA, then polls `/api/transmitter_ota_status`. It does **not** reboot immediately after upload. It waits until the transmitter reports:

- `ready_for_reboot = true`
- `last_success = true`

Only then does the receiver send the reboot command.

### Transmitter side
`ESPnowtransmitter2/espnowtransmitter2/src/network/ota_manager.cpp`

The transmitter only reports ready after `Update.end(true)` succeeds. That is a strong safety point:

- OTA data has been fully written
- OTA finalization succeeded
- reboot is still deferred until receiver command

This is the best current reference design.

---

## 2) Battery/inverter currently do not have an equivalent persisted-ready handshake

### Receiver page behavior
- `espnowreceiver_2/lib/webserver/pages/battery_settings_page.cpp`
- `espnowreceiver_2/lib/webserver/pages/inverter_settings_page.cpp`

These pages:

1. call receiver-local HTTP APIs such as:
   - `/api/set_battery_type`
   - `/api/set_battery_interface`
   - `/api/set_inverter_type`
   - `/api/set_inverter_interface`
2. treat the HTTP success response as completion
3. start the reboot countdown and eventually call `/api/reboot`

### Receiver API behavior
`espnowreceiver_2/lib/webserver/api/api_type_selection_handlers.cpp`

The APIs update receiver-local cached selection state, attempt to send the ESP-NOW message, and then return success JSON.

Important finding:

- these APIs currently return success even if the ESP-NOW send fails
- they log a warning, but still return `{"success":true,...}`

So the web UI can believe the change is complete even when the transmitter never received it.

This is a major safety gap.

---

## 3) The transmitter used to reboot immediately inside the component handlers

`ESPnowtransmitter2/espnowtransmitter2/src/espnow/component_catalog_handlers.cpp`

### Type selection path
`handle_component_config(...)`

This path updates component type selections and previously rebooted immediately after applying them.

### Interface selection path
`handle_component_interface(...)`

This path stores interface selections and previously rebooted immediately after applying them.

That means the receiver page and transmitter handler were both trying to own reboot timing.

That is the direct cause of the observed “reboots twice” behavior.

---

## 4) Persistence quality differs between type selection and interface selection

### Type selection persistence is relatively strong
`ESPnowtransmitter2/espnowtransmitter2/src/system_settings.cpp`

Battery type / inverter type changes go through `SystemSettings`, which:

- writes values through NVS helpers
- calls `nvs_commit(...)`
- returns `bool` success/failure

This is a decent persistence contract.

### Interface selection persistence is weaker
`ESPnowtransmitter2/espnowtransmitter2/src/espnow/component_catalog_handlers.cpp`

Interface selection currently uses `Preferences` directly:

- opens namespace
- writes `BATTCOMM` and `INVCOMM`
- closes preferences
- does not explicitly check each write result
- does not read back and verify values
- does not send any ACK to the receiver

This is functionally workable, but not ideal for a “reboot only when safe” flow.

---

## 5) Existing codebase already has a better model for non-OTA config changes

### Network config
`ESPnowtransmitter2/espnowtransmitter2/src/espnow/network_config_handlers.cpp`

This path already does the right thing conceptually:

1. validate input
2. process in background
3. save to NVS
4. send an ACK back to the receiver
5. ACK explicitly says `OK - reboot required`

### MQTT config
`ESPnowtransmitter2/espnowtransmitter2/src/espnow/mqtt_config_handlers.cpp`

This path also uses:

- validation
- save/apply
- explicit ACK to receiver

So the project already contains a clear architectural precedent:

> **persist first, acknowledge second, reboot later**

Battery/inverter should be brought into line with this approach.

---

## Safety Assessment

## What must be true before the receiver is allowed to issue reboot

At minimum, the transmitter must confirm all of the following:

1. the request was received and parsed correctly
2. the requested values were valid
3. the requested values were written to persistent storage
4. the stored values match what the receiver requested
5. the transmitter is now in a `reboot required` state

If those conditions are not met, the receiver should **not** send reboot.

---

## Recommended Architecture

## Preferred design: explicit component persisted ACK

This is the recommended long-term design.

### High-level flow

1. User presses save on battery/inverter page.
2. Receiver sends a **component-change transaction** to the transmitter.
3. Transmitter validates the request.
4. Transmitter writes the change to NVS.
5. Transmitter verifies persistence.
6. Transmitter sends back a **component ACK** indicating:
   - success/failure
   - values saved
   - reboot required yes/no
   - request id / correlation id
7. Receiver records that ACK locally.
8. Receiver web page polls a local status endpoint.
9. When status becomes `ready_for_reboot`, receiver starts the same countdown flow used by OTA.
10. Receiver sends exactly one reboot command.

This matches the OTA control philosophy while staying native to the ESP-NOW config path.

---

## Why ACK is better than a transmitter HTTP status endpoint here

A transmitter HTTP status endpoint is possible, but it is not the best primary design for battery/inverter.

### Why it is weaker
- it depends on transmitter IP availability
- it depends on transmitter HTTP service availability
- it splits the configuration path across two transports unnecessarily
- the actual setting update was sent over ESP-NOW, so the most reliable confirmation should also come back over ESP-NOW

### Why ACK is better
- same transport as the config command
- works even if HTTP/IP knowledge is stale or temporarily unavailable
- lower latency
- simpler transactional reasoning
- aligns with existing network/MQTT ACK patterns

### Best compromise
Use **ESP-NOW ACK as the authoritative signal**, and optionally mirror that state into a receiver-local HTTP endpoint for the web page to poll.

That gives the UI the OTA-like polling experience without making transmitter HTTP the source of truth.

---

## Detailed Design Recommendation

## 1) Introduce a component-change transaction model

### Preferred option
Create a new protocol message pair such as:

- `msg_component_apply_request`
- `msg_component_apply_ack`

### Why a new combined request is preferable
Today the battery page may send:

- one message for type
- one message for interface

That creates sequencing ambiguity and partial-update risk.

A single transaction message is cleaner.

### Suggested request fields
- `request_id` or `sequence`
- desired `battery_type`
- desired `inverter_type`
- desired `battery_interface`
- desired `inverter_interface`
- bitmask of which fields are being changed
- checksum / CRC

### Suggested ACK fields
- `request_id`
- `success`
- `reboot_required`
- `persisted_mask`
- final persisted values
- optional config version(s)
- short error string

This lets the receiver prove that:

- the ACK corresponds to the request it sent
- the transmitter saved exactly the values requested
- reboot is now safe and required

---

## 2) Receiver HTTP API should become asynchronous, not “save completed”

The receiver web API should not pretend the operation is finished immediately.

### Current problem
`/api/set_battery_type` and friends return success too early.

### Recommended behavior
The receiver-local HTTP API should instead:

1. generate a `request_id`
2. dispatch the ESP-NOW request
3. store transaction state locally as `pending`
4. return something like:

```json
{"success":true,"request_id":12345,"state":"pending"}
```

Then the web page polls a receiver-local status endpoint such as:

- `/api/component_apply_status?request_id=12345`

Possible states:

- `pending`
- `persisted`
- `ready_for_reboot`
- `failed`
- `timed_out`

This reproduces the OTA UX pattern cleanly.

---

## 3) Transmitter should never auto-reboot for these page-triggered component changes

If the design goal is “all reboot commands are instigated from the receiver”, then the transmitter must not call `ESP.restart()` as part of:

- component type change handling
- component interface change handling

Instead it should:

- persist
- verify
- send ACK
- wait for receiver reboot command

This should be a hard architectural rule.

---

## 4) Component persistence should be upgraded to a stronger contract

### Type settings
Type persistence is already reasonably solid because `SystemSettings` uses `nvs_commit(...)`.

### Interface settings
Interface persistence should be improved.

### Recommended improvements
- move interface persistence into a dedicated manager instead of raw `Preferences` calls inside the ESP-NOW handler
- check all write return values
- store the pair as a single versioned structure
- include CRC / integrity protection similar to `SettingsManager`
- optionally re-open read-only and verify the stored values before ACKing success

### Why this matters
The receiver should only reboot the transmitter when the transmitter has positively proved:

- values are durable
- values match the requested configuration

---

## 5) Add explicit timeout and retry rules

Receiver-side transaction handling should define:

- ACK timeout, for example 3-5 seconds
- retry count, for example 1-2 retries
- failure state if ACK never arrives
- no reboot when timeout occurs

The page should show clear state transitions:

- `Saving configuration...`
- `Waiting for transmitter confirmation...`
- `Configuration saved - reboot required`
- `Sending reboot command...`

This is both safer and easier to reason about than the current immediate-success model.

---

## Suggested End-State Flow

## Battery / inverter page flow

1. User changes type and/or interface.
2. User presses save.
3. Receiver local API creates a transaction and sends one component apply request.
4. UI switches to `Waiting for transmitter confirmation...`
5. Transmitter validates and persists settings.
6. Transmitter sends `component_apply_ack(success=true, reboot_required=true)`.
7. Receiver records transaction state as `ready_for_reboot`.
8. UI starts the OTA-style countdown.
9. Receiver sends one reboot command.
10. UI redirects home.

If any save or verification step fails:

- no reboot is issued
- UI shows the returned error
- selection remains pending for manual retry

---

## Practical Implementation Options

## Option A — Recommended
### Add a new component apply request + ACK protocol

**Pros**
- clean transaction model
- one save operation per page action
- best alignment with receiver-owned reboot rule
- easiest to reason about and test

**Cons**
- requires protocol additions
- touches both transmitter and receiver ESP-NOW handlers

This is the best long-term answer.

---

## Option B — Accept current messages, add ACKs to both existing handlers

Keep:

- `msg_component_config`
- `msg_component_interface`

Add:

- `msg_component_config_ack`
- `msg_component_interface_ack`

Then the receiver waits until all required ACKs arrive before rebooting.

**Pros**
- smaller protocol delta
- can reuse existing split handlers

**Cons**
- more state coordination on receiver
- more partial-update edge cases
- less elegant than a single transaction

This is workable, but not ideal.

---

## Option C — Poll transmitter HTTP for component apply status

Use a transmitter HTTP endpoint such as `/api/component_apply_status`.

**Pros**
- visually similar to OTA
- can be implemented without adding new ESP-NOW ACK structs

**Cons**
- weaker transport coupling
- depends on IP + HTTP path availability
- not as robust as using ESP-NOW ACKs

This is acceptable only as a secondary option.

---

## Additional Improvements Recommended

## 1) Receiver API must stop returning success when ESP-NOW send fails

Current behavior is unsafe.

If the send fails, the HTTP API should return:

- `success=false`
- a clear error message
- no reboot sequence

This should be fixed regardless of any larger redesign.

---

## 2) Store and expose transaction state centrally on the receiver

A small receiver-side transaction tracker would improve multiple flows:

- OTA
- battery/inverter apply
- future network config or MQTT config changes

It could be reused by:

- polling APIs
- SSE status updates
- web UI widgets

This would reduce ad-hoc state handling inside page JavaScript.

---

## 3) Add correlation IDs to critical request/ACK pairs

Without a `request_id`, delayed or duplicated ACKs are harder to reason about.

Correlation IDs should be added for:

- component apply
- future critical save/reboot flows

---

## 4) Treat “reboot required” as explicit transmitter state

Instead of inferring reboot need from page behavior, the transmitter should explicitly state:

- `reboot_required = true`
- `ready_for_reboot = true`

That makes the receiver logic deterministic.

---

## 5) Consider batching all page-triggered component changes

If a battery page change touches:

- battery type
- battery interface

then that should ideally become one transaction, one persisted ACK, one reboot.

That reduces:

- duplicate writes
- partial-save states
- duplicate reboot triggers
- UI complexity

---

## Suggested Implementation Sequence

## Phase 1 — Immediate safety hardening
1. Remove transmitter auto-reboot from component handlers.
2. Make receiver APIs fail when ESP-NOW send fails.
3. Do not reboot unless a positive persisted confirmation exists.

## Phase 2 — Add persisted ACK path
1. Add component ACK protocol messages.
2. Add receiver-side transaction tracker.
3. Add status endpoint for UI polling.
4. Update battery/inverter pages to mirror OTA readiness flow.

## Phase 3 — Clean transaction model
1. Replace split type/interface save path with single component apply transaction.
2. Add read-back verification on transmitter.
3. Add config/version fields to ACKs.

---

## Bottom-Line Recommendation

**Recommended architecture:**

- Keep **reboot ownership entirely on the receiver**.
- Require the transmitter to send an explicit **persisted-and-ready ACK** before reboot is allowed.
- Model the battery/inverter flow after OTA:
  - save/apply first,
  - wait for readiness confirmation,
  - reboot second.

### If only one change is made
The single most important design change is:

> **Do not let battery/inverter pages send reboot based only on local API success. Require a transmitter confirmation that the requested values were persisted successfully.**

### Best implementation choice
The best implementation is a **new component apply transaction + ACK**, with the receiver page polling a receiver-local transaction status endpoint before triggering the single reboot command.

---

## Concrete Files Most Likely To Change In A Follow-Up Implementation

### Receiver
- `espnowreceiver_2/lib/webserver/pages/battery_settings_page.cpp`
- `espnowreceiver_2/lib/webserver/pages/inverter_settings_page.cpp`
- `espnowreceiver_2/lib/webserver/api/api_type_selection_handlers.cpp`
- `espnowreceiver_2/src/espnow/espnow_send.cpp`
- receiver-side ESP-NOW message/ACK handlers and transaction state storage

### Transmitter
- `ESPnowtransmitter2/espnowtransmitter2/src/espnow/component_catalog_handlers.cpp`
- transmitter-side ACK send logic
- component persistence helper / manager for interface settings

### Shared protocol
- `esp32common/espnow_transmitter/espnow_common.h`

---

## Final Recommendation

Do **not** simply re-add delays or countdown tweaks.

That would hide the symptom, not fix the architecture.

The correct solution is:

1. transmitter saves safely,
2. transmitter confirms readiness,
3. receiver sends one reboot.

That gives battery/inverter the same safety model that OTA already uses successfully.
