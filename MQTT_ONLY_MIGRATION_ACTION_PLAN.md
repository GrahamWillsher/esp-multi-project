# MQTT-Only Migration - Detailed Action Plan

**Current Date:** 2026-05-17  
**Status:** Foundation complete (Phase 2), ready for cleanup phases

## What's Already Done

### ✅ Topic Contracts (Completed)
- `mqtt_topics_receiver.h/cpp` - All receiver topics defined with QoS/retention
- `mqtt_topics_transmitter.h/cpp` - All transmitter topics defined with command schemas
- Utility functions for topic extraction and routing

### ✅ Command Router (Completed)
- `mqtt_command_router.h/cpp` - Complete implementation for transmitter
- Subscribes to `batt-emu/mqtt-v1/rx/cmd/#`
- Implements idempotency cache (128 most recent request_ids)
- Publishes ACKs with request_id correlation
- Handler registration pattern for extensibility

### ✅ State Publisher (Completed)
- `mqtt_state_publisher.h/cpp` - Complete implementation for transmitter
- Publishes retained static models (battery, power, network, mqtt, led, catalogs)
- Publishes metadata (version, schema_versions, runtime)
- Implements chunking for large payloads
- Heartbeat at 1000-2000ms intervals

### ✅ Receiver MQTT Client (Already Present)
- All handlers present in `mqtt_client.cpp`
- FNV1a hash-based message routing
- Subscriptions configured correctly
- Cache integration working

## What Needs To Be Done (3 Priority Groups)

### Group 1: Verification (Low Risk, 30 minutes)

#### Task 1.1: Verify API Handlers Use MQTT Paths
**File:** `espnowreceiver_LCD/lib/webserver_lcd/api/api_settings_handlers.cpp`

Check that this code exists and is calling MQTT:
```cpp
#if MQTT_FEATURE_COMMANDS
  // Try MQTT first
  if (MqttClient::isEnabled() && MqttClient::isConnected()) {
    bool mqtt_sent = MqttCommandClient::sendSettingsUpdate(category, field, value, timeout_ms, &ack_result);
    if (mqtt_sent && ack_result.success) {
      // Apply to local cache
      return result with "mqtt" source
    }
  }
#endif
// Fall back to ESP-NOW
result = EspnowTxScheduler::send(...);
```

**Verification Steps:**
1. ✅ Read lines 100-150 of `api_settings_handlers.cpp`
2. ✅ Confirm `MqttCommandClient::sendSettingsUpdate()` is called
3. ✅ Confirm ESP-NOW fallback is available
4. ✅ Record status

**Expected Finding:** Code should already have both paths

#### Task 1.2: Verify Network Handler Has MQTT Path
**File:** `espnowreceiver_LCD/lib/webserver_lcd/api/api_network_handlers.cpp`

Same structure as settings handler:
```cpp
if (MQTT_FEATURE_COMMANDS && MqttClient::isConnected()) {
  mqtt_sent = MqttCommandClient::sendNetworkUpdate(...);
  // apply to cache if success
}
EspnowTxScheduler::send(...);  // fallback
```

**Action:** Quick verification, no changes needed if working

### Group 2: ESP-NOW Cleanup (Medium Risk, 3-4 hours)

#### Task 2.1: Remove ESP-NOW Boot Phases from Receiver
**File:** `espnowreceiver_LCD/src/main.cpp`

**What to find:**
- Boot phases or startup calls for `espnow_radio`
- Boot phases or startup calls for `espnow_state`
- Worker initialization for ESP-NOW runtime

**What to remove:**
- All `esp_now_init()` and related WiFi driver setup
- All ESP-NOW event handler registration
- All receiver peer/MAC discovery logic

**Expected Changes:**
- ~30-50 lines deleted from main.cpp
- ~0 lines added
- Result: Main boots MQTT only, no ESP-NOW radio

#### Task 2.2: Remove ESP-NOW Tasks from Receiver
**File:** `espnowreceiver_LCD/src/runtime/runtime_task_startup.cpp`

**What to find:**
- Task creation for `espnow_runtime` worker
- Task creation for `espnow_send` scheduler
- Worker pool initialization

**What to remove:**
- `xTaskCreatePinnedToCore(espnow_runtime_task, ...)` 
- `xTaskCreatePinnedToCore(espnow_send_task, ...)`
- Scheduler initialization calls

**Expected Changes:**
- ~50-80 lines deleted
- ~0 lines added
- Result: 2 fewer FreeRTOS tasks, ~10-20 KB more RAM available

#### Task 2.3: Remove ESP-NOW Boot from Transmitter
**File:** `ESPnowtransmitter2/espnowtransmitter2/src/main.cpp`

**What to find:**
- `esp_now_init()` calls
- Receiver peer/MAC registration
- Discovery mode initialization
- Connection state machine setup

**What to remove:**
- All ESP-NOW radio and peer management
- Discovery/reconnect task scheduling
- Version beacon publishing (move to MQTT)

**Expected Changes:**
- ~100-150 lines deleted
- ~0 lines added
- Result: Transmitter only publishes to MQTT, no ESP-NOW transmit

#### Task 2.4: Remove Transmitter Task Initialization
**File:** `ESPnowtransmitter2/espnowtransmitter2/src/runtime/task_startup.cpp` (or equivalent)

**What to find:**
- Task creation for `discovery_task`
- Task creation for `transmission_task`
- Task creation for `tx_reconnect_manager`
- Queue manager initialization

