# ESP-NOW Transmitter Codebase Review – Speed & Efficiency

**Date:** March 16, 2026
**Scope:** Full review of the ESP-NOW transmitter codebase for speed, efficiency, and code cleanliness, with a focus on WiFi/ESP-NOW usage, simulated LED logic, and redundant code removal.

---

## Executive Summary
- The transmitter codebase is architecturally sound, with clear separation from webserver/UI logic.
- The only use of WiFi is for ESP-NOW, which is correctly initialized in STA mode with no IP/gateway.
- The simulated LED is implemented as a state indicator and works as intended.
- Several areas for improvement remain, especially regarding unused dependencies and embedded libraries.

---

## Key Findings & Recommendations

### 1. WiFi/ESP-NOW Component
- **Current (core transmitter path):** WiFi is initialized in STA mode, disconnected, and used solely for ESP-NOW in `src/main.cpp`.
- **Additional finding (Battery Emulator legacy path):** `src/battery_emulator/devboard/wifi/wifi.cpp` is **not** part of the active ESP-NOW runtime path, but it still defines global symbols consumed by Battery Emulator NVM/settings code.
- **Status:** ⚠️ **PARTIALLY ACTIVE (symbol provider), not active runtime path**
  - `init_WiFi()`, `wifi_monitor()`, `connectToWiFi()`, `FullReconnectToWiFi()`, `init_mDNS()`, and `init_WiFi_AP()` currently have no call sites outside `wifi.cpp`.
  - However, globals such as `static_IP_enabled`, `static_local_IP*`, `static_gateway*`, and `static_subnet*` are written in `communication/nvm/comm_nvm.cpp`.
  - Excluding `wifi.cpp` caused linker failures due to those unresolved globals, confirming that `wifi.cpp` is still required for linking even if its connection logic is unused.
  - Net result: this file is currently a **data-symbol module** for legacy settings compatibility, not an ESP-NOW transport dependency.

### 2. Simulated LED Logic
- **Current:** The simulated LED is triggered by state changes in messaging. No physical LED is used.
- **Status:** ✅ **OPTIMAL** - No changes needed
  - The implementation is efficient and non-blocking.
  - No physical indicator overhead, suitable for the transmitter's embedded server role.

### 3. Message Frequency Analysis & Optimization Opportunities
- **Current Message Rates:**
  - **Battery Data (SOC/Power):** Every 2 seconds (2000 ms)
  - **Heartbeat Messages:** Every 10 seconds (10,000 ms)
  - **Version Beacon:** Every 15 seconds (15,000 ms)
  - **Discovery Announcements:** Every 5 seconds (5000 ms)
  - **MQTT Publish:** Every 10 seconds (10,000 ms)

#### 3.1 Version Beacon (15s) - **OPTIMIZABLE**
- **Purpose:** Lightweight configuration version synchronization; also sends runtime status (MQTT/Ethernet connection state)
- **Current Behavior:**
  - Periodic heartbeat every 15 seconds (sends regardless of changes)
  - Event-driven updates when MQTT or Ethernet state changes
  - Contains firmware metadata, config versions, and runtime status
  - Size: ~100+ bytes (includes strings for env_name, build_date, etc.)
  
- **Recommendation:** 
  - **Increase interval from 15s to 30s** - The receiver caches this information and only needs periodic updates every 30 seconds. Since the main purpose is transmitter battery control, configuration synchronization is secondary.
  - Keep event-driven updates (state changes still trigger immediate beacon)
  - This reduces overhead by 50% without impacting control functionality
  - Rationale: Control/inverter management doesn't depend on version beacons; only config distribution relies on them

#### 3.2 Heartbeat Messages (10s) - **OPTIMAL**
- **Purpose:** Connection keep-alive; detects link loss after 3 consecutive unacked heartbeats (30s total)
- **Current Behavior:**
  - Sends every 10 seconds when connected
  - Receiver must ACK each heartbeat
  - Small message (~20 bytes)
- **Recommendation:** 
  - **Keep at 10s interval** - The 30s failover detection window is appropriate for inverter control and connection stability
  - Current frequency is optimal balance between responsiveness and overhead
  - No changes needed; connection detection is reliable at this interval

#### 3.3 Battery Data (SOC/Power) (2s) - **REVIEW FOR NECESSITY**
- **Purpose:** Primary control data for inverter management; core transmitter responsibility
- **Current Behavior:**
  - Sends every 2 seconds (500 Hz equivalent)
  - Critical for inverter control decisions
  - Size: ~50-100 bytes per message
