# ESP32 Debugging Guide

## Common Issues and Solutions

### Brownout Detection

**Symptom:** Device continuously resets with "Brownout detector was triggered" message

**Root Cause:** Power supply cannot provide sufficient current during radio initialization

**Common Triggers:**
- Simultaneous Ethernet and WiFi initialization
- Network operations (NTP sync, HTTP requests) during early boot
- ESP-NOW channel scanning while Ethernet is active
- PSRAM initialization combined with radio startup

**Solutions:**

1. **Hardware Solutions (Recommended):**
   - Use quality power supply capable of 1A+ sustained current
   - Add bulk capacitors (100-470µF) near ESP32 power pins
   - Add ceramic capacitors (100nF) close to VDD pins
   - Ensure adequate power supply voltage (5V for USB, check regulator output)
   - Check for voltage drop on power traces/cables
   - Use shielded USB cables with adequate wire gauge

2. **Software Mitigations:**
   - Initialize radios sequentially, not simultaneously
   - Delay network operations until after boot stabilization
   - Reduce WiFi TX power during initialization: `WiFi.setTxPower(WIFI_POWER_8_5dBm)`
   - Disable PSRAM if not essential
   - Use `esp_wifi_set_ps(WIFI_PS_MAX_MODEM)` for power saving

3. **Code Pattern (if hardware fix not possible):**
   ```cpp
   // Initialize Ethernet first
   EthernetManager::instance().init();
   
   // Let Ethernet fully stabilize before WiFi
   delay(2000);
   
   // Initialize WiFi with reduced power
   WiFi.setTxPower(WIFI_POWER_8_5dBm);
   WiFi.mode(WIFI_STA);
   
   // Delay network requests until much later
   delay(5000);
   ```

**Note:** Software delays are workarounds. Proper solution is adequate power supply.

---

## Syslog Integration

### Overview
Replace Serial.print debugging with network-based syslog for remote monitoring and diagnostics.

### Implementation Location
`esp32common/logging_utilities/`

### Features Required
1. UDP syslog client (RFC 5424 compliant)
2. Dynamic log level control via web interface
3. Fallback to Serial when network unavailable
4. Circular buffer for log messages during network outage
5. Hostname/device identification in syslog messages

### Log Levels (RFC 5424)
```cpp
enum SyslogLevel {
    SYSLOG_EMERG   = 0,  // System unusable
    SYSLOG_ALERT   = 1,  // Action must be taken immediately
    SYSLOG_CRIT    = 2,  // Critical conditions
    SYSLOG_ERR     = 3,  // Error conditions
    SYSLOG_WARNING = 4,  // Warning conditions
    SYSLOG_NOTICE  = 5,  // Normal but significant
    SYSLOG_INFO    = 6,  // Informational
    SYSLOG_DEBUG   = 7   // Debug messages
};
```

### Proposed API

```cpp
// logging_utilities/syslog_client.h
class SyslogClient {
public:
    static SyslogClient& instance();
    
    void init(const char* syslog_server, uint16_t port = 514);
    void set_hostname(const char* hostname);
    void set_app_name(const char* app_name);
    void set_level(SyslogLevel min_level);
    SyslogLevel get_level() const;
    
    void log(SyslogLevel level, const char* format, ...);
    
    // Web API integration
    bool set_level_from_string(const char* level_str);
    const char* get_level_string() const;
    
private:
    WiFiUDP udp_;
    IPAddress server_ip_;
    uint16_t port_;
    String hostname_;
    String app_name_;
    SyslogLevel min_level_;
    
    void send_syslog(SyslogLevel level, const String& message);
};

// Convenience macros
#define SYSLOG_EMERG(...)   SyslogClient::instance().log(SYSLOG_EMERG, __VA_ARGS__)
#define SYSLOG_ALERT(...)   SyslogClient::instance().log(SYSLOG_ALERT, __VA_ARGS__)
#define SYSLOG_CRIT(...)    SyslogClient::instance().log(SYSLOG_CRIT, __VA_ARGS__)
#define SYSLOG_ERR(...)     SyslogClient::instance().log(SYSLOG_ERR, __VA_ARGS__)
#define SYSLOG_WARNING(...) SyslogClient::instance().log(SYSLOG_WARNING, __VA_ARGS__)
#define SYSLOG_NOTICE(...)  SyslogClient::instance().log(SYSLOG_NOTICE, __VA_ARGS__)
#define SYSLOG_INFO(...)    SyslogClient::instance().log(SYSLOG_INFO, __VA_ARGS__)
#define SYSLOG_DEBUG(...)   SyslogClient::instance().log(SYSLOG_DEBUG, __VA_ARGS__)
```

### Web Interface Integration

Add to web settings page:
```html
<h3>Debug Settings</h3>
<select id="syslog-level">
    <option value="emerg">Emergency</option>
    <option value="alert">Alert</option>
    <option value="crit">Critical</option>
    <option value="err">Error</option>
    <option value="warning">Warning</option>
    <option value="notice">Notice</option>
    <option value="info">Info</option>
    <option value="debug">Debug</option>
</select>
<button onclick="updateSyslogLevel()">Update Level</button>
```

### MQTT Integration (Alternative)
For better integration with existing MQTT infrastructure:
```cpp
void publish_debug_message(SyslogLevel level, const char* message) {
    String topic = "transmitter/" + device_id + "/debug/" + level_to_string(level);
    mqtt_client.publish(topic.c_str(), message);
}
```

