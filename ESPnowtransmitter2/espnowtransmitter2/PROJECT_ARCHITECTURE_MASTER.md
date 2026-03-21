# ESP-NOW Transmitter: Project Architecture Master Document

**Attribution**: This work is completely based on Dala the Great’s Battery Emulator: https://github.com/dalathegreat/Battery-Emulator
**Scope**: The goal is to split that project into two devices — one for control and one for display — with communication between them. If the display device stops working, it does not interfere with the main control device.

**Version**: 1.4 (Service Integration Results Merged)  
**Date**: March 20, 2026  
**Device**: Olimex ESP32-POE2 (Transmitter)  
**Status**: Active development — OTA hardening complete through Phase D; service integration progress merged

---

## Table of Contents

1. [Project Overview](#project-overview)
2. [Architecture Overview](#architecture-overview)
3. [Core Systems](#core-systems)
4. [Technical References](#technical-references)
5. [Hardware & GPIO Allocation](#hardware--gpio-allocation)
6. [First Release Timeline](#first-release-timeline)
7. [Post-Release Improvements](#post-release-improvements)
8. [Implementation Checklist](#implementation-checklist)
9. [Current Codebase Snapshot (Mar 2026)](#current-codebase-snapshot-mar-2026)

---

## Project Overview

### Mission

Build a **real-time battery monitoring and control system** that transmits CAN bus data from a battery management system (BMS) via wireless ESP-NOW protocol while maintaining Ethernet connectivity for telemetry, NTP time synchronization, and OTA firmware updates.

### Hardware

- **Transmitter**: Olimex ESP32-POE2
  - Wired Ethernet (MII interface with LAN8720 PHY)
  - Wireless ESP-NOW (IEEE 802.15.4 - same radio as WiFi, different protocol)
  - CAN bus interface (for battery data)
  - Built-in PoE (Power over Ethernet) for field deployment
  
- **Receiver**: LilyGo T-Display-S3
  - Wireless ESP-NOW receiver
  - TFT display (status visualization)
  - WiFi connectivity

### Key Features

| Feature | Purpose | Status |
|---------|---------|--------|
| **Real-time CAN data** | Monitor battery SOC, voltage, current, temperature | ✅ Phase 4a |
| **ESP-NOW transmission** | Low-power wireless to receiver | ✅ Section 11 |
| **Cable detection** | Physical Ethernet presence verification | ✅ New |
| **MQTT telemetry** | Cloud reporting (if Ethernet available) | ✅ Phase 3 |
| **OTA firmware updates** | Remote code deployment, anti-brick boot guard + rollback | ✅ Phase D |
| **NTP time sync** | Accurate timestamps via Ethernet | ✅ Phase 2 |
| **Heartbeat protocol** | Connection health monitoring (10s interval) | ✅ Section 11 |
| **State machine control** | Deterministic system behavior | ✅ New |

### Current Codebase Snapshot (Mar 2026)

Primary active modules now align to this structure:

- `src/espnow/tx_state_machine.*` + `src/espnow/tx_connection_handler.*` for transmitter-side ESP-NOW state and transitions.
- `src/network/ethernet_manager.*` for Ethernet lifecycle and readiness gating.
- `src/network/mqtt_manager.*`, `src/network/mqtt_task.*`, `src/network/time_manager.*`, `src/network/ota_manager.*` for network services.
- `src/espnow/component_catalog_handlers.*`, `src/espnow/component_config_sender.*`, and `src/espnow/control_handlers.*` for runtime configuration/control exchange with the receiver.

Receiver companion architecture reference: `../../espnowreceiver_2/PROJECT_ARCHITECTURE_MASTER.md`.

### Architecture Currency Addendum (Verified Mar 20, 2026)

This addendum is the current authoritative map for structure and operation.

1. **ESP-NOW communications and state machines**
  - `src/espnow/tx_state_machine.cpp`
  - `src/espnow/tx_connection_handler.cpp`
  - `src/espnow/discovery_task.cpp`
  - `src/espnow/transmission_task.cpp`
  - `src/espnow/heartbeat_manager.cpp`
  - shared transport/protocol plumbing: `../../esp32common/espnow_common_utils/*`

2. **MQTT communications**
  - `src/network/mqtt_manager.cpp`
  - `src/network/mqtt_task.cpp`
  - shared logging behavior: `../../esp32common/docs/MQTT_LOGGER_IMPLEMENTATION.md`

3. **Inter-device compatibility (versioning, heartbeat, OTA auth)**
  - version/protocol constants: `../../esp32common/firmware_version.h`
  - heartbeat protocol reference: `../../esp32common/docs/ESPNOW_HEARTBEAT.md`
  - OTA auth/session utilities: `../../esp32common/webserver_common_utils/include/webserver_common_utils/ota_auth_utils.h`, `../../esp32common/webserver_common_utils/include/webserver_common_utils/ota_session_utils.h`
  - OTA boot guard (shared): `../../esp32common/runtime_common_utils/include/runtime_common_utils/ota_boot_guard.h`
  - firmware compatibility policy (shared): `../../esp32common/firmware_metadata/firmware_compatibility_policy.h`
  - transmitter OTA runtime/HTTP server: `src/network/ota_manager.cpp`

4. **NTP and timing**
  - shared timing constants: `../../esp32common/config/timing_config.h`
  - transmitter time sync façade: `src/network/time_manager.cpp`
  - Ethernet-driven service lifecycle: `src/network/service_supervisor.cpp`

5. **NVS and persistent settings**
  - system settings persistence: `src/settings/settings_manager.cpp`
  - battery emulator persisted configuration: `src/battery_emulator/communication/nvm/comm_nvm.cpp`

6. **Firmware metadata and `.bin` content identity**
  - metadata structure embedded in `.rodata`: `../../esp32common/firmware_metadata/firmware_metadata.h`
  - pre/post build metadata injection and versioned filenames: `../../esp32common/scripts/version_firmware.py`

7. **Pin layout and HAL**
  - board pin constants: `src/config/hardware_config.h`
  - detailed pin conflict analysis: `CAN_ETHERNET_GPIO_CONFLICT_ANALYSIS.md`
  - battery emulator HAL integration: `src/battery_emulator/devboard/hal/*`

8. **PSRAM/runtime memory**
  - board config: `platformio.ini` (`board = esp32-poe2`, WROVER-E)
  - OTA HTTP path stack hardening to prevent canary faults: `src/network/ota_manager.cpp`

9. **Governance and coding standards**
  - project rules and coding standards: `../../esp32common/docs/project guidlines.md`
  - cross-device OTA guardrail checklist: `../../esp32common/OTA_CROSS_DEVICE_COMPATIBILITY_CHECKLIST.md`

---

## Architecture Overview

### System Diagram

Each physical interface feeds only its own dedicated subsystem.
WiFi → ESP-NOW only. Ethernet → IP networking only. CAN → battery data only.

```
┌──────────────────────────────────────────────────────────────────────┐
│                       ESP32-POE2 (Transmitter)                       │
├──────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  ┌──────────────┐  ┌───────────────────────┐  ┌──────────────────┐  │
│  │   CAN Bus    │  │      WiFi Radio       │  │    Ethernet      │  │
│  │  (HS-SPI /   │  │   *** ESP-NOW ONLY    │  │   (MII /         │  │
│  │   MCP2515)   │  │   No IP · No DHCP     │  │   LAN8720 PHY)   │  │
│  └──────┬───────┘  └──────────┬────────────┘  └────────┬─────────┘  │
│         │                     │                         │            │
│         ▼                     ▼                         ▼            │
│  ┌──────────────┐  ┌───────────────────────┐  ┌──────────────────┐  │
│  │  CANDriver   │  │      RadioInit        │  │ EthernetManager  │  │
│  │  (battery    │  │  STA mode · no IP     │  │  (9-state FSM ·  │  │
│  │  emulator    │  │  ESP-NOW only         │  │  cable/IP/ready) │  │
│  │  parsing)    │  └──────────┬────────────┘  └────────┬─────────┘  │
│  └──────┬───────┘             │                         │            │
│         │                     ▼                         ▼            │
│         │          ┌───────────────────────┐  ┌──────────────────┐  │
│         │          │    ESP-NOW Layer      │  │   IP Services    │  │
│         │          │  tx_connection_       │  │  MQTT · NTP      │  │
│         │          │  handler.cpp /        │  │  OTA · HTTP      │  │
│         │          │  tx_state_machine.cpp │  │  (Ethernet only) │  │
│         │          └──────────┬────────────┘  └────────┬─────────┘  │
│         │                     │                         │            │
│         ▼                     ▼                         ▼            │
│  ┌──────────────────────────────────────────────────────────────┐    │
│  │                       EnhancedCache                          │    │
│  │                (Dual storage · PSRAM-backed)                 │    │
│  └───────────────────────────┬──────────────────────────────────┘    │
│                              │                                       │
│  ┌───────────────────────────▼────────────────────────────────────┐  │
│  │                   FreeRTOS Task Scheduler                      │  │
│  │        Core 0: Main loop / CAN processing / Health checks      │  │
│  │        Core 1: TX / ESP-NOW dispatch                           │  │
│  └────────────────────────────────────────────────────────────────┘  │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘
            │                                           │
            ▼                                           ▼
      [LilyGo Receiver]                           [MQTT Broker]
      (via ESP-NOW · WiFi radio)                   (via Ethernet · IP)
```

> **WiFi** is configured in STA mode with no IP address and is used **exclusively** for ESP-NOW radio.
> **Ethernet** (LAN8720 PHY, MII) is the sole IP networking path for MQTT, NTP, OTA, and HTTP.

### Layered Architecture

```
┌───────────────────────────────────────────────────────────┐
│ Application Layer                                         │
│ ├─ Main loop (CAN processing, health checks)              │
│ ├─ Discovery (channel hopping, peer discovery)            │
│ ├─ Data sender (battery data → cache → wireless)          │
│ └─ Version beacon (periodic status announcements)         │
├───────────────────────────────────────────────────────────┤
│ Service Layer                                             │
│ ├─ NTP Manager (time sync when Ethernet ready)            │
│ ├─ MQTT Manager (telemetry when Ethernet ready)           │
│ ├─ OTA Manager (firmware updates when Ethernet ready)     │
│ ├─ Heartbeat Manager (connection health, Eth + ESP-NOW)   │
│ ├─ Ethernet Manager (9-state machine, cable detection)    │
│ └─ BatteryManager (CAN data processing)                   │
├───────────────────────────────────────────────────────────┤
│ State Machine Layer                                       │
│ ├─ EthernetConnectionState (9 states: cable/IP/ready)     │
│ ├─ EspNowConnectionState (10+ states: discovery/locked)   │
│ └─ FreeRTOS Task Scheduling (priority-based execution)    │
├───────────────────────────────────────────────────────────┤
│ Hardware Interface Layer                                  │
│ ├─ CAN Driver (MCP2515 or built-in, HSPI)                │
│ ├─ Ethernet Driver (LAN8720 PHY, MII)                    │
│ ├─ WiFi Radio (ESP32 built-in, STA mode)                 │
│ └─ NVRAM (NVS for config persistence)                    │
└───────────────────────────────────────────────────────────┘
```

---

## Core Systems

### 1. Ethernet Connectivity (9-State Machine)

**Purpose**: Reliably detect physical cable presence and manage network availability

**States**:
- UNINITIALIZED → PHY_RESET → CONFIG_APPLYING → LINK_ACQUIRING → IP_ACQUIRING → CONNECTED
- Error/Recovery: LINK_LOST → RECOVERING → ERROR_STATE

**Key Feature**: Physical cable detection via `ARDUINO_EVENT_ETH_CONNECTED` event

**Reference**: [ETHERNET_STATE_MACHINE_TECHNICAL_REFERENCE.md](ETHERNET_STATE_MACHINE_TECHNICAL_REFERENCE.md)

**Implementation**: 
- File: `src/network/ethernet_manager.h/cpp`
- Events: Auto-triggered by Arduino event loop
- Update: Called from main loop (every 1 second for timeout detection)

**Usage Example**:
```cpp
// Check if Ethernet fully ready (cable + IP)
if (EthernetManager::instance().is_fully_ready()) {
    // MQTT, OTA, NTP can proceed
}

// Check if just cable present (no IP yet)
if (EthernetManager::instance().is_link_present()) {
    // Waiting for DHCP
}
```

---

### 2. ESP-NOW Wireless Protocol

**Purpose**: Low-power wireless mesh for battery data transmission

**States**: 10+ connection states (discovery, channel locking, connected)

**Architecture**:
- Transmitter (active role): Scans channels, broadcasts PROBE messages
- Receiver (passive role): Listens, responds with ACK
- Discovery: 1s per channel, 13s max (6x faster than previous design)

**Reference**: [TRANSMITTER_STATE_MACHINE_IMPLEMENTATION.md](TRANSMITTER_STATE_MACHINE_IMPLEMENTATION.md)

**Implementation**:
- Files: `src/espnow/tx_connection_handler.cpp`, `src/espnow/tx_state_machine.cpp`, `src/espnow/discovery_task.cpp`
- Transmitter states: 17 (channel locking requires state granularity)
- Receiver states: 10 (passive role, simplified)
- Cache: EnhancedCache (dual storage: transient + state)

**Key Difference**: Transmitter cannot be simplified due to channel locking race conditions

---

### 3. Service Gating (Conditional Activation)

**Purpose**: Only start network services when Ethernet is ready

**Gating Hierarchy**:
1. **Ethernet-Only Services**: NTP, MQTT, OTA (start when CONNECTED state)
2. **Dual-Gated Services**: Heartbeat (requires CONNECTED Ethernet + CONNECTED ESP-NOW)
3. **Always-Active**: CAN processing, data caching, discovery

**Implementation**:
- Ethernet callbacks: `on_connected()` / `on_disconnected()`
- Service checks: `is_fully_ready()` before network operations
- Dual gating: Heartbeat checks both states

**Reference**: [OTA & Service Integration Status](#ota--service-integration-status)

---

### 4. Task Architecture (FreeRTOS)

**Purpose**: Prevent network I/O from blocking real-time CAN processing

**Tasks**:
| Task | Core | Priority | Role |
|------|------|----------|------|
| Main Loop | 0 | N/A | CAN/BMS processing (non-blocking) |
| RX Handler | Auto | Default | ESP-NOW packet reception |
| TX Background | 1 | 2 (LOW) | Transmission from cache |
| Discovery | Auto | Variable | Channel hopping |
| MQTT | Auto | 1 (LOW) | Cloud telemetry |
| NTP | Auto | 1 (LOW) | Time synchronization |

**Key Design**: TX task pinned to Core 1 (no CPU contention with main loop)

**Reference**: [TASK_ARCHITECTURE_AND_SERVICE_ISOLATION.md](TASK_ARCHITECTURE_AND_SERVICE_ISOLATION.md)

---

### 5. Battery Data Flow (Real-Time CAN)

**Purpose**: Acquire battery state from CAN bus, distribute via wireless and cloud

**Flow**:
```
CAN Bus (5 Hz)
    ↓
CANDriver (hardware interface)
    ↓
BatteryManager (update_transmitters)
    ↓
datalayer (global battery state)
    ↓
DataSender (cache reading)
    ↓
EnhancedCache (dual storage)
    ↓
ESP-NOW TX Task (wireless)
    ↓
MQTT Task (if Ethernet ready)
```

**Update Rate**: ~5 Hz from CAN, batched for wireless transmission

**Reference**: Phase 4a documentation (Battery Emulator Integration)

---

## OTA & Service Integration Status

Derived from: `../../esp32common/docs/systemworks/service integration.md` (last updated: March 2026)

### Completed Phases

| Phase | Description | Status |
|-------|-------------|--------|
| A.1 | Shared OTA boot-guard utility (`runtime_common_utils/ota_boot_guard`) added to `esp32common` | ✅ |
| A.2 | Transmitter boot-guard integrated (pending-verify detect + health gate + confirm/rollback) | ✅ |
| A.3 | Receiver boot-guard integrated (same contract as transmitter) | ✅ |
| A.4 | OTA status/health enriched with boot-guard and rollback state fields | ✅ |
| B.1 | Receiver self-OTA endpoint implemented and registered (`/api/ota_upload_receiver`) | ✅ |
| B.2a | OTA transaction ID telemetry (`ota_txn_id`) exposed by both device status APIs | ✅ |
| B.2b | Full two-phase commit state telemetry (`commit_state`, `commit_detail`, post-reboot polling) | ✅ |
| C.1 | OTA PSK loaded from provisioned NVS source (`security/ota_psk`); placeholder default blocked | ✅ |
| C.2a | Manifest image-hash verification (`X-OTA-Image-SHA256`) enforced during OTA stream | ✅ |
| C.3a | Receiver OTA UI preflight compatibility gate (device type + major version checks) | ✅ |
| C.3b | Server-side compatibility enforcement on OTA upload endpoints (both devices) | ✅ |
| NTP.1 | NTP lifecycle moved under `ServiceSupervisor` Ethernet callback ownership | ✅ |
| D.1 | Commit-verification monitor hardened: txn-mismatch rollback detection, `boot_guard_error`, reboot-window progress, timeout diagnostics | ✅ |
| D.2 | Shared `FirmwareCompatibilityPolicy` utility extracted into `esp32common`; consumed by both OTA endpoints | ✅ |

### Open / Remaining Items

| Item | Description | Priority |
|------|-------------|----------|
| 4.1.5 | Replace default OTA PSK path with provisioned secret (verify transmitter config) | Medium |
| 4.3.3 | Add common OTA transaction schema for status and telemetry | Medium |
| C.2b ⏭️ | Detached artifact signature / public-key verification (skipped for now) | Low |
| 4.1.4 ⏭️ | Integration tests for Ethernet flap + MQTT/NTP/OTA behavior (skipped for now) | Low |

### New Shared Utilities in `esp32common`

- **`firmware_metadata/firmware_compatibility_policy.h/.cpp`** — `FirmwareCompatibilityPolicy::MetadataScan` (sliding-window scanner), `validate_scan()` multi-rule policy (device type, major version, `min_compatible_major`). Consumed by `ota_manager.cpp` (transmitter) and `api_control_handlers.cpp` (receiver).
- **`runtime_common_utils/ota_boot_guard.h/.cpp`** — Boot-pending detection, health-gated confirm/rollback. Called from setup paths on both devices.

---

## Hardware & GPIO Allocation

### GPIO Reference Documentation

Use the following current documents for hardware pin allocation and conflicts:
- [CAN_ETHERNET_GPIO_CONFLICT_ANALYSIS.md](CAN_ETHERNET_GPIO_CONFLICT_ANALYSIS.md)
- [ETHERNET_SUMMARY.md](ETHERNET_SUMMARY.md)
- `src/config/hardware_config.h`

**Key Points:**
- **Ethernet**: 10 GPIO pins (0, 12, 18-27) for RMII interface
- **CAN**: 5 GPIO pins (4, 13-15, 32) for SPI + interrupt
- **Contactors**: 4 GPIO pins (33-36) for battery relay control
- **Critical**: GPIO 4 (MISO) chosen to avoid Ethernet conflicts with GPIO 19
- **Power**: GPIO 12 controls LAN8720 PHY power enable
- **Safety**: Contactors fail-safe (spring-open when de-energized)
- **Status**: ✅ No pin conflicts verified across all 18 GPIO allocations

---

## Technical References

### State Machine Documentation

**Ethernet State Machine** - [ETHERNET_STATE_MACHINE_TECHNICAL_REFERENCE.md](ETHERNET_STATE_MACHINE_TECHNICAL_REFERENCE.md)
- 9 states, complete transition table
- Physical cable detection, IP acquisition, recovery
- Timeout values, edge cases, state metrics
- Implementation code examples

**ESP-NOW / Transmitter State Machine** - [TRANSMITTER_STATE_MACHINE_IMPLEMENTATION.md](TRANSMITTER_STATE_MACHINE_IMPLEMENTATION.md)
- Transmitter-side states and transition behavior
- Channel locking and reconnect behavior
- Discovery + ACK/heartbeat interaction

**ESP-NOW Protocol (Shared)** - [../../esp32common/docs/ESP-NOW_Communication_Architecture.md](../../esp32common/docs/ESP-NOW_Communication_Architecture.md)
- Shared packet-level communication architecture
- Inter-device message flow context used by both transmitter and receiver

### Service Integration (Merged)

Service integration guidance and progress are merged into this document.
- Gating patterns (when to start/stop services)
- Callback registration
- Error handling
- Common mistakes to avoid

### Task Architecture

**TASK_ARCHITECTURE_AND_SERVICE_ISOLATION.md** - FreeRTOS task design
- Task isolation from main loop
- Core pinning strategy
- Priority levels
- Inter-task communication patterns

### Implementation Guides

**ETHERNET_STATE_MACHINE_COMPLETE_IMPLEMENTATION.md** - Production-ready code
- Complete event_handler() with cable detection
- State transition logic
- Keep-alive dual gating
- Testing strategy (7 test cases)

---

## First Release Timeline

### Phase Breakdown

**Current Status**: Implementation Phase (Ethernet state machine complete)

| Phase | Scope | Duration | Status |
|-------|-------|----------|--------|
| **P4a** | Battery data acquisition (CAN) | ✅ Complete | Real battery data from datalayer |
| **P4b** | Ethernet state machine | ✅ In Progress | 9-state cable detection |
| **P4c** | Service integration | ✅ In Progress | Callback registration |
| **Testing** | System integration tests | ⏳ Pending | 7 test scenarios |
| **Field Trial** | Deployment validation | ⏳ Pending | Real-world testing |
| **Production** | Release v1.0 | ⏳ Pending | ~4 weeks from now |

### Critical Path

```
Week 1: Ethernet state machine + tests
  ├─ Implement event handlers ✅
  ├─ Add state machine update ✅
  ├─ Add service callbacks ✅
  ├─ Test 7 scenarios (boot, unplug, reconnect, etc) 
  └─ Integration testing
  
Week 2: Service integration
  ├─ ✅ Completed: MQTT gating update
  ├─ ✅ Completed: NTP gating update
  ├─ ✅ Completed: OTA gating update
  ├─ ⏳ Remaining: Test real Ethernet scenarios
  └─ ⏳ Remaining: Regression testing
  
Week 3: Field trial
  ├─ Deploy to test site
  ├─ Monitor for 72 hours
  ├─ Collect logs
  └─ Fix critical issues
  
Week 4: Polish & Release
  ├─ Document final behavior
  ├─ Code review & cleanup
  ├─ Version bump (v1.0)
  └─ Release & deployment
```

---

## Post-Release Improvements

### Roadmap: v1.1 (2-3 months post-release)

After the first production build, prioritize these enhancements:

#### High Priority (Break functionality if missing)

1. **Link Flap Debouncing** (2 days)
   - Problem: Cable flapping causes service restart
   - Solution: 2-second debounce window for LINK_LOST → RECOVERING
   - Benefit: Stable during intermittent connections
   - Reference: [POST_RELEASE_IMPROVEMENTS.md](POST_RELEASE_IMPROVEMENTS.md#link-flap-debouncing)

2. **MQTT Reconnection Logic** ✅ Implemented
   - `MqttManager::update()` gates connection attempts on `EthernetManager::is_fully_ready()`.
   - Retry backoff and reconnect logic implemented; lifecycle start/stop tied to Ethernet callbacks via `ServiceSupervisor`.
   - Reference: `src/network/mqtt_manager.cpp`, `src/network/service_supervisor.cpp`

3. **OTA Rollback** ✅ Implemented
   - Boot guard (`ota_boot_guard`) integrated on both devices; marks app valid only after health gate passes, triggers rollback on failure.
   - Two-phase commit telemetry (`ota_txn_id`, `commit_state`, `commit_detail`) tracks the OTA cycle through reboot and post-boot validation.
   - Commit-verification monitor detects rollback-by-txn-mismatch, `boot_guard_error` state, and reboot-window progress; surfaces detailed timeout diagnostics.
   - Server-side compatibility enforcement (device type + major version) blocks incompatible firmware images at upload time.
   - Reference: `../../esp32common/docs/systemworks/service integration.md` (Phases A–D)

#### Medium Priority (Quality of life)

4. **State Transition Logging** (1 day)
   - Track why state changed (event, timeout, error)
   - Metrics: transition count, average state duration
   - Benefit: Easier debugging

5. **Network Config Web UI** (5 days)
   - Let users configure DHCP vs Static IP via web interface
   - Persist to NVS
   - Current code: Already has NVS save/load, just needs UI

6. **Heartbeat Timeout Recovery** (2 days)
   - Problem: Too many unacked heartbeats triggers disconnect
   - Solution: Adaptive timeout (1-20 heartbeats based on RSSI)
   - Benefit: Better resilience to RF interference

#### Lower Priority (Nice to have)

7. **Dual Ethernet Failover** (7 days)
   - Add WiFi as backup if Ethernet fails
   - Automatic switch, prefer Ethernet when available
   - Benefit: Redundancy

8. **Battery Voltage Prediction** (3 days)
   - Machine learning model for SOC vs voltage
   - Useful for forecasting
   - Data requirement: 2 weeks of historical data

9. **Channel Recommendation** (1 day)
   - Scan for WiFi networks on each channel
   - Suggest channels with least interference
   - Benefit: Faster discovery

### Post-Release Issues to Watch

**Logged during first week**:
```
[TODO] Monitor for:
 □ MQTT disconnect loops (retry backoff)
 □ Heartbeat timeout sensitivity (adjust MAX_UNACKED)
 □ Cable flap behavior (debounce)
 □ OTA update reliability (test on unstable network)
 □ Power-on startup sequence (race conditions)
 □ Memory leaks (PSRAM usage over 48h)
```

---

## Implementation Checklist

### Pre-Release (This Week)

- [x] Ethernet 9-state machine header
- [x] Ethernet state machine implementation
- [x] Physical cable detection (CONNECTED/DISCONNECTED events)
- [x] Event handler with state transitions
- [x] Main loop update() call
- [x] Service callbacks registration
- [x] Heartbeat dual gating (Ethernet + ESP-NOW)
- [ ] Test boot without cable
- [ ] Test unplug cable after running
- [ ] Test reconnect cable
- [ ] Test DHCP slow (>10s)
- [ ] Test static IP wrong
- [ ] Test cable flapping (intermittent)
- [ ] Test recovery after disconnect

### Code Quality

- [ ] Remove all legacy code references
- [ ] Update all includes/dependencies
- [ ] Compile without warnings
- [ ] Code review (architecture)
- [ ] Code review (implementation)
- [ ] Comment any complex sections

### Documentation

- [ ] Update README with state machine info
- [ ] Document known limitations
- [ ] Document state machine metrics (debugging)
- [ ] Create troubleshooting guide

### Testing

- [x] Unit tests (state transitions)
- [ ] Integration tests (with MQTT, NTP, OTA)
- [ ] System tests (full boot sequence)
- [ ] Stress tests (24h continuous)
- [ ] Power-up recovery tests

---

## Quick Reference

### State Machine States

**Ethernet**: UNINITIALIZED → PHY_RESET → CONFIG_APPLYING → LINK_ACQUIRING → IP_ACQUIRING → CONNECTED
- **Error Path**: → LINK_LOST → RECOVERING → ERROR_STATE

**Service Gating**: Use `is_fully_ready()` to check CONNECTED state

### Key Classes

| Class | File | Purpose |
|-------|------|---------|
| `EthernetManager` | `network/ethernet_manager.h` | 9-state machine |
| `EthernetConnectionState` | `network/ethernet_manager.h` | Enum of 9 states |
| `EspNowConnectionManager` | (external) | 10-17 state machine |
| `HeartbeatManager` | `espnow/heartbeat_manager.cpp` | 10s heartbeat, dual gating |

### Configuration Files

| File | Purpose |
|------|---------|
| `config/hardware_config.h` | GPIO pins, PHY address |
| `config/network_config.h` | MQTT broker, NTP server |
| `config/logging_config.h` | Log levels |
| `config/task_config.h` | Task priorities, stack sizes |

### Build Command

```bash
pio run -e olimex_esp32_poe2  # Compile
pio run -e olimex_esp32_poe2 -t upload -t monitor  # Compile + Flash + Monitor
```

---

## Document Cross-References

This master document references the following technical documents. Start with the architectural overview, then dive into details as needed:

1. **PROJECT_ARCHITECTURE_MASTER.md** (this file) - High-level overview
2. **ETHERNET_STATE_MACHINE_TECHNICAL_REFERENCE.md** - State machine details
3. **TRANSMITTER_STATE_MACHINE_IMPLEMENTATION.md** - Wireless protocol states
4. **TASK_ARCHITECTURE_AND_SERVICE_ISOLATION.md** - FreeRTOS task design
5. **ETHERNET_STATE_MACHINE_COMPLETE_IMPLEMENTATION.md** - Production code
6. **POST_RELEASE_IMPROVEMENTS.md** - Future enhancements
7. **ETHERNET_TIMING_ANALYSIS.md** - Historical debugging notes
8. **STATE_MACHINE_ARCHITECTURE_ANALYSIS.md** - Architecture decision log
9. **../../esp32common/docs/ESP-NOW_Communication_Architecture.md** - Shared ESP-NOW protocol architecture
10. **../../esp32common/docs/MQTT_LOGGER_IMPLEMENTATION.md** - Shared MQTT logging integration details
11. **../../espnowreceiver_2/PROJECT_ARCHITECTURE_MASTER.md** - Receiver architecture master document
12. **../../esp32common/docs/project guidlines.md** - Cross-project coding and architecture rules
13. **../../esp32common/docs/systemworks/service integration.md** - MQTT/NTP/OTA service integration investigation and live phase progress tracking

---

## Support & Questions

**For implementation questions**: See [OTA & Service Integration Status](#ota--service-integration-status)  
**For state machine details**: See ETHERNET_STATE_MACHINE_TECHNICAL_REFERENCE.md  
**For debugging**: See TASK_ARCHITECTURE_AND_SERVICE_ISOLATION.md  
**For future work**: See POST_RELEASE_IMPROVEMENTS.md

---

**Status**: ✅ OTA hardening complete through Phase D; service integration results merged (v1.4)  
**Next Step**: Complete remaining open items (4.1.5 OTA PSK config, 4.3.3 transaction schema), then run 7 integration test scenarios for final release sign-off

