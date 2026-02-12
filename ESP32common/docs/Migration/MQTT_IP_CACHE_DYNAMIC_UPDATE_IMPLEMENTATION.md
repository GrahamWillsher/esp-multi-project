# MQTT & IP Dynamic Cache Update - Implementation Plan

## Executive Summary

This document outlines the implementation required to dynamically update the receiver's cached transmitter data (MQTT connection status, IP address, etc.) after initial handshaking. Currently, static configuration is sent during initial ESP-NOW discovery, but runtime status changes (like MQTT connecting after boot) are not propagated to the receiver.

**Date**: February 10, 2026  
**Status**: Design & Implementation Plan  
**Priority**: Medium (UX Enhancement)

---

## Problem Statement

### Current Issues

1. **MQTT Connection Status Not Updated**
   - MQTT config is sent during initial handshake
   - `mqtt_connected` field sent reflects state at handshake time (usually `false` during boot)
   - When MQTT connects 2-3 seconds later, receiver cache is not updated
   - Web UI shows red dot (connecting) instead of green dot (connected)

2. **MQTT Enabled Checkbox Not Ticked**
   - `/transmitter/config` page shows unchecked MQTT enabled box
   - Suggests MQTT config is not being cached correctly
   - May be related to initial handshake timing

3. **IP Address Not Dynamically Updated**
   - If transmitter gets IP via DHCP after initial handshake, receiver doesn't know
   - Similar issue could occur with static IP changes

4. **General Pattern Needed**
   - Need mechanism for transmitter to push runtime status updates
   - Should work for any "slow-to-initialize" data (MQTT, IP, etc.)

### Root Cause Analysis

**Sequence of Events**:
```
T+0s:  Transmitter boots
T+1s:  ESP-NOW initialized, discovery starts
T+2s:  Receiver found, initial handshake (MQTT=disabled or enabled but not connected)
T+3s:  MQTT connects to broker (receiver unaware)
T+5s:  Ethernet gets DHCP IP (receiver might be unaware)
```

**Problem**: Initial handshake is a one-time snapshot. Runtime changes aren't propagated.

---

## Current System Architecture

### Transmitter Data Flow

```
Transmitter Boot
  ↓
ESP-NOW Init
  ↓
Channel Discovery → PROBE Messages
  ↓
Receiver Responds with ACK
  ↓
Initial Handshake Messages Sent:
  - METADATA (firmware version, device name)
  - NETWORK_CONFIG (IP, gateway, subnet, static/DHCP mode)
  - MQTT_CONFIG (enabled, server, port, credentials, connected=FALSE)
  - BATTERY_SETTINGS
  - POWER_PROFILE_LIST
  ↓
[MQTT Connects Later] ← NO UPDATE SENT TO RECEIVER
```

### Receiver Cache

**File**: `transmitter_manager.cpp`

**Cached Data**:
- MAC address
- Network config (IP, gateway, subnet, DNS, mode, version)
- MQTT config (enabled, server, port, credentials, **connected status**, version)
- Battery settings
- Power profiles
- Metadata (firmware version, build date)

**Cache Update Mechanism**: ESP-NOW message handlers call `TransmitterManager::store*()` methods

---

## Solution Design

### Approach 1: Version-Based Synchronization (Recommended ⭐)

**Concept**: Leverage existing version numbers to detect stale cache, request only changed sections

**Key Insight**: Each configuration section already has a version number:
- `mqtt_config_version`
- `network_config_version`
- `battery_settings_version`
- `power_profile_version`

**How It Works**:
```
Transmitter                           Receiver
    |                                     |
    |  Periodic Version Beacon (small)    |
    |------------------------------------>|
    |  [MQTT:v5, Network:v3, Battery:v2]  |
    |                                     |
    |                              Compare versions
    |                              Cache: MQTT:v4 ← OUT OF DATE!
    |                              Cache: Network:v3 ← OK
    |                                     |
    |  Request MQTT Config (v5)           |
    |<------------------------------------|
    |                                     |
    |  Send Full MQTT Config (v5)         |
    |------------------------------------>|
    |                              Update cache
```

**Message Types to Add**:
```cpp
// Periodic version beacon (very lightweight - only ~20 bytes)
MSG_VERSION_BEACON = 0x25

struct version_beacon_t {
    uint8_t type = MSG_VERSION_BEACON;
    uint32_t mqtt_config_version;
    uint32_t network_config_version;
    uint32_t battery_settings_version;
    uint32_t power_profile_version;
    bool mqtt_connected;            // Runtime status
    bool ethernet_connected;        // Runtime status
    uint8_t reserved[2];
} __attribute__((packed));

// Config request from receiver (if version mismatch detected)
MSG_CONFIG_REQUEST = 0x26

enum ConfigSection : uint8_t {
    CONFIG_MQTT = 0x01,
    CONFIG_NETWORK = 0x02,
    CONFIG_BATTERY = 0x03,
    CONFIG_POWER_PROFILE = 0x04
};

struct config_request_t {
    uint8_t type = MSG_CONFIG_REQUEST;
    ConfigSection section;
    uint32_t requested_version;  // Version we want
    uint8_t reserved[11];
} __attribute__((packed));
```

