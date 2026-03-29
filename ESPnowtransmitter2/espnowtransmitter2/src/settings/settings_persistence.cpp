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

constexpr uint16_t kBatteryBlobSchemaVersion = 1;
constexpr uint16_t kPowerBlobSchemaVersion = 3;
constexpr uint16_t kInverterBlobSchemaVersion = 1;
constexpr uint16_t kCanBlobSchemaVersion = 2;
constexpr uint16_t kContactorBlobSchemaVersion = 5;

struct __attribute__((packed)) BatterySettingsBlob {
    uint16_t schema_version;
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
    uint16_t schema_version;
    uint16_t charge_w;
    uint16_t discharge_w;
    uint16_t max_precharge_ms;
    uint16_t precharge_duration_ms;
    uint8_t  equipment_stop_type;
    bool     external_precharge_enabled;
    bool     no_inverter_disconnect_contactor;
    uint32_t version;
    uint32_t crc32;
};

struct __attribute__((packed)) InverterSettingsBlob {
    uint16_t schema_version;
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
    uint16_t schema_version;
    uint16_t frequency_khz;
    uint16_t fd_frequency_mhz;
    uint16_t sofar_id;
    uint16_t pylon_send_interval_ms;
    bool     use_canfd_as_classic;
    uint32_t version;
    uint32_t crc32;
};

struct __attribute__((packed)) ContactorSettingsBlob {
    uint16_t schema_version;
    bool     control_enabled;
    bool     nc_mode;
    uint16_t pwm_frequency_hz;
    bool     pwm_control_enabled;
    uint16_t pwm_hold_duty;
    bool     periodic_bms_reset;
    bool     bms_first_align_enabled;
    uint16_t bms_first_align_target_minutes;
    uint32_t version;
    uint32_t crc32;
};

struct BatteryLegacyDefaults {
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
};

struct PowerLegacyDefaults {
    uint16_t charge_w;
    uint16_t discharge_w;
    uint16_t max_precharge_ms;
    uint16_t precharge_duration_ms;
    uint8_t  equipment_stop_type;
    bool external_precharge_enabled;
    bool no_inverter_disconnect_contactor;
    uint32_t version;
};

struct InverterLegacyDefaults {
    uint8_t cells;
    uint8_t modules;
    uint8_t cells_per_module;
    uint16_t voltage_level;
    uint16_t capacity_ah;
    uint8_t battery_type;
    uint32_t version;
};

struct CanLegacyDefaults {
    uint16_t frequency_khz;
    uint16_t fd_frequency_mhz;
    uint16_t sofar_id;
    uint16_t pylon_send_interval_ms;
    bool use_canfd_as_classic;
    uint32_t version;
};

struct ContactorLegacyDefaults {
    bool control_enabled;
    bool nc_mode;
    uint16_t pwm_frequency_hz;
    bool pwm_control_enabled;
    uint16_t pwm_hold_duty;
    bool periodic_bms_reset;
    bool bms_first_align_enabled;
    uint16_t bms_first_align_target_minutes;
    uint32_t version;
};

constexpr uint8_t kBatteryChemistryLfp = 2;

constexpr BatteryLegacyDefaults kBatteryLegacyDefaults{
    30000,
    58000,
    46000,
    100.0f,
    100.0f,
    95,
    20,
    16,
    kBatteryChemistryLfp,
    false,
    580,
    460,
    4200,
    3000,
    false,
    0,
    0,
};

constexpr PowerLegacyDefaults kPowerLegacyDefaults{
    3000,
    3000,
    15000,
    100,
    0,  // equipment_stop_type
    false,
    false,
    0,  // version
};

constexpr InverterLegacyDefaults kInverterLegacyDefaults{
    0,
    0,
    0,
    0,
    0,
    0,
    0,
};

constexpr CanLegacyDefaults kCanLegacyDefaults{
    8,
    40,
    0,
    0,
    false,
    0,
};

