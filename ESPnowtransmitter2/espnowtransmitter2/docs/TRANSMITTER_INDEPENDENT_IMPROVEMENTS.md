# Transmitter Independent Improvements
## ESPnowtransmitter2 - Codebase-Specific Enhancements

**Date**: February 26, 2026  
**Scope**: Improvements that benefit the transmitter codebase **independently** of the receiver  
**Note**: These improvements do NOT require changes to the receiver codebase

---

## Overview

The transmitter has a better architectural foundation than the receiver but requires several critical production fixes that are unique to its network-heavy architecture. These improvements address reliability, resilience, and robustness specific to the transmitter.

---

## 1. MQTT Connection State Machine with Exponential Backoff

### Priority: 🔴 **CRITICAL**
**Effort**: 1 day  
**Blocking**: Yes - must fix before production deployment

### Problem

MQTT connection has no retry strategy - failure is permanent:

```cpp
// mqtt_manager.cpp - Current implementation
bool MqttManager::connect() {
    if (!EthernetManager::instance().is_connected()) {
        return false;  // ← No retry scheduled
    }
    
    if (!client_.connect(mqtt_server, mqtt_port, mqtt_username, mqtt_password)) {
        return false;  // ← No retry scheduled
    }
    
    return true;
}

// In mqtt_task - called once during init
void mqtt_task(void *pvParameters) {
    if (!MqttManager::instance().connect()) {
        // ← Hangs here, never retried
    }
}
```

### Consequence

- Device boots, MQTT server is down → connection fails once
- Never retried → device offline permanently for device lifetime
- No way to detect this without remote telemetry
- User has no visibility into problem

### Solution

Implement state machine with exponential backoff:

```cpp
// mqtt_manager.h
class MqttManager {
public:
    enum class MqttState {
        DISCONNECTED,       // Not connected, waiting to connect
        CONNECTING,         // Connection attempt in progress
        CONNECTED,          // Fully connected and operational
        CONNECTION_FAILED,  // Connection attempt failed
        NETWORK_ERROR       // Underlying network unavailable
    };
    
    static MqttManager& instance();
    
    void init();  // Called once from main
    void update();  // Called periodically (e.g., every 1 second)
    
    // Status queries
    MqttState get_state() const;
    bool is_connected() const { return get_state() == MqttState::CONNECTED; }
    
    // Publishing (always safe to call)
    bool publish(const char* topic, const uint8_t* payload, size_t length, bool retain = false);
    
    // Statistics
    struct Statistics {
        uint32_t total_connections;
        uint32_t failed_connections;
        uint32_t total_messages_published;
        uint32_t current_retry_delay_ms;
        uint32_t uptime_ms;
    };
    Statistics get_statistics() const;
    
private:
    MqttManager();
    
    void attempt_connection();
    void on_connection_success();
    void on_connection_failed();
    void on_network_error();
    
    MqttState state_;
    uint32_t last_state_change_;
    uint32_t last_connection_attempt_;
    uint32_t current_retry_delay_;
    uint32_t initialization_time_;
    
    uint32_t total_connections_;
    uint32_t failed_connections_;
    uint32_t total_messages_published_;
    
    static constexpr uint32_t INITIAL_RETRY_DELAY_MS = 5000;     // 5 seconds
    static constexpr uint32_t MAX_RETRY_DELAY_MS = 300000;       // 5 minutes
    static constexpr float RETRY_BACKOFF_MULTIPLIER = 1.5f;
    static constexpr uint32_t CONNECTION_TIMEOUT_MS = 10000;     // 10 seconds
};
```

### Implementation

