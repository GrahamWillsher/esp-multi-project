# Post-Release Improvement Roadmap

**Version**: 1.0  
**Date**: February 19, 2026  
**Target Release**: v2.0

---

## Overview

This document outlines improvements planned after the production release of the state machine implementation. These are refinements based on expected operational experience, not blocking issues for release.

**Release v1.0 Goal**: Stable ethernet state machine with proper service gating  
**Release v2.0 Goal**: Enhanced reliability and operational visibility

---

## Priority 1: Critical After Release (Weeks 1-2)

### P1.1 Cable Flap Debouncing

**Issue**: Rapid cable connection/disconnection (flapping) causes service restart thrashing.

**Symptom**:
```
T=0:00  Ethernet CONNECTED → on_connected()
T=0:05  Cable wiggle → LINK_LOST → on_disconnected()
T=0:10  Cable settle → LINK_ACQUIRING → CONNECTED → on_connected()
T=0:15  [MQTT reconnects] [OTA restarts]
T=0:20  [Network interference] → LINK_LOST → on_disconnected()
...services restart every 5-10 seconds
```

**Root Cause**: Physical connector issues, RF interference, or worn cables.

**Solution**: Add 2-second minimum hold time before transitioning LINK_LOST → RECOVERING.

**Implementation**:
```cpp
// In ethernet_manager.h
private:
    static constexpr uint32_t LINK_FLAP_HOLD_TIME_MS = 2000;
    uint32_t link_lost_start_time_ = 0;

// In ethernet_manager.cpp - update_state_machine()
if (current_state_ == LINK_LOST) {
    uint32_t now = millis();
    if (link_lost_start_time_ == 0) {
        link_lost_start_time_ = now;
    }
    
    // Hold LINK_LOST state for 2 seconds before transitioning
    if (now - link_lost_start_time_ >= LINK_FLAP_HOLD_TIME_MS) {
        set_state(RECOVERING);
        link_lost_start_time_ = 0;
    }
}
```

**Testing**:
```bash
# Test with rapid cable connects/disconnects
# Should see LINK_LOST hold for 2 seconds
# Services should NOT restart if cable restabilizes within 2 seconds
```

**Estimated Effort**: 30 minutes  
**Estimated Testing**: 1 hour  
**Risk**: Low (defensive timeout added)

---

### P1.2 Link Up Race Condition

**Issue**: LINK_ACQUIRING → IP_ACQUIRING transition might be too fast on LAN.

**Symptom**:
```
T=0.0s  Cable detected → LINK_ACQUIRING
T=0.1s  [Too fast] → IP_ACQUIRING
T=0.5s  Actually wait for cable negotiation...
T=1.2s  IP assigned
T=1.2s  CONNECTED
...services start 1+ second later than needed
```

**Root Cause**: LINK_ACQUIRING has 30s timeout, but transitions to IP_ACQUIRING immediately on event, not on actual link up.

**Solution**: Wait for link negotiation to complete (check ETH link status in LINK_ACQUIRING state).

**Implementation**:
```cpp
// In ethernet_manager.cpp - in event handler or update_state_machine()
bool is_link_ready() {
    return ETH.linkUp();  // Built-in Arduino method
}

// In update_state_machine()
if (current_state_ == LINK_ACQUIRING) {
    if (is_link_ready()) {
        set_state(IP_ACQUIRING);
        LOG_DEBUG("ETH", "Link negotiation complete");
    }
}
```

**Testing**:
```bash
# Measure time from cable detect to CONNECTED
# Should be ~1.5-2.5 seconds (cable negotiation + DHCP)
# Not variable 0.1-5 seconds
```

**Estimated Effort**: 15 minutes  
**Estimated Testing**: 45 minutes  
**Risk**: Low (improves reliability)

---

## Priority 2: High Value After Release (Weeks 2-4)

### P2.1 Metrics Dashboard

**Issue**: No operational visibility into ethernet state machine behavior over time.

**Opportunity**: Log metrics to NVS and expose via dashboard.

**Metrics to Track**:
- Total state transitions
- Time in each state (histogram)
- Link flap count
- Callback execution count
- Error count by state
- Average time to CONNECTED since boot
- Last 10 state transitions (audit trail)

