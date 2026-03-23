// settings_persistence.cpp
// Implements all NVS blob save/load member functions for SettingsManager.
// Extracted from settings_manager.cpp to keep that file focused on
// validation, initialisation, field-setter dispatch, and version tracking.

#include "settings_manager.h"
#include "../config/logging_config.h"
#include <Preferences.h>
#include <esp32common/espnow/packet_utils.h>

// ---------------------------------------------------------------------------
// NVS blob schema definitions (private to this TU)
// ---------------------------------------------------------------------------
namespace {

constexpr const char* kSettingsBlobKey = "blob_v1";

struct __attribute__((packed)) BatterySettingsBlob {
    uint32_t capacity_wh;
    uint32_t max_voltage_mv;
    uint32_t min_voltage_mv;
    float    max_charge_current_a;
    float    max_discharge_current_a;
    uint8_t  soc_high_limit;
    uint8_t  soc_low_limit;
    uint8_t  cell_count;
    uint8_t  chemistry;
    bool     double_enabled;
    uint16_t pack_max_voltage_dv;
    uint16_t pack_min_voltage_dv;
    uint16_t cell_max_voltage_mv;
    uint16_t cell_min_voltage_mv;
    bool     soc_estimated;
    uint8_t  led_mode;
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
    uint8_t  cells;
    uint8_t  modules;
    uint8_t  cells_per_module;
    uint16_t voltage_level;
    uint16_t capacity_ah;
    uint8_t  battery_type;
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
    bool     control_enabled;
    bool     nc_mode;
    uint16_t pwm_frequency_hz;
    uint32_t version;
    uint32_t crc32;
};

}  // namespace

// ---------------------------------------------------------------------------
// Battery
// ---------------------------------------------------------------------------

bool SettingsManager::load_battery_settings() {
    Preferences prefs;

    if (!prefs.begin("battery", true)) {  // Read-only
        LOG_WARN("SETTINGS",
                 "Battery namespace doesn't exist yet (first boot) - will use defaults");
        return false;
    }

    bool loaded_from_blob = false;
    const size_t blob_size = prefs.getBytesLength(kSettingsBlobKey);
    if (blob_size == sizeof(BatterySettingsBlob)) {
        BatterySettingsBlob blob{};
        prefs.getBytes(kSettingsBlobKey, &blob, sizeof(blob));
        if (EspnowPacketUtils::verify_message_crc32(&blob)) {
            battery_capacity_wh_              = blob.capacity_wh;
            battery_max_voltage_mv_           = blob.max_voltage_mv;
            battery_min_voltage_mv_           = blob.min_voltage_mv;
            battery_max_charge_current_a_     = blob.max_charge_current_a;
            battery_max_discharge_current_a_  = blob.max_discharge_current_a;
            battery_soc_high_limit_           = blob.soc_high_limit;
            battery_soc_low_limit_            = blob.soc_low_limit;
            battery_cell_count_               = blob.cell_count;
            battery_chemistry_                = blob.chemistry;
            battery_double_enabled_           = blob.double_enabled;
            battery_pack_max_voltage_dV_      = blob.pack_max_voltage_dv;
            battery_pack_min_voltage_dV_      = blob.pack_min_voltage_dv;
            battery_cell_max_voltage_mV_      = blob.cell_max_voltage_mv;
            battery_cell_min_voltage_mV_      = blob.cell_min_voltage_mv;
            battery_soc_estimated_            = blob.soc_estimated;
            battery_led_mode_                 = blob.led_mode;
            battery_settings_version_         = blob.version;
            loaded_from_blob = true;
        } else {
            LOG_WARN("SETTINGS",
                     "Battery settings CRC mismatch - falling back to legacy keys");
        }
    }

    if (!loaded_from_blob) {
        // Legacy fallback (pre-CRC storage)
        battery_capacity_wh_              = prefs.getUInt("capacity_wh", 30000);
        battery_max_voltage_mv_           = prefs.getUInt("max_volt_mv", 58000);
        battery_min_voltage_mv_           = prefs.getUInt("min_volt_mv", 46000);
        battery_max_charge_current_a_     = prefs.getFloat("max_chg_a", 100.0f);
        battery_max_discharge_current_a_  = prefs.getFloat("max_dis_a", 100.0f);
        battery_soc_high_limit_           = prefs.getUChar("soc_high", 95);
        battery_soc_low_limit_            = prefs.getUChar("soc_low", 20);
        battery_cell_count_               = prefs.getUChar("cell_count", 16);
        battery_chemistry_                = prefs.getUChar("chemistry", 2);  // LFP
        battery_double_enabled_           = prefs.getBool("double_enabled", false);
        battery_pack_max_voltage_dV_      = prefs.getUShort("pack_max_dv", 580);
        battery_pack_min_voltage_dV_      = prefs.getUShort("pack_min_dv", 460);
        battery_cell_max_voltage_mV_      = prefs.getUShort("cell_max_mv", 4200);
        battery_cell_min_voltage_mV_      = prefs.getUShort("cell_min_mv", 3000);
        battery_soc_estimated_            = prefs.getBool("soc_est", false);
        battery_led_mode_                 = prefs.getUChar("led_mode", 0);
        battery_settings_version_         = prefs.getUInt("version", 0);
    }

    prefs.end();

    last_validation_ = validate_battery_settings();
    if (!last_validation_.is_valid) {
        LOG_ERROR("SETTINGS", "Battery settings validation failed: %s",
                  last_validation_.error_message.c_str());
        return false;
    }

    LOG_INFO("SETTINGS",
             "Battery: %dWh, %dS, %dmV-%dmV, ±%.1fA/%.1fA, SOC:%d%%-%d%%, version:%u",
             battery_capacity_wh_, battery_cell_count_,
             battery_min_voltage_mv_, battery_max_voltage_mv_,
             battery_max_charge_current_a_, battery_max_discharge_current_a_,
             battery_soc_low_limit_, battery_soc_high_limit_,
             battery_settings_version_);

    return true;
}

