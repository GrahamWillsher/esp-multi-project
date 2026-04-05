# ESP-NOW Sequence Counter Ceiling & Overflow Analysis (2026-04-05)

## Scope

Requested analysis of sequential/counting fields across the active ESP-NOW transmitter/receiver/common codepaths, including:

- heartbeat sequence numbers
- temperature report sequence numbers
- event summary sequence numbers
- packet/correlation sequence identifiers
- internal receiver/transmitter rolling counters

This report focuses on **ceiling values**, **what happens at rollover**, and **recommended hardening actions**.

---

## Executive Summary

1. The major wire counters (`heartbeat.seq`, `temperature_report.seq`, `event_log_summary.seq`) are `uint32_t` and therefore wrap at `4,294,967,295`.
2. At current cadences, wrap timing for heartbeat/temperature is effectively non-operationally reachable (~1361 years at 10s cadence).
3. There are still two correctness gaps at wrap:
   - transmitter ACK freshness check uses plain `>` and will reject valid wrapped ACKs.
   - receiver TX-reboot detection uses plain `<` and will report a false reboot when heartbeat sequence wraps.
4. Several other “sequence-like” fields are random correlation IDs (`request_id`, packet `seq`, type-catalog `sequence`) and are not monotonic counters; overflow is not the right risk model for them.
5. Internal diagnostic counters (`g_rx_message_seq`, `g_snapshot_seq`) are `uint32_t`, wrap naturally, and currently have no dangerous ordering logic.

---

## Detailed Inventory

## A) Monotonic wire counters

### 1) `heartbeat_t.seq`
- **Type / ceiling:** `uint32_t`, max `4,294,967,295`
- **Producer:** transmitter `HeartbeatManager` (`++m_heartbeat_seq` per heartbeat)
- **Nominal cadence:** every `TimingConfig::HEARTBEAT_INTERVAL_MS` (currently heartbeat cadence)
- **Approx wrap time:**
  - at 10s cadence: `4,294,967,296 * 10s ≈ 1,361 years`
- **Current rollover behavior:**
  - wraps to `0` by C++ unsigned integer rules.

### 2) `heartbeat_ack_t.ack_seq`
- **Type / ceiling:** `uint32_t`, same as heartbeat seq (echoed)
- **Producer:** receiver ACK echoes heartbeat `seq`
- **Consumer:** transmitter `HeartbeatManager::on_heartbeat_ack()`
- **Critical logic today:**
  - ACK accepted only if `ack->ack_seq > m_last_ack_seq`
- **Rollover effect:**
  - when `m_last_ack_seq` is near max and wrapped ACK arrives near `0`, `>` fails forever (until reset), so valid ACKs are treated as old/duplicate.
- **Risk:** high correctness risk (even if very late in absolute time), because this can break ACK progression logic.

### 3) `temperature_report_t.seq`
- **Type / ceiling:** `uint32_t`
- **Producer:** transmitter `HeartbeatManager::send_temperature_report()` (`++m_temperature_seq`)
- **Cadence:** heartbeat cadence (one temperature report per successful heartbeat send path)
- **Approx wrap time:** similar to heartbeat (~1361 years at 10s)
- **Rollover effect:**
  - receiver currently stores latest value without monotonic compare, so no immediate logic break.

### 4) `event_log_summary_t.seq`
- **Type / ceiling:** `uint32_t`
- **Producer:** transmitter `message_routes.cpp` (`++g_event_log_summary_seq`)
- **Cadence:** on summary request/subscription events (not fixed high-frequency periodic loop)
- **Approx wrap time examples:**
  - 1 Hz: ~136 years
  - 0.1 Hz: ~1,361 years
- **Rollover effect:**
  - receiver stores latest summary without strict monotonic comparison, so no immediate rejection on wrap.

---

## B) Monotonic local/internal counters

### 5) Receiver worker message sequence (`g_rx_message_seq`)
- **Type / ceiling:** `uint32_t`
- **Producer:** incremented per dequeued ESP-NOW message in worker loop
- **Consumer:** copied to `RxStateMachine::stats_.last_message_seq` (diagnostic)
- **Rollover effect:**
  - natural wrap; no ordering logic dependent on `>`/`<` currently.
- **Risk:** low (telemetry/diagnostic only).

### 6) Receiver telemetry snapshot sequence (`g_snapshot_seq`)
- **Type / ceiling:** `uint32_t`
- **Producer:** incremented on each telemetry snapshot mutation (`BatteryData` updates)
- **Rollover effect:**
  - natural wrap; currently only exposed by getter, no wrap-sensitive compare in active code.