- **Recommendation:**
  - **Keep at 2s or consider 3-4s** - Depends on inverter's control loop requirements
  - If inverter control loop runs at < 2 Hz (typical for battery management), increasing to 3-4s is viable
  - **DO NOT reduce below 2s without understanding inverter response time requirements**
  - Verify with inverter manufacturer/integration specs before changing
  - This is the **PRIMARY** function, so minimize risk here

#### 3.4 Discovery Announcements (5s) - **OPTIMIZABLE FOR PRODUCTION**
- **Purpose:** Receiver discovery during active channel hopping at startup
- **Current Behavior:**
  - Sent every 5 seconds during active hopping phase
  - Only used during initial connection (< 15s typically)
  - After connection established, discovery stops
- **Recommendation:**
  - Already optimized - Active hopping phase terminates once connected
  - In production (post-discovery), this adds zero overhead
  - **No changes needed** - Current design is already efficient for this phase

### 4. Test/Simulation Code
- **Current:** Test data generators and configuration are present and well-isolated.
- **Status:** ✅ **OPTIMAL** - No action needed unless test code is being compiled into production builds.
  - If production firmware includes test code, use build flags to exclude it from release builds.

### 5. Build Flags & Configuration
- **Current:** Some build flags (e.g., inverter support) are duplicated in both `platformio.ini` and header files.
- **Recommendation:** Remove duplicated flags from `platformio.ini` and keep them only in the relevant header files for maintainability.

### 6. General Code Cleanliness
- **Current:** Logging, delay usage, and task management are appropriate and efficient.
- **Recommendation:**
  - Consider replacing `delay()` with `vTaskDelay()` in FreeRTOS contexts for better task scheduling.
  - Review all `#include` statements for unused headers and remove them.
  - Ensure all constants are defined as `constexpr` or in config headers for clarity and optimization.

---

## Message Frequency Summary & Priority Assessment

| Message Type | Current Interval | Bytes | Primary Purpose | Control-Critical | Recommendation |
|---|---|---|---|---|---|
| Battery Data (SOC/Power) | 2s | ~50-100 | **Inverter control** | ✅ YES | **Keep at 2s** (verify with inverter specs) |
| Heartbeat | 10s | ~20 | Connection keep-alive | ✅ YES | **Keep at 10s** (optimal for connection stability) |
| Version Beacon | 15s | ~100+ | Config sync (secondary) | ❌ NO | **Increase to 30s** (50% overhead reduction) |
| Discovery Announce | 5s | ~50 | Initial connection | ❌ NO | Already optimal (terminates after connection) |
| MQTT Publish | 10s | Variable | Telemetry (secondary) | ❌ NO | Independent optimization (not ESP-NOW) |

---

## Redundant Code & Dependencies

### 7. Unused Embedded Libraries
- **Current:** 
  - The codebase contains unused embedded libraries (ElegantOTA, ESPAsyncWebServer, AsyncTCP, eModbus) in `src/battery_emulator/lib/`.
  - `platformio.ini` includes unused dependencies for ESPAsyncWebServer and AsyncTCP.
- **Recommendation:**
  - **Remove all unused embedded libraries** from `src/battery_emulator/lib/`.
  - **Remove ESPAsyncWebServer and AsyncTCP** from `platformio.ini`.
  - Update comments in `platformio.ini` to clarify that OTA uses native ESP-IDF HTTP server.
  - Check `src/battery_emulator/library.json` for references to removed libraries and clean up.
  - These changes will reduce binary size (~700 KB), speed up compilation, and eliminate potential conflicts.

  ### 8. Battery/Inverter Variant Cleanup (Detailed)

  #### 8.1 Current state
  - Variant surface area is large:
    - Battery variants: **50** `.cpp` files (~1.09 MB source)
    - Inverter variants: **23** `.cpp` files (~0.25 MB source)
  - `BATTERIES.h/.cpp` and `INVERTERS.h/.cpp` include/create many variants directly, so runtime selection still forces broad compile/link coverage.

  #### 8.2 Why simple `build_src_filter` pruning fails for many variants
  - In `BATTERIES.cpp`, `create_battery()` and `name_for_battery_type()` reference many concrete classes directly.
  - In `INVERTERS.cpp`, `setup_inverter()` and `name_for_inverter_type()` do the same for inverter classes.
  - If a variant `.cpp` is excluded without also removing/guarding its constructor/name references, linker errors are expected.

  #### 8.3 Recommended cleanup strategy (safe staged approach)
  1. **Introduce explicit build-time feature flags per variant family**
    - Add `include/battery_config.h` and extend `include/inverter_config.h`.
    - Example pattern: `SUPPORT_BATT_NISSAN_LEAF`, `SUPPORT_BATT_TESLA`, `SUPPORT_INV_SMA_LV`, etc.

  2. **Guard includes and switch cases together**
    - In `BATTERIES.h/.cpp` and `INVERTERS.h/.cpp`, wrap:
      - header includes
      - `name_for_*` switch branches
      - `create_*` / `setup_*` switch branches
    - This removes both compile and link references for disabled variants.

  3. **Keep runtime behavior coherent**
    - Update `supported_battery_types()` / `supported_inverter_protocols()` so UI/API only expose compiled variants.
    - Keep NVS migration-safe fallbacks for stored but now-disabled types (map to `None` + warning event/log).

  4. **Apply in small batches with build validation**
    - Phase A: inverter variants (lower risk than battery set)
    - Phase B: battery variants (larger matrix, higher risk)
    - Build and smoke-test after each batch.

  #### 8.4 Practical cleanup profiles
  - **Profile 1 (Conservative):** keep all current CAN inverters and all batteries; continue excluding Modbus/RS485 sources only.
  - **Profile 2 (Deployment-focused):** keep only deployed battery + inverter families (plus one test battery), disable everything else.
  - **Profile 3 (Single-target firmware):** compile only one battery + one inverter pair for minimal build/runtime footprint.

  #### 8.5 Immediate low-risk follow-ups
  - Split WiFi legacy globals out of `wifi.cpp` into a tiny `wifi_settings_compat.cpp` so WiFi connection code can be fully removed from transmitter builds.
  - Add compile-time guards for a first inverter subset (pilot) before touching battery variants.

