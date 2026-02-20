# ESP-NOW Transmitter: Project Architecture Master Document

**Attribution**: This work is completely based on Dala the Great’s Battery Emulator: https://github.com/dalathegreat/Battery-Emulator
**Scope**: The goal is to split that project into two devices — one for control and one for display — with communication between them. If the display device stops working, it does not interfere with the main control device.

**Version**: 1.0 Release Candidate  
**Date**: February 19, 2026  
**Device**: Olimex ESP32-POE-ISO (Transmitter)  
**Status**: Ready for First Production Build

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

---

## Project Overview

### Mission

Build a **real-time battery monitoring and control system** that transmits CAN bus data from a battery management system (BMS) via wireless ESP-NOW protocol while maintaining Ethernet connectivity for telemetry, NTP time synchronization, and OTA firmware updates.

### Hardware

- **Transmitter**: Olimex ESP32-POE-ISO
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
| **OTA firmware updates** | Remote code deployment via Ethernet | ✅ Phase 3 |
| **NTP time sync** | Accurate timestamps via Ethernet | ✅ Phase 2 |
| **Heartbeat protocol** | Connection health monitoring (10s interval) | ✅ Section 11 |
| **State machine control** | Deterministic system behavior | ✅ New |

---

## Architecture Overview

### System Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                    ESP32-POE-ISO (Transmitter)             │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐     │
│  │    CAN Bus   │  │   WiFi/BLE   │  │  Ethernet    │     │
│  │   (HS-SPI)   │  │   (2.4 GHz)  │  │   (MII)      │     │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘     │
│         │                 │                  │              │
│         │                 │      ┌───────────┴────┐         │
│         │                 │      │                │         │
│  ┌──────▼───────┐  ┌──────▼─────▼──────┐  ┌──────▼────┐    │
│  │  CANDriver   │  │  EthernetManager  │  │ RadioInit │    │
│  │  (Battery)   │  │  (9-State Machine)│  │(WiFi STA) │    │
│  └──────┬───────┘  └──────┬────────────┘  └──────┬─────┘    │
│         │                 │                      │          │
│         │    ┌────────────┼──────────────┐       │          │
│         │    │            │              │       │          │
│  ┌──────▼────▼────┐  ┌────▼──────┐  ┌───▼───┐   │          │
│  │EnhancedCache   │  │ Services  │  │Tasks  │   │          │
│  │(Dual Storage)  │  │ (NTP/OTA) │  │(MQTT) │   │          │
│  └────────┬───────┘  └────┬──────┘  └───┬───┘   │          │
│           │               │              │       │          │
│  ┌────────▼───────────────▼──────────────▼───┐   │          │
│  │      FreeRTOS Task Scheduler              │   │          │
│  │  (Core 0: Main Loop / Core 1: TX)         │   │          │
│  └────────┬────────────────────────────────┬─┘   │          │
│           │                                │     │          │
│  ┌────────▼──────┐           ┌─────────────▼──┐  │          │
│  │ ESP-NOW Radio │           │ CAN Interface  │  │          │
│  └────────┬──────┘           └─────────────┬──┘  │          │
│           │                                │     │          │
│  ┌────────▼──────────────────────────────┘     │          │
│  │              Wireless/CAN Outputs          │          │
│  └─────────────────────────────────────────────┘          │
│                                                             │
└─────────────────────────────────────────────────────────────┘
         │                                    │
         ▼                                    ▼
    [LilyGo Receiver]                    [MQTT Broker]
    (Display)                            (Cloud)
