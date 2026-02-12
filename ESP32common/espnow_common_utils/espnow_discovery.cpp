/**
 * @file espnow_discovery.cpp
 * @brief Implementation of ESP-NOW bidirectional discovery
 */

#include "espnow_discovery.h"
#include "espnow_peer_manager.h"
#include <WiFi.h>
#include <esp_now.h>
#include <espnow_common.h>
#include <mqtt_logger.h>

EspnowDiscovery& EspnowDiscovery::instance() {
    static EspnowDiscovery instance;
    return instance;
}

void EspnowDiscovery::start(std::function<bool()> is_connected_callback,
                           uint32_t interval_ms,
                           uint8_t task_priority,
                           uint32_t stack_size) {
    if (task_handle_ != nullptr) {
        MQTT_LOG_WARNING("DISCOVERY", "Task already running");
        return;
    }
    
    // Save configuration for restart capability
    last_is_connected_callback_ = is_connected_callback;
    last_interval_ms_ = interval_ms;
    last_task_priority_ = task_priority;
    last_stack_size_ = stack_size;
    suspended_ = false;
    
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
        MQTT_LOG_INFO("DISCOVERY", "Announcement task started");
    } else {
        MQTT_LOG_ERROR("DISCOVERY", "Failed to create announcement task");
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
        
        suspended_ = false;
        MQTT_LOG_INFO("DISCOVERY", "Announcement task stopped");
    }
}

void EspnowDiscovery::suspend() {
    if (task_handle_ != nullptr && !suspended_) {
        suspended_ = true;
        MQTT_LOG_INFO("DISCOVERY", "Announcements suspended (task kept alive)");
    }
}

void EspnowDiscovery::resume() {
    if (task_handle_ != nullptr && suspended_) {
        suspended_ = false;
        MQTT_LOG_INFO("DISCOVERY", "Announcements resumed");
    } else if (task_handle_ == nullptr) {
        MQTT_LOG_WARNING("DISCOVERY", "Cannot resume - task not running");
    }
}

void EspnowDiscovery::restart() {
    MQTT_LOG_INFO("DISCOVERY", "Restarting discovery task");
    
    // Stop existing task if running
    stop();
    
    // Restart with saved parameters
    if (last_is_connected_callback_) {
        start(last_is_connected_callback_, last_interval_ms_, 
              last_task_priority_, last_stack_size_);
    } else {
        MQTT_LOG_ERROR("DISCOVERY", "Cannot restart - no saved configuration");
    }
}

void EspnowDiscovery::task_impl(void* parameter) {
    TaskConfig* config = static_cast<TaskConfig*>(parameter);
    
    MQTT_LOG_INFO("DISCOVERY", "Periodic announcement started (bidirectional discovery)");
    
    // Add broadcast peer for sending announcements
    if (!EspnowPeerManager::add_broadcast_peer()) {
        MQTT_LOG_ERROR("DISCOVERY", "Failed to add broadcast peer");
        vTaskDelete(nullptr);
        return;
    }
    
    // Send announcements periodically
    const TickType_t interval_ticks = pdMS_TO_TICKS(config->interval_ms);
    
    for (;;) {
        // Check if suspended
        if (instance().suspended_) {
            vTaskDelay(pdMS_TO_TICKS(1000));  // Sleep while suspended
            continue;
        }
        
        // Check if peer is connected (via callback)
        if (config->is_connected && config->is_connected()) {
            MQTT_LOG_INFO("DISCOVERY", "Peer connected - suspending announcements");
            instance().suspended_ = true;
            continue;  // Keep task alive, just suspend
        }
        
        // Send announcement PROBE
        probe_t announce = { msg_probe, (uint32_t)esp_random() };
        const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        esp_err_t result = esp_now_send(broadcast_mac, 
                                       (const uint8_t*)&announce, 
                                       sizeof(announce));
        
        if (result == ESP_OK) {
            MQTT_LOG_DEBUG("DISCOVERY", "Sent announcement (seq=%u) on channel %d", 
                          announce.seq, WiFi.channel());
        } else {
            MQTT_LOG_WARNING("DISCOVERY", "Send failed: %s", esp_err_to_name(result));
        }
        
        vTaskDelay(interval_ticks);
    }
}