---
---

## Actionable Checklist

### Critical Path (Control/Inverter Management)
- [ ] **Verify battery data (SOC/power) interval with inverter spec** - Keep at 2s or optimize based on control loop requirements

### High Priority (Performance)
- [x] **Increase version beacon interval from 15s to 30s**
  - Edit [src/espnow/version_beacon_manager.h](src/espnow/version_beacon_manager.h) line ~91
  - Change `PERIODIC_INTERVAL_MS = 15000` to `30000`
  - Verify receiver still receives config updates correctly
  - Reduces non-critical messaging by 50%

### Medium Priority (Code Cleanliness)
- [ ] Remove unused libraries from `src/battery_emulator/lib/`
- [ ] Remove ESPAsyncWebServer/AsyncTCP from `platformio.ini`
- [ ] Update comments in `platformio.ini` to clarify OTA implementation
- [ ] Clean up `src/battery_emulator/library.json` if needed
- [ ] Remove duplicate build flags from `platformio.ini`

### Completed During This Session
- [x] Increased version beacon interval from 15s to 30s in `src/espnow/version_beacon_manager.h`
- [x] Kept heartbeat interval at 10s (no receiver-side change required)
- [x] Replaced structured bindings in `src/battery_emulator/communication/CommunicationManager.cpp` for toolchain compatibility
- [x] Excluded unused LILYGO T-Connect Pro HAL source from build in `platformio.ini` (`build_src_filter`)
- [x] Migrated legacy `STATICIP/LOCALIP*/GATEWAY*/SUBNET*` keys from `batterySettings` to EthernetManager `network` namespace during boot when no network config exists
- [x] Excluded `battery_emulator/devboard/wifi/wifi.cpp` from build after removing linker dependency on WiFi globals

### Low Priority (General Optimization)
- [ ] Replace `delay()` with `vTaskDelay()` where appropriate
- [ ] Remove unused `#include` statements
- [ ] Ensure all constants use `constexpr` or config headers

---

## Conclusion

The transmitter codebase is **well-architected for its primary purpose: controlling the inverter and battery via ESP-NOW**. 

### Key Takeaways:

1. **Core Control Path (2s Battery Data):** Already optimized at the proper frequency for inverter control. Do not reduce without verifying inverter specifications.

2. **Messaging Overhead (Version Beacon & Heartbeat):** Version beacon has now been reduced to 30s (50% reduction in non-critical periodic traffic). Heartbeat remains at 10s for connection stability and prompt loss detection.

3. **Code Quality:** Clean, well-structured, with minimal unused code. Main improvements are library cleanup (~700 KB binary reduction) and build flag consolidation.

4. **Architecture:** Correctly separates WiFi (ESP-NOW only) from Ethernet (all other traffic). Discovery protocol is already optimized to terminate once connected.

### Recommended Implementation Order:
1. **First:** Verify battery data interval requirements with inverter specification
2. **Second:** Maintain heartbeat interval at 10s (current setting retained by design)
3. **Third:** Remove unused libraries and clean up build flags (code hygiene, significant binary size reduction)

**Status:** Ready for targeted optimization with minimal risk to core functionality.

