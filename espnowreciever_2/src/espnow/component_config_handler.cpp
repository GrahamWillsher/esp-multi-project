/**
 * @file component_config_handler.cpp
 * @brief Implementation of component configuration handler
 */

#include "component_config_handler.h"
#include "../common.h"
#include <esp_log.h>
#include <espnow_common.h>
#include <nvs_flash.h>
#include <cstring>

static const char* TAG = "COMP_CFG";

// NVS keys
#define NVS_NAMESPACE "comp_cfg"
#define NVS_BMS_TYPE_KEY "bms"
#define NVS_SEC_BMS_KEY "sec_bms"
#define NVS_INV_TYPE_KEY "inv"
#define NVS_CHG_TYPE_KEY "chg"
#define NVS_SHUNT_TYPE_KEY "shunt"
#define NVS_MULTI_BAT_KEY "multi"
#define NVS_VERSION_KEY "version"

// BMS type names (matching transmitter BatteryType enum)
static const char* BMS_NAMES[] = {
  "None", "BMW i3", "BMW iX", "BMW PHEV", "BMW SBox",
  "Bolt/Ampera", "BYD Atto 3", "Cellpower BMS", "CHAdeMO", "CMFA EV",
  "CMP Smart", "Daly BMS", "ECMP", "Ford Mach-E", "Foxess",
  "Geely Geometry C", "Hyundai Ioniq 28", "i-MiEV/C-Zero", "Jaguar I-PACE", "Kia 64FD",
  "Kia E-GMP", "Kia/Hyundai 64", "Kia/Hyundai Hybrid", "Maxus EV80", "VW MEB",
  "MG 5", "MG HS PHEV", "Nissan Leaf", "Orion BMS", "Pylon",
  "Range Rover PHEV", "Relion LV", "Renault Kangoo", "Renault Twizy", "Renault Zoe Gen1",
  "Renault Zoe Gen2", "Rivian", "RJXZS BMS", "Samsung SDI LV", "Santa Fe PHEV",
  "SimpBMS", "Sono", "Tesla", "Test/Fake", "Volvo SPA",
  "Volvo SPA Hybrid"
};
#define BMS_NAME_COUNT (sizeof(BMS_NAMES) / sizeof(BMS_NAMES[0]))

// Inverter type names (matching transmitter InverterType enum)
static const char* INVERTER_NAMES[] = {
  "None", "Afore battery over CAN", "BYD Battery-Box Premium HVS over CAN Bus", "BYD 11kWh HVM battery over Modbus RTU", "Ferroamp Pylon battery over CAN bus",
  "FoxESS compatible HV2600/ECS4100 battery", "Growatt High Voltage protocol via CAN", "Growatt Low Voltage (48V) protocol via CAN", "Growatt WIT compatible battery via CAN", "BYD battery via Kostal RS485",
  "Pylontech HV battery over CAN bus", "Pylontech LV battery over CAN bus", "Schneider V2 SE BMS CAN", "SMA compatible BYD H", "SMA compatible BYD Battery-Box HVS",
  "SMA Low Voltage (48V) protocol via CAN", "SMA Tripower CAN", "Sofar BMS (Extended) via CAN, Battery ID", "SolaX Triple Power LFP over CAN bus", "Solxpow compatible battery",
  "Sol-Ark LV protocol over CAN bus", "Sungrow SBRXXX emulation over CAN bus"
};
#define INVERTER_NAME_COUNT (sizeof(INVERTER_NAMES) / sizeof(INVERTER_NAMES[0]))

// Charger type names (matching transmitter ChargerType enum)
static const char* CHARGER_NAMES[] = {
  "None", "Chevy Volt", "Nissan Leaf"
};
#define CHARGER_NAME_COUNT (sizeof(CHARGER_NAMES) / sizeof(CHARGER_NAMES[0]))

// Shunt type names (matching transmitter ShuntTypeEnum)
static const char* SHUNT_NAMES[] = {
  "None", "BMW SBox", "Inverter"
};
#define SHUNT_NAME_COUNT (sizeof(SHUNT_NAMES) / sizeof(SHUNT_NAMES[0]))

