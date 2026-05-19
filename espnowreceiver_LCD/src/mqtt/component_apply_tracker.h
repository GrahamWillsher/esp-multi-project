#pragma once

#include <Arduino.h>
#include <esp32common/espnow/common.h>
#include <esp32common/patterns/save_apply_coordinator.h>

namespace ComponentApplyTracker {

using State = Esp32Common::Patterns::SaveApplyCoordinator::State;

struct Snapshot {
    uint32_t request_id = 0;
    State state = State::idle;
    uint32_t started_ms = 0;
    uint32_t updated_ms = 0;
    uint8_t apply_mask = 0;
    uint8_t persisted_mask = 0;
    bool success = false;
    bool reboot_required = false;
    bool ready_for_reboot = false;
    uint8_t battery_type = 0;
    uint8_t inverter_type = 0;
    uint8_t battery_interface = 0;
    uint8_t inverter_interface = 0;
    uint32_t settings_version = 0;
    char message[48] = {0};
};

void init();

bool start_transaction(uint32_t request_id,
                       uint8_t apply_mask,
                       uint8_t battery_type,
                       uint8_t inverter_type,
                       uint8_t battery_interface,
                       uint8_t inverter_interface);

void mark_failed(uint32_t request_id, const char* message);

void on_ack(const component_apply_ack_t& ack);
Snapshot get_snapshot();

} // namespace ComponentApplyTracker