bool SettingsManager::save_battery_settings() {
    Preferences prefs;

    last_validation_ = validate_battery_settings();
    if (!last_validation_.is_valid) {
        LOG_ERROR("SETTINGS", "Battery settings save aborted: %s",
                  last_validation_.error_message.c_str());
        return false;
    }

    if (!prefs.begin("battery", false)) {  // Read-write
        LOG_ERROR("SETTINGS", "Failed to open battery namespace for writing");
        return false;
    }

    prefs.putUInt("capacity_wh",   battery_capacity_wh_);
    prefs.putUInt("max_volt_mv",   battery_max_voltage_mv_);
    prefs.putUInt("min_volt_mv",   battery_min_voltage_mv_);
    prefs.putFloat("max_chg_a",    battery_max_charge_current_a_);
    prefs.putFloat("max_dis_a",    battery_max_discharge_current_a_);
    prefs.putUChar("soc_high",     battery_soc_high_limit_);
    prefs.putUChar("soc_low",      battery_soc_low_limit_);
    prefs.putUChar("cell_count",   battery_cell_count_);
    prefs.putUChar("chemistry",    battery_chemistry_);
    prefs.putBool("double_enabled", battery_double_enabled_);
    prefs.putUShort("pack_max_dv", battery_pack_max_voltage_dV_);
    prefs.putUShort("pack_min_dv", battery_pack_min_voltage_dV_);
    prefs.putUShort("cell_max_mv", battery_cell_max_voltage_mV_);
    prefs.putUShort("cell_min_mv", battery_cell_min_voltage_mV_);
    prefs.putBool("soc_est",       battery_soc_estimated_);
    prefs.putUChar("led_mode",     battery_led_mode_);
    prefs.putUInt("version",       battery_settings_version_);

    BatterySettingsBlob blob{};
    blob.capacity_wh              = battery_capacity_wh_;
    blob.max_voltage_mv           = battery_max_voltage_mv_;
    blob.min_voltage_mv           = battery_min_voltage_mv_;
    blob.max_charge_current_a     = battery_max_charge_current_a_;
    blob.max_discharge_current_a  = battery_max_discharge_current_a_;
    blob.soc_high_limit           = battery_soc_high_limit_;
    blob.soc_low_limit            = battery_soc_low_limit_;
    blob.cell_count               = battery_cell_count_;
    blob.chemistry                = battery_chemistry_;
    blob.double_enabled           = battery_double_enabled_;
    blob.pack_max_voltage_dv      = battery_pack_max_voltage_dV_;
    blob.pack_min_voltage_dv      = battery_pack_min_voltage_dV_;
    blob.cell_max_voltage_mv      = battery_cell_max_voltage_mV_;
    blob.cell_min_voltage_mv      = battery_cell_min_voltage_mV_;
    blob.soc_estimated            = battery_soc_estimated_;
    blob.led_mode                 = battery_led_mode_;
    blob.version                  = battery_settings_version_;
    blob.crc32 = EspnowPacketUtils::calculate_message_crc32_zeroed(&blob);
    const size_t written = prefs.putBytes(kSettingsBlobKey, &blob, sizeof(blob));

    prefs.end();

    if (written != sizeof(blob)) {
        LOG_ERROR("SETTINGS", "Failed to save battery settings blob");
        return false;
    }

    LOG_INFO("SETTINGS", "Battery settings saved to NVS (version %u)",
             battery_settings_version_);
    return true;
}