### Configuration
Add to `network_config.h`:
```cpp
namespace syslog_config {
    constexpr const char* SERVER = "192.168.1.100";  // Syslog server IP
    constexpr uint16_t PORT = 514;                   // Standard syslog UDP port
    constexpr SyslogLevel DEFAULT_LEVEL = SYSLOG_INFO;
    constexpr const char* HOSTNAME = "esp32-transmitter";
    constexpr const char* APP_NAME = "espnow-tx";
}
```

### Best Practices

1. **Always include fallback to Serial:**
   ```cpp
   if (!network_available()) {
       Serial.printf("[%s] %s\n", level_str, message);
   } else {
       send_syslog(level, message);
   }
   ```

2. **Rate limiting for debug messages:**
   ```cpp
   static unsigned long last_debug = 0;
   if (millis() - last_debug > 100) {  // Max 10 debug/sec
       SYSLOG_DEBUG("...");
       last_debug = millis();
   }
   ```

3. **Message buffering for startup:**
   ```cpp
   // Buffer messages until network ready
   std::vector<String> startup_buffer;
   if (!network_ready && startup_buffer.size() < 50) {
       startup_buffer.push_back(message);
   }
   ```

4. **Critical errors always to Serial:**
   ```cpp
   void log(SyslogLevel level, const char* format, ...) {
       // Always output critical errors to Serial
       if (level <= SYSLOG_CRIT) {
           Serial.printf(...);
       }
       // Then send to syslog if available
       send_syslog(level, message);
   }
   ```

### Testing Syslog Server

**Linux/macOS:**
```bash
# Listen for syslog messages
nc -lu 514
# or use rsyslog/syslog-ng
```

**Windows:**
- Use "Kiwi Syslog Server" or similar
- Configure to listen on UDP port 514

### Performance Considerations

- Syslog over UDP is fire-and-forget (no ACK, fast)
- No buffering = low memory overhead
- Typical message: ~200 bytes
- Network overhead: ~300 bytes/message with headers
- At INFO level: ~10-20 messages/second typical

### Future Enhancements

1. **Log rotation via MQTT:**
   - Subscribe to `transmitter/{id}/debug/set_level`
   - Publish current level to `transmitter/{id}/debug/level`

2. **Structured logging (JSON):**
   ```cpp
   SYSLOG_INFO_JSON({
       {"event": "mqtt_connect"},
       {"broker": broker_ip},
       {"duration_ms": duration}
   });
   ```

3. **Log filtering by component:**
   ```cpp
   SYSLOG_INFO_TAG("MQTT", "Connected to broker");
   ```

4. **Remote log retrieval:**
   - Store last N messages in circular buffer
   - Retrieve via web API endpoint: `/api/logs?level=debug&count=100`

---

## Version 2: MQTT-Based Debug System (Recommended)

### Overview
Leverage existing MQTT infrastructure for debugging. All debug messages published to MQTT topics with dynamic level control via web interface.

### Implementation Location
`esp32common/logging_utilities/mqtt_logger.h`  
`esp32common/logging_utilities/mqtt_logger.cpp`

### Advantages over Syslog
- ✅ Uses existing MQTT connection (no additional network overhead)
- ✅ Message persistence and replay via MQTT broker
- ✅ Easy integration with Node-RED, Home Assistant, Grafana
- ✅ QoS support for guaranteed delivery of critical messages
- ✅ Topic-based filtering (subscribe only to what you need)
- ✅ Retained messages for last state visibility

### MQTT Topic Structure
```
transmitter/{device_id}/debug/emerg      # Emergency (QoS 2, retained)
transmitter/{device_id}/debug/alert      # Alert (QoS 2, retained)
transmitter/{device_id}/debug/crit       # Critical (QoS 2)
transmitter/{device_id}/debug/error      # Error (QoS 1)
transmitter/{device_id}/debug/warning    # Warning (QoS 0)
transmitter/{device_id}/debug/notice     # Notice (QoS 0)
transmitter/{device_id}/debug/info       # Info (QoS 0)
transmitter/{device_id}/debug/debug      # Debug (QoS 0)

# Control topics
transmitter/{device_id}/debug/set_level  # Subscribe: change log level
transmitter/{device_id}/debug/level      # Publish: current level (retained)
transmitter/{device_id}/debug/status     # System status (retained)
```

### Proposed API

