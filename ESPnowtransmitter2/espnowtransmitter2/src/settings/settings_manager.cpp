#include "settings_manager.h"
#include "../config/logging_config.h"
#include <esp_now.h>
#include <espnow_packet_utils.h>

SettingsManager& SettingsManager::instance() {
    static SettingsManager instance;
    return instance;
}

SettingsManager::SettingsManager() {
}

bool SettingsManager::init() {
    if (initialized_) {
        LOG_WARN("[SETTINGS] Already initialized");
        return true;
    }
    
    LOG_INFO("[SETTINGS] Initializing settings manager...");
    
    // Load all settings from NVS
    // Note: load_battery_settings() returns false on first boot (namespace doesn't exist)
    // but still loads default values. We only call restore_defaults() explicitly if needed
    // to initialize the NVS namespace.
    bool loaded = load_all_settings();
    if (!loaded) {
        // First boot - NVS namespace doesn't exist yet
        // Settings already have default values from load function
        // Just save them to NVS to create the namespace
        LOG_INFO("[SETTINGS] First boot - initializing NVS with defaults");
        save_battery_settings();
    }
    
    initialized_ = true;
    LOG_INFO("[SETTINGS] Settings manager initialized");
    return true;
}

bool SettingsManager::load_all_settings() {
    bool success = true;
    
    // Load battery settings
    if (!load_battery_settings()) {
        LOG_WARN("[SETTINGS] Failed to load battery settings");
        success = false;
    }
    
    // Add other category loaders here in future phases
    // load_charger_settings();
    // load_inverter_settings();
    // load_system_settings();
    
    return success;
}

bool SettingsManager::load_battery_settings() {
    Preferences prefs;
    
    if (!prefs.begin("battery", true)) { // Read-only
        LOG_WARN("[SETTINGS] Battery namespace doesn't exist yet (first boot) - will use defaults");
        return false; // Not an error - just means first boot
    }
    
    // Load settings with defaults as fallback
    battery_capacity_wh_ = prefs.getUInt("capacity_wh", 30000);
    battery_max_voltage_mv_ = prefs.getUInt("max_volt_mv", 58000);
    battery_min_voltage_mv_ = prefs.getUInt("min_volt_mv", 46000);
    battery_max_charge_current_a_ = prefs.getFloat("max_chg_a", 100.0f);
    battery_max_discharge_current_a_ = prefs.getFloat("max_dis_a", 100.0f);
    battery_soc_high_limit_ = prefs.getUChar("soc_high", 95);
    battery_soc_low_limit_ = prefs.getUChar("soc_low", 20);
    battery_cell_count_ = prefs.getUChar("cell_count", 16);
    battery_chemistry_ = prefs.getUChar("chemistry", 2); // LFP
    battery_settings_version_ = prefs.getUInt("version", 0);
    
    prefs.end();
    
    LOG_INFO("[SETTINGS] Battery: %dWh, %dS, %dmV-%dmV, ±%.1fA/%.1fA, SOC:%d%%-%d%%, version:%u",
             battery_capacity_wh_, battery_cell_count_,
             battery_min_voltage_mv_, battery_max_voltage_mv_,
             battery_max_charge_current_a_, battery_max_discharge_current_a_,
             battery_soc_low_limit_, battery_soc_high_limit_, battery_settings_version_);
    
    return true;
}

bool SettingsManager::save_battery_settings() {
    Preferences prefs;
    
    if (!prefs.begin("battery", false)) { // Read-write
        LOG_ERROR("[SETTINGS] Failed to open battery namespace for writing");
        return false;
    }
    
    // Save all battery settings
    prefs.putUInt("capacity_wh", battery_capacity_wh_);
    prefs.putUInt("max_volt_mv", battery_max_voltage_mv_);
    prefs.putUInt("min_volt_mv", battery_min_voltage_mv_);
    prefs.putFloat("max_chg_a", battery_max_charge_current_a_);
    prefs.putFloat("max_dis_a", battery_max_discharge_current_a_);
    prefs.putUChar("soc_high", battery_soc_high_limit_);
    prefs.putUChar("soc_low", battery_soc_low_limit_);
    prefs.putUChar("cell_count", battery_cell_count_);
    prefs.putUChar("chemistry", battery_chemistry_);
    prefs.putUInt("version", battery_settings_version_);
    
    prefs.end();
    
    LOG_INFO("[SETTINGS] Battery settings saved to NVS (version %u)", battery_settings_version_);
    return true;
}

void SettingsManager::restore_defaults() {
    LOG_INFO("[SETTINGS] Restoring factory defaults...");
    
    // Battery defaults
    battery_capacity_wh_ = 30000;
    battery_max_voltage_mv_ = 58000;
    battery_min_voltage_mv_ = 46000;
    battery_max_charge_current_a_ = 100.0f;
    battery_max_discharge_current_a_ = 100.0f;
    battery_soc_high_limit_ = 95;
    battery_soc_low_limit_ = 20;
    battery_cell_count_ = 16;
    battery_chemistry_ = 2; // LFP
    
    // Save defaults to NVS
    save_battery_settings();
    
    LOG_INFO("[SETTINGS] Factory defaults restored");
}