// ---------------------------------------------------------------------------
// Power
// ---------------------------------------------------------------------------

bool SettingsManager::load_power_settings() {
    Preferences prefs;
    if (!prefs.begin("power", true)) {
        LOG_WARN("SETTINGS",
                 "Power namespace doesn't exist yet (first boot) - will use defaults");
        return false;
    }

    bool loaded_from_blob = false;
    const size_t blob_size = prefs.getBytesLength(kSettingsBlobKey);
    if (blob_size == sizeof(PowerSettingsBlob)) {
        PowerSettingsBlob blob{};
        prefs.getBytes(kSettingsBlobKey, &blob, sizeof(blob));
        if (EspnowPacketUtils::verify_message_crc32(&blob)) {
            power_charge_w_               = blob.charge_w;
            power_discharge_w_            = blob.discharge_w;
            power_max_precharge_ms_       = blob.max_precharge_ms;
            power_precharge_duration_ms_  = blob.precharge_duration_ms;
            power_settings_version_       = blob.version;
            loaded_from_blob = true;
        } else {
            LOG_WARN("SETTINGS",
                     "Power settings CRC mismatch - falling back to legacy keys");
        }
    }

    if (!loaded_from_blob) {
        power_charge_w_              = prefs.getUShort("charge_w", 3000);
        power_discharge_w_           = prefs.getUShort("discharge_w", 3000);
        power_max_precharge_ms_      = prefs.getUShort("max_precharge_ms", 15000);
        power_precharge_duration_ms_ = prefs.getUShort("precharge_ms", 100);
        power_settings_version_      = prefs.getUInt("version", 0);
    }

    prefs.end();

    last_validation_ = validate_power_settings();
    if (!last_validation_.is_valid) {
        LOG_ERROR("SETTINGS", "Power settings validation failed: %s",
                  last_validation_.error_message.c_str());
        return false;
    }
    return true;
}

bool SettingsManager::save_power_settings() {
    Preferences prefs;

    last_validation_ = validate_power_settings();
    if (!last_validation_.is_valid) {
        LOG_ERROR("SETTINGS", "Power settings save aborted: %s",
                  last_validation_.error_message.c_str());
        return false;
    }

    if (!prefs.begin("power", false)) {
        LOG_ERROR("SETTINGS", "Failed to open power namespace for writing");
        return false;
    }

    prefs.putUShort("charge_w",        power_charge_w_);
    prefs.putUShort("discharge_w",     power_discharge_w_);
    prefs.putUShort("max_precharge_ms", power_max_precharge_ms_);
    prefs.putUShort("precharge_ms",    power_precharge_duration_ms_);
    prefs.putUInt("version",           power_settings_version_);

    PowerSettingsBlob blob{};
    blob.charge_w              = power_charge_w_;
    blob.discharge_w           = power_discharge_w_;
    blob.max_precharge_ms      = power_max_precharge_ms_;
    blob.precharge_duration_ms = power_precharge_duration_ms_;
    blob.version               = power_settings_version_;
    blob.crc32 = EspnowPacketUtils::calculate_message_crc32_zeroed(&blob);
    const size_t written = prefs.putBytes(kSettingsBlobKey, &blob, sizeof(blob));
    prefs.end();

    return written == sizeof(blob);
}

// ---------------------------------------------------------------------------
// Inverter
// ---------------------------------------------------------------------------

