#include "../battery_handlers.h"
#include "../battery_data_store.h"
#include "status_handler_common.h"

void handle_inverter_status(const espnow_queue_msg_t* msg) {
    BatteryHandlersInternal::process_status_message<inverter_status_msg_t>(
        msg,
        "Inverter status",
        [](const inverter_status_msg_t& data) {
            BatteryData::update_inverter_status(data);
        },
        []() {
            LOG_DEBUG("BATTERY", "Inverter Status=%d, AC=%dV/%.1fA@%.1fHz, P=%dW",
                      BatteryData::inverter_status, BatteryData::inverter_ac_voltage_V,
                      BatteryData::inverter_ac_current_A, BatteryData::inverter_ac_frequency_Hz,
                      BatteryData::inverter_power_W);
        });
}