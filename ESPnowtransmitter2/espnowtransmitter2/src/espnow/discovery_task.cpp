#include "discovery_task.h"
#include "message_handler.h"
#include "data_cache.h"  // For cache flush on connection
#include "version_beacon_manager.h"  // For sending initial beacon on connection
#include "../config/task_config.h"
#include "../config/logging_config.h"
#include <Arduino.h>
#include <espnow_discovery.h>
#include <espnow_transmitter.h>  // For g_lock_channel and set_channel
#include <espnow_peer_manager.h>
#include <esp_wifi.h>
#include <WiFi.h>

extern QueueHandle_t espnow_message_queue;    // From main.cpp - application messages
extern QueueHandle_t espnow_discovery_queue;  // From main.cpp - discovery PROBE/ACK messages

DiscoveryTask& DiscoveryTask::instance() {
    static DiscoveryTask instance;
    return instance;
}

void DiscoveryTask::start() {
    // Use common discovery component with callback
    EspnowDiscovery::instance().start(
        []() -> bool {
            return EspnowMessageHandler::instance().is_receiver_connected();
        },
        timing::ANNOUNCEMENT_INTERVAL_MS,
        task_config::PRIORITY_LOW,
        task_config::STACK_SIZE_ANNOUNCEMENT
    );
    
    task_handle_ = EspnowDiscovery::instance().get_task_handle();
    LOG_DEBUG("[DISCOVERY] Using common discovery component");
}

void DiscoveryTask::restart() {
    LOG_INFO("[DISCOVERY] ═══ RESTART INITIATED (Attempt %d/%d) ═══", 
             restart_failure_count_ + 1, MAX_RESTART_FAILURES);
    
    // CRITICAL: Check if we have a valid channel to restart on
    if (g_lock_channel == 0) {
        LOG_ERROR("[DISCOVERY] Cannot restart - no valid channel (g_lock_channel=0)");
        LOG_INFO("[DISCOVERY] This indicates initial discovery has not completed yet");
        LOG_INFO("[DISCOVERY] Keep-alive manager should not trigger restart before discovery completes");
        return;  // Abort restart - let active hopping continue
    }
    
    uint32_t restart_start_time = millis();
    metrics_.total_restarts++;
    
    // STEP 1: Remove ALL ESP-NOW peers for guaranteed clean slate
    cleanup_all_peers();
    
    // STEP 2: Force channel lock and verify
    if (!force_and_verify_channel(g_lock_channel)) {
        restart_failure_count_++;
        metrics_.failed_restarts++;
        
        if (restart_failure_count_ >= MAX_RESTART_FAILURES) {
            LOG_ERROR("[DISCOVERY] ✗ Maximum restart failures reached (%d) - system needs attention", 
                     MAX_RESTART_FAILURES);
            transition_to(RecoveryState::PERSISTENT_FAILURE);
            restart_failure_count_ = 0;  // Reset for next cycle
            return;
        }
        
        // Exponential backoff before retry
        uint32_t backoff_ms = 500 * (1 << restart_failure_count_);  // 500, 1000, 2000ms
        LOG_WARN("[DISCOVERY] Restart failed, retrying in %dms", backoff_ms);
        delay(backoff_ms);
        
        restart();  // Recursive retry
        return;
    }
    
    // STEP 3: Restart discovery task with clean state
    EspnowDiscovery::instance().restart();
    task_handle_ = EspnowDiscovery::instance().get_task_handle();
    
    // Give new task time to stabilize
    delay(100);
    
    // STEP 4: Final verification
    uint8_t verify_ch = 0;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&verify_ch, &second);
    
    if (verify_ch != g_lock_channel) {
        LOG_ERROR("[DISCOVERY] ✗ Post-restart channel mismatch: %d != %d", verify_ch, g_lock_channel);
        metrics_.failed_restarts++;
        return;
    }
    
    // Success!
    restart_failure_count_ = 0;
    consecutive_failures_ = 0;
    metrics_.successful_restarts++;
    metrics_.last_restart_timestamp = millis();
    
    uint32_t restart_duration = millis() - restart_start_time;
    LOG_INFO("[DISCOVERY] ✓ Restart complete in %dms (channel: %d, clean state)", 
             restart_duration, verify_ch);
    
    transition_to(RecoveryState::NORMAL);
}

