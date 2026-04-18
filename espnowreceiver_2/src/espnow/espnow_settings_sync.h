#pragma once

#include <cstdint>
#include <esp32common/espnow/common.h>

/**
 * @file espnow_settings_sync.h
 * @brief ESP-NOW settings synchronisation — ACK/change handler API (espnowreceiver_2).
 *
 * Provides the same EspnowSettingsSync namespace and Snapshot contract as
 * espnowreceiver_LCD, so consumers in both projects have an identical API surface.
 *
 * This implementation additionally performs:
 *  - Granular category re-fetch after successful/failed ACK
 *  - BatterySettingsCache version tracking
 *  - ComponentApplyTracker::on_ack() forwarding
 */

namespace EspnowSettingsSync {

/** @brief Atomic snapshot of the most recently processed sync event. */
struct Snapshot {
    uint8_t  last_category              = 0xFF;
    uint8_t  last_field_id              = 0xFF;
    bool     last_update_success        = false;
    uint32_t settings_version           = 0;

    uint8_t  changed_category           = 0xFF;
    uint32_t changed_version            = 0;

    uint32_t last_component_apply_request_id = 0;
    bool     component_apply_success         = false;
    bool     component_apply_reboot_required = false;
    uint32_t component_settings_version      = 0;
};

/**
 * @brief Process a settings-update ACK message from the transmitter.
 * Stores snapshot, triggers granular category re-fetch, and updates
 * BatterySettingsCache version if applicable.
 * @return true on success, false if message was malformed.
 */
bool handle_settings_update_ack(const espnow_queue_msg_t* msg);

/**
 * @brief Process a settings-changed notification from the transmitter.
 * Stores snapshot and updates BatterySettingsCache version.
 * @return true on success, false if message was malformed or checksum failed.
 */
bool handle_settings_changed(const espnow_queue_msg_t* msg);

/**
 * @brief Process a component-apply ACK from the transmitter.
 * Stores snapshot and forwards to ComponentApplyTracker.
 * @return true on success, false if message was malformed or checksum failed.
 */
bool handle_component_apply_ack(const espnow_queue_msg_t* msg);

/**
 * @brief Read a consistent copy of the current snapshot under mutex.
 * @param out_snapshot  Filled with the current snapshot.
 * @return true if snapshot was read successfully.
 */
bool read_snapshot(Snapshot& out_snapshot);

}  // namespace EspnowSettingsSync