```cpp
// logging_utilities/mqtt_logger.h
#pragma once
#include <PubSubClient.h>
#include <functional>

enum MqttLogLevel {
    MQTT_LOG_EMERG   = 0,  // System unusable
    MQTT_LOG_ALERT   = 1,  // Action must be taken immediately
    MQTT_LOG_CRIT    = 2,  // Critical conditions
    MQTT_LOG_ERROR   = 3,  // Error conditions
    MQTT_LOG_WARNING = 4,  // Warning conditions
    MQTT_LOG_NOTICE  = 5,  // Normal but significant
    MQTT_LOG_INFO    = 6,  // Informational
    MQTT_LOG_DEBUG   = 7   // Debug messages
};

class MqttLogger {
public:
    static MqttLogger& instance();
    
    // Initialize with existing MQTT client
    void init(PubSubClient* mqtt_client, const char* device_id);
    
    // Set minimum log level (messages below this are ignored)
    void set_level(MqttLogLevel min_level);
    MqttLogLevel get_level() const { return min_level_; }
    
    // Main logging function
    void log(MqttLogLevel level, const char* tag, const char* format, ...);
    
    // Subscribe to control topics and handle level changes
    void subscribe_control_topics();
    void handle_level_command(const char* payload);
    
    // Publish current configuration
    void publish_status();
    
    // String conversions
    const char* level_to_string(MqttLogLevel level) const;
    MqttLogLevel string_to_level(const char* level_str) const;
    
private:
    MqttLogger() = default;
    
    PubSubClient* mqtt_client_ = nullptr;
    String device_id_;
    String topic_prefix_;
    MqttLogLevel min_level_ = MQTT_LOG_INFO;
    bool initialized_ = false;
    
    // Circular buffer for messages when MQTT unavailable
    static const size_t BUFFER_SIZE = 20;
    struct BufferedMessage {
        MqttLogLevel level;
        String tag;
        String message;
        unsigned long timestamp;
    };
    BufferedMessage buffer_[BUFFER_SIZE];
    size_t buffer_head_ = 0;
    size_t buffer_count_ = 0;
    
    void publish_message(MqttLogLevel level, const char* tag, const char* message);
    void flush_buffer();
    uint8_t get_qos(MqttLogLevel level) const;
    bool get_retained(MqttLogLevel level) const;
};

// Convenience macros with automatic tagging
#define MQTT_LOG_EMERG(tag, ...)   MqttLogger::instance().log(MQTT_LOG_EMERG, tag, __VA_ARGS__)
#define MQTT_LOG_ALERT(tag, ...)   MqttLogger::instance().log(MQTT_LOG_ALERT, tag, __VA_ARGS__)
#define MQTT_LOG_CRIT(tag, ...)    MqttLogger::instance().log(MQTT_LOG_CRIT, tag, __VA_ARGS__)
#define MQTT_LOG_ERROR(tag, ...)   MqttLogger::instance().log(MQTT_LOG_ERROR, tag, __VA_ARGS__)
#define MQTT_LOG_WARNING(tag, ...) MqttLogger::instance().log(MQTT_LOG_WARNING, tag, __VA_ARGS__)
#define MQTT_LOG_NOTICE(tag, ...)  MqttLogger::instance().log(MQTT_LOG_NOTICE, tag, __VA_ARGS__)
#define MQTT_LOG_INFO(tag, ...)    MqttLogger::instance().log(MQTT_LOG_INFO, tag, __VA_ARGS__)
#define MQTT_LOG_DEBUG(tag, ...)   MqttLogger::instance().log(MQTT_LOG_DEBUG, tag, __VA_ARGS__)

// Auto-tag from function name (C++20)
#define LOG_E(...) MQTT_LOG_ERROR(__func__, __VA_ARGS__)
#define LOG_W(...) MQTT_LOG_WARNING(__func__, __VA_ARGS__)
#define LOG_I(...) MQTT_LOG_INFO(__func__, __VA_ARGS__)
#define LOG_D(...) MQTT_LOG_DEBUG(__func__, __VA_ARGS__)
```

### Implementation Example