bool SettingsManager::save_battery_setting(uint8_t field_id, uint32_t value_uint32, float value_float, const char* value_string) {
    bool changed = false;
    
    switch (field_id) {
        case BATTERY_CAPACITY_WH:
            if (value_uint32 >= 1000 && value_uint32 <= 1000000) { // 1kWh - 1MWh range check
                battery_capacity_wh_ = value_uint32;
                changed = true;
                LOG_INFO("[SETTINGS] Battery capacity updated: %dWh", battery_capacity_wh_);
            } else {
                LOG_ERROR("[SETTINGS] Invalid capacity: %d (must be 1000-1000000)", value_uint32);
                return false;
            }
            break;
            
        case BATTERY_MAX_VOLTAGE_MV:
            if (value_uint32 >= 30000 && value_uint32 <= 100000) { // 30V - 100V range check
                battery_max_voltage_mv_ = value_uint32;
                changed = true;
                LOG_INFO("[SETTINGS] Max voltage updated: %dmV", battery_max_voltage_mv_);
            } else {
                LOG_ERROR("[SETTINGS] Invalid max voltage: %d", value_uint32);
                return false;
            }
            break;
            
        case BATTERY_MIN_VOLTAGE_MV:
            if (value_uint32 >= 20000 && value_uint32 <= 80000) { // 20V - 80V range check
                battery_min_voltage_mv_ = value_uint32;
                changed = true;
                LOG_INFO("[SETTINGS] Min voltage updated: %dmV", battery_min_voltage_mv_);
            } else {
                LOG_ERROR("[SETTINGS] Invalid min voltage: %d", value_uint32);
                return false;
            }
            break;
            
        case BATTERY_MAX_CHARGE_CURRENT_A:
            if (value_float >= 0.0f && value_float <= 500.0f) { // 0-500A range check
                battery_max_charge_current_a_ = value_float;
                changed = true;
                LOG_INFO("[SETTINGS] Max charge current updated: %.1fA", battery_max_charge_current_a_);
            } else {
                LOG_ERROR("[SETTINGS] Invalid charge current: %.1f", value_float);
                return false;
            }
            break;
            
        case BATTERY_MAX_DISCHARGE_CURRENT_A:
            if (value_float >= 0.0f && value_float <= 500.0f) { // 0-500A range check
                battery_max_discharge_current_a_ = value_float;
                changed = true;
                LOG_INFO("[SETTINGS] Max discharge current updated: %.1fA", battery_max_discharge_current_a_);
            } else {
                LOG_ERROR("[SETTINGS] Invalid discharge current: %.1f", value_float);
                return false;
            }
            break;
            
        case BATTERY_SOC_HIGH_LIMIT:
            if (value_uint32 >= 50 && value_uint32 <= 100) { // 50-100% range check
                battery_soc_high_limit_ = value_uint32;
                changed = true;
                LOG_INFO("[SETTINGS] SOC high limit updated: %d%%", battery_soc_high_limit_);
            } else {
                LOG_ERROR("[SETTINGS] Invalid SOC high: %d", value_uint32);
                return false;
            }
            break;
            
        case BATTERY_SOC_LOW_LIMIT:
            if (value_uint32 >= 0 && value_uint32 <= 50) { // 0-50% range check
                battery_soc_low_limit_ = value_uint32;
                changed = true;
                LOG_INFO("[SETTINGS] SOC low limit updated: %d%%", battery_soc_low_limit_);
            } else {
                LOG_ERROR("[SETTINGS] Invalid SOC low: %d", value_uint32);
                return false;
            }
            break;
            
        case BATTERY_CELL_COUNT:
            if (value_uint32 >= 4 && value_uint32 <= 32) { // 4-32 cells (12V-100V nominal)
                battery_cell_count_ = value_uint32;
                changed = true;
                LOG_INFO("[SETTINGS] Cell count updated: %dS", battery_cell_count_);
            } else {
                LOG_ERROR("[SETTINGS] Invalid cell count: %d", value_uint32);
                return false;
            }
            break;
            
        case BATTERY_CHEMISTRY:
            if (value_uint32 <= 3) { // 0=NCA, 1=NMC, 2=LFP, 3=LTO
                battery_chemistry_ = value_uint32;
                changed = true;
                const char* chem[] = {"NCA", "NMC", "LFP", "LTO"};
                LOG_INFO("[SETTINGS] Chemistry updated: %s", chem[battery_chemistry_]);
            } else {
                LOG_ERROR("[SETTINGS] Invalid chemistry: %d", value_uint32);
                return false;
            }
            break;
            
        default:
            LOG_ERROR("[SETTINGS] Unknown battery field ID: %d", field_id);
            return false;
    }
    
    // Save to NVS if changed
    if (changed) {
        increment_battery_version();
        bool saved = save_battery_settings();
        if (saved) {
            // Send change notification to receiver
            send_settings_changed_notification(SETTINGS_BATTERY, battery_settings_version_);
        }
        return saved;
    }
    
    return true;
}

