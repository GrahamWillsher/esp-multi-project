#include "mqtt_task.h"
#include "mqtt_manager.h"
#include "ethernet_manager.h"
#include "../config/task_config.h"
#include "../config/network_config.h"
#include "../config/logging_config.h"
#include "../datalayer/static_data.h"
#include "../system_settings.h"
#include <Arduino.h>
#include <ethernet_utilities.h>
#include <log_routed.h>
#include <mqtt_logger.h>
#include <esp32common/config/timing_config.h>
#include <esp32common/mqtt/mqtt_feature_flags.h>
#include "../battery_emulator/datalayer/datalayer.h"

namespace {

void initialize_mqtt_logger_if_needed(MqttManager& mqtt,
                                      bool& logger_initialized) {
    if (logger_initialized) {
        return;
    }

    MqttLogger::instance().init(mqtt.get_client(), "espnow/transmitter");
    MqttLogger::instance().set_level(MQTT_LOG_INFO);
    logger_initialized = true;

    LOG_INFO("MQTT", "MQTT logger initialized, level: %s",
             MqttLogger::instance().level_to_string(MQTT_LOG_INFO));
}

void publish_on_connect_bundle(MqttManager& mqtt,
                               EthernetManager& ethernet) {
    log_routed(LogSink::Mqtt, RoutedLevel::Notice,
               "MQTT", "MQTT broker connected successfully");
    log_routed(LogSink::Mqtt, RoutedLevel::Info,
               "SYSTEM", "Transmitter online, uptime: %lu ms", millis());
    log_routed(LogSink::Mqtt, RoutedLevel::Info,
               "ETH", "IP: %s, Gateway: %s",
               ethernet.get_local_ip().toString().c_str(),
               ethernet.get_gateway_ip().toString().c_str());

    // Update battery specs from datalayer (refresh number_of_cells after battery setup)
    LOG_INFO("MQTT", "Refreshing battery specs from datalayer...");
    StaticData::update_battery_specs(SystemSettings::instance().get_battery_profile_type());

    // Publish static configuration data (once on connect)
#if MQTT_FEATURE_RETAINED_SNAPSHOTS
    LOG_INFO("MQTT", "Publishing static configuration...");
    if (mqtt.publish_static_specs()) {
        LOG_INFO("MQTT", "✓ Combined legacy static publish skipped; per-model retained topics active");
    }
    if (mqtt.publish_inverter_specs()) {
        LOG_INFO("MQTT", "✓ Inverter specs published to batt-emu/mqtt-v1/tx/state/static/inverter");
    }
    if (mqtt.publish_battery_specs()) {
        LOG_INFO("MQTT", "✓ Battery specs published to batt-emu/mqtt-v1/tx/state/static/battery");
    }
    if (mqtt.publish_battery_type_catalog()) {
        LOG_INFO("MQTT", "✓ Battery type catalog published to batt-emu/mqtt-v1/tx/state/static/catalog_battery");
    }
    if (mqtt.publish_inverter_type_catalog()) {
        LOG_INFO("MQTT", "✓ Inverter type catalog published to batt-emu/mqtt-v1/tx/state/static/catalog_inverter");
    }
    if (mqtt.publish_static_network()) {
        LOG_INFO("MQTT", "✓ Static network config published");
    }
    if (mqtt.publish_static_mqtt()) {
        LOG_INFO("MQTT", "✓ Static MQTT config published");
    }
    if (mqtt.publish_static_power()) {
        LOG_INFO("MQTT", "✓ Static power config published");
    }
    if (mqtt.publish_static_led()) {
        LOG_INFO("MQTT", "✓ Static LED config published");
    }
    if (mqtt.publish_static_settings()) {
        LOG_INFO("MQTT", "✓ Static settings snapshot published");
    }
    if (mqtt.publish_runtime_led()) {
        LOG_INFO("MQTT", "✓ Runtime LED state published");
    }
    if (mqtt.publish_runtime_system()) {
        LOG_INFO("MQTT", "✓ Runtime system state published");
    }
    if (mqtt.publish_runtime_charger()) {
        LOG_INFO("MQTT", "✓ Runtime charger state published");
    }
    if (mqtt.publish_runtime_inverter()) {
        LOG_INFO("MQTT", "✓ Runtime inverter state published");
    }
    if (mqtt.publish_meta_version()) {
        LOG_INFO("MQTT", "✓ Meta version published");
    }
    if (mqtt.publish_meta_schema_versions()) {
        LOG_INFO("MQTT", "✓ Meta schema versions published");
    }
    if (mqtt.publish_meta_runtime()) {
        LOG_INFO("MQTT", "✓ Meta runtime published");
    }
#else
    LOG_INFO("MQTT", "Retained static snapshots disabled by feature flag");
#endif
}

void handle_mqtt_connection_transition(MqttManager& mqtt,
                                       EthernetManager& ethernet,
                                       bool is_connected_now,
                                       bool& was_connected,
                                       bool& logger_initialized) {
    if (is_connected_now == was_connected) {
        return;
    }

    MqttLogger::instance().set_mqtt_available(is_connected_now);

    if (is_connected_now) {
        initialize_mqtt_logger_if_needed(mqtt, logger_initialized);
        publish_on_connect_bundle(mqtt, ethernet);
    }

    was_connected = is_connected_now;
}

void publish_periodic_runtime_data(MqttManager& mqtt,
                                   EthernetManager& ethernet,
                                   bool is_connected_now,
                                   unsigned long now,
                                   unsigned long& last_publish,
                                   unsigned long& last_cell_publish,
                                   unsigned long& last_event_publish,
                                   unsigned long& last_heartbeat_publish) {
#if MQTT_FEATURE_HEARTBEAT
    if (is_connected_now && (now - last_heartbeat_publish > MQTT_HEARTBEAT_INTERVAL_MS)) {
        last_heartbeat_publish = now;
        mqtt.publish_heartbeat(ethernet.is_connected());
    }
#else
    (void)last_heartbeat_publish;
#endif

#if MQTT_FEATURE_LIVE_BATTERY_TELEMETRY
    if (is_connected_now && (now - last_publish > TimingConfig::MQTT_PUBLISH_INTERVAL_MS)) {
        last_publish = now;

        // Get formatted timestamp
        char timestamp_str[64];
        get_formatted_time(timestamp_str, sizeof(timestamp_str));

        const int soc = static_cast<int>(datalayer.battery.status.reported_soc / 100U);
        const long power = static_cast<long>(datalayer.battery.status.active_power_W);

        // Publish current data
        mqtt.publish_data(
            soc,
            power,
            timestamp_str,
            ethernet.is_connected()
        );
        (void)mqtt.publish_runtime_led();
        (void)mqtt.publish_runtime_system();
        (void)mqtt.publish_runtime_charger();
        (void)mqtt.publish_runtime_inverter();

        log_routed(LogSink::Mqtt, RoutedLevel::Info,
                   "TELEMETRY", "Data published: SOC=%d%%, Power=%ldW", soc, power);
    }
#else
    (void)last_publish;
#endif

#if MQTT_FEATURE_CELL_DATA_TELEMETRY
    if (is_connected_now && (now - last_cell_publish > TimingConfig::MQTT_CELL_PUBLISH_INTERVAL_MS)) {
        last_cell_publish = now;

        if (mqtt.publish_cell_data()) {
            LOG_DEBUG("MQTT", "✓ Cell data published to batt-emu/mqtt-v1/tx/state/cell_data/chunk");
        } else {
            LOG_DEBUG("MQTT", "Cell data publish skipped/failed");
        }
    }
#else
    (void)last_cell_publish;
#endif

#if MQTT_FEATURE_EVENT_LOG_STREAMING
    if (is_connected_now &&
        (now - last_event_publish > TimingConfig::MQTT_EVENT_PUBLISH_INTERVAL_MS)) {
        last_event_publish = now;
        mqtt.publish_event_logs();
    }
#else
    (void)last_event_publish;
#endif
}

}  // namespace

