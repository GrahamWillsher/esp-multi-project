// settings_field_setters.cpp
// Implements the five per-field setter dispatch functions for SettingsManager.
// Each function validates a single incoming field value, updates the
// corresponding member, increments the category version, persists to NVS, and
// broadcasts a settings-changed notification via ESP-NOW.
//
// Extracted from settings_manager.cpp to reduce file size.

#include "settings_manager.h"
#include "../config/logging_config.h"
#include "../datalayer/datalayer.h"
#include "../battery_emulator/devboard/utils/led_handler.h"

namespace {

// Generic bounded-range descriptor used by all settings categories.
struct UIntFieldDescriptor {
    uint8_t field_id;
    const char* name;
    uint32_t min_value;
    uint32_t max_value;
};

struct FloatFieldDescriptor {
    uint8_t field_id;
    const char* name;
    float min_value;
    float max_value;
};

struct UIntRange {
    uint32_t min;
    uint32_t max;
};

struct FloatRange {
    float min;
    float max;
};

constexpr UIntRange kRangeBatteryCapacityWh{1000, 1000000};
constexpr UIntRange kRangeBatteryMaxVoltageMv{30000, 100000};
constexpr UIntRange kRangeBatteryMinVoltageMv{20000, 80000};
constexpr UIntRange kRangeBatterySocHighLimit{50, 100};
constexpr UIntRange kRangeBatterySocLowLimit{0, 50};
constexpr UIntRange kRangeBatteryCellCount{4, 32};
constexpr UIntRange kRangeBatteryChemistry{0, 3};
constexpr UIntRange kRangeBatteryPackVoltageDv{100, 10000};
constexpr UIntRange kRangeBatteryCellMaxVoltageMv{1500, 5000};
constexpr UIntRange kRangeBatteryCellMinVoltageMv{1000, 4500};
constexpr UIntRange kRangeBatteryLedMode{0, 2};
constexpr FloatRange kRangeBatteryCurrentA{0.0f, 500.0f};

constexpr UIntRange kRangePowerWatts{0, 50000};
constexpr UIntRange kRangePowerMaxPrechargeMs{100, 60000};
constexpr UIntRange kRangePowerPrechargeDurationMs{10, 30000};

constexpr UIntRange kRangeInverterCells{0, 32};
constexpr UIntRange kRangeInverterVoltageLevel{0, 10000};
constexpr UIntRange kRangeInverterCapacityAh{0, 65000};
constexpr UIntRange kRangeInverterBatteryType{0, 20};

constexpr UIntRange kRangeCanFrequencyKhz{1, 1000};
constexpr UIntRange kRangeCanFdFrequencyMhz{1, 100};
constexpr UIntRange kRangeCanSofarId{0, 65535};
constexpr UIntRange kRangeCanPylonSendIntervalMs{0, 60000};

constexpr UIntRange kRangeContactorPwmFrequencyHz{100, 50000};

// --- Battery ---
constexpr UIntFieldDescriptor kBatteryUIntFieldDescriptors[] = {
    {BATTERY_CAPACITY_WH,         "capacity_wh",         kRangeBatteryCapacityWh.min,       kRangeBatteryCapacityWh.max},
    {BATTERY_MAX_VOLTAGE_MV,      "max_voltage_mv",      kRangeBatteryMaxVoltageMv.min,     kRangeBatteryMaxVoltageMv.max},
    {BATTERY_MIN_VOLTAGE_MV,      "min_voltage_mv",      kRangeBatteryMinVoltageMv.min,     kRangeBatteryMinVoltageMv.max},
    {BATTERY_SOC_HIGH_LIMIT,      "soc_high_limit",      kRangeBatterySocHighLimit.min,     kRangeBatterySocHighLimit.max},
    {BATTERY_SOC_LOW_LIMIT,       "soc_low_limit",       kRangeBatterySocLowLimit.min,      kRangeBatterySocLowLimit.max},
    {BATTERY_CELL_COUNT,          "cell_count",          kRangeBatteryCellCount.min,        kRangeBatteryCellCount.max},
    {BATTERY_CHEMISTRY,           "chemistry",           kRangeBatteryChemistry.min,        kRangeBatteryChemistry.max},
    {BATTERY_PACK_MAX_VOLTAGE_DV, "pack_max_voltage_dv", kRangeBatteryPackVoltageDv.min,    kRangeBatteryPackVoltageDv.max},
    {BATTERY_PACK_MIN_VOLTAGE_DV, "pack_min_voltage_dv", kRangeBatteryPackVoltageDv.min,    kRangeBatteryPackVoltageDv.max},
    {BATTERY_CELL_MAX_VOLTAGE_MV, "cell_max_voltage_mv", kRangeBatteryCellMaxVoltageMv.min, kRangeBatteryCellMaxVoltageMv.max},
    {BATTERY_CELL_MIN_VOLTAGE_MV, "cell_min_voltage_mv", kRangeBatteryCellMinVoltageMv.min, kRangeBatteryCellMinVoltageMv.max},
    {BATTERY_LED_MODE,            "led_mode",            kRangeBatteryLedMode.min,          kRangeBatteryLedMode.max},
};

constexpr FloatFieldDescriptor kBatteryFloatFieldDescriptors[] = {
    {BATTERY_MAX_CHARGE_CURRENT_A,    "max_charge_current_a",    kRangeBatteryCurrentA.min, kRangeBatteryCurrentA.max},
    {BATTERY_MAX_DISCHARGE_CURRENT_A, "max_discharge_current_a", kRangeBatteryCurrentA.min, kRangeBatteryCurrentA.max},
};

// --- Power ---
constexpr UIntFieldDescriptor kPowerUIntFieldDescriptors[] = {
    {POWER_CHARGE_W,              "power_charge_w",              kRangePowerWatts.min,               kRangePowerWatts.max},
    {POWER_DISCHARGE_W,           "power_discharge_w",           kRangePowerWatts.min,               kRangePowerWatts.max},
    {POWER_MAX_PRECHARGE_MS,      "power_max_precharge_ms",      kRangePowerMaxPrechargeMs.min,      kRangePowerMaxPrechargeMs.max},
    {POWER_PRECHARGE_DURATION_MS, "power_precharge_duration_ms", kRangePowerPrechargeDurationMs.min, kRangePowerPrechargeDurationMs.max},
};

// --- Inverter ---
constexpr UIntFieldDescriptor kInverterUIntFieldDescriptors[] = {
    {INVERTER_CELLS,            "inverter_cells",            kRangeInverterCells.min,        kRangeInverterCells.max},
    {INVERTER_MODULES,          "inverter_modules",          kRangeInverterCells.min,        kRangeInverterCells.max},
    {INVERTER_CELLS_PER_MODULE, "inverter_cells_per_module", kRangeInverterCells.min,        kRangeInverterCells.max},
    {INVERTER_VOLTAGE_LEVEL,    "inverter_voltage_level",    kRangeInverterVoltageLevel.min, kRangeInverterVoltageLevel.max},
    {INVERTER_CAPACITY_AH,      "inverter_capacity_ah",      kRangeInverterCapacityAh.min,   kRangeInverterCapacityAh.max},
    {INVERTER_BATTERY_TYPE,     "inverter_battery_type",     kRangeInverterBatteryType.min,  kRangeInverterBatteryType.max},
};

// --- CAN ---
constexpr UIntFieldDescriptor kCanUIntFieldDescriptors[] = {
    {CAN_FREQUENCY_KHZ,          "can_frequency_khz",          kRangeCanFrequencyKhz.min,      kRangeCanFrequencyKhz.max},
    {CAN_FD_FREQUENCY_MHZ,       "can_fd_frequency_mhz",       kRangeCanFdFrequencyMhz.min,    kRangeCanFdFrequencyMhz.max},
    {CAN_SOFAR_ID,               "can_sofar_id",               kRangeCanSofarId.min,           kRangeCanSofarId.max},
    {CAN_PYLON_SEND_INTERVAL_MS, "can_pylon_send_interval_ms", kRangeCanPylonSendIntervalMs.min, kRangeCanPylonSendIntervalMs.max},
};

// --- Contactor (bool fields excluded — any 0/1 is valid) ---
constexpr UIntFieldDescriptor kContactorUIntFieldDescriptors[] = {
    {CONTACTOR_PWM_FREQUENCY_HZ, "contactor_pwm_frequency_hz", kRangeContactorPwmFrequencyHz.min, kRangeContactorPwmFrequencyHz.max},
};

constexpr const char* kBatteryChemistryLabels[] = {"NCA", "NMC", "LFP", "LTO"};

// Generic descriptor-driven range-check for a uint32 value.
// Returns false (with error log) if a matching descriptor reports out-of-range.
// Returns true (pass-through) for field IDs not present in the table.
template<size_t N>
bool validate_uint_field(uint8_t field_id, uint32_t value,
                         const UIntFieldDescriptor (&table)[N]) {
    for (const auto& d : table) {
        if (d.field_id == field_id) {
            if (value < d.min_value || value > d.max_value) {
                LOG_ERROR("SETTINGS",
                          "Invalid %s: %u (must be %u-%u)",
                          d.name, value, d.min_value, d.max_value);
                return false;
            }
            return true;
        }
    }
    return true;
}

// Generic descriptor-driven range-check for a float value.
template<size_t N>
bool validate_float_field(uint8_t field_id, float value,
                          const FloatFieldDescriptor (&table)[N]) {
    for (const auto& d : table) {
        if (d.field_id == field_id) {
            if (value < d.min_value || value > d.max_value) {
                LOG_ERROR("SETTINGS",
                          "Invalid %s: %.2f (must be %.2f-%.2f)",
                          d.name, value, d.min_value, d.max_value);
                return false;
            }
            return true;
        }
    }
    return true;
}

// Convenience wrappers retained for the (unchanged) battery setter.
inline bool validate_battery_uint_field(uint8_t field_id, uint32_t value) {
    return validate_uint_field(field_id, value, kBatteryUIntFieldDescriptors);
}
inline bool validate_battery_float_field(uint8_t field_id, float value) {
    return validate_float_field(field_id, value, kBatteryFloatFieldDescriptors);
}

} // namespace