```cpp
// mqtt_manager.cpp
MqttManager& MqttManager::instance() {
    static MqttManager instance;
    return instance;
}

MqttManager::MqttManager()
    : state_(MqttState::DISCONNECTED)
    , last_state_change_(millis())
    , last_connection_attempt_(0)
    , current_retry_delay_(INITIAL_RETRY_DELAY_MS)
    , initialization_time_(millis())
    , total_connections_(0)
    , failed_connections_(0)
    , total_messages_published_(0) {
}

void MqttManager::init() {
    LOG_INFO("[MQTT] Manager initialized, waiting for Ethernet...");
    // Don't attempt connection here - wait for update() calls
}

void MqttManager::update() {
    uint32_t now = millis();
    
    // Check if Ethernet connectivity changed
    bool ethernet_connected = EthernetManager::instance().is_connected();
    
    switch (state_) {
        case MqttState::DISCONNECTED:
            if (ethernet_connected) {
                // Ethernet is ready, try to connect
                attempt_connection();
            }
            break;
            
        case MqttState::CONNECTING: {
            uint32_t elapsed = now - last_connection_attempt_;
            if (elapsed > CONNECTION_TIMEOUT_MS) {
                // Connection attempt timed out
                LOG_WARN("[MQTT] Connection timeout after %u ms", elapsed);
                on_connection_failed();
            }
            break;
        }
            
        case MqttState::CONNECTED:
            if (client_.connected()) {
                // Process subscriptions while connected
                client_.loop();
            } else {
                // Connection dropped
                LOG_WARN("[MQTT] Connection lost");
                state_ = MqttState::DISCONNECTED;
                last_state_change_ = now;
            }
            break;
            
        case MqttState::CONNECTION_FAILED: {
            uint32_t elapsed = now - last_connection_attempt_;
            if (elapsed >= current_retry_delay_) {
                // Time to retry
                if (ethernet_connected) {
                    LOG_INFO("[MQTT] Attempting reconnection (previous delay: %u ms)",
                            current_retry_delay_);
                    attempt_connection();
                } else {
                    LOG_DEBUG("[MQTT] Waiting for Ethernet to reconnect");
                    state_ = MqttState::NETWORK_ERROR;
                    last_state_change_ = now;
                }
            }
            break;
        }
            
        case MqttState::NETWORK_ERROR:
            if (ethernet_connected) {
                // Ethernet is back, try MQTT again
                LOG_INFO("[MQTT] Ethernet recovered, attempting connection");
                attempt_connection();
            }
            break;
    }
}

void MqttManager::attempt_connection() {
    LOG_INFO("[MQTT] Attempting connection to %s:%d...", mqtt_server, mqtt_port);
    
    state_ = MqttState::CONNECTING;
    last_connection_attempt_ = millis();
    
    // Attempt actual connection
    if (client_.connect(mqtt_server, mqtt_port, mqtt_username, mqtt_password)) {
        on_connection_success();
    } else {
        on_connection_failed();
    }
}

void MqttManager::on_connection_success() {
    LOG_INFO("[MQTT] Connected successfully");
    
    state_ = MqttState::CONNECTED;
    last_state_change_ = millis();
    total_connections_++;
    
    // Reset retry delay on success
    current_retry_delay_ = INITIAL_RETRY_DELAY_MS;
    
    // Subscribe to topics
    client_.subscribe("esp32/config/update");
    client_.subscribe("esp32/control/command");
}

void MqttManager::on_connection_failed() {
    LOG_WARN("[MQTT] Connection failed");
    
    state_ = MqttState::CONNECTION_FAILED;
    last_state_change_ = millis();
    last_connection_attempt_ = millis();
    failed_connections_++;
    
    // Exponential backoff
    uint32_t old_delay = current_retry_delay_;
    current_retry_delay_ = (uint32_t)(current_retry_delay_ * RETRY_BACKOFF_MULTIPLIER);
    
    if (current_retry_delay_ > MAX_RETRY_DELAY_MS) {
        current_retry_delay_ = MAX_RETRY_DELAY_MS;
    }
    
    LOG_WARN("[MQTT] Next retry in %u seconds (previous delay: %u ms)",
            current_retry_delay_ / 1000, old_delay);
}

void MqttManager::on_network_error() {
    LOG_WARN("[MQTT] Network unavailable");
    state_ = MqttState::NETWORK_ERROR;
    last_state_change_ = millis();
}

bool MqttManager::publish(const char* topic, const uint8_t* payload, size_t length, bool retain) {
    if (!is_connected()) {
        LOG_DEBUG("[MQTT] Not connected, message dropped");
        return false;
    }
    
    if (!client_.publish(topic, payload, length, retain)) {
        LOG_WARN("[MQTT] Publish failed");
        return false;
    }
    
    total_messages_published_++;
    return true;
}

MqttManager::Statistics MqttManager::get_statistics() const {
    return {
        total_connections_,
        failed_connections_,
        total_messages_published_,
        current_retry_delay_,
        millis() - initialization_time_
    };
}
```

### Updated MQTT Task

```cpp
// mqtt_task.cpp
void mqtt_task(void *pvParameters) {
    auto& mqtt = MqttManager::instance();
    mqtt.init();
    
    while (true) {
        mqtt.update();  // Call regularly to handle state transitions
        
        // Log statistics periodically
        static uint32_t last_log = 0;
        if (millis() - last_log > 30000) {
            auto stats = mqtt.get_statistics();
            LOG_INFO("[MQTT STATS] Connections: %u, Failed: %u, Published: %u, Uptime: %u s",
                    stats.total_connections,
                    stats.failed_connections,
                    stats.total_messages_published,
                    stats.uptime_ms / 1000);
            last_log = millis();
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000));  // Update every second
    }
}
```

