# ESP-NOW Radio Coexistence Implementation Plan
**Version:** 1.0  
**Date:** April 30, 2026  
**Branch:** `feature/battery-emulator-migration`  
**Status:** READY FOR DEVELOPMENT  

---

## 1. Executive Summary

This plan orchestrates the remaining implementation work to complete robust radio coexistence for the two-device ESP-NOW system (TX Ethernet MQTT + RX Wi-Fi MQTT + shared ESP-NOW). 

**Foundation in Place:**
- ✅ FSM model restored and side-path tuning removed
- ✅ Hardened specification document with release gates
- ✅ Both receiver variants build successfully

**Work Remaining:**
- **Phase 1 (HIGH PRIORITY):** Callback-paced ACK token system (R2) — addresses root `NO_MEM` contention
- **Phase 2 (HIGH PRIORITY):** Reconnect boundary atomicity (R1) — prevents partial-state reconnects
- **Phase 3 (MEDIUM PRIORITY):** Instrumentation & observability (R6) — enables field diagnostics
- **Phase 4 (VALIDATION):** Failure-injection testing (6 scenarios, 20 cycles each)
- **Phase 5 (GATE):** Promotion verification — code, metrics, system parity

**Critical Path:** Phase 1 → Phase 2 → Phase 3 → Phase 4 + Phase 5

**Estimated Timeline:** 4–6 weeks (development + validation)

---

## 2. Phase 1: Callback-Paced ACK Token System (R2)

**Priority:** HIGHEST  
**Objective:** Prevent duplicate ACK enqueue during resource contention. Reduce `ESP_ERR_ESPNOW_NO_MEM` failures.  
**Impact:** Direct fix for reconnect discovery ACK bursts that cause memory exhaustion during TCP teardown.

### 2.1 Design Overview

**Mechanism:**
- Per-peer single in-flight token for discovery ACKs in reconnect states
- Token acquired on ACK enqueue; held until send callback fires or watchdog timeout (500ms)
- Second ACK attempt while token held: silently dropped (logged)
- Watchdog timeout: treat as send failure, release token, allow retry

**Token State Machine:**
```
[IDLE] --enqueue ACK--> [IN_FLIGHT]
                            |
                    [callback or timeout]
                            |
                         [IDLE]
```

**Integration Points:**
1. `EspnowTxScheduler`: Add token tracking per peer
2. `ReceiverConnectionHandler::on_probe_received()`: Check token before enqueue
3. Send callback handler: Release token on success/failure
4. Watchdog timer: Reset tokens on timeout per peer

### 2.2 Implementation Tasks

#### Task 1.1: Add Token Structure to Scheduler
**File:** `esp32common/espnow_common_utils/espnow_tx_scheduler.h`

**Changes:**
- Add `struct AckToken` with fields: `peer_mac`, `acquired_time`, `callback_pending`
- Add `std::unordered_map<std::string, AckToken> ack_tokens_` to scheduler private members
- Add methods:
  - `bool try_acquire_ack_token(const uint8_t* peer_mac)` — returns true if token acquired, false if already held
  - `void release_ack_token(const uint8_t* peer_mac)`
  - `void check_ack_token_watchdogs()` — called periodically, release tokens older than 500ms
- Add constant: `static constexpr uint32_t ACK_TOKEN_WATCHDOG_MS = 500;`

**Success Criteria:**
- Compiles without errors
- Token acquire/release operations are O(1) via hash map
- Watchdog check is O(N) where N = active peers (typically 1–2)

#### Task 1.2: Integrate Send Callback with Token Release
**File:** `esp32common/espnow_common_utils/espnow_tx_scheduler.cpp`

**Changes:**
- Modify ESP-NOW send callback handler (`esp_now_send_cb_t`)
- On any send result (success/failure), call `release_ack_token(peer_mac)`
- Log token release event at DEBUG level with result code and dwell time

**Success Criteria:**
- Token released on all send callback invocations
- Dwell time (acquired → released) logged and inspectable in diagnostics
- No token leaks (watchdog always fires as safety net)

#### Task 1.3: Gate ACK Enqueue on Token Availability
**File:** `esp32common/espnow_common_utils/rx_connection_handler.cpp`

**Changes:**
- Modify `on_probe_received()` (around line 200–215):
  ```cpp
  // Existing: heartbeat freshness + state gating
  if (heartbeat_stale && not_steady_connected) {
      // NEW: Add token check before event raise
      if (!EspnowTxScheduler::instance().try_acquire_ack_token(peer_mac)) {
          ESP_LOGD(TAG, "ACK token held for peer; dropping duplicate probe");
          return;  // Silently drop, watchdog will retry
      }
      inject_fsm_event(RxRadioArbiterFsm::Event::EV_PROBE_WHILE_PREVIOUSLY_CONNECTED);
  }
  ```