void SettingsManager::handle_settings_update(const espnow_queue_msg_t& msg) {
    LOG_INFO("[SETTINGS] ═══ Settings Update Message Received ═══");
    LOG_INFO("[SETTINGS] Message length: %d bytes (expected: %d bytes)", msg.len, sizeof(settings_update_msg_t));
    
    if (msg.len < (int)sizeof(settings_update_msg_t)) {
        LOG_ERROR("[SETTINGS] Invalid message size: %d", msg.len);
        send_settings_ack(msg.mac, 0, 0, false, 0, "Invalid message size");
        return;
    }
    
    const settings_update_msg_t* update = (const settings_update_msg_t*)msg.data;
    
    LOG_INFO("[SETTINGS] From: %02X:%02X:%02X:%02X:%02X:%02X",
             msg.mac[0], msg.mac[1], msg.mac[2], msg.mac[3], msg.mac[4], msg.mac[5]);
    LOG_INFO("[SETTINGS] Type=%d, Category=%d, Field=%d", 
             update->type, update->category, update->field_id);
    LOG_INFO("[SETTINGS] Values - uint32=%u, float=%.2f, string='%s'",
             update->value_uint32, update->value_float, update->value_string);
    LOG_INFO("[SETTINGS] Checksum: %u", update->checksum);
    
    // Verify checksum
    uint8_t calculated_checksum = 0;
    const uint8_t* bytes = (const uint8_t*)update;
    for (size_t i = 0; i < sizeof(settings_update_msg_t) - sizeof(update->checksum); i++) {
        calculated_checksum ^= bytes[i];
    }
    
    if (calculated_checksum != update->checksum) {
        LOG_ERROR("[SETTINGS] Checksum mismatch! Expected=%u, Got=%u", 
                  calculated_checksum, update->checksum);
        send_settings_ack(msg.mac, update->category, update->field_id, false, 0, "Checksum error");
        return;
    }
    LOG_INFO("[SETTINGS] ✓ Checksum valid");
    
    bool success = false;
    char error_msg[48] = "";
    
    // Handle based on category
    switch (update->category) {
        case SETTINGS_BATTERY:
            success = save_battery_setting(update->field_id, update->value_uint32, 
                                          update->value_float, update->value_string);
            if (!success) {
                strcpy(error_msg, "Invalid value or NVS write failed");
            }
            break;
            
        case SETTINGS_CHARGER:
        case SETTINGS_INVERTER:
        case SETTINGS_SYSTEM:
        case SETTINGS_MQTT:
        case SETTINGS_NETWORK:
            LOG_WARN("[SETTINGS] Category %d not yet implemented", update->category);
            strcpy(error_msg, "Category not implemented yet");
            break;
            
        default:
            LOG_ERROR("[SETTINGS] Unknown category: %d", update->category);
            strcpy(error_msg, "Unknown settings category");
            break;
    }
    
    // Send acknowledgment
    send_settings_ack(msg.mac, update->category, update->field_id, success, battery_settings_version_, error_msg);
}

void SettingsManager::send_settings_ack(const uint8_t* mac, uint8_t category, uint8_t field_id, bool success, uint32_t new_version, const char* error_msg) {
    settings_update_ack_msg_t ack;
    ack.type = msg_settings_update_ack;
    ack.category = category;
    ack.field_id = field_id;
    ack.success = success;
    ack.new_version = new_version;
    
    if (error_msg && strlen(error_msg) > 0) {
        strncpy(ack.error_msg, error_msg, sizeof(ack.error_msg) - 1);
        ack.error_msg[sizeof(ack.error_msg) - 1] = '\0';
    } else {
        ack.error_msg[0] = '\0';
    }
    
    ack.checksum = 0; // TODO: Calculate checksum if needed
    
    esp_err_t result = esp_now_send(mac, (const uint8_t*)&ack, sizeof(ack));
    
    if (result == ESP_OK) {
        LOG_INFO("[SETTINGS] ACK sent: success=%d, version=%u", success, new_version);
    } else {
        LOG_WARN("[SETTINGS] Failed to send ACK (will retry if receiver requests): %s", esp_err_to_name(result));
    }
}

void SettingsManager::send_settings_changed_notification(uint8_t category, uint32_t new_version) {
    settings_changed_msg_t notification;
    notification.type = msg_settings_changed;
    notification.category = category;
    notification.new_version = new_version;
    
    // Calculate checksum using common utility
    notification.checksum = EspnowPacketUtils::calculate_message_checksum(&notification);
    
    // Send to receiver using broadcast (receiver MAC may not be known yet)
    extern uint8_t receiver_mac[6];
    esp_err_t result = esp_now_send(receiver_mac, (const uint8_t*)&notification, sizeof(notification));
    
    if (result == ESP_OK) {
        LOG_INFO("[SETTINGS] Sent change notification: category=%d, version=%u", category, new_version);
    } else {
        LOG_DEBUG("[SETTINGS] Notification send failed (receiver may request update): %s", esp_err_to_name(result));
    }
}

void SettingsManager::increment_battery_version() {
    VersionUtils::increment_version(battery_settings_version_);
    LOG_INFO("[SETTINGS] Battery settings version incremented to %u", battery_settings_version_);
}