void DiscoveryTask::cleanup_all_peers() {
    LOG_INFO("[DISCOVERY] Cleaning up all ESP-NOW peers...");
    
    const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    // Remove broadcast peer
    if (esp_now_is_peer_exist(broadcast_mac)) {
        esp_err_t result = esp_now_del_peer(broadcast_mac);
        if (result == ESP_OK) {
            LOG_INFO("[DISCOVERY]   ✓ Broadcast peer removed");
            metrics_.peer_cleanup_count++;
        } else {
            LOG_ERROR("[DISCOVERY]   ✗ Failed to remove broadcast peer: %s", esp_err_to_name(result));
        }
    } else {
        LOG_DEBUG("[DISCOVERY]   - Broadcast peer not present");
    }
    
    // Remove receiver peer if it exists
    if (receiver_mac[0] != 0 || receiver_mac[1] != 0 || receiver_mac[2] != 0) {
        if (esp_now_is_peer_exist(receiver_mac)) {
            esp_err_t result = esp_now_del_peer(receiver_mac);
            if (result == ESP_OK) {
                LOG_INFO("[DISCOVERY]   ✓ Receiver peer removed");
                metrics_.peer_cleanup_count++;
            } else {
                LOG_ERROR("[DISCOVERY]   ✗ Failed to remove receiver peer: %s", esp_err_to_name(result));
            }
        } else {
            LOG_DEBUG("[DISCOVERY]   - Receiver peer not present");
        }
    }
}

bool DiscoveryTask::force_and_verify_channel(uint8_t target_channel) {
    LOG_INFO("[DISCOVERY] Forcing channel lock to %d...", target_channel);
    
    // Force set channel
    if (!set_channel(target_channel)) {
        LOG_ERROR("[DISCOVERY]   ✗ Failed to set channel to %d", target_channel);
        return false;
    }
    
    LOG_DEBUG("[DISCOVERY]   - Channel set command executed");
    
    // Adequate delay for WiFi driver stabilization (industrial: 150ms)
    delay(150);
    
    // Verify channel was actually set
    uint8_t actual_ch = 0;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&actual_ch, &second);
    
    if (actual_ch != target_channel) {
        LOG_ERROR("[DISCOVERY]   ✗ Channel verification failed: expected=%d, actual=%d", 
                  target_channel, actual_ch);
        metrics_.channel_mismatches++;
        return false;
    }
    
    LOG_INFO("[DISCOVERY]   ✓ Channel locked and verified: %d", actual_ch);
    return true;
}

bool DiscoveryTask::validate_state() {
    bool valid = true;
    
    // Check WiFi channel matches locked channel
    uint8_t current_ch = 0;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&current_ch, &second);
    
    if (current_ch != g_lock_channel) {
        LOG_ERROR("[DISCOVERY] State validation failed: channel mismatch (%d != %d)", 
                  current_ch, g_lock_channel);
        metrics_.channel_mismatches++;
        valid = false;
    }
    
    // Check broadcast peer exists and has correct channel
    const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (esp_now_is_peer_exist(broadcast_mac)) {
        esp_now_peer_info_t peer;
        if (esp_now_get_peer(broadcast_mac, &peer) == ESP_OK) {
            if (peer.channel != g_lock_channel && peer.channel != 0) {
                LOG_ERROR("[DISCOVERY] Broadcast peer has wrong channel: %d (expected %d)", 
                          peer.channel, g_lock_channel);
                valid = false;
            }
        }
    } else {
        LOG_WARN("[DISCOVERY] Broadcast peer does not exist during validation");
        valid = false;
    }
    
    return valid;
}

