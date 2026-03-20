#include "../battery_handlers.h"
#include "../battery_data_store.h"
#include "status_handler_common.h"

void handle_battery_status(const espnow_queue_msg_t* msg) {
    BatteryHandlersInternal::process_status_message<battery_status_msg_t>(
        msg,
        "Battery status",
        [](const battery_status_msg_t& data) {
            BatteryData::update_battery_status(data);
        },
        []() {
            LOG_DEBUG("BATTERY", "Battery Status: SOC=%.2f%%, V=%.2fV, I=%.2fA, T=%.1fC, P=%dW, BMS=%d",
                      BatteryData::soc_percent, BatteryData::voltage_V, BatteryData::current_A,
                      BatteryData::temperature_C, BatteryData::power_W, BatteryData::bms_status);
        });
}