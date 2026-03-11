#include "espnow_heartbeat_monitor.h"
#include <Arduino.h>

EspNowHeartbeatMonitor::EspNowHeartbeatMonitor(uint32_t timeout_ms)
    : timeout_ms_(timeout_ms),
      last_heartbeat_time_(0),
      connection_start_time_(0),
      timeout_count_(0),
      connection_established_(false) {}

void EspNowHeartbeatMonitor::on_heartbeat_received() {
    last_heartbeat_time_ = millis();
}

void EspNowHeartbeatMonitor::on_connection_success() {
    connection_established_ = true;
    connection_start_time_ = millis();
    last_heartbeat_time_ = millis();  // Start with heartbeat received
}

bool EspNowHeartbeatMonitor::connection_lost() const {
    if (!connection_established_) {
        return false;  // Never established, so not "lost"
    }
    
    uint32_t elapsed = millis() - last_heartbeat_time_;
    if (elapsed >= timeout_ms_) {
        return true;
    }
    return false;
}

uint32_t EspNowHeartbeatMonitor::ms_since_last_heartbeat() const {
    return millis() - last_heartbeat_time_;
}

uint32_t EspNowHeartbeatMonitor::connection_duration_seconds() const {
    if (!connection_established_) {
        return 0;
    }
    return (millis() - connection_start_time_) / 1000;
}

void EspNowHeartbeatMonitor::reset() {
    last_heartbeat_time_ = millis();
    connection_start_time_ = millis();
    timeout_count_ = 0;
    connection_established_ = false;
}
