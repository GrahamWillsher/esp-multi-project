#include "../battery_handlers.h"
#include "../battery_data_store.h"

void handle_charger_status(const espnow_queue_msg_t* msg) {
    const charger_status_msg_t* data = (charger_status_msg_t*)msg->data;

    if (!validate_checksum(data, sizeof(*data))) {
        LOG_ERROR("BATTERY", "Charger status: Invalid checksum - message rejected");
        return;
    }

    BatteryData::charger_hv_voltage_V = data->hv_voltage_dV / 10.0;
    BatteryData::charger_hv_current_A = data->hv_current_dA / 10.0;
    BatteryData::charger_lv_voltage_V = data->lv_voltage_dV / 10.0;
    BatteryData::charger_ac_voltage_V = data->ac_voltage_V;
    BatteryData::charger_power_W = data->power_W;
    BatteryData::charger_status = data->charger_status;
    BatteryData::charger_received = true;

    LOG_DEBUG("BATTERY", "Charger Status=%d, HV=%.1fV/%.1fA, AC=%dV, P=%dW",
              BatteryData::charger_status, BatteryData::charger_hv_voltage_V,
              BatteryData::charger_hv_current_A, BatteryData::charger_ac_voltage_V,
              BatteryData::charger_power_W);
}