// MqttTask singleton implementation
MqttTask& MqttTask::instance() {
    static MqttTask instance;
    return instance;
}

bool MqttTask::is_connected() const {
    return MqttManager::instance().is_connected();
}

void task_mqtt_loop(void* parameter) {
    LOG_DEBUG("MQTT", "MQTT task started");

    auto& mqtt = MqttManager::instance();
    auto& ethernet = EthernetManager::instance();
    
    // Wait for Ethernet to be ready
    while (!ethernet.is_connected()) {
        LOG_DEBUG("MQTT", "MQTT waiting for Ethernet");
        vTaskDelay(pdMS_TO_TICKS(TimingConfig::MQTT_RECONNECT_INTERVAL_MS));
    }
    
    LOG_INFO("MQTT", "MQTT task active");
    
    unsigned long last_publish = 0;
    unsigned long last_cell_publish = 0;
    unsigned long last_event_publish = 0;
    unsigned long last_heartbeat_publish = 0;
    unsigned long last_stats_log = 0;
    bool logger_initialized = false;
    bool was_connected = false;  // Track previous MQTT connection state
    
    while (true) {
        unsigned long now = millis();
        
        // Update MQTT state machine (handles connection/reconnection)
        mqtt.update();

        // Check if MQTT connection state changed
        bool is_connected_now = mqtt.is_connected();
        handle_mqtt_connection_transition(
            mqtt,
            ethernet,
            is_connected_now,
            was_connected,
            logger_initialized
        );

        // Drain buffered MQTT logs from this single task context only.
        // This avoids PubSubClient cross-task publish races.
        if (is_connected_now) {
            MqttLogger::instance().flush_buffer();
        }
        
        // Log statistics periodically
        if (now - last_stats_log > TimingConfig::MQTT_STATS_LOG_INTERVAL_MS) {
            auto stats = mqtt.get_statistics();
            LOG_INFO("MQTT_STATS", "Connections: %lu, Failed: %lu, Published: %lu, Uptime: %lu s, State: %d",
                    stats.total_connections,
                    stats.failed_connections,
                    stats.total_messages_published,
                    stats.uptime_ms / 1000,
                    (int)stats.current_state);
            last_stats_log = now;
        }

        publish_periodic_runtime_data(
            mqtt,
            ethernet,
            is_connected_now,
            now,
            last_publish,
            last_cell_publish,
            last_event_publish,
            last_heartbeat_publish
        );

        // Update task every second (state machine runs inside update())
        vTaskDelay(pdMS_TO_TICKS(TimingConfig::MQTT_LOOP_DELAY_MS));
    }
}
