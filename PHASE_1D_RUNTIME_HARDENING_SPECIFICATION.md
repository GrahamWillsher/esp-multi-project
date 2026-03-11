# Phase 1D Runtime Hardening Specification

**Date:** March 10, 2026  
**Status:** SPECIFICATION READY FOR EXECUTION  
**Scope:** Comprehensive runtime validation of reconnect resilience, startup sequences, and fault scenarios

---

## Executive Summary

Phase 1D validates that the Phase 1C hardening (TX backoff-aware deferred discovery + common timeout ownership) actually **works in real-world scenarios**. This is the final "proof of work" before the state machine implementation can be considered production-ready.

**Objective:** Execute 5 test scenarios across 20+ test cases to validate:
- ✅ Repeated disconnect/reconnect cycles (robustness)
- ✅ Startup order permutations (initialization safety)
- ✅ Heartbeat loss/delay scenarios (fault tolerance)
- ✅ Stale/recovery regressions (data consistency)
- ✅ Performance under stress (latency/resource bounds)

**Duration:** 2-3 hours of hands-on testing + 1 hour documentation  
**Hardware:** 1 Transmitter ESP32, 1 Receiver ESP32, test harness  
**Success Criteria:** All 20+ test cases PASS with zero data loss or permanent hangs

---

## Test Matrix Overview

| Scenario | Test Cases | Expected | Current |
|----------|-----------|----------|---------|
| **1. Disconnect/Reconnect Endurance** | 6 cases | PASS | TBD |
| **2. Startup Order Permutations** | 4 cases | PASS | TBD |
| **3. Heartbeat Loss/Delay** | 5 cases | PASS | TBD |
| **4. Stale/Recovery Regressions** | 3 cases | PASS | TBD |
| **5. Performance Under Stress** | 2 cases | PASS | TBD |
| **TOTAL** | **20+ cases** | **PASS** | **TBD** |

---

## Scenario 1: Disconnect/Reconnect Endurance (6 Cases)

**Objective:** Verify system reliably reconnects after intentional disconnections without data loss or hangs.

### Test 1.1: Rapid Power Cycle (5 Cycles)
**Setup:**
- Both devices running, connected and transmitting
- Receiver actively receiving at 2-second intervals

**Procedure:**
1. TX sending every 2s (target: 5 messages before disconnect)
2. Power off RX (simulates receiver crash)
3. RX boots and re-establishes connection
4. Verify: No message loss, no duplicate messages
5. Repeat 5 times

**Expected Result:** ✅ PASS
- All reconnections succeed within 2-4 seconds
- No message duplication after reconnect
- No hang or timeout

**Pass Criteria:**
- Each reconnect completes in < 5 seconds
- All 50 messages received without loss or duplication (5 cycles × 10 messages/cycle)
- Display shows continuous state: NORMAL_OPERATION → STALE → NORMAL_OPERATION

### Test 1.2: TX Power Cycle (Transmitter Crash)
**Setup:** Reverse of 1.1 - TX powers off, RX stays on

**Procedure:**
1. RX actively receiving, display updating every 2s
2. Power off TX
3. Verify RX transitions: ACTIVE → STALE (after grace window) → STALE
4. Power on TX
5. Verify TX rediscovers RX and resumes transmission within 2-4 seconds

**Expected Result:** ✅ PASS
- RX detects stale within 90-95 seconds (HEARTBEAT_TIMEOUT_MS)
- Display shows "NO DATA" or STALE state after timeout
- TX reconnects and displays show ACTIVE within 2-4 seconds
- First message post-reconnect is not duplicated

**Pass Criteria:**
- Stale timeout fires reliably within configured window
- Reconnection occurs without timeout errors
- Grace window handling correct (stale vs. waiting for data)

### Test 1.3: Network Disconnection (WiFi Outage at TX)
**Setup:** TX Ethernet connected (MQTT can publish), RX on WiFi

**Procedure:**
1. Disable TX Ethernet (simulates network outage)
2. Verify: TX enters IDLE state (Ethernet lost)
3. Verify: RX transitions ACTIVE → STALE (after timeout)
4. Re-enable TX Ethernet
5. Verify: TX reconnects and resumes within 5 seconds

