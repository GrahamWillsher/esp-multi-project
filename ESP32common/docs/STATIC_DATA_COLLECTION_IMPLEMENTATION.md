# Static Data Collection Implementation Plan
**Version:** 1.0  
**Date:** February 4, 2026  
**Purpose:** Implement reliable static configuration synchronization between Receiver (Device 2 - Master) and Transmitter (Device 1 - Slave)

---

## ⚠️ Important: Scope - Static Configuration Only

**This document covers STATIC CONFIGURATION SYNCHRONIZATION only** (MQTT server/port, network settings, battery config, etc.).

**For runtime status handling (connection states, keep-alive), see:**
- [ESPNOW_HEARTBEAT.md](../docs/ESPNOW_HEARTBEAT.md) - Heartbeat protocol for connection detection (10s interval, 90s timeout)
- [PHASE4_VERSION_BEACON_IMPLEMENTATION_COMPLETE.md](../PHASE4_VERSION_BEACON_IMPLEMENTATION_COMPLETE.md) - Runtime status broadcasting (MQTT/Ethernet connected, 15s beacons)
- [STATIC_DATA_CACHE_ARCHITECTURE_REVIEW.md](../STATIC_DATA_CACHE_ARCHITECTURE_REVIEW.md) - Overall cache architecture and data flow

---

## Executive Summary

This document outlines the implementation of a professional-grade static configuration synchronization system between the ESP-NOW receiver (master) and transmitter (slave) devices. The system follows industry best practices used by Bosch, Sonoff, Philips Hue, and Matter devices.

**Key Pattern:** "Full Snapshot + Delta Updates"

**Roles:**
- **Device 2 (Receiver - Master):** LilyGo T-Display-S3 - displays and requests configuration
- **Device 1 (Transmitter - Slave):** ESP32-POE-ISO - holds authoritative configuration data

---

## Table of Contents

