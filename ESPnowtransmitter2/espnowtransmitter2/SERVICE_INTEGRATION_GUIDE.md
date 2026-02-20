# Service Integration Guide

**Version**: 1.0  
**Date**: February 19, 2026  
**For**: NTP, MQTT, OTA, Heartbeat services

---

## Overview

This guide explains how to properly integrate your services with the Ethernet state machine. Services must be **gated** (conditionally activated) based on Ethernet connectivity.

**Key Principle**: Services start when Ethernet enters CONNECTED state, stop when it enters LINK_LOST.

---

## Integration Pattern

### Pattern 1: Service Gating (Ethernet Only)

Use for services that ONLY need Ethernet (NTP, MQTT, OTA).

**Setup in `setup()`**:

```cpp
// Register callback when Ethernet becomes ready
EthernetManager::instance().on_connected([] {
    LOG_INFO("SERVICE", "Ethernet ready - starting NTP/MQTT/OTA");
    
    // Start NTP
    TimeManager::instance().sync();
    
    // Start MQTT
    if (config::features::MQTT_ENABLED) {
        MqttManager::instance().connect();
    }
    
    // Start OTA server
    OtaManager::instance().start_server();
});

// Register callback when Ethernet is no longer ready
EthernetManager::instance().on_disconnected([] {
    LOG_WARN("SERVICE", "Ethernet disconnected - stopping services");
    
    // Stop MQTT gracefully
    MqttManager::instance().disconnect();
    
    // Stop OTA server
    OtaManager::instance().stop_server();
    
    // NTP will naturally stop (no server to sync with)
});
```

**Inside Service Implementation**:

```cpp
void NtpManager::sync() {
    // Check Ethernet state BEFORE network I/O
    if (!EthernetManager::instance().is_fully_ready()) {
        LOG_WARN("NTP", "Ethernet not ready - skipping sync");
        return;
    }
    
    // Safe to make network call
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    LOG_INFO("NTP", "Sync started via Ethernet");
}
```

**Result**:
```
Boot sequence:
  T=0:00  Ethernet starting
  T=3:00  Ethernet CONNECTED
  T=3:00  ↓ on_connected() triggered
  T=3:01  NTP sync started
  T=3:01  MQTT client connecting
  T=3:02  OTA server ready

Unplug cable:
  T=60:00 Ethernet LINK_LOST
  T=60:00 ↓ on_disconnected() triggered
  T=60:01 MQTT client stopped
  T=60:01 OTA server stopped
```

---

### Pattern 2: Dual Gating (Ethernet + ESP-NOW)

Use for services that need BOTH networks (Heartbeat).

**Current Implementation in `heartbeat_manager.cpp`**:

```cpp
void HeartbeatManager::tick() {
    if (!m_initialized) return;
    
    // ✅ Gate 1: Check Ethernet (must have cable + IP)
    if (!EthernetManager::instance().is_fully_ready()) {
        return;  // Cable not present or IP not assigned
    }
    
    // ✅ Gate 2: Check ESP-NOW (must have receiver connection)
    if (EspNowConnectionManager::instance().get_state() != EspNowConnectionState::CONNECTED) {
        return;  // No receiver connection
    }
    
    // ✅ Both ready - safe to send heartbeat
    uint32_t now = millis();
    if (now - m_last_send_time >= HEARTBEAT_INTERVAL_MS) {
        send_heartbeat();
        m_last_send_time = now;
    }
}
```

**Why Both Checks?**
- **Ethernet check**: Keep-alive needs reliable transport (MQTT if enabled)
- **ESP-NOW check**: Need valid receiver to send to
- **Result**: Heartbeat ONLY sends when both networks operational

---

## Service Implementation Checklist

### For Each Service Using Ethernet

**Step 1: Register Callback in Setup**
```cpp
EthernetManager::instance().on_connected([] {
    YOUR_SERVICE::instance().start();
});

EthernetManager::instance().on_disconnected([] {
    YOUR_SERVICE::instance().stop();
});
```

**Step 2: Implement Start/Stop**
```cpp
void YOUR_SERVICE::start() {
    // Check state (defensive programming)
    if (is_running_) {
        LOG_WARN("SERVICE", "Already running");
        return;
    }
    
    // Init network connection
    if (!connect_to_server()) {
        LOG_ERROR("SERVICE", "Failed to connect");
        return;
    }
    
    is_running_ = true;
    LOG_INFO("SERVICE", "Started successfully");
}

void YOUR_SERVICE::stop() {
    if (!is_running_) return;  // Already stopped
    
    // Close connection gracefully
    disconnect_from_server();
    is_running_ = false;
    
    LOG_INFO("SERVICE", "Stopped");
}
```