**Implementation Sketch**:
```cpp
// In ethernet_manager.h
struct StateMetrics {
    uint32_t total_transitions = 0;
    uint32_t link_flaps = 0;
    uint32_t callbacks_executed = 0;
    uint32_t errors = 0;
    uint32_t time_in_connected_ms = 0;
    uint32_t boots_with_eth_ready = 0;
    
    struct StateTimings {
        uint32_t uninitialized_ms;
        uint32_t phy_reset_ms;
        uint32_t link_acquiring_ms;
        // ...etc
    } timings;
    
    struct RecentTransition {
        EthernetState from_state;
        EthernetState to_state;
        uint32_t timestamp_ms;
        const char* reason;  // "cable_detect", "dhcp_timeout", etc
    } recent[10];
};

// Accessor
const StateMetrics& get_metrics() const;

// Save to NVS periodically
void persist_metrics();
```

**Dashboard Integration**:
```cpp
// In web server (settings_body.html)
<div id="ethernet-metrics">
    <h3>Ethernet Statistics</h3>
    <ul>
        <li>Uptime: <span id="uptime">--</span></li>
        <li>State Transitions: <span id="transitions">--</span></li>
        <li>Link Flaps: <span id="flaps">--</span></li>
        <li>Current State: <span id="state">--</span></li>
        <li>Recent State Changes: <pre id="state-history"></pre></li>
    </ul>
</div>

// JavaScript (settings_scripts.html)
async function update_ethernet_metrics() {
    const resp = await fetch('/api/ethernet/metrics');
    const metrics = await resp.json();
    
    document.getElementById('transitions').textContent = metrics.total_transitions;
    document.getElementById('flaps').textContent = metrics.link_flaps;
    document.getElementById('state').textContent = metrics.current_state;
    
    // Format recent transitions as table
    let history = '';
    metrics.recent_transitions.forEach(t => {
        history += `${t.from_state} → ${t.to_state} (${t.reason})\n`;
    });
    document.getElementById('state-history').textContent = history;
    
    // Refresh every 5 seconds
    setTimeout(update_ethernet_metrics, 5000);
}
```

**Estimated Effort**: 2-3 hours  
**Estimated Testing**: 2 hours  
**Value**: HIGH (operational visibility)

---

### P2.2 Error State Recovery Improvements

**Issue**: Device stuck in ERROR_STATE requires power cycle.

**Current Behavior**:
```
Ethernet → ERROR_STATE (timeout in any state)
Device stuck forever (no automatic recovery)
User must power cycle or reboot via API
```

**Desired Behavior**:
```
Ethernet → ERROR_STATE
Wait 10 seconds
→ UNINITIALIZED (reset)
→ PHY_RESET (retry)
...attempt recovery sequence
```

**Implementation**:
```cpp
// In ethernet_manager.h
static constexpr uint32_t ERROR_STATE_RECOVERY_DELAY_MS = 10000;

// In update_state_machine()
if (current_state_ == ERROR_STATE) {
    uint32_t now = millis();
    if (error_state_start_time_ == 0) {
        error_state_start_time_ = now;
        LOG_WARN("ETH", "ERROR_STATE entered, will retry in 10s");
    }
    
    if (now - error_state_start_time_ >= ERROR_STATE_RECOVERY_DELAY_MS) {
        LOG_INFO("ETH", "Attempting recovery from ERROR_STATE");
        set_state(UNINITIALIZED);
        init();  // Restart initialization
        error_state_start_time_ = 0;
    }
}
```

**Testing**:
```bash
# Force timeout (e.g., no DHCP response)
# Wait for ERROR_STATE
# Verify auto-recovery to UNINITIALIZED after 10 seconds
# Verify services attempt restart
```

**Estimated Effort**: 30 minutes  
**Estimated Testing**: 1 hour  
**Value**: HIGH (reliability)

---

### P2.3 Network Configuration Persistence

**Issue**: IP configuration lost on reboot if not saved to NVS.

**Current Behavior**:
```
Boot 1: DHCP gets 192.168.1.100
Boot 2: DHCP might get different IP or fail temporarily
Boot 3: Network services might be unreachable due to IP change
```

**Opportunity**: Save working IP configuration and use as fallback.

**Implementation**:
```cpp
// In ethernet_manager.h
struct SavedNetworkConfig {
    char static_ip[16];      // "192.168.1.100"
    char gateway[16];        // "192.168.1.1"
    char subnet[16];         // "255.255.255.0"
    char dns_primary[16];    // "8.8.8.8"
    uint32_t saved_timestamp;
    bool is_valid;
};

// Methods to add
SavedNetworkConfig load_network_config();
void save_network_config(const IPAddress& ip, const IPAddress& gw, const IPAddress& subnet);

// In CONFIG_APPLYING state
if (saved_config.is_valid && saved_config.saved_timestamp > now - 7*24*3600) {
    // Config is less than 7 days old - use it as initial values
    ETH.config(saved_config.ip, saved_config.gw, saved_config.subnet, 
               saved_config.dns1);
}
```