### Benefits

- ✅ **Automatic recovery**: Handles temporary server outages
- ✅ **Exponential backoff**: Prevents server flooding
- ✅ **Network-aware**: Waits for Ethernet before attempting
- ✅ **Visible state**: Can query connection status anytime
- ✅ **Statistics**: Track connection health over time
- ✅ **Production-ready**: Safe for production deployment

### Code Changes Required

**Files to modify**:
- `src/network/mqtt_manager.h` - Add state machine, statistics
- `src/network/mqtt_manager.cpp` - Implement state transitions
- `src/mqtt/mqtt_task.cpp` - Call update() instead of connect()

**Dependencies**:
- None - purely internal improvement

---

## 2. Ethernet IP Acquisition Timeout

### Priority: 🔴 **CRITICAL**
**Effort**: 4 hours  
**Blocking**: Yes - prevents system hang

### Problem

IP acquisition has no timeout - can hang indefinitely if DHCP server is broken:

```cpp
// ethernet_manager.cpp - Current
case EthernetState::GETTING_IP:
    if (ETH.localIP() != IPAddress(0, 0, 0, 0)) {
        transition_to(EthernetState::IP_ASSIGNED);
    }
    // ← No timeout - hangs here forever if DHCP broken
```

### Consequence

- Cable plugged in but DHCP down → hangs in GETTING_IP forever
- Device unresponsive, only solution is power cycle
- Receiver also waiting for transmitter → receiver also hangs
- Cascade failure across network

### Solution

```cpp
// ethernet_manager.h
class EthernetManager {
public:
    enum class EthernetState {
        UNINITIALIZED,
        INITIALIZING,
        NO_CABLE,
        WAITING_FOR_LINK,
        LINK_UP,
        GETTING_IP,
        IP_ASSIGNED,
        CONNECTED,
        ERROR_STATE
    };
    
    // ... existing interface ...
    
private:
    uint32_t ip_acquire_start_time_;
    uint8_t dhcp_retry_count_;
    
    static constexpr uint32_t DHCP_TIMEOUT_MS = 30000;  // 30 second timeout
    static constexpr uint8_t MAX_DHCP_RETRIES = 3;
};

// ethernet_manager.cpp
void EthernetManager::update() {
    // ... other states ...
    
    case EthernetState::GETTING_IP: {
        uint32_t elapsed = millis() - ip_acquire_start_time_;
        
        if (ETH.localIP() != IPAddress(0, 0, 0, 0)) {
            // IP successfully acquired
            transition_to(EthernetState::IP_ASSIGNED);
            dhcp_retry_count_ = 0;
        }
        else if (elapsed > DHCP_TIMEOUT_MS) {
            // DHCP timeout
            LOG_WARN("[ETH] DHCP timeout after %u ms (attempt %d/%d)",
                    elapsed, dhcp_retry_count_ + 1, MAX_DHCP_RETRIES);
            
            dhcp_retry_count_++;
            
            if (dhcp_retry_count_ >= MAX_DHCP_RETRIES) {
                LOG_ERROR("[ETH] DHCP failed %u times, entering error state",
                        MAX_DHCP_RETRIES);
                transition_to(EthernetState::ERROR_STATE);
            } else {
                // Retry: go back to WAITING_FOR_LINK and try again
                LOG_INFO("[ETH] Retrying DHCP...");
                transition_to(EthernetState::WAITING_FOR_LINK);
            }
        }
        break;
    }
    
    case EthernetState::WAITING_FOR_LINK: {
        // Check for link first
        if (!ETH.linkUp()) {
            return;
        }
        
        // Link is up, start DHCP
        transition_to(EthernetState::LINK_UP);
        break;
    }
    
    case EthernetState::LINK_UP: {
        LOG_INFO("[ETH] Link up, requesting IP via DHCP...");
        
        // Reset DHCP configuration
        ETH.config(IPAddress(0, 0, 0, 0));  // Clear any cached IP
        
        transition_to(EthernetState::GETTING_IP);
        ip_acquire_start_time_ = millis();
        break;
    }
    
    case EthernetState::ERROR_STATE: {
        // Retry after 30 seconds
        static uint32_t error_entry_time = 0;
        if (state_ == EthernetState::ERROR_STATE) {
            if (error_entry_time == 0) {
                error_entry_time = millis();
            }
            
            if (millis() - error_entry_time > 30000) {
                LOG_INFO("[ETH] Error recovery attempt");
                error_entry_time = 0;
                dhcp_retry_count_ = 0;
                transition_to(EthernetState::INITIALIZING);
            }
        }
        break;
    }
}
```