void DiscoveryTask::audit_peer_state() {
    const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    LOG_INFO("[PEER_AUDIT] ═══ ESP-NOW Peer State Audit ═══");
    
    // Current WiFi channel
    uint8_t current_ch = 0;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&current_ch, &second);
    LOG_INFO("[PEER_AUDIT] WiFi Channel: %d (Locked: %d)", current_ch, g_lock_channel);
    
    // Check broadcast peer
    if (esp_now_is_peer_exist(broadcast_mac)) {
        esp_now_peer_info_t peer;
        esp_err_t result = esp_now_get_peer(broadcast_mac, &peer);
        
        if (result == ESP_OK) {
            LOG_INFO("[PEER_AUDIT] Broadcast Peer:");
            LOG_INFO("[PEER_AUDIT]   Channel: %d %s", peer.channel,
                     (peer.channel != 0 && peer.channel != g_lock_channel) ? "✗ MISMATCH" : "✓");
            LOG_INFO("[PEER_AUDIT]   Encrypt: %d %s", peer.encrypt, peer.encrypt ? "✗ UNEXPECTED" : "✓");
            LOG_INFO("[PEER_AUDIT]   Interface: %d %s", peer.ifidx, 
                     peer.ifidx == WIFI_IF_STA ? "✓" : "✗ WRONG");
        } else {
            LOG_ERROR("[PEER_AUDIT] Broadcast Peer: Failed to get info (%s)", esp_err_to_name(result));
        }
    } else {
        LOG_WARN("[PEER_AUDIT] Broadcast Peer: NOT PRESENT");
    }
    
    // Check receiver peer
    if (receiver_mac[0] != 0 || receiver_mac[1] != 0 || receiver_mac[2] != 0) {
        if (esp_now_is_peer_exist(receiver_mac)) {
            esp_now_peer_info_t peer;
            esp_err_t result = esp_now_get_peer(receiver_mac, &peer);
            
            if (result == ESP_OK) {
                LOG_INFO("[PEER_AUDIT] Receiver Peer (%02X:%02X:%02X:%02X:%02X:%02X):",
                         receiver_mac[0], receiver_mac[1], receiver_mac[2],
                         receiver_mac[3], receiver_mac[4], receiver_mac[5]);
                LOG_INFO("[PEER_AUDIT]   Channel: %d %s", peer.channel,
                         (peer.channel != 0 && peer.channel != g_lock_channel) ? "✗ MISMATCH" : "✓");
                LOG_INFO("[PEER_AUDIT]   Encrypt: %d", peer.encrypt);
            } else {
                LOG_ERROR("[PEER_AUDIT] Receiver Peer: Failed to get info (%s)", esp_err_to_name(result));
            }
        } else {
            LOG_WARN("[PEER_AUDIT] Receiver Peer: NOT PRESENT");
        }
    } else {
        LOG_INFO("[PEER_AUDIT] Receiver: MAC not yet discovered");
    }
    
    LOG_INFO("[PEER_AUDIT] ═══ Audit Complete ═══");
}

void DiscoveryTask::update_recovery() {
    uint32_t time_in_state = millis() - state_entry_time_;
    
    switch (recovery_state_) {
        case RecoveryState::RESTART_FAILED:
            if (time_in_state > 5000) {  // Wait 5s before retry
                if (consecutive_failures_ < 5) {
                    LOG_INFO("[RECOVERY] Retrying restart (attempt %d/5)", consecutive_failures_ + 1);
                    consecutive_failures_++;
                    restart();
                } else {
                    LOG_ERROR("[RECOVERY] Maximum consecutive failures - escalating to persistent failure");
                    transition_to(RecoveryState::PERSISTENT_FAILURE);
                }
            }
            break;
            
        case RecoveryState::PERSISTENT_FAILURE:
            LOG_ERROR("[RECOVERY] Persistent failure state - requires manual intervention");
            // In production, could trigger system reset after timeout
            if (time_in_state > 60000) {  // 60 seconds
                LOG_ERROR("[RECOVERY] Triggering system restart due to persistent failure");
                esp_restart();
            }
            break;
            
        default:
            break;
    }
}

void DiscoveryTask::transition_to(RecoveryState new_state) {
    if (recovery_state_ != new_state) {
        LOG_INFO("[RECOVERY] State transition: %s → %s", 
                 state_to_string(recovery_state_), state_to_string(new_state));
        recovery_state_ = new_state;
        state_entry_time_ = millis();
    }
}