- Log suppressed ACK attempts at DEBUG level (include peer MAC, holdtime)

**Success Criteria:**
- ACK enqueue blocked when token held
- Suppression logged for diagnostics
- No call to `release_ack_token()` on suppressed attempts (token remains held)

#### Task 1.4: Add Periodic Watchdog Maintenance
**File:** `esp32common/espnow_common_utils/rx_connection_handler.cpp`

**Changes:**
- In main connection handler task loop (or dedicated timer callback):
  - Call `EspnowTxScheduler::instance().check_ack_token_watchdogs()` every 100ms
  - Log any tokens forcibly released (indicates send failure or callback loss)

**Success Criteria:**
- Watchdog fires every ~500ms for stale tokens
- Tokens forcibly released by watchdog logged at WARN level
- No memory leaks from unreleased tokens

### 2.3 Testing & Validation (Phase 1)

#### Unit Tests
- **Test 1.1:** Token acquire/release lifecycle (success path)
- **Test 1.2:** Duplicate acquire attempt returns false
- **Test 1.3:** Watchdog timeout releases token
- **Test 1.4:** Token state persists across send success/failure

#### Integration Tests (Single RX)
- **Test 1.5:** Rapid probe flooding suppressed after first token acquire
- **Test 1.6:** ACK dwell time recorded and inspectable
- **Test 1.7:** Watchdog fires and releases stale tokens
- **Test 1.8:** Token release unblocks next ACK enqueue

#### Field Validation
- **Test 1.9:** RX LCD + TX with alternating probe floods (100ms gaps), monitor ACK success rate (target: ≥98%)
- **Test 1.10:** RX2 + TX with same profile, confirm parity

### 2.4 Success Criteria (Phase 1)

✅ **Code Gate:**
- Callback-paced token system implemented and integrated
- All unit tests pass
- Both RX variants build successfully

✅ **Metrics Gate:**
- Diagnostic output includes ACK token state (acquired/held/released)
- ACK dwell time visible in logs for all reconnect cycles
- Watchdog-triggered releases logged with reasons

✅ **Operational Gate:**
- Manual probe-flood test on both RX variants shows ≥98% ACK success
- No `ESP_ERR_ESPNOW_NO_MEM` in 10-minute continuous flood

---

## 3. Phase 2: Reconnect Boundary Atomicity (R1)

**Priority:** HIGH  
**Objective:** Ensure reconnect entry (MQTT disconnect + scheduler mode change + queue purge + settle timer) is single idempotent action.  
**Impact:** Prevents partial-state reconnects where scheduler mode is changed but MQTT remains connected, or vice versa.

### 3.1 Design Overview

**Current Issue:**
Reconnect triggering scattered across multiple entry points (FSM event handlers, heartbeat timeout, ACK pressure). Risk of partial-state transitions.

**Target Design:**
Single `initiate_reconnect(ReconnectReason)` function:
1. Check current state (guard against re-entry)
2. Disconnect MQTT (if connected)
3. Set scheduler to control-only mode
4. Purge non-control queues
5. Start reconnect settlement timer
6. Mark in FSM that reconnect has been initiated (idempotent)

### 3.2 Implementation Tasks

#### Task 2.1: Define Reconnect Reason Enum
**File:** `esp32common/espnow_common_utils/rx_connection_handler.h`

**Changes:**
```cpp
enum class ReconnectReason {
    HEARTBEAT_TIMEOUT,
    ACK_SEND_PRESSURE,
    MQTT_CONNECTION_LOST,
    USER_MANUAL,
};
```

**Success Criteria:**
- Enum covers all reconnect triggers
- Reason is logged and inspectable in diagnostics

#### Task 2.2: Consolidate Reconnect Entry Logic
**File:** `esp32common/espnow_common_utils/rx_connection_handler.cpp`

**Changes:**
- Add new method: `ReceiverConnectionHandler::initiate_reconnect(ReconnectReason reason)`
- Implementation (pseudo-code):
  ```cpp
  void ReceiverConnectionHandler::initiate_reconnect(ReconnectReason reason) {
      // Guard against re-entry
      if (reconnect_in_progress_) {
          ESP_LOGD(TAG, "Reconnect already in progress, ignoring %s", reason_str(reason));
          return;
      }
      reconnect_in_progress_ = true;
      
      // 1. Disconnect MQTT
      if (mqtt_client_.is_connected()) {
          mqtt_client_.disconnect();
          ESP_LOGI(TAG, "MQTT disconnected for reconnect: %s", reason_str(reason));
      }
      
      // 2. Set scheduler to control-only
      EspnowTxScheduler::instance().set_control_only_mode(true, true);  // purge non-control
      ESP_LOGI(TAG, "Scheduler: control-only mode enabled");
      
      // 3. Purge queues (redundant if scheduler purges, but explicit)
      // (optional, scheduler does this)
      
      // 4. Start settlement timer
      reconnect_settle_timer_.reset(RECONNECT_SETTLE_MS);
      ESP_LOGI(TAG, "Reconnect settle timer started: %u ms", RECONNECT_SETTLE_MS);
      
      // 5. Signal FSM (FSM should already be in reconnect state via event injection)
      ESP_LOGI(TAG, "Reconnect initiated: reason=%s, FSM state=%s", 
               reason_str(reason), fsm_.current_state_str());
  }
  ```