1. [System Architecture](#system-architecture)
2. [Configuration Data Structure](#configuration-data-structure)
3. [Message Protocol](#message-protocol)
4. [Implementation Changes](#implementation-changes)
5. [Retry and Reliability Mechanisms](#retry-and-reliability-mechanisms)
6. [Code Changes Required](#code-changes-required)

---

## 1. System Architecture

### Communication Flow

```
┌─────────────────────────────────┐         ┌─────────────────────────────────┐
│  Device 2 (Receiver - Master)   │         │  Device 1 (Transmitter - Slave) │
│  LilyGo T-Display-S3            │         │  ESP32-POE-ISO                  │
│                                 │         │                                 │
│  • Displays configuration       │         │  • Holds authoritative config   │
│  • Requests full snapshots      │         │  • Sends full snapshots         │
│  • Receives delta updates       │         │  • Sends delta updates          │
│  • Acknowledges updates         │         │  • Retries until ACK            │
└─────────────────────────────────┘         └─────────────────────────────────┘
         │                                               │
         │  1. REQUEST_FULL_CONFIG                      │
         │──────────────────────────────────────────────>│
         │                                               │
         │  2. FULL_CONFIG_SNAPSHOT (all sections)      │
         │<──────────────────────────────────────────────│
         │                                               │
         │  3. ACK (version 1)                          │
         │──────────────────────────────────────────────>│
         │                                               │
         │  ... time passes, config changes ...         │
         │                                               │
         │  4. UPDATE (mqtt/server: 192.168.1.222)      │
         │<──────────────────────────────────────────────│
         │                                               │
         │  5. ACK (version 2)                          │
         │──────────────────────────────────────────────>│
```

### When Full Snapshot is Needed

1. **Device 2 boots up** - First connection to Device 1
2. **Device 1 reboots** - New session starts
3. **Connection re-established** after long disconnection
4. **Version mismatch detected** - Resync required
5. **Multiple failed delta updates** - Fallback to full sync

---

## 2. Configuration Data Structure

### Configuration Sections (Subsections)

The configuration is organized into logical sections to enable granular delta updates:

```cpp
enum ConfigSection : uint8_t {
    CONFIG_MQTT = 0x01,           // MQTT broker settings
    CONFIG_NETWORK = 0x02,        // Network/Ethernet settings
    CONFIG_BATTERY = 0x03,        // Battery configuration
    CONFIG_POWER = 0x04,          // Power settings
    CONFIG_INVERTER = 0x05,       // Inverter configuration
    CONFIG_CAN = 0x06,            // CAN bus settings
    CONFIG_CONTACTOR = 0x07,      // Contactor control
    CONFIG_SYSTEM = 0x08          // System-level settings
};
```

### Configuration Data Structures

**File: `esp32common/config_sync/config_structures.h` (NEW)**

```cpp
#pragma once
#include <stdint.h>

// Version tracking
struct ConfigVersion {
    uint16_t global_version;      // Incremented on any config change
    uint16_t section_versions[8]; // Per-section version tracking
} __attribute__((packed));

// MQTT Configuration
struct MqttConfig {
    char server[64];              // MQTT broker IP/hostname
    uint16_t port;                // MQTT port (default 1883)
    char username[32];            // MQTT username
    char password[32];            // MQTT password
    char client_id[32];           // MQTT client identifier
    char topic_prefix[32];        // Base topic for publishing
    bool enabled;                 // MQTT enable/disable
    uint16_t timeout_ms;          // Connection timeout
} __attribute__((packed));

// Network Configuration
struct NetworkConfig {
    bool use_static_ip;           // Static vs DHCP
    uint8_t ip[4];                // Static IP address
    uint8_t gateway[4];           // Gateway address
    uint8_t subnet[4];            // Subnet mask
    uint8_t dns[4];               // DNS server
    char hostname[32];            // Device hostname
} __attribute__((packed));

// Battery Configuration
struct BatteryConfig {
    uint16_t pack_voltage_max;    // Max pack voltage (mV)
    uint16_t pack_voltage_min;    // Min pack voltage (mV)
    uint16_t cell_voltage_max;    // Max cell voltage (mV)
    uint16_t cell_voltage_min;    // Min cell voltage (mV)
    bool double_battery;          // Dual battery mode
    bool use_estimated_soc;       // Use SOC estimation
    uint8_t chemistry;            // Battery chemistry type
} __attribute__((packed));

// Power Settings
struct PowerConfig {
    uint16_t charge_power_w;      // Max charge power (W)
    uint16_t discharge_power_w;   // Max discharge power (W)
    uint16_t max_precharge_ms;    // Max precharge time
    uint16_t precharge_duration_ms; // Precharge duration
} __attribute__((packed));

// Inverter Configuration
struct InverterConfig {
    uint8_t total_cells;          // Total cell count
    uint8_t modules;              // Number of modules
    uint8_t cells_per_module;     // Cells per module
    uint16_t voltage_level;       // Nominal voltage
    uint16_t capacity_ah;         // Capacity in Ah
    uint8_t battery_type;         // Battery type enum
} __attribute__((packed));

// CAN Configuration
struct CanConfig {
    uint16_t frequency_khz;       // CAN bus frequency
    uint16_t fd_frequency_mhz;    // CAN-FD frequency
    uint16_t sofar_id;            // Sofar inverter ID
    uint16_t pylon_send_interval; // Pylon protocol interval
} __attribute__((packed));

// Contactor Control
struct ContactorConfig {
    bool control_enabled;         // Enable contactor control
    bool nc_contactor;            // Normally closed mode
    uint16_t pwm_frequency;       // PWM frequency (Hz)
} __attribute__((packed));

// System Configuration
struct SystemConfig {
    uint8_t led_mode;             // LED mode
    bool web_enabled;             // Web server enabled
    uint16_t log_level;           // Logging verbosity
} __attribute__((packed));

// Full configuration snapshot
struct FullConfigSnapshot {
    ConfigVersion version;
    MqttConfig mqtt;
    NetworkConfig network;
    BatteryConfig battery;
    PowerConfig power;
    InverterConfig inverter;
    CanConfig can;
    ContactorConfig contactor;
    SystemConfig system;
    uint32_t checksum;            // CRC32 for integrity
} __attribute__((packed));
```

### Field Identifiers for Delta Updates

```cpp
// MQTT fields
enum MqttField : uint8_t {
    MQTT_SERVER = 0x01,
    MQTT_PORT = 0x02,
    MQTT_USERNAME = 0x03,
    MQTT_PASSWORD = 0x04,
    MQTT_CLIENT_ID = 0x05,
    MQTT_TOPIC_PREFIX = 0x06,
    MQTT_ENABLED = 0x07,
    MQTT_TIMEOUT = 0x08
};

// Network fields
enum NetworkField : uint8_t {
    NET_USE_STATIC = 0x01,
    NET_IP_ADDRESS = 0x02,
    NET_GATEWAY = 0x03,
    NET_SUBNET = 0x04,
    NET_DNS = 0x05,
    NET_HOSTNAME = 0x06
};

// Battery fields
enum BatteryField : uint8_t {
    BATT_PACK_V_MAX = 0x01,
    BATT_PACK_V_MIN = 0x02,
    BATT_CELL_V_MAX = 0x03,
    BATT_CELL_V_MIN = 0x04,
    BATT_DOUBLE = 0x05,
    BATT_USE_EST_SOC = 0x06,
    BATT_CHEMISTRY = 0x07
};

// ... (similar enums for other sections)
```

---

## 3. Message Protocol

### New Message Types

Add to `espnow_packet_utils.h`:

```cpp
enum class MessageType : uint8_t {
    // Existing types...
    msg_data = 0x01,
    msg_probe = 0x02,
    msg_ack = 0x03,
    msg_version_announce = 0x10,
    
    // NEW: Configuration synchronization
    msg_config_request_full = 0x20,      // Request full snapshot
    msg_config_snapshot = 0x21,          // Full config snapshot
    msg_config_update_delta = 0x22,      // Delta update
    msg_config_ack = 0x23,               // Config ACK with version
    msg_config_request_resync = 0x24     // Request resync (delta failed)
};
```

### Message Structures

```cpp
// Request full configuration snapshot
struct config_request_full_t {
    MessageType type = MessageType::msg_config_request_full;
    uint32_t request_id;          // For tracking responses
} __attribute__((packed));

// Full configuration snapshot response
struct config_snapshot_t {
    MessageType type = MessageType::msg_config_snapshot;
    uint32_t request_id;          // Matches request
    FullConfigSnapshot config;    // Full configuration data
} __attribute__((packed));

// Delta update message
struct config_delta_update_t {
    MessageType type = MessageType::msg_config_update_delta;
    uint16_t global_version;      // New global version
    uint16_t section_version;     // New section version
    ConfigSection section;        // Which section changed
    uint8_t field_id;             // Specific field changed
    uint8_t value_length;         // Length of value data
    uint8_t value_data[64];       // Actual value (variable length)
    uint32_t timestamp;           // When change occurred
} __attribute__((packed));

// Configuration ACK
struct config_ack_t {
    MessageType type = MessageType::msg_config_ack;
    uint16_t acked_version;       // Version being acknowledged
    ConfigSection section;        // Section acknowledged
    bool success;                 // Success/failure flag
    uint32_t timestamp;           // When ACK sent
} __attribute__((packed));

// Request resync (fallback)
struct config_request_resync_t {
    MessageType type = MessageType::msg_config_request_resync;
    uint16_t last_known_version;  // Last version receiver had
    char reason[32];              // Why resync needed
} __attribute__((packed));
```

---

## 4. Implementation Changes

### File Structure

```
esp32common/
├── config_sync/
│   ├── config_structures.h         (NEW) - Data structures
│   ├── config_manager.h            (NEW) - Configuration manager class
│   ├── config_manager.cpp          (NEW) - Implementation
│   ├── config_delta.h              (NEW) - Delta update utilities
│   ├── config_delta.cpp            (NEW) - Delta generation/application
│   └── library.json                (NEW) - PlatformIO library manifest
│
├── espnow_common_utils/
│   ├── espnow_packet_utils.h       (MODIFY) - Add config message types
│   └── espnow_message_router.cpp   (MODIFY) - Route config messages
│
espnowreciever_2/
├── src/
│   ├── config/
│   │   ├── config_display.cpp      (NEW) - Display config on screen
│   │   └── config_display.h        (NEW)
│   │
│   └── espnow/
│       ├── espnow_callbacks.cpp    (MODIFY) - Handle config messages
│       └── config_receiver.cpp     (NEW) - Receiver-side config logic
│
ESPnowtransmitter2/ESPnowtransmitter/
└── src/
    ├── config_provider.cpp         (NEW) - Transmitter config provider
    ├── config_provider.h           (NEW)
    └── main.cpp                    (MODIFY) - Initialize config provider
```

---

## 5. Retry and Reliability Mechanisms

### Retry Policy for Delta Updates

When Device 1 (transmitter) sends a delta update to Device 2 (receiver):

```cpp
class ConfigUpdateRetryManager {
private:
    struct PendingUpdate {
        config_delta_update_t update;
        uint8_t retry_count;
        unsigned long last_send_time;
        unsigned long next_retry_time;
        bool ack_received;
    };
    
    std::vector<PendingUpdate> pending_updates;
    
    const uint8_t MAX_QUICK_RETRIES = 3;
    const uint16_t QUICK_RETRY_INTERVAL_MS = 150;
    const uint16_t SLOW_RETRY_INTERVAL_MS = 1000;
    const uint8_t MAX_TOTAL_RETRIES = 5;
    
public:
    void sendUpdate(config_delta_update_t& update);
    void onAckReceived(uint16_t version, ConfigSection section);
    void process();  // Called in loop to handle retries
};
```

**Retry Sequence:**

1. **Initial send** - Immediately
2. **Retry 1** - After 150ms if no ACK
3. **Retry 2** - After 150ms if no ACK
4. **Retry 3** - After 150ms if no ACK
5. **Retry 4** - After 1000ms if no ACK
6. **Retry 5** - After 1000ms if no ACK
7. **Give up** - Request full resync if still no ACK

### ACK Timeout Detection

```cpp
bool ConfigUpdateRetryManager::isAckOverdue(const PendingUpdate& update) {
    unsigned long timeout = (update.retry_count < MAX_QUICK_RETRIES) 
                           ? QUICK_RETRY_INTERVAL_MS 
                           : SLOW_RETRY_INTERVAL_MS;
    
    return (millis() - update.last_send_time) > timeout;
}
```

### Full Resync Trigger

After `MAX_TOTAL_RETRIES` failed attempts:

```cpp
void ConfigUpdateRetryManager::triggerResync() {
    config_request_resync_t resync_req;
    resync_req.last_known_version = current_version;
    strncpy(resync_req.reason, "Delta ACK timeout", sizeof(resync_req.reason));
    
    esp_now_send(receiver_mac, (uint8_t*)&resync_req, sizeof(resync_req));
    
    // Clear pending updates
    pending_updates.clear();
}
```

---

## 6. Code Changes Required

### 6.1 Transmitter (Device 1 - Slave)

**File: `ESPnowtransmitter2/ESPnowtransmitter/src/config_provider.h` (NEW)**

```cpp
#pragma once
#include <config_sync/config_structures.h>
#include <config_sync/config_manager.h>

class TransmitterConfigProvider {
public:
    static TransmitterConfigProvider& instance();
    
    // Initialize with current running config
    void init();
    
    // Get full snapshot
    FullConfigSnapshot getFullSnapshot();
    
    // Handle configuration requests
    void onFullSnapshotRequested(uint32_t request_id, const uint8_t* requester_mac);
    void onResyncRequested(uint16_t last_known_version, const uint8_t* requester_mac);
    
    // Notify of configuration changes (call when user changes settings)
    void notifyConfigChange(ConfigSection section, uint8_t field_id, 
                           const void* new_value, uint8_t value_length);
    
    // Process retry logic
    void process();
    
private:
    TransmitterConfigProvider() = default;
    
    ConfigManager config_manager_;
    ConfigUpdateRetryManager retry_manager_;
    uint8_t receiver_mac_[6];
    bool receiver_known_ = false;
    
    void sendFullSnapshot(uint32_t request_id);
    void sendDeltaUpdate(ConfigSection section, uint8_t field_id, 
                        const void* value, uint8_t value_length);
};
```

**File: `ESPnowtransmitter2/ESPnowtransmitter/src/config_provider.cpp` (NEW)**

```cpp
#include "config_provider.h"
#include <esp_now.h>
#include <Arduino.h>

extern const char* mqtt_server;
extern const int mqtt_port;
extern const char* mqtt_user;
extern const char* mqtt_password;
extern const char* mqtt_client_id;
// ... other external config variables

TransmitterConfigProvider& TransmitterConfigProvider::instance() {
    static TransmitterConfigProvider instance;
    return instance;
}

void TransmitterConfigProvider::init() {
    // Populate config from current global variables
    FullConfigSnapshot snapshot = {};
    
    // MQTT configuration
    strncpy(snapshot.mqtt.server, mqtt_server, sizeof(snapshot.mqtt.server));
    snapshot.mqtt.port = mqtt_port;
    strncpy(snapshot.mqtt.username, mqtt_user, sizeof(snapshot.mqtt.username));
    strncpy(snapshot.mqtt.password, mqtt_password, sizeof(snapshot.mqtt.password));
    strncpy(snapshot.mqtt.client_id, mqtt_client_id, sizeof(snapshot.mqtt.client_id));
    snapshot.mqtt.enabled = mqtt_enabled;
    
    // Network configuration
    snapshot.network.use_static_ip = eth_use_static_ip;
    memcpy(snapshot.network.ip, &eth_static_ip[0], 4);
    memcpy(snapshot.network.gateway, &eth_gateway[0], 4);
    memcpy(snapshot.network.subnet, &eth_subnet[0], 4);
    
    // ... populate other sections ...
    
    config_manager_.setFullConfig(snapshot);
    
    Serial.println("[CONFIG] Transmitter config provider initialized");
}

void TransmitterConfigProvider::onFullSnapshotRequested(uint32_t request_id, 
                                                        const uint8_t* requester_mac) {
    memcpy(receiver_mac_, requester_mac, 6);
    receiver_known_ = true;
    
    Serial.printf("[CONFIG] Full snapshot requested (ID=%u)\n", request_id);
    sendFullSnapshot(request_id);
}

void TransmitterConfigProvider::sendFullSnapshot(uint32_t request_id) {
    config_snapshot_t response;
    response.request_id = request_id;
    response.config = config_manager_.getFullConfig();
    
    // Calculate checksum
    response.config.checksum = calculateCRC32((uint8_t*)&response.config, 
                                              sizeof(FullConfigSnapshot) - sizeof(uint32_t));
    
    esp_err_t result = esp_now_send(receiver_mac_, (uint8_t*)&response, sizeof(response));
    
    if (result == ESP_OK) {
        Serial.printf("[CONFIG] Sent full snapshot (version %u)\n", 
                     response.config.version.global_version);
    } else {
        Serial.printf("[CONFIG] Failed to send snapshot: %s\n", esp_err_to_name(result));
    }
}

void TransmitterConfigProvider::notifyConfigChange(ConfigSection section, 
                                                   uint8_t field_id,
                                                   const void* new_value, 
                                                   uint8_t value_length) {
    if (!receiver_known_) {
        Serial.println("[CONFIG] Receiver not connected, skipping delta update");
        return;
    }
    
    // Update internal config and increment version
    config_manager_.updateField(section, field_id, new_value, value_length);
    
    // Send delta update
    sendDeltaUpdate(section, field_id, new_value, value_length);
}

void TransmitterConfigProvider::sendDeltaUpdate(ConfigSection section, 
                                                uint8_t field_id,
                                                const void* value, 
                                                uint8_t value_length) {
    config_delta_update_t update;
    update.global_version = config_manager_.getGlobalVersion();
    update.section_version = config_manager_.getSectionVersion(section);
    update.section = section;
    update.field_id = field_id;
    update.value_length = value_length;
    memcpy(update.value_data, value, value_length);
    update.timestamp = millis();
    
    // Hand to retry manager
    retry_manager_.sendUpdate(update);
}

void TransmitterConfigProvider::process() {
    // Process retries
    retry_manager_.process();
}
```

**Changes to `main.cpp`:**

```cpp
#include "config_provider.h"

void setup() {
    // ... existing setup ...
    
    // Initialize configuration provider
    TransmitterConfigProvider::instance().init();
    
    // Register ESP-NOW callbacks
    esp_now_register_recv_cb(on_config_data_recv);  // Handle config messages
}

void loop() {
    // ... existing loop ...
    
    // Process config updates (handle retries)
    TransmitterConfigProvider::instance().process();
}

// Example: When MQTT server changes
void updateMqttServer(const char* new_server) {
    mqtt_server = new_server;  // Update global
    
    // Notify config system
    TransmitterConfigProvider::instance().notifyConfigChange(
        CONFIG_MQTT,
        MQTT_SERVER,
        new_server,
        strlen(new_server) + 1
    );
}
```

### 6.2 Receiver (Device 2 - Master)

**File: `espnowreciever_2/src/espnow/config_receiver.h` (NEW)**

```cpp
#pragma once
#include <config_sync/config_structures.h>
#include <config_sync/config_manager.h>

class ReceiverConfigManager {
public:
    static ReceiverConfigManager& instance();
    
    // Request full snapshot from transmitter
    void requestFullSnapshot();
    
    // Handle incoming messages
    void onSnapshotReceived(const config_snapshot_t* snapshot);
    void onDeltaUpdateReceived(const config_delta_update_t* update);
    
    // Get current configuration
    const FullConfigSnapshot& getCurrentConfig() const;
    
    // Check if config is available
    bool isConfigAvailable() const { return config_received_; }
    
    // Get specific section
    const MqttConfig& getMqttConfig() const;
    const NetworkConfig& getNetworkConfig() const;
    // ... other getters ...
    
private:
    ReceiverConfigManager() = default;
    
    ConfigManager config_manager_;
    bool config_received_ = false;
    uint32_t last_request_id_ = 0;
    
    void sendAck(uint16_t version, ConfigSection section, bool success);
    void applyDeltaUpdate(const config_delta_update_t* update);
    bool validateChecksum(const FullConfigSnapshot* config);
};
```

**File: `espnowreciever_2/src/espnow/config_receiver.cpp` (NEW)**

```cpp
#include "config_receiver.h"
#include "../common.h"
#include <esp_now.h>

extern namespace ESPNow {
    extern uint8_t transmitter_mac[6];
}

ReceiverConfigManager& ReceiverConfigManager::instance() {
    static ReceiverConfigManager instance;
    return instance;
}

void ReceiverConfigManager::requestFullSnapshot() {
    config_request_full_t request;
    request.request_id = ++last_request_id_;
    
    esp_err_t result = esp_now_send(ESPNow::transmitter_mac, 
                                    (uint8_t*)&request, 
                                    sizeof(request));
    
    if (result == ESP_OK) {
        LOG_INFO("CONFIG: Requested full snapshot (ID=%u)", request.request_id);
    } else {
        LOG_ERROR("CONFIG: Failed to request snapshot: %s", esp_err_to_name(result));
    }
}

void ReceiverConfigManager::onSnapshotReceived(const config_snapshot_t* snapshot) {
    LOG_INFO("CONFIG: Received full snapshot (version %u)", 
             snapshot->config.version.global_version);
    
    // Validate checksum
    if (!validateChecksum(&snapshot->config)) {
        LOG_ERROR("CONFIG: Checksum validation failed!");
        sendAck(snapshot->config.version.global_version, CONFIG_SYSTEM, false);
        return;
    }
    
    // Store configuration
    config_manager_.setFullConfig(snapshot->config);
    config_received_ = true;
    
    // Send ACK
    sendAck(snapshot->config.version.global_version, CONFIG_SYSTEM, true);
    
    LOG_INFO("CONFIG: Configuration stored and acknowledged");
    
    // Update display with new config
    // (trigger display refresh)
}

void ReceiverConfigManager::onDeltaUpdateReceived(const config_delta_update_t* update) {
    LOG_INFO("CONFIG: Received delta update (section=%d, field=%d, version=%u)",
             update->section, update->field_id, update->global_version);
    
    // Apply the update
    applyDeltaUpdate(update);
    
    // Send ACK
    sendAck(update->global_version, update->section, true);
    
    LOG_INFO("CONFIG: Delta applied and acknowledged");
}

void ReceiverConfigManager::applyDeltaUpdate(const config_delta_update_t* update) {
    config_manager_.updateField(update->section, 
                               update->field_id,
                               update->value_data,
                               update->value_length);
    
    // Update display if needed
    // (only refresh affected section)
}

void ReceiverConfigManager::sendAck(uint16_t version, ConfigSection section, bool success) {
    config_ack_t ack;
    ack.acked_version = version;
    ack.section = section;
    ack.success = success;
    ack.timestamp = millis();
    
    esp_now_send(ESPNow::transmitter_mac, (uint8_t*)&ack, sizeof(ack));
}

bool ReceiverConfigManager::validateChecksum(const FullConfigSnapshot* config) {
    uint32_t calculated = calculateCRC32((uint8_t*)config, 
                                        sizeof(FullConfigSnapshot) - sizeof(uint32_t));
    return calculated == config->checksum;
}

const MqttConfig& ReceiverConfigManager::getMqttConfig() const {
    return config_manager_.getFullConfig().mqtt;
}

// ... other getters ...
```

**Changes to `espnow_callbacks.cpp`:**

```cpp
#include "config_receiver.h"

void on_data_recv(const uint8_t *mac_addr, const uint8_t *data, int len) {
    if (!data || len < 1) return;
    
    MessageType msg_type = static_cast<MessageType>(data[0]);
    
    switch (msg_type) {
        case MessageType::msg_config_snapshot: {
            if (len >= sizeof(config_snapshot_t)) {
                config_snapshot_t* snapshot = (config_snapshot_t*)data;
                ReceiverConfigManager::instance().onSnapshotReceived(snapshot);
            }
            break;
        }
        
        case MessageType::msg_config_update_delta: {
            if (len >= sizeof(config_delta_update_t)) {
                config_delta_update_t* update = (config_delta_update_t*)data;
                ReceiverConfigManager::instance().onDeltaUpdateReceived(update);
            }
            break;
        }
        
        // ... existing cases ...
    }
}
```

**Changes to `main.cpp`:**

```cpp
#include "espnow/config_receiver.h"

void setup() {
    // ... existing setup ...
    
    // After ESP-NOW initialization and transmitter connection
    // Request full configuration snapshot
    if (ESPNow::transmitter_connected) {
        ReceiverConfigManager::instance().requestFullSnapshot();
    }
}
```

### 6.3 Settings Page Update

**Modify `settings_page.cpp` to use ReceiverConfigManager:**

```cpp
static esp_err_t root_handler(httpd_req_t *req) {
    String content = R"rawliteral(
    <h1>ESP-NOW System Settings</h1>
    <h2>Transmitter Configuration</h2>
    )rawliteral";
    
    // Check if config is available
    if (!ReceiverConfigManager::instance().isConfigAvailable()) {
        content += R"rawliteral(
        <div class='note'>
            ⚠️ Configuration not yet received from transmitter. Waiting...
        </div>
        )rawliteral";
        
        String html = generatePage("ESP-NOW Settings", content, "", "");
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, html.c_str(), html.length());
        return ESP_OK;
    }
    
    // Get configuration from manager
    const MqttConfig& mqtt = ReceiverConfigManager::instance().getMqttConfig();
    const NetworkConfig& network = ReceiverConfigManager::instance().getNetworkConfig();
    
    // Build settings page with actual values
    content += "<div class='settings-card'>";
    content += "<h3>MQTT Configuration</h3>";
    content += "<div class='settings-row'>";
    content += "  <label>MQTT Enabled:</label>";
    content += String("<input type='checkbox' ") + (mqtt.enabled ? "checked" : "") + " disabled />";
    content += "</div>";
    content += "<div class='settings-row'>";
    content += "  <label>MQTT Server:</label>";
    content += "  <input type='text' value='" + String(mqtt.server) + "' disabled style='background-color: white; color: #333;' />";
    content += "</div>";
    // ... etc for all fields ...
    
    // Add version info
    const FullConfigSnapshot& config = ReceiverConfigManager::instance().getCurrentConfig();
    content += "<p style='color: #888; font-size: 12px;'>";
    content += "Configuration version: " + String(config.version.global_version);
    content += "</p>";
    
    String html = generatePage("ESP-NOW Settings", content, "", "");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html.c_str(), html.length());
    return ESP_OK;
}
```

---

## 7. Testing Plan

### Phase 1: Full Snapshot

1. **Test initial connection**
   - [ ] Receiver boots, transmitter already running
   - [ ] Transmitter boots, receiver already running
   - [ ] Both devices boot simultaneously
   - [ ] Verify full snapshot is received and displayed

### Phase 2: Delta Updates

2. **Test single field changes**
   - [ ] Change MQTT server on transmitter
   - [ ] Verify delta update sent
   - [ ] Verify ACK received
   - [ ] Verify receiver display updated
   - [ ] Check version incremented correctly

3. **Test multiple rapid changes**
   - [ ] Change 3-5 fields quickly
   - [ ] Verify all delta updates queued
   - [ ] Verify sequential ACKs
   - [ ] Verify all changes reflected on receiver

### Phase 3: Retry Logic

4. **Test retry mechanism**
   - [ ] Simulate missed ACK (don't send ACK)
   - [ ] Verify 3 quick retries (150ms spacing)
   - [ ] Verify slower retries (1s spacing)
   - [ ] Verify max 5 retries before giving up

5. **Test resync**
   - [ ] Force multiple ACK failures
   - [ ] Verify resync request sent
   - [ ] Verify full snapshot re-requested
   - [ ] Verify system recovers

### Phase 4: Edge Cases

6. **Test disconnection scenarios**
   - [ ] Transmitter disconnects during delta update
   - [ ] Receiver disconnects and reconnects
   - [ ] Verify full snapshot re-requested on reconnect
   - [ ] Verify no data corruption

7. **Test version mismatch**
   - [ ] Manually create version mismatch
   - [ ] Verify automatic resync triggered
   - [ ] Verify synchronization restored

---

## 8. Success Criteria

✅ **Reliability:**
- All configuration changes successfully propagated
- No data loss during updates
- Automatic recovery from failures

✅ **Performance:**
- Full snapshot received < 500ms
- Delta updates applied < 100ms
- Retry attempts succeed within 1 second

✅ **User Experience:**
- Settings page always shows current values
- No manual intervention needed
- Clear indication when sync in progress

✅ **Robustness:**
- Survives network interruptions
- Handles rapid configuration changes
- Checksum validation prevents corruption

---

## 9. Implementation Timeline

**Week 1:**
- Create configuration data structures
- Implement ConfigManager class
- Add message types to protocol

**Week 2:**
- Implement TransmitterConfigProvider
- Implement ReceiverConfigManager
- Add retry logic

**Week 3:**
- Update settings webpage
- Integrate with existing code
- Basic testing

**Week 4:**
- Comprehensive testing
- Bug fixes
- Documentation

---

## Conclusion

This implementation provides a professional-grade configuration synchronization system following industry best practices. The "Full Snapshot + Delta Updates" pattern ensures:

- **Reliability:** Retry logic and ACK mechanism
- **Efficiency:** Only changed fields transmitted
- **Robustness:** Automatic resync on failures
- **Scalability:** Section-based organization allows easy extension
- **Maintainability:** Clear separation of concerns

The system mirrors approaches used by commercial IoT devices from Bosch, Sonoff, and Philips Hue, ensuring enterprise-grade reliability for your ESP-NOW communication system.

---

**Document Version:** 1.0  
**Last Updated:** February 4, 2026  
**Next Review:** After Phase 1 Implementation