**Step 3: Add State Guard to Periodic Functions**
```cpp
void YOUR_SERVICE::update() {
    // Check state before any network I/O
    if (!is_running_) return;
    if (!EthernetManager::instance().is_fully_ready()) {
        is_running_ = false;
        return;
    }
    
    // Do your work
    process_data();
}
```

**Step 4: Handle Disconnection Gracefully**
```cpp
void YOUR_SERVICE::on_ethernet_disconnected() {
    // Called from callback
    // Clean up state, but don't crash
    
    if (socket_handle_) {
        close(socket_handle_);
        socket_handle_ = nullptr;
    }
    
    // Don't set is_running_ to false here
    // It will be set false when on_disconnected callback runs
}
```

---

## Common Mistakes & Fixes

### Mistake 1: Network I/O Without Checking State

❌ **WRONG**:
```cpp
void NtpManager::sync() {
    // No check! Crashes if Ethernet disconnected
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}
```

✅ **CORRECT**:
```cpp
void NtpManager::sync() {
    if (!EthernetManager::instance().is_fully_ready()) {
        return;  // Skip safely
    }
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}
```

---

### Mistake 2: Starting Service on LINK_ACQUIRING

❌ **WRONG** (on_connected triggers too early):
```cpp
// This fires when cable detected, before IP assigned!
EthernetManager::instance().on_connected([] {
    // Cable present but no IP yet
    // Network calls will fail!
    MqttManager::instance().connect();
});
```

✅ **CORRECT** (use is_fully_ready):
```cpp
// Callback only fires in CONNECTED state
// on_connected() is NOT called until IP is assigned
EthernetManager::instance().on_connected([] {
    // Guaranteed: CONNECTED state reached
    // Both cable AND IP confirmed
    MqttManager::instance().connect();
});
```

**Implementation Detail**: Callback only triggered on transition TO CONNECTED, not LINK_ACQUIRING

---

### Mistake 3: Blocking in Callbacks

❌ **WRONG** (Blocks main loop):
```cpp
EthernetManager::instance().on_connected([] {
    // This runs in event handler context!
    // Can't block for long
    download_large_file();  // ← BLOCKS EVENT LOOP
    sleep(10000);           // ← FREEZES DEVICE
});
```

✅ **CORRECT** (Use FreeRTOS task):
```cpp
EthernetManager::instance().on_connected([] {
    // Just signal, don't do work
    xTaskNotify(mqtt_task_handle, 0, eNoAction);
});

// In MQTT task (runs concurrently):
while (true) {
    // Wait for signal or timeout
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000))) {
        mqtt_client.connect();  // OK to block here
    }
}
```

---

### Mistake 4: Not Stopping Service on Disconnect

❌ **WRONG** (Services keep running):
```cpp
// Never registered on_disconnected
// MQTT keeps trying to send even though Ethernet is down
// Wastes resources, causes errors
```

✅ **CORRECT**:
```cpp
EthernetManager::instance().on_disconnected([] {
    MqttManager::instance().disconnect();
    OtaManager::instance().stop();
});
```

---

### Mistake 5: Dual Gating Only Checks One Network

❌ **WRONG** (Heartbeat incomplete):
```cpp
void HeartbeatManager::tick() {
    // Only checks ESP-NOW, forgets Ethernet
    if (EspNowConnectionManager::instance().get_state() != CONNECTED) {
        return;
    }
    
    // Heartbeat sends even if cable unplugged!
    send_heartbeat();  // ← Can hang if no buffer
}
```

✅ **CORRECT**:
```cpp
void HeartbeatManager::tick() {
    // Check BOTH
    if (!EthernetManager::instance().is_fully_ready()) {
        return;  // Cable not present
    }
    
    if (EspNowConnectionManager::instance().get_state() != CONNECTED) {
        return;  // No receiver
    }
    
    send_heartbeat();  // ← Safe to send
}
```

---

## Service-Specific Examples

### NTP (Time Synchronization)

```cpp
class TimeManager {
private:
    bool syncing_ = false;
    
public:
    void init() {
        // Empty - wait for Ethernet
    }
    
    void sync_now() {
        // Called from on_connected callback
        if (syncing_) return;
        
        LOG_INFO("NTP", "Starting time sync...");
        syncing_ = true;
        
        // configTime() is async, returns immediately
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    }
    
    void stop() {
        // Not really stoppable, just mark as not syncing
        syncing_ = false;
    }
    
    // Called from main loop periodically
    void update() {
        if (!syncing_) return;
        if (!EthernetManager::instance().is_fully_ready()) {
            syncing_ = false;
            return;
        }
        
        // Check if sync completed
        time_t now = time(NULL);
        if (now > 1600000000) {  // 2020-09-13 (sanity check)
            LOG_INFO("NTP", "✓ Time synced: %s", ctime(&now));
            syncing_ = false;
        }
    }
};
```