```cpp
// logging_utilities/mqtt_logger.cpp
#include "mqtt_logger.h"
#include <Arduino.h>

MqttLogger& MqttLogger::instance() {
    static MqttLogger instance;
    return instance;
}

void MqttLogger::init(PubSubClient* mqtt_client, const char* device_id) {
    mqtt_client_ = mqtt_client;
    device_id_ = device_id;
    topic_prefix_ = String("transmitter/") + device_id_ + "/debug/";
    initialized_ = true;
    
    // Publish initial status
    publish_status();
}

void MqttLogger::log(MqttLogLevel level, const char* tag, const char* format, ...) {
    // Filter by level
    if (level > min_level_) return;
    
    // Format message
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    // Always output critical messages to Serial
    if (level <= MQTT_LOG_CRIT) {
        Serial.printf("[%s][%s] %s\n", level_to_string(level), tag, buffer);
    }
    
    // Publish to MQTT
    if (initialized_ && mqtt_client_ && mqtt_client_->connected()) {
        publish_message(level, tag, buffer);
        
        // Flush any buffered messages
        if (buffer_count_ > 0) {
            flush_buffer();
        }
    } else {
        // Buffer message if MQTT not available
        if (buffer_count_ < BUFFER_SIZE) {
            buffer_[buffer_head_].level = level;
            buffer_[buffer_head_].tag = tag;
            buffer_[buffer_head_].message = buffer;
            buffer_[buffer_head_].timestamp = millis();
            buffer_head_ = (buffer_head_ + 1) % BUFFER_SIZE;
            buffer_count_++;
        }
        
        // Fallback to Serial
        Serial.printf("[%s][%s] %s\n", level_to_string(level), tag, buffer);
    }
}

void MqttLogger::publish_message(MqttLogLevel level, const char* tag, const char* message) {
    // Build topic
    String topic = topic_prefix_ + level_to_string(level);
    
    // Build JSON payload with metadata
    String payload = String("{\"tag\":\"") + tag + 
                    "\",\"msg\":\"" + message + 
                    "\",\"uptime\":" + String(millis()) +
                    ",\"heap\":" + String(ESP.getFreeHeap()) + "}";
    
    // Publish with appropriate QoS and retain flag
    mqtt_client_->publish(topic.c_str(), payload.c_str(), 
                          get_retained(level), get_qos(level));
}

uint8_t MqttLogger::get_qos(MqttLogLevel level) const {
    if (level <= MQTT_LOG_ALERT) return 2;  // Guaranteed delivery
    if (level <= MQTT_LOG_ERROR) return 1;  // At least once
    return 0;  // Best effort
}

bool MqttLogger::get_retained(MqttLogLevel level) const {
    // Retain only critical messages for visibility
    return (level <= MQTT_LOG_ALERT);
}

void MqttLogger::flush_buffer() {
    for (size_t i = 0; i < buffer_count_; i++) {
        size_t idx = (buffer_head_ - buffer_count_ + i) % BUFFER_SIZE;
        publish_message(buffer_[idx].level, 
                       buffer_[idx].tag.c_str(), 
                       buffer_[idx].message.c_str());
    }
    buffer_count_ = 0;
}

void MqttLogger::subscribe_control_topics() {
    if (!mqtt_client_ || !mqtt_client_->connected()) return;
    
    String topic = topic_prefix_ + "set_level";
    mqtt_client_->subscribe(topic.c_str());
}

void MqttLogger::handle_level_command(const char* payload) {
    MqttLogLevel new_level = string_to_level(payload);
    if (new_level != min_level_) {
        min_level_ = new_level;
        publish_status();
        Serial.printf("Log level changed to: %s\n", level_to_string(new_level));
    }
}

void MqttLogger::publish_status() {
    if (!mqtt_client_ || !mqtt_client_->connected()) return;
    
    String topic = topic_prefix_ + "level";
    mqtt_client_->publish(topic.c_str(), level_to_string(min_level_), true);
    
    // Publish detailed status
    String status_topic = topic_prefix_ + "status";
    String status = String("{\"level\":\"") + level_to_string(min_level_) + 
                   "\",\"device\":\"" + device_id_ + 
                   "\",\"uptime\":" + String(millis()) + "}";
    mqtt_client_->publish(status_topic.c_str(), status.c_str(), true);
}

const char* MqttLogger::level_to_string(MqttLogLevel level) const {
    switch (level) {
        case MQTT_LOG_EMERG:   return "emerg";
        case MQTT_LOG_ALERT:   return "alert";
        case MQTT_LOG_CRIT:    return "crit";
        case MQTT_LOG_ERROR:   return "error";
        case MQTT_LOG_WARNING: return "warning";
        case MQTT_LOG_NOTICE:  return "notice";
        case MQTT_LOG_INFO:    return "info";
        case MQTT_LOG_DEBUG:   return "debug";
        default:               return "unknown";
    }
}

MqttLogLevel MqttLogger::string_to_level(const char* level_str) const {
    if (strcasecmp(level_str, "emerg") == 0)   return MQTT_LOG_EMERG;
    if (strcasecmp(level_str, "alert") == 0)   return MQTT_LOG_ALERT;
    if (strcasecmp(level_str, "crit") == 0)    return MQTT_LOG_CRIT;
    if (strcasecmp(level_str, "error") == 0)   return MQTT_LOG_ERROR;
    if (strcasecmp(level_str, "warning") == 0) return MQTT_LOG_WARNING;
    if (strcasecmp(level_str, "notice") == 0)  return MQTT_LOG_NOTICE;
    if (strcasecmp(level_str, "info") == 0)    return MQTT_LOG_INFO;
    if (strcasecmp(level_str, "debug") == 0)   return MQTT_LOG_DEBUG;
    return min_level_;  // Keep current if invalid
}
```

### Integration in main.cpp

```cpp
#include "logging_utilities/mqtt_logger.h"

void setup() {
    // ... existing MQTT setup ...
    
    // Initialize MQTT logger
    MqttLogger::instance().init(&mqtt_client, "transmitter-001");
    MqttLogger::instance().set_level(MQTT_LOG_INFO);
    MqttLogger::instance().subscribe_control_topics();
    
    // Example usage
    LOG_I("System initialized, heap: %u", ESP.getFreeHeap());
    MQTT_LOG_DEBUG("ETH", "Ethernet connected: %s", eth_ip.toString().c_str());
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
    String topic_str = String(topic);
    String payload_str = String((char*)payload).substring(0, length);
    
    // Handle log level changes
    if (topic_str.endsWith("/debug/set_level")) {
        MqttLogger::instance().handle_level_command(payload_str.c_str());
        return;
    }
    
    // ... other MQTT message handling ...
}
```

### Web Interface HTML

```html
<div class="settings-section">
    <h3>Debug Settings</h3>
    <div class="form-group">
        <label for="debug-level">Log Level:</label>
        <select id="debug-level" onchange="updateDebugLevel()">
            <option value="emerg">Emergency</option>
            <option value="alert">Alert</option>
            <option value="crit">Critical</option>
            <option value="error">Error</option>
            <option value="warning">Warning</option>
            <option value="notice">Notice</option>
            <option value="info" selected>Info</option>
            <option value="debug">Debug</option>
        </select>
    </div>
    <div class="status">
        Current Level: <span id="current-level">info</span>
    </div>
</div>

<script>
function updateDebugLevel() {
    const level = document.getElementById('debug-level').value;
    
    fetch('/api/debug/set_level', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({level: level})
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            document.getElementById('current-level').textContent = level;
            console.log('Debug level updated to:', level);
        }
    });
}

// Also subscribe to MQTT topic to show current level
// (requires MQTT over WebSocket or separate MQTT client)
</script>
```

### Web Server API Endpoint