- Replace all scatter-point reconnect logic with call to `initiate_reconnect(reason)`

**Locations to Consolidate:**
- `on_heartbeat_timeout()` → `initiate_reconnect(HEARTBEAT_TIMEOUT)`
- `on_ack_send_pressure()` → `initiate_reconnect(ACK_SEND_PRESSURE)`
- `on_mqtt_connection_lost()` → `initiate_reconnect(MQTT_CONNECTION_LOST)`
- Manual reconnect trigger (if any) → `initiate_reconnect(USER_MANUAL)`

**Success Criteria:**
- Single entry point for all reconnect initiations
- Idempotent: calling twice in rapid succession ignores second call
- All state transitions (MQTT, scheduler, timers) are atomic from caller perspective

#### Task 2.3: Add Reconnect Guard State
**File:** `esp32common/espnow_common_utils/rx_connection_handler.h`

**Changes:**
- Add member: `std::atomic<bool> reconnect_in_progress_{false};`
- Add method: `bool is_reconnect_in_progress() const { return reconnect_in_progress_; }`
- Clear flag when reconnect settlement completes (in FSM state transition to STEADY_CONNECTED)

**Success Criteria:**
- Guard prevents re-entry during 8000ms settlement window
- Flag is only set in `initiate_reconnect()` and cleared in FSM settlement completion

#### Task 2.4: Verify FSM State Ownership
**File:** `esp32common/espnow_common_utils/rx_radio_arbiter_fsm.cpp`

**Changes:**
- Verify that FSM transitions are only triggered via `inject_fsm_event()`
- Verify that `initiate_reconnect()` in handler does NOT directly change FSM state; only injects event
- FSM state machine itself determines state transitions based on events and timers

**Success Criteria:**
- FSM state is source of truth
- `initiate_reconnect()` is side-effect (MQTT, scheduler), NOT FSM-state-changing
- All state transitions still driven by FSM events and timers

### 3.3 Testing & Validation (Phase 2)

#### Unit Tests
- **Test 2.1:** `initiate_reconnect()` called twice rapidly; second call ignored
- **Test 2.2:** MQTT disconnected before scheduler mode change
- **Test 2.3:** Scheduler in control-only mode after `initiate_reconnect()`
- **Test 2.4:** Settlement timer started and timeout handled

#### Integration Tests
- **Test 2.5:** Heartbeat timeout triggers `initiate_reconnect()` with correct reason
- **Test 2.6:** ACK pressure escalation triggers `initiate_reconnect()` with correct reason
- **Test 2.7:** MQTT connection loss triggers `initiate_reconnect()` with correct reason
- **Test 2.8:** Rapid heartbeat timeouts (simulated) cause only one reconnect initiation

#### Field Validation
- **Test 2.9:** Drop TX for 15 seconds (heartbeat timeout), observe single reconnect attempt on RX LCD
- **Test 2.10:** Manually disconnect MQTT on RX LCD, observe `initiate_reconnect(MQTT_CONNECTION_LOST)` logged
- **Test 2.11:** Same tests on RX2, confirm parity

### 3.4 Success Criteria (Phase 2)

✅ **Code Gate:**
- `initiate_reconnect(ReconnectReason)` implemented with guard
- All scatter-point reconnect logic consolidated
- Both RX variants build successfully with no regressions

✅ **Metrics Gate:**
- Reconnect reason logged and inspectable
- Duplicate reconnect attempts visible in logs (guard firing)
- Settlement timer duration logged

✅ **Operational Gate:**
- Manual heartbeat timeout test shows single reconnect attempt
- No partial-state reconnects (MQTT status and scheduler mode always consistent)

---

## 4. Phase 3: Instrumentation & Observability (R6)

**Priority:** MEDIUM  
**Objective:** Add runtime channel, power-save, and FSM diagnostic metrics.  
**Impact:** Enables field diagnosis of coexistence issues; essential for production confidence.

### 4.1 Metrics to Implement

#### 4.1.1 Radio Channel & Bandwidth Metrics
- **Current Wi-Fi channel:** Read from `wifi_ap_record_t` on successful scan/connect
- **Current ESP-NOW channel:** Read from `esp_now_peer_info_t` for each peer
- **Wi-Fi bandwidth (HT20/HT40):** Read from Wi-Fi PHY info
- **Channel mismatch events:** Count of probe attempts on wrong channel