**Integration**:
```cpp
// In setup()
EthernetManager::instance().on_connected([] {
    TimeManager::instance().sync_now();
});
```

---

### MQTT (Telemetry Publishing)

```cpp
class MqttManager {
private:
    PubSubClient client_;
    bool connecting_ = false;
    uint32_t last_reconnect_ = 0;
    static constexpr uint32_t RECONNECT_DELAY_MS = 5000;
    
public:
    void connect() {
        // Called from on_connected callback
        if (client_.connected()) {
            LOG_DEBUG("MQTT", "Already connected");
            return;
        }
        
        LOG_INFO("MQTT", "Connecting to broker...");
        connecting_ = true;
        last_reconnect_ = millis();
        
        client_.setServer(config::mqtt::SERVER, config::mqtt::PORT);
        client_.connect(config::mqtt::CLIENT_ID,
                       config::mqtt::USERNAME,
                       config::mqtt::PASSWORD);
    }
    
    void disconnect() {
        // Called from on_disconnected callback
        LOG_INFO("MQTT", "Disconnecting from broker");
        if (client_.connected()) {
            client_.disconnect();
        }
        connecting_ = false;
    }
    
    void update() {
        // Called from main loop (or MQTT task)
        if (!EthernetManager::instance().is_fully_ready()) {
            if (client_.connected()) {
                disconnect();
            }
            return;
        }
        
        // Try to maintain connection
        if (!client_.connected()) {
            if (connecting_ && (millis() - last_reconnect_) > RECONNECT_DELAY_MS) {
                connect();  // Retry
            }
        } else {
            client_.loop();  // Process messages
        }
    }
};
```

**Integration**:
```cpp
// In setup()
EthernetManager::instance().on_connected([] {
    MqttManager::instance().connect();
});

EthernetManager::instance().on_disconnected([] {
    MqttManager::instance().disconnect();
});

// In main loop
void loop() {
    MqttManager::instance().update();
    vTaskDelay(pdMS_TO_TICKS(100));
}
```

---

### OTA (Firmware Updates)

```cpp
class OtaManager {
private:
    bool server_running_ = false;
    httpd_handle_t server_ = nullptr;
    
public:
    void start_server() {
        // Called from on_connected callback
        if (server_running_) {
            LOG_DEBUG("OTA", "Server already running");
            return;
        }
        
        LOG_INFO("OTA", "Starting OTA server on port %d", config::OTA_PORT);
        
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = config::OTA_PORT;
        
        if (httpd_start(&server_, &config) == ESP_OK) {
            // Register URI handler
            httpd_uri_t upload_uri = {
                .uri = "/upload",
                .method = HTTP_POST,
                .handler = ota_upload_handler_,
                .user_ctx = nullptr
            };
            httpd_register_uri_handler(server_, &upload_uri);
            
            server_running_ = true;
            LOG_INFO("OTA", "✓ Server ready");
        } else {
            LOG_ERROR("OTA", "Failed to start server");
        }
    }
    
    void stop_server() {
        // Called from on_disconnected callback
        if (!server_running_) return;
        
        LOG_INFO("OTA", "Stopping OTA server");
        httpd_stop(server_);
        server_ = nullptr;
        server_running_ = false;
    }
    
    void update() {
        // Called from main loop (no action needed, server handles itself)
        if (server_running_ && !EthernetManager::instance().is_fully_ready()) {
            stop_server();
        }
    }
};
```

**Integration**:
```cpp
// In setup()
EthernetManager::instance().on_connected([] {
    OtaManager::instance().start_server();
});

EthernetManager::instance().on_disconnected([] {
    OtaManager::instance().stop_server();
});
```

---

### Heartbeat (Dual Gating)

**Already implemented in `src/espnow/heartbeat_manager.cpp`**:

```cpp
void HeartbeatManager::tick() {
    if (!m_initialized) return;
    
    // ✅ Gate 1: Ethernet
    if (!EthernetManager::instance().is_fully_ready()) {
        return;
    }
    
    // ✅ Gate 2: ESP-NOW
    if (EspNowConnectionManager::instance().get_state() != EspNowConnectionState::CONNECTED) {
        return;
    }
    
    uint32_t now = millis();
    if (now - m_last_send_time >= HEARTBEAT_INTERVAL_MS) {
        send_heartbeat();
        m_last_send_time = now;
    }
}
```