```cpp
// In your web server setup
server.on("/api/debug/set_level", HTTP_POST, [](AsyncWebServerRequest *request){}, 
    NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
    
    // Parse JSON
    DynamicJsonDocument doc(256);
    deserializeJson(doc, (const char*)data);
    const char* level = doc["level"];
    
    // Update logger
    MqttLogger::instance().handle_level_command(level);
    
    // Also publish to MQTT for other subscribers
    String topic = "transmitter/" + device_id + "/debug/set_level";
    mqtt_client.publish(topic.c_str(), level);
    
    // Send response
    request->send(200, "application/json", "{\"success\":true}");
});

server.on("/api/debug/level", HTTP_GET, [](AsyncWebServerRequest *request){
    const char* level = MqttLogger::instance().level_to_string(
        MqttLogger::instance().get_level()
    );
    
    String json = String("{\"level\":\"") + level + "\"}";
    request->send(200, "application/json", json);
});
```

### MQTT Broker Configuration

**IMPORTANT:** Configure your MQTT broker to handle debug message load:

```conf
# mosquitto.conf
max_queued_messages 1000
message_size_limit 1024

# Optional: separate ACLs for debug topics
# Only admins can change log levels
user admin
topic write transmitter/+/debug/set_level

# All users can read debug messages
user viewer
topic read transmitter/+/debug/#
```

### Monitoring Examples

**Subscribe to all errors and above:**
```bash
mosquitto_sub -h broker.local -t "transmitter/+/debug/error" \
                                -t "transmitter/+/debug/crit" \
                                -t "transmitter/+/debug/alert" \
                                -t "transmitter/+/debug/emerg"
```

**Change log level remotely:**
```bash
mosquitto_pub -h broker.local \
              -t "transmitter/transmitter-001/debug/set_level" \
              -m "debug"
```

**Node-RED flow example:**
```json
[
    {
        "id": "mqtt_debug_in",
        "type": "mqtt in",
        "topic": "transmitter/+/debug/#",
        "broker": "mqtt_broker",
        "output": "auto"
    },
    {
        "id": "debug_parser",
        "type": "json",
        "property": "payload"
    },
    {
        "id": "error_filter",
        "type": "switch",
        "property": "topic",
        "rules": [
            {"t": "regex", "v": "/error$"},
            {"t": "regex", "v": "/crit$"},
            {"t": "regex", "v": "/alert$"}
        ]
    },
    {
        "id": "notification",
        "type": "email",
        "to": "admin@example.com"
    }
]
```

### Performance & Best Practices

**Message Rate Limiting:**
```cpp
void rate_limited_debug(const char* tag, const char* msg) {
    static unsigned long last_msg[10] = {0};
    static uint32_t msg_hash = 0;
    
    // Hash message to detect duplicates
    msg_hash = hash(tag) ^ hash(msg);
    int slot = msg_hash % 10;
    
    if (millis() - last_msg[slot] > 1000) {  // Max 1 msg/sec per unique message
        MQTT_LOG_DEBUG(tag, "%s", msg);
        last_msg[slot] = millis();
    }
}
```

**Memory Considerations:**
- Circular buffer: 20 messages × ~100 bytes = 2KB
- JSON payload: ~150 bytes typical
- MQTT overhead: ~50 bytes
- Total per message: ~200 bytes

**Network Load:**
- At INFO level: ~5-10 msg/sec = 1-2 KB/sec
- At DEBUG level: ~20-50 msg/sec = 4-10 KB/sec
- Consider impact on existing MQTT traffic

### Migration from Serial.print

**Find and Replace:**
```cpp
// Old:
Serial.printf("Error: %s\n", msg);

// New:
MQTT_LOG_ERROR("MODULE", "Error: %s", msg);

// Or with auto-tagging:
LOG_E("Error: %s", msg);  // Uses function name as tag
```

**Conditional Compilation:**
```cpp
#define USE_MQTT_LOGGING 1

#if USE_MQTT_LOGGING
    #define DEBUG_LOG(...) MQTT_LOG_DEBUG(__func__, __VA_ARGS__)
#else
    #define DEBUG_LOG(...) Serial.printf(__VA_ARGS__)
#endif
```

### Advantages Summary

| Feature | Syslog (V1) | MQTT (V2) |
|---------|-------------|-----------|
| Setup Complexity | Medium | Low (reuses MQTT) |
| Message Persistence | No | Yes (broker) |
| QoS Support | No | Yes |
| Filtering | Client-side | Topic-based |
| Integration | Limited | Excellent |
| Bandwidth | Lower | Slightly higher |
| Reliability | Fire-and-forget | Configurable QoS |
| Web Interface | Requires WebSocket | Native support |

**Recommendation:** Use MQTT Version 2 for better integration with your existing infrastructure.

---

## ESP-NOW Integration for Debug Level Control

### Overview
Since the web interface runs on the **receiver** and needs to control the **transmitter's** debug level, we use ESP-NOW messaging for real-time control without MQTT dependency.

### Message Flow
```
[Web Interface] → [Receiver] → [ESP-NOW] → [Transmitter]
                                               ↓
                                       [Update Debug Level]
                                               ↓
                                       [Send ACK via ESP-NOW]
                                               ↓
                            [Receiver] → [Web Interface Response]
```

### ESP-NOW Packet Definition

Add to `esp32common/espnow_common_utils/espnow_packet_utils.h`:

```cpp
// Debug control packet
#define PACKET_TYPE_DEBUG_CONTROL 0x11

struct debug_control_packet {
    uint8_t packet_type;           // PACKET_TYPE_DEBUG_CONTROL
    uint8_t debug_level;           // 0-7 (EMERG to DEBUG)
    uint8_t flags;                 // Reserved for future use
    uint8_t checksum;
} __attribute__((packed));

// ACK packet for debug control
#define PACKET_TYPE_DEBUG_ACK 0x12

struct debug_ack_packet {
    uint8_t packet_type;           // PACKET_TYPE_DEBUG_ACK
    uint8_t applied_level;         // Level that was applied
    uint8_t previous_level;        // Previous level before change
    uint8_t status;                // 0=success, 1=invalid level, 2=error
} __attribute__((packed));
```

### Transmitter Side Implementation

**1. Add ESP-NOW Handler**

Add to `espnowtransmitter2/src/espnow/message_handler.cpp`:

```cpp
#include "logging_utilities/mqtt_logger.h"
#include <Preferences.h>

void EspnowMessageHandler::handle_debug_control(const debug_control_packet* pkt) {
    LOG_I("Received debug level change request: %u", pkt->debug_level);
    
    // Validate level
    if (pkt->debug_level > MQTT_LOG_DEBUG) {
        LOG_W("Invalid debug level: %u", pkt->debug_level);
        send_debug_ack(pkt->debug_level, MQTT_LOG_DEBUG, 1);
        return;
    }
    
    // Store previous level
    MqttLogLevel previous = MqttLogger::instance().get_level();
    
    // Apply new level
    MqttLogger::instance().set_level((MqttLogLevel)pkt->debug_level);
    
    // Save to preferences for persistence
    save_debug_level(pkt->debug_level);
    
    // Publish to MQTT
    MqttLogger::instance().publish_status();
    
    LOG_I("Debug level changed: %s → %s", 
          MqttLogger::instance().level_to_string(previous),
          MqttLogger::instance().level_to_string((MqttLogLevel)pkt->debug_level));
    
    // Send acknowledgment
    send_debug_ack(pkt->debug_level, previous, 0);
}

void EspnowMessageHandler::send_debug_ack(uint8_t applied, uint8_t previous, uint8_t status) {
    debug_ack_packet ack = {
        .packet_type = PACKET_TYPE_DEBUG_ACK,
        .applied_level = applied,
        .previous_level = previous,
        .status = status
    };
    
    // Send to receiver (stored peer address)
    esp_now_send(receiver_mac_, (uint8_t*)&ack, sizeof(ack));
}

void EspnowMessageHandler::save_debug_level(uint8_t level) {
    Preferences prefs;
    if (prefs.begin("debug", false)) {
        prefs.putUChar("log_level", level);
        prefs.end();
        LOG_D("Debug level saved to NVS: %u", level);
    }
}

uint8_t EspnowMessageHandler::load_debug_level() {
    Preferences prefs;
    uint8_t level = MQTT_LOG_INFO;  // Default
    
    if (prefs.begin("debug", true)) {
        level = prefs.getUChar("log_level", MQTT_LOG_INFO);
        prefs.end();
        LOG_I("Debug level loaded from NVS: %u", level);
    }
    
    return level;
}
```

**2. Register Handler in Message Router**

Add to `espnow_message_router.cpp`:

```cpp
void register_standard_handlers() {
    // ... existing handlers ...
    
    register_handler(PACKET_TYPE_DEBUG_CONTROL, [](const uint8_t* data, size_t len) {
        if (len == sizeof(debug_control_packet)) {
            auto* pkt = reinterpret_cast<const debug_control_packet*>(data);
            EspnowMessageHandler::instance().handle_debug_control(pkt);
        }
    });
}
```

**3. Load Saved Level on Boot**

Add to `main.cpp` setup():

```cpp
void setup() {
    // ... existing setup ...
    
    // Initialize MQTT logger with saved level
    uint8_t saved_level = EspnowMessageHandler::instance().load_debug_level();
    MqttLogger::instance().init(&mqtt_client, "transmitter-001");
    MqttLogger::instance().set_level((MqttLogLevel)saved_level);
    
    LOG_I("Debug system initialized, level: %s", 
          MqttLogger::instance().level_to_string((MqttLogLevel)saved_level));
}
```

### Receiver Side Implementation

**1. Add Web API Endpoint**

Add to `espnowreciever_2/lib/webserver/settings_handler.cpp`:

```cpp
void setup_debug_endpoints(AsyncWebServer& server) {
    // Get current debug level
    server.on("/api/debug/level", HTTP_GET, [](AsyncWebServerRequest *request){
        uint8_t level = get_transmitter_debug_level();  // From state machine
        
        StaticJsonDocument<128> doc;
        doc["level"] = level;
        doc["level_name"] = debug_level_to_string(level);
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
    
    // Set debug level (sends ESP-NOW message)
    server.on("/api/debug/set_level", HTTP_POST, [](AsyncWebServerRequest *request){}, 
        NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
        
        // Parse JSON
        StaticJsonDocument<128> doc;
        DeserializationError error = deserializeJson(doc, (const char*)data);
        
        if (error) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        
        uint8_t level;
        if (doc["level"].is<uint8_t>()) {
            level = doc["level"];
        } else if (doc["level"].is<const char*>()) {
            level = debug_string_to_level(doc["level"]);
        } else {
            request->send(400, "application/json", "{\"error\":\"Invalid level\"}");
            return;
        }
        
        // Send ESP-NOW message to transmitter
        bool success = send_debug_control_to_transmitter(level);
        
        if (success) {
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Command sent\"}");
        } else {
            request->send(500, "application/json", "{\"success\":false,\"error\":\"Failed to send\"}");
        }
    });
}

bool send_debug_control_to_transmitter(uint8_t level) {
    debug_control_packet pkt = {
        .packet_type = PACKET_TYPE_DEBUG_CONTROL,
        .debug_level = level,
        .flags = 0,
        .checksum = calculate_checksum((uint8_t*)&pkt, sizeof(pkt) - 1)
    };
    
    // Send via ESP-NOW to transmitter
    esp_err_t result = esp_now_send(transmitter_mac, (uint8_t*)&pkt, sizeof(pkt));
    
    if (result == ESP_OK) {
        LOG_INFO("Debug control sent: level=%u", level);
        return true;
    } else {
        LOG_ERROR("Failed to send debug control: %d", result);
        return false;
    }
}

const char* debug_level_to_string(uint8_t level) {
    const char* levels[] = {"emerg", "alert", "crit", "error", "warning", "notice", "info", "debug"};
    return (level <= 7) ? levels[level] : "unknown";
}

uint8_t debug_string_to_level(const char* level_str) {
    if (strcasecmp(level_str, "emerg") == 0)   return 0;
    if (strcasecmp(level_str, "alert") == 0)   return 1;
    if (strcasecmp(level_str, "crit") == 0)    return 2;
    if (strcasecmp(level_str, "error") == 0)   return 3;
    if (strcasecmp(level_str, "warning") == 0) return 4;
    if (strcasecmp(level_str, "notice") == 0)  return 5;
    if (strcasecmp(level_str, "info") == 0)    return 6;
    if (strcasecmp(level_str, "debug") == 0)   return 7;
    return 6;  // Default to INFO
}
```

**2. Handle ACK Response**

Add to receiver's ESP-NOW message handler:

```cpp
void handle_debug_ack(const debug_ack_packet* pkt) {
    if (pkt->status == 0) {
        LOG_INFO("Debug level changed successfully: %s → %s",
                 debug_level_to_string(pkt->previous_level),
                 debug_level_to_string(pkt->applied_level));
        
        // Update local state
        set_transmitter_debug_level(pkt->applied_level);
        
        // Notify web clients (via WebSocket if available)
        notify_web_clients_debug_change(pkt->applied_level);
    } else {
        LOG_WARN("Debug level change failed, status: %u", pkt->status);
    }
}

void register_receiver_handlers() {
    // ... existing handlers ...
    
    register_handler(PACKET_TYPE_DEBUG_ACK, [](const uint8_t* data, size_t len) {
        if (len == sizeof(debug_ack_packet)) {
            auto* pkt = reinterpret_cast<const debug_ack_packet*>(data);
            handle_debug_ack(pkt);
        }
    });
}
```

**3. Add to State Machine**

Add to `espnowreciever_2/src/state_machine.cpp`:

```cpp
// Global state for transmitter debug level
static uint8_t transmitter_debug_level = 6;  // Default INFO

uint8_t get_transmitter_debug_level() {
    return transmitter_debug_level;
}

void set_transmitter_debug_level(uint8_t level) {
    transmitter_debug_level = level;
}
```

### Web Interface Updates

**HTML Addition (settings page):**

```html
<div class="card">
    <h3>Transmitter Debug Settings</h3>
    <div class="form-group">
        <label for="debug-level">Log Level:</label>
        <select id="debug-level" class="form-control">
            <option value="0">Emergency</option>
            <option value="1">Alert</option>
            <option value="2">Critical</option>
            <option value="3">Error</option>
            <option value="4">Warning</option>
            <option value="5">Notice</option>
            <option value="6" selected>Info</option>
            <option value="7">Debug</option>
        </select>
    </div>
    <button onclick="updateDebugLevel()" class="btn btn-primary">Apply</button>
    <div class="status-message" id="debug-status"></div>
    <div class="current-level">
        Current Level: <strong id="current-debug-level">info</strong>
    </div>
</div>

<style>
.status-message {
    margin-top: 10px;
    padding: 8px;
    border-radius: 4px;
    display: none;
}
.status-message.success {
    background-color: #d4edda;
    color: #155724;
    display: block;
}
.status-message.error {
    background-color: #f8d7da;
    color: #721c24;
    display: block;
}
</style>
```

**JavaScript Implementation:**

```javascript
// Load current debug level on page load
async function loadDebugLevel() {
    try {
        const response = await fetch('/api/debug/level');
        const data = await response.json();
        
        document.getElementById('debug-level').value = data.level;
        document.getElementById('current-debug-level').textContent = data.level_name;
    } catch (error) {
        console.error('Failed to load debug level:', error);
    }
}

// Update debug level
async function updateDebugLevel() {
    const level = parseInt(document.getElementById('debug-level').value);
    const statusDiv = document.getElementById('debug-status');
    
    try {
        statusDiv.textContent = 'Sending command to transmitter...';
        statusDiv.className = 'status-message';
        statusDiv.style.display = 'block';
        
        const response = await fetch('/api/debug/set_level', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({level: level})
        });
        
        const data = await response.json();
        
        if (data.success) {
            statusDiv.textContent = 'Debug level updated successfully!';
            statusDiv.className = 'status-message success';
            
            // Update display (actual level will be updated when ACK received)
            const levelNames = ['emerg', 'alert', 'crit', 'error', 'warning', 'notice', 'info', 'debug'];
            document.getElementById('current-debug-level').textContent = levelNames[level];
            
            // Hide status message after 3 seconds
            setTimeout(() => {
                statusDiv.style.display = 'none';
            }, 3000);
        } else {
            statusDiv.textContent = 'Failed: ' + (data.error || 'Unknown error');
            statusDiv.className = 'status-message error';
        }
    } catch (error) {
        statusDiv.textContent = 'Error: ' + error.message;
        statusDiv.className = 'status-message error';
    }
}

// Load level on page load
document.addEventListener('DOMContentLoaded', loadDebugLevel);

// Optional: WebSocket support for real-time ACK updates
if (typeof ws !== 'undefined') {
    ws.on('debug_ack', (data) => {
        const levelNames = ['emerg', 'alert', 'crit', 'error', 'warning', 'notice', 'info', 'debug'];
        document.getElementById('current-debug-level').textContent = levelNames[data.applied_level];
        
        const statusDiv = document.getElementById('debug-status');
        statusDiv.textContent = 'Transmitter confirmed: ' + levelNames[data.applied_level];
        statusDiv.className = 'status-message success';
        setTimeout(() => statusDiv.style.display = 'none', 3000);
    });
}
```