### Benefits

- ✅ **Prevents hang**: Times out after 30 seconds
- ✅ **Automatic retry**: Retries up to 3 times
- ✅ **Error visibility**: Logs failures clearly
- ✅ **Graceful degradation**: Enters ERROR_STATE instead of hanging
- ✅ **Recovery mode**: Retries periodically in error state

---

## 3. Settings Integrity Checking and CRC Validation

### Priority: 🔴 **CRITICAL**
**Effort**: 8 hours  
**Blocking**: Partially - should do before Settings send optimization

### Problem

Settings are sent without validation:

```cpp
// settings_manager.cpp - Current
void SettingsManager::send_to_receiver() {
    // No validation of MQTT server address
    // No validation of port numbers
    // No CRC checking
    // No type checking
    
    esp_now_send(receiver_mac, (uint8_t*)&settings_, sizeof(settings_));
}
```

### Consequence

- Invalid settings could crash receiver
- Corrupt settings could break MQTT connection
- No way to verify settings arrived intact
- Receiver has no way to detect corruption

### Solution

Create validator and add CRC:

```cpp
// settings/settings_validator.h
class SettingsValidator {
public:
    struct ValidationResult {
        bool is_valid;
        std::string error_message;
        std::vector<std::string> warnings;
    };
    
    // Validate specific subsystems
    ValidationResult validate_mqtt_settings(const MqttSettings& mqtt);
    ValidationResult validate_battery_settings(const BatterySettings& battery);
    ValidationResult validate_wifi_settings(const WifiSettings& wifi);
    
    // Validate entire structure
    ValidationResult validate_all(const SystemSettings& settings);
    
    // CRC operations
    uint32_t calculate_crc32(const void* data, size_t length);
    bool verify_crc32(const void* data, size_t length, uint32_t expected_crc);
    
private:
    bool is_valid_ip_address(const char* ip);
    bool is_valid_port(uint16_t port);
    bool is_valid_hostname(const char* hostname);
    bool is_valid_mac_address(const uint8_t* mac);
};

// System settings structure with CRC
struct SystemSettings {
    // Existing fields...
    MqttSettings mqtt;
    BatterySettings battery;
    WifiSettings wifi;
    
    // Version and integrity
    uint16_t struct_version;
    uint32_t crc32;  // ← Add this
    
    // Helper to recalculate CRC
    void recalculate_crc() {
        // Zero out CRC field
        crc32 = 0;
        
        // Calculate CRC over entire structure except CRC field itself
        SettingsValidator validator;
        crc32 = validator.calculate_crc32(this, sizeof(*this) - sizeof(uint32_t));
    }
    
    // Verify integrity
    bool is_valid_crc() const {
        SettingsValidator validator;
        return validator.verify_crc32(this, sizeof(*this) - sizeof(uint32_t), crc32);
    }
};
```

### Implementation

```cpp
// settings/settings_validator.cpp
SettingsValidator::ValidationResult SettingsValidator::validate_mqtt_settings(
    const MqttSettings& mqtt) {
    
    ValidationResult result{true, "", {}};
    
    // Validate server address
    if (strlen(mqtt.server) == 0) {
        result.is_valid = false;
        result.error_message = "MQTT server address is empty";
        return result;
    }
    
    if (!is_valid_ip_address(mqtt.server) && !is_valid_hostname(mqtt.server)) {
        result.is_valid = false;
        result.error_message = "Invalid MQTT server address: " + std::string(mqtt.server);
        return result;
    }
    
    // Validate port
    if (mqtt.port == 0 || mqtt.port > 65535) {
        result.is_valid = false;
        result.error_message = "MQTT port out of range: " + std::to_string(mqtt.port);
        return result;
    }
    
    // Validate credentials
    if (mqtt.username[0] != '\0' && strlen(mqtt.username) > 128) {
        result.warnings.push_back("MQTT username is very long");
    }
    
    if (mqtt.password[0] != '\0' && strlen(mqtt.password) > 128) {
        result.warnings.push_back("MQTT password is very long");
    }
    
    return result;
}

uint32_t SettingsValidator::calculate_crc32(const void* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t* bytes = (const uint8_t*)data;
    
    for (size_t i = 0; i < length; i++) {
        uint8_t byte = bytes[i];
        crc ^= byte;
        
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    
    return crc ^ 0xFFFFFFFF;
}

bool SettingsValidator::verify_crc32(const void* data, size_t length, uint32_t expected_crc) {
    uint32_t calculated = calculate_crc32(data, length);
    return calculated == expected_crc;
}
```