constexpr ContactorLegacyDefaults kContactorLegacyDefaults{
    false,
    false,
    20000,
    false,
    250,  // pwm_hold_duty
    false,
    false,
    120,  // 02:00
    0,    // version
};

bool log_nvs_write_failure(const char* settings_ns,
                           const char* key,
                           size_t expected,
                           size_t actual) {
    LOG_ERROR("SETTINGS",
              "NVS write failed (%s/%s): expected=%u, wrote=%u",
              settings_ns,
              key,
              static_cast<unsigned>(expected),
              static_cast<unsigned>(actual));
    return false;
}

bool write_u32_checked(Preferences& prefs,
                       const char* settings_ns,
                       const char* key,
                       uint32_t value) {
    const size_t written = prefs.putUInt(key, value);
    return (written == sizeof(uint32_t))
               ? true
               : log_nvs_write_failure(settings_ns, key, sizeof(uint32_t), written);
}

bool write_u16_checked(Preferences& prefs,
                       const char* settings_ns,
                       const char* key,
                       uint16_t value) {
    const size_t written = prefs.putUShort(key, value);
    return (written == sizeof(uint16_t))
               ? true
               : log_nvs_write_failure(settings_ns, key, sizeof(uint16_t), written);
}

bool write_u8_checked(Preferences& prefs,
                      const char* settings_ns,
                      const char* key,
                      uint8_t value) {
    const size_t written = prefs.putUChar(key, value);
    return (written == sizeof(uint8_t))
               ? true
               : log_nvs_write_failure(settings_ns, key, sizeof(uint8_t), written);
}

bool write_float_checked(Preferences& prefs,
                         const char* settings_ns,
                         const char* key,
                         float value) {
    const size_t written = prefs.putFloat(key, value);
    return (written == sizeof(float))
               ? true
               : log_nvs_write_failure(settings_ns, key, sizeof(float), written);
}

bool write_bool_checked(Preferences& prefs,
                        const char* settings_ns,
                        const char* key,
                        bool value) {
    const size_t written = prefs.putBool(key, value);
    return (written == sizeof(uint8_t))
               ? true
               : log_nvs_write_failure(settings_ns, key, sizeof(uint8_t), written);
}

bool write_blob_checked(Preferences& prefs,
                        const char* settings_ns,
                        const void* blob,
                        size_t blob_size) {
    const size_t written = prefs.putBytes(kSettingsBlobKey, blob, blob_size);
    return (written == blob_size)
               ? true
               : log_nvs_write_failure(settings_ns, kSettingsBlobKey, blob_size, written);
}

template <typename BlobType>
bool read_blob_checked(Preferences& prefs,
                       const char* settings_ns,
                       BlobType* out_blob) {
    const size_t read_len = prefs.getBytes(kSettingsBlobKey,
                                           out_blob,
                                           sizeof(BlobType));
    if (read_len != sizeof(BlobType)) {
        LOG_WARN("SETTINGS",
                 "NVS blob read incomplete (%s/%s): expected=%u, read=%u",
                 settings_ns,
                 kSettingsBlobKey,
                 static_cast<unsigned>(sizeof(BlobType)),
                 static_cast<unsigned>(read_len));
        return false;
    }
    return true;
}

