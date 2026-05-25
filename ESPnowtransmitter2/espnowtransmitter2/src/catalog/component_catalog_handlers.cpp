#include "component_catalog_handlers.h"

#include "../system_settings.h"
#include "../datalayer/static_data.h"
#include "../network/mqtt_manager.h"
#include "../config/logging_config.h"

#if CONFIG_CAN_ENABLED
#include "../battery/battery_manager.h"
#include "../battery_emulator/battery/BATTERIES.h"
#include "../battery_emulator/battery/Battery.h"
#include "../battery_emulator/inverter/INVERTERS.h"
#include "../battery_emulator/inverter/InverterProtocol.h"
#endif

#include <Arduino.h>
#include <Preferences.h>
#include <firmware_version.h>
#include <runtime_common_utils/crc_utils.h>
#include <runtime_common_utils/mac_utils.h>
#include <vector>
#include <cstddef>
#include <cstring>
#include <esp32common/contracts/shared_contracts.h>

namespace {

constexpr const char* kComponentInterfaceNamespace = "component_if";
constexpr const char* kBatteryInterfaceKey = "battery_if";
constexpr const char* kInverterInterfaceKey = "inverter_if";

bool is_valid_interface_id(uint8_t id) {
    return id <= 5;
}

void load_interface_selection(uint8_t& battery_interface, uint8_t& inverter_interface) {
    battery_interface = 0;
    inverter_interface = 0;

    Preferences prefs;
    if (!prefs.begin(kComponentInterfaceNamespace, true)) {
        return;
    }

    battery_interface = static_cast<uint8_t>(prefs.getUInt(kBatteryInterfaceKey, 0));
    inverter_interface = static_cast<uint8_t>(prefs.getUInt(kInverterInterfaceKey, 0));
    prefs.end();
}

bool save_interface_selection(uint8_t battery_interface, uint8_t inverter_interface) {
    Preferences prefs;
    if (!prefs.begin(kComponentInterfaceNamespace, false)) {
        return false;
    }

    const size_t batt_written = prefs.putUInt(kBatteryInterfaceKey, battery_interface);
    const size_t inv_written = prefs.putUInt(kInverterInterfaceKey, inverter_interface);
    prefs.end();

    if (batt_written != sizeof(uint32_t) || inv_written != sizeof(uint32_t)) {
        return false;
    }

    uint8_t verify_batt = 0;
    uint8_t verify_inv = 0;
    load_interface_selection(verify_batt, verify_inv);
    return verify_batt == battery_interface && verify_inv == inverter_interface;
}

#if CONFIG_CAN_ENABLED
bool is_supported_battery_selection(uint8_t raw_type) {
    const BatteryType type = static_cast<BatteryType>(raw_type);
    const std::vector<BatteryType> supported_types = supported_battery_types();
    return std::find(supported_types.begin(), supported_types.end(), type) != supported_types.end();
}

bool is_supported_inverter_selection(uint8_t raw_type) {
    const InverterProtocolType type = static_cast<InverterProtocolType>(raw_type);
    const std::vector<InverterProtocolType> supported_types = supported_inverter_protocols();
    return std::find(supported_types.begin(), supported_types.end(), type) != supported_types.end();
}

#endif

} // namespace