### Updated Settings Manager

```cpp
// settings_manager.h
class SettingsManager {
public:
    static SettingsManager& instance();
    
    // Load settings from NVS/flash
    bool load_settings();
    
    // Save settings to NVS/flash
    bool save_settings();
    
    // Send validated settings to receiver
    bool send_to_receiver();
    
    // Update individual settings
    bool update_mqtt_settings(const MqttSettings& mqtt);
    bool update_battery_settings(const BatterySettings& battery);
    
    // Query current settings
    const SystemSettings& get_settings() const { return current_settings_; }
    
    // Get last validation result
    SettingsValidator::ValidationResult get_last_validation() const {
        return last_validation_;
    }
    
private:
    SystemSettings current_settings_;
    SettingsValidator validator_;
    SettingsValidator::ValidationResult last_validation_;
    
    bool validate_before_send(const SystemSettings& settings);
    bool validate_before_save(const SystemSettings& settings);
};

// settings_manager.cpp
bool SettingsManager::send_to_receiver() {
    // Validate before sending
    if (!validate_before_send(current_settings_)) {
        LOG_ERROR("[SETTINGS] Validation failed: %s",
                last_validation_.error_message.c_str());
        
        for (const auto& warn : last_validation_.warnings) {
            LOG_WARN("[SETTINGS] %s", warn.c_str());
        }
        
        return false;
    }
    
    // Recalculate CRC
    current_settings_.recalculate_crc();
    
    LOG_INFO("[SETTINGS] Sending to receiver (CRC: 0x%08X)",
            current_settings_.crc32);
    
    // Send via ESP-NOW
    uint8_t status = esp_now_send(
        receiver_mac,
        (uint8_t*)&current_settings_,
        sizeof(SystemSettings)
    );
    
    if (status != ESP_OK) {
        LOG_ERROR("[SETTINGS] Send failed with status %d", status);
        return false;
    }
    
    return true;
}

bool SettingsManager::update_mqtt_settings(const MqttSettings& mqtt) {
    // Validate before updating
    auto result = validator_.validate_mqtt_settings(mqtt);
    
    if (!result.is_valid) {
        LOG_ERROR("[SETTINGS] MQTT validation failed: %s",
                result.error_message.c_str());
        return false;
    }
    
    // Show warnings
    for (const auto& warn : result.warnings) {
        LOG_WARN("[SETTINGS] %s", warn.c_str());
    }
    
    // Update
    current_settings_.mqtt = mqtt;
    current_settings_.recalculate_crc();
    
    // Save to NVS
    return save_settings();
}
```

### Benefits

- ✅ **Prevents corruption**: Validates before sending
- ✅ **Detects errors**: CRC catches transmission errors
- ✅ **Self-healing**: Receiver can detect and reject corrupt settings
- ✅ **Audit trail**: Logs all settings changes
- ✅ **Type safety**: Validates data types and ranges

---

## 4. Event-Driven Discovery (Non-Blocking)

### Priority: 🔴 **CRITICAL**
**Effort**: 1.5 days  
**Blocking**: Yes - improves responsiveness

### Problem

Discovery task uses blocking delays:

```cpp
// discovery_task.cpp - Current
void discovery_task() {
    while (should_continue) {
        vTaskDelay(pdMS_TO_TICKS(PROBE_INTERVAL_MS));  // ← Blocks
        
        for (int ch = 1; ch <= 13; ch++) {
            esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
            vTaskDelay(pdMS_TO_TICKS(100));  // ← Blocks waiting for channel stabilization
            
            send_probe_request();
        }
    }
}
```

### Consequence

- Blocks entire discovery task during delays
- Cannot process ACKs while waiting
- Wastes scheduler cycles
- Not responsive to stop signals

### Solution

Create state machine:

```cpp
// espnow/discovery_manager.h
class DiscoveryManager {
public:
    enum class DiscoveryState {
        IDLE,
        PROBING_CHANNELS,
        WAITING_FOR_ACK,
        CHANNEL_CHANGE,
        CHANNEL_STABILIZATION,
        PEER_FOUND,
        TIMEOUT
    };
    
    static DiscoveryManager& instance();
    
    void start();
    void stop();
    void update(uint32_t now);  // Call periodically
    
    bool is_discovering() const;
    uint8_t get_current_channel() const;
    uint32_t get_discovery_duration_ms() const;
    
    // Register discovery result callback
    using DiscoveryCallback = std::function<void(const uint8_t* peer_mac, int8_t rssi)>;
    void on_peer_found(DiscoveryCallback callback);
    
    // Statistics
    struct Statistics {
        uint32_t probes_sent;
        uint32_t acks_received;
        uint32_t discovery_attempts;
        uint32_t last_discovery_time_ms;
    };
    Statistics get_statistics() const;
    
private:
    DiscoveryManager();
    
    void send_probe();
    void next_channel();
    void on_ack_received(const uint8_t* peer_mac, int8_t rssi);
    
    DiscoveryState state_;
    uint32_t state_entry_time_;
    uint32_t discovery_start_time_;
    
    uint8_t current_channel_;
    uint8_t probe_count_;
    
    uint32_t probes_sent_;
    uint32_t acks_received_;
    uint32_t discovery_attempts_;
    uint32_t last_discovery_time_;
    
    DiscoveryCallback callback_;
    
    // Timing constants
    static constexpr uint32_t PROBES_PER_CHANNEL = 3;
    static constexpr uint32_t PROBE_INTERVAL_MS = 50;
    static constexpr uint32_t CHANNEL_STABILIZATION_MS = 100;
    static constexpr uint32_t ACK_TIMEOUT_MS = 500;
    static constexpr uint32_t DISCOVERY_TIMEOUT_MS = 15000;  // 15 second total timeout
};
```

### Implementation

```cpp
// espnow/discovery_manager.cpp
void DiscoveryManager::update(uint32_t now) {
    if (state_ == DiscoveryState::IDLE) {
        return;  // Not discovering
    }
    
    uint32_t elapsed = now - state_entry_time_;
    
    switch (state_) {
        case DiscoveryState::PROBING_CHANNELS:
            if (elapsed >= PROBE_INTERVAL_MS) {
                send_probe();
                probe_count_++;
                
                if (probe_count_ >= PROBES_PER_CHANNEL) {
                    state_ = DiscoveryState::WAITING_FOR_ACK;
                    state_entry_time_ = now;
                    probe_count_ = 0;
                } else {
                    state_entry_time_ = now;
                }
            }
            break;
            
        case DiscoveryState::WAITING_FOR_ACK:
            // Process any pending ACKs (handled by RX callback)
            
            if (elapsed > ACK_TIMEOUT_MS) {
                // No ACK, move to next channel
                state_ = DiscoveryState::CHANNEL_CHANGE;
                state_entry_time_ = now;
            }
            break;
            
        case DiscoveryState::CHANNEL_CHANGE:
            next_channel();
            state_ = DiscoveryState::CHANNEL_STABILIZATION;
            state_entry_time_ = now;
            break;
            
        case DiscoveryState::CHANNEL_STABILIZATION:
            if (elapsed >= CHANNEL_STABILIZATION_MS) {
                // Channel is stable, start probing
                state_ = DiscoveryState::PROBING_CHANNELS;
                state_entry_time_ = now;
                probe_count_ = 0;
            }
            break;
            
        case DiscoveryState::PEER_FOUND:
            // Found peer, wait before restarting discovery
            if (elapsed > 5000) {
                LOG_INFO("[DISCOVERY] Restarting discovery...");
                state_ = DiscoveryState::CHANNEL_CHANGE;
                state_entry_time_ = now;
            }
            break;
            
        case DiscoveryState::TIMEOUT:
            // Discovery timed out, stop
            LOG_WARN("[DISCOVERY] Discovery timeout");
            stop();
            break;
    }
    
    // Check overall discovery timeout
    if (now - discovery_start_time_ > DISCOVERY_TIMEOUT_MS) {
        state_ = DiscoveryState::TIMEOUT;
    }
}

void DiscoveryManager::send_probe() {
    LOG_DEBUG("[DISCOVERY] Sending probe on channel %d", current_channel_);
    
    // Send ESP-NOW probe frame
    uint8_t peer_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};  // Broadcast
    uint8_t probe_data[32] = "DISCOVERY_PROBE";
    
    esp_now_send(peer_mac, probe_data, 32);
    probes_sent_++;
}

void DiscoveryManager::next_channel() {
    current_channel_++;
    if (current_channel_ > 13) {
        current_channel_ = 1;
    }
    
    LOG_DEBUG("[DISCOVERY] Switching to channel %d", current_channel_);
    esp_wifi_set_channel(current_channel_, WIFI_SECOND_CHAN_NONE);
}

void DiscoveryManager::on_ack_received(const uint8_t* peer_mac, int8_t rssi) {
    if (state_ != DiscoveryState::WAITING_FOR_ACK) {
        return;  // Not expecting ACK right now
    }
    
    LOG_INFO("[DISCOVERY] ACK received from peer on channel %d (RSSI: %d dBm)",
            current_channel_, rssi);
    
    acks_received_++;
    discovery_attempts_++;
    last_discovery_time_ = millis() - discovery_start_time_;
    
    state_ = DiscoveryState::PEER_FOUND;
    state_entry_time_ = millis();
    
    // Notify callback
    if (callback_) {
        callback_(peer_mac, rssi);
    }
}
```