ComponentConfigHandler& ComponentConfigHandler::instance() {
  static ComponentConfigHandler instance;
  return instance;
}

ComponentConfigHandler::~ComponentConfigHandler() {
  if (nvs_handle_ != 0) {
    nvs_close(nvs_handle_);
  }
}

bool ComponentConfigHandler::init() {
  ESP_LOGI(TAG, "Initializing component config handler...");
  
  // Initialize NVS
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_LOGW(TAG, "NVS partition needs erasing, reinitializing...");
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "NVS init failed: %d", err);
    return false;
  }
  
  // Open NVS handle
  err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to open NVS namespace: %d", err);
    return false;
  }
  
  // Load stored configuration
  bool loaded = load_from_nvs();
  
  ESP_LOGI(TAG, "✓ Component config handler initialized");
  if (loaded) {
    print_config();
  }
  
  return true;
}

bool ComponentConfigHandler::handle_message(const uint8_t* data, size_t len) {
  if (len != sizeof(component_config_msg_t)) {
    ESP_LOGW(TAG, "Invalid message size: %d (expected %d)", len, sizeof(component_config_msg_t));
    return false;
  }
  
  // Validate checksum
  if (!validate_checksum(data, len)) {
    ESP_LOGW(TAG, "Checksum validation failed");
    return false;
  }
  
  const component_config_msg_t* msg = reinterpret_cast<const component_config_msg_t*>(data);
  
  // Check if this is a new version
  if (msg->config_version <= config_.config_version && config_received_) {
    // Older or same version - ignore
    return true;
  }
  
  // Update configuration
  config_.bms_type = msg->bms_type;
  config_.secondary_bms_type = msg->secondary_bms_type;
  config_.inverter_type = msg->inverter_type;
  config_.charger_type = msg->charger_type;
  config_.shunt_type = msg->shunt_type;
  config_.multi_battery_enabled = (msg->multi_battery_enabled != 0);
  config_.config_version = msg->config_version;
  config_.last_update_ms = millis();
  
  config_received_ = true;
  
  ESP_LOGI(TAG, "✓ Received component config v%lu", config_.config_version);
  print_config();
  
  // Save to NVS
  return save_to_nvs();
}

const char* ComponentConfigHandler::get_bms_name(uint8_t type) const {
  if (type < BMS_NAME_COUNT) {
    return BMS_NAMES[type];
  }
  return "Unknown";
}

const char* ComponentConfigHandler::get_inverter_name(uint8_t type) const {
  if (type < INVERTER_NAME_COUNT) {
    return INVERTER_NAMES[type];
  }
  return "Unknown";
}

const char* ComponentConfigHandler::get_charger_name(uint8_t type) const {
  if (type < CHARGER_NAME_COUNT) {
    return CHARGER_NAMES[type];
  }
  return "Unknown";
}

const char* ComponentConfigHandler::get_shunt_name(uint8_t type) const {
  if (type < SHUNT_NAME_COUNT) {
    return SHUNT_NAMES[type];
  }
  return "Unknown";
}

void ComponentConfigHandler::print_config() {
  ESP_LOGI(TAG, "=== Component Configuration v%lu ===", config_.config_version);
  ESP_LOGI(TAG, "Primary BMS: %s (type %d)", get_bms_name(config_.bms_type), config_.bms_type);
  
  if (config_.multi_battery_enabled && config_.secondary_bms_type != 0) {
    ESP_LOGI(TAG, "Secondary BMS: %s (type %d)", 
             get_bms_name(config_.secondary_bms_type), config_.secondary_bms_type);
  }
  
  ESP_LOGI(TAG, "Inverter: %s (type %d)", 
           get_inverter_name(config_.inverter_type), config_.inverter_type);
  ESP_LOGI(TAG, "Charger: %s (type %d)", 
           get_charger_name(config_.charger_type), config_.charger_type);
  ESP_LOGI(TAG, "Shunt: %s (type %d)", 
           get_shunt_name(config_.shunt_type), config_.shunt_type);
  ESP_LOGI(TAG, "Multi-battery: %s", config_.multi_battery_enabled ? "ENABLED" : "DISABLED");
  ESP_LOGI(TAG, "================================");
}