**Logging:** Every connection status change + every 30 seconds if connected

#### 4.1.2 Modem Sleep & Power-Save Metrics
- **Modem sleep enabled state:** Query `esp_wifi_get_ps_type()`
- **Number of times sleep mode changed:** Count transitions
- **Power-save mode active:** Query Wi-Fi sta state
- **Expected Wi-Fi beacon period:** Log from connection parameters

**Logging:** On power-save state change + every 60 seconds if connected

#### 4.1.3 FSM Diagnostic Metrics
- **Current FSM state:** Log on every state transition
- **FSM event queue depth:** Count pending events (if queue exposed)
- **FSM transition duration:** Time from RECONNECT_DETECTED to STEADY_CONNECTED
- **FSM policy outputs:** Log `mqtt_allowed`, `control_only_mode`, `ack_retry_profile` on change

**Logging:** On every FSM state change + every 10 seconds snapshot

#### 4.1.4 ACK Token & Scheduler Metrics (from Phase 1)
- **ACK token state per peer:** Acquired/held/released
- **ACK dwell time:** Duration from acquire to release
- **Scheduler queue depths:** Control, Discovery, Data, Monitoring queue sizes
- **Scheduler mode:** Control-only or normal
- **Control queue purge events:** Count and reason

**Logging:** On every ACK token change + every 5 seconds queue snapshot

### 4.2 Implementation Tasks

#### Task 3.1: Add Radio Channel Diagnostics
**File:** `esp32common/espnow_common_utils/rx_connection_handler.cpp`

**Changes:**
- Add method: `void log_radio_diagnostics()`
- Query current Wi-Fi channel, bandwidth, ESP-NOW channel for each peer
- Log at INFO level: `"Radio: Wi-Fi CH=%d BW=%s, ESP-NOW peers: [peer1@CH=%d, peer2@CH=%d]"`
- Call from connection status change handler + periodic 30s timer

**Success Criteria:**
- Radio diagnostics logged on every connection state change
- Periodic logging every 30 seconds captures channel drift
- Zero false channel mismatch alerts

#### Task 3.2: Add Modem Sleep Diagnostics
**File:** `esp32common/espnow_common_utils/rx_connection_handler.cpp`

**Changes:**
- Add method: `void log_power_save_diagnostics()`
- Query `esp_wifi_get_ps_type()`, beacon period, power-save mode
- Log at INFO level: `"Power-Save: PS=%s, beacon_interval=%u ms, modem_sleep=%s"`
- Call on Wi-Fi connection change + periodic 60s timer
- Track power-save state changes and log transitions

**Success Criteria:**
- Modem sleep state visible in diagnostics
- Power-save mode transitions logged with reason (if available)
- Beacon period logged to correlate with ACK timing

#### Task 3.3: Add FSM & Scheduler Diagnostics
**File:** `esp32common/espnow_common_utils/rx_connection_handler.cpp`

**Changes:**
- Add method: `void log_fsm_diagnostics()`
- Log current FSM state, all policy flags (`mqtt_allowed`, `control_only_mode`, etc.)
- Log scheduler mode (control-only or normal)
- Log scheduler queue depths for all 4 queues
- Log ACK token state if in reconnect phase
- Call on FSM state change + periodic 10s snapshot
- Include transition duration (if tracking RECONNECT_DETECTED → STEADY_CONNECTED)

**Success Criteria:**
- FSM policy fully transparent in logs
- Scheduler mode and queue depths inspectable
- ACK token state visible during reconnect

#### Task 3.4: Add Observability API
**File:** `esp32common/espnow_common_utils/rx_connection_handler.h`

**Changes:**
- Add struct: `struct DiagnosticSnapshot { FSMState fsm_state; SchedulerMode mode; int queue_depths[4]; ... };`
- Add method: `DiagnosticSnapshot get_diagnostic_snapshot()` for programmatic access
- Allow external (e.g., webserver) to query diagnostics without parsing logs

**Success Criteria:**
- Structured diagnostics available via API
- Both human-readable logs and machine-readable snapshot available

### 4.3 Testing & Validation (Phase 3)

#### Unit Tests
- **Test 3.1:** Channel diagnostics read correct values from Wi-Fi/ESP-NOW drivers
- **Test 3.2:** Power-save diagnostics reflect `esp_wifi_get_ps_type()` state
- **Test 3.3:** FSM diagnostics log on state transition
- **Test 3.4:** Queue depth logging works without scheduler lock contention

#### Integration Tests
- **Test 3.5:** Connect RX LCD to TX, observe radio diagnostics logged
- **Test 3.6:** Change Wi-Fi channel (via scan), observe channel update in diagnostics
- **Test 3.7:** Enable modem sleep, observe power-save state change in diagnostics
- **Test 3.8:** Trigger FSM reconnect, observe full FSM transition in diagnostics