### Main Loop Integration

```cpp
// In main loop or discovery task
void loop() {
    uint32_t now = millis();
    
    // Non-blocking discovery update
    DiscoveryManager::instance().update(now);
    
    // Can do other work while discovery is happening
    // No blocking waits
    
    vTaskDelay(pdMS_TO_TICKS(10));  // Small delay, not blocking discovery
}
```

### Benefits

- ✅ **Non-blocking**: Can respond to ACKs immediately
- ✅ **State-based**: Clear progression through phases
- ✅ **Responsive**: Can be interrupted with stop()
- ✅ **Observable**: Can query current state and statistics
- ✅ **Resilient**: Timeout prevents infinite discovery

---

## 5. Encapsulate ESP-NOW Queue Variables

### Priority: 🟡 **HIGH**
**Effort**: 1 day  
**Blocking**: No - but good for monitoring

### Problem

Global queue handles used directly everywhere:

```cpp
// main.cpp - Global
QueueHandle_t espnow_message_queue = nullptr;
QueueHandle_t espnow_discovery_queue = nullptr;
QueueHandle_t espnow_rx_queue = nullptr;

// Direct access from multiple files
xQueueSend(espnow_message_queue, &msg, 0);
xQueueReceive(espnow_rx_queue, &msg, pdMS_TO_TICKS(1000));
```

### Solution

```cpp
// queue/message_queue_manager.h
class MessageQueueManager {
public:
    static MessageQueueManager& instance();
    
    bool send_message(const espnow_message_t& msg, uint32_t timeout_ms = 0);
    bool receive_message(espnow_message_t& msg, uint32_t timeout_ms);
    
    size_t get_queue_depth() const;
    size_t get_max_depth() const;
    bool is_queue_full() const;
    
    struct Statistics {
        uint32_t total_sent;
        uint32_t total_received;
        uint32_t queue_overflows;
        size_t peak_depth;
        uint32_t largest_msg_size;
    };
    
    Statistics get_statistics() const;
    void reset_statistics();
    
private:
    MessageQueueManager();
    
    QueueHandle_t queue_;
    Statistics stats_;
    
    static constexpr size_t QUEUE_SIZE = 20;
};

// queue/discovery_queue_manager.h
class DiscoveryQueueManager {
public:
    static DiscoveryQueueManager& instance();
    
    bool send_discovery_event(const discovery_event_t& event, uint32_t timeout_ms = 0);
    bool receive_discovery_event(discovery_event_t& event, uint32_t timeout_ms);
    
    // ... similar interface ...
    
private:
    QueueHandle_t queue_;
    static constexpr size_t QUEUE_SIZE = 10;
};

// queue/rx_queue_manager.h
class RxQueueManager {
public:
    static RxQueueManager& instance();
    
    bool send_rx_frame(const uint8_t* data, size_t length, uint32_t timeout_ms = 0);
    bool receive_rx_frame(uint8_t* buffer, size_t& length, uint32_t timeout_ms);
    
    // ... similar interface ...
    
private:
    QueueHandle_t queue_;
    static constexpr size_t QUEUE_SIZE = 30;
};
```

### Benefits

- ✅ **Encapsulation**: No direct queue access
- ✅ **Monitoring**: Can track queue depth and overflows
- ✅ **Consistency**: Single point of access
- ✅ **Testability**: Can be mocked for unit tests
- ✅ **Resilience**: Can add queue overflow handling

---

## 6. Magic Numbers Centralization

### Priority: 🟡 **HIGH**
**Effort**: 8 hours  
**Blocking**: No - independent improvement

### Problem

Timing constants scattered throughout:

```cpp
// discovery_task.cpp
vTaskDelay(pdMS_TO_TICKS(50));
vTaskDelay(pdMS_TO_TICKS(100));
vTaskDelay(pdMS_TO_TICKS(150));

// transmission_task.cpp
#define TRANSMIT_INTERVAL 1000
const int ACK_TIMEOUT = 500;

// heartbeat_manager.cpp
#define HEARTBEAT_INTERVAL 30000
#define HEARTBEAT_TIMEOUT 90000
```

