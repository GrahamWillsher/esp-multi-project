#include "settings_manager.h"
#include "../config/logging_config.h"
#include "../datalayer/datalayer.h"
#include "../battery_emulator/devboard/utils/led_handler.h"
#include <esp_now.h>
#include <esp32common/espnow/connection_manager.h>
#include <esp32common/espnow/packet_utils.h>

namespace {

constexpr const char* kSettingsBlobKey = "blob_v1";

struct __attribute__((packed)) BatterySettingsBlob {
    uint32_t capacity_wh;
    uint32_t max_voltage_mv;
    uint32_t min_voltage_mv;
    float max_charge_current_a;
    float max_discharge_current_a;
    uint8_t soc_high_limit;
    uint8_t soc_low_limit;
    uint8_t cell_count;
    uint8_t chemistry;
    bool double_enabled;
    uint16_t pack_max_voltage_dv;
    uint16_t pack_min_voltage_dv;
    uint16_t cell_max_voltage_mv;
    uint16_t cell_min_voltage_mv;
    bool soc_estimated;
    uint8_t led_mode;
    uint32_t version;
    uint32_t crc32;
};

struct __attribute__((packed)) PowerSettingsBlob {
    uint16_t charge_w;
    uint16_t discharge_w;
    uint16_t max_precharge_ms;
    uint16_t precharge_duration_ms;
    uint32_t version;
    uint32_t crc32;
};

struct __attribute__((packed)) InverterSettingsBlob {
    uint8_t cells;
    uint8_t modules;
    uint8_t cells_per_module;
    uint16_t voltage_level;
    uint16_t capacity_ah;
    uint8_t battery_type;
    uint32_t version;
    uint32_t crc32;
};

struct __attribute__((packed)) CanSettingsBlob {
    uint16_t frequency_khz;
    uint16_t fd_frequency_mhz;
    uint16_t sofar_id;
    uint16_t pylon_send_interval_ms;
    uint32_t version;
    uint32_t crc32;
};

struct __attribute__((packed)) ContactorSettingsBlob {
    bool control_enabled;
    bool nc_mode;
    uint16_t pwm_frequency_hz;
    uint32_t version;
    uint32_t crc32;
};

template <typename T>
uint32_t calculate_blob_crc32(const T& blob) {
    T copy = blob;
    copy.crc32 = 0;
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&copy);
    for (size_t i = 0; i < sizeof(copy); i++) {
        crc ^= bytes[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc ^ 0xFFFFFFFF;
}

template <typename T>
bool verify_blob_crc32(const T& blob) {
    return calculate_blob_crc32(blob) == blob.crc32;
}

}  // namespace

SettingsManager& SettingsManager::instance() {
    static SettingsManager instance;
    return instance;
}

SettingsManager::SettingsManager() {
}

uint32_t SettingsManager::calculate_crc32(const void* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t* bytes = static_cast<const uint8_t*>(data);

    for (size_t i = 0; i < length; i++) {
        crc ^= bytes[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc ^ 0xFFFFFFFF;
}

SettingsManager::ValidationResult SettingsManager::validate_battery_settings() const {
    if (battery_capacity_wh_ < 1000 || battery_capacity_wh_ > 1000000) {
        ValidationResult result;
        result.is_valid = false;
        result.error_message = "Battery capacity out of range";
        return result;
    }
    if (battery_min_voltage_mv_ >= battery_max_voltage_mv_) {
        ValidationResult result;
        result.is_valid = false;
        result.error_message = "Battery min voltage must be less than max voltage";
        return result;
    }
    if (battery_cell_min_voltage_mV_ >= battery_cell_max_voltage_mV_) {
        ValidationResult result;
        result.is_valid = false;
        result.error_message = "Cell min voltage must be less than cell max voltage";
        return result;
    }
    if (battery_soc_low_limit_ > battery_soc_high_limit_) {
        ValidationResult result;
        result.is_valid = false;
        result.error_message = "SOC low limit must be less than or equal to SOC high limit";
        return result;
    }
    if (battery_led_mode_ > 2) {
        ValidationResult result;
        result.is_valid = false;
        result.error_message = "LED mode out of range";
        return result;
    }
    return ValidationResult{};
}

SettingsManager::ValidationResult SettingsManager::validate_power_settings() const {
    if (power_charge_w_ > 50000 || power_discharge_w_ > 50000) {
        ValidationResult result;
        result.is_valid = false;
        result.error_message = "Power settings out of allowed range";
        return result;
    }
    if (power_precharge_duration_ms_ > power_max_precharge_ms_) {
        ValidationResult result;
        result.is_valid = false;
        result.error_message = "Precharge duration cannot exceed max precharge duration";
        return result;
    }
    return ValidationResult{};
}

SettingsManager::ValidationResult SettingsManager::validate_inverter_settings() const {
    if (inverter_cells_ > 32 || inverter_modules_ > 32 || inverter_cells_per_module_ > 32) {
        ValidationResult result;
        result.is_valid = false;
        result.error_message = "Inverter cell/module settings out of range";
        return result;
    }
    return ValidationResult{};
}

SettingsManager::ValidationResult SettingsManager::validate_can_settings() const {
    if (can_frequency_khz_ == 0 || can_frequency_khz_ > 1000) {
        ValidationResult result;
        result.is_valid = false;
        result.error_message = "CAN frequency out of range";
        return result;
    }
    if (can_fd_frequency_mhz_ == 0 || can_fd_frequency_mhz_ > 100) {
        ValidationResult result;
        result.is_valid = false;
        result.error_message = "CAN-FD frequency out of range";
        return result;
    }
    return ValidationResult{};
}

SettingsManager::ValidationResult SettingsManager::validate_contactor_settings() const {
    if (contactor_pwm_frequency_hz_ < 100 || contactor_pwm_frequency_hz_ > 50000) {
        ValidationResult result;
        result.is_valid = false;
        result.error_message = "Contactor PWM frequency out of range";
        return result;
    }
    return ValidationResult{};
}

SettingsManager::ValidationResult SettingsManager::validate_all_settings() const {
    ValidationResult result = validate_battery_settings();
    if (!result.is_valid) return result;

    result = validate_power_settings();
    if (!result.is_valid) return result;

    result = validate_inverter_settings();
    if (!result.is_valid) return result;

    result = validate_can_settings();
    if (!result.is_valid) return result;

    return validate_contactor_settings();
}

bool SettingsManager::init() {
    if (initialized_) {
        LOG_WARN("SETTINGS", "Already initialized");
        return true;
    }
    
    LOG_INFO("SETTINGS", "Initializing settings manager...");
    
    // Load all settings from NVS
    // Note: load_battery_settings() returns false on first boot (namespace doesn't exist)
    // but still loads default values. We only call restore_defaults() explicitly if needed
    // to initialize the NVS namespace.
    bool loaded = load_all_settings();
    if (!loaded) {
        // First boot - NVS namespace doesn't exist yet
        // Settings already have default values from load function
        // Just save them to NVS to create the namespace
        LOG_INFO("SETTINGS", "First boot - initializing NVS with defaults");
        save_battery_settings();
        save_power_settings();
        save_inverter_settings();
        save_can_settings();
        save_contactor_settings();
    }
    
    initialized_ = true;

    // Keep runtime LED mode aligned with authoritative managed settings.
    datalayer.battery.status.led_mode = static_cast<led_mode_enum>(battery_led_mode_);

    LOG_INFO("SETTINGS", "Settings manager initialized");
    return true;
}

bool SettingsManager::load_all_settings() {
    bool success = true;
    
    // Load battery settings
    if (!load_battery_settings()) {
        LOG_WARN("SETTINGS", "Failed to load battery settings");
        success = false;
    }
    
    if (!load_power_settings()) {
        LOG_WARN("SETTINGS", "Failed to load power settings");
        success = false;
    }

    if (!load_inverter_settings()) {
        LOG_WARN("SETTINGS", "Failed to load inverter settings");
        success = false;
    }

    if (!load_can_settings()) {
        LOG_WARN("SETTINGS", "Failed to load CAN settings");
        success = false;
    }

    if (!load_contactor_settings()) {
        LOG_WARN("SETTINGS", "Failed to load contactor settings");
        success = false;
    }

    last_validation_ = validate_all_settings();
    if (!last_validation_.is_valid) {
        LOG_ERROR("SETTINGS", "Settings validation failed after load: %s", last_validation_.error_message.c_str());
        success = false;
    }
    
    return success;
}

bool SettingsManager::load_battery_settings() {
    Preferences prefs;
    
    if (!prefs.begin("battery", true)) { // Read-only
        LOG_WARN("SETTINGS", "Battery namespace doesn't exist yet (first boot) - will use defaults");
        return false; // Not an error - just means first boot
    }
    
    bool loaded_from_blob = false;
    const size_t blob_size = prefs.getBytesLength(kSettingsBlobKey);
    if (blob_size == sizeof(BatterySettingsBlob)) {
        BatterySettingsBlob blob{};
        prefs.getBytes(kSettingsBlobKey, &blob, sizeof(blob));
        if (verify_blob_crc32(blob)) {
            battery_capacity_wh_ = blob.capacity_wh;
            battery_max_voltage_mv_ = blob.max_voltage_mv;
            battery_min_voltage_mv_ = blob.min_voltage_mv;
            battery_max_charge_current_a_ = blob.max_charge_current_a;
            battery_max_discharge_current_a_ = blob.max_discharge_current_a;
            battery_soc_high_limit_ = blob.soc_high_limit;
            battery_soc_low_limit_ = blob.soc_low_limit;
            battery_cell_count_ = blob.cell_count;
            battery_chemistry_ = blob.chemistry;
            battery_double_enabled_ = blob.double_enabled;
            battery_pack_max_voltage_dV_ = blob.pack_max_voltage_dv;
            battery_pack_min_voltage_dV_ = blob.pack_min_voltage_dv;
            battery_cell_max_voltage_mV_ = blob.cell_max_voltage_mv;
            battery_cell_min_voltage_mV_ = blob.cell_min_voltage_mv;
            battery_soc_estimated_ = blob.soc_estimated;
            battery_led_mode_ = blob.led_mode;
            battery_settings_version_ = blob.version;
            loaded_from_blob = true;
        } else {
            LOG_WARN("SETTINGS", "Battery settings CRC mismatch - falling back to legacy keys");
        }
    }

    if (!loaded_from_blob) {
        // Legacy fallback (pre-CRC storage)
        battery_capacity_wh_ = prefs.getUInt("capacity_wh", 30000);
        battery_max_voltage_mv_ = prefs.getUInt("max_volt_mv", 58000);
        battery_min_voltage_mv_ = prefs.getUInt("min_volt_mv", 46000);
        battery_max_charge_current_a_ = prefs.getFloat("max_chg_a", 100.0f);
        battery_max_discharge_current_a_ = prefs.getFloat("max_dis_a", 100.0f);
        battery_soc_high_limit_ = prefs.getUChar("soc_high", 95);
        battery_soc_low_limit_ = prefs.getUChar("soc_low", 20);
        battery_cell_count_ = prefs.getUChar("cell_count", 16);
        battery_chemistry_ = prefs.getUChar("chemistry", 2); // LFP
        battery_double_enabled_ = prefs.getBool("double_enabled", false);
        battery_pack_max_voltage_dV_ = prefs.getUShort("pack_max_dv", 580);
        battery_pack_min_voltage_dV_ = prefs.getUShort("pack_min_dv", 460);
        battery_cell_max_voltage_mV_ = prefs.getUShort("cell_max_mv", 4200);
        battery_cell_min_voltage_mV_ = prefs.getUShort("cell_min_mv", 3000);
        battery_soc_estimated_ = prefs.getBool("soc_est", false);
        battery_led_mode_ = prefs.getUChar("led_mode", 0);
        battery_settings_version_ = prefs.getUInt("version", 0);
    }
    
    prefs.end();

    last_validation_ = validate_battery_settings();
    if (!last_validation_.is_valid) {
        LOG_ERROR("SETTINGS", "Battery settings validation failed: %s", last_validation_.error_message.c_str());
        return false;
    }
    
    LOG_INFO("SETTINGS", "Battery: %dWh, %dS, %dmV-%dmV, ±%.1fA/%.1fA, SOC:%d%%-%d%%, version:%u",
             battery_capacity_wh_, battery_cell_count_,
             battery_min_voltage_mv_, battery_max_voltage_mv_,
             battery_max_charge_current_a_, battery_max_discharge_current_a_,
             battery_soc_low_limit_, battery_soc_high_limit_, battery_settings_version_);
    
    return true;
}

bool SettingsManager::save_battery_settings() {
    Preferences prefs;

    last_validation_ = validate_battery_settings();
    if (!last_validation_.is_valid) {
        LOG_ERROR("SETTINGS", "Battery settings save aborted: %s", last_validation_.error_message.c_str());
        return false;
    }
    
    if (!prefs.begin("battery", false)) { // Read-write
        LOG_ERROR("SETTINGS", "Failed to open battery namespace for writing");
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
    prefs.putBool("double_enabled", battery_double_enabled_);
    prefs.putUShort("pack_max_dv", battery_pack_max_voltage_dV_);
    prefs.putUShort("pack_min_dv", battery_pack_min_voltage_dV_);
    prefs.putUShort("cell_max_mv", battery_cell_max_voltage_mV_);
    prefs.putUShort("cell_min_mv", battery_cell_min_voltage_mV_);
    prefs.putBool("soc_est", battery_soc_estimated_);
    prefs.putUChar("led_mode", battery_led_mode_);
    prefs.putUInt("version", battery_settings_version_);

    BatterySettingsBlob blob{};
    blob.capacity_wh = battery_capacity_wh_;
    blob.max_voltage_mv = battery_max_voltage_mv_;
    blob.min_voltage_mv = battery_min_voltage_mv_;
    blob.max_charge_current_a = battery_max_charge_current_a_;
    blob.max_discharge_current_a = battery_max_discharge_current_a_;
    blob.soc_high_limit = battery_soc_high_limit_;
    blob.soc_low_limit = battery_soc_low_limit_;
    blob.cell_count = battery_cell_count_;
    blob.chemistry = battery_chemistry_;
    blob.double_enabled = battery_double_enabled_;
    blob.pack_max_voltage_dv = battery_pack_max_voltage_dV_;
    blob.pack_min_voltage_dv = battery_pack_min_voltage_dV_;
    blob.cell_max_voltage_mv = battery_cell_max_voltage_mV_;
    blob.cell_min_voltage_mv = battery_cell_min_voltage_mV_;
    blob.soc_estimated = battery_soc_estimated_;
    blob.led_mode = battery_led_mode_;
    blob.version = battery_settings_version_;
    blob.crc32 = calculate_blob_crc32(blob);
    size_t written = prefs.putBytes(kSettingsBlobKey, &blob, sizeof(blob));
    
    prefs.end();

    if (written != sizeof(blob)) {
        LOG_ERROR("SETTINGS", "Failed to save battery settings blob");
        return false;
    }
    
    LOG_INFO("SETTINGS", "Battery settings saved to NVS (version %u)", battery_settings_version_);
    return true;
}

bool SettingsManager::load_power_settings() {
    Preferences prefs;
    if (!prefs.begin("power", true)) {
        LOG_WARN("SETTINGS", "Power namespace doesn't exist yet (first boot) - will use defaults");
        return false;
    }

    bool loaded_from_blob = false;
    const size_t blob_size = prefs.getBytesLength(kSettingsBlobKey);
    if (blob_size == sizeof(PowerSettingsBlob)) {
        PowerSettingsBlob blob{};
        prefs.getBytes(kSettingsBlobKey, &blob, sizeof(blob));
        if (verify_blob_crc32(blob)) {
            power_charge_w_ = blob.charge_w;
            power_discharge_w_ = blob.discharge_w;
            power_max_precharge_ms_ = blob.max_precharge_ms;
            power_precharge_duration_ms_ = blob.precharge_duration_ms;
            power_settings_version_ = blob.version;
            loaded_from_blob = true;
        } else {
            LOG_WARN("SETTINGS", "Power settings CRC mismatch - falling back to legacy keys");
        }
    }

    if (!loaded_from_blob) {
        power_charge_w_ = prefs.getUShort("charge_w", 3000);
        power_discharge_w_ = prefs.getUShort("discharge_w", 3000);
        power_max_precharge_ms_ = prefs.getUShort("max_precharge_ms", 15000);
        power_precharge_duration_ms_ = prefs.getUShort("precharge_ms", 100);
        power_settings_version_ = prefs.getUInt("version", 0);
    }

    prefs.end();

    last_validation_ = validate_power_settings();
    if (!last_validation_.is_valid) {
        LOG_ERROR("SETTINGS", "Power settings validation failed: %s", last_validation_.error_message.c_str());
        return false;
    }
    return true;
}

bool SettingsManager::save_power_settings() {
    Preferences prefs;

    last_validation_ = validate_power_settings();
    if (!last_validation_.is_valid) {
        LOG_ERROR("SETTINGS", "Power settings save aborted: %s", last_validation_.error_message.c_str());
        return false;
    }

    if (!prefs.begin("power", false)) {
        LOG_ERROR("SETTINGS", "Failed to open power namespace for writing");
        return false;
    }

    prefs.putUShort("charge_w", power_charge_w_);
    prefs.putUShort("discharge_w", power_discharge_w_);
    prefs.putUShort("max_precharge_ms", power_max_precharge_ms_);
    prefs.putUShort("precharge_ms", power_precharge_duration_ms_);
    prefs.putUInt("version", power_settings_version_);

    PowerSettingsBlob blob{};
    blob.charge_w = power_charge_w_;
    blob.discharge_w = power_discharge_w_;
    blob.max_precharge_ms = power_max_precharge_ms_;
    blob.precharge_duration_ms = power_precharge_duration_ms_;
    blob.version = power_settings_version_;
    blob.crc32 = calculate_blob_crc32(blob);
    size_t written = prefs.putBytes(kSettingsBlobKey, &blob, sizeof(blob));
    prefs.end();

    return written == sizeof(blob);
}

bool SettingsManager::load_inverter_settings() {
    Preferences prefs;
    if (!prefs.begin("inverter", true)) {
        LOG_WARN("SETTINGS", "Inverter namespace doesn't exist yet (first boot) - will use defaults");
        return false;
    }

    bool loaded_from_blob = false;
    const size_t blob_size = prefs.getBytesLength(kSettingsBlobKey);
    if (blob_size == sizeof(InverterSettingsBlob)) {
        InverterSettingsBlob blob{};
        prefs.getBytes(kSettingsBlobKey, &blob, sizeof(blob));
        if (verify_blob_crc32(blob)) {
            inverter_cells_ = blob.cells;
            inverter_modules_ = blob.modules;
            inverter_cells_per_module_ = blob.cells_per_module;
            inverter_voltage_level_ = blob.voltage_level;
            inverter_capacity_ah_ = blob.capacity_ah;
            inverter_battery_type_ = blob.battery_type;
            inverter_settings_version_ = blob.version;
            loaded_from_blob = true;
        } else {
            LOG_WARN("SETTINGS", "Inverter settings CRC mismatch - falling back to legacy keys");
        }
    }

    if (!loaded_from_blob) {
        inverter_cells_ = prefs.getUChar("cells", 0);
        inverter_modules_ = prefs.getUChar("modules", 0);
        inverter_cells_per_module_ = prefs.getUChar("cells_per_module", 0);
        inverter_voltage_level_ = prefs.getUShort("voltage_level", 0);
        inverter_capacity_ah_ = prefs.getUShort("capacity_ah", 0);
        inverter_battery_type_ = prefs.getUChar("battery_type", 0);
        inverter_settings_version_ = prefs.getUInt("version", 0);
    }

    prefs.end();

    last_validation_ = validate_inverter_settings();
    if (!last_validation_.is_valid) {
        LOG_ERROR("SETTINGS", "Inverter settings validation failed: %s", last_validation_.error_message.c_str());
        return false;
    }
    return true;
}

bool SettingsManager::save_inverter_settings() {
    Preferences prefs;

    last_validation_ = validate_inverter_settings();
    if (!last_validation_.is_valid) {
        LOG_ERROR("SETTINGS", "Inverter settings save aborted: %s", last_validation_.error_message.c_str());
        return false;
    }

    if (!prefs.begin("inverter", false)) {
        LOG_ERROR("SETTINGS", "Failed to open inverter namespace for writing");
        return false;
    }

    prefs.putUChar("cells", inverter_cells_);
    prefs.putUChar("modules", inverter_modules_);
    prefs.putUChar("cells_per_module", inverter_cells_per_module_);
    prefs.putUShort("voltage_level", inverter_voltage_level_);
    prefs.putUShort("capacity_ah", inverter_capacity_ah_);
    prefs.putUChar("battery_type", inverter_battery_type_);
    prefs.putUInt("version", inverter_settings_version_);

    InverterSettingsBlob blob{};
    blob.cells = inverter_cells_;
    blob.modules = inverter_modules_;
    blob.cells_per_module = inverter_cells_per_module_;
    blob.voltage_level = inverter_voltage_level_;
    blob.capacity_ah = inverter_capacity_ah_;
    blob.battery_type = inverter_battery_type_;
    blob.version = inverter_settings_version_;
    blob.crc32 = calculate_blob_crc32(blob);
    size_t written = prefs.putBytes(kSettingsBlobKey, &blob, sizeof(blob));
    prefs.end();
    return written == sizeof(blob);
}

bool SettingsManager::load_can_settings() {
    Preferences prefs;
    if (!prefs.begin("can", true)) {
        LOG_WARN("SETTINGS", "CAN namespace doesn't exist yet (first boot) - will use defaults");
        return false;
    }

    bool loaded_from_blob = false;
    const size_t blob_size = prefs.getBytesLength(kSettingsBlobKey);
    if (blob_size == sizeof(CanSettingsBlob)) {
        CanSettingsBlob blob{};
        prefs.getBytes(kSettingsBlobKey, &blob, sizeof(blob));
        if (verify_blob_crc32(blob)) {
            can_frequency_khz_ = blob.frequency_khz;
            can_fd_frequency_mhz_ = blob.fd_frequency_mhz;
            can_sofar_id_ = blob.sofar_id;
            can_pylon_send_interval_ms_ = blob.pylon_send_interval_ms;
            can_settings_version_ = blob.version;
            loaded_from_blob = true;
        } else {
            LOG_WARN("SETTINGS", "CAN settings CRC mismatch - falling back to legacy keys");
        }
    }

    if (!loaded_from_blob) {
        can_frequency_khz_ = prefs.getUShort("freq_khz", 8);
        can_fd_frequency_mhz_ = prefs.getUShort("fd_freq_mhz", 40);
        can_sofar_id_ = prefs.getUShort("sofar_id", 0);
        can_pylon_send_interval_ms_ = prefs.getUShort("pylon_send_ms", 0);
        can_settings_version_ = prefs.getUInt("version", 0);
    }

    prefs.end();

    last_validation_ = validate_can_settings();
    if (!last_validation_.is_valid) {
        LOG_ERROR("SETTINGS", "CAN settings validation failed: %s", last_validation_.error_message.c_str());
        return false;
    }
    return true;
}

bool SettingsManager::save_can_settings() {
    Preferences prefs;

    last_validation_ = validate_can_settings();
    if (!last_validation_.is_valid) {
        LOG_ERROR("SETTINGS", "CAN settings save aborted: %s", last_validation_.error_message.c_str());
        return false;
    }

    if (!prefs.begin("can", false)) {
        LOG_ERROR("SETTINGS", "Failed to open CAN namespace for writing");
        return false;
    }

    prefs.putUShort("freq_khz", can_frequency_khz_);
    prefs.putUShort("fd_freq_mhz", can_fd_frequency_mhz_);
    prefs.putUShort("sofar_id", can_sofar_id_);
    prefs.putUShort("pylon_send_ms", can_pylon_send_interval_ms_);
    prefs.putUInt("version", can_settings_version_);

    CanSettingsBlob blob{};
    blob.frequency_khz = can_frequency_khz_;
    blob.fd_frequency_mhz = can_fd_frequency_mhz_;
    blob.sofar_id = can_sofar_id_;
    blob.pylon_send_interval_ms = can_pylon_send_interval_ms_;
    blob.version = can_settings_version_;
    blob.crc32 = calculate_blob_crc32(blob);
    size_t written = prefs.putBytes(kSettingsBlobKey, &blob, sizeof(blob));
    prefs.end();
    return written == sizeof(blob);
}

bool SettingsManager::load_contactor_settings() {
    Preferences prefs;
    if (!prefs.begin("contactor", true)) {
        LOG_WARN("SETTINGS", "Contactor namespace doesn't exist yet (first boot) - will use defaults");
        return false;
    }

    bool loaded_from_blob = false;
    const size_t blob_size = prefs.getBytesLength(kSettingsBlobKey);
    if (blob_size == sizeof(ContactorSettingsBlob)) {
        ContactorSettingsBlob blob{};
        prefs.getBytes(kSettingsBlobKey, &blob, sizeof(blob));
        if (verify_blob_crc32(blob)) {
            contactor_control_enabled_ = blob.control_enabled;
            contactor_nc_mode_ = blob.nc_mode;
            contactor_pwm_frequency_hz_ = blob.pwm_frequency_hz;
            contactor_settings_version_ = blob.version;
            loaded_from_blob = true;
        } else {
            LOG_WARN("SETTINGS", "Contactor settings CRC mismatch - falling back to legacy keys");
        }
    }

    if (!loaded_from_blob) {
        contactor_control_enabled_ = prefs.getBool("control_enabled", false);
        contactor_nc_mode_ = prefs.getBool("nc_mode", false);
        contactor_pwm_frequency_hz_ = prefs.getUShort("pwm_hz", 20000);
        contactor_settings_version_ = prefs.getUInt("version", 0);
    }

    prefs.end();

    last_validation_ = validate_contactor_settings();
    if (!last_validation_.is_valid) {
        LOG_ERROR("SETTINGS", "Contactor settings validation failed: %s", last_validation_.error_message.c_str());
        return false;
    }
    return true;
}

bool SettingsManager::save_contactor_settings() {
    Preferences prefs;

    last_validation_ = validate_contactor_settings();
    if (!last_validation_.is_valid) {
        LOG_ERROR("SETTINGS", "Contactor settings save aborted: %s", last_validation_.error_message.c_str());
        return false;
    }

    if (!prefs.begin("contactor", false)) {
        LOG_ERROR("SETTINGS", "Failed to open contactor namespace for writing");
        return false;
    }

    prefs.putBool("control_enabled", contactor_control_enabled_);
    prefs.putBool("nc_mode", contactor_nc_mode_);
    prefs.putUShort("pwm_hz", contactor_pwm_frequency_hz_);
    prefs.putUInt("version", contactor_settings_version_);

    ContactorSettingsBlob blob{};
    blob.control_enabled = contactor_control_enabled_;
    blob.nc_mode = contactor_nc_mode_;
    blob.pwm_frequency_hz = contactor_pwm_frequency_hz_;
    blob.version = contactor_settings_version_;
    blob.crc32 = calculate_blob_crc32(blob);
    size_t written = prefs.putBytes(kSettingsBlobKey, &blob, sizeof(blob));
    prefs.end();
    return written == sizeof(blob);
}

void SettingsManager::increment_power_version() {
    VersionUtils::increment_version(power_settings_version_);
}

void SettingsManager::increment_inverter_version() {
    VersionUtils::increment_version(inverter_settings_version_);
}

void SettingsManager::increment_can_version() {
    VersionUtils::increment_version(can_settings_version_);
}

void SettingsManager::increment_contactor_version() {
    VersionUtils::increment_version(contactor_settings_version_);
}

void SettingsManager::restore_defaults() {
    LOG_INFO("SETTINGS", "Restoring factory defaults...");
    
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
    battery_double_enabled_ = false;
    battery_pack_max_voltage_dV_ = 580;
    battery_pack_min_voltage_dV_ = 460;
    battery_cell_max_voltage_mV_ = 4200;
    battery_cell_min_voltage_mV_ = 3000;
    battery_soc_estimated_ = false;
    battery_led_mode_ = 0;
    datalayer.battery.status.led_mode = static_cast<led_mode_enum>(battery_led_mode_);
    
    // Save defaults to NVS
    save_battery_settings();
    
    LOG_INFO("SETTINGS", "Factory defaults restored");
}

bool SettingsManager::save_battery_setting(uint8_t field_id, uint32_t value_uint32, float value_float, const char* value_string) {
    bool changed = false;
    
    switch (field_id) {
        case BATTERY_CAPACITY_WH:
            if (value_uint32 >= 1000 && value_uint32 <= 1000000) { // 1kWh - 1MWh range check
                battery_capacity_wh_ = value_uint32;
                changed = true;
                LOG_INFO("SETTINGS", "Battery capacity updated: %dWh", battery_capacity_wh_);
            } else {
                LOG_ERROR("SETTINGS", "Invalid capacity: %d (must be 1000-1000000)", value_uint32);
                return false;
            }
            break;
            
        case BATTERY_MAX_VOLTAGE_MV:
            if (value_uint32 >= 30000 && value_uint32 <= 100000) { // 30V - 100V range check
                battery_max_voltage_mv_ = value_uint32;
                changed = true;
                LOG_INFO("SETTINGS", "Max voltage updated: %dmV", battery_max_voltage_mv_);
            } else {
                LOG_ERROR("SETTINGS", "Invalid max voltage: %d", value_uint32);
                return false;
            }
            break;
            
        case BATTERY_MIN_VOLTAGE_MV:
            if (value_uint32 >= 20000 && value_uint32 <= 80000) { // 20V - 80V range check
                battery_min_voltage_mv_ = value_uint32;
                changed = true;
                LOG_INFO("SETTINGS", "Min voltage updated: %dmV", battery_min_voltage_mv_);
            } else {
                LOG_ERROR("SETTINGS", "Invalid min voltage: %d", value_uint32);
                return false;
            }
            break;
            
        case BATTERY_MAX_CHARGE_CURRENT_A:
            if (value_float >= 0.0f && value_float <= 500.0f) { // 0-500A range check
                battery_max_charge_current_a_ = value_float;
                changed = true;
                LOG_INFO("SETTINGS", "Max charge current updated: %.1fA", battery_max_charge_current_a_);
            } else {
                LOG_ERROR("SETTINGS", "Invalid charge current: %.1f", value_float);
                return false;
            }
            break;
            
        case BATTERY_MAX_DISCHARGE_CURRENT_A:
            if (value_float >= 0.0f && value_float <= 500.0f) { // 0-500A range check
                battery_max_discharge_current_a_ = value_float;
                changed = true;
                LOG_INFO("SETTINGS", "Max discharge current updated: %.1fA", battery_max_discharge_current_a_);
            } else {
                LOG_ERROR("SETTINGS", "Invalid discharge current: %.1f", value_float);
                return false;
            }
            break;
            
        case BATTERY_SOC_HIGH_LIMIT:
            if (value_uint32 >= 50 && value_uint32 <= 100) { // 50-100% range check
                battery_soc_high_limit_ = value_uint32;
                changed = true;
                LOG_INFO("SETTINGS", "SOC high limit updated: %d%%", battery_soc_high_limit_);
            } else {
                LOG_ERROR("SETTINGS", "Invalid SOC high: %d", value_uint32);
                return false;
            }
            break;
            
        case BATTERY_SOC_LOW_LIMIT:
            if (value_uint32 >= 0 && value_uint32 <= 50) { // 0-50% range check
                battery_soc_low_limit_ = value_uint32;
                changed = true;
                LOG_INFO("SETTINGS", "SOC low limit updated: %d%%", battery_soc_low_limit_);
            } else {
                LOG_ERROR("SETTINGS", "Invalid SOC low: %d", value_uint32);
                return false;
            }
            break;
            
        case BATTERY_CELL_COUNT:
            if (value_uint32 >= 4 && value_uint32 <= 32) { // 4-32 cells (12V-100V nominal)
                battery_cell_count_ = value_uint32;
                changed = true;
                LOG_INFO("SETTINGS", "Cell count updated: %dS", battery_cell_count_);
            } else {
                LOG_ERROR("SETTINGS", "Invalid cell count: %d", value_uint32);
                return false;
            }
            break;
            
        case BATTERY_CHEMISTRY:
            if (value_uint32 <= 3) { // 0=NCA, 1=NMC, 2=LFP, 3=LTO
                battery_chemistry_ = value_uint32;
                changed = true;
                const char* chem[] = {"NCA", "NMC", "LFP", "LTO"};
                LOG_INFO("SETTINGS", "Chemistry updated: %s", chem[battery_chemistry_]);
            } else {
                LOG_ERROR("SETTINGS", "Invalid chemistry: %d", value_uint32);
                return false;
            }
            break;

        case BATTERY_DOUBLE_ENABLED:
            battery_double_enabled_ = value_uint32 ? true : false;
            changed = true;
            LOG_INFO("SETTINGS", "Double battery updated: %s", battery_double_enabled_ ? "ENABLED" : "DISABLED");
            break;

        case BATTERY_PACK_MAX_VOLTAGE_DV:
            if (value_uint32 >= 100 && value_uint32 <= 10000) { // 10.0V - 1000.0V
                battery_pack_max_voltage_dV_ = value_uint32;
                changed = true;
                LOG_INFO("SETTINGS", "Pack max voltage updated: %u dV", battery_pack_max_voltage_dV_);
            } else {
                LOG_ERROR("SETTINGS", "Invalid pack max voltage: %d", value_uint32);
                return false;
            }
            break;

        case BATTERY_PACK_MIN_VOLTAGE_DV:
            if (value_uint32 >= 100 && value_uint32 <= 10000) { // 10.0V - 1000.0V
                battery_pack_min_voltage_dV_ = value_uint32;
                changed = true;
                LOG_INFO("SETTINGS", "Pack min voltage updated: %u dV", battery_pack_min_voltage_dV_);
            } else {
                LOG_ERROR("SETTINGS", "Invalid pack min voltage: %d", value_uint32);
                return false;
            }
            break;

        case BATTERY_CELL_MAX_VOLTAGE_MV:
            if (value_uint32 >= 1500 && value_uint32 <= 5000) {
                battery_cell_max_voltage_mV_ = value_uint32;
                changed = true;
                LOG_INFO("SETTINGS", "Cell max voltage updated: %u mV", battery_cell_max_voltage_mV_);
            } else {
                LOG_ERROR("SETTINGS", "Invalid cell max voltage: %d", value_uint32);
                return false;
            }
            break;

        case BATTERY_CELL_MIN_VOLTAGE_MV:
            if (value_uint32 >= 1000 && value_uint32 <= 4500) {
                battery_cell_min_voltage_mV_ = value_uint32;
                changed = true;
                LOG_INFO("SETTINGS", "Cell min voltage updated: %u mV", battery_cell_min_voltage_mV_);
            } else {
                LOG_ERROR("SETTINGS", "Invalid cell min voltage: %d", value_uint32);
                return false;
            }
            break;

        case BATTERY_SOC_ESTIMATED:
            battery_soc_estimated_ = value_uint32 ? true : false;
            changed = true;
            LOG_INFO("SETTINGS", "SOC estimation updated: %s", battery_soc_estimated_ ? "ENABLED" : "DISABLED");
            break;

        case BATTERY_LED_MODE:
            if (value_uint32 <= 2) { // 0=Classic, 1=Energy Flow, 2=Heartbeat
                battery_led_mode_ = value_uint32;
                datalayer.battery.status.led_mode = static_cast<led_mode_enum>(battery_led_mode_);
                changed = true;
                LOG_INFO("SETTINGS", "LED mode updated: %u", battery_led_mode_);
            } else {
                LOG_ERROR("SETTINGS", "Invalid LED mode: %d", value_uint32);
                return false;
            }
            break;
            
        default:
            LOG_ERROR("SETTINGS", "Unknown battery field ID: %d", field_id);
            return false;
    }
    
    // Save to NVS if changed
    if (changed) {
        increment_battery_version();
        bool saved = save_battery_settings();
        if (saved) {
            // Send change notification to receiver
            send_settings_changed_notification(SETTINGS_BATTERY, battery_settings_version_);

            // Apply LED policy changes immediately at runtime (if connected).
            if (field_id == BATTERY_LED_MODE) {
                const esp_err_t led_result = led_publish_current_state(true, nullptr);
                if (led_result != ESP_OK && led_result != ESP_ERR_INVALID_STATE && led_result != ESP_ERR_INVALID_ARG) {
                    LOG_WARN("SETTINGS", "LED replay after LEDMODE update failed: %s", esp_err_to_name(led_result));
                }
            }
        }
        return saved;
    }
    
    return true;
}

bool SettingsManager::save_power_setting(uint8_t field_id, uint32_t value_uint32) {
    bool changed = false;

    switch (field_id) {
        case POWER_CHARGE_W:
            power_charge_w_ = value_uint32;
            changed = true;
            break;
        case POWER_DISCHARGE_W:
            power_discharge_w_ = value_uint32;
            changed = true;
            break;
        case POWER_MAX_PRECHARGE_MS:
            power_max_precharge_ms_ = value_uint32;
            changed = true;
            break;
        case POWER_PRECHARGE_DURATION_MS:
            power_precharge_duration_ms_ = value_uint32;
            changed = true;
            break;
        default:
            LOG_ERROR("SETTINGS", "Unknown power field ID: %d", field_id);
            return false;
    }

    if (changed) {
        increment_power_version();
        bool saved = save_power_settings();
        if (saved) {
            send_settings_changed_notification(SETTINGS_POWER, power_settings_version_);
        }
        return saved;
    }

    return true;
}

bool SettingsManager::save_inverter_setting(uint8_t field_id, uint32_t value_uint32) {
    bool changed = false;

    switch (field_id) {
        case INVERTER_CELLS:
            inverter_cells_ = value_uint32;
            changed = true;
            break;
        case INVERTER_MODULES:
            inverter_modules_ = value_uint32;
            changed = true;
            break;
        case INVERTER_CELLS_PER_MODULE:
            inverter_cells_per_module_ = value_uint32;
            changed = true;
            break;
        case INVERTER_VOLTAGE_LEVEL:
            inverter_voltage_level_ = value_uint32;
            changed = true;
            break;
        case INVERTER_CAPACITY_AH:
            inverter_capacity_ah_ = value_uint32;
            changed = true;
            break;
        case INVERTER_BATTERY_TYPE:
            inverter_battery_type_ = value_uint32;
            changed = true;
            break;
        default:
            LOG_ERROR("SETTINGS", "Unknown inverter field ID: %d", field_id);
            return false;
    }

    if (changed) {
        increment_inverter_version();
        bool saved = save_inverter_settings();
        if (saved) {
            send_settings_changed_notification(SETTINGS_INVERTER, inverter_settings_version_);
        }
        return saved;
    }

    return true;
}

bool SettingsManager::save_can_setting(uint8_t field_id, uint32_t value_uint32) {
    bool changed = false;

    switch (field_id) {
        case CAN_FREQUENCY_KHZ:
            can_frequency_khz_ = value_uint32;
            changed = true;
            break;
        case CAN_FD_FREQUENCY_MHZ:
            can_fd_frequency_mhz_ = value_uint32;
            changed = true;
            break;
        case CAN_SOFAR_ID:
            can_sofar_id_ = value_uint32;
            changed = true;
            break;
        case CAN_PYLON_SEND_INTERVAL_MS:
            can_pylon_send_interval_ms_ = value_uint32;
            changed = true;
            break;
        default:
            LOG_ERROR("SETTINGS", "Unknown CAN field ID: %d", field_id);
            return false;
    }

    if (changed) {
        increment_can_version();
        bool saved = save_can_settings();
        if (saved) {
            send_settings_changed_notification(SETTINGS_CAN, can_settings_version_);
        }
        return saved;
    }

    return true;
}

bool SettingsManager::save_contactor_setting(uint8_t field_id, uint32_t value_uint32) {
    bool changed = false;

    switch (field_id) {
        case CONTACTOR_CONTROL_ENABLED:
            contactor_control_enabled_ = value_uint32 ? true : false;
            changed = true;
            break;
        case CONTACTOR_NC_MODE:
            contactor_nc_mode_ = value_uint32 ? true : false;
            changed = true;
            break;
        case CONTACTOR_PWM_FREQUENCY_HZ:
            contactor_pwm_frequency_hz_ = value_uint32;
            changed = true;
            break;
        default:
            LOG_ERROR("SETTINGS", "Unknown contactor field ID: %d", field_id);
            return false;
    }

    if (changed) {
        increment_contactor_version();
        bool saved = save_contactor_settings();
        if (saved) {
            send_settings_changed_notification(SETTINGS_CONTACTOR, contactor_settings_version_);
        }
        return saved;
    }

    return true;
}

void SettingsManager::handle_settings_update(const espnow_queue_msg_t& msg) {
    LOG_INFO("SETTINGS", "═══ Settings Update Message Received ═══");
    LOG_INFO("SETTINGS", "Message length: %d bytes (expected: %d bytes)", msg.len, sizeof(settings_update_msg_t));
    
    if (msg.len < (int)sizeof(settings_update_msg_t)) {
        LOG_ERROR("SETTINGS", "Invalid message size: %d", msg.len);
        send_settings_ack(msg.mac, 0, 0, false, 0, "Invalid message size");
        return;
    }
    
    const settings_update_msg_t* update = (const settings_update_msg_t*)msg.data;
    
    LOG_INFO("SETTINGS", "From: %02X:%02X:%02X:%02X:%02X:%02X",
             msg.mac[0], msg.mac[1], msg.mac[2], msg.mac[3], msg.mac[4], msg.mac[5]);
    LOG_INFO("SETTINGS", "Type=%d, Category=%d, Field=%d", 
             update->type, update->category, update->field_id);
    LOG_INFO("SETTINGS", "Values - uint32=%u, float=%.2f, string='%s'",
             update->value_uint32, update->value_float, update->value_string);
    LOG_INFO("SETTINGS", "Checksum: %u", update->checksum);
    
    // Verify checksum
    uint8_t calculated_checksum = 0;
    const uint8_t* bytes = (const uint8_t*)update;
    for (size_t i = 0; i < sizeof(settings_update_msg_t) - sizeof(update->checksum); i++) {
        calculated_checksum ^= bytes[i];
    }
    
    if (calculated_checksum != update->checksum) {
        LOG_ERROR("SETTINGS", "Checksum mismatch! Expected=%u, Got=%u", 
                  calculated_checksum, update->checksum);
        send_settings_ack(msg.mac, update->category, update->field_id, false, 0, "Checksum error");
        return;
    }
    LOG_INFO("SETTINGS", "✓ Checksum valid");
    
    bool success = false;
    char error_msg[48] = "";
    uint32_t new_version = 0;
    
    // Handle based on category
    switch (update->category) {
        case SETTINGS_BATTERY:
            success = save_battery_setting(update->field_id, update->value_uint32, 
                                          update->value_float, update->value_string);
            new_version = battery_settings_version_;
            if (!success) {
                strcpy(error_msg, "Invalid value or NVS write failed");
            }
            break;

        case SETTINGS_POWER:
            success = save_power_setting(update->field_id, update->value_uint32);
            new_version = power_settings_version_;
            if (!success) {
                strcpy(error_msg, "Invalid value or NVS write failed");
            }
            break;

        case SETTINGS_INVERTER:
            success = save_inverter_setting(update->field_id, update->value_uint32);
            new_version = inverter_settings_version_;
            if (!success) {
                strcpy(error_msg, "Invalid value or NVS write failed");
            }
            break;

        case SETTINGS_CAN:
            success = save_can_setting(update->field_id, update->value_uint32);
            new_version = can_settings_version_;
            if (!success) {
                strcpy(error_msg, "Invalid value or NVS write failed");
            }
            break;

        case SETTINGS_CONTACTOR:
            success = save_contactor_setting(update->field_id, update->value_uint32);
            new_version = contactor_settings_version_;
            if (!success) {
                strcpy(error_msg, "Invalid value or NVS write failed");
            }
            break;

        case SETTINGS_CHARGER:
        case SETTINGS_SYSTEM:
        case SETTINGS_MQTT:
        case SETTINGS_NETWORK:
            LOG_WARN("SETTINGS", "Category %d not yet implemented", update->category);
            strcpy(error_msg, "Category not implemented yet");
            break;
            
        default:
            LOG_ERROR("SETTINGS", "Unknown category: %d", update->category);
            strcpy(error_msg, "Unknown settings category");
            break;
    }
    
    // Send acknowledgment
    send_settings_ack(msg.mac, update->category, update->field_id, success, new_version, error_msg);
}

void SettingsManager::send_settings_ack(const uint8_t* mac, uint8_t category, uint8_t field_id, bool success, uint32_t new_version, const char* error_msg) {
    settings_update_ack_msg_t ack{};
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
    
    ack.checksum = EspnowPacketUtils::calculate_message_checksum(&ack);
    
    esp_err_t result = esp_now_send(mac, (const uint8_t*)&ack, sizeof(ack));
    
    if (result == ESP_OK) {
        LOG_INFO("SETTINGS", "ACK sent: success=%d, version=%u", success, new_version);
    } else {
        LOG_WARN("SETTINGS", "Failed to send ACK (will retry if receiver requests): %s", esp_err_to_name(result));
    }
}

void SettingsManager::send_settings_changed_notification(uint8_t category, uint32_t new_version) {
    settings_changed_msg_t notification;
    notification.type = msg_settings_changed;
    notification.category = category;
    notification.new_version = new_version;
    
    // Calculate checksum using common utility
    notification.checksum = EspnowPacketUtils::calculate_message_checksum(&notification);
    
    // Send to receiver if connected
    const uint8_t* peer_mac = EspNowConnectionManager::instance().get_peer_mac();
    esp_err_t result = esp_now_send(peer_mac, (const uint8_t*)&notification, sizeof(notification));
    
    if (result == ESP_OK) {
        LOG_INFO("SETTINGS", "Sent change notification: category=%d, version=%u", category, new_version);
    } else {
        LOG_DEBUG("SETTINGS", "Notification send failed (receiver may request update): %s", esp_err_to_name(result));
    }
}

void SettingsManager::increment_battery_version() {
    VersionUtils::increment_version(battery_settings_version_);
    LOG_INFO("SETTINGS", "Battery settings version incremented to %u", battery_settings_version_);
}