```

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

**Reference**: [ESPNOW_CONNECTION_STATE_MACHINE_TECHNICAL_REFERENCE.md](ESPNOW_CONNECTION_STATE_MACHINE_TECHNICAL_REFERENCE.md)

**Implementation**:
- Files: `src/espnow/connection_manager.cpp`, `src/espnow/discovery_task.cpp`
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

**Reference**: [SERVICE_INTEGRATION_GUIDE.md](SERVICE_INTEGRATION_GUIDE.md)

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

## Hardware & GPIO Allocation

### GPIO Reference Documentation

**TRANSMITTER_GPIO_ALLOCATION.md** - Complete GPIO pin mapping
- Quick reference table with all 18 allocated GPIO pins
- Detailed Ethernet RMII interface (10 pins to LAN8720)
- CAN SPI interface (5 pins to MCP2515 controller)
- Contactor/Relay control (4 pins for battery contactors)
- GPIO conflict resolution (why GPIO 4 is used for MISO instead of GPIO 19)
- Initialization sequence (Ethernet → CAN → Contactors)
- Power management (ETH_POWER_PIN control)
- Precharge sequence for safe battery connection
- Available GPIO for future expansion
- SPI bus sharing analysis
- Design constraints & lessons learned

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

**ESP-NOW State Machine** - [ESPNOW_CONNECTION_STATE_MACHINE_TECHNICAL_REFERENCE.md](ESPNOW_CONNECTION_STATE_MACHINE_TECHNICAL_REFERENCE.md)
- Transmitter: 17 states (why not simplified)
- Receiver: 10 states (simplified by design)
- Channel locking mechanism
- Discovery algorithm (1s per channel)
- ACK/heartbeat protocol

### Service Integration Guide

**SERVICE_INTEGRATION_GUIDE.md** - How to use state machines with services
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
  ├─ Update MQTT gating
  ├─ Update NTP gating
  ├─ Update OTA gating
  ├─ Test real Ethernet scenarios
  └─ Regression testing
  
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

2. **MQTT Reconnection Logic** (3 days)
   - Problem: MQTT doesn't auto-reconnect on disconnect
   - Solution: Retry backoff (exponential) with max attempts
   - Benefit: Reliable cloud reporting
   - Reference: [POST_RELEASE_IMPROVEMENTS.md](POST_RELEASE_IMPROVEMENTS.md#mqtt-reconnection)

3. **OTA Rollback** (2 days)
   - Problem: Failed OTA leaves device bricked
   - Solution: Validate firmware signature, rollback on CRC error
   - Benefit: Safer remote updates
   - Reference: [POST_RELEASE_IMPROVEMENTS.md](POST_RELEASE_IMPROVEMENTS.md#ota-rollback)

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
pio run -e esp32_poe_iso  # Compile
pio run -e esp32_poe_iso --upload  # Compile + Flash
pio device monitor  # Serial monitor
```

---

## Document Cross-References

This master document references the following technical documents. Start with the architectural overview, then dive into details as needed:

1. **PROJECT_ARCHITECTURE_MASTER.md** (this file) - High-level overview
2. **ETHERNET_STATE_MACHINE_TECHNICAL_REFERENCE.md** - State machine details
3. **ESPNOW_CONNECTION_STATE_MACHINE_TECHNICAL_REFERENCE.md** - Wireless protocol states
4. **SERVICE_INTEGRATION_GUIDE.md** - How to use state machines
5. **TASK_ARCHITECTURE_AND_SERVICE_ISOLATION.md** - FreeRTOS task design
6. **ETHERNET_STATE_MACHINE_COMPLETE_IMPLEMENTATION.md** - Production code
7. **POST_RELEASE_IMPROVEMENTS.md** - Future enhancements
8. **ETHERNET_TIMING_ANALYSIS.md** - Historical debugging notes
9. **STATE_MACHINE_ARCHITECTURE_ANALYSIS.md** - Architecture decision log

---

## Support & Questions

**For implementation questions**: See SERVICE_INTEGRATION_GUIDE.md  
**For state machine details**: See ETHERNET_STATE_MACHINE_TECHNICAL_REFERENCE.md  
**For debugging**: See TASK_ARCHITECTURE_AND_SERVICE_ISOLATION.md  
**For future work**: See POST_RELEASE_IMPROVEMENTS.md

---

**Status**: ✅ Ready for First Production Release  
**Next Step**: Run 7 test scenarios, then proceed to integration testing