### Solution

Create centralized config:

```cpp
// config/timing_config.h
namespace TimingConfig {
    // Discovery
    constexpr uint32_t DISCOVERY_PROBE_INTERVAL_MS = 50;
    constexpr uint32_t DISCOVERY_CHANNEL_STABILIZATION_MS = 100;
    constexpr uint32_t DISCOVERY_ACK_TIMEOUT_MS = 500;
    constexpr uint32_t DISCOVERY_TIMEOUT_MS = 15000;
    
    // Transmission
    constexpr uint32_t DATA_SEND_INTERVAL_MS = 1000;
    constexpr uint32_t DATA_SEND_TIMEOUT_MS = 500;
    
    // Heartbeat
    constexpr uint32_t HEARTBEAT_INTERVAL_MS = 30000;
    constexpr uint32_t HEARTBEAT_TIMEOUT_MS = 90000;
    constexpr uint32_t HEARTBEAT_ACK_TIMEOUT_MS = 1000;
    
    // Network
    constexpr uint32_t DHCP_TIMEOUT_MS = 30000;
    constexpr uint32_t MQTT_CONNECT_TIMEOUT_MS = 10000;
    constexpr uint32_t MQTT_INITIAL_RETRY_DELAY_MS = 5000;
    constexpr uint32_t MQTT_MAX_RETRY_DELAY_MS = 300000;
    
    // OTA
    constexpr uint32_t OTA_TIMEOUT_MS = 60000;
}

// Usage
#include "config/timing_config.h"
vTaskDelay(pdMS_TO_TICKS(TimingConfig::DISCOVERY_PROBE_INTERVAL_MS));
```

### Benefits

- ✅ **Consistency**: All timing uses same values
- ✅ **Maintainability**: Change once, updates everywhere
- ✅ **Documentation**: Clear intent of each timing
- ✅ **Tuning**: Easy to tune all timeouts from one file

---

## 7. Blocking Delays Elimination (Long-Term)

### Priority: 🟡 **MEDIUM**
**Effort**: 2 days  
**Blocking**: Partially - some blocking is OK in lower-priority tasks

### Note

This is more of a long-term architectural improvement. Individual blocking delays (e.g., 100ms for channel stabilization) are acceptable in dedicated tasks. The key is to avoid blocking in critical paths like RX or transmission.

**What to keep blocking**:
- Discovery task channel stabilization (non-critical, separate task)
- Initialization delays (one-time cost)

**What to eliminate**:
- Blocking in RX critical path
- Blocking in transmission task
- Blocking in time-sensitive callbacks

---

## 8. OTA Version Verification

### Priority: 🟡 **MEDIUM**
**Effort**: 1 day  
**Blocking**: No - but recommended before production

### Problem

No firmware integrity checking:

```cpp
// ota_manager.cpp - Current
void OtaManager::perform_update(const char* url) {
    httpUpdate.update(client, url);
    // ← No version checking, just downloads and reboots
}
```

### Solution

```cpp
// ota/ota_manager.h
class OtaManager {
public:
    struct FirmwareMetadata {
        uint32_t version;
        uint32_t build_timestamp;
        uint32_t file_size;
        uint32_t checksum;
    };
    
    enum class OtaState {
        IDLE,
        CHECKING_VERSION,
        DOWNLOADING,
        VERIFYING,
        INSTALLING,
        SUCCESS,
        FAILED
    };
    
    OtaState perform_update(const char* url, const FirmwareMetadata& expected);
    
private:
    bool verify_firmware_integrity(uint32_t expected_checksum);
    bool is_version_newer(uint32_t new_version, uint32_t current_version);
};
```

---

## Summary

### Implementation Order

**Week 1** (Critical fixes):
1. MQTT State Machine → Prevents permanent offline
2. Ethernet Timeout → Prevents system hang
3. Settings Validation → Prevents corruption

**Week 2** (High priority):
4. Event-Driven Discovery → Responsiveness
5. Queue Encapsulation → Monitoring

**Week 3** (Medium priority):
6. Magic Numbers → Clarity
7. OTA Verification → Firmware integrity

### Total Effort

- **Critical**: 2.5 days
- **High**: 2 days
- **Medium**: 2 days

**Total**: ~6.5 development days for transmitter-specific improvements

### Key Points

- **MQTT reconnection is the most critical fix** - device can hang indefinitely without it
- **Ethernet timeout is second most critical** - prevents system hang on broken DHCP
- **Settings validation prevents corruption** - safety against invalid config
- **Event-driven discovery improves responsiveness** - can receive during discovery
- All improvements are independent of receiver