const char* DiscoveryTask::state_to_string(RecoveryState state) {
    switch (state) {
        case RecoveryState::NORMAL: return "NORMAL";
        case RecoveryState::CHANNEL_MISMATCH_DETECTED: return "CHANNEL_MISMATCH";
        case RecoveryState::RESTART_IN_PROGRESS: return "RESTARTING";
        case RecoveryState::RESTART_FAILED: return "FAILED";
        case RecoveryState::PERSISTENT_FAILURE: return "PERSISTENT_FAILURE";
        default: return "UNKNOWN";
    }
}

void DiscoveryMetrics::log_summary() const {
    LOG_INFO("[METRICS] ═══ Discovery Task Statistics ═══");
    LOG_INFO("[METRICS] Total Restarts: %d", total_restarts);
    LOG_INFO("[METRICS]   Successful: %d", successful_restarts);
    LOG_INFO("[METRICS]   Failed: %d", failed_restarts);
    LOG_INFO("[METRICS] Channel Mismatches: %d", channel_mismatches);
    LOG_INFO("[METRICS] Peer Cleanups: %d", peer_cleanup_count);
    LOG_INFO("[METRICS] Longest Downtime: %d ms", longest_downtime_ms);
    
    // Calculate reliability
    float success_rate = total_restarts > 0 
        ? (float)successful_restarts / total_restarts * 100.0f 
        : 100.0f;
    LOG_INFO("[METRICS] Restart Success Rate: %.1f%%", success_rate);
    LOG_INFO("[METRICS] ═══════════════════════════════");
}

// ============================================================================
// ACTIVE CHANNEL HOPPING IMPLEMENTATION (Section 11: Transmitter-Active)
// ============================================================================

void DiscoveryTask::start_active_channel_hopping() {
    LOG_INFO("[DISCOVERY] Starting ACTIVE channel hopping (Section 11 - transmitter-active mode)");
    LOG_INFO("[DISCOVERY] Transmitter will broadcast PROBE on each channel (1s/channel, 13s max)");
    
    // Create continuous hopping task
    xTaskCreatePinnedToCore(
        active_channel_hopping_task,
        "active_hop",
        task_config::STACK_SIZE_ANNOUNCEMENT,
        this,
        task_config::PRIORITY_LOW,  // Low priority (Priority 2) - doesn't block control code
        &task_handle_,
        1  // Core 1 - isolated from Battery Emulator (Core 0)
    );
    
    if (task_handle_ == nullptr) {
        LOG_ERROR("[DISCOVERY] Failed to create active channel hopping task!");
    } else {
        LOG_INFO("[DISCOVERY] Active hopping task started on Core 1 (Priority %d)", task_config::PRIORITY_LOW);
    }
}

void DiscoveryTask::send_probe_on_channel(uint8_t channel) {
    const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    
    // CRITICAL FIX: Delete old broadcast peer before adding new one
    // This prevents "peer already exists" errors when hopping channels
    if (esp_now_is_peer_exist(broadcast_mac)) {
        esp_err_t del_result = esp_now_del_peer(broadcast_mac);
        if (del_result != ESP_OK) {
            LOG_WARN("[DISCOVERY] Failed to remove old broadcast peer: %s", esp_err_to_name(del_result));
        }
    }
    
    // Add broadcast peer with explicit channel (0 = use current WiFi channel)
    esp_now_peer_info_t broadcast_peer = {};
    memcpy(broadcast_peer.peer_addr, broadcast_mac, 6);
    broadcast_peer.channel = 0;  // Use current WiFi channel
    broadcast_peer.encrypt = false;
    broadcast_peer.ifidx = WIFI_IF_STA;
    
    esp_err_t result = esp_now_add_peer(&broadcast_peer);
    if (result != ESP_OK && result != ESP_ERR_ESPNOW_EXIST) {
        LOG_ERROR("[DISCOVERY] Failed to add broadcast peer on channel %d: %s", 
                  channel, esp_err_to_name(result));
        return;
    }
    
    // Send PROBE broadcast
    probe_t probe;
    probe.type = msg_probe;
    probe.seq = millis();  // Use timestamp as sequence number
    
    result = esp_now_send(broadcast_mac, (const uint8_t*)&probe, sizeof(probe));
    if (result == ESP_OK) {
        LOG_DEBUG("[DISCOVERY] PROBE sent on channel %d (seq: %u)", channel, probe.seq);
    } else {
        LOG_ERROR("[DISCOVERY] Failed to send PROBE on channel %d: %s", 
                  channel, esp_err_to_name(result));
    }
}