namespace TxComponentCatalogHandlers {

ComponentApplyResult apply_components(uint8_t apply_mask,
                                      uint8_t battery_type,
                                      uint8_t inverter_type,
                                      uint8_t battery_interface,
                                      uint8_t inverter_interface) {
    ComponentApplyResult result{};
    result.settings_version = static_cast<uint32_t>(SystemSettings::instance().get_config_version());
    strncpy(result.message, "Apply failed", sizeof(result.message) - 1);

    SystemSettings& settings = SystemSettings::instance();

    uint8_t current_battery_interface = 0;
    uint8_t current_inverter_interface = 0;
    load_interface_selection(current_battery_interface, current_inverter_interface);

    result.battery_type      = settings.get_battery_profile_type();
    result.inverter_type     = settings.get_inverter_type();
    result.battery_interface = current_battery_interface;
    result.inverter_interface = current_inverter_interface;

    const bool wants_battery_type       = (apply_mask & component_apply_battery_type) != 0;
    const bool wants_inverter_type      = (apply_mask & component_apply_inverter_type) != 0;
    const bool wants_battery_interface  = (apply_mask & component_apply_battery_interface) != 0;
    const bool wants_inverter_interface = (apply_mask & component_apply_inverter_interface) != 0;

    if (apply_mask == 0) {
        result.success = true;
        strncpy(result.message, "No changes requested", sizeof(result.message) - 1);
        return result;
    }

#if CONFIG_CAN_ENABLED
    if (wants_battery_type && !is_supported_battery_selection(battery_type)) {
        strncpy(result.message, "Unsupported battery type", sizeof(result.message) - 1);
        return result;
    }

    if (wants_inverter_type && !is_supported_inverter_selection(inverter_type)) {
        strncpy(result.message, "Unsupported inverter type", sizeof(result.message) - 1);
        return result;
    }
#endif

    if (wants_battery_interface && !is_valid_interface_id(battery_interface)) {
        strncpy(result.message, "Invalid battery interface", sizeof(result.message) - 1);
        return result;
    }

    if (wants_inverter_interface && !is_valid_interface_id(inverter_interface)) {
        strncpy(result.message, "Invalid inverter interface", sizeof(result.message) - 1);
        return result;
    }

    bool changed_any = false;
    bool battery_type_changed = false;
    bool inverter_type_changed = false;

    if (wants_battery_type) {
        if (settings.get_battery_profile_type() == battery_type) {
            result.persisted_mask |= component_apply_battery_type;
        } else if (settings.set_battery_profile_type(battery_type)) {
            result.persisted_mask |= component_apply_battery_type;
            changed_any = true;
            battery_type_changed = true;
            result.battery_type = battery_type;
        } else {
            strncpy(result.message, "Failed saving battery type", sizeof(result.message) - 1);
            return result;
        }
    }

    if (wants_inverter_type) {
        if (settings.get_inverter_type() == inverter_type) {
            result.persisted_mask |= component_apply_inverter_type;
        } else if (settings.set_inverter_type(inverter_type)) {
            result.persisted_mask |= component_apply_inverter_type;
            changed_any = true;
            inverter_type_changed = true;
            result.inverter_type = inverter_type;
        } else {
            strncpy(result.message, "Failed saving inverter type", sizeof(result.message) - 1);
            return result;
        }
    }

    const uint8_t target_battery_interface  = wants_battery_interface  ? battery_interface  : current_battery_interface;
    const uint8_t target_inverter_interface = wants_inverter_interface ? inverter_interface : current_inverter_interface;

    if (wants_battery_interface || wants_inverter_interface) {
        const bool interface_changed =
            (target_battery_interface  != current_battery_interface) ||
            (target_inverter_interface != current_inverter_interface);

        if (!interface_changed || save_interface_selection(target_battery_interface, target_inverter_interface)) {
            if (wants_battery_interface)  { result.persisted_mask |= component_apply_battery_interface; }
            if (wants_inverter_interface) { result.persisted_mask |= component_apply_inverter_interface; }
            changed_any = changed_any || interface_changed;
            result.battery_interface  = target_battery_interface;
            result.inverter_interface = target_inverter_interface;
        } else {
            strncpy(result.message, "Failed saving interfaces", sizeof(result.message) - 1);
            return result;
        }
    }

#if CONFIG_CAN_ENABLED
    if (wants_battery_type) {
        user_selected_battery_type = static_cast<BatteryType>(result.battery_type);
        if (!BatteryManager::instance().is_primary_battery_initialized()) {
            BatteryManager::instance().init_primary_battery(static_cast<BatteryType>(result.battery_type));
        }
    }

    if (wants_inverter_type) {
        user_selected_inverter_protocol = static_cast<InverterProtocolType>(result.inverter_type);
        if (!BatteryManager::instance().is_inverter_initialized()) {
            BatteryManager::instance().init_inverter(static_cast<InverterProtocolType>(result.inverter_type));
        }
    }
#endif

    if (battery_type_changed) {
        StaticData::update_battery_specs(result.battery_type);
    }
    if (inverter_type_changed) {
        StaticData::update_inverter_specs(result.inverter_type);
    }

    if ((battery_type_changed || inverter_type_changed) && MqttManager::instance().is_connected()) {
        if (battery_type_changed) {
            MqttManager::instance().publish_battery_specs();
            MqttManager::instance().publish_battery_type_catalog();
        }
        if (inverter_type_changed) {
            MqttManager::instance().publish_inverter_specs();
            MqttManager::instance().publish_inverter_type_catalog();
        }
        MqttManager::instance().publish_static_specs();
    }

    result.success          = (result.persisted_mask == apply_mask) ? true : false;
    result.reboot_required  = (changed_any && result.success);
    result.settings_version = static_cast<uint32_t>(settings.get_config_version());

    if (result.success) {
        strncpy(result.message,
                result.reboot_required ? "Persisted - reboot required" : "No reboot required",
                sizeof(result.message) - 1);
    } else {
        strncpy(result.message, "Persisted mask mismatch", sizeof(result.message) - 1);
    }

    return result;
}

} // namespace TxComponentCatalogHandlers