#### Field Validation
- **Test 3.9:** RX LCD connected for 5 minutes, verify diagnostics logged every 10s without performance impact
- **Test 3.10:** Same test on RX2, confirm parity and no CPU/memory overhead

### 4.4 Success Criteria (Phase 3)

✅ **Code Gate:**
- All 4 diagnostic categories implemented
- Diagnostic snapshot API working
- Both RX variants build successfully

✅ **Metrics Gate:**
- Diagnostics logged at appropriate intervals (5s–60s)
- No performance degradation (CPU, memory, Wi-Fi latency)
- Diagnostics capture full reconnect lifecycle

✅ **Operational Gate:**
- Field logs contain sufficient detail to diagnose coexistence issues
- No log spam (burst diagnostics suppressed)

---

## 5. Phase 4: Failure-Injection Validation (Testing)

**Priority:** HIGH (Prerequisite for production)  
**Objective:** Run 20-cycle reconnect campaigns under 6 broker/network failure scenarios.  
**Impact:** Validates robustness of Phases 1–3 combined.

### 5.1 Test Matrix (from Hardening Profile H4)

| Scenario | Setup | Failure Mode | Expected Recovery |
|----------|-------|--------------|-------------------|
| **S1: Broker TCP Reset** | TX + RX on LAN, both MQTT connected | Broker force-closes TCP stream (e.g., `kill -9` or firewall drop) | RX detects lost MQTT, injects heartbeat timeout, reconnects within 8s |
| **S2: RX Wi-Fi Drop** | TX + RX connected, probe-ack flowing | RX Wi-Fi disconnects (simulate: `wifi.mode(OFF)` for 5s) | RX re-joins Wi-Fi, heartbeat recovers, ESP-NOW probing resumes within 15s |
| **S3: TX ESP-NOW Flood** | TX + RX connected, normal flow | TX floods RX with 1000 ESP-NOW msgs/sec for 10s | RX survives (queues, timeouts), ACK backpressure holds, recovery within 5s of flood end |
| **S4: Broker Latency Spike** | TX + RX connected, normal flow | Broker introduces 5s latency on all publish/subscribe (e.g., iptables delay rule) | RX MQTT hangs (app-layer timeout), triggers reconnect, recovers within 8s |
| **S5: RX→TX Channel Asymmetry** | TX + RX connected, probe-ack flowing | ESP-NOW channel changes on TX side only (simulate: TX moves to CH7, RX stays CH6) | RX detects lost probes, triggers reconnect FSM, recovers when channel re-syncs (within 30s) |
| **S6: Simultaneous Wi-Fi + ESP-NOW Loss** | TX + RX connected, normal flow | Both Wi-Fi and ESP-NOW fail for 5s (simulate: `wifi.mode(OFF)`, TX power off) | RX detects heartbeat timeout + no Wi-Fi beacon, enters DEGRADED_FALLBACK, recovers within 10s of re-power |

### 5.2 Test Procedure

#### Setup Phase
1. Build both RX variants and TX with instrumentation enabled
2. Flash to hardware:
   - TX: Olimex ESP32-POE2 (Ethernet MQTT, broker on LAN)
   - RX LCD: Waveshare with LCD display
   - RX2: LilyGo T-Display-S3
3. Connect to test network (isolated LAN with controlled broker)
4. Verify baseline connectivity (probe ACKs flowing, MQTT pubs/subs working)

#### Test Execution (Per Scenario)
1. **Warm-up (60s):** Normal operation, collect baseline metrics
2. **Failure injection (30–60s):** Apply scenario failure condition
3. **Recovery window (120s):** Monitor reconnection metrics
4. **Settle (60s):** Verify stable operation post-recovery
5. **Repeat 20 cycles** for each scenario

#### Metrics Collection (Per Cycle)
- **ACK success rate:** % of probes answered with ACK (target: ≥95% in same-dwell window)
- **MQTT re-connect time:** Duration from failure detection to first successful PUBLISH
- **Scheduler queue depth:** Max queue depth during failure (target: <80% capacity)
- **FSM transition duration:** Time from reconnect trigger to STEADY_CONNECTED
- **Error log count:** Count of ESP_ERR_ESPNOW_NO_MEM, MQTT disconnects, Wi-Fi auth fails
- **System stability:** Crashes, watchdog resets, memory leaks (none expected)

### 5.3 Success Criteria (Phase 4)

✅ **Scenario S1 (Broker Reset):**
- 20/20 cycles: RX detects MQTT loss and triggers reconnect
- ACK success rate ≥95% within 8s of reconnect completion
- No ESP_ERR_ESPNOW_NO_MEM during recovery

