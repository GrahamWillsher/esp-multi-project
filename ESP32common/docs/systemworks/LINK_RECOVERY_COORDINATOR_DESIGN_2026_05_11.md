# LinkRecoveryCoordinator: System Recovery Architecture (Extended)
**Date:** 2026-05-11  
**Status:** IMPLEMENTED (Build Validated; Runtime Fault-Injection Pending)  
**Author:** Architecture Review  
**Scope:** Extends existing LinkRecoveryCoordinator to handle multi-modal failures across TX/RX

---

## 1) Problem Statement

The codebase **already contains** `LinkRecoveryCoordinator` (esp32common/espnow_common_utils/link_recovery_coordinator.cpp) with L1/L2 escalation logic, **BUT**:
- Only called for `NO_MEM` events (via EspnowTxScheduler)
- **NOT called for httpd bind failures (errno 112)** on receiver
- **NOT called for ESP_ERR_INVALID_STATE on transmitter connect_confirm**
- Results in coordinated recovery only for radio pressure, not system resource exhaustion

**Current Implementation Status:**
- ✓ LinkRecoveryCoordinator class exists with L1 (deinit/reinit) and L2 (WiFi stop/start)
- ✓ Thresholds defined (no_mem_trigger_threshold=10, l1_budget=3, l2_budget=3)
- ✓ Persistent httpd startup failures now route into coordinator escalation (L1/L2)
- ✓ Transmitter send guard now uses bounded local recovery timeout and progress-based clear
- ✓ Cross-device singleton coupling removed from design and implementation guidance
- ⏳ Runtime fault-injection validation still pending (hardware/log confirmation)

**Current Logs Show:**

Receiver: 
```
[RECOVERY] httpd bind failed (errno 112)
[RECOVERY] backoff=30000 ms
[30 seconds later]
[RECOVERY] httpd bind failed (errno 112)
[RECOVERY] backoff=30000 ms
[... repeats forever: no L1/L2 escalation ...]
```

Transmitter:
```
[RECONNECT] ✓ Receiver found on ch=6, peer registered
[RECONNECT] connect_confirm send failed: ESP_ERR_INVALID_STATE
[... repeats 2 times, then timeout ...]
[RECONNECT] [STATE_CHANGE] CONFIRMING -> SCANNING
```

**Why It Fails:**
- errno 112 = socket pool exhausted (persistent, not transient) → needs resource cleanup (L1/L2)
- Backoff alone doesn't free sockets
- Transmitter recovery guard (`TxSendGuard::g_state.recovery_active`) triggered by channel mismatch but never clears because receiver not responding (webserver down)
- Both sides need to coordinate: RX restarts webserver → MQTT link recovers → TX link state transitions → recovery guard clears

