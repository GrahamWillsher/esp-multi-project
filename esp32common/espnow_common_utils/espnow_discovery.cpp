/**
 * @file espnow_discovery.cpp
 * @brief Implementation of ESP-NOW bidirectional discovery
 */

#include "espnow_discovery.h"
#include "espnow_peer_manager.h"
#include <WiFi.h>
#include <esp_now.h>
#include <espnow_common.h>

EspnowDiscovery& EspnowDiscovery::instance() {
    static EspnowDiscovery instance;
    return instance;
}

void EspnowDiscovery::start(std::function<bool()> is_connected_callback,
                           uint32_t interval_ms,
                           uint8_t task_priority,
                           uint32_t stack_size) {
    if (task_handle_ != nullptr) {
        Serial.println("[DISCOVERY] Task already running");
        return;
    }
    
    // Allocate task configuration
    config_ = new TaskConfig{is_connected_callback, interval_ms};
    
    // Create announcement task
    BaseType_t result = xTaskCreate(
        task_impl,
        "espnow_announce",
        stack_size,
        (void*)config_,
        task_priority,
        &task_handle_
    );
    
    if (result == pdPASS) {
        Serial.println("[DISCOVERY] Announcement task started");
    } else {
        Serial.println("[DISCOVERY] Failed to create announcement task");
        delete config_;
        config_ = nullptr;
    }
}

void EspnowDiscovery::stop() {
    if (task_handle_ != nullptr) {
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
        
        if (config_ != nullptr) {
            delete config_;
            config_ = nullptr;
        }
        
        Serial.println("[DISCOVERY] Announcement task stopped");
    }
}

void EspnowDiscovery::task_impl(void* parameter) {
    TaskConfig* config = static_cast<TaskConfig*>(parameter);
    
    Serial.println("[DISCOVERY] Periodic announcement started (bidirectional discovery)");
    
    // Add broadcast peer for sending announcements
    if (!EspnowPeerManager::add_broadcast_peer()) {
        Serial.println("[DISCOVERY] ERROR: Failed to add broadcast peer");
        vTaskDelete(nullptr);
        return;
    }
    
    // Send announcements periodically
    const TickType_t interval_ticks = pdMS_TO_TICKS(config->interval_ms);
    
    for (;;) {
        // Check if peer is connected (via callback)
        if (config->is_connected && config->is_connected()) {
            Serial.println("[DISCOVERY] Peer connected - stopping announcements");
            instance().task_handle_ = nullptr;
            delete config;
            vTaskDelete(nullptr);
            return;
        }
        
        // Send announcement PROBE
        probe_t announce = { msg_probe, (uint32_t)esp_random() };
        const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        esp_err_t result = esp_now_send(broadcast_mac, 
                                       (const uint8_t*)&announce, 
                                       sizeof(announce));
        
        if (result == ESP_OK) {
            Serial.printf("[DISCOVERY] Sent announcement (seq=%u) on channel %d\n", 
                         announce.seq, WiFi.channel());
        } else {
            Serial.printf("[DISCOVERY] Send failed: %s\n", esp_err_to_name(result));
        }
        
        vTaskDelay(interval_ticks);
    }
}