✅ **Scenario S2 (Wi-Fi Drop):**
- 20/20 cycles: RX re-joins Wi-Fi and resumes ESP-NOW within 15s
- Heartbeat returns to normal cadence
- Zero unplanned MQTT disconnects during re-join

✅ **Scenario S3 (TX Flood):**
- 20/20 cycles: RX survives (no crashes, no watchdog resets)
- Scheduler queue remains <80% capacity
- Recovery within 5s of flood end

✅ **Scenario S4 (Broker Latency):**
- 20/20 cycles: RX detects latency (app-layer timeout) and reconnects
- MQTT re-connects without cascading failures
- ACK success rate returns to ≥95%

✅ **Scenario S5 (Channel Asymmetry):**
- 20/20 cycles: RX detects lost probes and enters reconnect FSM
- Channel re-syncs (TX returns to RX channel) → recovery within 30s
- Diagnostics clearly show channel mismatch events

✅ **Scenario S6 (Simultaneous Failure):**
- 20/20 cycles: RX enters DEGRADED_FALLBACK state
- Recovery within 10s of simultaneous re-power
- No cascading failures or hangs

✅ **Overall Validation Gate:**
- **Code stability:** Zero crashes, zero watchdog resets across all 120 cycles (20 × 6 scenarios)
- **Memory stability:** No leaks detected (monitor heap via `heap_caps_get_free_size()`)
- **Metrics stability:** All scenarios meet acceptance criteria ≥95% success rate
- **System parity:** RX LCD and RX2 metrics within 5% of each other

### 5.4 Test Automation

**Option A (Recommended):** Manual + Scripted
- Write Python test harness to:
  1. Flash binaries
  2. Connect to test network
  3. Inject failure via network manipulation (iptables, tc, `kill` process)
  4. Collect logs via serial/syslog
  5. Parse metrics and compare against acceptance criteria
- Run on laptop over 2–3 days
- Collect CSV of all 120 cycles for analysis

**Option B:** Fully Automated
- Implement on-device test harness (e.g., HTTP endpoint `/test/start?scenario=S1&cycles=20`)
- Device injects failures autonomously, logs results to SD card
- Risk: if device crashes, test stops; recommend Option A first

### 5.5 Timeline (Phase 4)

- **Scripted test harness:** 2–3 days
- **Test execution (120 cycles × 5 min/cycle ≈ 600 min ≈ 10 hours):** 1–2 days (parallel on RX LCD + RX2)
- **Analysis & results compilation:** 1 day
- **Total Phase 4:** ~4–5 days (including debugging if failures found)

---

## 6. Phase 5: Promotion Gate Verification

**Priority:** GATE (Must pass before production deployment)  
**Objective:** Verify all implementation complete and promotion criteria met.  
**Impact:** Final sign-off for production release.

### 6.1 Code Review Gate (C1)

**Checklist:**
- ✅ Callback-paced ACK token system implemented per Phase 1 design
- ✅ Token acquire/release integration tested (unit + integration tests)
- ✅ Reconnect boundary atomicity implemented per Phase 2 design
- ✅ Reconnect guard state prevents re-entry
- ✅ All scatter-point reconnect logic consolidated to `initiate_reconnect()`
- ✅ Instrumentation added per Phase 3 (radio, power-save, FSM, scheduler diagnostics)
- ✅ Diagnostic snapshot API implemented
- ✅ No regressions: both RX variants compile, no new warnings
- ✅ All unit tests pass (Phases 1–3)
- ✅ All integration tests pass (Phases 1–3)
- ✅ Code review by peer: architecture, thread-safety, no new bugs

**Approval:** Code review signoff required before proceeding to Phase 4

### 6.2 Metrics & Observability Gate (M1)

**Checklist:**
- ✅ Radio channel diagnostics logged every 30s
- ✅ Power-save diagnostics logged on state change
- ✅ FSM diagnostics logged on transition + every 10s
- ✅ ACK token state logged on change
- ✅ Scheduler diagnostics (queue depths, mode) logged every 5s
- ✅ No performance degradation (CPU <10% overhead, memory <50KB, Wi-Fi latency unchanged)
- ✅ Diagnostic snapshot API queryable without blocking
- ✅ Logs contain sufficient detail to diagnose top 5 failure modes (per root cause analysis)
- ✅ Field logs from Phase 4 testing readable and actionable

**Approval:** Log analysis of Phase 4 cycles shows all required metrics present and non-intrusive

### 6.3 System Validation Gate (S1)

