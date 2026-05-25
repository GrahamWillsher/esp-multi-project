#pragma once

#include <cstdint>

namespace TxComponentCatalogHandlers {

/**
 * @brief Transport-agnostic result of a component apply operation.
 * Used by both the ESP-NOW and MQTT command paths.
 */
struct ComponentApplyResult {
    bool success{false};
    bool reboot_required{false};
    uint8_t persisted_mask{0};
    uint8_t battery_type{0};
    uint8_t inverter_type{0};
    uint8_t battery_interface{0};
    uint8_t inverter_interface{0};
    uint32_t settings_version{0};
    char message[64]{};
};

/**
 * @brief Apply component selections (battery/inverter type and interface).
 *
 * Called by both the ESP-NOW queue handler and the MQTT command handler so
 * the core business logic is transport-agnostic.
 *
 * @param apply_mask  Bitmask of component_apply_mask_t fields to update.
 * @param battery_type     Desired battery profile type (ignored if bit not set).
 * @param inverter_type    Desired inverter type (ignored if bit not set).
 * @param battery_interface  Desired battery comm interface (ignored if bit not set).
 * @param inverter_interface Desired inverter comm interface (ignored if bit not set).
 * @return ComponentApplyResult describing what was applied.
 */
ComponentApplyResult apply_components(uint8_t apply_mask,
                                      uint8_t battery_type,
                                      uint8_t inverter_type,
                                      uint8_t battery_interface,
                                      uint8_t inverter_interface);

} // namespace TxComponentCatalogHandlers