**Beacon Schedule**:
- Send immediately when any version changes (config saved)
- Send immediately when MQTT or Ethernet status changes
- Send periodic heartbeat every **15 seconds** (ensures receiver stays synced)
- With version-based approach, bandwidth is so minimal we can afford more frequent updates
- Minimal bandwidth: ~20 bytes per beacon vs. ~200+ bytes for full data

**Advantages**:
- ✅ **Extremely efficient**: Only sends version numbers (20 bytes vs 200+ bytes)
- ✅ **Faster updates**: 15s interval instead of 30-60s (thanks to minimal bandwidth)
- ✅ **Receiver pulls only what changed**: No redundant data transfer
- ✅ **Leverages existing infrastructure**: Version numbers already implemented
- ✅ **Handles all static config changes**: MQTT, network, battery, power profiles
- ✅ **Includes runtime status**: MQTT connected, Ethernet state in beacon
- ✅ **Self-healing**: Receiver detects stale cache and auto-updates
- ✅ **Minimal transmitter overhead**: Just send small beacon periodically

**Disadvantages**:
- ⚠️ Slightly more complex (request/response pattern)
- ⚠️ Up to 15s delay before receiver detects stale cache (acceptable for config data)

**Why This is Superior**:
1. **Bandwidth Efficient**: Version beacon is 90% smaller than full config data
2. **Smart Caching**: Receiver only requests what's actually changed
3. **Scalable**: Adding new config sections just adds 4 bytes to beacon
4. **Already Implemented**: Version tracking exists, just need to expose it

### Approach 2: Periodic Full Status Broadcast

**Concept**: Transmitter periodically sends lightweight status updates with runtime state

**Message**:
```cpp
struct runtime_status_t {
    uint8_t type = MSG_RUNTIME_STATUS;
    bool mqtt_connected;
    uint8_t current_ip[4];
    uint8_t current_gateway[4];
    bool ethernet_connected;
    uint32_t uptime_seconds;
} __attribute__((packed));
```

**Advantages**:
- ✅ Simple implementation
- ✅ Immediate runtime status updates

**Disadvantages**:
- ❌ Sends ~40 bytes every 30-60s regardless of changes
- ❌ Doesn't handle static config changes (MQTT server, network settings, etc.)
- ❌ Less efficient than version-based approach

### Approach 3: Poll-Based (Not Recommended)

**Concept**: Receiver periodically requests status from transmitter

**Disadvantages**:
- ❌ Adds request/response overhead
- ❌ Delayed updates
- ❌ More complex implementation
- ❌ Wastes bandwidth on redundant requests

---

## Recommended Implementation

**Use Approach 1 (Version-Based Synchronization)** - Most efficient and scalable:

### Transmitter Side Implementation

#### 1. Add Version Beacon Message Types

**File**: `espnow_common.h` (common library)

```cpp
// Add to MessageType enum
enum MessageType : uint8_t {
    // ... existing types ...
    MSG_VERSION_BEACON = 0x25,   // Periodic version sync beacon
    MSG_CONFIG_REQUEST = 0x26,   // Request specific config section
};

// Configuration section identifiers
enum ConfigSection : uint8_t {
    CONFIG_MQTT = 0x01,
    CONFIG_NETWORK = 0x02,
    CONFIG_BATTERY = 0x03,
    CONFIG_POWER_PROFILE = 0x04
};

// Lightweight version beacon (only ~20 bytes)
struct version_beacon_t {
    uint8_t type = MSG_VERSION_BEACON;
    uint32_t mqtt_config_version;
    uint32_t neVersion Beacon Manager

**New File**: `transmitter/src/version_beacon_manager.h`

```cpp
#pragma once
#include <Arduino.h>
#include <espnow_common.h>

class VersionBeaconManager {
public:
    static VersionBeaconManager& instance();
    
    // Initialize and start periodic beacons
    void init();
    
    // Notify of state changes (triggers immediate beacon)
    void notify_mqtt_connected(bool connected);
    void notify_ethernet_changed(bool connected);
    void notify_config_version_changed(ConfigSection section);
    
    // Periodic update (called from task or loop)
    void update();
    
    // Handle config request from receiver
    void handle_config_request(const config_request_t* request, const uint8_t* sender_mac);
    
private:
    VersionBeaconManager() = default;
    
    void send_version_beacon(bool force = false);
    void send_config_section(ConfigSection section, const uint8_t* receiver_mac);
    bool has_runtime_state_changed();
    uint32_t get_config_version(ConfigSection section);
    
    // Current runtime state
    bool mqtt_connected_{false};
    bool ethernet_connected_{false};
    
    // Previous runtime state (for change detection)
    bool prev_mqtt_connected_{false};
    bool prev_ethernet_connected_{false};
    
    // Timing
    uint32_t last_beacon_ms_{0};
    static constexpr uint32_t PERIODIC_INTERVAL_MS = 15000;  // 15 seconds
    static constexpr uint32_t MIN_BEACON_INTERVAL_MS = 1000; // Rate limit
};
```

**Implementation**: `version_beacon_manager.cpp`

```cpp
#include "version_beacon_manager.h"
#include <esp_now.h>
#include <ETH.h>
#include "../config/logging_config.h"
#include "message_handler.h"
#include "../settings/settings_manager.h"
#include "../network/mqtt_manager.h"
#include "../network/ethernet_manager.h"