**Expected Result:** ✅ PASS
- Ethernet loss doesn't block ESP-NOW reconnection
- RX stale state is transient (clears on reconnect)
- No MQTT publish failures visible in logs

**Pass Criteria:**
- Independent network (Ethernet) loss doesn't cause permanent disconnection
- Reconnect still proceeds via ESP-NOW after network recovers

### Test 1.4: Repeated Disconnection (20 Cycles, 30 Second Intervals)
**Setup:** Automated disconnect via test harness (can be simulated with manual power cycling)

**Procedure:**
1. Connect TX and RX
2. Transmit for 30 seconds (15 messages)
3. Power off RX for 3 seconds
4. Power on RX (reconnects within 2-4 seconds)
5. Repeat 20 times

**Expected Result:** ✅ PASS
- System never hangs or requires reset
- Reconnection backoff works (doesn't DOS the receiver with probes)
- No message duplication detected across 20 cycles

**Pass Criteria:**
- All 20 reconnections succeed
- Exponential backoff active (3rd-5th reconnect attempts slightly slower)
- Memory stable (no memory leak growth)

### Test 1.5: Long Silence Followed by Reconnection
**Setup:** RX powered off for 5+ minutes, then reconnected

**Procedure:**
1. TX running continuously
2. Power off RX for 5 minutes (well past all timeouts)
3. Verify TX enters PERSISTENT_FAILURE state after 3 retry attempts
4. Power on RX
5. Verify: TX detects RX within 2-4 seconds
6. Verify: Full data transmission resumes

**Expected Result:** ✅ PASS
- TX timeout handling works (doesn't retry forever)
- TX correctly transitions to PERSISTENT_FAILURE after backoff exhausted
- Reconnect after long silence is clean (no state contamination)

**Pass Criteria:**
- Backoff timeout is finite (3 retries with exponential backoff, max ~5 seconds total)
- Reconnection after PERSISTENT_FAILURE state is clean
- First message post-reconnect has correct sequence number

### Test 1.6: Reconnection During Message Transmission
**Setup:** TX actively sending, RX/TX lose connection mid-message

**Procedure:**
1. TX starts sending large message (300+ bytes, multi-frame)
2. After 1 frame received, power off RX
3. TX should handle failure gracefully (no hang)
4. RX boots and TX reconnects
5. Verify: Next complete message is received without corruption

**Expected Result:** ✅ PASS
- Fragment timeout (5s) abandons incomplete message
- TX doesn't deadlock waiting for ACK
- Reconnection proceeds normally
- No corrupted/partial messages in display

**Pass Criteria:**
- Timeout for incomplete fragments is reliable (5 seconds)
- No hang or deadlock on fragment timeout
- Message boundaries are preserved (no partial message display)

---

## Scenario 2: Startup Order Permutations (4 Cases)

**Objective:** Verify system handles any boot sequence order without race conditions or missed connections.

### Test 2.1: RX Powers On First
**Procedure:**
1. Power ON RX only (nothing to connect to)
2. Wait 10 seconds (RX listening, no signals)
3. Power ON TX
4. Verify: TX discovers RX within 2-4 seconds
5. Verify: Both show ACTIVE/NORMAL_OPERATION state

**Expected Result:** ✅ PASS
- TX active hopping finds RX (cold start discovery)
- RX transitions: IDLE → CONNECTED → ACTIVE within 5 seconds
- First few messages may be retried, but no data loss after stable state reached

**Pass Criteria:**
- TX completes discovery in < 4 seconds (using last_channel optimization or full scan)
- RX correctly transitions to CONNECTED on PROBE reception
- No race conditions or missed initial messages

### Test 2.2: TX Powers On First
**Procedure:**
1. Power ON TX only (no receiver)
2. Wait 10 seconds (TX in RECONNECTING, backing off)
3. Power ON RX
4. Verify: RX connects within 2-4 seconds despite TX backoff

**Expected Result:** ✅ PASS
- TX backoff doesn't prevent rapid reconnect when RX appears
- RX doesn't require TX to actively probe (TX backoff state is temporary)
- Connection succeeds without timeout

**Pass Criteria:**
- Reconnection completes even with TX backoff active (shows robustness)
- No timeout failures

### Test 2.3: Simultaneous Boot
**Procedure:**
1. Power ON both TX and RX at exactly the same time (within 1 second)
2. Verify: Connection established within 2-4 seconds (no race condition)
3. Verify: Data transmission begins normally

**Expected Result:** ✅ PASS
- No deadlock or race condition
- Discovery works correctly (TX probes, RX responds)
- Connection is stable

**Pass Criteria:**
- Connection succeeds in < 5 seconds
- No error messages in logs related to race conditions

### Test 2.4: Staggered Boot (TX 30s before RX)
**Procedure:**
1. Power ON TX
2. Wait 30 seconds (TX in PERSISTENT_FAILURE after backoff exhausted)
3. Power ON RX
4. Verify: TX doesn't retry forever, gives up gracefully
5. Verify: When RX boots, TX rediscovers within 2-4 seconds

**Expected Result:** ✅ PASS
- TX doesn't retry infinitely (3 backoff attempts, then gives up)
- RX boot triggers TX to attempt reconnection again
- Connection succeeds without user intervention

**Pass Criteria:**
- TX backoff timeout is finite
- Reconnection on RX boot is automatic (no manual reset needed)

---

## Scenario 3: Heartbeat Loss/Delay (5 Cases)

**Objective:** Verify system handles delayed or lost heartbeats correctly (key robustness property).

### Test 3.1: Single Heartbeat Loss (RX → TX)
**Setup:** Connection active, heartbeat cycle 10s, timeout 30s

**Procedure:**
1. System connected and stable
2. Suppress 1 heartbeat ACK (manual firmware modification for testing)
3. Verify: TX doesn't immediately timeout (should wait until 3rd heartbeat loss)
4. Verify: System remains connected and transmitting
5. Resume heartbeats

**Expected Result:** ✅ PASS
- Single heartbeat loss is tolerated
- No state change or disconnect
- System shows ACTIVE/NORMAL_OPERATION

**Pass Criteria:**
- Timeout requires 3+ consecutive heartbeat losses, not 1
- Connection remains stable across single loss

### Test 3.2: Multiple Heartbeat Losses (3 Consecutive)
**Procedure:**
1. System connected and stable
2. Suppress heartbeats for 3 cycles (30 seconds, assuming 10s heartbeat)
3. Verify: TX detects timeout after 3rd loss (~30 seconds)
4. Verify: TX transitions to RECONNECTING, initiates backoff
5. Resume heartbeats

**Expected Result:** ✅ PASS
- Timeout fires after ~30 seconds (matches HEARTBEAT_TIMEOUT_MS)
- TX initiates reconnection with backoff
- Connection re-establishes within 2-4 seconds

**Pass Criteria:**
- Timeout is reliable (fires within configured window)
- Reconnection procedure works after timeout

### Test 3.3: Delayed Heartbeats (2s Delay Each)
**Setup:** Add 2-second delay to heartbeat path (simulates high latency)

**Procedure:**
1. System connected and stable
2. Introduce 2-second delay to all heartbeats (total RTT ~4s vs normal ~100ms)
3. Transmit continuously for 60 seconds
4. Verify: System remains connected despite high latency
5. Remove delay

**Expected Result:** ✅ PASS
- System tolerates increased latency (4s RTT)
- Data transmission continues without dropout
- State remains ACTIVE/NORMAL_OPERATION

**Pass Criteria:**
- Latency increase doesn't trigger false timeout
- Connection is stable under load

### Test 3.4: Burst Packet Loss (1/10 packets dropped)
**Setup:** Simulate 10% random packet loss on ESP-NOW channel

**Procedure:**
1. System connected and stable
2. Enable packet loss simulation (drop 1 of every 10 packets)
3. Transmit continuously for 120 seconds (24 packets)
4. Verify: Expected 2-3 packets lost, system recovers
5. Verify: Remaining 20+ packets received correctly
6. Disable packet loss

**Expected Result:** ✅ PASS
- System tolerates 10% packet loss
- No infinite retries or backoff triggered
- Data continues flowing after loss

**Pass Criteria:**
- Message loss < expected statistical level (2-3 out of 24)
- Connection remains ACTIVE throughout
- No spurious stale/reconnect events

### Test 3.5: Asymmetric Heartbeat Loss (RX → TX Lost, TX → RX OK)
**Setup:** Suppress ACKs in one direction only

**Procedure:**
1. System connected and stable
2. Suppress RX → TX heartbeat ACKs only (TX → RX continues)
3. Verify: TX detects timeout after 30 seconds
4. Verify: RX doesn't notice (still receiving TX messages)
5. Verify: TX initiates reconnection

**Expected Result:** ✅ PASS
- One-way heartbeat loss is detected (TX timeout)
- RX initially unaware of TX problem
- When TX reconnects, full bidirectional communication resumes
- No deadlock or one-way connection state

**Pass Criteria:**
- Asymmetric loss is properly detected by TX
- Reconnection resolves the asymmetry
- No hanging connections

---

## Scenario 4: Stale/Recovery Regressions (3 Cases)

**Objective:** Ensure recent stale-window and grace-window handling doesn't break existing behavior.

### Test 4.1: Stale Detection with Grace Window
**Setup:** TX sends config sync message (triggers grace window)

**Procedure:**
1. System connected, normal message flow
2. TX sends CONFIG_SYNC message (fires `on_config_update_sent()`)
3. Suppress data messages for 95 seconds (past normal 90s timeout, but within grace window)
4. Verify: RX does NOT transition to STALE (grace window extends timeout)
5. Verify: Display continues showing ACTIVE (not WARNING/STALE)
6. Resume data messages

**Expected Result:** ✅ PASS
- Grace window extends stale timeout by configured duration
- RX tolerates extended silence during config updates
- Display state remains stable

**Pass Criteria:**
- Grace window prevents false STALE during expected silence
- Timeout after grace window expires is correct

### Test 4.2: Stale Recovery (No Grace Window)
**Setup:** Normal operation, no grace window applied

**Procedure:**
1. System connected and transmitting
2. Suppress all data/heartbeat messages for 95 seconds
3. Verify: RX transitions ACTIVE → STALE (after 90s)
4. Display shows STALE/WARNING
5. Resume transmission
6. Verify: RX transitions STALE → ACTIVE within 2-4 seconds

**Expected Result:** ✅ PASS
- Stale detection fires reliably after 90 seconds
- Display shows correct warning state
- Recovery is fast and clean (no race conditions)

**Pass Criteria:**
- Stale timeout fires in 90-95 second window
- Recovery to ACTIVE is immediate on data arrival
- No flicker or state oscillation during recovery

### Test 4.3: Grace Window Expiration
**Setup:** Grace window configured to 30 seconds

**Procedure:**
1. System connected, TX sends CONFIG_SYNC
2. Suppress data messages for 120 seconds
3. Verify: STALE transition fires at 90 + 30 = 120 seconds (not earlier)
4. Display shows STALE/WARNING at 120-second mark
5. Resume transmission

**Expected Result:** ✅ PASS
- Grace window extends stale timeout by configured amount
- STALE fires only after grace window expiration
- No false positive STALE during grace period

**Pass Criteria:**
- Stale transition delayed by exactly grace window duration
- Timing is reliable (120s ± 2s acceptable)

---

## Scenario 5: Performance Under Stress (2 Cases)

**Objective:** Verify system performance characteristics under load don't degrade.

### Test 5.1: High Message Rate (10/sec for 60s)
**Setup:** Increase TX message rate from 0.5/sec (normal) to 10/sec

**Procedure:**
1. System connected and stable
2. Set TX to send every 100ms (10 messages/second)
3. Transmit continuously for 60 seconds (target: 600 messages)
4. Monitor: RX receiving rate, dropped packets, latency
5. Verify: All 600 messages received without loss or duplicates
6. Return to normal transmission rate

**Expected Result:** ✅ PASS
- All 600 messages received
- No queue overflows or packet loss
- Latency remains < 100ms per message

**Pass Criteria:**
- Message loss: 0
- Duplication detection: 0 duplicates
- Latency: < 150ms (allowing for processing)

### Test 5.2: Concurrent Ethernet/MQTT Load (TX Only)
**Setup:** TX transmitting on ESP-NOW while MQTT publishing and OTA server running

**Procedure:**
1. TX system:
   - Sending data every 2 seconds via ESP-NOW
   - Publishing MQTT telemetry every 10 seconds
   - HTTP server responsive (OTA endpoint accessible)
2. Transmit continuously for 120 seconds (60 ESP-NOW messages)
3. Monitor: All three interfaces functioning without interference
4. Verify: No message loss or timeout

**Expected Result:** ✅ PASS
- ESP-NOW transmission unaffected by concurrent Ethernet/MQTT
- All 60 messages received correctly
- No timeout or disconnection

**Pass Criteria:**
- Message loss: 0
- Latency stable (no spike during MQTT publish)
- No MQTT errors or HTTP timeouts

---

## Test Execution Procedure

### Pre-Test Checklist
- [ ] Both devices fully charged (or externally powered)
- [ ] Serial console connected for both devices (logging enabled)
- [ ] Test harness ready (power control, packet injection tools if available)
- [ ] Logging configured at DEBUG level to capture all state transitions
- [ ] Phase findings document open for results recording

### During Test Execution
1. **Log Setup:** Record device MAC addresses, firmware versions, test start time
2. **Monitor Live:** Watch both device serial outputs during each test
3. **Capture Failures:** Screenshot/log any anomalies (timeouts, errors, state flicker)
4. **Manual Observation:** Note LED patterns, display states, latency feel
5. **Data Collection:** Record message counts, timing data, resource usage if available

### Post-Test Analysis
1. **Parse Logs:** Extract state transitions, timeouts, error counts
2. **Aggregate Results:** Tally pass/fail for each test case
3. **Identify Gaps:** Note any incomplete tests or partial failures
4. **Document Findings:** Update phase findings with results and observations
5. **Make Recommendations:** Suggest fixes for failures or optimizations for stress cases

---

## Success Definition

**Phase 1D is COMPLETE** when:

✅ **Scenario 1:** All 6 disconnect/reconnect tests PASS  
✅ **Scenario 2:** All 4 startup permutation tests PASS  
✅ **Scenario 3:** All 5 heartbeat loss tests PASS  
✅ **Scenario 4:** All 3 stale/recovery tests PASS  
✅ **Scenario 5:** All 2 performance tests PASS  
✅ **Total:** 20+ test cases, 0 failures, 0 timeouts, 0 data loss  
✅ **Documentation:** All results recorded in phase findings  
✅ **Recommendation:** Clear path to production deployment identified  

---

## Expected Outcomes

### Best Case (Target)
- **All 20 tests PASS** with zero issues
- Reconnection reliability: **100%**
- Data integrity: **100%** (zero loss, zero duplication)
- Recommendation: **READY FOR PRODUCTION**

### Good Case (Acceptable)
- **18/20 tests PASS**, 1-2 minor issues identified
- Issues are documented and low-priority (e.g., optimization opportunity)
- Recommendation: **READY WITH KNOWN LIMITATIONS**

### Poor Case (Requires More Work)
- **< 18/20 tests PASS**, critical issues found
- Reconnection reliability: < 95%
- Data loss or duplication observed
- Recommendation: **NEEDS MORE HARDENING BEFORE PRODUCTION**

---

## Timeline

- **Pre-Test Setup:** 15 minutes
- **Test Execution:** 90-120 minutes (tests can run in parallel where independent)
- **Data Analysis:** 30 minutes
- **Documentation:** 30 minutes
- **Total:** 2.5-3 hours

---

## Next Steps

1. **Review** this specification
2. **Gather** test equipment (devices, serial cables, power supplies)
3. **Configure** logging and monitoring
4. **Execute** tests one scenario at a time
5. **Record** results in [PHASE_1D_TEST_RESULTS.md](#) as you proceed
6. **Update** phase findings with final assessment and recommendation

**Status: READY FOR EXECUTION** ✅

