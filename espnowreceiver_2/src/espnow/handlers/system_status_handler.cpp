#include "../battery_handlers.h"
#include "../battery_data_store.h"
#include "status_handler_common.h"

void handle_system_status(const espnow_queue_msg_t* msg) {
    BatteryHandlersInternal::process_status_message<system_status_msg_t>(
        msg,
        "System status",
        [](const system_status_msg_t& data) {
            BatteryData::update_system_status(data);
        },
        []() {
            LOG_DEBUG("BATTERY", "System Status: Contactors=0x%02X, Errors=0x%02X, Warnings=0x%02X, Uptime=%us",
                      BatteryData::contactor_state, BatteryData::error_flags,
                      BatteryData::warning_flags, BatteryData::uptime_seconds);
        });
}