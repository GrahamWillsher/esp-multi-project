/**
 * @file component_config_sender.cpp
 * @brief Implementation of component configuration sender
 */

#include "component_config_sender.h"
#include "../system_settings.h"
#include "../config/logging_config.h"
#include <mqtt_logger.h>
#include <espnow_common.h>
#include <espnow_send_utils.h>
#include <espnow_peer_manager.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

ComponentConfigSender& ComponentConfigSender::instance() {
  static ComponentConfigSender instance;
  return instance;
}

bool ComponentConfigSender::send_component_config() {
  SystemSettings& settings = SystemSettings::instance();
  
  // Build component config message
  component_config_msg_t msg;
  msg.type = msg_component_config;
  msg.bms_type = settings.get_bms_type();
  msg.secondary_bms_type = settings.get_secondary_bms_type();
  msg.battery_type = settings.get_battery_profile_type();
  msg.inverter_type = settings.get_inverter_type();
  msg.charger_type = settings.get_charger_type();
  msg.shunt_type = settings.get_shunt_type();
  msg.multi_battery_enabled = settings.is_multi_battery_enabled() ? 1 : 0;
  msg.config_version = config_version_;
  
  // Calculate checksum (simple sum)
  msg.checksum = 0;
  uint8_t* data = reinterpret_cast<uint8_t*>(&msg);
  for (size_t i = 0; i < sizeof(msg) - sizeof(msg.checksum); i++) {
    msg.checksum += data[i];
  }
  
  // Send via ESP-NOW (use broadcast MAC - peer manager will forward to receiver)
  bool success = EspnowSendUtils::send_with_retry(
    ESPNOW_BROADCAST_MAC,
    &msg,
    sizeof(msg),
    "component_config"
  );
  
  if (success) {
    LOG_DEBUG("COMP_CFG", "Sent component config: BMS=%d, Battery=%d, Inv=%d, Chg=%d, Shunt=%d (v%lu)",
              msg.bms_type, msg.battery_type, msg.inverter_type, msg.charger_type, msg.shunt_type, msg.config_version);
  } else {
    LOG_WARN("COMP_CFG", "Failed to send component config");
  }
  
  return success;
}

void ComponentConfigSender::start_periodic_sender() {
  if (task_handle_ != nullptr) {
    LOG_WARN("COMP_CFG", "Periodic sender already running");
    return;
  }
  
  LOG_INFO("COMP_CFG", "Starting periodic component config sender (5s interval)");
  
  xTaskCreate(
    periodic_task_impl,
    "comp_cfg_send",
    3072,  // Stack size
    this,
    2,     // Priority
    &task_handle_
  );
}

void ComponentConfigSender::stop_periodic_sender() {
  if (task_handle_ == nullptr) {
    return;
  }
  
  LOG_INFO("COMP_CFG", "Stopping periodic component config sender");
  
  vTaskDelete(task_handle_);
  task_handle_ = nullptr;
}

void ComponentConfigSender::notify_config_changed() {
  config_version_++;
  config_changed_ = true;
  LOG_INFO("COMP_CFG", "Configuration changed, version now %lu", config_version_);
}

void ComponentConfigSender::periodic_task_impl(void* parameter) {
  ComponentConfigSender* sender = static_cast<ComponentConfigSender*>(parameter);
  
  TickType_t last_send = xTaskGetTickCount();
  const TickType_t interval = pdMS_TO_TICKS(5000);  // 5 seconds
  
  while (true) {
    // Check if immediate send is needed due to config change
    if (sender->config_changed_) {
      sender->send_component_config();
      sender->config_changed_ = false;
      last_send = xTaskGetTickCount();
    }
    
    // Check if periodic send is due
    TickType_t now = xTaskGetTickCount();
    if ((now - last_send) >= interval) {
      sender->send_component_config();
      last_send = now;
    }
    
    // Sleep for 500ms before checking again
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
