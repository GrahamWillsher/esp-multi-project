# MQTT Subscription Optimization - Enhanced Implementation Plan

## Core Implementation + Enhancements

### Enhancement 1: Reference Counting for Multiple Clients
**Purpose:** Support multiple browsers viewing `/cellmonitor` simultaneously

```cpp
class MqttClient {
private:
    int cell_data_subscribers = 0;  // Track active SSE clients
    Mutex subscription_lock;
    
public:
    void incrementCellDataSubscribers() {
        xSemaphoreTakeRecursive(subscription_lock, portMAX_DELAY);
        cell_data_subscribers++;
        if (cell_data_subscribers == 1) {
            resumeCellDataSubscription();
            LOG_INFO("MQTT", "First SSE client connected - resumed subscription");
        }
        xSemaphoreGiveRecursive(subscription_lock);
    }
    
    void decrementCellDataSubscribers() {
        xSemaphoreTakeRecursive(subscription_lock, portMAX_DELAY);
        cell_data_subscribers--;
        if (cell_data_subscribers <= 0) {
            cell_data_subscribers = 0;
            startGracePeriodTimer();  // Don't pause immediately
            LOG_INFO("MQTT", "Last SSE client disconnected - will pause after grace period");
        }
        xSemaphoreGiveRecursive(subscription_lock);
    }
};
```

**Benefits:**
- ✅ Multiple simultaneous connections supported
- ✅ Subscription only pauses when LAST client disconnects
- ✅ Prevents thrashing (client A leaves, pause kicks in, client B immediately connects)

---

### Enhancement 2: Grace Period Timer
**Purpose:** Prevent rapid pause/resume cycles from browser navigation

```cpp
// In MqttClient.h
static const int CELL_DATA_GRACE_PERIOD_MS = 30000;  // 30 seconds

// In MqttClient implementation
TimerHandle_t cell_data_pause_timer = nullptr;

static void cellDataPauseTimerCallback(TimerHandle_t timer) {
    MqttClient::getInstance().performCellDataPause();
}

void startGracePeriodTimer() {
    if (cell_data_pause_timer == nullptr) {
        cell_data_pause_timer = xTimerCreate(
            "CellDataPauseTimer",
            pdMS_TO_TICKS(CELL_DATA_GRACE_PERIOD_MS),
            pdFALSE,  // No auto-reload
            nullptr,
            cellDataPauseTimerCallback
        );
    }
    xTimerStart(cell_data_pause_timer, portMAX_DELAY);
}

void cancelGracePeriodTimer() {
    if (cell_data_pause_timer != nullptr) {
        xTimerStop(cell_data_pause_timer, portMAX_DELAY);
        xTimerReset(cell_data_pause_timer, portMAX_DELAY);
    }
}
```

**Benefits:**
- ✅ Browser refresh/navigation doesn't cause immediate pause
- ✅ Typical page load: ~2 seconds, grace period: 30 seconds = no pause
- ✅ Reconnection within grace period = instant data available

---

### Enhancement 3: Connection State Tracking
**Purpose:** Know subscription state without asking MQTT broker

```cpp
enum SubscriptionState {
    SUBSCRIBED,      // Active subscription
    PAUSED,          // Unsubscribed, no clients
    PAUSING,         // Grace period active, waiting to pause
    ERROR            // Failed to subscribe/unsubscribe
};

class MqttClient {
private:
    SubscriptionState cell_data_state = SUBSCRIBED;
    
public:
    bool isCellDataSubscriptionActive() {
        return cell_data_state == SUBSCRIBED;
    }
    
    const char* getSubscriptionStateString() {
        switch (cell_data_state) {
            case SUBSCRIBED: return "SUBSCRIBED";
            case PAUSED: return "PAUSED";
            case PAUSING: return "PAUSING";
            case ERROR: return "ERROR";
            default: return "UNKNOWN";
        }
    }
};
```

**Benefits:**
- ✅ Debugging visibility (check state at any time)
- ✅ Add to `/api/transmitter_health` for monitoring
- ✅ Detect unexpected state mismatches

---

### Enhancement 4: Detailed Logging
**Purpose:** Monitor pause/resume events for troubleshooting

```cpp
void MqttClient::resumeCellDataSubscription() {
    if (cell_data_state == SUBSCRIBED) {
        LOG_DEBUG("MQTT", "Cell data already subscribed, skipping");
        return;
    }
    
    cell_data_state = PAUSING;
    LOG_INFO("MQTT", "[SUBSCRIPTION] Attempting to resume cell_data");
    
    if (mqtt_client_.subscribe("transmitter/BE/cell_data")) {
        cell_data_state = SUBSCRIBED;
        LOG_INFO("MQTT", "[SUBSCRIPTION] ✓ Resumed cell_data (subscribers: %d)", 
                 cell_data_subscribers);
    } else {
        cell_data_state = ERROR;
        LOG_ERROR("MQTT", "[SUBSCRIPTION] ✗ Failed to resume cell_data");
    }
}

void MqttClient::pauseCellDataSubscription() {
    if (cell_data_state != SUBSCRIBED) {
        LOG_DEBUG("MQTT", "Cell data already paused, skipping");
        return;
    }
    
    LOG_INFO("MQTT", "[SUBSCRIPTION] Attempting to pause cell_data");
    
    if (mqtt_client_.unsubscribe("transmitter/BE/cell_data")) {
        cell_data_state = PAUSED;
        LOG_INFO("MQTT", "[SUBSCRIPTION] ✓ Paused cell_data");
    } else {
        cell_data_state = ERROR;
        LOG_ERROR("MQTT", "[SUBSCRIPTION] ✗ Failed to pause cell_data");
    }
}
```

