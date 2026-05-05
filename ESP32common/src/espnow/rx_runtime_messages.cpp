#include "rx_runtime_messages.h"

namespace EspNowReceiver {

RuntimeMessageClass classify_message(uint8_t message_type) {
    switch (message_type) {
        case msg_probe:
        case msg_ack:
            return RuntimeMessageClass::Discovery;

        case msg_heartbeat:
        case msg_heartbeat_ack:
            return RuntimeMessageClass::Heartbeat;

        case msg_data:
        case msg_packet:
        case msg_battery_status:
        case msg_battery_info:
        case msg_charger_status:
        case msg_inverter_status:
        case msg_system_status:
        case msg_event_log_summary:
        case msg_temperature_report:
        case msg_time_transitions_snapshot:
            return RuntimeMessageClass::Data;

        case msg_request_data:
        case msg_abort_data:
        case msg_reboot:
        case msg_ota_start:
        case msg_flash_led:
        case msg_debug_control:
        case msg_debug_ack:
        case msg_version_announce:
        case msg_version_request:
        case msg_version_response:
        case msg_metadata_response:
        case msg_component_config:
        case msg_battery_settings_update:
        case msg_settings_update_ack:
        case msg_settings_changed:
        case msg_network_config_request:
        case msg_network_config_update:
        case msg_network_config_ack:
        case msg_mqtt_config_request:
        case msg_mqtt_config_update:
        case msg_mqtt_config_ack:
        case msg_version_beacon:
        case msg_config_section_request:
        case msg_config_changed:
        case msg_component_interface:
        case msg_component_apply_request:
        case msg_component_apply_ack:
        case msg_event_logs_control:
        case msg_event_log_summary_request:
        case msg_event_logs_clear_ack:
        case msg_request_battery_types:
        case msg_battery_types_fragment:
        case msg_request_inverter_types:
        case msg_inverter_types_fragment:
        case msg_request_inverter_interfaces:
        case msg_inverter_interfaces_fragment:
        case msg_request_type_catalog_versions:
        case msg_type_catalog_versions:
        case msg_led_state_request:
            return RuntimeMessageClass::Control;

        default:
            return RuntimeMessageClass::Unknown;
    }
}

bool is_discovery_message(uint8_t message_type) {
    return classify_message(message_type) == RuntimeMessageClass::Discovery;
}

bool is_heartbeat_message(uint8_t message_type) {
    return classify_message(message_type) == RuntimeMessageClass::Heartbeat;
}

bool participates_in_generic_activity(uint8_t message_type) {
    return !is_discovery_message(message_type);
}

}  // namespace EspNowReceiver