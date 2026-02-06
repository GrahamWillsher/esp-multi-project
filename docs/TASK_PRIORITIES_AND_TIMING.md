# Battery Emulator Task Priorities and Timing Analysis

**Created**: February 6, 2026  
**Phase**: 0 (Pre-Migration Setup)  
**Purpose**: Document Battery Emulator task structure and define ESP32Projects task priorities

---

## 1. Battery Emulator Task Structure (Source)

From `Battery-Emulator-9.2.4/Software/Software.cpp`:

### 1.1 Tasks Defined

| Task Name | Function | Core | Priority | Stack Size | Timing |
|-----------|----------|------|----------|------------|--------|
| main_loop_task | Core 10ms control loop | 0 | ? | ? | 10ms cycle |
| connectivity_loop_task | WiFi, webserver, OTA, display | 1 | ? | ? | ~1ms delay |
| logging_loop_task | SD card logging | ? | ? | ? | On demand |
| mqtt_loop_task | MQTT publishing | ? | ? | ? | Variable |

### 1.2 Main Loop Task (Core Control)

**Function**: Core 10ms control loop

**Responsibilities**:
1. Read CAN messages from battery/charger/inverter
2. Update datalayer with sensor data
3. Apply user settings limits (charge/discharge current/voltage)
4. Calculate active power (W = V × A)
5. Run contactor control logic
6. Run precharge sequence
7. Check safety limits
8. Send CAN messages (commands to charger/inverter)
9. Transmit inverter CAN messages

**Critical Timing**:
- **Target cycle time**: 10ms
- **Must complete within**: 10ms (hard deadline)
- **Measured timing**: Uses `time_10ms_us` performance counter

```cpp
void loop() {
  START_TIME_MEASUREMENT(10ms);
  currentMillis = millis();
  
  // Every 10ms:
  if (currentMillis - previousMillis10ms >= INTERVAL_10_MS) {
    previousMillis10ms = currentMillis;
    
    // 1. Read CAN (battery, charger, inverter)
    receive_can();
    
    // 2. Update datalayer
    update_values_battery();
    update_values_charger();
    update_values_inverter();
    
    // 3. Run control logic
    handle_contactors();
    handle_precharge();
    check_safeties();
    
    // 4. Send CAN commands
    transmit_can();
  }
  
  END_TIME_MEASUREMENT_MAX(10ms, datalayer.system.status.time_10ms_us);
}
```

**Priority Required**: HIGHEST (must never be interrupted by ESP-NOW/MQTT/web tasks)

---

### 1.3 Connectivity Loop Task

**Function**: WiFi, webserver, OTA, display updates

**Responsibilities**:
1. Monitor WiFi connection
2. Handle OTA requests
3. Serve web pages
4. Update display (if present)
5. Handle mDNS

**Timing**: ~1ms delay between iterations

**Priority Required**: LOW (can be delayed without affecting control)

---

### 1.4 Logging Loop Task

**Function**: SD card and CAN logging

**Responsibilities**:
1. Write general logs to SD card
2. Write CAN frames to SD card

**Timing**: Runs when logging enabled, blocks on SD writes

**Priority Required**: LOWEST (background only)

---

### 1.5 MQTT Loop Task

**Function**: MQTT publishing

**Responsibilities**:
1. Publish telemetry to MQTT broker
2. Handle MQTT reconnection

**Timing**: Variable based on MQTT timeout

**Priority Required**: LOWEST (background only)

---

## 2. ESP32Projects Task Priorities (Target)

### 2.1 Transmitter Task Structure

**Hardware**: ESP32-POE-ISO (dual-core ESP32)

| Task Name | Function | Core | Priority | Stack | Timing |
|-----------|----------|------|----------|-------|--------|
| battery_control_loop | **CRITICAL** Battery control | 0 | **4** (HIGHEST) | 8192 | **10ms** |
| can_tx_task | CAN bus transmission | 0 | 3 | 4096 | On demand |
| can_rx_task | CAN bus reception | 0 | 3 | 4096 | On interrupt |
| espnow_rx_handler | ESP-NOW message processing | 1 | 2 | 4096 | On receive |
| espnow_data_sender | **LOW PRIORITY** Status updates | 1 | **1** | 4096 | **200ms** |
| espnow_discovery | Receiver discovery/announcements | 1 | 1 | 4096 | 5000ms |
| mqtt_task | MQTT telemetry | 1 | 0 | 8192 | Variable |
| ethernet_task | Ethernet/NTP | 1 | 0 | 8192 | Variable |