**Checklist:**
- ✅ Phase 4 testing: 120 cycles (6 scenarios × 20 cycles) completed
- ✅ Phase 4 results: All scenarios meet ≥95% ACK success criteria
- ✅ Phase 4 results: Zero crashes, zero watchdog resets across 120 cycles
- ✅ Phase 4 results: RX LCD and RX2 metrics within 5% parity
- ✅ Failure recovery time meets spec (heartbeat detect 8s, re-join 15s, etc.)
- ✅ Memory stable (no leaks detected via heap instrumentation)
- ✅ Field test: 24-hour continuous operation on both RX variants (no manual intervention)

**Approval:** Test results CSV + field log analysis reviewed; system ready for production

### 6.4 Release Gate Process

**Gate Entry Conditions:**
- All phases (1–4) complete
- All code gate checklist items ✅
- All metrics gate checklist items ✅
- All system validation gate checklist items ✅

**Gate Exit Conditions (Sign-Off):**
1. **Developer sign-off:** I have implemented and tested all phases per spec ✅
2. **Peer review sign-off:** Code review complete, no major issues ✅
3. **QA sign-off:** Phase 4 testing complete, all acceptance criteria met ✅
4. **Architecture sign-off:** System meets hardening profile invariants ✅

**Post-Gate Actions:**
- Tag release branch: `release/esp-now-coexistence-v1.0`
- Merge to `main`
- Update firmware version in `firmware_version.h`
- Document release notes with hardening improvements

---

## 7. Implementation Timeline & Resource Plan

### 7.1 Phased Timeline

| Phase | Task | Duration | Dependencies | Status |
|-------|------|----------|--------------|--------|
| 1 | Callback-paced ACK token system | 3–4 days | None (can start immediately) | READY |
| 2 | Reconnect boundary atomicity | 2–3 days | Phase 1 complete (optional, but recommended) | READY |
| 3 | Instrumentation & observability | 2–3 days | Phase 1–2 (optional, but eases Phase 4 debugging) | READY |
| 4 | Failure-injection validation | 4–5 days | Phase 1–3 complete + test harness setup | READY |
| 5 | Promotion gate verification | 1–2 days | Phase 4 complete | READY |
| **Total** | | **12–17 days** | | |

**Critical Path:** Phase 1 → Phase 2 → Phase 4 + Phase 5

**Optional Optimization:** Phase 3 can overlap with Phase 4 (instrumentation helps debugging Phase 4 failures).

### 7.2 Resource Allocation

- **Primary Developer:** You (sole implementer on this branch)
- **Peer Review:** Code review of Phases 1–3 (estimated 2 hours)
- **Testing:** Field testing in Phases 4–5 (estimated 10–12 hours automated + 4 hours manual)

### 7.3 Risk Mitigation

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|-----------|
| Phase 1 token system causes Wi-Fi latency | LOW | HIGH | Unit test token operations timing; implement lockfree if needed |
| Phase 2 reconnect guard causes deadlock | LOW | HIGH | Peer review reconnect idempotency logic; unit test re-entry scenarios |
| Phase 3 instrumentation causes memory bloat | MEDIUM | MEDIUM | Profile diagnostic snapshot size; use ring buffer for old metrics |
| Phase 4 testing uncovers new issue | MEDIUM | MEDIUM | Allocate 2–3 extra days for debugging; have debug builds ready |
| Build regression after phases | LOW | HIGH | Build both RX variants after each phase; verify no new warnings |

---

## 8. Success Definition & Graduation Criteria

### 8.1 Development Success (Phase 1–3 Complete)

✅ **Code Quality:**
- Callback-paced ACK token system implemented and integrated
- Reconnect atomicity enforced with guard state
- Instrumentation provides full diagnostic visibility
- All unit tests pass
- All integration tests pass
- Zero regressions (builds, warnings, functionality)

✅ **Architectural Alignment:**
- FSM remains single source of truth for radio state
- Callback-paced ACK directly addresses `NO_MEM` contention
- Reconnect boundary atomicity prevents partial-state failures
- Diagnostics enable field troubleshooting

### 8.2 Validation Success (Phase 4 Complete)

✅ **Robustness:**
- 120 test cycles (6 scenarios × 20 cycles) pass acceptance criteria
- ≥95% ACK success rate in each scenario
- Zero crashes, zero watchdog resets
- RX LCD and RX2 parity within 5%

✅ **Performance:**
- Recovery time meets spec (heartbeat 8s, re-join 15s, etc.)
- Memory stable (no leaks)
- CPU overhead <10%
- No Wi-Fi latency increase

### 8.3 Production Readiness (Phase 5 Complete)

✅ **Promotion Gate:**
- Code review sign-off: ✅
- Metrics gate sign-off: ✅
- System validation sign-off: ✅
- Architecture sign-off: ✅

✅ **Release Artifacts:**
- Release branch tagged: `release/esp-now-coexistence-v1.0`
- Merged to `main`
- Firmware version updated
- Release notes published

---

## 9. Appendix: Phase Implementation Checklists

### Phase 1 Implementation Checklist