VersionBeaconManager& VersionBeaconManager::instance() {
    static VersionBeaconManager instance;
    return instance;
}

void VersionBeaconManager::init() {
    LOG_INFO("[VERSION_BEACON] Manager initialized");
    
    // Send initial beacon immediately
    send_version_beacon(true);
}

void VersionBeaconManager::notify_mqtt_connected(bool connected) {
    if (mqtt_connected_ != connected) {
        mqtt_connected_ = connected;
        LOG_INFO("[VERSION_BEACON] MQTT state changed: %s", 
                 connected ? "CONNECTED" : "DISCONNECTED");
        send_version_beacon(true);  // Force immediate beacon
    }
}

void VersionBeaconManager::notify_ethernet_changed(bool connected) {
    if (ethernet_connected_ != connected) {
        ethernet_connected_ = connected;
        LOG_INFO("[VERSION_BEACON] Ethernet state changed: %s", 
                 connected ? "CONNECTED" : "DISCONNECTED");
        send_version_beacon(true);
    }
}

void VersionBeaconManager::notify_config_version_changed(ConfigSection section) {
    LOG_INFO("[VERSION_BEACON] Config version changed: section=%d", (int)section);
    send_version_beacon(true);  // Force immediate beacon
}

void VersionBeaconManager::update() {
    uint32_t now = millis();
    
    // Periodic heartbeat beacon
    if (now - last_beacon_ms_ >= PERIODIC_INTERVAL_MS) {
        send_version_beacon(false);  // Don't force if no changes
    }
}

bool VersionBeaconManager::has_runtime_state_changed() {
    // Check MQTT state
    if (mqtt_connected_ != prev_mqtt_connected_) return true;
    
    // Check Ethernet state
    if (ethernet_connected_ != prev_ethernet_connected_) return true;
    
    return false;
}

uint32_t VersionBeaconManager::get_config_version(ConfigSection section) {
    switch (section) {
        case CONFIG_MQTT:
            // Get MQTT config version from settings manager
            return SettingsManager::instance().get_mqtt_config_version();
            
        case CONFIG_NETWORK:
            // Get network config version from ethernet manager
            return EthernetManager::instance().get_network_config_version();
            
        case CONFIG_BATTERY:
            // Get battery settings version
            return SettingsManager::instance().get_battery_settings_version();
            
        case CONFIG_POWER_PROFILE:
            // Get power profile version
            return SettingsManager::instance().get_power_profile_version();
            
        default:
            return 0;
    }
}