bool SettingsManager::load_inverter_settings() {
    Preferences prefs;
    if (!prefs.begin("inverter", true)) {
        LOG_WARN("SETTINGS",
                 "Inverter namespace doesn't exist yet (first boot) - will use defaults");
        return false;
    }

    bool loaded_from_blob = false;
    const size_t blob_size = prefs.getBytesLength(kSettingsBlobKey);
    if (blob_size == sizeof(InverterSettingsBlob)) {
        InverterSettingsBlob blob{};
        prefs.getBytes(kSettingsBlobKey, &blob, sizeof(blob));
        if (EspnowPacketUtils::verify_message_crc32(&blob)) {
            inverter_cells_             = blob.cells;
            inverter_modules_           = blob.modules;
            inverter_cells_per_module_  = blob.cells_per_module;
            inverter_voltage_level_     = blob.voltage_level;
            inverter_capacity_ah_       = blob.capacity_ah;
            inverter_battery_type_      = blob.battery_type;
            inverter_settings_version_  = blob.version;
            loaded_from_blob = true;
        } else {
            LOG_WARN("SETTINGS",
                     "Inverter settings CRC mismatch - falling back to legacy keys");
        }
    }

    if (!loaded_from_blob) {
        inverter_cells_            = prefs.getUChar("cells", 0);
        inverter_modules_          = prefs.getUChar("modules", 0);
        inverter_cells_per_module_ = prefs.getUChar("cells_per_module", 0);
        inverter_voltage_level_    = prefs.getUShort("voltage_level", 0);
        inverter_capacity_ah_      = prefs.getUShort("capacity_ah", 0);
        inverter_battery_type_     = prefs.getUChar("battery_type", 0);
        inverter_settings_version_ = prefs.getUInt("version", 0);
    }

    prefs.end();

    last_validation_ = validate_inverter_settings();
    if (!last_validation_.is_valid) {
        LOG_ERROR("SETTINGS", "Inverter settings validation failed: %s",
                  last_validation_.error_message.c_str());
        return false;
    }
    return true;
}

bool SettingsManager::save_inverter_settings() {
    Preferences prefs;

    last_validation_ = validate_inverter_settings();
    if (!last_validation_.is_valid) {
        LOG_ERROR("SETTINGS", "Inverter settings save aborted: %s",
                  last_validation_.error_message.c_str());
        return false;
    }

    if (!prefs.begin("inverter", false)) {
        LOG_ERROR("SETTINGS", "Failed to open inverter namespace for writing");
        return false;
    }

    prefs.putUChar("cells",             inverter_cells_);
    prefs.putUChar("modules",           inverter_modules_);
    prefs.putUChar("cells_per_module",  inverter_cells_per_module_);
    prefs.putUShort("voltage_level",    inverter_voltage_level_);
    prefs.putUShort("capacity_ah",      inverter_capacity_ah_);
    prefs.putUChar("battery_type",      inverter_battery_type_);
    prefs.putUInt("version",            inverter_settings_version_);

    InverterSettingsBlob blob{};
    blob.cells            = inverter_cells_;
    blob.modules          = inverter_modules_;
    blob.cells_per_module = inverter_cells_per_module_;
    blob.voltage_level    = inverter_voltage_level_;
    blob.capacity_ah      = inverter_capacity_ah_;
    blob.battery_type     = inverter_battery_type_;
    blob.version          = inverter_settings_version_;
    blob.crc32 = EspnowPacketUtils::calculate_message_crc32_zeroed(&blob);
    const size_t written = prefs.putBytes(kSettingsBlobKey, &blob, sizeof(blob));
    prefs.end();
    return written == sizeof(blob);
}

// ---------------------------------------------------------------------------
// CAN
// ---------------------------------------------------------------------------

bool SettingsManager::load_can_settings() {
    Preferences prefs;
    if (!prefs.begin("can", true)) {
        LOG_WARN("SETTINGS",
                 "CAN namespace doesn't exist yet (first boot) - will use defaults");
        return false;
    }

    bool loaded_from_blob = false;
    const size_t blob_size = prefs.getBytesLength(kSettingsBlobKey);
    if (blob_size == sizeof(CanSettingsBlob)) {
        CanSettingsBlob blob{};
        prefs.getBytes(kSettingsBlobKey, &blob, sizeof(blob));
        if (EspnowPacketUtils::verify_message_crc32(&blob)) {
            can_frequency_khz_          = blob.frequency_khz;
            can_fd_frequency_mhz_       = blob.fd_frequency_mhz;
            can_sofar_id_               = blob.sofar_id;
            can_pylon_send_interval_ms_ = blob.pylon_send_interval_ms;
            can_settings_version_       = blob.version;
            loaded_from_blob = true;
        } else {
            LOG_WARN("SETTINGS",
                     "CAN settings CRC mismatch - falling back to legacy keys");
        }
    }

    if (!loaded_from_blob) {
        can_frequency_khz_          = prefs.getUShort("freq_khz", 8);
        can_fd_frequency_mhz_       = prefs.getUShort("fd_freq_mhz", 40);
        can_sofar_id_               = prefs.getUShort("sofar_id", 0);
        can_pylon_send_interval_ms_ = prefs.getUShort("pylon_send_ms", 0);
        can_settings_version_       = prefs.getUInt("version", 0);
    }

    prefs.end();

    last_validation_ = validate_can_settings();
    if (!last_validation_.is_valid) {
        LOG_ERROR("SETTINGS", "CAN settings validation failed: %s",
                  last_validation_.error_message.c_str());
        return false;
    }
    return true;
}

