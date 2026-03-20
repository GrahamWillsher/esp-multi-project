#include "../battery_handlers.h"
#include "../battery_data_store.h"
#include "status_handler_common.h"

void handle_charger_status(const espnow_queue_msg_t* msg) {
    BatteryHandlersInternal::process_status_message<charger_status_msg_t>(
        msg,
        "Charger status",
        [](const charger_status_msg_t& data) {
            BatteryData::update_charger_status(data);
        },
        []() {
            LOG_DEBUG("BATTERY", "Charger Status=%d, HV=%.1fV/%.1fA, AC=%dV, P=%dW",
                      BatteryData::charger_status, BatteryData::charger_hv_voltage_V,
                      BatteryData::charger_hv_current_A, BatteryData::charger_ac_voltage_V,
                      BatteryData::charger_power_W);
        });
}