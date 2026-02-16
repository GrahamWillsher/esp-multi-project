# Static Data Cache Architecture - Comprehensive Review

**Date**: February 13, 2026  
**Status**: Analysis Complete - Recommendations Provided  
**Version**: 1.0  
**Scope**: Static Configuration & Runtime Status ONLY (Real-time telemetry in Appendix A)

---

## Executive Summary

### Current State
The project implements **THREE SEPARATE** caching systems for static data, leading to confusion, duplication, and the MQTT config issue you encountered:

1. **Config Sync System** (Full Snapshot + Delta) - Legacy, fragmented
2. **Version Beacon System** (Phase 4) - Partially implemented  
3. **Individual Request/ACK** (MQTT, Network) - Ad-hoc, inconsistent

### Critical Finding
**The systems don't integrate properly.** When the receiver's web page loads, it queries the TransmitterManager cache (System #2/#3), but initialization uses ReceiverConfigManager (System #1), causing cache misses and "config not cached" errors.

### Recommendation
**Consolidate to a single Version Beacon-based cache system** for all static data communication. This provides:
- ✅ Automatic synchronization
- ✅ Minimal bandwidth (20 bytes every 15s)
- ✅ Instant updates when config changes
- ✅ Single source of truth
- ✅ No web page request/wait logic needed

---

## Important Distinctions

### Static Config vs Runtime Status vs Real-Time Data

This document addresses **three distinct data types** with different architectural needs:

#### 1. Static Configuration Data
**Characteristics**:
- Changes infrequently (manual user configuration)
- Small payload per section (20-200 bytes)
- Examples: MQTT server IP/port/credentials, static IP settings, battery capacity limits
- **Frequency**: Changes once per user action (minutes/hours/days between updates)

**Transmission Strategy**: ✅ Version Beacon System (this document's focus)

#### 2. Runtime Status Data
**Characteristics**:
- Changes frequently based on system state
- Very small payload (boolean flags, single values)
- Examples: MQTT connected (true/false), Ethernet connected, current IP address
- **Frequency**: Changes on connection events (seconds/minutes between updates)
- **Critical**: May change AFTER static config is sent but BEFORE beacon

**Transmission Strategy**: ✅ Included in Version Beacon (updated every 15s automatically)

#### 3. Real-Time Telemetry Data
**Characteristics**:
- Continuously changing sensor/measurement data
- Large payload (96 cells × 2-4 bytes = 192-384 bytes per reading)
- Examples: Battery cell voltages, temperatures, current, SOC, power
- **Frequency**: 1-10 Hz updates (100ms-1s between readings)
- **Volume**: High - could be 10-50 KB/minute

**Transmission Strategy**: ❌ **DO NOT USE VERSION BEACON CACHE SYSTEM**  
✅ Use separate streaming system (see Real-Time Data Architecture section below)

---

## CRITICAL: Receiver Has TWO Data Sources

### The Receiver Manages Both:

**1. Transmitter's Static Data (Cached from ESP-NOW)**
- MQTT config from transmitter
- Network config from transmitter  
- Battery settings from transmitter
- Transmitter metadata (TX MAC, TX firmware version, TX device name)
- Transmitter runtime status (MQTT connected, Ethernet connected)

**Storage**: ✅ NVS-backed cache (write-through pattern)
**Source**: Version beacons + section requests from transmitter
**Purpose**: Display transmitter's configuration on receiver's web UI

**2. Receiver's Own Static Data (Local Configuration)** ⚠️ **EQUALLY IMPORTANT**
- **Receiver's IP address** (static or DHCP-assigned)
- **Receiver's MAC address**
- **Receiver's firmware version** (major.minor.patch)
- **Receiver's device name** (e.g., "Battery Monitor 1")
- **Receiver's network settings** (if different from transmitter)
- **Receiver's web server settings** (port, auth credentials)
- **Receiver's display preferences** (theme, units, timezone)

**Storage**: ✅ NVS-backed cache (SAME architecture as transmitter data)
**Source**: Local configuration (user settings, system detection)
**Purpose**: Display receiver's own identity and configuration on web UI

### Architecture Principle: Symmetric Cache Management

Both transmitter data AND receiver data use the **SAME cache/NVS architecture**:

```cpp
// RECEIVER SIDE - TWO SEPARATE CACHES

// Cache #1: Transmitter's data (received via ESP-NOW)
class TransmitterManager {
    // Transmitter's MQTT config
    mqtt_config_t tx_mqtt_config;
    uint32_t tx_mqtt_config_version;
    
    // Transmitter's metadata
    uint8_t tx_mac_address[6];
    char tx_firmware_version[16];
    char tx_device_name[32];
    
    // Load/save transmitter data to NVS namespace "tx_cache"
    void loadFromNVS();
    void saveToNVS();
};

// Cache #2: Receiver's own data (local configuration)
class ReceiverConfigManager {
    // Receiver's own network config
    char rx_ip_address[16];          // "192.168.1.50"
    uint8_t rx_mac_address[6];       // Receiver's MAC
    char rx_firmware_version[16];    // "2.1.0"
    char rx_device_name[32];         // "Battery Monitor 1"
    
    // Web server settings
    uint16_t webserver_port;         // 80
    bool auth_required;              // true
    
    // Display preferences
    char timezone[32];               // "America/New_York"
    bool use_fahrenheit;             // false
    
    // Load/save receiver data to NVS namespace "rx_config"
    void loadFromNVS();
    void saveToNVS();
};
```

### Web UI Shows BOTH Data Sources

```html
<!-- Receiver's Web Page Displays BOTH -->

<!-- Section 1: Receiver Information (Local Data) -->
<div class="device-info">
  <h2>Receiver Status</h2>
  <p>Device Name: Battery Monitor 1</p>
  <p>IP Address: 192.168.1.50</p>
  <p>MAC Address: AA:BB:CC:DD:EE:FF</p>
  <p>Firmware: v2.1.0</p>
  <p>Uptime: 3 days 14 hours</p>
</div>

<!-- Section 2: Transmitter Information (Cached Data) -->
<div class="transmitter-info">
  <h2>Transmitter Status</h2>
  <p>Device Name: Battery TX 1</p>
  <p>MAC Address: 11:22:33:44:55:66</p>
  <p>Firmware: v2.0.0</p>
  <p>Connection: ✅ Connected (beacon 5s ago)</p>
  <p>MQTT: ✅ Connected to 192.168.1.221:1883</p>
  <p>Ethernet: ✅ Connected (192.168.1.100)</p>
</div>
```

### Implementation Impact

**Phase 1 Tasks Must Include**:
1. ✅ TransmitterManager for caching TX data (already exists)
2. ⚠️ **ReceiverConfigManager for receiver's own data** (needs enhancement)
3. ⚠️ **Separate NVS namespaces**: 
   - `"tx_cache"` for transmitter data
   - `"rx_config"` for receiver configuration
4. ⚠️ **Web UI endpoints for BOTH**:
   - `/api/get_receiver_info` → ReceiverConfigManager
   - `/api/get_transmitter_info` → TransmitterManager

**Key Principle**: The receiver is NOT just a passive display - it's a device with its own identity, configuration, and settings that need the same careful management as the transmitter's cached data.

---

## Current Architecture Analysis

### System 1: Config Sync (CONFIG_SYNC_IMPLEMENTATION_COMPLETE.md)

**Location**: `esp32common/config_sync/`  
**Status**: ❌ **FRAGMENTED & INCOMPLETE**

**Architecture**:
```
Transmitter                          Receiver
┌──────────────┐                    ┌──────────────┐
│ ConfigManager│                    │ReceiverConfig│
│ (NVS backed) │                    │    Manager   │
└──────┬───────┘                    └──────┬───────┘
       │                                    │
       │ 1. CONFIG_REQUEST_FULL             │
       │◄───────────────────────────────────┤
       │                                    │
       │ 2. CONFIG_SNAPSHOT (fragments)     │
       ├────────────────────────────────────►
       │    (250 bytes × N fragments)       │
       │                                    │
       │ 3. CONFIG_ACK                      │
       │◄───────────────────────────────────┤
       │                                    │
       │ 4. CONFIG_UPDATE_DELTA (optional)  │
       ├────────────────────────────────────►
       │                                    │
```

**Data Structures**:
```cpp
// Full snapshot - 250 bytes per fragment
struct FullConfigSnapshot {
    MqttConfig mqtt;
    NetworkConfig network;
    BatteryConfig battery;
    PowerConfig power;
    InverterConfig inverter;
    CanConfig can;
    ContactorConfig contactor;
    SystemConfig system;
    VersionInfo version;
    uint32_t checksum;
};
```

**Problems**:
1. ❌ **Large payload**: 250+ bytes per fragment
2. ❌ **Complex fragmentation**: Multi-message reassembly required
3. ❌ **No automatic sync**: Receiver must manually request
4. ❌ **Not used consistently**: Web UI doesn't query ReceiverConfigManager
5. ❌ **Separate from display cache**: TransmitterManager holds separate copy

**Usage**: 
- ✅ Called during receiver initialization (`rx_connection_handler.cpp`)
- ❌ NOT used by web UI API handlers
- ❌ NOT updated by version beacons

---

### System 2: Version Beacon (PHASE4_VERSION_BEACON_IMPLEMENTATION_COMPLETE.md)

**Location**: 
- Transmitter: `src/espnow/version_beacon_manager.{h,cpp}`
- Receiver: Handler in `espnow_tasks.cpp`

**Status**: ⚠️ **PARTIALLY IMPLEMENTED**

**Architecture**:
```
Transmitter                          Receiver
┌──────────────┐                    ┌──────────────┐
│VersionBeacon │                    │TransmitterMgr│
│   Manager    │                    │    Cache     │
└──────┬───────┘                    └──────┬───────┘
       │                                    │
       │ VERSION_BEACON (every 15s)         │
       ├────────────────────────────────────►
       │ {                                  │
       │   mqtt_config_version: 5           │
       │   network_config_version: 3        │
       │   battery_settings_version: 1      │
       │   power_profile_version: 0         │
       │   mqtt_connected: true             │
       │   ethernet_connected: true         │
       │ }                                  │
       │                                    │
       │◄─────If version mismatch──────────┤
       │ CONFIG_SECTION_REQUEST             │
       │   (section: MQTT, version: 5)      │
       │                                    │
       │ MQTT_CONFIG_ACK                    │
       ├────────────────────────────────────►
       │   (full MQTT config data)          │
       │                                    │
```

**Data Structures**:
```cpp
// Version beacon - 20 bytes
struct version_beacon_t {
    uint8_t type;                      // msg_version_beacon
    uint32_t mqtt_config_version;
    uint32_t network_config_version;
    uint32_t battery_settings_version;
    uint32_t power_profile_version;
    bool mqtt_connected;
    bool ethernet_connected;
    uint8_t reserved[2];
};

// Config section request - 16 bytes
struct config_section_request_t {
    uint8_t type;                      // msg_config_section_request
    config_section_t section;          // Which section to send
    uint32_t requested_version;
    uint8_t reserved[10];
};
```

**Strengths**:
1. ✅ **Minimal bandwidth**: 20 bytes every 15 seconds = 4.8 KB/hour
2. ✅ **Automatic updates**: Config changes trigger immediate beacon
3. ✅ **Runtime status included**: MQTT/Ethernet connection state
4. ✅ **On-demand fetch**: Receiver only requests when needed
5. ✅ **Version comparison**: Prevents unnecessary data transfer

**Current Implementation Status**:
- ✅ Beacon transmission (15s periodic + event-driven)
- ✅ Version tracking for MQTT, Network, Battery, Power Profile
- ✅ Runtime status (MQTT connected, Ethernet connected)
- ✅ Receiver version beacon handler
- ✅ Config section request mechanism
- ⚠️ **PARTIAL**: Response handlers exist but NOT integrated with cache
- ❌ **MISSING**: Battery settings send not implemented
- ❌ **MISSING**: Power profile send not implemented

**Problems**:
1. ❌ **Dual response paths**: Both `version_beacon_manager.cpp::send_config_section()` AND `message_handler.cpp::handle_mqtt_config_request()` send same data
2. ❌ **Inconsistent storage**: Responses go to TransmitterManager, not ReceiverConfigManager
3. ❌ **Manual web UI requests**: Page still sends individual requests instead of using cache
4. ❌ **Cache timing issue**: Web page loads before beacon arrives, cache empty

---

### System 3: Individual Request/ACK Messages

**Location**: Multiple files (message_handler.cpp, api_handlers.cpp)  
**Status**: ❌ **AD-HOC & REDUNDANT**

**Messages**:
- `msg_mqtt_config_request` → `msg_mqtt_config_ack`
- `msg_mqtt_config_update` → `msg_mqtt_config_ack`
- `msg_network_config_request` → `msg_network_config_ack`
- `msg_network_config_update` → `msg_network_config_ack`

**Flow** (Example: MQTT Config):
```
Web Page                 Receiver API             Transmitter
┌────────┐              ┌────────────┐           ┌──────────┐
│ Load   │              │            │           │          │
│ MQTT   │              │            │           │          │
│ Page   │              │            │           │          │
└───┬────┘              └─────┬──────┘           └────┬─────┘
    │                         │                       │
    │ GET /api/get_mqtt_config│                       │
    ├────────────────────────►│                       │
    │                         │                       │
    │     Cache empty         │                       │
    │◄────────────────────────┤                       │
    │                         │                       │
    │ POST /api/request_mqtt_config                   │
    ├────────────────────────►│                       │
    │                         │ MQTT_CONFIG_REQUEST   │
    │                         ├──────────────────────►│
    │                         │                       │
    │  Wait 2 seconds...      │                       │
    │                         │   MQTT_CONFIG_ACK     │
    │                         │◄──────────────────────┤
    │                         │ (stores in cache)     │
    │                         │                       │
    │ GET /api/get_mqtt_config (retry)                │
    ├────────────────────────►│                       │
    │                         │                       │
    │     Config data         │                       │
    │◄────────────────────────┤                       │
    │                         │                       │
```

**Problems**:
1. ❌ **Race condition**: Web page loads faster than cache populates
2. ❌ **Manual timing**: JavaScript has hard-coded 2-second wait
3. ❌ **Duplicate handlers**: Same config sent by both version_beacon AND request handlers
4. ❌ **No integration**: Doesn't use version beacon system
5. ❌ **Inconsistent cache**: Data stored in TransmitterManager, not ReceiverConfigManager

---

## Cache Storage Analysis

### Transmitter Side

**Storage Locations**:

1. **MqttConfigManager** (`lib/mqtt_manager/`)
   - NVS-backed storage
   - Version tracking: `config_version_` (increments on save)
   - Connection status: ❌ **WAS BROKEN** (hardcoded false, NOW FIXED)

2. **EthernetManager** (`src/network/ethernet_manager.{h,cpp}`)
   - NVS-backed storage for static IP config
   - Version tracking: `network_config_version_` (increments on save)
   - Runtime IP: From Ethernet stack

3. **SettingsManager** (`src/settings/settings_manager.{h,cpp}`)
   - NVS-backed battery settings
   - Version tracking: ✅ Implemented
   - Usage: ❌ Not sent via version beacon yet

4. **EnhancedCache** (`src/espnow/enhanced_cache.{h,cpp}`)
   - **Transient queue**: 250 entries for battery readings (FIFO)
   - **State slots**: Network, MQTT, Battery (versioned, never deleted)
   - ⚠️ **NOT USED**: State slots exist but aren't populated/used

**Current Flow**:
```
Config Source          Beacon System       Storage
┌────────────┐        ┌──────────┐       ┌────────┐
│MqttConfig  │───────►│VersionBe│───────►│  NVS   │
│  Manager   │ v_num  │  acon    │ save  │        │
└────────────┘        └──────────┘       └────────┘
       │                     │
       │                     │ get_config_version()
       │                     ▼
       │              ┌──────────────┐
       │              │ version_     │
       │              │ beacon_t     │
       │              │ {            │
       │              │   mqtt: v5   │
       │              │   net:  v3   │
       │              │ }            │
       │              └──────────────┘
       │
       └─── Also used by handle_mqtt_config_request() ───┐
                                                          │
                                ┌─────────────────────────┘
                                ▼
                          DUPLICATE SEND
```

### Receiver Side

**Storage Locations**:

1. **TransmitterManager** (`lib/webserver/utils/transmitter_manager.{h,cpp}`)
   - Static member variables (RAM only)
   - Stores: MQTT config, Network config, Battery settings, Metadata
   - Version tracking: ✅ Per-section version numbers
   - Used by: Web UI API handlers
   - Populated by: Individual ACK messages AND version beacon handler

2. **ReceiverConfigManager** (`src/config/config_receiver.{h,cpp}`)
   - Wraps ConfigManager (full config struct)
   - Stores: Complete configuration snapshot
   - Used by: ❌ **NOTHING** (initialized but not queried)
   - Populated by: CONFIG_REQUEST_FULL flow

3. **BatterySettingsCache** (`src/espnow/battery_settings_cache.{h,cpp}`)
   - NVS-backed version tracking
   - Stores: Only version number (data in TransmitterManager)
   - Purpose: Version comparison for re-request logic

**Current Flow**:
```
Beacon Arrives          Handler                Cache Update
┌────────────┐        ┌──────────┐           ┌────────────┐
│VERSION_    │───────►│ espnow_  │──────────►│Transmitter │
│BEACON      │ route  │ tasks.cpp│ store     │  Manager   │
│{           │        │          │           │ (static)   │
│ mqtt: v5   │        │ if(need) │           │            │
│ net:  v3   │        │ request  │           │ mqtt_cfg   │
│}           │        │ section  │           │ net_cfg    │
└────────────┘        └────┬─────┘           │ batt_cfg   │
                           │                 └────────────┘
                           │                        ▲
                           │ CONFIG_SECTION_REQUEST │
                           ▼                        │
                    ┌──────────┐                   │
                    │Transmit  │─── MQTT_CONFIG_ACK│
                    │  ter     │                    │
                    └──────────┘────────────────────┘

Web Page Load           API Handler           Cache
┌────────────┐        ┌──────────┐          ┌────────┐
│GET /mqtt   │───────►│api_get_  │─────────►│Transmit│
│            │ fetch  │mqtt_     │ query    │  ter   │
│            │        │config    │          │Manager │
│            │        │          │          │        │
│            │◄───────┤if empty  │◄─────────┤mqtt_   │
│"not cached"│ error  │return err│ known?   │config_ │
└────────────┘        └──────────┘          │known=  │
                                            │ false  │
                                            └────────┘
```

---

## Root Cause of MQTT Config Issue

### The Problem
**Web page reports "MQTT config not cached" even though transmitter sends config ACK.**

### Analysis
The issue has **THREE ROOT CAUSES** working together:

1. **Timing**: Web page loads immediately, queries cache before beacon system populates it
   ```javascript
   // settings_page.cpp line 1018
   window.addEventListener('DOMContentLoaded', function() {
       loadMqttConfig();  // ❌ Immediate query, cache may be empty
   });
   ```

2. **Dual Initialization**: 
   - Receiver init calls `ReceiverConfigManager::requestFullSnapshot()` 
   - Web page calls `api_request_mqtt_config` (separate system)
   - These don't share cache storage!

3. **Cache Storage Split**:
   - Full snapshot → `ReceiverConfigManager` (not queried by API)
   - Individual ACK → `TransmitterManager` (queried by API)
   - Web page asks the wrong cache!

### Why It Now Works (After Your Fix)
You saw the transmitter sending the ACK successfully because:
1. ✅ MQTT connection status fix allows actual `connected=1` to be sent
2. ✅ Transmitter properly responds to `msg_mqtt_config_request`
3. ✅ Receiver stores ACK in `TransmitterManager::storeMqttConfig()`

**But the web page still shows "not cached" initially because:**
- Page loads and queries cache **BEFORE** beacon arrives
- Initialization requests may complete after page renders
- JavaScript's 2-second retry may not be enough if connection just established

---

## Recommended Architecture: Unified Version Beacon Cache

### Design Principles
1. **Single Source of Truth**: Transmitter NVS is authoritative, receiver NVS is synchronized copy
2. **Automatic Synchronization**: Version beacon system handles all updates
3. **Unified Cache**: One receiver-side cache for all data (working copy in RAM)
4. **Write-Through Cache**: Updates flow Beacon → Cache → NVS
5. **Fast Boot**: Cache loads from NVS on startup (no waiting for beacons)

### Proposed Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                        TRANSMITTER                                  │
└─────────────────────────────────────────────────────────────────────┘
                                │
                                │ Every 15s + on change
                                ▼
                    ┌───────────────────────┐
                    │   VERSION BEACON      │
                    │  (20 bytes)           │
                    │  ┌─────────────────┐  │
                    │  │ mqtt: v5        │  │
                    │  │ network: v3     │  │
                    │  │ battery: v1     │  │
                    │  │ profile: v0     │  │
                    │  │ mqtt_conn: true │  │
                    │  │ eth_conn: true  │  │
                    │  └─────────────────┘  │
                    └──────────┬────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────────────┐
│                         RECEIVER                                    │
│                                                                     │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │         VERSION BEACON HANDLER                               │  │
│  │         (espnow_tasks.cpp)                                   │  │
│  └────────────────────┬──────────────────────┬──────────────────┘  │
│                       │                      │                     │
│              Compare versions        Update runtime status         │
│                       │                      │                     │
│                       ▼                      ▼                     │
│         ┌───────────────────────┐  ┌─────────────────┐            │
│         │ If version mismatch:  │  │  mqtt_connected │            │
│         │ - Cache empty         │  │  eth_connected  │            │
│         │ - Version < beacon    │  │                 │            │
│         └───────────┬───────────┘  └─────────────────┘            │
│                     │                                              │
│              Send request                                          │
│                     │                                              │
│                     ▼                                              │
│         ┌───────────────────────┐                                 │
│         │ CONFIG_SECTION_REQUEST│                                 │
│         │ (section: MQTT, v: 5) │                                 │
│         └───────────┬───────────┘                                 │
│                     │                                              │
│              Transmitter responds                                  │
│                     │                                              │
│                     ▼                                              │
│         ┌───────────────────────┐                                 │
│         │  MQTT_CONFIG_ACK      │                                 │
│         │  (full config data)   │                                 │
│         └───────────┬───────────┘                                 │
│                     │                                              │
│              Store in unified cache                                │
│                     │                                              │
│                     ▼                                              │
│  ┌──────────────────────────────────────────────────────────────┐ │
│  │            UNIFIED STATIC DATA CACHE                         │ │
│  │            (TransmitterManager - enhanced)                    │ │
│  │                                                              │ │
│  │  ┌─────────────────────────────────────────────┐            │ │
│  │  │ MQTT Config Section                         │            │ │
│  │  │  - version: 5                               │            │ │
│  │  │  - enabled: true                            │            │ │
│  │  │  - server: 192.168.1.221                    │            │ │
│  │  │  - port: 1883                               │            │ │
│  │  │  - username, password, client_id            │            │ │
│  │  │  - connected: true (runtime)                │            │ │
│  │  │  - known: true                              │            │ │
│  │  └─────────────────────────────────────────────┘            │ │
│  │                                                              │ │
│  │  ┌─────────────────────────────────────────────┐            │ │
│  │  │ Network Config Section                      │            │ │
│  │  │  - version: 3                               │            │ │
│  │  │  - current_ip: 192.168.1.100                │            │ │
│  │  │  - static_ip: 192.168.1.100                 │            │ │
│  │  │  - use_static: true                         │            │ │
│  │  │  - gateway, subnet, dns                     │            │ │
│  │  │  - known: true                              │            │ │
│  │  └─────────────────────────────────────────────┘            │ │
│  │                                                              │ │
│  │  ┌─────────────────────────────────────────────┐            │ │
│  │  │ Battery Settings Section                    │            │ │
│  │  │  - version: 1                               │            │ │
│  │  │  - capacity, voltage limits                 │            │ │
│  │  │  - current limits, SOC limits               │            │ │
│  │  │  - known: true                              │            │ │
│  │  └─────────────────────────────────────────────┘            │ │
│  │                                                              │ │
│  │  ┌─────────────────────────────────────────────┐            │ │
│  │  │ Metadata Section                            │            │ │
│  │  │  - firmware version: 2.0.0                  │            │ │
│  │  │  - build date                               │            │ │
│  │  │  - device type                              │            │ │
│  │  └─────────────────────────────────────────────┘            │ │
│  │                                                              │ │
│  │  � CACHE (Working Copy - RAM)                              │ │
│  │     ↕                                                        │ │
│  │  💾 NVS (Persistent Storage - Flash)                        │ │
│  │                                                              │ │
│  │  Boot:   NVS → Cache (load)                                 │ │
│  │  Update: Beacon → Cache → NVS (write-through)               │ │
│  │  Display: Cache → WebServer (always from cache)             │ │
│  └──────────────────────────────────────────────────────────────┘ │
│                     ▲                                              │
│                     │                                              │
│           Direct query (no waiting)                                │
│                     │                                              │
│                     │                                              │
│  ┌──────────────────┴──────────────────┐                          │
│  │     WEB UI API HANDLERS             │                          │
│  │  ┌───────────────────────────────┐  │                          │
│  │  │ /api/get_mqtt_config          │  │                          │
│  │  │  → Query cache directly       │  │                          │
│  │  │  → No manual request needed   │  │                          │
│  │  │  → Always has data (if conn)  │  │                          │
│  │  └───────────────────────────────┘  │                          │
│  └─────────────────────────────────────┘                          │
└─────────────────────────────────────────────────────────────────────┘
```

### Key Changes

#### 1. Remove Redundant Systems
- ❌ Delete `ReceiverConfigManager` (config_receiver.{h,cpp})
- ❌ Remove individual request handlers from web page JavaScript
- ❌ Remove `handle_mqtt_config_request()` from message_handler.cpp
- ❌ Remove CONFIG_REQUEST_FULL fragmentation system
- ✅ Keep only VERSION_BEACON + CONFIG_SECTION_REQUEST flow

#### 2. Consolidate Cache Storage
**File**: `espnowreciever_2/lib/webserver/utils/transmitter_manager.{h,cpp}`

**Changes**:
```cpp
// Add section state tracking
struct ConfigSection {
    uint32_t version;
    bool known;
    bool dirty;  // Needs to be saved to NVS
};

// Enhanced TransmitterManager
class TransmitterManager {
private:
    // Section tracking
    static ConfigSection mqtt_section_;
    static ConfigSection network_section_;
    static ConfigSection battery_section_;
    static ConfigSection metadata_section_;
    
    // Existing data (MQTT, network, battery, metadata)
    // ...
    
public:
    // Initialize from NVS
    static void init();
    
    // Save all sections to NVS
    static void saveAllToNVS();
    
    // Save specific section to NVS
    static void saveSectionToNVS(config_section_t section);
    
    // Check if ANY section needs refresh
    static bool needsRefresh(const version_beacon_t* beacon);
    
    // Request missing sections
    static void requestMissingSections(const version_beacon_t* beacon, 
                                      const uint8_t* transmitter_mac);
};
```

#### 3. Enhanced Version Beacon Handler

**File**: `espnowreciever_2/src/espnow/espnow_tasks.cpp`

**New Logic**:
```cpp
router.register_route(msg_version_beacon,
    [](const espnow_queue_msg_t* msg, void* ctx) {
        if (msg->len >= (int)sizeof(version_beacon_t)) {
            const version_beacon_t* beacon = 
                reinterpret_cast<const version_beacon_t*>(msg->data);
            
            // 1. Update runtime status
            TransmitterManager::updateRuntimeStatus(
                beacon->mqtt_connected,
                beacon->ethernet_connected
            );
            
            // 2. Check if any sections need refresh
            if (TransmitterManager::needsRefresh(beacon)) {
                // 3. Request all missing/stale sections in one batch
                TransmitterManager::requestMissingSections(beacon, msg->mac);
            }
            
            // 4. Save beacon timestamp for connection monitoring
            TransmitterManager::setLastBeaconTime(millis());
        }
    },
    0xFF, nullptr);
```

#### 4. Remove Web Page Manual Requests

**File**: `espnowreciever_2/lib/webserver/pages/settings_page.cpp`

**OLD (lines 653-693)**:
```javascript
async function loadMqttConfig() {
    const response = await fetch('/api/get_mqtt_config');
    const data = await response.json();
    
    if (!data.success) {
        // ❌ REMOVE THIS - Manual request logic
        await fetch('/api/request_mqtt_config', { method: 'POST' });
        await new Promise(resolve => setTimeout(resolve, 2000));
        // retry...
    }
    
    populateMqttConfig(data);
}
```

**NEW**:
```javascript
async function loadMqttConfig() {
    // Simple direct query - cache always populated by beacon system
    const response = await fetch('/api/get_mqtt_config');
    const data = await response.json();
    
    if (!data.success) {
        // Only show error if transmitter disconnected
        console.error('Transmitter not connected or config not synced yet');
        // Could retry after a delay, but beacon system will populate
    } else {
        populateMqttConfig(data);
    }
}
```

#### 5. Transmitter: Complete Version Beacon Send

**File**: `ESPnowtransmitter2/espnowtransmitter2/src/espnow/version_beacon_manager.cpp`

**Add Battery Settings**:
```cpp
case config_section_battery: {
    // Get battery settings from SettingsManager
    const auto& settings = SettingsManager::instance().getCurrentSettings();
    
    battery_settings_update_t batt_msg;
    batt_msg.type = msg_battery_settings_update;
    batt_msg.capacity_wh = settings.capacity_wh;
    batt_msg.max_voltage_mv = settings.max_voltage_mv;
    batt_msg.min_voltage_mv = settings.min_voltage_mv;
    batt_msg.max_charge_current_a = settings.max_charge_current_a;
    batt_msg.max_discharge_current_a = settings.max_discharge_current_a;
    batt_msg.soc_high_limit = settings.soc_high_limit;
    batt_msg.soc_low_limit = settings.soc_low_limit;
    batt_msg.cell_count = settings.cell_count;
    batt_msg.chemistry = settings.chemistry;
    batt_msg.version = SettingsManager::instance().getVersion();
    
    esp_now_send(receiver_mac, (const uint8_t*)&batt_msg, sizeof(batt_msg));
    LOG_INFO("VERSION_BEACON", "Sent battery settings (v%u)", batt_msg.version);
    break;
}
```

**Add Metadata**:
```cpp
case config_section_metadata: {  // New section
    metadata_response_t meta_msg;
    meta_msg.type = msg_metadata_response;
    meta_msg.is_valid = true;
    
    strncpy(meta_msg.environment, FW_ENVIRONMENT, sizeof(meta_msg.environment) - 1);
    strncpy(meta_msg.device_name, FW_DEVICE_NAME, sizeof(meta_msg.device_name) - 1);
    meta_msg.version_major = FW_VERSION_MAJOR;
    meta_msg.version_minor = FW_VERSION_MINOR;
    meta_msg.version_patch = FW_VERSION_PATCH;
    strncpy(meta_msg.build_datetime, FW_BUILD_DATETIME, sizeof(meta_msg.build_datetime) - 1);
    
    esp_now_send(receiver_mac, (const uint8_t*)&meta_msg, sizeof(meta_msg));
    LOG_INFO("VERSION_BEACON", "Sent metadata");
    break;
}
```

#### 6. Write-Through Cache with NVS Persistence

**File**: `espnowreciever_2/lib/webserver/utils/transmitter_manager.cpp`

**Architecture**: Receiver uses same pattern as transmitter - NVS-backed cache with write-through updates.

**Flow**:
```
┌─────────────────────────────────────────────────────────────┐
│ RECEIVER BOOT SEQUENCE                                      │
│ 1. TransmitterManager::init()                               │
│ 2. Load all sections from NVS → Cache (RAM)                │
│ 3. Web server starts, serves from cache immediately         │
│ 4. Version beacon arrives, updates cache if newer           │
│ 5. Cache changes written back to NVS                        │
└─────────────────────────────────────────────────────────────┘
```

**Implementation**:
```cpp
void TransmitterManager::init() {
    Preferences prefs;
    if (!prefs.begin("tx_cache", true)) {  // Read-only
        LOG_WARN("TX_CACHE", "Could not open NVS");
        return;
    }
    
    // Load MQTT section
    mqtt_section_.version = prefs.getUInt("mqtt_ver", 0);
    mqtt_section_.known = prefs.getBool("mqtt_known", false);
    if (mqtt_section_.known) {
        mqtt_enabled = prefs.getBool("mqtt_en", false);
        prefs.getBytes("mqtt_srv", mqtt_server, 4);
        mqtt_port = prefs.getUShort("mqtt_port", 1883);
        prefs.getString("mqtt_user", mqtt_username, sizeof(mqtt_username));
        prefs.getString("mqtt_pass", mqtt_password, sizeof(mqtt_password));
        prefs.getString("mqtt_cid", mqtt_client_id, sizeof(mqtt_client_id));
        mqtt_config_version = mqtt_section_.version;
        mqtt_config_known = true;
        LOG_INFO("TX_CACHE", "Loaded MQTT config from NVS (v%u)", mqtt_section_.version);
    }
    
    // Load Network section
    network_section_.version = prefs.getUInt("net_ver", 0);
    network_section_.known = prefs.getBool("net_known", false);
    if (network_section_.known) {
        prefs.getBytes("net_ip", current_ip, 4);
        prefs.getBytes("net_gw", current_gateway, 4);
        prefs.getBytes("net_sub", current_subnet, 4);
        prefs.getBytes("net_sip", static_ip, 4);
        prefs.getBytes("net_sgw", static_gateway, 4);
        prefs.getBytes("net_ssub", static_subnet, 4);
        prefs.getBytes("net_dns1", static_dns_primary, 4);
        prefs.getBytes("net_dns2", static_dns_secondary, 4);
        is_static_ip = prefs.getBool("net_static", false);
        network_config_version = network_section_.version;
        ip_known = true;
        LOG_INFO("TX_CACHE", "Loaded network config from NVS (v%u)", network_section_.version);
    }
    
    // Load Battery section
    battery_section_.version = prefs.getUInt("batt_ver", 0);
    battery_section_.known = prefs.getBool("batt_known", false);
    if (battery_section_.known) {
        // Load battery settings
        battery_settings.version = battery_section_.version;
        battery_settings_known = true;
        LOG_INFO("TX_CACHE", "Loaded battery settings from NVS (v%u)", battery_section_.version);
    }
    
    // Load Metadata section
    metadata_section_.version = prefs.getUInt("meta_ver", 0);
    metadata_section_.known = prefs.getBool("meta_known", false);
    if (metadata_section_.known) {
        metadata_received = true;
        metadata_valid = prefs.getBool("meta_valid", false);
        prefs.getString("meta_env", metadata_env, sizeof(metadata_env));
        prefs.getString("meta_dev", metadata_device, sizeof(metadata_device));
        metadata_major = prefs.getUChar("meta_maj", 0);
        metadata_minor = prefs.getUChar("meta_min", 0);
        metadata_patch = prefs.getUChar("meta_pat", 0);
        prefs.getString("meta_date", metadata_build_date, sizeof(metadata_build_date));
        LOG_INFO("TX_CACHE", "Loaded metadata from NVS (v%u)", metadata_section_.version);
    }
    
    prefs.end();
}

void TransmitterManager::saveSectionToNVS(config_section_t section) {
    Preferences prefs;
    if (!prefs.begin("tx_cache", false)) {  // Read-write
        LOG_ERROR("TX_CACHE", "Could not open NVS for writing");
        return;
    }
    
    switch (section) {
        case config_section_mqtt:
            prefs.putUInt("mqtt_ver", mqtt_section_.version);
            prefs.putBool("mqtt_known", mqtt_section_.known);
            if (mqtt_section_.known) {
                prefs.putBool("mqtt_en", mqtt_enabled);
                prefs.putBytes("mqtt_srv", mqtt_server, 4);
                prefs.putUShort("mqtt_port", mqtt_port);
                prefs.putString("mqtt_user", mqtt_username);
                prefs.putString("mqtt_pass", mqtt_password);
                prefs.putString("mqtt_cid", mqtt_client_id);
                LOG_INFO("TX_CACHE", "Saved MQTT config to NVS (v%u)", mqtt_section_.version);
            }
            mqtt_section_.dirty = false;
            break;
            
        case config_section_network:
            prefs.putUInt("net_ver", network_section_.version);
            prefs.putBool("net_known", network_section_.known);
            if (network_section_.known) {
                prefs.putBytes("net_ip", current_ip, 4);
                prefs.putBytes("net_gw", current_gateway, 4);
                prefs.putBytes("net_sub", current_subnet, 4);
                prefs.putBytes("net_sip", static_ip, 4);
                prefs.putBytes("net_sgw", static_gateway, 4);
                prefs.putBytes("net_ssub", static_subnet, 4);
                prefs.putBytes("net_dns1", static_dns_primary, 4);
                prefs.putBytes("net_dns2", static_dns_secondary, 4);
                prefs.putBool("net_static", is_static_ip);
                LOG_INFO("TX_CACHE", "Saved network config to NVS (v%u)", network_section_.version);
            }
            network_section_.dirty = false;
            break;
            
        case config_section_battery:
            prefs.putUInt("batt_ver", battery_section_.version);
            prefs.putBool("batt_known", battery_section_.known);
            if (battery_section_.known) {
                // Save battery settings
                LOG_INFO("TX_CACHE", "Saved battery settings to NVS (v%u)", battery_section_.version);
            }
            battery_section_.dirty = false;
            break;
            
        default:
            LOG_WARN("TX_CACHE", "Unknown section %d for NVS save", (int)section);
            break;
    }
    
    prefs.end();
}

// Called from version beacon handler after storing config in cache
void TransmitterManager::storeMqttConfig(const mqtt_config_ack_t* config) {
    // Update cache (working copy)
    mqtt_enabled = config->enabled;
    memcpy(mqtt_server, config->server, 4);
    mqtt_port = config->port;
    strncpy(mqtt_username, config->username, sizeof(mqtt_username) - 1);
    strncpy(mqtt_password, config->password, sizeof(mqtt_password) - 1);
    strncpy(mqtt_client_id, config->client_id, sizeof(mqtt_client_id) - 1);
    mqtt_config_version = config->version;
    mqtt_config_known = true;
    mqtt_connected = config->connected;  // Runtime status from beacon
    
    // Update section tracking
    mqtt_section_.version = config->version;
    mqtt_section_.known = true;
    mqtt_section_.dirty = true;
    
    // Write-through to NVS
    saveSectionToNVS(config_section_mqtt);
    
    LOG_INFO("TX_CACHE", "MQTT config updated in cache and NVS (v%u)", config->version);
}
```

---

## Write-Through Cache Architecture for Receiver

### Question: Should Receiver Cache Be Saved to NVS?

#### Arguments AGAINST NVS Persistence on Receiver ✅ RECOMMENDED

**1. Limited NVS Write Cycles**
- NVS flash has ~100,000 write cycle limit
- Unnecessary writes reduce device lifespan
- Config changes trigger writes, but changes are infrequent

**2. Fast Re-Population**
- Initial data request is small (< 500 bytes total)
- Version beacon triggers automatic sync within 15 seconds
- Transmitter always has source of truth in its NVS

**3. Runtime Status Cannot Be Cached**
```
SCENARIO:
1. Receiver boots, loads cache from NVS: mqtt_connected = true
2. ReAdd web UI loading state**:
   - Show "Loading from transmitter..." instead of "not cached"
   - Add 15-second timeout instead of 2-second retry
   - Display beacon arrival status
5. **Test**: Verify web page loads MQTT/Network config on first boot

**Result**: Working system with existing architecture, no NVS needed on receiver

**4. Simplified Code**
- No NVS read/write logic on receiver
- No dirty tracking
- No periodic save tasks
- Fewer failure modes

**5. Minimal User Impact**
```
WITHOUT NVS:
- Receiver boots
- Web page loads (cache empty)
- Wait 0-15 seconds for first beacon
- Cache populates automatically
- Total delay: < 15 seconds (one-time)

WITH NVS:
- Receiver boots
- Load potentially stale cache (200ms)
- Web page shows OLD data
- Wait 0-15 seconds for beacon to correct
- Cache updates
- Result: Same 15-second delay, but with wrong data shown initially
```

#### Arguments FOR NVS Persistence on Receiver

**1. Instant Web Page Load**
- Cache available immediately on boot
- No "Waiting for data..." message
- Better UX for impatient users

**2. Offline Viewing**
- Can see last-known config even if transmitter is off
- Useful for debugging configuration issues
- Historical record of what settings were

**3. Reduced Network Chattiness**
- No re-request on every reboot
- Matters if receiver reboots frequently (crashes, testing)

#### RECOMMENDATION: NO NVS Persistence on Receiver

**Reasoning**:
1. ✅ Runtime status (MQTT connected, current IP) invalidates cached data
2. ✅ 15-second delay is acceptable for initial load
3. ✅ Transmitter NVS is single source of truth
4. ✅ Simpler code, fewer failure modes
5. ✅ Saves receiver NVS wear

**Recommended Approach with Multi-Layer Disconnection Detection**:

### Disconnection Detection: ESP-NOW Layer vs Application Layer

**Question**: Why not just use ESP-NOW's send status instead of application-layer beacon timeout?

**Answer**: Use BOTH for complete picture:

| Layer | Detection Method | What It Tells You | Latency | Use Case |
|-------|-----------------|-------------------|---------|----------|
| **ESP-NOW** | Send callback status | Radio reachable? | Immediate | Detect immediate radio issues |
| **Application** | Beacon timeout | Peer alive & functioning? | 15-30s | Detect peer crash/hang |

**The Difference**:
```
SCENARIO 1: Radio disconnection (out of range)
- ESP-NOW: ❌ esp_now_send() returns ESP_ERR_ESPNOW_NOT_FOUND immediately
- Beacon: ❌ Times out after 30 seconds (slow detection)
→ Use ESP-NOW status for fast detection

SCENARIO 2: Peer crash/hang (in range but not responding)
- ESP-NOW: ✅ esp_now_send() returns ESP_OK (radio delivered packet)
- Beacon: ❌ No beacon received (peer isn't running)
→ Use beacon timeout to detect this case

SCENARIO 3: Peer slow/overloaded (in range, running, but delayed)
- ESP-NOW: ✅ esp_now_send() returns ESP_OK
- Beacon: ✅ Arrives late (e.g., 20s instead of 15s)
→ Beacon timestamp shows degradation
```

**Recommended: Hybrid Approach**

**Transmitter Side** (detects if receiver is reachable):
```cpp
// version_beacon_manager.cpp
class VersionBeaconManager {
private:
    uint32_t last_successful_send_ms_ = 0;
    uint32_t consecutive_send_failures_ = 0;
    
public:
    void sendBeacon() {
        version_beacon_t beacon;
        // ... populate beacon
        
        esp_err_t result = esp_now_send(receiver_mac, (uint8_t*)&beacon, sizeof(beacon));
        
        if (result == ESP_OK) {
            last_successful_send_ms_ = millis();
            consecutive_send_failures_ = 0;
            LOG_DEBUG("BEACON", "Sent successfully");
        } else {
            consecutive_send_failures_++;
            LOG_WARN("BEACON", "Send failed: %s (failures: %d)", 
                     esp_err_to_name(result), consecutive_send_failures_);
            
            // After 3 consecutive failures, consider receiver offline
            if (consecutive_send_failures_ >= 3) {
                onReceiverDisconnected();  // Notify system
            }
        }
    }
    
    bool isReceiverReachable() {
        // Fast detection: Did last few sends succeed?
        return consecutive_send_failures_ < 3;
    }
};
```

**Receiver Side** (detects if transmitter is alive):
```cpp
// TransmitterManager.h
class TransmitterManager {
private:
    static uint32_t last_beacon_time_ms_;
    static const uint32_t BEACON_TIMEOUT_MS = 30000;  // 2× beacon interval + margin
    
public:
    // Called when beacon arrives
    static void updateBeaconTimestamp() {
        last_beacon_time_ms_ = millis();
    }
    
    // Application-layer detection: Is peer functioning?
    static bool isTransmitterAlive() {
        return (millis() - last_beacon_time_ms_) < BEACON_TIMEOUT_MS;
    }
    
    static uint32_t getSecondsSinceLastBeacon() {
        return (millis() - last_beacon_time_ms_) / 1000;
    }
};
```

**Better Approach: Bidirectional Heartbeat (Optional Enhancement)**

Instead of beacon timeout being passive, make receiver ACK beacons:

```cpp
// Transmitter sends beacon
void VersionBeaconManager::sendBeacon() {
    // ... send beacon
    waiting_for_ack_ = true;
    ack_timeout_start_ = millis();
}

// Transmitter receives ACK
void VersionBeaconManager::handleBeaconAck(const beacon_ack_t* ack) {
    waiting_for_ack_ = false;
    last_ack_received_ms_ = millis();
    // Now we KNOW receiver is alive (not just "radio delivered")
}

// Check if receiver is alive (ACK-based)
bool VersionBeaconManager::isReceiverAlive() {
    if (waiting_for_ack_) {
        // Timeout after 5 seconds
        return (millis() - ack_timeout_start_) < 5000;
    }
    // Last ACK within 30 seconds
    return (millis() - last_ack_received_ms_) < 30000;
}

// Receiver sends ACK
void handleVersionBeacon(const version_beacon_t* beacon) {
    // Process beacon...
    
    // Send ACK
    beacon_ack_t ack;
    ack.type = msg_beacon_ack;
    ack.beacon_sequence = beacon->sequence_number;  // Add to beacon
    ack.receiver_uptime_ms = millis();
    esp_now_send(transmitter_mac, (uint8_t*)&ack, sizeof(ack));
}
```

**Chosen Approach: Option 3 - Hybrid Detection** ✅

This provides the best balance of reliability and simplicity:
- Receiver: Beacon timeout (detects peer hang/crash)
- Transmitter: ESP-NOW send status (fast radio detection)
- ✅ Fast detection of radio issues, slow detection of peer issues
- ✅ Two independent checks catch different failure modes
- ✅ No additional messages required
- ✅ Simple to implement

**Alternative Options** (for reference):

**Option 1: Beacon Timeout Only**
- Receiver: Beacon timeout (30s) - simple, works
- Transmitter: Don't know if receiver is alive (one-way monitoring)
- ❌ Slow detection (30s), no radio-level detection

**Option 2: ESP-NOW Status Only**
- Track ESP-NOW send status from messages sent to peer
- ❌ Only works if regularly sending messages
- ❌ Doesn't detect peer hangs (radio delivers but app frozen)

**Option 4: Bidirectional ACK**
- Beacons include sequence number, receiver ACKs each beacon
- ✅ Most reliable, both sides know peer is alive
- ❌ More complexity, additional messages
- 💡 Consider for future enhancement if needed

**Web UI Display Logic**:
```javascript
// settings_page.js
function updateTransmitterStatus() {
    fetch('/api/transmitter_status')
        .then(response => response.json())
        .then(data => {
            if (!data.connected) {
                showError('Transmitter offline (no beacon for ' + 
                         data.seconds_since_beacon + 's)');
                disableConfigEditing();  // Can't change config if TX offline
            } else {
                showSuccess('Transmitter connected');
                enableConfigEditing();
            }
        });
}

// Check every 5 seconds
setInterval(updateTransmitterStatus, 5000);
```

**State Transitions**:
1. **Initial boot**: Cache empty, "Waiting for transmitter..."
2. **Radio detection**: ESP-NOW send status available if sending messages
3. **First beacon**: Update timestamp, cache populates, "Connected"
4. **Runtime**: Beacons every 15s, status updated
5. **Radio disconnection**: ESP-NOW send fails (immediate detection if sending)
6. **Peer hang/crash**: Beacon timeout after 30s, "Transmitter offline"
7. **Reconnection**: Beacon/send succeeds, update timestamp, "Connected"

**Implementation Details for Option 3 (Hybrid)**:

**Receiver Side** (Primary - needs to monitor transmitter):
```cpp
// TransmitterManager.cpp
class TransmitterManager {
private:
    static uint32_t last_beacon_time_ms_;
    static const uint32_t BEACON_TIMEOUT_MS = 30000;  // 2× beacon interval + margin
    
public:
    // Called when any beacon arrives
    static void updateBeaconTimestamp() {
        last_beacon_time_ms_ = millis();
    }
    
    // Application-layer: Is transmitter alive and functioning?
    static bool isTransmitterAlive() {
        return (millis() - last_beacon_time_ms_) < BEACON_TIMEOUT_MS;
    }
    
    static uint32_t getSecondsSinceLastBeacon() {
        return (millis() - last_beacon_time_ms_) / 1000;
    }
    
    static const char* getConnectionStatus() {
        if (!isTransmitterAlive()) {
            return "Offline";
        }
        uint32_t seconds = getSecondsSinceLastBeacon();
        if (seconds < 20) return "Connected";
        if (seconds < 30) return "Degraded";
        return "Offline";
    }
};
```

**Transmitter Side** (Optional - can monitor receiver if needed):
```cpp
// version_beacon_manager.cpp
class VersionBeaconManager {
private:
    uint32_t consecutive_send_failures_ = 0;
    
public:
    void sendBeacon() {
        version_beacon_t beacon;
        // ... populate beacon
        
        esp_err_t result = esp_now_send(receiver_mac, (uint8_t*)&beacon, sizeof(beacon));
        
        if (result == ESP_OK) {
            consecutive_send_failures_ = 0;
            LOG_DEBUG("BEACON", "Sent (RX reachable)");
        } else {
            consecutive_send_failures_++;
            LOG_WARN("BEACON", "Send failed: %s (failures: %d)", 
                     esp_err_to_name(result), consecutive_send_failures_);
            
            if (consecutive_send_failures_ >= 3) {
                // Receiver unreachable (radio level)
                LOG_ERROR("BEACON", "Receiver unreachable (radio)");
                // Optional: Reduce beacon rate to save power
            }
        }
    }
    
    // Optional: Expose status
    bool isReceiverReachable() {
        return consecutive_send_failures_ < 3;
    }
};
```

**Why This Works**:
1. ✅ Receiver detects transmitter hang/crash via beacon timeout
2. ✅ Transmitter detects radio issues via ESP-NOW send status
3. ✅ No extra messages (beacons already sent every 15s)
4. ✅ Independent failure detection on each side

**Handling Stale Data**:
- Static config remains valid even when transmitter offline (e.g., MQTT server IP)
- Runtime status marked as STALE when transmitter offline
- Web UI shows: "MQTT Config: 192.168.1.221:1883 (Status: Unknown - TX offline)"
- Don't delete cache, just indicate status is not current

**Exception**: 
If receiver reboots more than once per hour in production, reconsider NVS caching to reduce network load.

---

## Appendix A: Real-Time Telemetry Data (OUT OF SCOPE)

**⚠️ IMPORTANT**: Real-time telemetry data (battery cells, temperatures, current, power) uses a **COMPLETELY SEPARATE ARCHITECTURE** from the static config cache system described in this document.

**This appendix is for reference only** to clarify the distinction between static/runtime data (covered in this document) and real-time telemetry (separate system).

**Why Completely Separate?**
- ❌ Different update frequency: 1-10 Hz vs 15-second beacons
- ❌ Different data volume: 192-384 bytes/reading vs 20-byte beacon
- ❌ Different storage: Circular RAM buffer vs NVS
- ❌ Different transmission: Continuous streaming vs version-based requests
- ❌ Different display: WebSocket live updates vs HTTP GET API
- ❌ Different persistence: Never saved vs write-through cache

**Full implementation details will be in**: `REALTIME_TELEMETRY_ARCHITECTURE.md` (separate document)

### Real-Time Data Architecture Overview (Battery Cell Telemetry)

### Problem Statement

**96 battery cells** require continuous monitoring:
- Cell voltage (16-bit, 2 bytes): 96 × 2 = 192 bytes
- Cell temperature (8-bit or 16-bit): 96 × 1-2 = 96-192 bytes
- Update rate: 1-10 Hz (every 100ms to 1 second)
- **Total bandwidth**: 288-384 bytes × 1-10 Hz = 2.8-38.4 KB/second

### Why Version Beacon Cache is WRONG for This

❌ **Version beacon is for static data**:
- 20-byte beacon every 15 seconds = 1.3 bytes/second
- Cell data is 2,800-38,400 bytes/second
- **21,000× more data volume**

❌ **Caching makes no sense**:
- Cache implies "request once, use many times"
- Cell voltages change continuously
- No "version number" - always latest reading

❌ **Web UI needs live updates**:
- Users want real-time graphs
- Stale data is useless for monitoring
- Need WebSocket or Server-Sent Events

### Recommended Architecture: Separate Streaming System

```
┌─────────────────────────────────────────────────────────────────────┐
│                        TRANSMITTER                                  │
│                                                                     │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  Battery Monitoring Task (100ms loop)                        │  │
│  │  ┌────────────────────────────────────────────────────────┐  │  │
│  │  │ Read all 96 cells via I2C/SPI                          │  │  │
│  │  │ ┌────┬────┬────┬────┬─────┬─────┬─────┬─────┐         │  │  │
│  │  │ │Cell│Cell│Cell│Cell│ ... │Cell │Cell │Cell │         │  │  │
│  │  │ │ 0  │ 1  │ 2  │ 3  │     │ 93  │ 94  │ 95  │         │  │  │
│  │  │ │3.7V│3.7V│3.7V│3.7V│ ... │3.7V │3.7V │3.7V │         │  │  │
│  │  │ └────┴────┴────┴────┴─────┴─────┴─────┴─────┘         │  │  │
│  │  └────────────────────────────────────────────────────────┘  │  │
│  │                                                               │  │
│  │  Decision Point: How to transmit?                            │  │
│  └──────────────┬────────────────────────────────────────────────┘  │
│                 │                                                   │
│                 ▼                                                   │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │         OPTION A: ESP-NOW Streaming (Low Latency)           │  │
│  │  ┌────────────────────────────────────────────────────────┐ │  │
│  │  │ Compress into 250-byte ESP-NOW packets                 │ │  │
│  │  │                                                         │ │  │
│  │  │ Packet 1: Cells 0-31   (2 bytes × 32 = 64 bytes)      │ │  │
│  │  │ Packet 2: Cells 32-63  (64 bytes)                     │ │  │
│  │  │ Packet 3: Cells 64-95  (64 bytes)                     │ │  │
│  │  │                                                         │ │  │
│  │  │ Total: 192 bytes / 3 packets                           │ │  │
│  │  │ Transmission time: ~30ms                               │ │  │
│  │  └────────────────────────────────────────────────────────┘ │  │
│  │                                                               │  │
│  │         OPTION B: MQTT Publish (Network Available)           │  │
│  │  ┌────────────────────────────────────────────────────────┐ │  │
│  │  │ Publish JSON to topic: battery/cells                   │ │  │
│  │  │ {                                                       │ │  │
│  │  │   "cells": [3700, 3700, 3700, ..., 3700],             │ │  │
│  │  │   "temps": [25, 25, 26, ..., 25],                     │ │  │
│  │  │   "timestamp": 1738531200                              │ │  │
│  │  │ }                                                       │ │  │
│  │  │                                                         │ │  │
│  │  │ Compression: GZIP or binary encoding                   │ │  │
│  │  └────────────────────────────────────────────────────────┘ │  │
│  └──────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────┘
                                 │
                                 ▼
┌─────────────────────────────────────────────────────────────────────┐
│                         RECEIVER                                    │
│                                                                     │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  ESP-NOW Handler (if Option A)                              │  │
│  │  ┌────────────────────────────────────────────────────────┐ │  │
│  │  │ msg_battery_cells_chunk_t                              │ │  │
│  │  │ - packet_id: 1/3                                       │ │  │
│  │  │ - cell_start_index: 0                                  │ │  │
│  │  │ - cell_count: 32                                       │ │  │
│  │  │ - data: [3700, 3700, ...]                             │ │  │
│  │  │                                                         │ │  │
│  │  │ Reassemble 3 packets → Full 96-cell array             │ │  │
│  │  └────────────────────────────────────────────────────────┘ │  │
│  │                                                               │  │
│  │  MQTT Subscriber (if Option B)                               │  │
│  │  ┌────────────────────────────────────────────────────────┐ │  │
│  │  │ Subscribe: battery/cells                               │ │  │
│  │  │ On message: Parse JSON → Extract cell array           │ │  │
│  │  └────────────────────────────────────────────────────────┘ │  │
│  └──────────────┬────────────────────────────────────────────────┘  │
│                 │                                                   │
│                 ▼                                                   │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  Circular Buffer (RAM Storage)                              │  │
│  │  ┌────────────────────────────────────────────────────────┐ │  │
│  │  │ Keep last N readings (e.g., 100 samples = 10s @ 10Hz) │ │  │
│  │  │                                                         │ │  │
│  │  │ Reading 0: [3700, 3700, ...] @ t=0ms                  │ │  │
│  │  │ Reading 1: [3701, 3700, ...] @ t=100ms                │ │  │
│  │  │ Reading 2: [3701, 3701, ...] @ t=200ms                │ │  │
│  │  │ ...                                                     │ │  │
│  │  │ Reading 99: [3699, 3700, ...] @ t=9900ms              │ │  │
│  │  │                                                         │ │  │
│  │  │ ❌ NO NVS - data too volatile, too high frequency      │ │  │
│  │  │ ✅ RAM only - overwrite oldest when full               │ │  │
│  │  └────────────────────────────────────────────────────────┘ │  │
│  └──────────────┬────────────────────────────────────────────────┘  │
│                 │                                                   │
│                 ▼                                                   │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  WebSocket Server                                            │  │
│  │  ┌────────────────────────────────────────────────────────┐ │  │
│  │  │ Client connects → Send current reading immediately     │ │  │
│  │  │ New data arrives → Broadcast to all connected clients  │ │  │
│  │  │                                                         │ │  │
│  │  │ Message format (JSON):                                 │ │  │
│  │  │ {                                                       │ │  │
│  │  │   "type": "cell_update",                               │ │  │
│  │  │   "timestamp": 1738531200,                             │ │  │
│  │  │   "cells": [3700, 3700, 3700, ..., 3700],             │ │  │
│  │  │   "min": 3695,                                         │ │  │
│  │  │   "max": 3705,                                         │ │  │
│  │  │   "avg": 3700                                          │ │  │
│  │  │ }                                                       │ │  │
│  │  └────────────────────────────────────────────────────────┘ │  │
│  └──────────────┬────────────────────────────────────────────────┘  │
│                 │                                                   │
└─────────────────┼───────────────────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      WEB UI (Browser)                               │
│                                                                     │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  JavaScript WebSocket Client                                 │  │
│  │  ┌────────────────────────────────────────────────────────┐ │  │
│  │  │ const ws = new WebSocket('ws://receiver.local/cells'); │ │  │
│  │  │                                                         │ │  │
│  │  │ ws.onmessage = (event) => {                            │ │  │
│  │  │   const data = JSON.parse(event.data);                 │ │  │
│  │  │   updateCellGraph(data.cells);                         │ │  │
│  │  │   updateMinMaxAvg(data.min, data.max, data.avg);      │ │  │
│  │  │ };                                                      │ │  │
│  │  └────────────────────────────────────────────────────────┘ │  │
│  │                                                               │  │
│  │  Chart.js / Plotly.js Real-Time Graph                        │  │
│  │  ┌────────────────────────────────────────────────────────┐ │  │
│  │  │  Battery Cell Voltages                                 │ │  │
│  │  │  ┌──────────────────────────────────────────────────┐ │ │  │
│  │  │  │                                                   │ │ │  │
│  │  │  │  ●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●           │ │ │  │
│  │  │  │  ●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●●           │ │ │  │
│  │  │  │                                                   │ │ │  │
│  │  │  │  Cell 0    Cell 24    Cell 48    Cell 72  Cell 95│ │ │  │
│  │  │  └──────────────────────────────────────────────────┘ │ │  │
│  │  │                                                         │ │  │
│  │  │  Min: 3.695V  Max: 3.705V  Avg: 3.700V  Δ: 10mV      │ │  │
│  │  └────────────────────────────────────────────────────────┘ │  │
│  └──────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────┘
```

### Implementation Recommendations

#### Option A: ESP-NOW Streaming (Recommended for Offline Operation)

**Advantages**:
- ✅ Works without WiFi/MQTT
- ✅ Very low latency (< 50ms)
- ✅ Reliable with auto-retry
- ✅ No broker dependency

**Message Definition**:
```cpp
// espnow_common.h
#define MAX_CELLS_PER_PACKET 32

struct battery_cells_chunk_t {
    uint8_t type;                          // msg_battery_cells_chunk
    uint8_t chunk_id;                      // 0, 1, 2 (for 96 cells / 32 per packet)
    uint8_t total_chunks;                  // 3
    uint8_t cell_start_index;              // 0, 32, 64
    uint8_t cell_count;                    // 32 (or remaining)
    uint32_t timestamp_ms;                 // millis() when read
    uint16_t cell_voltages_mv[MAX_CELLS_PER_PACKET];  // 2 bytes × 32 = 64 bytes
    uint16_t checksum;                     // CRC16 of voltages
    uint8_t reserved[6];
} __attribute__((packed));
// Total: 1 + 1 + 1 + 1 + 1 + 4 + 64 + 2 + 6 = 81 bytes per packet
```

**Transmission**:
```cpp
void send_battery_cells() {
    static uint16_t cell_voltages[96];
    read_all_cells(cell_voltages);  // Read from BMS
    
    for (uint8_t chunk = 0; chunk < 3; chunk++) {
        battery_cells_chunk_t msg;
        msg.type = msg_battery_cells_chunk;
        msg.chunk_id = chunk;
        msg.total_chunks = 3;
        msg.cell_start_index = chunk * 32;
        msg.cell_count = (chunk == 2) ? 32 : 32;  // All chunks have 32
        msg.timestamp_ms = millis();
        
        memcpy(msg.cell_voltages_mv, 
               &cell_voltages[chunk * 32], 
               msg.cell_count * sizeof(uint16_t));
        
        msg.checksum = crc16(msg.cell_voltages_mv, msg.cell_count * 2);
        
        esp_now_send(receiver_mac, (uint8_t*)&msg, sizeof(msg));
        delay(5);  // Small delay between chunks
    }
}
```

**Reception**:
```cpp
class BatteryCellCache {
private:
    uint16_t current_cells_[96];
    uint32_t last_update_ms_;
    bool chunks_received_[3];
    
public:
    void handleChunk(const battery_cells_chunk_t* chunk) {
        if (verify_checksum(chunk)) {
            memcpy(&current_cells_[chunk->cell_start_index],
                   chunk->cell_voltages_mv,
                   chunk->cell_count * sizeof(uint16_t));
            
            chunks_received_[chunk->chunk_id] = true;
            
            // If all chunks received, update timestamp and notify WebSocket
            if (chunks_received_[0] && chunks_received_[1] && chunks_received_[2]) {
                last_update_ms_ = chunk->timestamp_ms;
                notifyWebSocketClients();
                memset(chunks_received_, 0, sizeof(chunks_received_));
            }
        }
    }
};
```

#### Option B: MQTT Publish/Subscribe (Recommended with Network)

**Advantages**:
- ✅ Can publish to external monitoring systems
- ✅ No ESP-NOW packet management
- ✅ Built-in QoS and persistence
- ✅ Easier debugging (use MQTT client to view)

**Disadvantages**:
- ❌ Requires MQTT broker
- ❌ Higher latency (100-200ms)
- ❌ More overhead

**Implementation**:
```cpp
void publish_battery_cells() {
    static uint16_t cell_voltages[96];
    read_all_cells(cell_voltages);
    
    // Option B1: JSON (human-readable, larger)
    DynamicJsonDocument doc(2048);
    JsonArray cells = doc.createNestedArray("cells");
    for (int i = 0; i < 96; i++) {
        cells.add(cell_voltages[i]);
    }
    doc["timestamp"] = millis();
    doc["min"] = *std::min_element(cell_voltages, cell_voltages + 96);
    doc["max"] = *std::max_element(cell_voltages, cell_voltages + 96);
    
    String json;
    serializeJson(doc, json);
    mqtt_client.publish("battery/cells", json.c_str(), false);  // No retain
    
    // Option B2: Binary (compact, ~200 bytes)
    struct {
        uint32_t timestamp;
        uint16_t cells[96];
    } payload;
    payload.timestamp = millis();
    memcpy(payload.cells, cell_voltages, sizeof(cell_voltages));
    mqtt_client.publish("battery/cells/binary", (uint8_t*)&payload, sizeof(payload), false);
}
```

### Key Differences from Static Config System

| Aspect | Static Config Cache | Real-Time Cell Data |
|--------|-------------------|-------------------|
| **Update Frequency** | User action (minutes/hours) | Continuous (0.1-1s) |
| **Data Size** | Small (20-200 bytes/section) | Large (192-384 bytes/reading) |
| **Version Tracking** | Yes - increment on change | No - always latest |
| **Caching Strategy** | Store and reuse | Stream and discard |
| **Storage** | NVS (persistent) | RAM only (volatile) |
| **Transmission** | On-demand (version mismatch) | Periodic/continuous |
| **Web UI Access** | REST API (HTTP GET) | WebSocket (push) |
| **Importance of Stale Data** | Acceptable (show last known) | Unacceptable (misleading) |

### Battery Monitoring Page Architecture

**Separate from Settings Page**:
```
/settings       → Uses static config cache (MQTT, Network, Battery limits)
/monitoring     → Uses WebSocket for real-time cell data
```

**JavaScript Example**:
```javascript
// monitoring_page.js
class BatteryMonitor {
    constructor() {
        this.ws = null;
        this.chart = null;
        this.initWebSocket();
        this.initChart();
    }
    
    initWebSocket() {
        this.ws = new WebSocket('ws://' + location.host + '/ws/cells');
        
        this.ws.onopen = () => {
            console.log('Connected to cell data stream');
            document.getElementById('status').textContent = 'Live';
        };
        
        this.ws.onmessage = (event) => {
            const data = JSON.parse(event.data);
            this.updateChart(data.cells);
            this.updateStats(data.min, data.max, data.avg);
        };
        
        this.ws.onerror = () => {
            document.getElementById('status').textContent = 'Disconnected';
            setTimeout(() => this.initWebSocket(), 5000);  // Reconnect
        };
    }
    
    updateChart(cells) {
        // Update Chart.js with new cell voltages
        this.chart.data.datasets[0].data = cells;
        this.chart.update('none');  // No animation for real-time
    }
}
```

---

## Implementation Plan

### Phase 1: Immediate Fixes (Week 1)
**Goal**: Fix current MQTT config issue without major refactor + Setup receiver's own data management

**Tasks**:
1. ✅ **DONE**: Fix `MqttConfigManager::isConnected()` to return real connection status
2. ✅ **DONE**: Verify transmitter sends MQTT ACK with `connected=1`
3. **Setup receiver's own data management**:
   ```cpp
   // ReceiverConfigManager.h - Enhance existing or create new
   class ReceiverConfigManager {
   public:
       // Receiver's own identity (NOT from transmitter)
       static char rx_ip_address[16];        // "192.168.1.50"
       static uint8_t rx_mac_address[6];     // Receiver's MAC
       static char rx_firmware_version[16];  // "2.1.0"
       static char rx_device_name[32];       // "Battery Monitor 1"
       
       // Load/save to separate NVS namespace "rx_config"
       static void init();                   // Load from NVS on boot
       static void saveToNVS();              // Write-through on changes
       
       // API endpoints
       static String getReceiverInfoJson();  // For web UI
   };
   ```
4. **Add init-time config request** in receiver:
   ```cpp
   // rx_connection_handler.cpp::send_initialization_requests()
   // Add MQTT and Network config requests alongside existing requests
   mqtt_config_request_t mqtt_req;
   mqtt_req.type = msg_mqtt_config_request;
   esp_now_send(transmitter_mac, (const uint8_t*)&mqtt_req, sizeof(mqtt_req));
   
   network_config_request_t net_req;
   net_req.type = msg_network_config_request;
   esp_now_send(transmitter_mac, (const uint8_t*)&net_req, sizeof(net_req));
   ```
5. **Increase web page retry time** from 2s to 5s for initial load
6. **Setup Option 3 hybrid disconnection detection**:
   ```cpp
   // TransmitterManager.h - Add tracking
   uint32_t last_beacon_timestamp = 0;
   bool last_espnow_send_success = true;
   
   // version_beacon_manager.cpp - Track send status
   esp_err_t result = esp_now_send(rx_mac, beacon, sizeof(*beacon));
   TransmitterManager::last_espnow_send_success = (result == ESP_OK);
   
   // rx_connection_handler.cpp - Check both conditions
   bool isTransmitterConnected() {
       bool beacon_recent = (millis() - TransmitterManager::last_beacon_timestamp) < 30000;
       bool espnow_ok = TransmitterManager::last_espnow_send_success;
       return beacon_recent && espnow_ok;
   }
   ```
7. **Add web UI endpoint for receiver info**:
   ```cpp
   // webserver.cpp
   server.on("/api/get_receiver_info", HTTP_GET, [](AsyncWebServerRequest *request) {
       request->send(200, "application/json", ReceiverConfigManager::getReceiverInfoJson());
   });
   ```
8. **Test**: 
   - Verify web page loads MQTT/Network config on first boot
   - Verify receiver info displays correctly (IP, MAC, firmware version)
   - Verify transmitter info displays separately from receiver info

**Result**: Working system with existing architecture + receiver's own data properly managed

### Phase 2: Version Beacon Consolidation (Week 2-3)
**Goal**: Migrate all config to version beacon system

**Tasks**:
1. **Complete transmitter version beacon responses**:
   - ✅ MQTT section: Already working
   - ✅ Network section: Already working
   - ⚠️ Battery section: Add handler in `version_beacon_manager.cpp`
   - ⚠️ Metadata section: Add new section type and handler

2. **Enhance receiver version beacon handler**:
   - Batch all section requests together
   - Add retry logic if response doesn't arrive
   - Mark sections as "requested" to avoid duplicate requests

3. **Remove redundant request paths**:
   - Delete `api_request_mqtt_config` endpoint
   - Delete `api_request_network_config` endpoint
   - Remove individual request handlers from transmitter `message_handler.cpp`
   - Keep only ACK handlers (for backward compatibility during transition)

4. **Test**: Verify version beacon triggers all config updates

**Result**: Version beaWITHOUT NVS persistence (RAM only)

**Tasks**:
1. **Simplify TransmitterManager** (NO NVS):
   - Keep RAM-only cache
   - No `saveSectionToNVS()` methods
   - No periodic save tasks
   - Cache repopulates from version beacons after reboot

2. **Remove ReceiverConfigManager**:
   - Migrate any unique functionality to TransmitterManager
   - Delete `config_receiver.{h,cpp}`
   - Remove CONFIG_REQUEST_FULL message handlers

3. **Update web UI**:
   - Remove retry wait logic
   - Show "Waiting for version beacon..." if cache empty
   - Direct query only
   - Add connection status indicator

4. **Test**: 
   - Receiver reboot → cache empties
   - First beacon arrives → cache populates automatically
   - Transmitter reboot → receiver detects and requests all sections
   - Web page shows loading state during initial 15-second window

**Result**: Clean, unified cache system without NVS complexitye-sync
   - Web page always shows current data
Real-Time Data System (Week 5-6)
**Goal**: Implement battery cell monitoring with separate architecture

**Tasks**:
1. **Design decision**: Choose ESP-NOW streaming vs MQTT publish
   - If transmitter has reliable network: MQTT
   - If offline operation required: ESP-NOW
   - Can implement both with runtime selection

2. **Transmitter implementation**:
   - Add battery cell reading task (configurable rate: 1-10 Hz)
   - Implement chunked ESP-NOW messages (3 packets for 96 cells)
   - OR implement MQTT publish with binary/JSON payload
   - Add compression if needed (delta encoding, run-length)

3. **Receiver implementation**:
   - Add BatteryCellCache class (RAM circular buffer)
   - Implement WebSocket server endpoint `/ws/cells`
   - Add reassembly logic for chunked messages
   - Broadcast to all connected WebSocket clients

4. **Web UI implementation**:
   - Create `/monitoring` page separate from `/settings`
   - Add Chart.js real-time graph (96 cells)
   - Implement WebSocket client with auto-reconnect
   - Show min/max/avg/delta statistics
   - Add cell imbalance warnings

5. **Test**:
   - Verify 1-10 Hz update rate achievable
   - Test with multiple WebSocket clients
   - Measure bandwidth usage
   - Test ESP-NOW packet loss handling
   - Verify graph performance with 96 cells

**Result**: Separate real-time monitoring system, distinct from static config cache

### Phase 5: Extensibility (Week 7
**Result**: Clean, unified cache system

### Phase 4: Extensibility (Week 5+)
**Goal**: Easy addition of new config sections
❌ No NVS persistence on receiver (reduced complexity, no wear)
- ✅ Single version beacon handles everything
- ✅ Cache always populated (if connected)
- ✅ Clear separation: Static config vs Real-time telemetry
1. **Template for new sections**:
   ```cpp
   // To add new section "PowerProfile":
   
   // 1. Add enum in espnow_common.h
   config_section_power_profile = 0x04
   
   // 2. Add to version beacon
   uint32_t power_profile_version;
   
   // 3. Add to TransmitterManager
   static uint32_t power_profile_version;
   static bool power_profile_known;
   static PowerProfile power_profile_data;
   
   // 4. Add send handler in version_beacon_manager.cpp
   case config_section_power_profile:
       // Send power profile data
       break;
   
   // 5. Add receive handler in espnow_tasks.cpp
   // Add API endpoint in api_handlers.cpp
   // Add UI in settings_page.cpp
   ```

2. **Documentation**:
   - Create "Adding New Config Section" guide
   - Document version number management
   - Document cache invalidation strategy

---

## Benefits of Recommended Architecture
Data Freshness (No Receiver NVS)
**Current**:
- Receiver cache lost on reboot
- Must re-request everything
- Slows initial page load

**Proposed**:
- Cache NOT saved to NVS (intentionally)
- Receiver boots with empty cache
- First beacon arrives within 15 seconds → auto-populate
- Web page shows "Loading..." during initial sync
- **Benefit**: Always fresh data, no stale runtime status, no NVS wear
- **Trade-off**: 15-second delay on receiver reboot (acceptable)tes × 3 fragments = 750 bytes (on connect)
- Individual requests: 179 bytes (MQTT ACK) + 100 bytes (Network ACK) = 279 bytes

**Proposed**:
- Version beacon: 20 bytes every 15s
- Config sections: Only sent when version changes
- **80% reduction** in normal operation

### 3. Faster Updates
**Current**:
- Manual request → 2s wait → retry → display
- Total: 2-5 seconds from user action to display

**Proposed**:
- Config change → immediate beacon → section send → display
- Total: < 500ms from change to display
- **4-10x faster**

### 4. Automatic Synchronization
**Current**:
- Web page must manually request each section
- Receiver init requests full snapshot (not used by UI)
- Race conditions between different request sources

**Proposed**:
- Version beacon triggers automatic sync
- Cache pre-populated before web page loads
- Zero race conditions

### 5. Persistence
**Current**:
- Receiver cache lost on reboot
- Must re-request everything
- Slows initial page load

**Proposed**:
- Cache saved to NVS
- Receiver boots with last-known config
- Only requests if version changed
- Instant web page load

### 6. Extensibility
**Current**:
- Adding new config requires:
  - New message types
  - New request/ACK handlers
  - New fragmentation logic (if large)
  - New web page request functions
  - Update multiple files

**Proposed**:
- Adding new config requires:
  - Add to version beacon (4 bytes)
  - Add case in `send_config_section()`
  - Add case in receiver handler
  - Add to cache storage
  - **That's it!**

---

## Migration Strategy

### Backward Compatibility
During migration, support BOTH systems:

```cpp
// Transmitter message_handler.cpp
void handle_mqtt_config_request(const espnow_queue_msg_t& msg) {
    // Keep this handler active during migration
    LOG_INFO("MQTT_CFG", "⚠️ LEGACY: Using old request/ACK path (will be removed)");
    send_mqtt_config_ack(true, "Current configuration");
}
```

### Gradual Rollout
1. **Week 1**: Add init-time requests (Phase 1)
2. **Week 2**: Complete version beacon system (Phase 2)
3. **Week 3**: Deprecate old paths with warnings (Phase 2)
4. **Week 4**: Add NVS persistence (Phase 3)
5. **Week 5**: Remove legacy code (Phase 3)

### Testing Checkpoints
- [ ] Receiver can boot with empty cache and populate automatically
- [ ] Web page loads instantly with cached data from NVS
- [ ] Config changes on transmitter appear within 500ms on receiver
- [ ] Receiver reboot preserves cache (NVS)
- [ ] Transmitter reboot detected and triggers full re-sync
- [ ] Version mismatches auto-resolve
- [ ] **Disconnection Detection (Option 3 Hybrid)**:
  - [ ] Transmitter powered off → Receiver shows "Offline" after 30s (beacon timeout)
  - [ ] Transmitter out of range → ESP-NOW send fails immediately, status updated
  - [ ] Transmitter hangs/crashes → Receiver shows "Offline" after 30s (beacon timeout)
  - [ ] Transmitter reconnects → Receiver shows "Connected" on first beacon
  - [ ] ESP-NOW send failure triggers immediate status update (no 30s wait)
  - [ ] Beacon timeout (30s) catches transmitter hangs/crashes
  - [ ] Both detection methods work independently and in combination
  - [ ] Web UI displays seconds since last beacon + connection status
  - [ ] Static config remains valid when transmitter offline
  - [ ] Runtime status marked as "Unknown" when transmitter offline
- [ ] **Network disconnection doesn't lose cache**

## Additional Recommendations

### 1. Runtime Status vs Static Config Separation

**Problem**: MQTT connection status can change between beacon transmissions.

**Example Scenario**:
```
T+0s:   Transmitter boots, MQTT disconnected
T+5s:   Version beacon sent: mqtt_connected=false, mqtt_config_version=5
T+7s:   MQTT connects successfully
T+10s:  Receiver requests MQTT config (saw version 5)
T+11s:  Transmitter sends mqtt_config_ack with mqtt_connected=true
T+15s:  Next beacon sent: mqtt_connected=true, mqtt_config_version=5
```

**Solution**: Version beacon includes BOTH static config versions AND runtime status values

**How It Works**:
```cpp
// Version beacon structure (20 bytes)
struct version_beacon_t {
    uint8_t type;
    // STATIC CONFIG - version tracked
    uint32_t mqtt_config_version;      // Increments when config changes
    uint32_t network_config_version;   // Increments when config changes
    uint32_t battery_settings_version; // Increments when config changes
    uint32_t power_profile_version;    // Increments when config changes
    // RUNTIME STATUS - NOT version tracked
    bool mqtt_connected;               // Current state (changes frequently)
    bool ethernet_connected;           // Current state (changes frequently)
    uint8_t reserved[2];
};
```

**Receiver Processing**:
```cpp
void handleVersionBeacon(const version_beacon_t* beacon) {
    // 1. Update runtime status IMMEDIATELY (no version check, always update)
    // ⭐ THIS IS THE KEY: Status updates bypass version checking entirely
    TransmitterManager::mqtt_connected = beacon->mqtt_connected;
    TransmitterManager::ethernet_connected = beacon->ethernet_connected;
    // ❌ NOT saved to NVS - RAM cache only
    
    // 2. Check static config versions (only request if different)
    if (TransmitterManager::mqtt_config_version != beacon->mqtt_config_version) {
        requestConfigSection(config_section_mqtt);
    }
    if (TransmitterManager::network_config_version != beacon->network_config_version) {
        requestConfigSection(config_section_network);
    }
    // ... check other sections
}
```

**Key Mechanism - How Status Updates Without Version Changes**:

The runtime status fields are **completely independent from version numbers**. Every time a beacon arrives, the receiver:

1. **Extracts status fields**: `mqtt_connected`, `ethernet_connected`
2. **Updates cache immediately**: No version check, no conditional logic
3. **Writes to RAM only**: Direct assignment to TransmitterManager member variables
4. **Never checks/compares version**: Status update happens regardless of version values

```cpp
// This is what happens EVERY beacon (every 15 seconds)
TransmitterManager::mqtt_connected = beacon->mqtt_connected;  // ALWAYS happens
TransmitterManager::ethernet_connected = beacon->ethernet_connected;  // ALWAYS happens

// This ONLY happens if version numbers differ
if (TransmitterManager::mqtt_config_version != beacon->mqtt_config_version) {
    // Request full config section
}
```

**Example Timeline**:
```
T+0s:   TX boots, MQTT disconnected
        TX sends beacon: mqtt_connected=false, mqtt_config_version=5
        RX receives beacon
        → mqtt_connected = false (status updated)
        → mqtt_config_version = 5 (no change from cache, no request)

T+2s:   TX MQTT connects successfully
        Status changed, but config didn't change
        TX still sends beacon: mqtt_connected=true, mqtt_config_version=5
        RX receives beacon
        → mqtt_connected = true ✅ (status updated to latest)
        → mqtt_config_version = 5 (still same, no request sent)
        → Web UI immediately shows "Connected" because mqtt_connected is true

T+4s:   User changes MQTT server IP
        TX detects change, increments version
        TX sends beacon: mqtt_connected=true, mqtt_config_version=6
        RX receives beacon
        → mqtt_connected = true (status remains true)
        → mqtt_config_version changed from 5→6 ✅ (now requests config!)
```

**Benefits**:
- Runtime status updates cache every 15 seconds (always fresh)
- Static config only requested when version changes (bandwidth efficient)
- Version numbers unchanged by runtime status (MQTT connects/disconnects don't trigger version increment)
- Cache always has latest runtime status + static config
- **Runtime status NEVER written to NVS** (prevents stale data on boot)

### 2. Common Codebase Implementation (esp32common)

**Project Structure**:
```
esp32common/
├── config_sync/               # Shared version beacon system
│   ├── version_beacon.h       # Beacon structures
│   ├── version_beacon.cpp     # Common beacon logic
│   └── config_sections.h      # Section enum definitions
├── espnow_common_utils/
│   ├── espnow_common.h        # Message type definitions
│   └── message_router.h       # Common message routing
├── firmware_version.h         # Version macros (FW_VERSION_MAJOR, etc.)
└── docs/
    └── ADDING_CONFIG_SECTION.md  # Guide for adding new sections
```

**Device-Specific Code**:
```
ESPnowtransmitter2/
└── src/
    ├── espnow/
    │   ├── version_beacon_manager.cpp  # TX-specific: Send beacons
    │   └── config_sender.cpp           # TX-specific: Send config sections
    └── config/
        ├── mqtt_config_manager.cpp     # TX-specific: Manage MQTT NVS
        └── ethernet_manager.cpp        # TX-specific: Manage network NVS

espnowreciever_2/
└── src/
    ├── espnow/
    │   └── version_beacon_handler.cpp  # RX-specific: Handle beacons
    └── cache/
        └── transmitter_manager.cpp     # RX-specific: Cache + NVS
```

**Guidelines**:
1. **Common code** (esp32common):
   - Message structure definitions
   - Beacon format (version_beacon_t)
   - Config section enums
   - Utility functions (CRC, validation)
   
2. **Device-specific code**:
   - Transmitter: Beacon sending, config retrieval from NVS
   - Receiver: Beacon handling, cache management
   - Web UI (receiver only)
   - MQTT client (transmitter only)

3. **Version compatibility**:
   - Include `firmware_version.h` from esp32common
   - Transmitter sends version in metadata
   - Receiver checks compatibility and warns if mismatch

### 3. Growing Static Data Considerations

As more configuration sections are added:

**Current Static Sections**:
- MQTT config: ~179 bytes
- Network config: ~100 bytes
- Battery settings: ~50 bytes
- Metadata: ~80 bytes
- **Total**: ~409 bytes

**Potential Future Sections**:
- Power profile settings: ~100 bytes
- Inverter config: ~150 bytes
- CAN bus config: ~80 bytes
- Display settings: ~50 bytes
- **Future Total**: ~780 bytes

**Metadata Section Details**:
```cpp
// Metadata is special - identifies ANY device (transmitter OR receiver)
struct metadata_config_t {
    uint8_t type;                     // msg_metadata_config
    uint32_t version;                 // Version (changes on firmware update)
    
    // Device Identification
    uint8_t mac_address[6];          // Device MAC (unique identifier)
    char device_name[32];            // User-defined name (e.g., "Battery TX 1" or "Monitor RX 1")
    char device_type[16];            // "TRANSMITTER" / "RECEIVER"
    
    // Firmware Information (from firmware_version.h)
    char environment[16];            // "PRODUCTION" / "DEVELOPMENT"
    uint8_t fw_version_major;        // 2
    uint8_t fw_version_minor;        // 0
    uint8_t fw_version_patch;        // 1
    char build_datetime[20];         // "2026-02-13 14:30:00"
    char git_commit[8];              // "a3f2c1e" (short hash)
    
    // Hardware Information
    char board_type[16];             // "ESP32-S3" / "ESP32-C3"
    uint32_t flash_size_mb;          // 16
    uint32_t psram_size_mb;          // 8
    
    uint8_t reserved[8];
} __attribute__((packed));
// Total: ~130 bytes
```

**Purpose**:
- **Device Discovery**: Identify both transmitter (via ESP-NOW) AND receiver (local)
- **Version Compatibility**: Warn if TX/RX firmware versions incompatible
- **Multi-Device Support**: Future - receiver can cache multiple TXs by MAC
- **Debugging**: Show firmware versions on web UI for troubleshooting
- **Identification**: User-friendly device names instead of MAC addresses

**Important**: 
- **Transmitter metadata** → Sent via ESP-NOW → Cached in TransmitterManager
- **Receiver metadata** → Local data only → Stored in ReceiverConfigManager
- **Web UI displays BOTH** → Shows receiver identity + transmitter identity

**Metadata Section Details**:
```cpp
// Metadata is special - identifies the transmitter device
struct metadata_config_t {
    uint8_t type;                     // msg_metadata_config
    uint32_t version;                 // Version (changes on firmware update)
    
    // Device Identification
    uint8_t mac_address[6];          // Transmitter MAC (unique identifier)
    char device_name[32];            // User-defined name (e.g., "Battery TX 1")
    char device_type[16];            // "TRANSMITTER" / "RECEIVER"
    
    // Firmware Information (from firmware_version.h)
    char environment[16];            // "PRODUCTION" / "DEVELOPMENT"
    uint8_t fw_version_major;        // 2
    uint8_t fw_version_minor;        // 0
    uint8_t fw_version_patch;        // 1
    char build_datetime[20];         // "2026-02-13 14:30:00"
    char git_commit[8];              // "a3f2c1e" (short hash)
    
    // Hardware Information
    char board_type[16];             // "ESP32-S3" / "ESP32-C3"
    uint32_t flash_size_mb;          // 16
    uint32_t psram_size_mb;          // 8
    
    uint8_t reserved[8];
} __attribute__((packed));
// Total: ~130 bytes
```

**Purpose**:
- **Device Discovery**: Receiver knows which transmitter it's talking to
- **Version Mismatch Detection**: Warn if TX/RX firmware versions incompatible
- **Multi-Transmitter Support**: Future enhancement - receiver can cache multiple TXs by MAC
- **Debugging**: Show firmware version on web UI for troubleshooting

**Version Beacon Scaling**:
```cpp
// Current: 20 bytes for 4 sections
struct version_beacon_t {
    uint8_t type;                      // 1 byte
    uint32_t mqtt_config_version;      // 4 bytes
    uint32_t network_config_version;   // 4 bytes
    uint32_t battery_settings_version; // 4 bytes
    uint32_t power_profile_version;    // 4 bytes
    bool mqtt_connected;               // 1 byte
    bool ethernet_connected;           // 1 byte
    uint8_t reserved[2];               // 2 bytes
};  // Total: 20 bytes

// Future: 36 bytes for 8 sections (still very small)
struct version_beacon_v2_t {
    uint8_t type;                      // 1 byte
    uint32_t version_numbers[8];       // 32 bytes (8 sections × 4 bytes)
    uint8_t runtime_flags;             // 1 byte (8 boolean flags)
    uint8_t reserved[2];               // 2 bytes
};  // Total: 36 bytes
```

**Recommendation**: Version beacon scales well up to 10-15 config sections before size becomes concern.

### 3. Symmetric Storage Strategy (Both Sides Use NVS)

**Transmitter** (Source of Truth):
- ✅ All static config sections saved to NVS
- ✅ Version numbers persisted
- ✅ RAM cache for fast access
- ✅ Write-through: Config change → Cache → NVS
- ❌ No real-time telemetry in NVS (too volatile)

**Receiver** (Synchronized Copy):
- ✅ All static config sections saved to NVS (same as transmitter)
- ✅ Version numbers persisted
- ✅ RAM cache for fast web server access
- ✅ Write-through: Beacon update → Cache → NVS
- ✅ Runtime status in cache only (no NVS)
- ❌ No real-time telemetry in NVS (too volatile)

**Data Flow**:
```
Static Config: TX NVS ─beacon→ RX Cache ─write-through→ RX NVS
Runtime Status: Beacon ─every 15s→ RX Cache (no NVS)
Real-Time Data: ESP-NOW/MQTT ─stream→ RX Circular Buffer (no NVS)
```

**Rationale**:
- Both sides use same architecture (easier to understand/maintain)
- Receiver NVS enables instant page load after reboot
- Runtime status separated (cache-only, no unnecessary NVS writes)
- Real-time telemetry never touches NVS (meaningless after reboot)

### 4. Data Classification Guidelines

When adding new data to the system, classify it:

**Static Configuration** → Use Version Beacon Cache System
- Changes: Manual user action
- Frequency: Minutes/hours/days between updates
- Size: < 200 bytes per section
- Examples: IP addresses, credentials, capacity limits
- Storage: **Both transmitter AND receiver NVS** (write-through cache)
- Transmission: Version beacon + on-demand section request
- Display: Always from cache (instant access)

**Runtime Status** → Include in Version Beacon
- Changes: System events
- Frequency: Seconds/minutes between updates
- Size: Boolean flags or single values
- Examples: Connected/disconnected, enabled/disabled, current IP (DHCP)
- Storage: **CACHE ONLY (RAM)** - ❌ **NEVER SAVED TO NVS**
- Reason: Status becomes stale immediately on reboot, misleads user
- Transmission: Every version beacon (15s)
- Display: From cache (updated automatically by beacon)
- On boot: Status unknown until first beacon arrives (< 15s)

**Critical Implementation Note**:
```cpp
// ✅ CORRECT - Update cache only
void updateRuntimeStatus(bool mqtt_conn, bool eth_conn) {
    mqtt_connected = mqtt_conn;        // Update RAM cache
    ethernet_connected = eth_conn;     // Update RAM cache
    // ❌ DO NOT CALL saveSectionToNVS() for runtime status
}

// ✅ CORRECT - Update cache AND save to NVS
void storeMqttConfig(const mqtt_config_ack_t* config) {
    // Static config fields
    mqtt_server_ip = config->server;   // Update cache
    mqtt_port = config->port;          // Update cache
    mqtt_config_version = config->version;
    
    saveSectionToNVS(config_section_mqtt);  // ✅ Save static config to NVS
    
    // Runtime status field (from beacon, not config ACK)
    mqtt_connected = latest_beacon.mqtt_connected;  // Update cache
    // ❌ NOT saved - runtime status is volatile
}
```

**Real-Time Telemetry** → Use Streaming System
- Changes: Continuous sensor updates
- Frequency: 0.1-10 Hz
- Size: Large arrays (96+ values)
- Examples: Cell voltages, temperatures, current, power
- Storage: Circular buffer in RAM
- Transmission: ESP-NOW chunks or MQTT publish + WebSocket

**Historical Data** → Consider External Database
- Changes: Accumulated over time
- Frequency: Continuous logging
- Size: Unbounded (grows forever)
- Examples: Daily energy usage, cell degradation trends
- Storage: SD card, external database (InfluxDB, PostgreSQL)
- Transmission: MQTT to external system

### 5. Future-Proofing Recommendations
NVS write frequency becomes a concern**:
- Monitor actual write count with `prefs.putUInt("write_count", ++count)`
- If > 1000 writes/day, add dirty flag and batch saves
- Consider write coalescing (only save after 5-minute idle period)
- Current design: < 100 writes/year is well within limits
- Implement "cache valid" flag with timestamp

**If static data grows significantly** (> 1 KB total):
- Compress config sections (GZIP, LZ4)
- Implement delta encoding (only send changed fields)
- Split large sections into sub-sections

**If real-time data volume exceeds bandwidth**:
- Reduce sampling rate (e.g., 10 Hz → 1 Hz)
- Implement delta encoding (only send changed cells)
- Use compression (run-length encoding for similar values)
- Aggregate data on transmitter (send min/max/avg instead of all cells)

**If network becomes unreliable**:
- Add QoS levels to ESP-NOW messages
- Implement receiver acknowledgment for critical updates
- Add sequence numbers to detect packet loss
- Buffer last N beacons to detect missed updates

---

## Conclusion

### Current Problem
You have **three overlapping cache systems** that don't integrate:
1. Config Sync (full snapshot) - not used by web UI
2. Version Beacon - partially implemented
3. Individual requests - ad-hoc and racy

This causes the "MQTT config not cached" issue and requires manual request/retry logic.

### Recommended Solution
**Consolidate to a single Version Beacon-based cache system for static data:**
- Automatic synchronization via 15-second beacons
- On-demand section requests when vwith **NVS persistence** (write-through pattern)
- Web UI directly queries cache (instant display, no waiting)
- Symmetric architecture (both transmitter and receiver use NVS-backed cache)
- Runtime status updated in cache only (no NVS writes on receiver)
- Web UI directly queries cache (no waiting/retrying)
- Simple to extend for new config sections

**Separate architecture for real-time telemetry:**
- ESP-NOW chunked streaming OR MQTT publish for battery cell data
- WebSocket server for live web UI updates
- Circular buffer in RAM (no persistence)
- Chart.js real-time graphing
- Completely independent from static config system

**Option 3 (Hybrid) disconnection detection** ✅ CHOSEN:
- Combines beacon timeout (30s) + ESP-NOW send status
- Fast detection for range/radio issues (immediate)
- Catches transmitter hangs/crashes (30s timeout)
- Best balance of responsiveness and reliability
- Multi-layer detection for production robustness

### Key Benefits
1. ✅ **YES NVS on receiver** - write-through cache for instant page load, same pattern as transmitter
2. ✅ **Runtime status cache-only** - updated every 15s from beacon, no NVS writes
3. ✅ **Separate real-time data** - streaming system (ESP-NOW/MQTT + WebSocket), not cache system
4. ✅ **Static config versioned** - only request when version changes, auto-persist to NVS
5. ✅ **Symmetric architecture** - both sides use NVS-backed cache (easier to understand/maintain)
6. ✅ **Hybrid disconnection detection** - fast ESP-NOW failure detection + reliable beacon timeout
7. ✅ **Runtime status in beacon** - always fresh, updated every 15 seconds
8. ✅ **Metadata included** - MAC, firmware version, device name for identification
9. ✅ **Receiver manages own data** - receiver's IP, MAC, firmware version cached separately from transmitter data
10. ✅ **Dual data sources** - web UI displays both receiver identity AND transmitter status

### Next Steps
1. Implement Phase 1 immediate fixes (this week)
2. Review this document and architectural decisions
3. Choose ESP-NOW vs MQTT for real-time cell data
4. Proceed with Phases 2-5 based on priority
5## Next Steps
1. Implement Phase 1 immediate fixes (this week)
2. Review this document with team
3. Proceed with Phases 2-4 based on priority
4. Create issue tracking for each phase

---

**Document Version**: 1.0  
**Last Updated**: February 13, 2026  
**Author**: GitHub Copilot (Architectural Analysis)  
**Status**: Ready for Review