**Testing**:
```bash
# Boot with DHCP enabled
# Verify config saved to NVS
# Power cycle device
# Verify config used as fallback
# Verify new config overwrites old on next DHCP success
```

**Estimated Effort**: 1 hour  
**Estimated Testing**: 1 hour  
**Value**: MEDIUM (improves boot reliability)

---

### P2.4 Embed PlatformIO Environment in Metadata

**Issue**: Firmware metadata does not capture the build environment identifier (e.g., `olimex_esp32_poe2`), making it harder to trace device identity from logs and OTA artifacts.

**Opportunity**: Embed the PlatformIO environment name into firmware metadata so the device can report its exact build target.

**Implementation**:
- Add a metadata field for `env_name` (or `device_env`) and populate it from a build flag.
- Update the pre-build Python script to inject `PIO_ENV_NAME` (or a dedicated `DEVICE_ENV`) into the generated metadata header.
- Example for transmitter: `device_env = "olimex_esp32_poe2"`.
- Once the pre-build script injects the value, remove the temporary build flag from platformio.ini to avoid duplication.

**Estimated Effort**: 30 minutes  
**Estimated Testing**: 30 minutes  
**Value**: MEDIUM (traceability + support)

---

## Priority 3: Enhancement After Stabilization (Weeks 4+)

### P3.1 State Machine Visualization

**Issue**: Difficult to understand state machine behavior without detailed logs.

**Opportunity**: Create interactive state diagram with real-time transitions.

**Implementation**:
```html
<!-- In settings_body.html -->
<div id="ethernet-state-diagram">
    <svg width="600" height="400">
        <!-- Draw state circles and transition arrows -->
        <!-- Color current state highlight -->
        <!-- Animate transitions in real-time -->
    </svg>
</div>

<script>
// Real-time WebSocket connection
const ws = new WebSocket('ws://device.local/ws/ethernet');

ws.onmessage = (event) => {
    const state_change = JSON.parse(event.data);
    
    // Update SVG: highlight new state, animate transition arrow
    update_state_diagram(state_change.from, state_change.to);
};
</script>
```

**Estimated Effort**: 3-4 hours  
**Value**: MEDIUM (debugging aid)

---

### P3.2 Redundant Network Path

**Issue**: Single Ethernet link is single point of failure.

**Opportunity**: Fallback to cellular or alternative connection.

**Note**: Out of scope for ESP32-POE-ISO (no cellular modem). Consider for future hardware.

---

### P3.3 Network Performance Monitoring

**Issue**: No metrics on Ethernet link quality.

**Opportunity**: Monitor latency, packet loss, link speed.

**Implementation**:
```cpp
struct LinkQuality {
    uint8_t link_speed;      // Mbps (100 or 1000)
    bool full_duplex;        // true/false
    uint32_t ping_latency_ms;
    uint8_t packet_loss_pct;  // 0-100%
};

// Periodic ping test
LinkQuality measure_link_quality();

// Alert if quality degrades
if (link_quality.ping_latency_ms > 100) {
    LOG_WARN("ETH", "High latency detected: %u ms", 
             link_quality.ping_latency_ms);
}
```

**Estimated Effort**: 2-3 hours  
**Value**: LOW-MEDIUM (operational insight)

---

## Priority 4: Optimization (Post-Stabilization)

### P4.1 Reduce Flash Wear from Logging

**Issue**: Frequent logging to flash (NVS, SPIFFS) causes wear.

**Solution**: Buffer metrics in RAM, periodically flush to flash.

**Estimated Effort**: 1-2 hours

---

### P4.2 Timeout Tuning Based on Real Data

**Issue**: Timeouts chosen conservatively (might be too long).

**Solution**: Collect operational data, optimize timeouts.

**Current Timeouts**:
- PHY_RESET: 100 ms
- LINK_ACQUIRING: 30 s
- IP_ACQUIRING: 60 s
- RECOVERING: 30 s

**After 1 month of data**:
- Can determine actual time distributions
- Optimize for 99.5th percentile (not worst case)

**Estimated Effort**: 4-6 hours (includes data analysis)

---

## Priority 5: Future Hardware (Out of Scope)

### P5.1 Dual-Port Ethernet (Redundancy)

Requires hardware change (ESP32-S3-ETH board or custom design).

### P5.2 Wake-on-LAN

Allows device to wake from sleep via network packet.

### P5.3 Power-over-Ethernet (PoE) Improvements

Monitor PSE status, detect power loss, switch to battery.