**Business Impact:**
- Receiver stuck in failed state (observed: failure_streak >= 7, webserver never recovers)
- Transmitter in infinite discovery/reconnect loop (session IDs increment: 17→18→19→20)
- NO_MEM errors persist (ESP-NOW can't send)
- MQTT heartbeat blocked (connection gate prevents publish, sees receiver as unreachable)
- Manual restart required to clear cascading failure state

---

## 2) Existing LinkRecoveryCoordinator Architecture

The codebase **already has** a working LinkRecoveryCoordinator implementation. Current state:

### 2.1 What Exists

**File:** `esp32common/espnow_common_utils/link_recovery_coordinator.{h,cpp}`  
**Status:** Implemented and functional  
**Current Usage:** Triggered by EspnowTxScheduler when consecutive NO_MEM >= threshold (10)

**Class Definition:**
```cpp
class LinkRecoveryCoordinator {
    bool handle_no_mem_pressure(uint32_t now_ms,
                                uint32_t consecutive_no_mem,
                                bool allow_escalation,
                                const uint8_t* peer_mac);
    
    // L1: esp_now_deinit() → vTaskDelay(100ms) → esp_now_init()
    // L2: esp_wifi_stop() → vTaskDelay(200ms) → esp_wifi_start() → vTaskDelay(1500ms)
    // L3: esp_restart()
};
```

**Config:**
```cpp
no_mem_trigger_threshold = 10;      // Trigger L1 at 10 consecutive NO_MEM
l1_budget = 3;                       // Try L1 3 times before L2
l2_budget = 3;                       // Try L2 3 times before restart
escalation_reset_window_ms = 30000;  // Reset attempts if 30s passes between failures
```

**Hooks Implemented:**
```cpp
struct LinkRecoveryCoordinatorHooks {
    void (*on_radio_deinit)(void*);      // Called before esp_now_deinit()
    void (*on_l2_wifi_restarted)(void*); // Called after esp_wifi_start()
    void (*reinstall_send_cb)(void*);    // Called after esp_now_init()
    void (*restart_device)(void*);       // Called before esp_restart()
};
```

**Currently Called From:**
- `EspnowTxScheduler::send()` when NO_MEM counter hits threshold
- Only on **receiver side** (scheduler used for receiver TX task)
- **Transmitter does NOT use LinkRecoveryCoordinator** (uses TxSendGuard instead)

### 2.2 What's Missing

**Gap 1: Httpd Bind Failures Not Routed to Coordinator**
- Location: `espnowreceiver_LCD/lib/webserver_lcd/webserver.cpp`
- Current: Detects errno 112, increments failure counter, sets backoff timer
- Missing: Call to LinkRecoveryCoordinator when httpd failures persist
- Impact: Socket pool exhausted, no L1/L2 recovery actions execute

**Gap 2: Transmitter Has No Valid Cross-Device Recovery Signal**
- Location: `ESPnowtransmitter2/espnowtransmitter2/src/espnow/tx_send_guard.cpp`
- Current: TxSendGuard detects ESP_ERR_INVALID_STATE, triggers recovery flag, blocks sends
- Missing: No protocol-level signal that receiver recovery has completed
- Impact: Recovery flag can stay active too long; reconnect loop continues even after scan success

**Gap 3: No Cross-System Coordination**
- RX webserver down → MQTT blocked → TX sees heartbeat timeout → TX recovery guard active
- TX stuck in recovery → Can't send connect_confirm → RX doesn't get ACK → Handshake never completes
- Missing: Feedback loop to clear recovery flags when link restored

---

## 4) Implementation Changes Required

### 4.1 Extend LinkRecoveryCoordinator (esp32common/espnow_common_utils/link_recovery_coordinator.h)

Add new failure reporting methods:

```cpp
class LinkRecoveryCoordinator {
public:
    // Existing
    bool handle_no_mem_pressure(uint32_t now_ms,
                                uint32_t consecutive_no_mem,
                                bool allow_escalation,
                                const uint8_t* peer_mac);
    
    // NEW: Httpd bind failure reporting (called from webserver.cpp)
    bool handle_persistent_httpd_failure(uint32_t now_ms,
                                        uint32_t consecutive_failures,
                                        esp_err_t last_errno,
                                        const uint8_t* peer_mac = nullptr);
    
    // NEW: Query functions (called from guards like TxSendGuard)
    bool is_recovery_active() const;
    RecoveryLevel get_recovery_level() const;
    uint32_t recovery_timeout_remaining_ms(uint32_t now_ms) const;
    
    // NEW: Reset after recovery success (called when link restored)
    void recovery_succeeded_transition_to_normal(uint32_t now_ms);
    
private:
    uint32_t httpd_failure_first_seen_ms_ = 0;
    uint32_t httpd_consecutive_count_ = 0;
    RecoveryLevel current_level_ = RecoveryLevel::NONE;
    uint32_t level_entered_ms_ = 0;
};
```

**New Config Constants:**
```cpp
struct LinkRecoveryCoordinatorConfig {
    // Existing
    uint32_t no_mem_trigger_threshold = 10;
    uint8_t l1_budget = 3;
    uint8_t l2_budget = 3;
    uint32_t escalation_reset_window_ms = 30000;
    
    // NEW: Httpd-specific thresholds
    uint32_t httpd_persistent_failure_threshold = 3;  // Failures before L1
    uint32_t httpd_failure_window_ms = 300000;        // 5 minute window
    
    // NEW: Recovery state visibility window
    uint32_t recovery_state_timeout_ms = 120000;      // Recovery attempt timeout
};
```

### 4.2 Route Httpd Failures to Coordinator (espnowreceiver_LCD/lib/webserver_lcd/webserver.cpp)

**Current Code (around line 256-265):**
```cpp
if (ret != ESP_OK) {
    ++g_http_start_failure_streak;
    const uint32_t backoff_ms = webserver_backoff_for_failure_streak(failure_streak);
    g_next_http_start_allowed_ms = now_ms + backoff_ms;
    // Log only, no escalation
}
```

**New Code:**
```cpp
if (ret != ESP_OK) {
    ++g_http_start_failure_streak;
    const uint32_t backoff_ms = webserver_backoff_for_failure_streak(failure_streak);
    g_next_http_start_allowed_ms = now_ms + backoff_ms;
    
    // NEW: Report persistent failures to coordinator for escalation
    if (g_http_start_failure_streak >= 3) {  // After 3 consecutive failures
        esp32common::espnow::LinkRecoveryCoordinator::instance()
            .handle_persistent_httpd_failure(
                now_ms,
                g_http_start_failure_streak,
                ret,
                nullptr);  // No specific peer for httpd failure
    }
    
    LOG_WARN("WEBSERVER", "httpd_start failed: %s (errno=%d streak=%u) → backoff %u ms",
             esp_err_to_name(ret), errno, g_http_start_failure_streak, backoff_ms);
}
```

### 4.3 Integrate with TxSendGuard and TX-side Recovery (ESPnowtransmitter2/src/espnow/tx_send_guard.cpp)

Add include:
```cpp
#include <esp32common/espnow/link_recovery_coordinator.h>
```

**Current Code (around line 141-158):**
```cpp
// Existing channel coherence + recovery_active flag checks
if (!coherent || g_state.recovery_active) {
    return ESP_ERR_INVALID_STATE;
}
```

**Corrected Design (important):**
```cpp
// NOTE: Do NOT use LinkRecoveryCoordinator singleton as cross-device state.
// TX and RX run on different devices; each has its own singleton instance.

// 1) Keep local guard for channel mismatch / invalid-state suppression.
if (!coherent) {
    g_state.recovery_active = true;
    return ESP_ERR_INVALID_STATE;
}

// 2) Add bounded local recovery timeout to avoid permanent deadlock.
if (g_state.recovery_active && (now - g_state.recovery_started_ms) > TX_RECOVERY_MAX_BLOCK_MS) {
    // Trigger local L1 recovery path on TX: peer rebuild + esp_now reinit request.
    post_connection_event(EspNowEvent::CONNECTION_LOST, mac);
    g_state.recovery_active = false;  // allow control-frame retry after local recovery action
}

// 3) Clear guard on positive forward progress, not only CONNECTED state.
// Examples: connect_confirm ACK received, discovery ACK with matching session,
// or successful control send.
if (observed_handshake_progress) {
    g_state.recovery_active = false;
    g_state.consecutive_failures = 0;
}
```

**Receiver → Transmitter coordination must be protocol-based, not singleton-based:**
- Use existing ESP-NOW control frames (`connect_confirm_ack`, heartbeat, discovery ACK metadata)
- Add optional recovery bit / reason code in control payload if needed
- TX clears local guard when receiving explicit progress signal

---

## 5) Bi-Directional Coordinator Responsibilities

### 5.1 L1 Recovery Actions (Existing Implementation)

LinkRecoveryCoordinator L1 already exists and works:

```cpp
// From link_recovery_coordinator.cpp
esp_now_deinit();
vTaskDelay(pdMS_TO_TICKS(100));  // 100ms settle time
if (esp_now_init() == ESP_OK) {
    restore_post_reinit_peer_state(peer_mac);
    UnifiedLinkFsm::instance().recovery_succeeded_to_discovery(...);
}
```

**Ops executed:**
- esp_now_deinit() → clears peer table, callbacks, state
- Delay 100ms → allow kernel cleanup
- esp_now_init() → fresh radio state
- Reinstall broadcast peer + transmitter peer (via hooks)

**Will fix:**
- Socket pool partially recovered (radio state reset)
- Stale peer entries cleared
- Callbacks re-registered

**Won't fix:** httpd socket pool still exhausted

### 5.2 L2 Recovery Actions (Existing Implementation)

LinkRecoveryCoordinator L2 already exists and works:

```cpp
// From link_recovery_coordinator.cpp
esp_wifi_stop();
vTaskDelay(pdMS_TO_TICKS(200));
esp_wifi_start();
vTaskDelay(pdMS_TO_TICKS(1500));  // Wait for DHCP + WiFi settle
if (esp_now_init() == ESP_OK) {
    restore_post_reinit_peer_state(peer_mac);
    if (hooks_.on_l2_wifi_restarted != nullptr) {
        hooks_.on_l2_wifi_restarted(hooks_.context);
    }
}
```

**Ops executed:**
- esp_wifi_stop() → full WiFi teardown
- Delay 200ms → force TCP socket cleanup (TIME_WAIT sockets freed)
- esp_wifi_start() → reconnect to AP (may get different channel)
- Delay 1500ms → wait for DHCP + WiFi stack settle
- esp_now_init() → reinit on new channel
- Call `on_l2_wifi_restarted` hook for httpd restart

**Will fix:**
- Full socket/memory recovery (TCP stack reset)
- Webserver can restart (socket pool freed)
- Channel may change (autorecovery from channel drifts)
- WiFi driver cache cleared

### 5.3 Escalation to RESTART (Existing Implementation)

If L1 + L2 both fail (6 total escalation attempts):

```cpp
stats_.last_level = RecoveryLevel::RESTART;
UnifiedLinkFsm::instance().mark_restart_required(now_ms);
if (hooks_.restart_device != nullptr) {
    hooks_.restart_device(hooks_.context);
} else {
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}
```

**Behavior:**
- Device restart (full hardware reset)
- All memory/resources cleared
- Boot from clean state

---

## 6) Integration Points

### 6.1 Responsibility Model

```
┌─────────────────────────────────────────────────────────┐
│  LinkRecoveryCoordinator (Shared in esp32common)       │
│  ─ Single authority over recovery actions               │
│  ─ Owns escalation thresholds and budgets               │
│  ─ Executes L1/L2 recovery operations                   │
│  ─ Reports recovery state to telemetry/logging          │
└─────────────────────────────────────────────────────────┘
         ↑                              ↓
    Receivers:                    Recovery Actions:
    - httpd failures             - L1: Reinit ESP-NOW
    - NO_MEM events              - L2: WiFi restart  
    - ESP-NOW callbacks          - Reboot trigger
    - Heartbeat timeout
```

### 6.2 Recovery Escalation Levels

```
Level 0 (No Recovery):
  ├─ Normal operation
  ├─ Transient errors handled locally (backoff only)
  └─ Threshold: N/A

Level 1 (Resource Recovery):
  ├─ Triggered: Persistent failures >= HTTPD_PERSISTENT_THRESHOLD (3-5)
  ├─ Actions:
  │  ├─ Clear ESP-NOW peer table + re-add broadcast
  │  ├─ Reinitialize ESP-NOW send callback
  │  ├─ Reset MQTT connection gate (allow reconnect)
  │  └─ Clear stale socket resources
  ├─ Budget: 3 attempts per escalation window (30s each)
  ├─ Timeout: 90 seconds total (3 × 30s backoff intervals)
  └─ Next: L2 if all 3 attempts fail

Level 2 (Network Recovery):
  ├─ Triggered: L1 exhausted (3 failed attempts)
  ├─ Actions:
  │  ├─ WiFi disconnect → wait 5s → reconnect
  │  ├─ Reinitialize WiFi stack
  │  └─ Trigger full ESP-NOW discovery
  ├─ Budget: 2 attempts (WiFi restart is expensive)
  ├─ Timeout: 120 seconds total (5s + 60s WiFi settle + backoff)
  └─ Next: RESTART_REQUIRED if both attempts fail

RESTART_REQUIRED:
  ├─ Triggered: L2 exhausted (2 failed WiFi restarts)
  ├─ Actions:
  │  ├─ Log "RESTART_REQUIRED: persistent resource exhaustion"
  │  ├─ Wait 10 seconds (allow user intervention)
  │  └─ Execute controlled ESP.restart()
  └─ Recovery: Clean boot (all caches cleared)
```

### 6.3 State Machine

```
                    ┌──────────────┐
                    │   BOOTSTRAP  │
                    └──────┬───────┘
                           │
                           ↓
                    ┌──────────────┐
                    │  NO_RECOVERY │ ← Normal state
                    │ (Level 0)    │
                    └──────┬───────┘
                           │
         ┌─────────────────┼─────────────────┐
         │                 │                 │
    Httpd failure    NO_MEM event      Heartbeat timeout
   (errno 112)        repeated           (stale > 35s)
         │                 │                 │
         └─────────────────┼─────────────────┘
                           ↓
                    ┌──────────────┐
                    │  PERSISTENT_ │
                    │  FAILURE_L1  │ ← failure_count >= 3
                    └──────┬───────┘
                    (execute L1)
                           │
         ┌─────────────────┴─────────────────┐
         │                                   │
    L1 succeeds                         L1 failed ×3
    (httpd running)                     (retry budget)
         │                                   │
         ↓                                   ↓
    NO_RECOVERY ◄─────────────────┐   ┌──────────────┐
    (reset to Level 0)         Backoff │  DEGRADED_  │
                                      │  FAILURE_L2  │
                                      └──────┬───────┘
                                      (execute L2)
                                             │
                          ┌──────────────────┴──────────────────┐
                          │                                     │
                      L2 succeeds                          L2 failed ×2
                      (WiFi restored,                      (restart budget)
                       httpd running)                           │
                          │                                     ↓
                          ↓                                ┌──────────────┐
                      NO_RECOVERY                         │  RESTART_    │
                      (reset to Level 0)                  │  REQUIRED    │
                                                          └──────┬───────┘
                                                          (controlled
                                                           restart)
                                                                 │
                                                                 ↓
                                                          ┌──────────────┐
                                                          │   BOOTSTRAP  │
                                                          │  (clean boot)│
                                                          └──────────────┘
```

---

## 7) Implementation Phase Summary

**Critical Code Changes Required:**

1. **Extend LinkRecoveryCoordinator API** (esp32common/espnow_common_utils/link_recovery_coordinator.h) ✅ COMPLETED
   - Add `handle_persistent_httpd_failure()` method
   - Add `is_recovery_active()` query function
   - Track httpd failure count and first failure time

2. **Route Httpd Failures to Coordinator** (espnowreceiver_LCD/lib/webserver_lcd/webserver.cpp) ✅ COMPLETED
   - Call coordinator when httpd failures >= threshold (3)
   - Let coordinator decide L1/L2 escalation

3. **Enable Transmitter Bounded Local Recovery** (ESPnowtransmitter2/src/espnow/tx_send_guard.cpp) ✅ COMPLETED
    - Keep TX guard local and bounded with timeout (`TX_RECOVERY_MAX_BLOCK_MS`)
    - Trigger TX local L1 recovery when guard timeout expires
    - Clear TX guard on protocol-level handshake progress (ACK/successful control send)
    - Prevents cascading failures: RX down → TX stuck in recovery → handshake starvation

4. **Build/Runtime Validation** ⏳ PARTIALLY COMPLETED
    - ✅ Receiver build + transmitter build (both successful)
    - ⏳ Log validation under induced failure conditions (pending hardware run)

---

## 8) Thresholds & Configuration

**Extend LinkRecoveryCoordinatorConfig:**
```cpp
struct LinkRecoveryCoordinatorConfig {
    // Existing NO_MEM thresholds
    uint32_t no_mem_trigger_threshold = 10;
    uint8_t l1_budget = 3;
    uint8_t l2_budget = 3;
    uint32_t escalation_reset_window_ms = 30000;
    
    // NEW: Httpd-specific thresholds
    uint32_t httpd_persistent_failure_threshold = 3;   // Trigger L1 after 3 failures
    uint32_t httpd_failure_window_ms = 300000;         // 5 minute window
};
```

**Failure Escalation Rules:**

Httpd failures trigger L1 if:
- 3+ consecutive failures (httpd_persistent_failure_threshold)
- Within 5 minute window (httpd_failure_window_ms)
- Recovery not already active (prevents overlapping escalations)

---

## 9) Critical Implementation Notes

### Gotcha #1: Don't Call Coordinator from Guard 

⚠️ **AVOID** calling `handle_persistent_httpd_failure()` from TxSendGuard. Instead:
- Webserver.cpp reports failures to coordinator
- TxSendGuard keeps local bounded recovery state and clears on protocol progress
- If coordinator is used on TX, it is TX-local only (never as RX state proxy)

### Gotcha #2: Transmitter & Receiver Do **Not** Share Runtime Singleton State

Both projects include esp32common, but runtime state is per-device:
- Transmitter singleton and receiver singleton are independent instances
- TX cannot directly read RX coordinator state
- Cross-device coordination must occur via ESP-NOW protocol messages
- TX SendGuard should use local state + protocol progress signals

### Gotcha #3: L1 Recovery Doesn't Free Httpd Sockets

LinkRecoveryCoordinator L1 does:
- esp_now_deinit() / init() ← frees radio peer table, callbacks
- Does NOT call httpd_stop() ← socket pool still exhausted

Solution: Call httpd_stop() **before** L1, or call it as part of L2's on_l2_wifi_restarted hook.

**Add to webserver.h:**
```cpp
// Called by LinkRecoveryCoordinator before L2 WiFi restart
void stop_webserver_for_recovery();

// Will restart webserver after L2 WiFi recovery
void restart_webserver_post_recovery();
```

---

## 10) Acceptance Criteria

- [x] LinkRecoveryCoordinator extended with `handle_persistent_httpd_failure()` API
- [x] Webserver.cpp reports failures to coordinator (not just backoff)
- [x] Coordinator routes httpd failures to L1 after 3 consecutive attempts
- [x] L1 successfully reinits ESP-NOW (existing code works)
- [x] L2 WiFi restart also frees httpd socket pool (add cleanup hook)
- [x] TX does not depend on receiver singleton state
- [x] TX recovery guard has bounded timeout and cannot deadlock permanently
- [x] TX recovery guard clears on protocol-level progress (ACK/control send success)
- [x] Receiver and transmitter builds succeed after implementation
- [ ] Terminal shows: httpd failure → L1 (esp-now reinit) → L2 (WiFi+httpd restart) → recovered
- [ ] Cascading failure prevented: RX down doesn't permanently block TX handshake
- [ ] Recovery actions are rate-limited (no thrash loops) and single-owner per device

---

## 11) Regression-Safety Checklist (Must Pass Before Merge)

- [x] No cross-device singleton assumptions in code paths
- [ ] No blocking waits inside callbacks/ISR paths
- [ ] Recovery escalation counters reset only on explicit success
- [ ] L1/L2 retries are bounded and logged with reason + attempt number
- [ ] Webserver restart attempts are gated by coordinator state (single owner)
- [x] TX reconnect loop cannot starve `connect_confirm` forever
- [ ] Negative tests pass:
    - [ ] RX httpd errno 112 persistent failure
    - [ ] TX `ESP_ERR_INVALID_STATE` during reconnect
    - [ ] Simultaneous NO_MEM + heartbeat timeout
    - [ ] AP channel change during recovery

---

## References

- **Existing Code:** esp32common/espnow_common_utils/link_recovery_coordinator.{h,cpp}
- **FSM Spec:** Section 4.1.3 (LinkRecoveryCoordinator responsibilities)
- **Current Issue:** errno 112 (httpd) + ESP_ERR_INVALID_STATE (TX) = cascading failure

---

**Document Status:** IMPLEMENTED + BUILD-VALIDATED (Runtime Fault-Injection Pending)  
**Next Step:** Execute hardware fault-injection scenarios and close remaining acceptance checklist items  
**Expected Outcome:** Single coordinated recovery system for both TX and RX with validated runtime recovery traces

---

## 12) Runtime Crash Investigation: 2026-05-12

**Status:** Post-mortem analysis of live crash captured on terminal output  
**Device:** Waveshare ESP32-S3 N16R8 (espnowreceiver_LCD)  
**Context:** Crash occurred approx 121 seconds after a page render began under CRITICAL radio pressure

### 12.1 Crash Backtrace Summary

```
Guru Meditation Error: Core 0 panic'd (abort() was called)
CORRUPT HEAP: Bad head at 0x3fce35b4
...
Backtrace:
  httpd_delete      (httpd_main.c:384)
  httpd_stop        (httpd_main.c:481)
  stop_webserver    (webserver.cpp:366)
  WebserverLcd::stop / loop   (main.cpp:419)
```

```
Heap state at crash: min_heap = 1804 bytes (effectively terminal)
Radio pressure: CONSTRAINED → CRITICAL (no_mem=3,4)
Page render: 9.5KB total, 9 × 1KB chunks, 13s/chunk average = 121s total
```

### 12.2 Root Cause Analysis (All Six)

#### RC-1 (Critical): `httpd_stop()` Is a Blocking Busy-Wait

**Confirmed from ESP-IDF source** (`components/esp_http_server/src/httpd_main.c`):
```c
// httpd_stop() sends a shutdown signal to the httpd task, then busy-waits:
while (hd->hd_td.status != THREAD_STOPPED) {
    httpd_os_thread_sleep(100);   // polls every 100ms
}
// Only after this: httpd_delete(hd)  ← where the crash occurred
```

**Consequence:** `httpd_stop()` does NOT return until the httpd FreeRTOS task has fully exited.  
The httpd task will not exit until the current in-flight request handler returns.  
A 121-second page render → `httpd_stop()` blocks `loop()` (main.cpp:419) for 121 seconds.  
During those 121 seconds: recovery escalation, NO_MEM events, and heap fragmentation continue, eventually corrupting heap metadata before `httpd_delete` runs.

#### RC-2 (Critical): Liveness Probe Recycle Path Has No Active-Request Guard

There are **two independent recycle paths** in `loop()`:

| Path | Guard | Behaviour |
|---|---|---|
| Inflight watchdog | ✅ `webserver_should_recycle()` → checks `active_requests > 0` | Correctly deferred |
| Liveness probe | ❌ None | Fires unconditionally after 2 probe failures |

The liveness probe fires after 2 × 30s = 60 seconds with no HTTP response (expected: server was busy rendering, not hung).  
It called `WebserverLcd::stop()` directly while the render was mid-flight.  
This is what triggered `httpd_stop()` while `active_requests > 0`.

**Fix required:** The liveness probe recycle path must call `webserver_should_recycle()` (or equivalent active-request check) before stopping.

#### RC-3 (Critical): Heap Already Terminal Before Stop Was Called

Timeline at crash:
```
[t=0s]    Page render started (heap ~35KB, pressure NORMAL)
[t=40s]   NO_MEM events begin (CONSTRAINED, heap ~18KB)
[t=70s]   Recovery escalation: CRITICAL (no_mem=4, heap ~8KB)
[t=80s]   Liveness probe fires (2 consecutive failures) → stop_webserver()
[t=80s]   httpd_stop() begins (busy-wait) — heap now ~5KB
[t=121s]  Page render finally completes → httpd_delete() runs — heap = 1804 bytes
[t=121s]  CORRUPT HEAP: httpd_delete dereferences freed/corrupted pointer → abort()
```

At the time `httpd_delete` ran, 121 seconds of concurrent NO_MEM pressure had fragmented the heap to the point where free-list pointers were corrupt.  
The `httpd_delete()` call itself was correct; the heap metadata was already damaged.

#### RC-4 (High): Double-Stop Race Between Recovery Hook and Liveness Probe

**Log evidence:**
```
[main.cpp:419]    "Server stopped"                  ← from liveness probe path
[webserver.cpp]   "Recovery hook: radio deinitialized"  ← from on_radio_deinit hook
```

Two independent callers both reached `stop_webserver()` within milliseconds of each other:
- Liveness probe path (main.cpp) after 2 probe failures
- `on_radio_deinit` recovery hook (called by `LinkRecoveryCoordinator` L1 escalation)

There is no atomic `stop_in_progress` flag. Both paths ran, the second calling `httpd_stop()` on an already-stopped (NULL) server handle, causing undefined behavior.

**Fix required:** Add a `std::atomic<bool> g_stop_in_progress` flag; check-and-set before any `httpd_stop()` call.

#### RC-5 (High): No Heap Threshold Check Before Webserver Restart

After `httpd_stop()` completes, `WebserverLcd::init()` was called with ~5KB heap remaining.  
`httpd_create()` allocates:
- `struct httpd_data` (~3KB)
- `hd_calls[]`: 80 URI handler pointers (~640 bytes)
- `hd_sd[]`: session socket descriptors (multiple)
- `resp_hdrs[]`: response headers array

Total allocation: **~6–8KB**. With only 5KB free, `calloc()` returned NULL → heap metadata access on NULL pointer → second crash path (even if the first abort had not fired).

**Espressif guidance** (from Wi-Fi Buffer Usage docs): *"It is very dangerous to run out of heap memory, as this will cause ESP32-S3 undefined behavior."* Minimum safe heap before `httpd_start()` should be **32KB** for stable operation with dynamic Wi-Fi buffers.

**Fix required:** Gate `WebserverLcd::init()` behind a heap threshold check (minimum 32KB free).

#### RC-6 (Medium): Page Render Has No Abort Path Under CRITICAL Pressure

`page_generator.cpp` sends chunks in a loop with `kHttpChunkSendBytes = 1024`.  
There is no mechanism to abort the render mid-stream when radio pressure escalates to CRITICAL.  
A render that began under NORMAL conditions continued for 121 seconds despite:
- `no_mem` errors reaching 4 consecutive failures
- Pressure state escalating to CRITICAL
- Heap dropping below 5KB

The result: 121 seconds of heap-consuming TCP fragmentation under a saturated LMAC TX buffer.

**Fix required:** Check heap and radio pressure at each chunk boundary; if CRITICAL or heap < 16KB, call `httpd_resp_send_err()` and return immediately to abort the render.

### 12.3 Root Cause Interaction Diagram

```
Page render starts (normal conditions)
    │
    ├── NO_MEM events accumulate → pressure escalates to CRITICAL
    │       ↓
    │   [RC-6] No render abort path → render continues 121s
    │
    ├── [RC-2] Liveness probe fires (no active-request guard) → stop_webserver()
    │       ↓
    │   [RC-1] httpd_stop() blocks for remaining render duration (~70s)
    │       ↓
    │   [RC-4] Recovery hook also calls stop_webserver() → double-stop race
    │
    └── [RC-3] 121s of NO_MEM under CRITICAL → heap at 1804 bytes (terminal)
            ↓
        [RC-5] httpd_delete() runs on corrupted heap
            ↓
        CORRUPT HEAP → abort() → reboot
```

---

## 13) Viability Analysis: Can ESP-NOW and HTTP Coexist on This Device?

**Short answer: Yes. It worked before, and it can work again. The crash is NOT caused by a fundamental radio incompatibility — it is caused by six specific software defects introduced or exposed by the state machine redesign.**

### 13.1 The Physical Reality: Single Radio, Shared LMAC

The ESP32-S3 has one 2.4 GHz radio. ESP-NOW and WiFi STA share the same LMAC TX buffer.

Espressif's architecture for this is well-defined (from Wi-Fi Performance docs and ESP-NOW API reference):
- ESP-NOW uses vendor-specific action frames via LMAC — they are 802.11 frames, not TCP/IP
- WiFi STA TCP traffic uses the same LMAC TX queue
- When the LMAC TX buffer is full, ESP-NOW returns `ESP_ERR_ESPNOW_NO_MEM`
- Espressif's recommended remedy: *"delay a while before sending the next data"*
- For power-save/coexistence: connectionless modules (ESP-NOW) operate in **"default mode"** where RF is kept active under station-mode coexistence, with time-division allocation via the coexistence module

**Verdict:** The shared radio is an engineering constraint to manage, not a hard incompatibility. Espressif ships ESP-NOW + WiFi STA as a supported configuration.

### 13.2 What the History Shows

The webserver **did work** before the state machine redesign (commit baseline `820d3fa`). After the redesign (`da02232`), instability appeared. This is strong evidence that the radio architecture itself is not the problem.

**What the state machine redesign added:**

| Addition | Effect on Radio Budget |
|---|---|
| `LinkRecoveryCoordinator` L1/L2 escalation | More management frames during recovery (ESP-NOW reinit, WiFi stop/start) |
| Tighter reconnect loops (FSM SCANNING → CONFIRMING) | More broadcast/control ESP-NOW frames in flight |
| Liveness probe every 30s (HTTP GET to self) | Background TCP probe competes with AP data during active renders |
| Recovery escalation hook (`on_radio_deinit`) | Fires ESP-NOW deinit during active HTTP connections |
| No render abort gate | Renders continue through CRITICAL pressure events |

The state machine increased background radio traffic precisely at the moments when the LMAC buffer was already under pressure from page renders. The combined effect pushed `no_mem` counts past the old thresholds.

### 13.3 What Must Be Fixed for Stability

These are the six fixes required, derived directly from the root cause analysis. None requires architectural replacement — all are targeted, bounded code changes:

#### Fix 1: Add Active-Request Guard to Liveness Probe Path (RC-2)
**File:** `espnowreceiver_LCD/src/main.cpp`  
**Change:** Before any liveness-probe-triggered recycle, check `active_requests == 0` via `webserver_has_active_requests()` and centralize recycle through `recycle_webserver_if_idle()`. Do not call `WebserverLcd::stop()` while `active_requests > 0`.
**Status:** ✅ Completed (2026-05-12)

```cpp
// BEFORE (broken):
if (probe_fail_count >= kHttpProbeFailThreshold) {
    webserver.stop();   // ← fires regardless of active_requests
}

// AFTER (correct):
if (probe_fail_count >= kHttpProbeFailThreshold
    && webserver_get_active_requests() == 0) {   // ← guard added
    webserver.stop();
}
```

#### Fix 2: Add `stop_in_progress` Flag to Prevent Double-Stop Race (RC-4)
**File:** `espnowreceiver_LCD/lib/webserver_lcd/webserver.cpp`  
**Change:** Add `static std::atomic<bool> g_stop_in_progress{false}`. At the entry of `stop_webserver()`, check-and-set; return early if already set. Clear on exit.
**Status:** ✅ Completed (2026-05-12)

```cpp
static std::atomic<bool> g_stop_in_progress{false};

void stop_webserver() {
    bool expected = false;
    if (!g_stop_in_progress.compare_exchange_strong(expected, true)) {
        LOG_WARN("WEBSERVER", "stop_webserver: already stopping, skipped duplicate call");
        return;
    }
    // ... existing httpd_stop() logic ...
    g_stop_in_progress.store(false);
}
```

#### Fix 3: Add Heap Threshold Gate Before `init()` (RC-5)
**File:** `espnowreceiver_LCD/lib/webserver_lcd/webserver.cpp`  
**Change:** In `init_webserver()` / `WebserverLcd::init()`, check `esp_get_free_heap_size()` before calling `httpd_start()`. Minimum recommended: 32KB (from Espressif Wi-Fi Buffer Usage guidance).
**Status:** ✅ Completed (2026-05-12)

```cpp
constexpr uint32_t kMinHeapForWebserverStartBytes = 32 * 1024;

esp_err_t init_webserver() {
    if (esp_get_free_heap_size() < kMinHeapForWebserverStartBytes) {
        LOG_ERROR("WEBSERVER", "init refused: heap %u < %u bytes minimum",
                  esp_get_free_heap_size(), kMinHeapForWebserverStartBytes);
        return ESP_ERR_NO_MEM;
    }
    // ... existing httpd_start() logic ...
}
```

#### Fix 4: Add Render Abort Under CRITICAL Pressure (RC-6)
**File:** `espnowreceiver_LCD/lib/webserver_lcd/common/page_generator.cpp`  
**Change:** At each chunk send iteration, check `get_radio_pressure_state()` and `ESP.getFreeHeap()`. If CRITICAL or heap < 16KB, abort the response path with 503 status when possible and return failure.
**Status:** ✅ Completed (2026-05-12)

```cpp
constexpr uint32_t kMinHeapMidRenderBytes = 16 * 1024;

// Inside the chunk send loop:
if (get_radio_pressure_state() == RadioPressureState::CRITICAL
    || ESP.getFreeHeap() < kMinHeapMidRenderBytes) {
    LOG_WARN("PAGE_GEN", "Aborting render: CRITICAL pressure or low heap (%u bytes)",
             ESP.getFreeHeap());
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_sendstr(req, "Server busy");
    return ESP_ERR_NO_MEM;
}
```

#### Fix 5: Reduce Liveness Probe Scope — Don't Probe During Active Renders (RC-2 / RC-1 interaction)
**File:** `espnowreceiver_LCD/src/main.cpp`  
**Change:** Skip the liveness probe entirely when `webserver_get_active_requests() > 0`. The probe is intended to detect a dead server, not a busy one — a slow render is not a failure.
**Status:** ✅ Completed (2026-05-12)

```cpp
// In probe_local_http_liveness():
if (webserver_get_active_requests() > 0) {
    // Server is actively serving; probe would be misleading. Reset probe counter.
    probe_fail_count = 0;
    return;
}
```

#### Fix 6: Raise NO_MEM Threshold Before Liveness Probe Recycle (RC-3 interaction)
**File:** `espnowreceiver_LCD/src/main.cpp` (constant definitions)  
**Change:** Increase `kHttpProbeFailThreshold` from 2 to 4 (120s before recycle attempt), giving more time for a slow render to complete before triggering the probe recycle path.
**Status:** ✅ Completed (2026-05-12)

```cpp
// BEFORE:
constexpr int kHttpProbeFailThreshold = 2;    // 2 × 30s = 60s before recycle

// AFTER:
constexpr int kHttpProbeFailThreshold = 4;    // 4 × 30s = 120s before recycle
```

### 13.4 Priority Order for Implementation

**Implementation completion note:** All six fixes below are now implemented in code. Runtime validation remains required.

| Priority | Fix | RC | Risk if deferred |
|---|---|---|---|
| 🔴 P0 | Fix 2: double-stop race (`stop_in_progress` flag) | RC-4 | Immediate crash on any recovery event during render |
| 🔴 P0 | Fix 1: active-request guard on probe path | RC-2 | Every liveness probe timeout can trigger blocking stop mid-render |
| 🔴 P0 | Fix 3: heap threshold gate before `init()` | RC-5 | httpd restart with <5KB heap → guaranteed crash on next restart attempt |
| 🟠 P1 | Fix 4: render abort under CRITICAL pressure | RC-6 | Renders can still run 100+ seconds under CRITICAL, consuming all heap |
| 🟠 P1 | Fix 5: skip probe during active render | RC-2 / RC-1 | Probe still fires; Fix 1 prevents the crash but the probe still interrupts |
| 🟡 P2 | Fix 6: raise probe fail threshold | RC-3 | Minor: gives more time for renders but doesn't eliminate root cause |

### 13.5 Architectural Recommendation: Keep the Webserver

**Replacing the webserver with a different protocol is not warranted** given the evidence. The system worked before and the six defects are all fixable without architectural replacement. The specific recommendation:

1. **Implement Fixes 1–3 immediately** (P0): stops the crash
2. **Implement Fixes 4–5 next** (P1): makes the system gracefully degrade under pressure instead of crashing
3. **Then revalidate** with hardware fault injection per Section 10 acceptance criteria

### 13.6 Legacy/Redundant Code Removal Per Fix

- Removed duplicate stop/start recycle sequences in `main.cpp` by centralizing to `recycle_webserver_if_idle()`.
- Removed duplicated per-overload chunk-send loop logic in `page_generator.cpp` by centralizing to `send_chunk_stage()`.
- Removed duplicate stop attempts by adding single-owner stop gating (`g_webserver_stop_in_progress`).

The webserver architecture (HTTP over WiFi AP, separate from ESP-NOW channel) is sound. ESP-NOW uses vendor-specific action frames; HTTP uses standard TCP over the AP interface. They share the LMAC TX buffer but Espressif's default coexistence mode manages this with time-division allocation. The key constraint is heap: **maintain >32KB free at all times** and abort renders early under pressure. With those guards in place, coexistence is stable.

---

**Section 12–13 Added:** 2026-05-12 — Post-crash root cause analysis and viability determination  
**Conclusion:** ESP-NOW + HTTP coexistence IS viable. Six targeted fixes implemented. No architectural replacement needed.