bool SettingsManager::save_can_settings() {
    Preferences prefs;

    last_validation_ = validate_can_settings();
    if (!last_validation_.is_valid) {
        LOG_ERROR("SETTINGS", "CAN settings save aborted: %s",
                  last_validation_.error_message.c_str());
        return false;
    }

    if (!prefs.begin("can", false)) {
        LOG_ERROR("SETTINGS", "Failed to open CAN namespace for writing");
        return false;
    }

    prefs.putUShort("freq_khz",      can_frequency_khz_);
    prefs.putUShort("fd_freq_mhz",   can_fd_frequency_mhz_);
    prefs.putUShort("sofar_id",      can_sofar_id_);
    prefs.putUShort("pylon_send_ms", can_pylon_send_interval_ms_);
    prefs.putUInt("version",         can_settings_version_);

    CanSettingsBlob blob{};
    blob.frequency_khz          = can_frequency_khz_;
    blob.fd_frequency_mhz       = can_fd_frequency_mhz_;
    blob.sofar_id               = can_sofar_id_;
    blob.pylon_send_interval_ms = can_pylon_send_interval_ms_;
    blob.version                = can_settings_version_;
    blob.crc32 = EspnowPacketUtils::calculate_message_crc32_zeroed(&blob);
    const size_t written = prefs.putBytes(kSettingsBlobKey, &blob, sizeof(blob));
    prefs.end();
    return written == sizeof(blob);
}

// ---------------------------------------------------------------------------
// Contactor
// ---------------------------------------------------------------------------

bool SettingsManager::load_contactor_settings() {
    Preferences prefs;
    if (!prefs.begin("contactor", true)) {
        LOG_WARN("SETTINGS",
                 "Contactor namespace doesn't exist yet (first boot) - will use defaults");
        return false;
    }

    bool loaded_from_blob = false;
    const size_t blob_size = prefs.getBytesLength(kSettingsBlobKey);
    if (blob_size == sizeof(ContactorSettingsBlob)) {
        ContactorSettingsBlob blob{};
        prefs.getBytes(kSettingsBlobKey, &blob, sizeof(blob));
        if (EspnowPacketUtils::verify_message_crc32(&blob)) {
            contactor_control_enabled_  = blob.control_enabled;
            contactor_nc_mode_          = blob.nc_mode;
            contactor_pwm_frequency_hz_ = blob.pwm_frequency_hz;
            contactor_settings_version_ = blob.version;
            loaded_from_blob = true;
        } else {
            LOG_WARN("SETTINGS",
                     "Contactor settings CRC mismatch - falling back to legacy keys");
        }
    }

    if (!loaded_from_blob) {
        contactor_control_enabled_  = prefs.getBool("control_enabled", false);
        contactor_nc_mode_          = prefs.getBool("nc_mode", false);
        contactor_pwm_frequency_hz_ = prefs.getUShort("pwm_hz", 20000);
        contactor_settings_version_ = prefs.getUInt("version", 0);
    }

    prefs.end();

    last_validation_ = validate_contactor_settings();
    if (!last_validation_.is_valid) {
        LOG_ERROR("SETTINGS", "Contactor settings validation failed: %s",
                  last_validation_.error_message.c_str());
        return false;
    }
    return true;
}

bool SettingsManager::save_contactor_settings() {
    Preferences prefs;

    last_validation_ = validate_contactor_settings();
    if (!last_validation_.is_valid) {
        LOG_ERROR("SETTINGS", "Contactor settings save aborted: %s",
                  last_validation_.error_message.c_str());
        return false;
    }

    if (!prefs.begin("contactor", false)) {
        LOG_ERROR("SETTINGS", "Failed to open contactor namespace for writing");
        return false;
    }

    prefs.putBool("control_enabled",  contactor_control_enabled_);
    prefs.putBool("nc_mode",          contactor_nc_mode_);
    prefs.putUShort("pwm_hz",         contactor_pwm_frequency_hz_);
    prefs.putUInt("version",          contactor_settings_version_);

    ContactorSettingsBlob blob{};
    blob.control_enabled  = contactor_control_enabled_;
    blob.nc_mode          = contactor_nc_mode_;
    blob.pwm_frequency_hz = contactor_pwm_frequency_hz_;
    blob.version          = contactor_settings_version_;
    blob.crc32 = EspnowPacketUtils::calculate_message_crc32_zeroed(&blob);
    const size_t written = prefs.putBytes(kSettingsBlobKey, &blob, sizeof(blob));
    prefs.end();
    return written == sizeof(blob);
}