- **Risk:** low at present.

### 7) `time_transitions_snapshot_t.revision`
- **Type / ceiling:** `uint16_t` (max `65,535`)
- **Current status:** defined in shared wire struct, no active producer/consumer implementation found in current codepaths.
- **Risk:** currently dormant; when implemented, wrap-safe comparison rules will be needed.

---

## C) Sequence-like correlation IDs (not monotonic counters)

### 8) Discovery PROBE/ACK sequence (`probe_t.seq`, `ack_t.seq`)
- **Type:** `uint32_t`
- **Generation:** random (`esp_random()`)
- **Validation model:** equality compare (`ack.seq == expected_seq`) when expected value is set
- **Overflow model:** not applicable (random ID domain, not incrementing counter).

### 9) Fragmented packet `espnow_packet_t.seq`
- **Type:** `uint32_t`
- **Observed generation:** random (`esp_random()`) in request-data response path
- **Purpose:** correlate fragments/request cycle, not ordering timeline.
- **Overflow model:** not applicable; collision probability is the practical concern.

### 10) Type catalog fragment `type_catalog_fragment_t.sequence`
- **Type:** `uint16_t`
- **Generation:** random (`esp_random() & 0xFFFF`)
- **Receiver behavior:** resets staging when sequence/fragment_total changes.
- **Overflow model:** not applicable; collision/stale interleaving is the practical concern.

### 11) `request_id` fields (`component_apply`, `version`, `metadata`)
- **Type:** `uint32_t`
- **Generation:** random/time-mixed in receiver (`esp_random() ^ millis()` for component apply)
- **Usage:** request/response correlation by equality.
- **Overflow model:** not applicable; collision is the practical concern.

---

## Known Wrap-Sensitive Logic Findings

## Finding F1 (Important)
**Location:** transmitter heartbeat ACK handling

Current logic accepts ACK only when:
- `ack_seq > last_ack_seq`

At wrap, valid new ACKs become numerically smaller and are rejected as old/duplicate.

## Finding F2 (Important)
**Location:** receiver heartbeat handling reboot detection

Current logic flags reboot when:
- `hb->seq < m_last_heartbeat_seq`

At wrap, a normal wrapped sequence triggers false reboot detection.

---

## Ceiling and Time-to-Wrap Quick Reference

For an unsigned $N$-bit counter:

$$
\text{max} = 2^N - 1
$$

$$
\text{time-to-wrap} = \frac{2^N}{\text{increments per second}}
$$

- `uint32_t`: $2^{32} = 4,294,967,296$ states
- `uint16_t`: $2^{16} = 65,536$ states

Examples:
- Heartbeat/temperature at $0.1\,Hz$ (10s): ~$1,361$ years
- Local counter at $100\,Hz$: ~$497$ days

---

## Recommendations

## Priority P0 (should implement)
1. Add a shared wrap-safe sequence helper for `uint32_t`:
   - `is_newer_u32(a, b)` based on modular arithmetic with signed delta window.
   - Use it for heartbeat ACK progression instead of plain `>`.
2. Update receiver reboot detection to use wrap-aware progression logic:
   - differentiate true reset vs natural modulo wrap.

## Priority P1 (strongly recommended)
3. Add explicit unit tests for wrap boundaries:
   - `0xFFFFFFFE -> 0xFFFFFFFF -> 0x00000000 -> 0x00000001`
   - duplicate/out-of-order packets around boundary
   - ACK acceptance around boundary
4. Document sequence semantics per field in protocol docs:
   - monotonic counter vs random correlation ID
   - required comparator (`==` vs wrap-safe newer-than)

## Priority P2 (optional hardening)
5. For random correlation fields with small space (`uint16_t` type catalog sequence), consider:
   - moving to `uint32_t`, or
   - augmenting with sender uptime/request nonce to reduce collision ambiguity.
6. If `time_transitions_snapshot_t.revision` becomes active, predefine wrap-safe update/compare contract now.

---

## Suggested Implementation Pattern (for monotonic `uint32_t`)

Use modular distance rather than raw `>`:

- Newer if `(int32_t)(a - b) > 0`
- Equal if `a == b`
- Older if `(int32_t)(a - b) < 0`

This is standard serial-number arithmetic for 32-bit wrapping counters (valid as long as reordering does not exceed half-range).

---

## Proposed Next Step

If approved, I recommend a focused hardening change set in shared/common code to:

1. introduce reusable wrap-safe comparator utilities,
2. apply them to heartbeat ACK progression and reboot detection,
3. add boundary tests in `esp32common/tests`.

This resolves the two real wrap correctness risks without changing current wire formats.