**CRITICAL PRINCIPLE**: 
- **Battery control loop (Priority 4) must NEVER be blocked**
- **ESP-NOW data sender (Priority 1) runs ONLY when CPU available**
- **Display/webserver updates are NOT time-critical** (200ms-1000ms acceptable)

---

### 2.2 Receiver Task Structure

**Hardware**: LilyGo T-Display-S3 (dual-core ESP32-S3)

| Task Name | Function | Core | Priority | Stack | Timing |
|-----------|----------|------|----------|-------|--------|
| espnow_rx_handler | ESP-NOW message processing | 0 | 2 | 4096 | On receive |
| espnow_tx_task | ESP-NOW transmission (settings) | 0 | 2 | 4096 | On demand |
| display_update_task | TFT display rendering | 1 | 1 | 8192 | 100ms |
| webserver_task | HTTP request handling | 1 | 1 | 8192 | On demand |
| sse_update_task | SSE client updates | 1 | 1 | 4096 | 200ms |

**Receiver Timing**:
- Display updates can tolerate delays (100ms-1000ms acceptable)
- SSE updates tied to ESP-NOW receive rate (200ms nominal)
- Webserver runs on demand (HTTP requests)

---

## 3. Timing Budget Analysis

### 3.1 Transmitter 10ms Control Loop Budget

**Available time per cycle**: 10,000 μs (10ms)

| Operation | Estimated Time | Percentage |
|-----------|---------------|-----------|
| CAN RX (read 10-20 frames) | 500 μs | 5% |
| Datalayer update (calculations) | 200 μs | 2% |
| Contactor logic | 100 μs | 1% |
| Precharge logic | 100 μs | 1% |
| Safety checks | 200 μs | 2% |
| CAN TX (send 10-15 frames) | 800 μs | 8% |
| **Total** | **1,900 μs** | **19%** |
| **Margin** | **8,100 μs** | **81%** |

**Result**: Plenty of margin for control loop

**NEVER DO IN CONTROL LOOP**:
- ❌ ESP-NOW transmission (50-500 μs per message, unpredictable)
- ❌ MQTT publishing (1000-5000 μs, network dependent)
- ❌ NVS writes (1000-10000 μs, flash dependent)
- ❌ Serial logging (variable, 100-1000 μs)

---

### 3.2 ESP-NOW Bandwidth Budget

**ESP-NOW Latency** (per message):
- Best case: 1-2 ms (same WiFi channel, no interference)
- Typical case: 5-10 ms (good conditions)
- Worst case: 50-100 ms (interference, retries)

**Data Sender Task Timing** (200ms cycle):

| Message | Size | Frequency | Time/cycle |
|---------|------|-----------|-----------|
| battery_status | 40 bytes | Every cycle (200ms) | ~5ms |
| charger_status | 24 bytes | Every cycle (200ms) | ~5ms |
| system_status | 13 bytes | Every cycle (200ms) | ~5ms |
| cell_voltages_chunk | 49 bytes | Every cycle (1 chunk) | ~5ms |
| battery_settings | 18 bytes | Every 1000ms (1 in 5 cycles) | ~1ms (avg) |

**Total per cycle**: ~20ms actual transmission time
**Cycle period**: 200ms
**CPU time**: ~10% (acceptable for Priority 1 task)

**Result**: ESP-NOW does NOT interfere with control loop

---

## 4. Phase 4 Implementation Guidelines

### 4.1 Control Loop Task (Priority 4 - HIGHEST)

```cpp
void battery_control_loop_task(void* parameter) {
  // Core 0, Priority 4 (HIGHEST)
  const TickType_t interval = pdMS_TO_TICKS(10);  // 10ms
  TickType_t last_wake = xTaskGetTickCount();
  
  while (true) {
    uint32_t start_us = esp_timer_get_time();
    
    // 1. Read CAN messages
    receive_can_messages();  // Battery, charger, inverter
    
    // 2. Update datalayer
    update_battery_status();
    update_charger_status();
    update_inverter_status();
    apply_user_settings_limits();
    calculate_active_power();
    
    // 3. Run control logic
    update_contactors();
    update_precharge();
    check_safety_limits();
    
    // 4. Send CAN messages
    send_can_commands();
    transmit_inverter_can();
    
    // 5. Performance measurement
    uint32_t loop_time_us = esp_timer_get_time() - start_us;
    if (loop_time_us > 10000) {
      LOG_WARN("Control loop exceeded 10ms: %lu us", loop_time_us);
    }
    datalayer.system.status.core_task_10s_max_us = max(datalayer.system.status.core_task_10s_max_us, loop_time_us);
    
    // 6. CRITICAL: Maintain precise 10ms timing
    vTaskDelayUntil(&last_wake, interval);
  }
}
```