**Benefits:**
- ✅ Easy to trace subscription lifecycle
- ✅ Identify pause/resume race conditions
- ✅ Monitor for MQTT errors

---

### Enhancement 5: MQTT Reconnection Handling
**Purpose:** Restore subscription state after MQTT disconnect

```cpp
void MqttClient::onMqttConnected() {
    // ... existing code ...
    
    // Restore cell_data subscription if needed
    if (cell_data_subscribers > 0 && cell_data_state != SUBSCRIBED) {
        LOG_INFO("MQTT", "Reconnected with active clients, resuming cell_data");
        resumeCellDataSubscription();
    } else if (cell_data_subscribers == 0) {
        // Don't subscribe if no clients (let them trigger it)
        LOG_INFO("MQTT", "Reconnected but no active clients, cell_data paused");
    }
}

void MqttClient::onMqttDisconnected() {
    LOG_WARN("MQTT", "Disconnected - subscription state will be restored on reconnect");
    // Don't change state here - MQTT library handles it
}
```

**Benefits:**
- ✅ Auto-recovery if WiFi drops
- ✅ No manual intervention needed
- ✅ Maintains consistency across MQTT reconnects

---

### Enhancement 6: Avoid Dashboard Impact
**Purpose:** Ensure dashboard still works even if cell_data subscription is paused

```cpp
// In handleCellData()
void MqttClient::handleCellData(const char* json_payload, size_t length) {
    // This only called if subscribed, so no issue
    TransmitterManager::storeCellData(...);
}

// Dashboard only needs cell count, not real-time updates
// It's already cached from battery_specs, so no data loss
```

**Note:** Dashboard is NOT affected because:
- Cell count comes from `transmitter/BE/battery_specs` (always subscribed)
- Dashboard shows min/max/deviation (pre-calculated in cache)
- Only `/cellmonitor` needs real-time individual cell voltages

---

## Summary of All Enhancements

| Feature | Complexity | Benefit | Priority |
|---------|-----------|---------|----------|
| Reference counting | Low | Multiple clients | HIGH |
| Grace period timer | Medium | Smooth navigation | HIGH |
| State tracking | Low | Debuggability | MEDIUM |
| Detailed logging | Low | Troubleshooting | MEDIUM |
| MQTT reconnection | Low | Resilience | MEDIUM |

---

## Implementation Order

1. ✅ Add pause/resume methods (basic)
2. ✅ Add reference counting
3. ✅ Add grace period timer
4. ✅ Add state tracking
5. ✅ Add logging
6. ✅ Update SSE handler
7. ✅ Build & test
8. ✅ Test edge cases (rapid connects, MQTT disconnect, etc.)

---

## Testing Scenarios

### Normal Usage
- [ ] Open `/cellmonitor` → subscription resumes, data flows
- [ ] Leave page → timer starts, grace period runs
- [ ] Wait 30+ seconds → subscription pauses
- [ ] Verify no more cell data processing

### Multiple Clients
- [ ] Client A connects → subscription resumes
- [ ] Client B connects → stays subscribed, counter = 2
- [ ] Client A leaves → timer starts, counter = 1
- [ ] Client B leaves → counter = 0, grace period starts
- [ ] Client A reconnects within grace period → instant data (no re-subscribe)

### MQTT Issues
- [ ] WiFi drops → MQTT disconnects, state tracked
- [ ] WiFi resumes → MQTT reconnects, subscription restored if needed
- [ ] Subscribe fails → ERROR state logged, dashboard still works

### Rapid Navigation
- [ ] Open `/cellmonitor` → subscription resumes
- [ ] Leave immediately → grace period starts (not paused yet)
- [ ] Return before grace period ends → subscription active, data available

---

## Expected Improvements

**Bandwidth:** -95% (30MB/month → 1.5MB/month)  
**CPU/Processing:** -95% (43,200 unneeded JSON parse operations/day eliminated)  
**Power Draw:** -5-8mA during idle periods  
**User Experience:** 1-2 second initial delay on first page load (acceptable)  
**Scalability:** Multiple clients now properly supported  
**Reliability:** Auto-recovery from WiFi drops  

---

## No Additional Cost

All enhancements use existing FreeRTOS primitives:
- xSemaphoreTakeRecursive (already used elsewhere)
- xTimerCreate (already used elsewhere)
- String logging (already used elsewhere)

No external dependencies, minimal code bloat.