- [ ] Add `AckToken` struct to scheduler header
- [ ] Implement `try_acquire_ack_token()` method
- [ ] Implement `release_ack_token()` method
- [ ] Implement `check_ack_token_watchdogs()` method
- [ ] Add watchdog constant: `ACK_TOKEN_WATCHDOG_MS = 500`
- [ ] Integrate send callback with `release_ack_token()`
- [ ] Gate `on_probe_received()` on token availability
- [ ] Add watchdog maintenance in handler task loop
- [ ] Unit test token lifecycle
- [ ] Unit test watchdog timeout
- [ ] Integration test probe flooding
- [ ] Build RX LCD: SUCCESS
- [ ] Build RX2: SUCCESS
- [ ] Field test: ACK success ≥98% under probe flood

### Phase 2 Implementation Checklist

- [ ] Define `ReconnectReason` enum
- [ ] Implement `initiate_reconnect(reason)` method
- [ ] Add `reconnect_in_progress_` guard state
- [ ] Consolidate heartbeat timeout reconnect logic
- [ ] Consolidate ACK pressure reconnect logic
- [ ] Consolidate MQTT connection loss reconnect logic
- [ ] Verify FSM state not directly changed in reconnect path
- [ ] Unit test idempotency (re-entry suppressed)
- [ ] Integration test reason logging
- [ ] Build RX LCD: SUCCESS
- [ ] Build RX2: SUCCESS
- [ ] Field test: Single reconnect on heartbeat timeout

### Phase 3 Implementation Checklist

- [ ] Implement `log_radio_diagnostics()` method
- [ ] Implement `log_power_save_diagnostics()` method
- [ ] Implement `log_fsm_diagnostics()` method
- [ ] Define `DiagnosticSnapshot` struct
- [ ] Implement `get_diagnostic_snapshot()` API
- [ ] Add 30s timer for radio diagnostics
- [ ] Add power-save event/timer logging
- [ ] Add FSM state change + 10s timer logging
- [ ] Add scheduler queue depth logging
- [ ] Unit test diagnostic methods
- [ ] Integration test logging intervals
- [ ] Build RX LCD: SUCCESS
- [ ] Build RX2: SUCCESS
- [ ] Field test: 5min continuous operation, metrics logged, <10% CPU overhead

### Phase 4 Test Checklist

- [ ] Set up isolated test network with controlled broker
- [ ] Write Python test harness (flash, inject, collect logs)
- [ ] Implement Scenario S1 (Broker Reset): 20 cycles
- [ ] Implement Scenario S2 (Wi-Fi Drop): 20 cycles
- [ ] Implement Scenario S3 (TX Flood): 20 cycles
- [ ] Implement Scenario S4 (Broker Latency): 20 cycles
- [ ] Implement Scenario S5 (Channel Asymmetry): 20 cycles
- [ ] Implement Scenario S6 (Simultaneous Failure): 20 cycles
- [ ] Verify S1 acceptance criteria (8s recovery, ≥95% ACK, no NO_MEM)
- [ ] Verify S2 acceptance criteria (15s re-join, normal heartbeat)
- [ ] Verify S3 acceptance criteria (survive flood, <80% queue, 5s recovery)
- [ ] Verify S4 acceptance criteria (detect latency, reconnect, ≥95% ACK)
- [ ] Verify S5 acceptance criteria (detect channel mismatch, 30s recovery)
- [ ] Verify S6 acceptance criteria (DEGRADED_FALLBACK, 10s recovery)
- [ ] Verify overall stability (zero crashes × 120 cycles)
- [ ] Verify memory stability (no leaks)
- [ ] Verify parity (RX LCD vs RX2 within 5%)

### Phase 5 Gate Verification Checklist

- [ ] Code review: All phases complete per spec
- [ ] Code review: No regressions
- [ ] Code review: Thread-safety verified
- [ ] Metrics gate: All diagnostics present
- [ ] Metrics gate: No performance degradation
- [ ] System gate: Phase 4 results ≥95% acceptance
- [ ] System gate: Zero crashes in 120 cycles
- [ ] System gate: RX LCD + RX2 parity verified
- [ ] System gate: 24-hour continuous operation tested
- [ ] Developer sign-off: Implementation complete
- [ ] Peer sign-off: Code review complete
- [ ] QA sign-off: Phase 4 testing complete
- [ ] Architecture sign-off: Hardening profile met
- [ ] Tag release branch: `release/esp-now-coexistence-v1.0`
- [ ] Merge to main: ✅
- [ ] Update firmware version: ✅
- [ ] Release notes published: ✅

---

## 10. Document Versioning

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2026-04-30 | Initial implementation plan created from hardening profile |

---

**End of Implementation Plan**

*Next Action: Begin Phase 1 implementation per checklist. Estimated completion: 3–4 days.*