### Complete Checklist for End-to-End Implementation

**Common Files (esp32common):**
- [ ] Add packet definitions to `espnow_packet_utils.h`
- [ ] Create `mqtt_logger.h` and `mqtt_logger.cpp` in `logging_utilities/`
- [ ] Add standard handler registration for `PACKET_TYPE_DEBUG_CONTROL`

**Transmitter Side (espnowtransmitter2):**
- [ ] Add `#include <Preferences.h>` to platformio.ini or includes
- [ ] Implement `handle_debug_control()` in message handler
- [ ] Implement `send_debug_ack()` function
- [ ] Implement `save_debug_level()` and `load_debug_level()` with Preferences
- [ ] Register debug control handler in message router
- [ ] Initialize MqttLogger with saved level in `main.cpp`
- [ ] Add dependency: PubSubClient library (if not already present)

**Receiver Side (espnowreciever_2):**
- [ ] Add `/api/debug/level` GET endpoint
- [ ] Add `/api/debug/set_level` POST endpoint
- [ ] Implement `send_debug_control_to_transmitter()` function
- [ ] Implement `handle_debug_ack()` handler
- [ ] Register debug ACK handler in message router
- [ ] Add state variables for transmitter debug level
- [ ] Update HTML settings page with debug control UI
- [ ] Add JavaScript for debug level control
- [ ] Add WebSocket support for real-time ACK (optional)

**Testing:**
- [ ] Test ESP-NOW message transmission (receiver → transmitter)
- [ ] Test ESP-NOW ACK reception (transmitter → receiver)
- [ ] Test debug level persistence across reboots
- [ ] Test MQTT publishing of debug messages at different levels
- [ ] Test web interface responsiveness
- [ ] Test invalid level handling
- [ ] Test behavior when transmitter offline
- [ ] Test concurrent level changes

**Additional Considerations:**

1. **Timeout Handling:**
```cpp
// In receiver, add timeout for ACK
struct PendingDebugCommand {
    uint8_t level;
    unsigned long timestamp;
    bool pending;
};

PendingDebugCommand pending_cmd = {0, 0, false};

void send_debug_control_to_transmitter(uint8_t level) {
    // ... send ESP-NOW ...
    
    pending_cmd.level = level;
    pending_cmd.timestamp = millis();
    pending_cmd.pending = true;
}

void check_pending_ack() {
    if (pending_cmd.pending && (millis() - pending_cmd.timestamp > 5000)) {
        LOG_WARN("Debug control ACK timeout");
        notify_web_clients_error("Transmitter did not respond");
        pending_cmd.pending = false;
    }
}
```

2. **Retry Mechanism:**
```cpp
void retry_debug_control(uint8_t level, uint8_t max_retries = 3) {
    for (uint8_t i = 0; i < max_retries; i++) {
        if (send_debug_control_to_transmitter(level)) {
            delay(100);  // Wait for ACK
            if (!pending_cmd.pending) return;  // ACK received
        }
        delay(500);
    }
    LOG_ERROR("Failed to set debug level after %u retries", max_retries);
}
```

3. **Security:**
```cpp
// Add authentication to prevent unauthorized debug level changes
struct debug_control_packet {
    uint8_t packet_type;
    uint8_t debug_level;
    uint8_t flags;
    uint8_t checksum;
    uint32_t auth_token;  // Simple token validation
} __attribute__((packed));
```

---

## Additional Debugging Tools

### Memory Monitoring
```cpp
void log_memory_status() {
    SYSLOG_INFO("Free heap: %u bytes, Min free: %u bytes",
                ESP.getFreeHeap(), ESP.getMinFreeHeap());
    
    if (psramFound()) {
        SYSLOG_INFO("Free PSRAM: %u bytes", ESP.getFreePsram());
    }
}
```

### Task Monitoring
```cpp
void log_task_stats() {
    char buf[512];
    vTaskGetRunTimeStats(buf);
    SYSLOG_DEBUG("Task stats:\n%s", buf);
}
```

### Network Diagnostics
```cpp
void log_network_status() {
    SYSLOG_INFO("Ethernet: %s, WiFi: %s, MQTT: %s, NTP: %s",
                eth_connected ? "UP" : "DOWN",
                wifi_connected ? "UP" : "DOWN",
                mqtt_connected ? "UP" : "DOWN",
                time_synced ? "SYNCED" : "UNSYNCED");
} 