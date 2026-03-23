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
            if (value_uint32 >= 1000 && value_uint32 <= 1000000) {
                battery_capacity_wh_ = value_uint32;
                changed = true;
                LOG_INFO("SETTINGS", "Battery capacity updated: %dWh",
                         battery_capacity_wh_);
            } else {
                LOG_ERROR("SETTINGS",
                          "Invalid capacity: %d (must be 1000-1000000)",
                          value_uint32);
                return false;
            }
            break;

        case BATTERY_MAX_VOLTAGE_MV:
            if (value_uint32 >= 30000 && value_uint32 <= 100000) {
                battery_max_voltage_mv_ = value_uint32;
                changed = true;
                LOG_INFO("SETTINGS", "Max voltage updated: %dmV",
                         battery_max_voltage_mv_);
            } else {
                LOG_ERROR("SETTINGS", "Invalid max voltage: %d", value_uint32);
                return false;
            }
            break;

        case BATTERY_MIN_VOLTAGE_MV:
            if (value_uint32 >= 20000 && value_uint32 <= 80000) {
                battery_min_voltage_mv_ = value_uint32;
                changed = true;
                LOG_INFO("SETTINGS", "Min voltage updated: %dmV",
                         battery_min_voltage_mv_);
            } else {
                LOG_ERROR("SETTINGS", "Invalid min voltage: %d", value_uint32);
                return false;
            }
            break;

        case BATTERY_MAX_CHARGE_CURRENT_A:
            if (value_float >= 0.0f && value_float <= 500.0f) {
                battery_max_charge_current_a_ = value_float;
                changed = true;
                LOG_INFO("SETTINGS", "Max charge current updated: %.1fA",
                         battery_max_charge_current_a_);
            } else {
                LOG_ERROR("SETTINGS", "Invalid charge current: %.1f",
                          value_float);
                return false;
            }
            break;

        case BATTERY_MAX_DISCHARGE_CURRENT_A:
            if (value_float >= 0.0f && value_float <= 500.0f) {
                battery_max_discharge_current_a_ = value_float;
                changed = true;
                LOG_INFO("SETTINGS", "Max discharge current updated: %.1fA",
                         battery_max_discharge_current_a_);
            } else {
                LOG_ERROR("SETTINGS", "Invalid discharge current: %.1f",
                          value_float);
                return false;
            }
            break;

        case BATTERY_SOC_HIGH_LIMIT:
            if (value_uint32 >= 50 && value_uint32 <= 100) {
                battery_soc_high_limit_ = value_uint32;
                changed = true;
                LOG_INFO("SETTINGS", "SOC high limit updated: %d%%",
                         battery_soc_high_limit_);
            } else {
                LOG_ERROR("SETTINGS", "Invalid SOC high: %d", value_uint32);
                return false;
            }
            break;

        case BATTERY_SOC_LOW_LIMIT:
            if (value_uint32 >= 0 && value_uint32 <= 50) {
                battery_soc_low_limit_ = value_uint32;
                changed = true;
                LOG_INFO("SETTINGS", "SOC low limit updated: %d%%",
                         battery_soc_low_limit_);
            } else {
                LOG_ERROR("SETTINGS", "Invalid SOC low: %d", value_uint32);
                return false;
            }
            break;

        case BATTERY_CELL_COUNT:
            if (value_uint32 >= 4 && value_uint32 <= 32) {
                battery_cell_count_ = value_uint32;
                changed = true;
                LOG_INFO("SETTINGS", "Cell count updated: %dS",
                         battery_cell_count_);
            } else {
                LOG_ERROR("SETTINGS", "Invalid cell count: %d", value_uint32);
                return false;
            }
            break;

        case BATTERY_CHEMISTRY:
            if (value_uint32 <= 3) {  // 0=NCA, 1=NMC, 2=LFP, 3=LTO
                battery_chemistry_ = value_uint32;
                changed = true;
                const char* chem[] = {"NCA", "NMC", "LFP", "LTO"};
                LOG_INFO("SETTINGS", "Chemistry updated: %s",
                         chem[battery_chemistry_]);
            } else {
                LOG_ERROR("SETTINGS", "Invalid chemistry: %d", value_uint32);
                return false;
            }
            break;

        case BATTERY_DOUBLE_ENABLED:
            battery_double_enabled_ = value_uint32 ? true : false;
            changed = true;
            LOG_INFO("SETTINGS", "Double battery updated: %s",
                     battery_double_enabled_ ? "ENABLED" : "DISABLED");
            break;

        case BATTERY_PACK_MAX_VOLTAGE_DV:
            if (value_uint32 >= 100 && value_uint32 <= 10000) {
                battery_pack_max_voltage_dV_ = value_uint32;
                changed = true;
                LOG_INFO("SETTINGS", "Pack max voltage updated: %u dV",
                         battery_pack_max_voltage_dV_);
            } else {
                LOG_ERROR("SETTINGS", "Invalid pack max voltage: %d",
                          value_uint32);
                return false;
            }
            break;

        case BATTERY_PACK_MIN_VOLTAGE_DV:
            if (value_uint32 >= 100 && value_uint32 <= 10000) {
                battery_pack_min_voltage_dV_ = value_uint32;
                changed = true;
                LOG_INFO("SETTINGS", "Pack min voltage updated: %u dV",
                         battery_pack_min_voltage_dV_);
            } else {
                LOG_ERROR("SETTINGS", "Invalid pack min voltage: %d",
                          value_uint32);
                return false;
            }
            break;

        case BATTERY_CELL_MAX_VOLTAGE_MV:
            if (value_uint32 >= 1500 && value_uint32 <= 5000) {
                battery_cell_max_voltage_mV_ = value_uint32;
                changed = true;
                LOG_INFO("SETTINGS", "Cell max voltage updated: %u mV",
                         battery_cell_max_voltage_mV_);
            } else {
                LOG_ERROR("SETTINGS", "Invalid cell max voltage: %d",
                          value_uint32);
                return false;
            }
            break;

        case BATTERY_CELL_MIN_VOLTAGE_MV:
            if (value_uint32 >= 1000 && value_uint32 <= 4500) {
                battery_cell_min_voltage_mV_ = value_uint32;
                changed = true;
                LOG_INFO("SETTINGS", "Cell min voltage updated: %u mV",
                         battery_cell_min_voltage_mV_);
            } else {
                LOG_ERROR("SETTINGS", "Invalid cell min voltage: %d",
                          value_uint32);
                return false;
            }
            break;

        case BATTERY_SOC_ESTIMATED:
            battery_soc_estimated_ = value_uint32 ? true : false;
            changed = true;
            LOG_INFO("SETTINGS", "SOC estimation updated: %s",
                     battery_soc_estimated_ ? "ENABLED" : "DISABLED");
            break;

        case BATTERY_LED_MODE:
            if (value_uint32 <= 2) {  // 0=Classic, 1=Energy Flow, 2=Heartbeat
                battery_led_mode_ = value_uint32;
                datalayer.battery.status.led_mode =
                    static_cast<led_mode_enum>(battery_led_mode_);
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
        const bool saved = save_contactor_settings();
        if (saved) {
            send_settings_changed_notification(SETTINGS_CONTACTOR,
                                               contactor_settings_version_);
        }
        return saved;
    }

    return true;
}
