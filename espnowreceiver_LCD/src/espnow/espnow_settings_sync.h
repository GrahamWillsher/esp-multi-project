#pragma once

#include <cstdint>
#include <esp32common/espnow/common.h>

namespace EspnowSettingsSync {

struct Snapshot {
    uint8_t last_category = 0xFF;
    uint8_t last_field_id = 0xFF;
    bool last_update_success = false;
    uint32_t settings_version = 0;

    uint8_t changed_category = 0xFF;
    uint32_t changed_version = 0;

    uint32_t last_component_apply_request_id = 0;
    bool component_apply_success = false;
    bool component_apply_reboot_required = false;
    uint32_t component_settings_version = 0;
};

bool handle_settings_update_ack(const espnow_queue_msg_t* msg);
bool handle_settings_changed(const espnow_queue_msg_t* msg);
bool handle_component_apply_ack(const espnow_queue_msg_t* msg);

bool read_snapshot(Snapshot& out_snapshot);

}  // namespace EspnowSettingsSync