**What to remove:**
- `xTaskCreatePinnedToCore(discovery_task, ...)`
- `xTaskCreatePinnedToCore(transmission_task, ...)`
- `xTaskCreatePinnedToCore(tx_reconnect_manager, ...)`
- `EspnowQueueManager::init()`

**Expected Changes:**
- ~150-200 lines deleted
- ~0 lines added
- Result: 3-4 fewer FreeRTOS tasks, ~25-35 KB more RAM available

### Group 3: Build & Test (30 minutes)

#### Task 3.1: Build Receiver
```bash
cd c:\Users\GrahamWillsher\ESP32Projects\espnowreceiver_LCD
pio run -e waveshare_esp32s3_lcd7_lvgl
```

**Expected Outcome:**
- ✅ SUCCESS in <120 seconds
- ✅ Zero compilation errors
- ✅ Flash size ~50-100 KB less than previous
- ✅ RAM usage unchanged or better

**Record:**
- Build time
- Flash usage
- Errors (if any)

#### Task 3.2: Build Transmitter
```bash
cd c:\Users\GrahamWillsher\ESP32Projects\ESPnowtransmitter2\espnowtransmitter2
pio run
```

**Expected Outcome:**
- ✅ SUCCESS in <100 seconds
- ✅ Zero compilation errors
- ✅ Flash size ~100-150 KB less than previous
- ✅ RAM usage unchanged or better

**Record:**
- Build time
- Flash usage
- Errors (if any)

## File Inventory for Cleanup

### Receiver Files to Check/Modify (4 files)
- `src/main.cpp` - remove ESP-NOW boot phases
- `src/runtime/runtime_task_startup.cpp` - remove ESP-NOW task creation
- `lib/webserver_lcd/api/api_settings_handlers.cpp` - verify MQTT path (no changes)
- `lib/webserver_lcd/api/api_network_handlers.cpp` - verify MQTT path (no changes)

### Receiver Files to Delete Eventually (23 files in `src/espnow/*`)
```
battery_data_store.cpp/h
battery_handlers.cpp/h
battery_settings_cache.cpp/h
component_apply_tracker.cpp/h
component_config_handler.cpp/h
espnow_protocol_min.h
espnow_runtime.cpp/h
espnow_runtime_detail.h
espnow_runtime_ingress.cpp
espnow_runtime_messages.cpp
espnow_runtime_routes.cpp
espnow_send.cpp/h
espnow_settings_sync.cpp/h
type_catalog_cache.cpp/h
```

**Note:** Don't delete yet. Just remove from boot/startup paths.

### Transmitter Files to Check/Modify (2 files)
- `espnowtransmitter2/src/main.cpp` - remove ESP-NOW boot
- `espnowtransmitter2/src/runtime/task_startup.cpp` - remove ESP-NOW tasks

### Transmitter Files to Delete Eventually (35+ files in `src/espnow/*`)
```
component_catalog_handlers.cpp/h
config_handler_common.cpp/h
control_handlers.cpp/h
data_cache.cpp/h
data_sender.cpp/h
discovery_task.cpp/h
enhanced_cache.cpp/h
heartbeat_manager.cpp/h
message_handler.cpp/h
message_routes.cpp
mqtt_config_handlers.cpp/h
network_config_handlers.cpp/h
request_data_handlers.cpp/h
transmission_task.cpp/h
tx_connection_handler.cpp/h
tx_reconnect_manager.cpp/h
tx_send_guard.cpp/h
tx_state_machine.cpp/h
version_beacon_manager.cpp/h
... and queue/scheduler files
```

## Risk Assessment

| Task | Risk | Mitigation |
|------|------|-----------|
| Remove boot phases | Low | Just remove initialization, don't delete files yet |
| Remove tasks | Low | Tasks won't start if not created, safe deletion |
| Compilation | Low | Linker might complain, easy to fix |
| Runtime bugs | Medium | Deploy and test carefully |
| MQTT message loss | Low | Retained topics ensure replay |
| Settings persistence | Low | MQTT ACK confirms receipt |

## Success Checklist

- [ ] Receiver builds without errors
- [ ] Transmitter builds without errors
- [ ] Flash size reduction: transmitter >100 KB, receiver >50 KB
- [ ] API handlers verified calling MQTT paths
- [ ] ESP-NOW boot code removed from both projects
- [ ] Both projects boot MQTT-only
- [ ] Hardware test: settings update via MQTT succeeds
- [ ] Hardware test: 30-minute soak with no crashes
- [ ] MQTT command/ACK round-trip p95 <= 500ms

## Next Command Sequence

When ready to proceed:

1. **Verify API handlers** (15 min)
   - Read api_settings_handlers.cpp lines 100-150
   - Confirm MQTT+fallback pattern exists

2. **Clean receiver startup** (1 hour)
   - Remove ESP-NOW boot from main.cpp
   - Remove ESP-NOW tasks from runtime_task_startup.cpp
   - Build & verify no errors

3. **Clean transmitter startup** (1 hour)
   - Remove ESP-NOW boot from ESPnowtransmitter2/src/main.cpp
   - Remove ESP-NOW tasks from runtime startup
   - Build & verify no errors

4. **Test builds** (30 min)
   - Build receiver: verify flash savings
   - Build transmitter: verify flash savings
   - Record metrics

5. **Deploy & validate** (2 hours)
   - Flash both devices
   - Test MQTT settings update
   - Run 30-minute soak test

---

**Recommendation:** Proceed with Group 1 (verification) first, then Group 2 (cleanup) in phases, then Group 3 (build/test).