---

## Implementation Priority Matrix

| Feature | Effort | Value | Risk | Priority |
|---------|--------|-------|------|----------|
| Cable Flap Debouncing | 30m | HIGH | LOW | **P1.1** ✓ |
| Link Up Race Fix | 15m | HIGH | LOW | **P1.2** ✓ |
| Metrics Dashboard | 2-3h | HIGH | LOW | **P2.1** |
| Error State Recovery | 30m | HIGH | LOW | **P2.2** |
| Config Persistence | 1h | MEDIUM | LOW | **P2.3** |
| State Visualization | 3-4h | MEDIUM | MED | **P3.1** |
| Link Quality Monitor | 2-3h | MEDIUM | LOW | **P3.3** |
| Timeout Optimization | 4-6h | MEDIUM | LOW | **P4.2** |
| Flash Wear Reduction | 1-2h | LOW | LOW | **P4.1** |

---

## Timeline Proposal

### Week 1 (Release v1.0)
- ✓ Ship current implementation
- Monitor for issues in production

### Week 2 (v1.1 Hotfix)
- [ ] Implement P1.1 (Cable Flap Debouncing)
- [ ] Implement P1.2 (Link Up Race Fix)

### Week 3-4 (v2.0 Beta)
- [ ] Implement P2.1 (Metrics Dashboard)
- [ ] Implement P2.2 (Error State Recovery)
- [ ] Implement P2.3 (Config Persistence)

### Week 5-6 (v2.0 Stable)
- [ ] Implement P3.1 (State Visualization)
- [ ] Bug fixes from beta feedback

### Week 7+ (Future)
- [ ] Optional P3.3, P4.x features
- [ ] Hardware improvements for P5.x

---

## Known Limitations in v1.0

These are documented limitations that do NOT affect release but are tracked for future improvement:

1. **No Automatic Recovery from ERROR_STATE**
   - Device stuck until power cycle
   - Fix: P2.2 (automatic retry after 10s)
   - Workaround: Use `/api/ethernet/reset` endpoint

2. **Cable Flap Causes Service Thrashing**
   - Rapid reconnects restart services unnecessarily
   - Fix: P1.1 (2-second hold time)
   - Workaround: Ensure stable cable connection

3. **No IP Configuration Fallback**
   - Different IP each boot (DHCP)
   - Fix: P2.3 (save working config to NVS)
   - Workaround: Use static IP if available

4. **Limited Operational Visibility**
   - No metrics dashboard
   - Fix: P2.1 (comprehensive dashboard)
   - Workaround: Parse debug logs

5. **Timeout Values Conservative**
   - Takes longer than necessary in some cases
   - Fix: P4.2 (tune based on real data)
   - Impact: Negligible for most deployments

---

## Success Criteria

### v1.0 Success Metrics (Release)
- [ ] No crashes after 7 days continuous operation
- [ ] CONNECTED state reached within 10 seconds of power-on
- [ ] Services start/stop correctly with ethernet transitions
- [ ] No deadlocks or resource leaks
- [ ] Documentation complete and tested

### v2.0 Success Metrics (Post-Release)
- [ ] Cable flap rate < 1 per day in production
- [ ] Error state recovery > 95% success rate
- [ ] Metrics dashboard accessible and accurate
- [ ] User-reported issues < 5 per 100 devices

---

## Feedback Collection Plan

**Metrics to Monitor in Production**:
1. How often devices reach ERROR_STATE?
2. How often cable flap occurs?
3. Average time from cable connect to services ready?
4. How often DHCP fails on first attempt?
5. Link quality measurements (latency, packet loss)?

**Collection Method**:
- Log metrics to `/api/ethernet/metrics` endpoint
- Optional: Send telemetry to cloud dashboard (if enabled)
- Manual: Users report issues via email/support

**Analysis**:
- Weekly review of metrics
- Identify top 3 issues to fix
- Prioritize fixes based on impact

---

## Related Documents

- [ETHERNET_STATE_MACHINE_TECHNICAL_REFERENCE.md](ETHERNET_STATE_MACHINE_TECHNICAL_REFERENCE.md) - State machine specification
- [SERVICE_INTEGRATION_GUIDE.md](SERVICE_INTEGRATION_GUIDE.md) - How to integrate services
- [PROJECT_ARCHITECTURE_MASTER.md](PROJECT_ARCHITECTURE_MASTER.md) - Overall project architecture

---

**Document Status**: COMPLETE - Ready for team review  
**Last Updated**: February 19, 2026  
**Prepared By**: Automated Analysis Agent