size_t get_blob_size_if_present(Preferences& prefs) {
    return prefs.isKey(kSettingsBlobKey)
               ? prefs.getBytesLength(kSettingsBlobKey)
               : 0;
}

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
    const size_t blob_size = get_blob_size_if_present(prefs);
    if (blob_size == sizeof(BatterySettingsBlob)) {
        BatterySettingsBlob blob{};
        if (read_blob_checked(prefs, "battery", &blob) &&
            EspnowPacketUtils::verify_message_crc32(&blob) &&
            blob.schema_version == kBatteryBlobSchemaVersion) {
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
                     "Battery settings blob invalid/schema mismatch - falling back to legacy keys");
        }
    }

    if (!loaded_from_blob) {
        // Legacy fallback (pre-CRC storage)
        battery_capacity_wh_              = prefs.getUInt("capacity_wh", kBatteryLegacyDefaults.capacity_wh);
        battery_max_voltage_mv_           = prefs.getUInt("max_volt_mv", kBatteryLegacyDefaults.max_voltage_mv);
        battery_min_voltage_mv_           = prefs.getUInt("min_volt_mv", kBatteryLegacyDefaults.min_voltage_mv);
        battery_max_charge_current_a_     = prefs.getFloat("max_chg_a", kBatteryLegacyDefaults.max_charge_current_a);
        battery_max_discharge_current_a_  = prefs.getFloat("max_dis_a", kBatteryLegacyDefaults.max_discharge_current_a);
        battery_soc_high_limit_           = prefs.getUChar("soc_high", kBatteryLegacyDefaults.soc_high_limit);
        battery_soc_low_limit_            = prefs.getUChar("soc_low", kBatteryLegacyDefaults.soc_low_limit);
        battery_cell_count_               = prefs.getUChar("cell_count", kBatteryLegacyDefaults.cell_count);
        battery_chemistry_                = prefs.getUChar("chemistry", kBatteryLegacyDefaults.chemistry);
        battery_double_enabled_           = prefs.getBool("double_enabled", kBatteryLegacyDefaults.double_enabled);
        battery_pack_max_voltage_dV_      = prefs.getUShort("pack_max_dv", kBatteryLegacyDefaults.pack_max_voltage_dv);
        battery_pack_min_voltage_dV_      = prefs.getUShort("pack_min_dv", kBatteryLegacyDefaults.pack_min_voltage_dv);
        battery_cell_max_voltage_mV_      = prefs.getUShort("cell_max_mv", kBatteryLegacyDefaults.cell_max_voltage_mv);
        battery_cell_min_voltage_mV_      = prefs.getUShort("cell_min_mv", kBatteryLegacyDefaults.cell_min_voltage_mv);
        battery_soc_estimated_            = prefs.getBool("soc_est", kBatteryLegacyDefaults.soc_estimated);
        battery_led_mode_                 = prefs.getUChar("led_mode", kBatteryLegacyDefaults.led_mode);
        battery_settings_version_         = prefs.getUInt("version", kBatteryLegacyDefaults.version);
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

    bool writes_ok = true;
    writes_ok &= write_u32_checked(prefs, "battery", "capacity_wh", battery_capacity_wh_);
    writes_ok &= write_u32_checked(prefs, "battery", "max_volt_mv", battery_max_voltage_mv_);
    writes_ok &= write_u32_checked(prefs, "battery", "min_volt_mv", battery_min_voltage_mv_);
    writes_ok &= write_float_checked(prefs, "battery", "max_chg_a", battery_max_charge_current_a_);
    writes_ok &= write_float_checked(prefs, "battery", "max_dis_a", battery_max_discharge_current_a_);
    writes_ok &= write_u8_checked(prefs, "battery", "soc_high", battery_soc_high_limit_);
    writes_ok &= write_u8_checked(prefs, "battery", "soc_low", battery_soc_low_limit_);
    writes_ok &= write_u8_checked(prefs, "battery", "cell_count", battery_cell_count_);
    writes_ok &= write_u8_checked(prefs, "battery", "chemistry", battery_chemistry_);
    writes_ok &= write_bool_checked(prefs, "battery", "double_enabled", battery_double_enabled_);
    writes_ok &= write_u16_checked(prefs, "battery", "pack_max_dv", battery_pack_max_voltage_dV_);
    writes_ok &= write_u16_checked(prefs, "battery", "pack_min_dv", battery_pack_min_voltage_dV_);
    writes_ok &= write_u16_checked(prefs, "battery", "cell_max_mv", battery_cell_max_voltage_mV_);
    writes_ok &= write_u16_checked(prefs, "battery", "cell_min_mv", battery_cell_min_voltage_mV_);
    writes_ok &= write_bool_checked(prefs, "battery", "soc_est", battery_soc_estimated_);
    writes_ok &= write_u8_checked(prefs, "battery", "led_mode", battery_led_mode_);
    writes_ok &= write_u32_checked(prefs, "battery", "version", battery_settings_version_);

    BatterySettingsBlob blob{};
    blob.schema_version           = kBatteryBlobSchemaVersion;
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
    writes_ok &= write_blob_checked(prefs, "battery", &blob, sizeof(blob));

    prefs.end();

    if (!writes_ok) {
        LOG_ERROR("SETTINGS", "Failed to save battery settings (one or more NVS writes failed)");
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
    const size_t blob_size = get_blob_size_if_present(prefs);
    if (blob_size == sizeof(PowerSettingsBlob)) {
        PowerSettingsBlob blob{};
        if (read_blob_checked(prefs, "power", &blob) &&
            EspnowPacketUtils::verify_message_crc32(&blob) &&
            blob.schema_version == kPowerBlobSchemaVersion) {
            power_charge_w_               = blob.charge_w;
            power_discharge_w_            = blob.discharge_w;
            power_max_precharge_ms_       = blob.max_precharge_ms;
            power_precharge_duration_ms_  = blob.precharge_duration_ms;
            power_equipment_stop_type_    = blob.equipment_stop_type;
            power_external_precharge_enabled_ = blob.external_precharge_enabled;
            power_no_inverter_disconnect_contactor_ = blob.no_inverter_disconnect_contactor;
            power_settings_version_       = blob.version;
            loaded_from_blob = true;
        } else {
            LOG_WARN("SETTINGS",
                     "Power settings blob invalid/schema mismatch - falling back to legacy keys");
        }
    }

    if (!loaded_from_blob) {
        power_charge_w_              = prefs.getUShort("charge_w", kPowerLegacyDefaults.charge_w);
        power_discharge_w_           = prefs.getUShort("discharge_w", kPowerLegacyDefaults.discharge_w);
        power_max_precharge_ms_      = prefs.getUShort("max_precharge_ms", kPowerLegacyDefaults.max_precharge_ms);
        power_precharge_duration_ms_ = prefs.getUShort("precharge_ms", kPowerLegacyDefaults.precharge_duration_ms);
        power_equipment_stop_type_   = prefs.getUChar("eq_stop_type", kPowerLegacyDefaults.equipment_stop_type);
        power_external_precharge_enabled_ = prefs.getBool("ext_precharge", kPowerLegacyDefaults.external_precharge_enabled);
        power_no_inverter_disconnect_contactor_ = prefs.getBool("no_inv_disc", kPowerLegacyDefaults.no_inverter_disconnect_contactor);
        power_settings_version_      = prefs.getUInt("version", kPowerLegacyDefaults.version);
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

    bool writes_ok = true;
    writes_ok &= write_u16_checked(prefs, "power", "charge_w", power_charge_w_);
    writes_ok &= write_u16_checked(prefs, "power", "discharge_w", power_discharge_w_);
    writes_ok &= write_u16_checked(prefs, "power", "max_precharge_ms", power_max_precharge_ms_);
    writes_ok &= write_u16_checked(prefs, "power", "precharge_ms", power_precharge_duration_ms_);
    writes_ok &= write_u8_checked(prefs, "power", "eq_stop_type", power_equipment_stop_type_);
    writes_ok &= write_bool_checked(prefs, "power", "ext_precharge", power_external_precharge_enabled_);
    writes_ok &= write_bool_checked(prefs, "power", "no_inv_disc", power_no_inverter_disconnect_contactor_);
    writes_ok &= write_u32_checked(prefs, "power", "version", power_settings_version_);

    PowerSettingsBlob blob{};
    blob.schema_version       = kPowerBlobSchemaVersion;
    blob.charge_w              = power_charge_w_;
    blob.discharge_w           = power_discharge_w_;
    blob.max_precharge_ms      = power_max_precharge_ms_;
    blob.precharge_duration_ms = power_precharge_duration_ms_;
    blob.equipment_stop_type   = power_equipment_stop_type_;
    blob.external_precharge_enabled = power_external_precharge_enabled_;
    blob.no_inverter_disconnect_contactor = power_no_inverter_disconnect_contactor_;
    blob.version               = power_settings_version_;
    blob.crc32 = EspnowPacketUtils::calculate_message_crc32_zeroed(&blob);
    writes_ok &= write_blob_checked(prefs, "power", &blob, sizeof(blob));
    prefs.end();

    return writes_ok;
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
    const size_t blob_size = get_blob_size_if_present(prefs);
    if (blob_size == sizeof(InverterSettingsBlob)) {
        InverterSettingsBlob blob{};
        if (read_blob_checked(prefs, "inverter", &blob) &&
            EspnowPacketUtils::verify_message_crc32(&blob) &&
            blob.schema_version == kInverterBlobSchemaVersion) {
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
                     "Inverter settings blob invalid/schema mismatch - falling back to legacy keys");
        }
    }

    if (!loaded_from_blob) {
        inverter_cells_            = prefs.getUChar("cells", kInverterLegacyDefaults.cells);
        inverter_modules_          = prefs.getUChar("modules", kInverterLegacyDefaults.modules);
        inverter_cells_per_module_ = prefs.getUChar("cells_per_module", kInverterLegacyDefaults.cells_per_module);
        inverter_voltage_level_    = prefs.getUShort("voltage_level", kInverterLegacyDefaults.voltage_level);
        inverter_capacity_ah_      = prefs.getUShort("capacity_ah", kInverterLegacyDefaults.capacity_ah);
        inverter_battery_type_     = prefs.getUChar("battery_type", kInverterLegacyDefaults.battery_type);
        inverter_settings_version_ = prefs.getUInt("version", kInverterLegacyDefaults.version);
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

    bool writes_ok = true;
    writes_ok &= write_u8_checked(prefs, "inverter", "cells", inverter_cells_);
    writes_ok &= write_u8_checked(prefs, "inverter", "modules", inverter_modules_);
    writes_ok &= write_u8_checked(prefs, "inverter", "cells_per_module", inverter_cells_per_module_);
    writes_ok &= write_u16_checked(prefs, "inverter", "voltage_level", inverter_voltage_level_);
    writes_ok &= write_u16_checked(prefs, "inverter", "capacity_ah", inverter_capacity_ah_);
    writes_ok &= write_u8_checked(prefs, "inverter", "battery_type", inverter_battery_type_);
    writes_ok &= write_u32_checked(prefs, "inverter", "version", inverter_settings_version_);

    InverterSettingsBlob blob{};
    blob.schema_version   = kInverterBlobSchemaVersion;
    blob.cells            = inverter_cells_;
    blob.modules          = inverter_modules_;
    blob.cells_per_module = inverter_cells_per_module_;
    blob.voltage_level    = inverter_voltage_level_;
    blob.capacity_ah      = inverter_capacity_ah_;
    blob.battery_type     = inverter_battery_type_;
    blob.version          = inverter_settings_version_;
    blob.crc32 = EspnowPacketUtils::calculate_message_crc32_zeroed(&blob);
    writes_ok &= write_blob_checked(prefs, "inverter", &blob, sizeof(blob));
    prefs.end();
    return writes_ok;
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
    const size_t blob_size = get_blob_size_if_present(prefs);
    if (blob_size == sizeof(CanSettingsBlob)) {
        CanSettingsBlob blob{};
        if (read_blob_checked(prefs, "can", &blob) &&
            EspnowPacketUtils::verify_message_crc32(&blob) &&
            blob.schema_version == kCanBlobSchemaVersion) {
            can_frequency_khz_          = blob.frequency_khz;
            can_fd_frequency_mhz_       = blob.fd_frequency_mhz;
            can_sofar_id_               = blob.sofar_id;
            can_pylon_send_interval_ms_ = blob.pylon_send_interval_ms;
            can_use_canfd_as_classic_   = blob.use_canfd_as_classic;
            can_settings_version_       = blob.version;
            loaded_from_blob = true;
        } else {
            LOG_WARN("SETTINGS",
                     "CAN settings blob invalid/schema mismatch - falling back to legacy keys");
        }
    }

    if (!loaded_from_blob) {
        can_frequency_khz_          = prefs.getUShort("freq_khz", kCanLegacyDefaults.frequency_khz);
        can_fd_frequency_mhz_       = prefs.getUShort("fd_freq_mhz", kCanLegacyDefaults.fd_frequency_mhz);
        can_sofar_id_               = prefs.getUShort("sofar_id", kCanLegacyDefaults.sofar_id);
        can_pylon_send_interval_ms_ = prefs.getUShort("pylon_send_ms", kCanLegacyDefaults.pylon_send_interval_ms);
        can_use_canfd_as_classic_   = prefs.getBool("canfd_classic", kCanLegacyDefaults.use_canfd_as_classic);
        can_settings_version_       = prefs.getUInt("version", kCanLegacyDefaults.version);
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

    bool writes_ok = true;
    writes_ok &= write_u16_checked(prefs, "can", "freq_khz", can_frequency_khz_);
    writes_ok &= write_u16_checked(prefs, "can", "fd_freq_mhz", can_fd_frequency_mhz_);
    writes_ok &= write_u16_checked(prefs, "can", "sofar_id", can_sofar_id_);
    writes_ok &= write_u16_checked(prefs, "can", "pylon_send_ms", can_pylon_send_interval_ms_);
    writes_ok &= write_bool_checked(prefs, "can", "canfd_classic", can_use_canfd_as_classic_);
    writes_ok &= write_u32_checked(prefs, "can", "version", can_settings_version_);

    CanSettingsBlob blob{};
    blob.schema_version        = kCanBlobSchemaVersion;
    blob.frequency_khz          = can_frequency_khz_;
    blob.fd_frequency_mhz       = can_fd_frequency_mhz_;
    blob.sofar_id               = can_sofar_id_;
    blob.pylon_send_interval_ms = can_pylon_send_interval_ms_;
    blob.use_canfd_as_classic   = can_use_canfd_as_classic_;
    blob.version                = can_settings_version_;
    blob.crc32 = EspnowPacketUtils::calculate_message_crc32_zeroed(&blob);
    writes_ok &= write_blob_checked(prefs, "can", &blob, sizeof(blob));
    prefs.end();
    return writes_ok;
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
    const size_t blob_size = get_blob_size_if_present(prefs);
    if (blob_size == sizeof(ContactorSettingsBlob)) {
        ContactorSettingsBlob blob{};
        if (read_blob_checked(prefs, "contactor", &blob) &&
            EspnowPacketUtils::verify_message_crc32(&blob) &&
            blob.schema_version == kContactorBlobSchemaVersion) {
            contactor_control_enabled_      = blob.control_enabled;
            contactor_nc_mode_              = blob.nc_mode;
            contactor_pwm_frequency_hz_     = blob.pwm_frequency_hz;
            contactor_pwm_control_enabled_  = blob.pwm_control_enabled;
            contactor_pwm_hold_duty_        = blob.pwm_hold_duty;
            contactor_periodic_bms_reset_   = blob.periodic_bms_reset;
            contactor_bms_first_align_enabled_ = blob.bms_first_align_enabled;
            contactor_bms_first_align_target_minutes_ = blob.bms_first_align_target_minutes;
            contactor_settings_version_     = blob.version;
            loaded_from_blob = true;
        } else {
            LOG_WARN("SETTINGS",
                     "Contactor settings blob invalid/schema mismatch - falling back to legacy keys");
        }
    }

    if (!loaded_from_blob) {
        contactor_control_enabled_      = prefs.getBool("control_enabled", kContactorLegacyDefaults.control_enabled);
        contactor_nc_mode_              = prefs.getBool("nc_mode", kContactorLegacyDefaults.nc_mode);
        contactor_pwm_frequency_hz_     = prefs.getUShort("pwm_hz", kContactorLegacyDefaults.pwm_frequency_hz);
        contactor_pwm_control_enabled_  = prefs.getBool("pwm_ctrl", kContactorLegacyDefaults.pwm_control_enabled);
        contactor_pwm_hold_duty_        = prefs.getUShort("pwm_hold", kContactorLegacyDefaults.pwm_hold_duty);
        // Prefer short key for NVS key-length safety, fallback to legacy long key
        contactor_periodic_bms_reset_   = prefs.getBool("per_bms", prefs.getBool("periodic_bms_reset", kContactorLegacyDefaults.periodic_bms_reset));
        contactor_bms_first_align_enabled_ = prefs.getBool("bms1st_en", kContactorLegacyDefaults.bms_first_align_enabled);
        contactor_bms_first_align_target_minutes_ = prefs.getUShort("bms1st_min", kContactorLegacyDefaults.bms_first_align_target_minutes);
        contactor_settings_version_     = prefs.getUInt("version", kContactorLegacyDefaults.version);
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

    bool writes_ok = true;
    writes_ok &= write_bool_checked(prefs, "contactor", "control_enabled", contactor_control_enabled_);
    writes_ok &= write_bool_checked(prefs, "contactor", "nc_mode", contactor_nc_mode_);
    writes_ok &= write_u16_checked(prefs, "contactor", "pwm_hz", contactor_pwm_frequency_hz_);
    writes_ok &= write_bool_checked(prefs, "contactor", "pwm_ctrl", contactor_pwm_control_enabled_);
    writes_ok &= write_u16_checked(prefs, "contactor", "pwm_hold", contactor_pwm_hold_duty_);
    // Keep key short to avoid KEY_TOO_LONG on ESP32 NVS
    writes_ok &= write_bool_checked(prefs, "contactor", "per_bms", contactor_periodic_bms_reset_);
    writes_ok &= write_bool_checked(prefs, "contactor", "bms1st_en", contactor_bms_first_align_enabled_);
    writes_ok &= write_u16_checked(prefs, "contactor", "bms1st_min", contactor_bms_first_align_target_minutes_);
    writes_ok &= write_u32_checked(prefs, "contactor", "version", contactor_settings_version_);

    ContactorSettingsBlob blob{};
    blob.schema_version       = kContactorBlobSchemaVersion;
    blob.control_enabled      = contactor_control_enabled_;
    blob.nc_mode              = contactor_nc_mode_;
    blob.pwm_frequency_hz     = contactor_pwm_frequency_hz_;
    blob.pwm_control_enabled  = contactor_pwm_control_enabled_;
    blob.pwm_hold_duty        = contactor_pwm_hold_duty_;
    blob.periodic_bms_reset   = contactor_periodic_bms_reset_;
    blob.bms_first_align_enabled = contactor_bms_first_align_enabled_;
    blob.bms_first_align_target_minutes = contactor_bms_first_align_target_minutes_;
    blob.version              = contactor_settings_version_;
    blob.crc32 = EspnowPacketUtils::calculate_message_crc32_zeroed(&blob);
    writes_ok &= write_blob_checked(prefs, "contactor", &blob, sizeof(blob));
    prefs.end();
    return writes_ok;
}