bool ComponentConfigHandler::load_from_nvs() {
  ESP_LOGI(TAG, "Loading component config from NVS...");
  
  esp_err_t err;
  
  // Load each field (use defaults if not found)
  err = nvs_get_u8(nvs_handle_, NVS_BMS_TYPE_KEY, &config_.bms_type);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    config_.bms_type = 29;  // Default: Pylon
  }
  
  err = nvs_get_u8(nvs_handle_, NVS_SEC_BMS_KEY, &config_.secondary_bms_type);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    config_.secondary_bms_type = 0;  // None
  }
  
  err = nvs_get_u8(nvs_handle_, NVS_INV_TYPE_KEY, &config_.inverter_type);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    config_.inverter_type = 0;  // None
  }
  
  err = nvs_get_u8(nvs_handle_, NVS_CHG_TYPE_KEY, &config_.charger_type);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    config_.charger_type = 0;  // None
  }
  
  err = nvs_get_u8(nvs_handle_, NVS_SHUNT_TYPE_KEY, &config_.shunt_type);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    config_.shunt_type = 0;  // None
  }
  
  uint8_t multi_u8 = 0;
  err = nvs_get_u8(nvs_handle_, NVS_MULTI_BAT_KEY, &multi_u8);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    multi_u8 = 0;
  }
  config_.multi_battery_enabled = (multi_u8 != 0);
  
  err = nvs_get_u32(nvs_handle_, NVS_VERSION_KEY, &config_.config_version);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    config_.config_version = 0;
  }
  
  ESP_LOGI(TAG, "✓ Loaded component config from NVS (v%lu)", config_.config_version);
  return true;
}

bool ComponentConfigHandler::save_to_nvs() {
  ESP_LOGD(TAG, "Saving component config to NVS...");
  
  esp_err_t err;
  
  err = nvs_set_u8(nvs_handle_, NVS_BMS_TYPE_KEY, config_.bms_type);
  if (err != ESP_OK) return false;
  
  err = nvs_set_u8(nvs_handle_, NVS_SEC_BMS_KEY, config_.secondary_bms_type);
  if (err != ESP_OK) return false;
  
  err = nvs_set_u8(nvs_handle_, NVS_INV_TYPE_KEY, config_.inverter_type);
  if (err != ESP_OK) return false;
  
  err = nvs_set_u8(nvs_handle_, NVS_CHG_TYPE_KEY, config_.charger_type);
  if (err != ESP_OK) return false;
  
  err = nvs_set_u8(nvs_handle_, NVS_SHUNT_TYPE_KEY, config_.shunt_type);
  if (err != ESP_OK) return false;
  
  uint8_t multi_u8 = config_.multi_battery_enabled ? 1 : 0;
  err = nvs_set_u8(nvs_handle_, NVS_MULTI_BAT_KEY, multi_u8);
  if (err != ESP_OK) return false;
  
  err = nvs_set_u32(nvs_handle_, NVS_VERSION_KEY, config_.config_version);
  if (err != ESP_OK) return false;
  
  // Commit
  err = nvs_commit(nvs_handle_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "NVS commit failed: %d", err);
    return false;
  }
  
  ESP_LOGD(TAG, "✓ Saved component config to NVS");
  return true;
}

bool ComponentConfigHandler::validate_checksum(const uint8_t* data, size_t len) const {
  if (len < sizeof(uint16_t)) {
    return false;
  }
  
  // Calculate checksum (exclude last 2 bytes which are the checksum itself)
  uint16_t calculated = 0;
  for (size_t i = 0; i < len - sizeof(uint16_t); i++) {
    calculated += data[i];
  }
  
  // Extract stored checksum (last 2 bytes)
  uint16_t stored;
  memcpy(&stored, data + len - sizeof(uint16_t), sizeof(uint16_t));
  
  return (calculated == stored);
}