bool DiscoveryTask::active_channel_hop_scan(uint8_t* discovered_channel) {
    LOG_INFO("[DISCOVERY] ═══ ACTIVE CHANNEL HOP SCAN (Broadcasting PROBE) ═══");
    
    // Channels to scan (regulatory domain dependent)
    const uint8_t channels[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
    const uint8_t num_channels = sizeof(channels) / sizeof(channels[0]);
    
    // Transmit duration per channel (ms)
    // Section 11: 1s per channel (vs 6s in Section 10 passive)
    // Total scan time: 13s max (vs 78s in Section 10)
    const uint32_t TRANSMIT_DURATION_MS = 1000;
    const uint32_t PROBE_INTERVAL_MS = 100;  // Send PROBE every 100ms on each channel
    
    volatile bool ack_received = false;
    volatile uint8_t ack_channel = 0;
    uint8_t ack_mac[6] = {0};
    
    // Scan each channel
    for (uint8_t i = 0; i < num_channels; i++) {
        uint8_t ch = channels[i];
        
        LOG_INFO("[DISCOVERY] Broadcasting PROBE on channel %d for %dms...", ch, TRANSMIT_DURATION_MS);
        
        // Switch to channel
        if (!set_channel(ch)) {
            LOG_ERROR("[DISCOVERY] Failed to set channel %d, skipping", ch);
            continue;
        }
        
        // Verify channel was set
        uint8_t actual_ch = 0;
        wifi_second_chan_t second;
        esp_wifi_get_channel(&actual_ch, &second);
        if (actual_ch != ch) {
            LOG_ERROR("[DISCOVERY] Channel mismatch: requested=%d, actual=%d", ch, actual_ch);
            metrics_.channel_mismatches++;
            continue;
        }
        
        // Transmit PROBE broadcasts on this channel
        // Using separate discovery queue so RX task doesn't consume our ACK messages
        ack_received = false;
        uint32_t start_time = millis();
        uint32_t last_probe_time = 0;
        
        // Flush any stale messages from discovery queue
        espnow_queue_msg_t flush_msg;
        while (xQueueReceive(espnow_discovery_queue, &flush_msg, 0) == pdTRUE) {}
        
        while (millis() - start_time < TRANSMIT_DURATION_MS) {
            // Send PROBE broadcast at intervals
            if (millis() - last_probe_time >= PROBE_INTERVAL_MS) {
                send_probe_on_channel(actual_ch);
                last_probe_time = millis();
            }
            
            // Check discovery queue for ACK response
            // This queue is separate from the main RX queue, so RX task won't consume it
            espnow_queue_msg_t msg;
            if (xQueueReceive(espnow_discovery_queue, &msg, pdMS_TO_TICKS(10)) == pdTRUE) {
                // Check if this is an ACK message
                if (msg.len >= (int)sizeof(ack_t)) {
                    const ack_t* a = reinterpret_cast<const ack_t*>(msg.data);
                    if (a->type == msg_ack) {
                        ack_received = true;
                        // CRITICAL: Use channel from ACK message, not transmitter's current channel
                        // The ACK contains the receiver's actual WiFi channel
                        ack_channel = a->channel;
                        memcpy(ack_mac, msg.mac, 6);
                        
                        LOG_INFO("[DISCOVERY] ✓ ACK received from %02X:%02X:%02X:%02X:%02X:%02X",
                                 msg.mac[0], msg.mac[1], msg.mac[2], 
                                 msg.mac[3], msg.mac[4], msg.mac[5]);
                        LOG_INFO("[DISCOVERY]   Channel in ACK: %d (receiver's WiFi channel)", ack_channel);
                        LOG_INFO("[DISCOVERY]   Sequence: %u (via discovery queue)", a->seq);
                        break;  // Exit transmit loop for this channel
                    }
                }
            }
            
            // Brief yield to prevent watchdog and allow ACKs to be received
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        
        if (ack_received) {
            // Found receiver! Register peer
            LOG_INFO("[DISCOVERY] ✓ Receiver found on channel %d", ack_channel);
            
            // CRITICAL: Change WiFi channel BEFORE adding peer
            // Otherwise peer channel won't match home channel and sends will fail
            esp_wifi_set_channel(ack_channel, WIFI_SECOND_CHAN_NONE);
            LOG_DEBUG("[DISCOVERY] WiFi channel set to %d", ack_channel);
            
            // Store receiver MAC globally
            memcpy(receiver_mac, ack_mac, 6);
            
            // Register peer with explicit channel (now matching our WiFi channel)
            if (!EspnowPeerManager::add_peer(ack_mac, ack_channel)) {
                LOG_ERROR("[DISCOVERY] Failed to add receiver as peer");
                continue;  // Try next channel
            }
            
            LOG_INFO("[DISCOVERY] ✓ Receiver registered as peer");
            
            // CRITICAL: Allow peer registration to stabilize before attempting sends
            // ESP-NOW peer table needs time to propagate through WiFi driver
            delay(200);
            LOG_DEBUG("[DISCOVERY] Peer registration stabilized");
            
            *discovered_channel = ack_channel;
            return true;  // Success!
        }
        
        LOG_DEBUG("[DISCOVERY] Channel %d: No ACK received", ch);
    }
    
    LOG_WARN("[DISCOVERY] ✗ Full scan complete - receiver not found");
    return false;
}

void DiscoveryTask::active_channel_hopping_task(void* parameter) {
    DiscoveryTask* self = static_cast<DiscoveryTask*>(parameter);
    
    uint8_t discovered_channel = 0;
    uint32_t scan_attempt = 0;
    bool discovery_complete = false;
    
    LOG_INFO("[DISCOVERY] ═══ ACTIVE CHANNEL HOPPING STARTED ═══");
    LOG_INFO("[DISCOVERY] Transmitter broadcasts PROBE until receiver ACKs");
    LOG_INFO("[DISCOVERY] Each full scan takes ~13 seconds (1s × 13 channels)");
    LOG_INFO("[DISCOVERY] Section 11 Architecture: 6x faster than Section 10 passive (78s → 13s)");
    
    while (!discovery_complete) {
        scan_attempt++;
        LOG_INFO("[DISCOVERY] ═══ Active Hopping Scan Attempt #%d ═══", scan_attempt);
        
        if (self->active_channel_hop_scan(&discovered_channel)) {
            // Receiver found!
            LOG_INFO("[DISCOVERY] ✓ Receiver discovered on channel %d", discovered_channel);
            
            // Lock to discovered channel
            g_lock_channel = discovered_channel;
            self->force_and_verify_channel(discovered_channel);
            
            // Flush cached data (will use EnhancedCache in future)
            if (!DataCache::instance().is_empty()) {
                LOG_INFO("[DISCOVERY] Flushing %d cached messages...", DataCache::instance().size());
                size_t flushed = DataCache::instance().flush();
                LOG_INFO("[DISCOVERY] ✓ %d messages flushed to receiver", flushed);
            }
            
            // Notify message handler
            LOG_INFO("[DISCOVERY] ✓ ESP-NOW connection established");
            
            // Send initial version beacon with current config versions
            // This allows receiver to request any config sections it doesn't have cached
            VersionBeaconManager::instance().send_version_beacon(true);
            LOG_INFO("[DISCOVERY] ✓ Initial version beacon sent to receiver");
            
            discovery_complete = true;  // Mark complete
            break;  // Exit scan loop
        }
        
        // No receiver found this cycle - wait before retrying
        LOG_INFO("[DISCOVERY] Waiting 5s before next scan cycle...");
        vTaskDelay(pdMS_TO_TICKS(5000));  // 5s retry (vs 10s in passive mode)
    }
    
    LOG_INFO("[DISCOVERY] ✓ Active channel hopping complete - receiver connected");
    LOG_INFO("[DISCOVERY] Total scan attempts: %d", scan_attempt);
    LOG_INFO("[DISCOVERY] Discovery time: ~%d seconds", scan_attempt * 13);
    
    // Task can exit - connection is now established
    // Keep-alive will be handled by separate manager
    vTaskSuspend(NULL);  // Suspend task (not delete - may want to restart later)
}