// ---------------------------------------------------------------------------
// Battery field setter
// ---------------------------------------------------------------------------

bool SettingsManager::save_battery_setting(uint8_t field_id,
                                           uint32_t value_uint32,
                                           float value_float,
                                           const char* value_string) {
    (void)value_string;  // Reserved for future string fields
    bool changed = false;

    switch (field_id) {
        case BATTERY_CAPACITY_WH:
            if (!validate_battery_uint_field(field_id, value_uint32)) {
                return false;
            }
            battery_capacity_wh_ = value_uint32;
            changed = true;
            LOG_INFO("SETTINGS", "Battery capacity updated: %dWh",
                     battery_capacity_wh_);
            break;

        case BATTERY_MAX_VOLTAGE_MV:
            if (!validate_battery_uint_field(field_id, value_uint32)) {
                return false;
            }
            battery_max_voltage_mv_ = value_uint32;
            changed = true;
            LOG_INFO("SETTINGS", "Max voltage updated: %dmV",
                     battery_max_voltage_mv_);
            break;

        case BATTERY_MIN_VOLTAGE_MV:
            if (!validate_battery_uint_field(field_id, value_uint32)) {
                return false;
            }
            battery_min_voltage_mv_ = value_uint32;
            changed = true;
            LOG_INFO("SETTINGS", "Min voltage updated: %dmV",
                     battery_min_voltage_mv_);
            break;

        case BATTERY_MAX_CHARGE_CURRENT_A:
            if (!validate_battery_float_field(field_id, value_float)) {
                return false;
            }
            battery_max_charge_current_a_ = value_float;
            changed = true;
            LOG_INFO("SETTINGS", "Max charge current updated: %.1fA",
                     battery_max_charge_current_a_);
            break;

        case BATTERY_MAX_DISCHARGE_CURRENT_A:
            if (!validate_battery_float_field(field_id, value_float)) {
                return false;
            }
            battery_max_discharge_current_a_ = value_float;
            changed = true;
            LOG_INFO("SETTINGS", "Max discharge current updated: %.1fA",
                     battery_max_discharge_current_a_);
            break;

        case BATTERY_SOC_HIGH_LIMIT:
            if (!validate_battery_uint_field(field_id, value_uint32)) {
                return false;
            }
            battery_soc_high_limit_ = value_uint32;
            changed = true;
            LOG_INFO("SETTINGS", "SOC high limit updated: %d%%",
                     battery_soc_high_limit_);
            break;

        case BATTERY_SOC_LOW_LIMIT:
            if (!validate_battery_uint_field(field_id, value_uint32)) {
                return false;
            }
            battery_soc_low_limit_ = value_uint32;
            changed = true;
            LOG_INFO("SETTINGS", "SOC low limit updated: %d%%",
                     battery_soc_low_limit_);
            break;

        case BATTERY_CELL_COUNT:
            if (!validate_battery_uint_field(field_id, value_uint32)) {
                return false;
            }
            battery_cell_count_ = value_uint32;
            changed = true;
            LOG_INFO("SETTINGS", "Cell count updated: %dS",
                     battery_cell_count_);
            break;

        case BATTERY_CHEMISTRY:
            if (!validate_battery_uint_field(field_id, value_uint32)) {
                return false;
            }
            battery_chemistry_ = value_uint32;
            changed = true;
            LOG_INFO("SETTINGS", "Chemistry updated: %s",
                     kBatteryChemistryLabels[battery_chemistry_]);
            break;

        case BATTERY_DOUBLE_ENABLED:
            battery_double_enabled_ = value_uint32 ? true : false;
            changed = true;
            LOG_INFO("SETTINGS", "Double battery updated: %s",
                     battery_double_enabled_ ? "ENABLED" : "DISABLED");
            break;

        case BATTERY_PACK_MAX_VOLTAGE_DV:
            if (!validate_battery_uint_field(field_id, value_uint32)) {
                return false;
            }
            battery_pack_max_voltage_dV_ = value_uint32;
            changed = true;
            LOG_INFO("SETTINGS", "Pack max voltage updated: %u dV",
                     battery_pack_max_voltage_dV_);
            break;

        case BATTERY_PACK_MIN_VOLTAGE_DV:
            if (!validate_battery_uint_field(field_id, value_uint32)) {
                return false;
            }
            battery_pack_min_voltage_dV_ = value_uint32;
            changed = true;
            LOG_INFO("SETTINGS", "Pack min voltage updated: %u dV",
                     battery_pack_min_voltage_dV_);
            break;

        case BATTERY_CELL_MAX_VOLTAGE_MV:
            if (!validate_battery_uint_field(field_id, value_uint32)) {
                return false;
            }
            battery_cell_max_voltage_mV_ = value_uint32;
            changed = true;
            LOG_INFO("SETTINGS", "Cell max voltage updated: %u mV",
                     battery_cell_max_voltage_mV_);
            break;

        case BATTERY_CELL_MIN_VOLTAGE_MV:
            if (!validate_battery_uint_field(field_id, value_uint32)) {
                return false;
            }
            battery_cell_min_voltage_mV_ = value_uint32;
            changed = true;
            LOG_INFO("SETTINGS", "Cell min voltage updated: %u mV",
                     battery_cell_min_voltage_mV_);
            break;

        case BATTERY_SOC_ESTIMATED:
            battery_soc_estimated_ = value_uint32 ? true : false;
            changed = true;
            LOG_INFO("SETTINGS", "SOC estimation updated: %s",
                     battery_soc_estimated_ ? "ENABLED" : "DISABLED");
            break;

        case BATTERY_LED_MODE:
            if (!validate_battery_uint_field(field_id, value_uint32)) {
                return false;
            }
            battery_led_mode_ = value_uint32;
            datalayer.battery.status.led_mode =
                static_cast<led_mode_enum>(battery_led_mode_);
            changed = true;
            LOG_INFO("SETTINGS", "LED mode updated: %u", battery_led_mode_);
            break;

        default:
            LOG_ERROR("SETTINGS", "Unknown battery field ID: %d", field_id);
            return false;
    }

    if (changed) {
        increment_battery_version();
        const bool saved = save_battery_settings();
        if (saved) {
            send_settings_changed_notification(SETTINGS_BATTERY,
                                               battery_settings_version_);

            // Apply LED policy changes immediately at runtime (if connected).
            if (field_id == BATTERY_LED_MODE) {
                const esp_err_t led_result =
                    led_publish_current_state(true, nullptr);
                if (led_result != ESP_OK &&
                    led_result != ESP_ERR_INVALID_STATE &&
                    led_result != ESP_ERR_INVALID_ARG) {
                    LOG_WARN("SETTINGS",
                             "LED replay after LEDMODE update failed: %s",
                             esp_err_to_name(led_result));
                }
            }
        }
        return saved;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Power field setter
// ---------------------------------------------------------------------------

bool SettingsManager::save_power_setting(uint8_t field_id,
                                         uint32_t value_uint32) {
    bool changed = false;

    switch (field_id) {
        case POWER_CHARGE_W:
            if (!validate_uint_field(field_id, value_uint32, kPowerUIntFieldDescriptors)) {
                return false;
            }
            power_charge_w_ = value_uint32;
            changed = true;
            LOG_INFO("SETTINGS", "Power charge limit updated: %uW",
                     static_cast<unsigned>(power_charge_w_));
            break;
        case POWER_DISCHARGE_W:
            if (!validate_uint_field(field_id, value_uint32, kPowerUIntFieldDescriptors)) {
                return false;
            }
            power_discharge_w_ = value_uint32;
            changed = true;
            LOG_INFO("SETTINGS", "Power discharge limit updated: %uW",
                     static_cast<unsigned>(power_discharge_w_));
            break;
        case POWER_MAX_PRECHARGE_MS:
            if (!validate_uint_field(field_id, value_uint32, kPowerUIntFieldDescriptors)) {
                return false;
            }
            power_max_precharge_ms_ = value_uint32;
            changed = true;
            LOG_INFO("SETTINGS", "Max precharge duration updated: %ums",
                     static_cast<unsigned>(power_max_precharge_ms_));
            break;
        case POWER_PRECHARGE_DURATION_MS:
            if (!validate_uint_field(field_id, value_uint32, kPowerUIntFieldDescriptors)) {
                return false;
            }
            power_precharge_duration_ms_ = value_uint32;
            changed = true;
            LOG_INFO("SETTINGS", "Precharge duration updated: %ums",
                     static_cast<unsigned>(power_precharge_duration_ms_));
            break;
        default:
            LOG_ERROR("SETTINGS", "Unknown power field ID: %d", field_id);
            return false;
    }

    if (changed) {
        increment_power_version();
        const bool saved = save_power_settings();
        if (saved) {
            send_settings_changed_notification(SETTINGS_POWER,
                                               power_settings_version_);
        }
        return saved;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Inverter field setter
// ---------------------------------------------------------------------------

bool SettingsManager::save_inverter_setting(uint8_t field_id,
                                            uint32_t value_uint32) {
    bool changed = false;

    switch (field_id) {
        case INVERTER_CELLS:
            if (!validate_uint_field(field_id, value_uint32, kInverterUIntFieldDescriptors)) {
                return false;
            }
            inverter_cells_ = value_uint32;
            changed = true;
            LOG_INFO("SETTINGS", "Inverter cells updated: %u",
                     static_cast<unsigned>(inverter_cells_));
            break;
        case INVERTER_MODULES:
            if (!validate_uint_field(field_id, value_uint32, kInverterUIntFieldDescriptors)) {
                return false;
            }
            inverter_modules_ = value_uint32;
            changed = true;
            LOG_INFO("SETTINGS", "Inverter modules updated: %u",
                     static_cast<unsigned>(inverter_modules_));
            break;
        case INVERTER_CELLS_PER_MODULE:
            if (!validate_uint_field(field_id, value_uint32, kInverterUIntFieldDescriptors)) {
                return false;
            }
            inverter_cells_per_module_ = value_uint32;
            changed = true;
            LOG_INFO("SETTINGS", "Inverter cells-per-module updated: %u",
                     static_cast<unsigned>(inverter_cells_per_module_));
            break;
        case INVERTER_VOLTAGE_LEVEL:
            if (!validate_uint_field(field_id, value_uint32, kInverterUIntFieldDescriptors)) {
                return false;
            }
            inverter_voltage_level_ = value_uint32;
            changed = true;
            LOG_INFO("SETTINGS", "Inverter voltage level updated: %u",
                     static_cast<unsigned>(inverter_voltage_level_));
            break;
        case INVERTER_CAPACITY_AH:
            if (!validate_uint_field(field_id, value_uint32, kInverterUIntFieldDescriptors)) {
                return false;
            }
            inverter_capacity_ah_ = value_uint32;
            changed = true;
            LOG_INFO("SETTINGS", "Inverter capacity updated: %uAh",
                     static_cast<unsigned>(inverter_capacity_ah_));
            break;
        case INVERTER_BATTERY_TYPE:
            if (!validate_uint_field(field_id, value_uint32, kInverterUIntFieldDescriptors)) {
                return false;
            }
            inverter_battery_type_ = value_uint32;
            changed = true;
            LOG_INFO("SETTINGS", "Inverter battery type updated: %u",
                     static_cast<unsigned>(inverter_battery_type_));
            break;
        default:
            LOG_ERROR("SETTINGS", "Unknown inverter field ID: %d", field_id);
            return false;
    }

    if (changed) {
        increment_inverter_version();
        const bool saved = save_inverter_settings();
        if (saved) {
            send_settings_changed_notification(SETTINGS_INVERTER,
                                               inverter_settings_version_);
        }
        return saved;
    }

    return true;
}

// ---------------------------------------------------------------------------
// CAN field setter
// ---------------------------------------------------------------------------

bool SettingsManager::save_can_setting(uint8_t field_id,
                                       uint32_t value_uint32) {
    bool changed = false;

    switch (field_id) {
        case CAN_FREQUENCY_KHZ:
            if (!validate_uint_field(field_id, value_uint32, kCanUIntFieldDescriptors)) {
                return false;
            }
            can_frequency_khz_ = value_uint32;
            changed = true;
            LOG_INFO("SETTINGS", "CAN frequency updated: %ukHz",
                     static_cast<unsigned>(can_frequency_khz_));
            break;
        case CAN_FD_FREQUENCY_MHZ:
            if (!validate_uint_field(field_id, value_uint32, kCanUIntFieldDescriptors)) {
                return false;
            }
            can_fd_frequency_mhz_ = value_uint32;
            changed = true;
            LOG_INFO("SETTINGS", "CAN-FD frequency updated: %uMHz",
                     static_cast<unsigned>(can_fd_frequency_mhz_));
            break;
        case CAN_SOFAR_ID:
            if (!validate_uint_field(field_id, value_uint32, kCanUIntFieldDescriptors)) {
                return false;
            }
            can_sofar_id_ = value_uint32;
            changed = true;
            LOG_INFO("SETTINGS", "CAN Sofar ID updated: 0x%04X",
                     static_cast<unsigned>(can_sofar_id_));
            break;
        case CAN_PYLON_SEND_INTERVAL_MS:
            if (!validate_uint_field(field_id, value_uint32, kCanUIntFieldDescriptors)) {
                return false;
            }
            can_pylon_send_interval_ms_ = value_uint32;
            changed = true;
            LOG_INFO("SETTINGS", "CAN Pylon send interval updated: %ums",
                     static_cast<unsigned>(can_pylon_send_interval_ms_));
            break;
        default:
            LOG_ERROR("SETTINGS", "Unknown CAN field ID: %d", field_id);
            return false;
    }

    if (changed) {
        increment_can_version();
        const bool saved = save_can_settings();
        if (saved) {
            send_settings_changed_notification(SETTINGS_CAN,
                                               can_settings_version_);
        }
        return saved;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Contactor field setter
// ---------------------------------------------------------------------------

bool SettingsManager::save_contactor_setting(uint8_t field_id,
                                             uint32_t value_uint32) {
    bool changed = false;

    switch (field_id) {
        case CONTACTOR_CONTROL_ENABLED:
            contactor_control_enabled_ = value_uint32 ? true : false;
            changed = true;
            LOG_INFO("SETTINGS", "Contactor control updated: %s",
                     contactor_control_enabled_ ? "ENABLED" : "DISABLED");
            break;
        case CONTACTOR_NC_MODE:
            contactor_nc_mode_ = value_uint32 ? true : false;
            changed = true;
            LOG_INFO("SETTINGS", "Contactor NC mode updated: %s",
                     contactor_nc_mode_ ? "NC" : "NO");
            break;
        case CONTACTOR_PWM_FREQUENCY_HZ:
            if (!validate_uint_field(field_id, value_uint32, kContactorUIntFieldDescriptors)) {
                return false;
            }
            contactor_pwm_frequency_hz_ = value_uint32;
            changed = true;
            LOG_INFO("SETTINGS", "Contactor PWM frequency updated: %uHz",
                     static_cast<unsigned>(contactor_pwm_frequency_hz_));
            break;
        default:
            LOG_ERROR("SETTINGS", "Unknown contactor field ID: %d", field_id);
            return false;
    }

    if (changed) {
        increment_contactor_version();
        const bool saved = save_contactor_settings();
        if (saved) {
            send_settings_changed_notification(SETTINGS_CONTACTOR,
                                               contactor_settings_version_);
        }
        return saved;
    }

    return true;
}