**KEY POINTS**:
- Runs on Core 0 (dedicated to real-time tasks)
- Priority 4 (higher than ALL other tasks)
- NO ESP-NOW, NO MQTT, NO NVS in this task
- Uses `vTaskDelayUntil()` for precise timing
- Measures and logs timing violations

---

### 4.2 ESP-NOW Data Sender Task (Priority 1 - LOW)

```cpp
void espnow_data_sender_task(void* parameter) {
  // Core 1, Priority 1 (LOW - runs when CPU available)
  const TickType_t interval = pdMS_TO_TICKS(200);  // 200ms nominal
  TickType_t last_wake = xTaskGetTickCount();
  static uint8_t cycle = 0;
  
  while (true) {
    if (receiver_connected) {
      // Stagger messages to avoid bursts
      send_battery_status_espnow();
      vTaskDelay(pdMS_TO_TICKS(20));  // 20ms spacing
      
      send_charger_status_espnow();
      vTaskDelay(pdMS_TO_TICKS(20));
      
      send_system_status_espnow();
      vTaskDelay(pdMS_TO_TICKS(20));
      
      // Cell voltages (chunked, one chunk per cycle)
      static uint8_t cell_chunk = 0;
      send_cell_voltages_chunk(cell_chunk);
      cell_chunk = (cell_chunk + 1) % 10;  // 10 chunks total
      vTaskDelay(pdMS_TO_TICKS(20));
      
      // Settings (every 1000ms)
      if (cycle % 5 == 0) {
        send_battery_settings_espnow();
      }
      
      cycle++;
    }
    
    // NOT strict timing - yield to higher priority tasks
    vTaskDelayUntil(&last_wake, interval);
  }
}
```

**KEY POINTS**:
- Runs on Core 1 (opposite core from control loop)
- Priority 1 (LOW - yields to control loop, CAN, ESP-NOW RX)
- 200ms nominal cycle (NOT strict - can slip to 500ms-1000ms if busy)
- Staggers messages with 20ms delays
- Uses `vTaskDelayUntil()` for nominal timing, but yields as needed

---

### 4.3 Task Creation

```cpp
void setup() {
  // ... initialization
  
  // Control loop on Core 0, Priority 4 (HIGHEST)
  xTaskCreatePinnedToCore(
    battery_control_loop_task,   // Function
    "BatteryControl",            // Name
    8192,                        // Stack size
    NULL,                        // Parameter
    4,                           // Priority (HIGHEST)
    &battery_control_task,       // Handle
    0                            // Core 0
  );
  
  // ESP-NOW data sender on Core 1, Priority 1 (LOW)
  xTaskCreatePinnedToCore(
    espnow_data_sender_task,
    "ESPNOWSender",
    4096,
    NULL,
    1,                           // Priority (LOW)
    &espnow_sender_task,
    1                            // Core 1
  );
  
  // ESP-NOW RX handler on Core 1, Priority 2
  xTaskCreatePinnedToCore(
    espnow_rx_handler_task,
    "ESPNOWRX",
    4096,
    NULL,
    2,                           // Priority (MEDIUM)
    &espnow_rx_task,
    1                            // Core 1
  );
  
  // MQTT on Core 1, Priority 0 (LOWEST)
  xTaskCreatePinnedToCore(
    mqtt_task_loop,
    "MQTT",
    8192,
    NULL,
    0,                           // Priority (LOWEST)
    &mqtt_task_handle,
    1                            // Core 1
  );
}
```

---

## 5. Timing Validation Tests

### 5.1 Control Loop Timing Test

