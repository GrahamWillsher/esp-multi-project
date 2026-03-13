#include "../battery_handlers.h"
#include "../battery_data_store.h"

void handle_system_status(const espnow_queue_msg_t* msg) {
    const system_status_msg_t* data = (system_status_msg_t*)msg->data;

    if (!validate_checksum(data, sizeof(*data))) {
        LOG_ERROR("BATTERY", "System status: Invalid checksum - message rejected");
        return;
    }

    BatteryData::contactor_state = data->contactor_state;
    BatteryData::error_flags = data->error_flags;
    BatteryData::warning_flags = data->warning_flags;
    BatteryData::uptime_seconds = data->uptime_seconds;
    BatteryData::system_received = true;

    LOG_DEBUG("BATTERY", "System Status: Contactors=0x%02X, Errors=0x%02X, Warnings=0x%02X, Uptime=%us",
              BatteryData::contactor_state, BatteryData::error_flags,
              BatteryData::warning_flags, BatteryData::uptime_seconds);
}