**No integration needed** - Already complete!

---

## Testing Service Gating

### Test 1: Start with Ethernet Ready

```bash
# Device already connected to network
Power on
Wait for boot

Expected:
  T=1s  Ethernet CONNECTED
  T=1s  ↓ on_connected() callback
  T=1s  NTP syncing
  T=1s  MQTT connecting
  T=1s  OTA server ready
  T=2s  NTP sync complete
  T=3s  MQTT connected
```

### Test 2: Start Without Ethernet

```bash
# No cable plugged
Power on
Wait 10 seconds

Expected:
  T=0s  Boot starts
  T=5s  Ethernet timeout → ERROR_STATE
  T=5s  on_disconnected() called (never called on_connected)
  T=5s  Services remain stopped
  T=5s  Device waits for reboot
```

### Test 3: Ethernet Comes Up During Boot

```bash
# Plug cable 3 seconds after power-on
Power on (no cable)
Wait 3 seconds
Plug cable

Expected:
  T=0s  Boot starts
  T=2s  LINK_ACQUIRING (waiting for cable)
  T=3s  Cable detected
  T=3s  Waiting for IP
  T=4s  IP assigned
  T=4s  CONNECTED
  T=4s  ↓ on_connected() callback
  T=4s  NTP/MQTT/OTA start
```

### Test 4: Cable Unplugged During Operation

```bash
# Unplug cable after device is running
Wait for Ethernet CONNECTED
Wait 10 seconds
Unplug cable

Expected:
  T=0s  All services running
  T=10s LINK_LOST (detected immediately)
  T=10s ↓ on_disconnected() callback
  T=10s MQTT disconnects
  T=10s OTA server stops
  T=10s Heartbeat stops
  T=11s RECOVERING (auto-transition)
  T=11s Waiting for cable reconnection...
```

### Test 5: Cable Reconnected

```bash
# Reconnect cable after 5 seconds
Cable unplugged
Wait 5 seconds
Plug cable back in

Expected:
  T=0s   LINK_LOST
  T=1s   RECOVERING
  T=5s   Cable detected
  T=5s   → LINK_ACQUIRING
  T=6s   IP assigned
  T=6s   ↓ on_connected() callback
  T=6s   NTP/MQTT/OTA restart
  T=7s   Services operational again
```

---

## Troubleshooting

### Problem: Services Never Start

**Check**:
1. Ethernet reaches CONNECTED state?
   ```cpp
   LOG_INFO("TEST", "ETH state: %s", EthernetManager::instance().get_state_string());
   // Should see: "ETH state: CONNECTED"
   ```

2. Callback registered?
   ```cpp
   LOG_INFO("TEST", "Callbacks registered: %d", 
            EthernetManager::instance().get_metrics().state_transitions);
   // Should see transitions logged
   ```

3. Callback executing?
   Add logging inside callback:
   ```cpp
   EthernetManager::instance().on_connected([] {
       LOG_INFO("CALLBACK", "on_connected() called!");  // ← Add this
       YourService::instance().start();
   });
   ```

### Problem: Services Restart Frequently

**Check**:
1. Cable flapping?
   ```cpp
   LOG_INFO("TEST", "Link flaps: %u", 
            EthernetManager::instance().get_metrics().link_flaps);
   // If high (> 1 per minute), cable is unstable
   ```

2. DHCP not stable?
   Monitor with packet capture or check logs

3. Need debouncing?
   See POST_RELEASE_IMPROVEMENTS.md for fix

### Problem: Service Keeps Running After Disconnect

**Check**:
1. on_disconnected callback registered?
2. Service.stop() actually stops the service?
   Add logging to verify

---

## Summary

| Aspect | Pattern |
|--------|---------|
| **Initialization** | Register callbacks in setup() |
| **Ethernet-Only Services** | Gate on `is_fully_ready()` |
| **Dual-Gated Services** | Gate on BOTH Ethernet AND other network |
| **Callback Rules** | Don't block, signal task instead |
| **Stop Pattern** | Register `on_disconnected()` callback |
| **Testing** | 5 scenarios: startup, no-eth, late-eth, disconnect, reconnect |

---

**Next Step**: See [ETHERNET_STATE_MACHINE_TECHNICAL_REFERENCE.md](ETHERNET_STATE_MACHINE_TECHNICAL_REFERENCE.md) for state machine details.