```cpp
// Test: Measure control loop timing over 10,000 cycles
void test_control_loop_timing() {
  uint32_t min_us = UINT32_MAX;
  uint32_t max_us = 0;
  uint32_t sum_us = 0;
  uint32_t violations = 0;
  
  for (int i = 0; i < 10000; i++) {
    uint32_t start = esp_timer_get_time();
    
    // Run one control loop iteration
    battery_control_loop_iteration();
    
    uint32_t elapsed = esp_timer_get_time() - start;
    
    min_us = min(min_us, elapsed);
    max_us = max(max_us, elapsed);
    sum_us += elapsed;
    
    if (elapsed > 10000) {
      violations++;
    }
    
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  
  LOG_INFO("Control loop timing over 10,000 cycles:");
  LOG_INFO("  Min: %lu μs", min_us);
  LOG_INFO("  Max: %lu μs", max_us);
  LOG_INFO("  Avg: %lu μs", sum_us / 10000);
  LOG_INFO("  Violations (> 10ms): %lu (%0.2f%%)", violations, (violations / 100.0));
  
  // PASS CRITERIA: Max < 10,000 μs AND Violations = 0
  TEST_ASSERT(max_us < 10000);
  TEST_ASSERT(violations == 0);
}
```

### 5.2 ESP-NOW Interference Test

```cpp
// Test: Control loop timing NOT affected by heavy ESP-NOW traffic
void test_espnow_no_interference() {
  uint32_t max_us_no_espnow = 0;
  uint32_t max_us_with_espnow = 0;
  
  // 1. Baseline: Control loop without ESP-NOW
  for (int i = 0; i < 1000; i++) {
    uint32_t start = esp_timer_get_time();
    battery_control_loop_iteration();
    uint32_t elapsed = esp_timer_get_time() - start;
    max_us_no_espnow = max(max_us_no_espnow, elapsed);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  
  // 2. Test: Control loop WITH heavy ESP-NOW traffic
  // Send 100 messages/second (5x normal rate)
  xTaskCreate(espnow_stress_sender, "ESPNOWStress", 4096, NULL, 1, NULL, 1);
  
  for (int i = 0; i < 1000; i++) {
    uint32_t start = esp_timer_get_time();
    battery_control_loop_iteration();
    uint32_t elapsed = esp_timer_get_time() - start;
    max_us_with_espnow = max(max_us_with_espnow, elapsed);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  
  LOG_INFO("Control loop max timing:");
  LOG_INFO("  Without ESP-NOW: %lu μs", max_us_no_espnow);
  LOG_INFO("  With ESP-NOW: %lu μs", max_us_with_espnow);
  LOG_INFO("  Delta: %ld μs", (int32_t)max_us_with_espnow - (int32_t)max_us_no_espnow);
  
  // PASS CRITERIA: Delta < 500 μs AND Both < 10,000 μs
  TEST_ASSERT(max_us_with_espnow - max_us_no_espnow < 500);
  TEST_ASSERT(max_us_with_espnow < 10000);
}
```

---

## 6. Summary

### 6.1 Critical Requirements

✅ **Control loop runs at 10ms** - Measured with microsecond precision  
✅ **Control loop NEVER exceeds 10ms** - Even under heavy load  
✅ **ESP-NOW is lower priority** - Runs on separate core, lower priority  
✅ **Display updates NOT time-critical** - 200ms-1000ms acceptable  
✅ **Webserver NOT time-critical** - Can tolerate delays  
✅ **MQTT NOT time-critical** - Background only  

### 6.2 Task Priority Summary

| Priority | Core | Tasks |
|----------|------|-------|
| 4 (HIGHEST) | 0 | Battery control loop (10ms) |
| 3 | 0 | CAN TX/RX |
| 2 | 1 | ESP-NOW RX handler |
| 1 | 1 | ESP-NOW data sender, Discovery |
| 0 (LOWEST) | 1 | MQTT, Ethernet, NTP |

### 6.3 Migration Success Criteria

- [ ] Control loop stable at 10ms (±100 μs jitter acceptable)
- [ ] Zero timing violations over 24 hours
- [ ] ESP-NOW traffic does NOT affect control loop timing
- [ ] Display updates happen (200ms-1000ms delay acceptable)
- [ ] Settings updates work (1-5 seconds delay acceptable)
- [ ] MQTT publishes work (delays acceptable)
- [ ] No task starvation (all tasks get CPU time)

---

**Status**: ✓ COMPLETE  
**Phase 0 Documentation**: COMPLETE  
**Next**: Commit all Phase 0 docs and begin Phase 1
