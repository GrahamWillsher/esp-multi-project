/**
 * @file component_config_sender.h
 * @brief Sends component configuration to receiver via ESP-NOW
 * 
 * Transmits active component selections (BMS type, inverter type, etc.)
 * to receiver for display and NVS storage.
 * 
 * Transmission strategy:
 * - Send on connection establishment
 * - Send every 5 seconds (periodic update)
 * - Send immediately when configuration changes
 */

#pragma once

#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class ComponentConfigSender {
 public:
  static ComponentConfigSender& instance();
  
  /**
   * @brief Send current component configuration to receiver
   * @return true if sent successfully
   */
  bool send_component_config();
  
  /**
   * @brief Start periodic sender task (sends every 5s)
   */
  void start_periodic_sender();
  
  /**
   * @brief Stop periodic sender task
   */
  void stop_periodic_sender();
  
  /**
   * @brief Notify that configuration has changed (triggers immediate send)
   */
  void notify_config_changed();
  
 private:
  ComponentConfigSender() = default;
  ~ComponentConfigSender() = default;
  
  // Task for periodic sending
  static void periodic_task_impl(void* parameter);
  
  // Configuration version tracking
  uint32_t config_version_ = 1;
  
  // Task handle
  TaskHandle_t task_handle_ = nullptr;
  
  // Flag to trigger immediate send
  volatile bool config_changed_ = false;
};