void VersionBeaconManager::send_version_beacon(bool force) {
    uint32_t now = millis();
    
    // Rate limiting (except for forced beacons)
    if (!force && now - last_beacon_ms_ < MIN_BEACON_INTERVAL_MS) {
        return;
    }
    
    // Update current runtime state
    mqtt_connected_ = MqttManager::instance().is_connected();
    ethernet_connected_ = EthernetManager::instance().is_connected();
    
    // Check if anything changed (unless forced)
    if (!force && !has_runtime_state_changed()) {
        return;  // No changes, skip beacon
    }
    
    // Build version beacon
    version_beacon_t beacon;
    beacon.mqtt_config_version = get_config_version(CONFIG_MQTT);
    beacon.network_config_version = get_config_version(CONFIG_NETWORK);
    beacon.battery_settings_version = get_config_version(CONFIG_BATTERY);
    beacon.power_profile_version = get_config_version(CONFIG_POWER_PROFILE);
    beacon.mqtt_connected = mqtt_connected_;
    beacon.ethernet_connected = ethernet_connected_;
    
    // Send via ESP-NOW to receiver
    if (EspnowMessageHandler::instance().is_receiver_connected()) {
        esp_err_t result = esp_now_send(
            receiver_mac,
            (const uint8_t*)&beacon,
            sizeof(beacon)
        );
        
        if (result == ESP_OK) {
            LOG_DEBUG("[VERSION_BEACON] Sent: MQTT:v%u, Net:v%u, Batt:v%u, Profile:v%u (MQTT:%s, ETH:%s)",
                     beacon.mqtt_config_version,
                     beacon.network_config_version,
                     beacon.battery_settings_version,
                     beacon.power_profile_version,
                     beacon.mqtt_connected ? "CONN" : "DISC",
                     beacon.ethernet_connected ? "UP" : "DOWN");
        } else {
            LOG_ERROR("[VERSION_BEACON] Send failed: %s", esp_err_to_name(result));
        }
    }version_beacon_manager.h"

bool MqttManager::connect() {
    // ... existing connect code ...
    
    if (client_.connect(/* ... */)) {
        LOG_INFO("[MQTT] Connected to broker");
        
        // ✅ NOTIFY VERSION BEACON MANAGER (triggers immediate beacon)
        VersionBeaconManager::instance().notify_mqtt_connected(true);
        
        return true;
    }
    
    return false;
}

void MqttManager::loop() {
    if (!client_.loop()) {
        if (was_connected_) {
            LOG_WARN("[MQTT] Connection lost");
            
            // ✅ NOTIFY VERSION BEACON MANAGER (triggers immediate beacon)
            VersionBeaconRK: {
            // Build and send network config message
            network_config_t net_msg;
            net_msg.type = msg_network_config;
            // ... populate from EthernetManager
            esp_now_send(receiver_mac, (const uint8_t*)&net_msg, sizeof(net_msg));
            break;
        }
        
        case CONFIG_BATTERY: {
            // Build and send battery settings message
            battery_settings_t batt_msg;
            batt_msg.type = msg_battery_settings;
            /version_beacon_manager.h"

void EthernetManager::on_ethernet_event(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_ETH_GOT_IP:
            LOG_INFO("[ETH] IP Address: %s", ETH.localIP().toString().c_str());
            
            // ✅ NOTIFY VERSION BEACON MANAGER
            // IP change triggers network config version increment
            VersionBeaconManager::instance().notify_config_version_changed(CONFIG_NETWORK);
            break;
            
        case ARDUINO_EVENT_ETH_CONNECTED:
            LOG_INFO("[ETH] Link Connected");
            VersionBeaconManager::instance().notify_ethernet_changed(true);
            break;
            
        case ARDUINO_EVENT_ETH_DISCONNECTED:
            LOG_WARN("[ETH] Link Disconnected");
            VersionBeaconion = get_config_version(request->section);
    
    if (current_version != request->requested_version) {
        LOG_WARN("[VERSION_BEACON] Version mismatch: requested v%u, current v%u",
                 request->requested_version, current_version);
        // Send anyway - receiver wants to update
    }
    
    // Send the requested config section
    send_config_section(request->section, sender_mac)
        
        if (result == ESP_OK) {
            LOG_INFO("[RUNTIME_STATUS] Status sent (MQTT:%s, IP:%d.%d.%d.%d, ETH:%s)",
                     status.mqtt_connected ? "CONN" : "DISC",
                     status.current_ip[0], status.current_ip[1], 
                     status.current_ip[2], status.current_ip[3],
                     status.ethernet_connected ? "UP" : "DOWN");
        } else {
            LOG_ERROR("[RUNTIME_STATUS] Send failed: %s", esp_err_to_name(result));
        }
    }
    
    // UIntegrate with Settings Manager

**File**: `transmitter/src/settings/settings_manager.cpp`

```cpp
#include "../version_beacon_manager.h"

bool SettingsManager::save_mqtt_config(const MqttConfig& config) {
    // ... existing save code ...
    
    if (prefs.end()) {
        mqtt_config_version_++;  // Increment version
        LOG_INFO("[SETTINGS] MQTT config saved (version %u)", mqtt_config_version_);
        
        // ✅ NOTIFY VERSION BEACON MANAGER
        VersionBeaconManager::instance().notify_config_version_changed(CONFIG_MQTT);
        
        return true;
    }
    return false;
}

bool SettingsManager::save_network_config(const NetworkConfig& config) {
    // ... existing save code ...
    
    if (success) {
        network_config_verss

**File**: `receiver/src/espnow_tasks.cpp`

```cpp
// Add to message routing
void on_espnow_recv_impl(const uint8_t *mac, const uint8_t *data, int len) {
    // ... existing handlers ...
    
    // Version beacon received - check for stale cache
    if (len == sizeof(version_beacon_t) && data[0] == MSG_VERSION_BEACON) {
        const version_beacon_t* beacon = (const version_beacon_t*)data;
        
        LOG_DEBUG("[ESPNOW_RX] Version beacon: MQTT:v%u, Net:v%u, Batt:v%u, Profile:v%u",
                  beacon->mqtt_config_version,
                  beacon->network_config_version,
                  beacon->battery_settings_version,
                  beacon->power_profile_version);
        
        // Update runtime status (MQTT connected, Ethernet connected)
        TransmitterManager::updateRuntimeStatus(
            beacon->mqtt_connected,
            beacon->ethernet_connected
        );
        
        // Check each config section version
        bool request_sent = false;
        
        // Check MQTT config version
        if (TransmitterManager::isMqttConfigKnown()) {
            uint32_t cached_version = TransmitterManager::getMqttConfigVersion();
            if (cached_version != beacon->mqtt_config_version) {
                LOG_INFO("[ESPNs

**File**: `receiver/lib/webserver/utils/transmitter_manager.h`

```cpp
class TransmitterManager {
public:
    // ... existing methods ...
    
    // Update runtime status from version beacon
    static void updateRuntimeStatus(bool mqtt_conn, bool eth_conn);
    
    // Get version numbers for cache validation
    static uint32_t getMqttConfigVersion();
    static uint32_t getNetworkConfigVersion();
    static uint32_t getBatterySettingsVersion();
    static uint32_t getPowerProfileVersion();
    
    // Get last beacon timestamp
    static uint32_t getLastBeaconTime();
    
private:
    static uint32_t last_beacon_ms;
};
```

**Implementation**: `transmitter_manager.cpp`

```cpp
uint32_t TransmitterManager::last_beacon_ms = 0;

void TransmitterManager::updateRuntimeStatus(bool mqtt_conn, bool eth_conn) {
    // Update MQTT connection status (keep existing config data)
    mqtt_connected = mqtt_conn;
    
    // Update Ethernet connection status
    // (Note: IP address is in network config, updated separately when version changes)
    
    // Update beacon timestamp
    last_beacon_ms = millis();
    
    Serial.printf("[TX_MGR] Runtime status updated: MQTT=%s, ETH=%s\n",
                  mqtt_conn ? "CONNECTED" : "DISCONNECTED",
                  eth_conn ? "UP" : "DOWN");
}

uint32_t TransmitterManager::getMqttConfigVersion() {
    return mqtt_config_version;
}

uint32_t TransmitterManager::getNetworkConfigVersion() {
    return network_config_version;
}

uint32_t TransmitterManager::getBatterySettingsVersion() {
    return battery_settings_version;  // Assuming this exists
}

uint32_t TransmitterManager::getPowerProfileVersion() {
    return power_profile_version;  // Assuming this exists
}

uint32_t TransmitterManager::getLastBeaconTime() {
    return last_beacon Config request sent: section=%d, version=%u", (int)section, version);
    } else {
        LOG_ERROR("[ESPNOW_TX] Config request failed: %s", esp_err_to_name(result));
    }
}
```

**File**: `receiver/src/espnow_tasks.cpp` (transmitter side - handle config requests)

```cpp
// In transmitter message handler
void on_espnow_recv_impl(const uint8_t *mac, const uint8_t *data, int len) {
    // ... existing handlers ...
    
    // Config request from receiver
    if (len == sizeof(config_request_t) && data[0] == MSG_CONFIG_REQUEST) {
        const config_request_t* request = (const config_request_t*)data;
        
        LOG_INFO("[ESPNOW_RX] Config request: section=%d, version=%u",
                 (int)request->section, request->requested_version);
        
        // Handle the config request
        VersionBeaconManager::instance().handle_config_request(request, mac

void loop() {
    // ... existing loop code ...
    
    // Periodic version beacon updates (30s heartbeat)
    VersionBeacon
    if (client_.connect(/* ... */)) {
        LOG_INFO("[MQTT] Connected to broker");
        
        // ✅ NOTIFY RUNTIME STATUS MANAGER
        RuntimeStatusManager::instance().notify_mqtt_connected(true);
        
        return true;
    }
    
    return false;
}

void MqttManager::loop() {
    if (!client_.loop()) {
        if (was_connected_) {
            LOG_WARN("[MQTT] Connection lost");
            
            // ✅ NOTIFY RUNTIME STATUS MANAGER
            RuntimeStatusManager::instance().notify_mqtt_connected(false);
            
            was_connected_ = false;
        }
    }
}
```

#### 4. Integrate with Ethernet Manager

**File**: `transmitter/src/network/ethernet_manager.cpp`

```cpp
#include "../runtime_status_manager.h"

void EthernetManager::on_ethernet_event(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_ETH_GOT_IP:
            LOG_INFO("[ETH] IP Address: %s", ETH.localIP().toString().c_str());
            
            // ✅ NOTIFY RUNTIME STATUS MANAGER
            RuntimeStatusManager::instance().notify_ip_changed();
            break;
            
        case ARDUINO_EVENT_ETH_CONNECTED:
            LOG_INFO("[ETH] Link Connected");
            RuntimeStatusManager::instance().notify_ethernet_changed(true);
            break;
            
        case ARDUINO_EVENT_ETH_DISCONNECTED:
            LOG_WARN("[ETH] Link Disconnected");
            RuntimeStatusManager::instance().notify_ethernet_changed(false);
            break;
    }
}
```

#### 5. Add to Main Loop

**File**: `transmitter/src/main.cpp`

```cpp
#include "runtime_status_manager.h"

void setup() {
    // ... existing setup ...
    
    // Initialize runtime status manager (after MQTT, network init)
    RuntimeStatusManager::instance().init();
}

void loop() {
    // ... existing loop code ...
    
    // Periodic runtime status updates
    RuntimeStatusManager::instance().update();
    
    vTaskDelay(pdMS_TO_TICKS(1000));
}
```

### Receiver Side Implementation

#### 1. Add Message Handler

**File**: `receiver/src/espnow_tasks.cpp`

```cpp
// Add to message routing
void on_espnow_recv_impl(const uint8_t *mac, const uint8_t *data, int len) {
    // ... existing handlers ...
    
    // Runtime status update
    if (len == sizeof(runtime_status_t) && data[0] == MSG_RUNTIME_STATUS) {
        const runtime_status_t* status = (const runtime_status_t*)data;
        
        LOG_INFO("[ESPNOW_RX] Runtime status: MQTT:%s, IP:%d.%d.%d.%d",
                 status->mqtt_connected ? "CONN" : "DISC",
                 status->current_ip[0], status->current_ip[1],
                 status->current_ip[2], status->current_ip[3]);
        
        // Update transmitter cache
        TransmitterManager::updateRuntimeStatus(
            status->mqtt_connected,
            status->current_ip,
            status->current_gateway,
            status->ethernet_connected
        );
        
        return;
    }
}
```

#### 2. Add Cache Update Method

**File**: `receiver/lib/webserver/utils/transmitter_manager.h`

```cpp
class TransmitterManager {
public:
    // ... existing methods ...
    
    // Update runtime status (called when runtime_status_t received)
    static void updateRuntimeStatus(bool mqtt_conn, const uint8_t* ip, 
                                    const uint8_t* gateway, bool eth_conn);
    
    // Get last update timestamp
    static uint32_t getLastStatusUpdate();
    
private:
    static uint32_t last_status_update_ms;
};
```

**Implementation**: `transmitter_manager.cpp`

```cpp
uint32_t TransmitterManager::last_status_update_ms = 0;

void TransmitterManager::updateRuntimeStatus(bool mqtt_conn, const uint8_t* ip,
                                             const uint8_t* gateway, bool eth_conn) {
    // Update MQTT connection status (keep existing config)
    mqtt_connected = mqtt_conn;
    
    // Update current IP and gateway
    if (ip) {
        memcpy(current_ip, ip, 4);
    }
    if (gateway) {
        memcpy(current_gateway, gateway, 4);
    }
    
    // Update timestamp
    last_status_update_ms = millis();
    
    Serial.printf("[TX_MGR] Runtime status updated: MQTT=%s, IP=%d.%d.%d.%d\n",
                  mqtt_conn ? "CONNECTED" : "DISCONNECTED",
                  current_ip[0], current_ip[1], current_ip[2], current_ip[3]);
}

uint32_t TransmitterManager::getLastStatusUpdate() {
    return last_status_update_ms;
}
```

#### 3. Update Web UI to Show Live Status

**File**: `receiver/lib/webserver/pages/settings_page.cpp`

**Add MQTT Status Indicator**:

```html
<!-- In MQTT Configuration section -->
<div class='settings-card'>
    <h3>
        MQTT Configuration 
        <span id='mqttStatusDot' class='status-dot' title='MQTT Status'></span>
    </h3>
    <!-- ... existing MQTT fields ... -->
</div>
```

**Add CSS**:

```css
<style>
.status-dot {
    display: inline-block;
    width: 12px;
    height: 12px;
    border-radius: 50%;
    margin-left: 8px;
    vertical-align: middle;
}
.status-dot.connected {
    background-color: #28a745;  /* Green */
    box-shadow: 0 0 8px #28a745;
}
.status-dot.connecting {
    background-color: #FF9800;  /* Orange */
    animation: pulse 1.5s ease-in-out infinite;
}
.status-dot.disconnected {
    background-color: #dc3545;  /* Red */
}
@keyframes pulse {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.5; }
}
</style>
```

**Add JavaScript Status Update**:

```javascript
// Update MQTT status indicator
function updateMqttStatusIndicator(connected, enabled) {
    const dot = document.getElementById('mqttStatusDot');
    if (!dot) return;
    
    dot.className = 'status-dot';
    
    if (!enabled) {
        dot.style.display = 'none';
        dot.title = 'MQTT Disabled';
    } else {
        dot.style.display = 'inline-block';
        if (connected) {
            dot.classList.add('connected');
            dot.title = 'MQTT Connected';
        } else {
            dot.classList.add('connecting');
            dot.title = 'MQTT Connecting...';
        }
    }
}

// Call when loading MQTT config
function populateMqttConfig(data) {
    // ... existing population code ...
    
    // Update status indicator
    updateMqttStatusIndicator(data.connected, data.enabled);
}
version_beacon_manager.h` - Version beacon manager interface
2. `src/version_beacon live updates (optional enhancement)
if (typeof eventSource !== 'undefined') {
    eventSource.addEventListener('mqtt_status', function(e) {
        const data = JSON.parse(e.data);MQTT connection state notifications
4. `src/network/ethernet_manager.cpp` - Add Ethernet state notifications
5. `src/settings/settings_manager.cpp` - Add config version change notifications
6. `src/settings/settings_manager.h` - Add version getter methods
7. `src/main.cpp` - Initialize and update version beacon
}
```

#### 4. Fix MQTT Enabled Checkbox Issue

**Root Cause**: Initial handshake sends MQTT config, but cache might not be updated correctly.

**Investigation Required**:
1. Check if `populateMqttConfig()` is called on page load
2. Verify `api_get_mqtt_config_handler()` returns correct `enabled` field
3. Check if checkbox `checked` attribute is set correctly

**Quick Fix** (if needed):

```javascript
// In loadMqttConfig() function
async function loadMqttConfig() {
    try {
        const response = await fetch('/api/get_mqtt_config');
        const data = await response.json();
        
        console.log('[DEBUG] MQTT config data:', data);  // Add debugging
        
        if (data.success) {
            populateMqttConfig(data);
        } else {
            // Request from transmitter if cache empty
            await requestMqttConfigFromTransmitter();
        }
    } catch (error) {
        console.error('Failed to load MQTT config:', error);
    }
}

// Ensure this is called on page load
window.addEventListener('load', function() {
    loadMqttConfig();
    // ... other init functions ...
});
```

---

## Additional Improvements

### 1. Server-Sent Events (SSE) for Real-Time Updates

**Purpose**: Push live updates to web UI without polling

**Implementation**:

**Transmitter**: Existing SSE endpoint sends MQTT status
**Receiver**: Add SSE event for transmitter runtime status

```cpp
// In receiver SSE handler
void sse_task(void *params) {
    while (true) {
        // ... existing events ...
        
        // Transmitter runtime status
        if (TransmitterManager::isMACKnown()) {
            snprintf(buf, sizeof(buf),
                "event: mqtt_status\n"
                "data: {\"connected\":%s,\"enabled\":%s}\n\n",
                TransmitterManager::isMqttConnected() ? "true" : "false",
                TransmitterManager::isMqttEnabled() ? "true" : "false"
            );
            httpd_sse_send(client, buf);
        }
        
        delay(2000);
8. `espnow_common_utils/espnow_common.h` - Add `MSG_VERSION_BEACON`, `MSG_CONFIG_REQUEST`, structures and enums
}
```
9. `src/espnow_tasks.cpp` - Add version beacon handler and config request logic
10. `lib/webserver/utils/transmitter_manager.h` - Add version getters and runtime status update
11. `lib/webserver/utils/transmitter_manager.cpp` - Implement version comparison and cache update
12. `lib/webserver/pages/settings_page.cpp` - Add status indicator UI
13
**Solution**: Add timestamp to each cache entry

```cpp
// In TransmitterManager
static uint32_t mqtt_config_last_update_ms;
static uint32_t network_config_last_update_ms;
static uint32_t runtime_status_last_update_ms;

// Add staleness check
static bool is_mqtt_config_stale() {
    return (millis() - mqtt_config_last_update_ms) > 120000;  // 2 minutes
}

// Display warning in UI if stale
```
## Advantages of Version-Based Approach

### Bandwidth Efficiency

**Traditional Periodic Broadcast** (30s interval):
```
Every 30s: Send ~200 bytes (full MQTT + Network + Battery + Power Profile)
Per hour: 200 × 120 = 24,000 bytes = 24 KB/hour
```

**Version-Based Beacon** (15s interval - faster updates!):
```
Every 15s: Send ~20 bytes (just version numbers + runtime status)
Per hour: 20 × 240 = 4,800 bytes = 4.8 KB/hour
Plus occasional config requests when changed (rare)
```

**Savings**: 80% reduction in bandwidth usage **with 2x faster update rate**!

**Key Insight**: Because beacons are so lightweight, we can send them **twice as frequently** (15s vs 30s) and still use 80% less bandwidth than the old approach. This means:
- ✅ Faster MQTT status updates (max 15s delay vs 30s)
- ✅ Quicker config sync detection  
- ✅ Still extremely efficient bandwidth usage

### Scalability

Adding new config sections:
- **Traditional**: Increases periodic broadcast size significantly
- **Version-Based**: Only adds 4 bytes to beacon (version number)

### Self-Healing

If receiver misses a beacon:
- Next beacon (15s later) will still detect version mismatch
- Receiver automatically requests missing data
- No permanent data loss

### Change Detection

Version numbers provide clear change tracking:
- Easy to debug config sync issues
- Can log version history
- Can display "last updated" timestamps in UI

## Validation & Testing

### Test 1: Version Beacon Transmission

**Steps**:
1. Boot transmitter
2. Monitor serial output
3. **Expected**: Version beacon sent every 15s with current versions

**Validation**:
```
[VERSION_BEACON] Sent: MQTT:v5, Net:v3, Batt:v2, Profile:v1 (MQTT:CONN, ETH:UP)
```

### Test 2: MQTT Connection Status Update

**Steps**:
1. Transmitter boots (MQTT not yet connected)
2. Initial beacon shows `MQTT:DISC`
3. MQTT connects 3 seconds later
4. **Expected**: Immediate beacon sent with `MQTT:CONN`

**Validation**:
- Receiver shows green dot within 1-2 seconds of MQTT connection
- Event-driven update (no waiting for next periodic beacon)

### Test 3: Config Change Detection

**Steps**:
1. Change MQTT server via web UI
2. Save config (version increments: v5 → v6)
3. **Expected**: Immediate beacon sent with new version
4. Receiver detects version mismatch
5. Receiver requests MQTT config v6
6. Transmitter sends full MQTT config

**Validation**:
```
[RECEIVER] MQTT config stale: cached v5, current v6 - requesting update
[TRANSMITTER] Config request received: section=MQTT, version=6
[TRANSMITTER] Sending MQTT config v6
[RECEIVER] MQTT config updated to v6
```

### Test 4: Multiple Stale Configs

**Steps**:
1. Receiver offline for 10 minutes
2. During offline period:
   - MQTT config changed (v5 → v6)
   - Network config changed (v3 → v4)
   - Battery settings unchanged (v2)
3. Receiver comes back online
4. Receives version beacon
5. **Expected**: Requests MQTT v6 and Network v4 simultaneously

**Validation**:
- Both config sections updated
- Battery settings not re-requested (already current)

### Test 5: Lost Beacon Recovery

**Steps**:
1. Normal operation with synced cache
2. Manually drop 3-5 beacons (simulate ESP-NOW packet loss)
3. **Expected**: Next beacon (15s later) still maintains sync
4. No permanent data loss
5. Even with packet loss, sync recovers quickly due to frequent beacons

### Test 6: Bandwidth Usage

**Steps**:
1. Monitor ESP-NOW traffic for 1 hour
2. Count beacon messages sent
3. Count config request/response pairs
4. **Expected**:
   - 240 beacons × 20 bytes = 4.8 KB (15s interval)
   - 0-2 config updates (only if configs change)
   - Total < 8 KB/hour
   - Still 70-80% less than traditional broadcast approach

## Future Enhancements

1. **Version History**: Store last 10 version changes with timestamps
2. **Uptime Display**: Include transmitter uptime in beacon
3. **Connection Quality Metrics**: Track beacon delivery success rate
4. **Selective Requests**: Receiver can request specific config fields instead of full section
5. **Compression**: Use delta encoding for config updates
6. **Alert on Staleness**: Show warning if no beacon received in 60 seconds (4 missed beacons)
7. **Adaptive Interval**: Reduce to 10s when receiver actively viewing config page
function updateMqttStatusIndicator(connected, enabled) {
    const dot = document.getElementById('mqttStatusDot');
    if (!dot) return;
    
    // Check if data is recent (within last 2 minutes)
    const lastUpdate = getLastStatusUpdate();  // From API
    const isStale = (Date.now() - lastUpdate) > 120000;
    
    if (isStale) {
        dot.classList.add('disconnected');
        dot.title = 'Status Unknown (No Recent Update)';
    } else {
        // Normal status update
        // ...
    }
}
```

---

## Testing Plan

### Test 1: MQTT Connection Status Update

**Steps**:
1. Boot transmitter with MQTT disabled
2. Verify receiver shows MQTT disabled (unchecked box)
3. Enable MQTT via web UI, save
4. Reboot transmitter
5. **Expected**: Receiver shows connecting (orange dot) immediately
6. Wait 2-3 seconds for MQTT to connect
7. **Expected**: Dot turns green (connected)

### Test 2: MQTT Disconnection

**Steps**:
1. With transmitter connected to MQTT
2. Stop MQTT broker
3. **Expected**: Within 30-60s, receiver shows orange/red dot

### Test 3: IP Address Change

**Steps**:
1. Transmitter using DHCP
2. Release/renew DHCP lease (or change static IP)
3. **Expected**: Receiver cache updates with new IP within 1-2 seconds

### Test 4: Periodic Heartbeat

**Steps**:
1. Normal operation
2. Monitor serial output
3. **Expected**: See runtime status send every 60 seconds (only if state changed or forced)

### Test 5: Cache Persistence Across Receiver Reboot

**Steps**:
1. Transmitter running, MQTT connected
2. Reboot receiver
3. **Expected**: Last known status shown (might be stale until next update)

---

## Migration Path

### Phase 1: Core Implementation (1-2 hours)
- [ ] Add `runtime_status_t` message type to common library
- [ ] Implement `RuntimeStatusManager` on transmitter
- [ ] Add receiver message handler
- [ ] Update `TransmitterManager` cache methods

### Phase 2: Integration (1 hour)
- [ ] Hook MQTT connect/disconnect events
- [ ] Hook Ethernet IP change events
- [ ] Add periodic update to main loop

### Phase 3: Web UI Enhancement (1 hour)
- [ ] Add status dot indicator
- [ ] Fix MQTT enabled checkbox issue
- [ ] Add CSS animations
- [ ] Add SSE real-time updates (optional)

### Phase 4: Testing & Validation (1 hour)
- [ ] Test all scenarios from testing plan
- [ ] Verify no regressions
- [ ] Monitor ESP-NOW bandwidth usage

**Total Estimated Time**: 4-5 hours

---

## Files to Modify/Create

### New Files (Transmitter)
1. `src/runtime_status_manager.h` - Runtime status manager interface
2. `src/runtime_status_manager.cpp` - Implementation

### Modified Files (Transmitter)
3. `src/network/mqtt_manager.cpp` - Add status notifications
4. `src/network/ethernet_manager.cpp` - Add IP change notifications
5. `src/main.cpp` - Initialize and update runtime status manager

### Modified Files (Common Library)
6. `espnow_common_utils/espnow_common.h` - Add `MSG_RUNTIME_STATUS` and `runtime_status_t`

### Modified Files (Receiver)
7. `src/espnow_tasks.cpp` - Add runtime status message handler
8. `lib/webserver/utils/transmitter_manager.h` - Add updateRuntimeStatus()
9. `lib/webserver/utils/transmitter_manager.cpp` - Implement cache update
10. `lib/webserver/pages/settings_page.cpp` - Add status indicator UI
11. `lib/webserver/api/api_handlers.cpp` - Ensure proper cache return (debug checkbox issue)

---

## Success Criteria

✅ **MQTT connection status updates in real-time** (within 1-2 seconds)  
✅ **Web UI shows green dot when MQTT connected**, orange when connecting, red when disconnected  
✅ **MQTT enabled checkbox is checked correctly** on page load  
✅ **IP address updates dynamically** when DHCP renews or static IP changes  
✅ **No significant increase in ESP-NOW bandwidth** (<1% overhead)  
✅ **System remains stable** during network disruptions  
✅ **Graceful handling of old firmware** (without runtime status support)

---

## Future Enhancements

1. **Status History**: Store last 10 status changes with timestamps
2. **Uptime Display**: Show transmitter uptime in web UI
3. **Connection Quality Metrics**: Track ESP-NOW message success rate
4. **Automatic Retry**: If status update fails, retry with exponential backoff
5. **Status on Demand**: Add API endpoint to force status refresh
6. **Alert on Staleness**: Show warning if no status update in 5 minutes

---

**Document Created**: February 10, 2026  
**Author**: ESP-NOW System Review  
**Status**: Ready for Implementation  
**Estimated Effort**: 4-5 hours for complete implementation and testing
