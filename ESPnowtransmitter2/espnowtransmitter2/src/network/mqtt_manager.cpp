#include "mqtt_manager.h"
#include "ethernet_manager.h"
#include "../config/network_config.h"
#include "../config/event_log_config.h"
#include "../config/logging_config.h"
#include "../datalayer/static_data.h"
#include "../espnow/control_handlers.h"
#include "../espnow/component_catalog_handlers.h"
#include "../datalayer/datalayer.h"
#include "../settings/settings_manager.h"
#include "../battery_emulator/devboard/utils/events.h"
#include "../test_data/test_data_config.h"
#include "ota_manager.h"
#include "time_manager.h"
#if CONFIG_CAN_ENABLED
#include "../battery_emulator/battery/Battery.h"
#include "../battery_emulator/inverter/InverterProtocol.h"
#endif
#include <Arduino.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <vector>
#include <firmware_version.h>
#include <mqtt_logger.h>
#include <esp32common/config/timing_config.h>
#include <esp32common/mqtt/mqtt_feature_flags.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mqtt_manager.h>
#include <ethernet_utilities.h>

namespace {
constexpr uint16_t MAX_CATALOG_BASE_VERSION = 32767;

// FNV1a hash for O(1)-like topic dispatch
constexpr uint32_t fnv1a_const(const char* str, uint32_t hash = 2166136261u) {  // NOLINT
    return (*str == '\0') ? hash : fnv1a_const(str + 1, (hash ^ static_cast<uint8_t>(*str)) * 16777619u);
}

uint32_t fnv1a_runtime(const char* str) {
    uint32_t hash = 2166136261u;
    while (str && *str) {
        hash ^= static_cast<uint8_t>(*str++);
        hash *= 16777619u;
    }
    return hash;
}

// ── §17 Large-payload chunking ────────────────────────────────────────────
// Threshold: chunk when serialised payload exceeds this byte count.
constexpr size_t kChunkThreshold  = 2048u;
// Bytes of original payload included in each chunk's `data` field.
constexpr size_t kChunkPayloadLen = 1024u;
// Working buffer for a single chunk envelope (header + slice + JSON escaping headroom).
constexpr size_t kChunkEnvBufLen  = 1500u;
char g_chunk_env_buf[kChunkEnvBufLen];  // reused across chunks; safe: single MQTT task

/**
 * @brief Publish a large payload as a series of §17-compliant chunk envelopes.
 *
 * Envelope format:
 *   {"chunk_id":"chk-XXXXXXXX-XXXX","part_num":N,"total_parts":N,"data":"..."}
 *
 * The `data` value contains up to kChunkPayloadLen bytes of the original payload
 * with only '"' and '\' JSON-escaped.  The receiver concatenates the raw data
 * strings to reconstruct the original payload before parsing it.
 *
 * @param client     PubSubClient instance.
 * @param topic      Destination MQTT topic.
 * @param payload    Pointer to the full serialised payload.
 * @param payload_len  Byte length of the payload.
 * @return true if every chunk was published successfully.
 */
static bool publish_in_chunks(PubSubClient& client,
                               const char*   topic,
                               const char*   payload,
                               size_t        payload_len) {
    const size_t total_parts =
        (payload_len + kChunkPayloadLen - 1u) / kChunkPayloadLen;

    // Unique identifier for this transfer: "chk-<millis8hex>-<rand4hex>"
    char chunk_id[24];
    snprintf(chunk_id, sizeof(chunk_id), "chk-%08lx-%04x",
             static_cast<unsigned long>(millis()),
             static_cast<unsigned>(esp_random() & 0xFFFFu));

    for (size_t part = 0; part < total_parts; part++) {
        const size_t offset    = part * kChunkPayloadLen;
        const size_t slice_len = (payload_len - offset < kChunkPayloadLen)
                                     ? payload_len - offset
                                     : kChunkPayloadLen;

        // Write the fixed envelope header into g_chunk_env_buf
        int hdr = snprintf(g_chunk_env_buf, kChunkEnvBufLen,
                           "{\"chunk_id\":\"%s\",\"part_num\":%u,"
                           "\"total_parts\":%u,\"data\":\"",
                           chunk_id,
                           static_cast<unsigned>(part),
                           static_cast<unsigned>(total_parts));
        if (hdr < 0 || static_cast<size_t>(hdr) >= kChunkEnvBufLen) {
            return false;
        }

        // Append the slice bytes, JSON-escaping '"' and '\'
        size_t pos = static_cast<size_t>(hdr);
        for (size_t i = 0; i < slice_len; i++) {
            const uint8_t c = static_cast<uint8_t>(payload[offset + i]);
            if (pos + 3u >= kChunkEnvBufLen) {
                return false;  // No room – should never happen with kChunkEnvBufLen = 1500
            }
            if (c == '"' || c == '\\') {
                g_chunk_env_buf[pos++] = '\\';
            }
            g_chunk_env_buf[pos++] = static_cast<char>(c);
        }

        // Close the envelope
        if (pos + 2u >= kChunkEnvBufLen) {
            return false;
        }
        g_chunk_env_buf[pos++] = '"';
        g_chunk_env_buf[pos++] = '}';
        g_chunk_env_buf[pos]   = '\0';

        if (!client.publish(topic, g_chunk_env_buf, false)) {
            return false;
        }
    }
    return true;
}

constexpr const char* MQTT_TOPIC_TX_HEARTBEAT = "batt-emu/mqtt-v1/tx/state/heartbeat";
constexpr const char* MQTT_TOPIC_RX_CMD_UPDATE_BATTERY = "batt-emu/mqtt-v1/rx/cmd/update/battery";
constexpr const char* MQTT_TOPIC_RX_CMD_UPDATE_NETWORK = "batt-emu/mqtt-v1/rx/cmd/update/network";
constexpr const char* MQTT_TOPIC_RX_CMD_UPDATE_MQTT = "batt-emu/mqtt-v1/rx/cmd/update/mqtt";
constexpr const char* MQTT_TOPIC_RX_CMD_CONTROL_DEBUG_LEVEL = "batt-emu/mqtt-v1/rx/cmd/control/debug_level";
constexpr const char* MQTT_TOPIC_RX_CMD_CONTROL_TEST_DATA_MODE = "batt-emu/mqtt-v1/rx/cmd/control/test_data_mode";
constexpr const char* MQTT_TOPIC_RX_CMD_CONTROL_EVENT_LOGS_CLEAR = "batt-emu/mqtt-v1/rx/cmd/control/event_logs_clear";
constexpr const char* MQTT_TOPIC_RX_CMD_CONTROL_REBOOT = "batt-emu/mqtt-v1/rx/cmd/control/reboot";
constexpr const char* MQTT_TOPIC_RX_CMD_CONTROL_OTA_START = "batt-emu/mqtt-v1/rx/cmd/control/ota_start";
constexpr const char* MQTT_TOPIC_RX_CMD_CONTROL_COMPONENT_APPLY = "batt-emu/mqtt-v1/rx/cmd/control/component_apply";
constexpr const char* MQTT_TOPIC_RX_CMD_REFRESH_POWER = "batt-emu/mqtt-v1/rx/cmd/refresh/power";
constexpr const char* MQTT_TOPIC_RX_CMD_REFRESH_BATTERY = "batt-emu/mqtt-v1/rx/cmd/refresh/battery";
constexpr const char* MQTT_TOPIC_RX_CMD_REFRESH_NETWORK = "batt-emu/mqtt-v1/rx/cmd/refresh/network";
constexpr const char* MQTT_TOPIC_RX_CMD_REFRESH_MQTT = "batt-emu/mqtt-v1/rx/cmd/refresh/mqtt";
constexpr const char* MQTT_TOPIC_RX_CMD_REFRESH_SETTINGS = "batt-emu/mqtt-v1/rx/cmd/refresh/settings";
constexpr const char* MQTT_TOPIC_RX_CMD_REFRESH_CATALOG_BATTERY = "batt-emu/mqtt-v1/rx/cmd/refresh/catalog_battery";
constexpr const char* MQTT_TOPIC_RX_CMD_REFRESH_CATALOG_INVERTER = "batt-emu/mqtt-v1/rx/cmd/refresh/catalog_inverter";
constexpr const char* MQTT_TOPIC_RX_CMD_REFRESH_LED = "batt-emu/mqtt-v1/rx/cmd/refresh/led";
constexpr const char* MQTT_TOPIC_RX_CMD_STREAM_EVENT_LOGS = "batt-emu/mqtt-v1/rx/cmd/stream/event_logs";
constexpr const char* MQTT_TOPIC_RX_CMD_STREAM_CELL_DATA = "batt-emu/mqtt-v1/rx/cmd/stream/cell_data";
constexpr const char* MQTT_TOPIC_TX_ACK_CONTROL = "batt-emu/mqtt-v1/tx/ack/control";
constexpr const char* MQTT_TOPIC_TX_ACK_BATTERY = "batt-emu/mqtt-v1/tx/ack/battery";
constexpr const char* MQTT_TOPIC_TX_ACK_NETWORK = "batt-emu/mqtt-v1/tx/ack/network";
constexpr const char* MQTT_TOPIC_TX_ACK_MQTT = "batt-emu/mqtt-v1/tx/ack/mqtt";
constexpr const char* MQTT_TOPIC_TX_ACK_EVENT_LOGS_CLEAR = "batt-emu/mqtt-v1/tx/ack/event_logs_clear";
constexpr const char* MQTT_TOPIC_TX_STATE_STATIC_NETWORK = "batt-emu/mqtt-v1/tx/state/static/network";
constexpr const char* MQTT_TOPIC_TX_STATE_STATIC_MQTT    = "batt-emu/mqtt-v1/tx/state/static/mqtt";
constexpr const char* MQTT_TOPIC_TX_STATE_STATIC_BATTERY = "batt-emu/mqtt-v1/tx/state/static/battery";
constexpr const char* MQTT_TOPIC_TX_STATE_STATIC_INVERTER = "batt-emu/mqtt-v1/tx/state/static/inverter";
constexpr const char* MQTT_TOPIC_TX_STATE_STATIC_POWER = "batt-emu/mqtt-v1/tx/state/static/power";
constexpr const char* MQTT_TOPIC_TX_STATE_STATIC_LED = "batt-emu/mqtt-v1/tx/state/static/led";
constexpr const char* MQTT_TOPIC_TX_STATE_STATIC_SETTINGS = "batt-emu/mqtt-v1/tx/state/static/settings";
constexpr const char* MQTT_TOPIC_TX_STATE_STATIC_CATALOG_BATTERY  = "batt-emu/mqtt-v1/tx/state/static/catalog_battery";
constexpr const char* MQTT_TOPIC_TX_STATE_STATIC_CATALOG_INVERTER = "batt-emu/mqtt-v1/tx/state/static/catalog_inverter";
constexpr const char* MQTT_TOPIC_TX_STATE_BATTERY_LIVE = "batt-emu/mqtt-v1/tx/state/battery_live";
constexpr const char* MQTT_TOPIC_TX_STATE_RUNTIME_LED = "batt-emu/mqtt-v1/tx/state/runtime/led";
constexpr const char* MQTT_TOPIC_TX_STATE_RUNTIME_SYSTEM = "batt-emu/mqtt-v1/tx/state/runtime/system";
constexpr const char* MQTT_TOPIC_TX_STATE_RUNTIME_CHARGER = "batt-emu/mqtt-v1/tx/state/runtime/charger";
constexpr const char* MQTT_TOPIC_TX_STATE_RUNTIME_INVERTER = "batt-emu/mqtt-v1/tx/state/runtime/inverter";
constexpr const char* MQTT_TOPIC_TX_STATE_CELL_DATA_CHUNK = "batt-emu/mqtt-v1/tx/state/cell_data/chunk";
constexpr const char* MQTT_TOPIC_TX_STATE_EVENT_LOG_SUMMARY = "batt-emu/mqtt-v1/tx/state/summary/event_logs";
constexpr const char* MQTT_TOPIC_TX_STATE_EVENT_LOG_CHUNK = "batt-emu/mqtt-v1/tx/state/event_logs/chunk";
constexpr const char* MQTT_TOPIC_TX_META_VERSION = "batt-emu/mqtt-v1/tx/meta/version";
constexpr const char* MQTT_TOPIC_TX_META_SCHEMA_VERSIONS = "batt-emu/mqtt-v1/tx/meta/schema_versions";
constexpr const char* MQTT_TOPIC_TX_META_RUNTIME = "batt-emu/mqtt-v1/tx/meta/runtime";

bool parse_ipv4_string(const char* value, uint8_t out[4]) {
    if (value == nullptr || out == nullptr) {
        return false;
    }

    int a = 0, b = 0, c = 0, d = 0;
    if (sscanf(value, "%d.%d.%d.%d", &a, &b, &c, &d) != 4) {
        return false;
    }

    if (a < 0 || a > 255 || b < 0 || b > 255 || c < 0 || c > 255 || d < 0 || d > 255) {
        return false;
    }

    out[0] = static_cast<uint8_t>(a);
    out[1] = static_cast<uint8_t>(b);
    out[2] = static_cast<uint8_t>(c);
    out[3] = static_cast<uint8_t>(d);
    return true;
}

bool is_disabled_placeholder_label(const char* label) {
    return (label != nullptr) && (strstr(label, "(disabled)") != nullptr);
}

const char* normalized_request_id(const char* request_id, char* out, size_t out_len) {
    if (request_id != nullptr && request_id[0] != '\0') {
        return request_id;
    }

    if (out == nullptr || out_len == 0) {
        return "tx-auto";
    }

    snprintf(out,
             out_len,
             "tx-auto-%08lx-%08lx",
             static_cast<unsigned long>(millis()),
             static_cast<unsigned long>(esp_random()));
    out[out_len - 1] = '\0';
    return out;
}

uint16_t catalog_base_version() {
    const uint32_t fw = FW_VERSION_NUMBER;
    return static_cast<uint16_t>((fw > MAX_CATALOG_BASE_VERSION) ? MAX_CATALOG_BASE_VERSION : fw);
}

uint16_t mqtt_battery_type_catalog_version() {
    return static_cast<uint16_t>(catalog_base_version() * 2u);
}

uint16_t mqtt_inverter_type_catalog_version() {
    return static_cast<uint16_t>((catalog_base_version() * 2u) + 1u);
}

const char* inverter_catalog_fallback_name(uint8_t id) {
    static const char* fallback_names[] = {
        "None",
        "Afore battery over CAN",
        "BYD Battery-Box Premium HVS over CAN Bus",
        "BYD 11kWh HVM battery over Modbus RTU",
        "Ferroamp Pylon battery over CAN bus",
        "FoxESS compatible HV2600/ECS4100 battery",
        "Growatt High Voltage protocol via CAN",
        "Growatt Low Voltage (48V) protocol via CAN",
        "Growatt WIT compatible battery via CAN",
        "BYD battery via Kostal RS485",
        "Pylontech HV battery over CAN bus",
        "Pylontech LV battery over CAN bus",
        "Schneider V2 SE BMS CAN",
        "SMA compatible BYD H",
        "SMA compatible BYD Battery-Box HVS",
        "SMA Low Voltage (48V) protocol via CAN",
        "SMA Tripower CAN",
        "Sofar BMS (Extended) via CAN, Battery ID",
        "SolaX Triple Power LFP over CAN bus",
        "Solxpow compatible battery",
        "Sol-Ark LV protocol over CAN bus",
        "Sungrow SBRXXX emulation over CAN bus"
    };

    if (id >= (sizeof(fallback_names) / sizeof(fallback_names[0]))) {
        return nullptr;
    }

    return fallback_names[id];
}

uint8_t led_color_from_emulator_status(EMULATOR_STATUS status) {
    switch (status) {
        case EMULATOR_STATUS::STATUS_OK:
            return LED_WIRE_GREEN;
        case EMULATOR_STATUS::STATUS_WARNING:
            return LED_WIRE_ORANGE;
        case EMULATOR_STATUS::STATUS_UPDATING:
            return LED_WIRE_BLUE;
        case EMULATOR_STATUS::STATUS_ERROR:
            return LED_WIRE_RED;
        default:
            return LED_WIRE_ORANGE;
    }
}

uint8_t led_effect_from_mode(uint8_t led_mode) {
    switch (led_mode) {
        case 0: return LED_WIRE_CONTINUOUS;
        case 1: return LED_WIRE_FLASH;
        case 2: return LED_WIRE_HEARTBEAT;
        default: return LED_WIRE_CONTINUOUS;
    }
}

uint8_t compose_contactor_state_bits() {
    uint8_t contactor_state = 0;

    if (datalayer.system.status.contactors_engaged == 1 ||
        datalayer.shunt.contactors_engaged ||
        datalayer.system.status.contactors_battery2_engaged) {
        // Legacy bit contract: bit0=positive, bit1=negative, bit2=precharge.
        // We model "engaged" as both main contactors closed.
        contactor_state |= 0x03;
    }

    if (datalayer.shunt.precharging ||
        datalayer.system.status.precharge_status != AUTO_PRECHARGE_IDLE ||
        datalayer.system.info.start_precharging) {
        contactor_state |= 0x04;
    }

    return contactor_state;
}

uint8_t compose_error_flags(EMULATOR_STATUS status) {
    return (status == EMULATOR_STATUS::STATUS_ERROR) ? 0x01 : 0x00;
}

uint8_t compose_warning_flags(EMULATOR_STATUS status) {
    uint8_t flags = 0;
    if (status == EMULATOR_STATUS::STATUS_WARNING) {
        flags |= 0x01;
    }
    if (status == EMULATOR_STATUS::STATUS_UPDATING) {
        flags |= 0x02;
    }
    return flags;
}
}

MqttManager::MqttManager() : client_(eth_client_) {
    initialization_time_ = millis();
    state_enter_time_ = millis();
}

MqttManager& MqttManager::instance() {
    static MqttManager instance;
    return instance;
}

bool MqttManager::is_connected() {
    // State machine must report CONNECTED and PubSubClient transport must still be valid.
    return (state_ == MqttState::CONNECTED) && (client_.state() == 0);
}

void MqttManager::init() {
    // Always hydrate runtime MQTT config cache from NVS at boot.
    // This ensures persisted values are available for static/mqtt publication
    // and configuration pages after reboot.
    const bool mqtt_cfg_loaded = MqttConfigManager::loadConfig();
    if (mqtt_cfg_loaded) {
        LOG_INFO("MQTT", "MQTT runtime config hydrated from NVS at boot");
    } else {
        LOG_WARN("MQTT", "MQTT runtime config not found in NVS at boot (using defaults)");
    }

    if (!config::features::MQTT_ENABLED) {
        LOG_INFO("MQTT", "MQTT disabled in configuration");
        return;
    }
    
    LOG_INFO("MQTT", "Initializing MQTT client...");
    // Set buffer size to accommodate cells + event logs (cell_data can be ~6KB)
    client_.setBufferSize(6144);
    client_.setServer(config::get_mqtt_config().server, config::get_mqtt_config().port);
    client_.setCallback(message_callback);
    client_.setKeepAlive(60);
    client_.setSocketTimeout(10);
    LOG_INFO("MQTT", "MQTT client configured (will connect when Ethernet ready)");
}

void MqttManager::update() {
    if (!config::features::MQTT_ENABLED) return;
    
    uint32_t now = millis();
    
    // Check if Ethernet connectivity changed
    bool ethernet_connected = EthernetManager::instance().is_fully_ready();
    
    switch (state_) {
        case MqttState::DISCONNECTED:
            if (ethernet_connected) {
                // Ethernet is ready, try to connect
                attempt_connection();
            }
            break;
            
        case MqttState::CONNECTING: {
            uint32_t elapsed = now - last_connection_attempt_;
            if (elapsed > CONNECTION_TIMEOUT_MS) {
                // Connection attempt timed out
                LOG_WARN("MQTT", "Connection timeout after %lu ms", elapsed);
                on_connection_failed();
            }
            break;
        }
            
        case MqttState::CONNECTED:
            if (client_.state() == MQTT_CONNECTED) {
                // Process subscriptions while connected
                client_.loop();
            } else {
                // Connection dropped
                const int rc = client_.state();
                LOG_WARN("MQTT", "Connection lost (rc=%d) - applying retry backoff", rc);
                on_connection_failed();
            }
            break;
            
        case MqttState::CONNECTION_FAILED: {
            uint32_t elapsed = now - last_connection_attempt_;
            if (elapsed >= current_retry_delay_) {
                // Time to retry
                if (ethernet_connected) {
                    LOG_INFO("MQTT", "Attempting reconnection (previous delay: %lu ms)",
                            current_retry_delay_);
                    attempt_connection();
                } else {
                    LOG_DEBUG("MQTT", "Waiting for Ethernet to reconnect");
                    transition_to(MqttState::NETWORK_ERROR);
                }
            }
            break;
        }
            
        case MqttState::NETWORK_ERROR:
            if (ethernet_connected) {
                // Ethernet is back, try MQTT again
                LOG_INFO("MQTT", "Ethernet recovered, attempting connection");
                attempt_connection();
            }
            break;
    }
}

void MqttManager::attempt_connection() {
    if (!EthernetManager::instance().is_fully_ready()) {
        LOG_WARN("MQTT", "Ethernet not ready, deferring MQTT connection attempt");
        on_network_error();
        return;
    }

    LOG_INFO("MQTT", "Attempting connection to %s:%d...", 
             config::get_mqtt_config().server, config::get_mqtt_config().port);
    
    transition_to(MqttState::CONNECTING);
    last_connection_attempt_ = millis();
    
    // Attempt actual connection
    bool success = false;
    if (strlen(config::get_mqtt_config().username) > 0) {
        success = client_.connect(config::get_mqtt_config().client_id, 
                                 config::get_mqtt_config().username, 
                                 config::get_mqtt_config().password);
    } else {
        success = client_.connect(config::get_mqtt_config().client_id);
    }
    
    if (success) {
        on_connection_success();
    } else {
        LOG_ERROR("MQTT", "Connection failed, rc=%d", client_.state());
        on_connection_failed();
    }
}

void MqttManager::on_connection_success() {
    LOG_INFO("MQTT", "Connected successfully");
    
    transition_to(MqttState::CONNECTED);
    connected_ = true;  // Legacy flag
    total_connections_++;
    MqttLogger::instance().set_mqtt_available(true);
    
    // Reset retry delay on success
    current_retry_delay_ = INITIAL_RETRY_DELAY_MS;
    
    // Publish connection status
    client_.publish(config::get_mqtt_config().topics.status, "online", true);
    
    // Subscribe to OTA topic
    if (client_.subscribe(config::get_mqtt_config().topics.ota)) {
        LOG_INFO("MQTT", "Subscribed to OTA topic: %s", config::get_mqtt_config().topics.ota);
    } else {
        LOG_ERROR("MQTT", "Failed to subscribe to OTA topic");
    }

#if MQTT_FEATURE_COMMANDS
    if (client_.subscribe(MQTT_TOPIC_RX_CMD_UPDATE_BATTERY)) {
        LOG_INFO("MQTT", "Subscribed to update topic: %s", MQTT_TOPIC_RX_CMD_UPDATE_BATTERY);
    } else {
        LOG_ERROR("MQTT", "Failed to subscribe to %s", MQTT_TOPIC_RX_CMD_UPDATE_BATTERY);
    }

    if (client_.subscribe(MQTT_TOPIC_RX_CMD_UPDATE_NETWORK)) {
        LOG_INFO("MQTT", "Subscribed to update topic: %s", MQTT_TOPIC_RX_CMD_UPDATE_NETWORK);
    } else {
        LOG_ERROR("MQTT", "Failed to subscribe to %s", MQTT_TOPIC_RX_CMD_UPDATE_NETWORK);
    }

    if (client_.subscribe(MQTT_TOPIC_RX_CMD_UPDATE_MQTT)) {
        LOG_INFO("MQTT", "Subscribed to update topic: %s", MQTT_TOPIC_RX_CMD_UPDATE_MQTT);
    } else {
        LOG_ERROR("MQTT", "Failed to subscribe to %s", MQTT_TOPIC_RX_CMD_UPDATE_MQTT);
    }

    if (client_.subscribe(MQTT_TOPIC_RX_CMD_STREAM_EVENT_LOGS)) {
        LOG_INFO("MQTT", "Subscribed to stream control topic: %s", MQTT_TOPIC_RX_CMD_STREAM_EVENT_LOGS);
    } else {
        LOG_ERROR("MQTT", "Failed to subscribe to %s", MQTT_TOPIC_RX_CMD_STREAM_EVENT_LOGS);
    }

    if (client_.subscribe(MQTT_TOPIC_RX_CMD_STREAM_CELL_DATA)) {
        LOG_INFO("MQTT", "Subscribed to stream control topic: %s", MQTT_TOPIC_RX_CMD_STREAM_CELL_DATA);
    } else {
        LOG_ERROR("MQTT", "Failed to subscribe to %s", MQTT_TOPIC_RX_CMD_STREAM_CELL_DATA);
    }

    if (client_.subscribe(MQTT_TOPIC_RX_CMD_CONTROL_DEBUG_LEVEL)) {
        LOG_INFO("MQTT", "Subscribed to control topic: %s", MQTT_TOPIC_RX_CMD_CONTROL_DEBUG_LEVEL);
    } else {
        LOG_ERROR("MQTT", "Failed to subscribe to %s", MQTT_TOPIC_RX_CMD_CONTROL_DEBUG_LEVEL);
    }

    if (client_.subscribe(MQTT_TOPIC_RX_CMD_CONTROL_TEST_DATA_MODE)) {
        LOG_INFO("MQTT", "Subscribed to control topic: %s", MQTT_TOPIC_RX_CMD_CONTROL_TEST_DATA_MODE);
    } else {
        LOG_ERROR("MQTT", "Failed to subscribe to %s", MQTT_TOPIC_RX_CMD_CONTROL_TEST_DATA_MODE);
    }

    if (client_.subscribe(MQTT_TOPIC_RX_CMD_CONTROL_EVENT_LOGS_CLEAR)) {
        LOG_INFO("MQTT", "Subscribed to control topic: %s", MQTT_TOPIC_RX_CMD_CONTROL_EVENT_LOGS_CLEAR);
    } else {
        LOG_ERROR("MQTT", "Failed to subscribe to %s", MQTT_TOPIC_RX_CMD_CONTROL_EVENT_LOGS_CLEAR);
    }

    if (client_.subscribe(MQTT_TOPIC_RX_CMD_CONTROL_REBOOT)) {
        LOG_INFO("MQTT", "Subscribed to control topic: %s", MQTT_TOPIC_RX_CMD_CONTROL_REBOOT);
    } else {
        LOG_ERROR("MQTT", "Failed to subscribe to %s", MQTT_TOPIC_RX_CMD_CONTROL_REBOOT);
    }

    if (client_.subscribe(MQTT_TOPIC_RX_CMD_CONTROL_OTA_START)) {
        LOG_INFO("MQTT", "Subscribed to control topic: %s", MQTT_TOPIC_RX_CMD_CONTROL_OTA_START);
    } else {
        LOG_ERROR("MQTT", "Failed to subscribe to %s", MQTT_TOPIC_RX_CMD_CONTROL_OTA_START);
    }

    if (client_.subscribe(MQTT_TOPIC_RX_CMD_CONTROL_COMPONENT_APPLY)) {
        LOG_INFO("MQTT", "Subscribed to component apply topic: %s", MQTT_TOPIC_RX_CMD_CONTROL_COMPONENT_APPLY);
    } else {
        LOG_ERROR("MQTT", "Failed to subscribe to %s", MQTT_TOPIC_RX_CMD_CONTROL_COMPONENT_APPLY);
    }

    if (client_.subscribe(MQTT_TOPIC_RX_CMD_REFRESH_POWER)) {
        LOG_INFO("MQTT", "Subscribed to refresh topic: %s", MQTT_TOPIC_RX_CMD_REFRESH_POWER);
    } else {
        LOG_ERROR("MQTT", "Failed to subscribe to %s", MQTT_TOPIC_RX_CMD_REFRESH_POWER);
    }

    if (client_.subscribe(MQTT_TOPIC_RX_CMD_REFRESH_BATTERY)) {
        LOG_INFO("MQTT", "Subscribed to refresh topic: %s", MQTT_TOPIC_RX_CMD_REFRESH_BATTERY);
    } else {
        LOG_ERROR("MQTT", "Failed to subscribe to %s", MQTT_TOPIC_RX_CMD_REFRESH_BATTERY);
    }

    if (client_.subscribe(MQTT_TOPIC_RX_CMD_REFRESH_NETWORK)) {
        LOG_INFO("MQTT", "Subscribed to refresh topic: %s", MQTT_TOPIC_RX_CMD_REFRESH_NETWORK);
    } else {
        LOG_ERROR("MQTT", "Failed to subscribe to %s", MQTT_TOPIC_RX_CMD_REFRESH_NETWORK);
    }

    if (client_.subscribe(MQTT_TOPIC_RX_CMD_REFRESH_MQTT)) {
        LOG_INFO("MQTT", "Subscribed to refresh topic: %s", MQTT_TOPIC_RX_CMD_REFRESH_MQTT);
    } else {
        LOG_ERROR("MQTT", "Failed to subscribe to %s", MQTT_TOPIC_RX_CMD_REFRESH_MQTT);
    }

    if (client_.subscribe(MQTT_TOPIC_RX_CMD_REFRESH_SETTINGS)) {
        LOG_INFO("MQTT", "Subscribed to refresh topic: %s", MQTT_TOPIC_RX_CMD_REFRESH_SETTINGS);
    } else {
        LOG_ERROR("MQTT", "Failed to subscribe to %s", MQTT_TOPIC_RX_CMD_REFRESH_SETTINGS);
    }

    if (client_.subscribe(MQTT_TOPIC_RX_CMD_REFRESH_CATALOG_BATTERY)) {
        LOG_INFO("MQTT", "Subscribed to refresh topic: %s", MQTT_TOPIC_RX_CMD_REFRESH_CATALOG_BATTERY);
    } else {
        LOG_ERROR("MQTT", "Failed to subscribe to %s", MQTT_TOPIC_RX_CMD_REFRESH_CATALOG_BATTERY);
    }

    if (client_.subscribe(MQTT_TOPIC_RX_CMD_REFRESH_CATALOG_INVERTER)) {
        LOG_INFO("MQTT", "Subscribed to refresh topic: %s", MQTT_TOPIC_RX_CMD_REFRESH_CATALOG_INVERTER);
    } else {
        LOG_ERROR("MQTT", "Failed to subscribe to %s", MQTT_TOPIC_RX_CMD_REFRESH_CATALOG_INVERTER);
    }

    if (client_.subscribe(MQTT_TOPIC_RX_CMD_REFRESH_LED)) {
        LOG_INFO("MQTT", "Subscribed to refresh topic: %s", MQTT_TOPIC_RX_CMD_REFRESH_LED);
    } else {
        LOG_ERROR("MQTT", "Failed to subscribe to %s", MQTT_TOPIC_RX_CMD_REFRESH_LED);
    }

#endif
}

void MqttManager::on_connection_failed() {
    LOG_WARN("MQTT", "Connection failed");

    // Ensure underlying socket is fully closed before backoff/retry
    client_.disconnect();
    MqttLogger::instance().set_mqtt_available(false);
    
    transition_to(MqttState::CONNECTION_FAILED);
    connected_ = false;  // Legacy flag
    failed_connections_++;
    
    // Exponential backoff
    uint32_t old_delay = current_retry_delay_;
    current_retry_delay_ = (uint32_t)(current_retry_delay_ * RETRY_BACKOFF_MULTIPLIER);
    
    if (current_retry_delay_ > MAX_RETRY_DELAY_MS) {
        current_retry_delay_ = MAX_RETRY_DELAY_MS;
    }
    
    LOG_WARN("MQTT", "Next retry in %lu seconds (previous delay: %lu ms)",
            current_retry_delay_ / 1000, old_delay);
}

void MqttManager::on_network_error() {
    LOG_WARN("MQTT", "Network unavailable");
    transition_to(MqttState::NETWORK_ERROR);
    connected_ = false;  // Legacy flag
    MqttLogger::instance().set_mqtt_available(false);
}

void MqttManager::transition_to(MqttState new_state) {
    if (new_state != state_) {
        state_ = new_state;
        state_enter_time_ = millis();
    }
}

MqttStatistics MqttManager::get_statistics() const {
    MqttStatistics stats;
    stats.total_connections = total_connections_;
    stats.failed_connections = failed_connections_;
    stats.total_messages_published = total_messages_published_;
    stats.current_retry_delay_ms = current_retry_delay_;
    stats.uptime_ms = millis() - initialization_time_;
    stats.time_in_current_state_ms = millis() - state_enter_time_;
    stats.current_state = state_;
    return stats;
}

bool MqttManager::ensure_publish_buffer(size_t required_bytes) {
    if (required_bytes == 0) {
        return false;
    }

    if (publish_buffer_ != nullptr && publish_buffer_capacity_ >= required_bytes) {
        return true;
    }

    if (publish_buffer_ != nullptr) {
        free(publish_buffer_);
        publish_buffer_ = nullptr;
        publish_buffer_capacity_ = 0;
    }

    publish_buffer_ = static_cast<char*>(ps_malloc(required_bytes));
    if (!publish_buffer_) {
        LOG_ERROR("MQTT", "Failed to allocate %u-byte PSRAM publish buffer",
                  (unsigned)required_bytes);
        return false;
    }

    publish_buffer_capacity_ = required_bytes;
    LOG_DEBUG("MQTT", "Allocated reusable PSRAM publish buffer (%u bytes)",
              (unsigned)publish_buffer_capacity_);
    return true;
}

bool MqttManager::connect() {
    if (!config::features::MQTT_ENABLED) {
        return false;
    }

    LOG_DEBUG("MQTT", "Legacy connect() wrapper invoked; forwarding to state machine connection path");
    attempt_connection();
    return is_connected();
}

bool MqttManager::publish_data(int soc, long power, const char* timestamp, bool eth_connected) {
    if (!is_connected()) return false;

    const int32_t voltage_mv = static_cast<int32_t>(datalayer.battery.status.voltage_dV) * 100;
    const int32_t current_ma = static_cast<int32_t>(datalayer.battery.status.current_dA) * 100;
    // Battery pack temperature (from BMS, in deci-Celsius → convert to centi-Celsius)
    const int16_t battery_temp_centi_c = static_cast<int16_t>(datalayer.battery.status.temperature_max_dC) * 10;
    const uint8_t bms_status = static_cast<uint8_t>(datalayer.battery.status.real_bms_status);

    static uint32_t battery_live_seq = 0;

    snprintf(payload_buffer_, sizeof(payload_buffer_),
             R"({"soc":%d,"power":%ld,"voltage_mv":%ld,"current_ma":%ld,"battery_temp_centi_c":%d,"bms_status":%u,"max_charge_power_w":%lu,"max_discharge_power_w":%lu,"seq":%lu,"uptime_ms":%lu,"ts_ms":%lu,"time":"%s","eth_present":%s})",
             soc,
             power,
             static_cast<long>(voltage_mv),
             static_cast<long>(current_ma),
             static_cast<int>(battery_temp_centi_c),
             static_cast<unsigned>(bms_status),
             static_cast<unsigned long>(datalayer.battery.status.max_charge_power_W),
             static_cast<unsigned long>(datalayer.battery.status.max_discharge_power_W),
             static_cast<unsigned long>(++battery_live_seq),
             static_cast<unsigned long>(millis()),
             static_cast<unsigned long>(millis()),
             timestamp,
             eth_connected ? "true" : "false");
    
    bool success = client_.publish(MQTT_TOPIC_TX_STATE_BATTERY_LIVE, payload_buffer_, false);
    
    if (success) {
        LOG_DEBUG("MQTT", "Published: %s", payload_buffer_);
        total_messages_published_++;
    } else {
        LOG_ERROR("MQTT", "Publish failed");
    }
    
    return success;
}

bool MqttManager::publish_heartbeat(bool eth_connected) {
#if MQTT_FEATURE_HEARTBEAT
    if (!is_connected()) {
        return false;
    }

    static uint32_t heartbeat_seq = 0;
    const uint32_t now_ms = millis();
    const uint64_t unix_time = TimeManager::instance().get_unix_time();
    const int16_t utc_offset_min = get_cached_utc_offset_min();
    const uint8_t time_source = TimeManager::instance().get_time_source_byte();
    const uint8_t heartbeat_flags = is_geolocation_configured() ? HEARTBEAT_FLAG_GEOLOCATION_VALID : 0;
    // Use ESP32 internal chip temperature (not battery pack temperature)
    float chip_temp_c = temperatureRead();
    // Filter out the erroneous 0x42555555 value the ESP32 sensor sometimes returns (= 53.33°C)
    union { float f; uint32_t u; } chip_temp_check = { .f = chip_temp_c };
    if (chip_temp_check.u == 0x42555555u) { chip_temp_c = 0.0f; }
    const int16_t temperature_centi_c = static_cast<int16_t>(chip_temp_c * 100.0f);
    uint8_t tx_mac_bytes[6] = {0};
    esp_wifi_get_mac(WIFI_IF_STA, tx_mac_bytes);
    char tx_mac_str[18];
    snprintf(tx_mac_str,
             sizeof(tx_mac_str),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             tx_mac_bytes[0], tx_mac_bytes[1], tx_mac_bytes[2],
             tx_mac_bytes[3], tx_mac_bytes[4], tx_mac_bytes[5]);

    snprintf(payload_buffer_, sizeof(payload_buffer_),
             R"({"ts_ms":%lu,"uptime_ms":%lu,"unix_time":%llu,"utc_offset_min":%d,"time_source":%u,"heartbeat_flags":%u,"temperature_centi_c":%d,"tx_mac":"%s","seq":%lu,"status":"ok","mqtt_connected":true,"ethernet_connected":%s})",
             static_cast<unsigned long>(now_ms),
             static_cast<unsigned long>(now_ms),
             static_cast<unsigned long long>(unix_time),
             static_cast<int>(utc_offset_min),
             static_cast<unsigned>(time_source),
             static_cast<unsigned>(heartbeat_flags),
             static_cast<int>(temperature_centi_c),
             tx_mac_str,
             static_cast<unsigned long>(++heartbeat_seq),
             eth_connected ? "true" : "false");

    const bool success = client_.publish(MQTT_TOPIC_TX_HEARTBEAT, payload_buffer_, false);
    if (success) {
        total_messages_published_++;
    } else {
        LOG_WARN("MQTT", "Heartbeat publish failed");
    }
    return success;
#else
    (void)eth_connected;
    return false;
#endif
}

void MqttManager::disconnect() {
    if (connected_) {
        LOG_INFO("MQTT", "Disconnecting from broker...");
        client_.publish(config::get_mqtt_config().topics.status, "offline", true);
        client_.disconnect();
        connected_ = false;
        // Give time for disconnect to complete
        delay(100);
        LOG_INFO("MQTT", "Disconnected gracefully");
    }

    // Ethernet-triggered disconnect is intentional: force stable network-error state
    // and reset reconnect backoff baseline for next cable restore.
    transition_to(MqttState::NETWORK_ERROR);
    current_retry_delay_ = INITIAL_RETRY_DELAY_MS;
    MqttLogger::instance().set_mqtt_available(false);
}

bool MqttManager::publish_status(const char* message, bool retained) {
    if (!is_connected()) return false;
    return client_.publish(config::get_mqtt_config().topics.status, message, retained);
}

bool MqttManager::publish_static_specs() {
    // Legacy combined static payload publisher is retired in MQTT-only mode.
    // Per-model retained topics are now the authoritative source of truth.
    if (!is_connected()) return false;
    return true;
}

bool MqttManager::publish_battery_specs() {
    if (!is_connected()) return false;

    constexpr size_t kBufferSize = 512;
    if (!ensure_publish_buffer(kBufferSize)) {
        return false;
    }

    size_t len = StaticData::serialize_battery_specs(publish_buffer_, kBufferSize);
    
    bool success = false;
    if (len > 0) {
        success = client_.publish(MQTT_TOPIC_TX_STATE_STATIC_BATTERY, publish_buffer_, true);
        if (success) {
            LOG_DEBUG("MQTT", "Published battery specs (%u bytes)", len);
        }
    }

    return success;
}

bool MqttManager::publish_cell_data() {
    if (!is_connected()) {
        return false;
    }

    if (cell_data_subscriptions_.empty()) {
        LOG_DEBUG("MQTT", "No cell_data stream subscribers; skipping cell_data publish");
        return true;
    }

    // Needs 6KB for 96 cells + balancing + metadata
    constexpr size_t kBufferSize = 6144;
    if (!ensure_publish_buffer(kBufferSize)) {
        return false;
    }

    size_t len = StaticData::serialize_cell_data(publish_buffer_, kBufferSize);
    
    bool success = false;
    if (len > 0) {
        if (len > kChunkThreshold) {
            // §17: Payload exceeds threshold – send as kChunkPayloadLen-byte chunks
            success = publish_in_chunks(client_,
                                        MQTT_TOPIC_TX_STATE_CELL_DATA_CHUNK,
                                        publish_buffer_, len);
            if (success) {
                const size_t parts = (len + kChunkPayloadLen - 1u) / kChunkPayloadLen;
                LOG_DEBUG("MQTT", "Published cell data chunked (%u bytes, %u parts)",
                          static_cast<unsigned>(len), static_cast<unsigned>(parts));
            } else {
                LOG_ERROR("MQTT", "Failed to publish chunked cell data");
            }
        } else {
            success = client_.publish(MQTT_TOPIC_TX_STATE_CELL_DATA_CHUNK,
                                      publish_buffer_, false);
            if (success) {
                LOG_DEBUG("MQTT", "Published cell data (%u bytes)",
                          static_cast<unsigned>(len));
            }
        }
    } else {
        LOG_DEBUG("MQTT", "serialize_cell_data returned 0 bytes, skipping publish");
    }

    return success;
}

bool MqttManager::publish_inverter_specs() {
    if (!is_connected()) return false;

    constexpr size_t kBufferSize = 768;
    if (!ensure_publish_buffer(kBufferSize)) {
        return false;
    }

    size_t len = StaticData::serialize_inverter_specs(publish_buffer_, kBufferSize);
    
    bool success = false;
    if (len > 0) {
        success = client_.publish(MQTT_TOPIC_TX_STATE_STATIC_INVERTER, publish_buffer_, true);
        if (success) {
            LOG_DEBUG("MQTT", "Published inverter specs (%u bytes)", len);
        }
    }

    return success;
}

bool MqttManager::publish_static_network() {
    if (!is_connected()) return false;

    auto& eth = EthernetManager::instance();
    const IPAddress current_ip = eth.get_local_ip();
    const IPAddress gateway    = eth.get_gateway_ip();
    const IPAddress subnet     = eth.get_subnet_mask();
    const IPAddress static_ip  = eth.get_static_ip();
    const IPAddress static_gw  = eth.get_static_gateway();
    const IPAddress static_sn  = eth.get_static_subnet_mask();
    const IPAddress dns1       = eth.get_static_dns_primary();
    const IPAddress dns2       = eth.get_static_dns_secondary();
    const bool use_static      = eth.is_static_ip();

    constexpr size_t kBufSize = 512;
    if (!ensure_publish_buffer(kBufSize)) return false;

    snprintf(publish_buffer_, kBufSize,
             R"({"use_static_ip":%s,"current_ip":"%d.%d.%d.%d","gateway":"%d.%d.%d.%d",)"
             R"("subnet":"%d.%d.%d.%d","static_ip":"%d.%d.%d.%d",)"
             R"("static_gateway":"%d.%d.%d.%d","static_subnet":"%d.%d.%d.%d",)"
             R"("dns_primary":"%d.%d.%d.%d","dns_secondary":"%d.%d.%d.%d"})",
             use_static ? "true" : "false",
             current_ip[0], current_ip[1], current_ip[2], current_ip[3],
             gateway[0],    gateway[1],    gateway[2],    gateway[3],
             subnet[0],     subnet[1],     subnet[2],     subnet[3],
             static_ip[0],  static_ip[1],  static_ip[2],  static_ip[3],
             static_gw[0],  static_gw[1],  static_gw[2],  static_gw[3],
             static_sn[0],  static_sn[1],  static_sn[2],  static_sn[3],
             dns1[0],       dns1[1],       dns1[2],       dns1[3],
             dns2[0],       dns2[1],       dns2[2],       dns2[3]);

    const bool ok = client_.publish(MQTT_TOPIC_TX_STATE_STATIC_NETWORK, publish_buffer_, true);
    if (ok) {
        total_messages_published_++;
        LOG_INFO("MQTT", "Published static/network (%u bytes)", (unsigned)strlen(publish_buffer_));
    } else {
        LOG_WARN("MQTT", "Failed to publish static/network");
    }
    return ok;
}

bool MqttManager::publish_static_mqtt() {
    if (!is_connected()) return false;

    const bool      enabled   = MqttConfigManager::isEnabled();
    const IPAddress server    = MqttConfigManager::getServer();
    const uint16_t  port      = MqttConfigManager::getPort();
    const char*     username  = MqttConfigManager::getUsername();
    const char*     client_id = MqttConfigManager::getClientId();

    constexpr size_t kBufSize = 256;
    if (!ensure_publish_buffer(kBufSize)) return false;

    snprintf(publish_buffer_, kBufSize,
             R"({"enabled":%s,"server":"%d.%d.%d.%d","port":%u,"username":"%s","client_id":"%s"})",
             enabled ? "true" : "false",
             server[0], server[1], server[2], server[3],
             static_cast<unsigned>(port),
             username  ? username  : "",
             client_id ? client_id : "");

    const bool ok = client_.publish(MQTT_TOPIC_TX_STATE_STATIC_MQTT, publish_buffer_, true);
    if (ok) {
        total_messages_published_++;
        LOG_INFO("MQTT", "Published static/mqtt (%u bytes)", (unsigned)strlen(publish_buffer_));
    } else {
        LOG_WARN("MQTT", "Failed to publish static/mqtt");
    }
    return ok;
}

bool MqttManager::publish_static_power() {
    if (!is_connected()) return false;

    constexpr size_t kBufSize = 768;
    if (!ensure_publish_buffer(kBufSize)) return false;

    size_t len = StaticData::serialize_charger_specs(publish_buffer_, kBufSize);
    if (len == 0) {
        LOG_WARN("MQTT", "Failed to serialize power specs");
        return false;
    }

    const bool ok = client_.publish(MQTT_TOPIC_TX_STATE_STATIC_POWER, publish_buffer_, true);
    if (ok) {
        total_messages_published_++;
        LOG_INFO("MQTT", "Published static/power (%u bytes)", (unsigned)len);
    } else {
        LOG_WARN("MQTT", "Failed to publish static/power");
    }
    return ok;
}

bool MqttManager::publish_static_led() {
    if (!is_connected()) return false;

    constexpr size_t kBufSize = 512;
    if (!ensure_publish_buffer(kBufSize)) return false;

    size_t len = StaticData::serialize_system_specs(publish_buffer_, kBufSize);
    if (len == 0) {
        LOG_WARN("MQTT", "Failed to serialize LED/system specs");
        return false;
    }

    const bool ok = client_.publish(MQTT_TOPIC_TX_STATE_STATIC_LED, publish_buffer_, true);
    if (ok) {
        total_messages_published_++;
        LOG_INFO("MQTT", "Published static/led (%u bytes)", (unsigned)len);
    } else {
        LOG_WARN("MQTT", "Failed to publish static/led");
    }
    return ok;
}

bool MqttManager::publish_static_settings() {
    if (!is_connected()) return false;

    constexpr size_t kBufSize = 1792;
    if (!ensure_publish_buffer(kBufSize)) return false;

    const auto& settings = SettingsManager::instance();

    DynamicJsonDocument doc(kBufSize);
    doc["schema"] = 1;

    JsonObject battery = doc.createNestedObject("battery");
    battery["capacity_wh"] = settings.get_battery_capacity_wh();
    battery["max_voltage_mv"] = settings.get_battery_max_voltage_mv();
    battery["min_voltage_mv"] = settings.get_battery_min_voltage_mv();
    battery["max_charge_current_a"] = settings.get_battery_max_charge_current_a();
    battery["max_discharge_current_a"] = settings.get_battery_max_discharge_current_a();
    battery["soc_high_limit"] = settings.get_battery_soc_high_limit();
    battery["soc_low_limit"] = settings.get_battery_soc_low_limit();
    battery["cell_count"] = settings.get_battery_cell_count();
    battery["chemistry"] = settings.get_battery_chemistry();
    battery["version"] = settings.get_battery_settings_version();

    JsonObject battery_emu = doc.createNestedObject("battery_emulator");
    battery_emu["double_battery"] = settings.get_battery_double_enabled();
    battery_emu["pack_max_voltage_dV"] = settings.get_battery_pack_max_voltage_dV();
    battery_emu["pack_min_voltage_dV"] = settings.get_battery_pack_min_voltage_dV();
    battery_emu["cell_max_voltage_mV"] = settings.get_battery_cell_max_voltage_mV();
    battery_emu["cell_min_voltage_mV"] = settings.get_battery_cell_min_voltage_mV();
    battery_emu["soc_estimated"] = settings.get_battery_soc_estimated();
    battery_emu["led_mode"] = settings.get_battery_led_mode();

    JsonObject power = doc.createNestedObject("power");
    power["charge_w"] = settings.get_power_charge_w();
    power["discharge_w"] = settings.get_power_discharge_w();
    power["max_precharge_ms"] = settings.get_power_max_precharge_ms();
    power["precharge_duration_ms"] = settings.get_power_precharge_duration_ms();
    power["equipment_stop_type"] = settings.get_power_equipment_stop_type();
    power["external_precharge_enabled"] = settings.get_power_external_precharge_enabled();
    power["no_inverter_disconnect_contactor"] = settings.get_power_no_inverter_disconnect_contactor();
    power["version"] = settings.get_power_settings_version();

    JsonObject can = doc.createNestedObject("can");
    can["frequency_khz"] = settings.get_can_frequency_khz();
    can["fd_frequency_mhz"] = settings.get_can_fd_frequency_mhz();
    can["sofar_id"] = settings.get_can_sofar_id();
    can["pylon_send_interval_ms"] = settings.get_can_pylon_send_interval_ms();
    can["use_canfd_as_classic"] = settings.get_can_use_canfd_as_classic();
    can["version"] = settings.get_can_settings_version();

    JsonObject contactor = doc.createNestedObject("contactor");
    contactor["control_enabled"] = settings.get_contactor_control_enabled();
    contactor["nc_contactor"] = settings.get_contactor_nc_mode();
    contactor["pwm_frequency_hz"] = settings.get_contactor_pwm_frequency_hz();
    contactor["pwm_control_enabled"] = settings.get_contactor_pwm_control_enabled();
    contactor["pwm_hold_duty"] = settings.get_contactor_pwm_hold_duty();
    contactor["periodic_bms_reset"] = settings.get_contactor_periodic_bms_reset();
    contactor["bms_first_align_enabled"] = settings.get_contactor_bms_first_align_enabled();
    contactor["bms_first_align_target_minutes"] = settings.get_contactor_bms_first_align_target_minutes();
    contactor["version"] = settings.get_contactor_settings_version();

    const size_t len = serializeJson(doc, publish_buffer_, kBufSize);
    if (len == 0) {
        LOG_WARN("MQTT", "Failed to serialize static/settings");
        return false;
    }

    const bool ok = client_.publish(MQTT_TOPIC_TX_STATE_STATIC_SETTINGS, publish_buffer_, true);
    if (ok) {
        total_messages_published_++;
        LOG_INFO("MQTT", "Published static/settings (%u bytes)", (unsigned)len);
    } else {
        LOG_WARN("MQTT", "Failed to publish static/settings");
    }
    return ok;
}

bool MqttManager::publish_meta_version() {
    if (!is_connected()) return false;

    snprintf(payload_buffer_, sizeof(payload_buffer_),
             R"({"device":"%s","firmware":"%s","firmware_number":%lu,"protocol":%u,"build_date":"%s","build_time":"%s","schema":1,"ts_ms":%lu})",
             DEVICE_NAME,
             FW_VERSION_STRING,
             static_cast<unsigned long>(FW_VERSION_NUMBER),
             static_cast<unsigned>(PROTOCOL_VERSION),
             FW_BUILD_DATE,
             FW_BUILD_TIME,
             static_cast<unsigned long>(millis()));

    const bool ok = client_.publish(MQTT_TOPIC_TX_META_VERSION, payload_buffer_, true);
    if (ok) {
        total_messages_published_++;
        LOG_INFO("MQTT", "Published meta/version");
    } else {
        LOG_WARN("MQTT", "Failed to publish meta/version");
    }
    return ok;
}

bool MqttManager::publish_meta_schema_versions() {
    if (!is_connected()) return false;

    const auto& settings = SettingsManager::instance();

    snprintf(payload_buffer_, sizeof(payload_buffer_),
             R"({"schema":1,"battery":%lu,"power":%lu,"inverter":%lu,"can":%lu,"contactor":%lu,"network":%lu,"mqtt":%lu,"catalog_battery":%u,"catalog_inverter":%u,"ts_ms":%lu})",
             static_cast<unsigned long>(settings.get_battery_settings_version()),
             static_cast<unsigned long>(settings.get_power_settings_version()),
             static_cast<unsigned long>(settings.get_inverter_settings_version()),
             static_cast<unsigned long>(settings.get_can_settings_version()),
             static_cast<unsigned long>(settings.get_contactor_settings_version()),
             static_cast<unsigned long>(EthernetManager::instance().get_network_config_version()),
             static_cast<unsigned long>(MqttConfigManager::getConfigVersion()),
             static_cast<unsigned>(mqtt_battery_type_catalog_version()),
             static_cast<unsigned>(mqtt_inverter_type_catalog_version()),
             static_cast<unsigned long>(millis()));

    const bool ok = client_.publish(MQTT_TOPIC_TX_META_SCHEMA_VERSIONS, payload_buffer_, true);
    if (ok) {
        total_messages_published_++;
        LOG_INFO("MQTT", "Published meta/schema_versions");
    } else {
        LOG_WARN("MQTT", "Failed to publish meta/schema_versions");
    }
    return ok;
}

bool MqttManager::publish_meta_runtime() {
    if (!is_connected()) return false;

    const bool eth_connected = EthernetManager::instance().is_fully_ready();
    const bool mqtt_connected = is_connected();
    const uint64_t unix_time = TimeManager::instance().get_unix_time();
    const int16_t utc_offset_min = get_cached_utc_offset_min();
    const uint8_t time_source = TimeManager::instance().get_time_source_byte();
    const uint8_t heartbeat_flags = is_geolocation_configured() ? HEARTBEAT_FLAG_GEOLOCATION_VALID : 0;
    uint8_t tx_mac_bytes[6] = {0};
    esp_wifi_get_mac(WIFI_IF_STA, tx_mac_bytes);
    char tx_mac_str[18];
    snprintf(tx_mac_str,
             sizeof(tx_mac_str),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             tx_mac_bytes[0], tx_mac_bytes[1], tx_mac_bytes[2],
             tx_mac_bytes[3], tx_mac_bytes[4], tx_mac_bytes[5]);

    snprintf(payload_buffer_, sizeof(payload_buffer_),
             R"({"mqtt_connected":%s,"ethernet_connected":%s,"uptime_ms":%lu,"unix_time":%llu,"utc_offset_min":%d,"time_source":%u,"heartbeat_flags":%u,"tx_mac":"%s","ts_ms":%lu})",
             mqtt_connected ? "true" : "false",
             eth_connected ? "true" : "false",
             static_cast<unsigned long>(millis()),
             static_cast<unsigned long long>(unix_time),
             static_cast<int>(utc_offset_min),
             static_cast<unsigned>(time_source),
             static_cast<unsigned>(heartbeat_flags),
             tx_mac_str,
             static_cast<unsigned long>(millis()));

    const bool ok = client_.publish(MQTT_TOPIC_TX_META_RUNTIME, payload_buffer_, true);
    if (ok) {
        total_messages_published_++;
        LOG_INFO("MQTT", "Published meta/runtime");
    } else {
        LOG_WARN("MQTT", "Failed to publish meta/runtime");
    }
    return ok;
}

bool MqttManager::publish_runtime_led() {
    if (!is_connected()) return false;

    static uint32_t led_seq = 0;
    const EMULATOR_STATUS status = get_emulator_status();
    const uint8_t color = led_color_from_emulator_status(status);
    const uint8_t effect = led_effect_from_mode(static_cast<uint8_t>(datalayer.battery.status.led_mode));

    snprintf(payload_buffer_, sizeof(payload_buffer_),
             R"({"color":%u,"effect":%u,"status":"%s","seq":%lu,"ts_ms":%lu})",
             static_cast<unsigned>(color),
             static_cast<unsigned>(effect),
             get_emulator_status_string(status),
             static_cast<unsigned long>(++led_seq),
             static_cast<unsigned long>(millis()));

    const bool ok = client_.publish(MQTT_TOPIC_TX_STATE_RUNTIME_LED, payload_buffer_, false);
    if (ok) {
        total_messages_published_++;
    }
    return ok;
}

bool MqttManager::publish_runtime_system() {
    if (!is_connected()) return false;

    static uint32_t system_seq = 0;
    const EMULATOR_STATUS status = get_emulator_status();
    const uint8_t contactor_state = compose_contactor_state_bits();
    const uint8_t error_flags = compose_error_flags(status);
    const uint8_t warning_flags = compose_warning_flags(status);
    const uint32_t uptime_seconds = millis() / 1000UL;

    snprintf(payload_buffer_, sizeof(payload_buffer_),
             R"({"contactor_state":%u,"error_flags":%u,"warning_flags":%u,"uptime_seconds":%lu,"seq":%lu,"ts_ms":%lu})",
             static_cast<unsigned>(contactor_state),
             static_cast<unsigned>(error_flags),
             static_cast<unsigned>(warning_flags),
             static_cast<unsigned long>(uptime_seconds),
             static_cast<unsigned long>(++system_seq),
             static_cast<unsigned long>(millis()));

    const bool ok = client_.publish(MQTT_TOPIC_TX_STATE_RUNTIME_SYSTEM, payload_buffer_, false);
    if (ok) {
        total_messages_published_++;
    }
    return ok;
}

bool MqttManager::publish_runtime_charger() {
    if (!is_connected()) return false;

    static uint32_t charger_seq = 0;
    const auto& charger = datalayer.charger;
    const uint16_t hv_voltage_dV = static_cast<uint16_t>(charger.charger_stat_HVvol * 10.0f + 0.5f);
    const int16_t hv_current_dA = static_cast<int16_t>(charger.charger_stat_HVcur * 10.0f);
    const uint16_t lv_voltage_dV = static_cast<uint16_t>(charger.charger_stat_LVvol * 10.0f + 0.5f);
    const int16_t lv_current_dA = static_cast<int16_t>(charger.charger_stat_LVcur * 10.0f);
    const uint16_t ac_voltage_V = static_cast<uint16_t>(charger.charger_stat_ACvol + 0.5f);
    const int16_t ac_current_dA = static_cast<int16_t>(charger.charger_stat_ACcur * 10.0f);
    const uint16_t power_W = static_cast<uint16_t>((charger.charger_stat_HVvol * charger.charger_stat_HVcur) + 0.5f);
    const uint8_t charger_status = charger.charger_HV_enabled ? 1 : ((charger.CAN_charger_still_alive == 0) ? 2 : 0);

    snprintf(payload_buffer_, sizeof(payload_buffer_),
             R"({"hv_voltage_dV":%u,"hv_current_dA":%d,"lv_voltage_dV":%u,"lv_current_dA":%d,"ac_voltage_V":%u,"ac_current_dA":%d,"power_W":%u,"charger_status":%u,"seq":%lu,"ts_ms":%lu})",
             static_cast<unsigned>(hv_voltage_dV),
             static_cast<int>(hv_current_dA),
             static_cast<unsigned>(lv_voltage_dV),
             static_cast<int>(lv_current_dA),
             static_cast<unsigned>(ac_voltage_V),
             static_cast<int>(ac_current_dA),
             static_cast<unsigned>(power_W),
             static_cast<unsigned>(charger_status),
             static_cast<unsigned long>(++charger_seq),
             static_cast<unsigned long>(millis()));

    const bool ok = client_.publish(MQTT_TOPIC_TX_STATE_RUNTIME_CHARGER, payload_buffer_, false);
    if (ok) {
        total_messages_published_++;
    }
    return ok;
}

bool MqttManager::publish_runtime_inverter() {
    if (!is_connected()) return false;

    static uint32_t inverter_seq = 0;
    const auto& battery = datalayer.battery;
    const uint16_t ac_voltage_V = static_cast<uint16_t>(battery.status.voltage_dV / 10U);
    const uint16_t ac_frequency_dHz = 500;
    const int16_t ac_current_dA = battery.status.current_dA;
    const int32_t power_W = battery.status.active_power_W;
    uint8_t inverter_status = 0;
    if (battery.status.real_bms_status == BatteryEmulator_real_bms_status_enum::BMS_DISCONNECTED) {
        inverter_status = 2;
    } else if (datalayer.system.status.contactors_engaged != 0) {
        inverter_status = 1;
    }

    snprintf(payload_buffer_, sizeof(payload_buffer_),
             R"({"ac_voltage_V":%u,"ac_frequency_dHz":%u,"ac_current_dA":%d,"power_W":%ld,"inverter_status":%u,"seq":%lu,"ts_ms":%lu})",
             static_cast<unsigned>(ac_voltage_V),
             static_cast<unsigned>(ac_frequency_dHz),
             static_cast<int>(ac_current_dA),
             static_cast<long>(power_W),
             static_cast<unsigned>(inverter_status),
             static_cast<unsigned long>(++inverter_seq),
             static_cast<unsigned long>(millis()));

    const bool ok = client_.publish(MQTT_TOPIC_TX_STATE_RUNTIME_INVERTER, payload_buffer_, false);
    if (ok) {
        total_messages_published_++;
    }
    return ok;
}

bool MqttManager::publish_battery_type_catalog() {
    if (!is_connected()) return false;

    DynamicJsonDocument doc(4096);
    doc["catalog_version"] = mqtt_battery_type_catalog_version();
    JsonArray types = doc.createNestedArray("types");

#if CONFIG_CAN_ENABLED
    for (int id = 0; id < static_cast<int>(BatteryType::Highest); ++id) {
        const char* name = name_for_battery_type(static_cast<BatteryType>(id));
        if (!name || name[0] == '\0') {
            continue;
        }
        JsonObject obj = types.createNestedObject();
        obj["id"] = id;
        obj["name"] = name;
    }
#else
    JsonObject obj = types.createNestedObject();
    obj["id"] = 0;
    obj["name"] = "None";
#endif

    constexpr size_t kBufferSize = 4096;
    if (!ensure_publish_buffer(kBufferSize)) {
        return false;
    }

    const size_t len = serializeJson(doc, publish_buffer_, kBufferSize);
    bool success = false;
    if (len > 0) {
        success = client_.publish(MQTT_TOPIC_TX_STATE_STATIC_CATALOG_BATTERY, publish_buffer_, true);
        if (success) {
            LOG_INFO("MQTT", "Published battery type catalog (%u bytes)", (unsigned)len);
        }
    }

    return success;
}

bool MqttManager::publish_inverter_type_catalog() {
    if (!is_connected()) return false;

    DynamicJsonDocument doc(3072);
    doc["catalog_version"] = mqtt_inverter_type_catalog_version();
    JsonArray types = doc.createNestedArray("types");

#if CONFIG_CAN_ENABLED
    for (int id = 0; id < static_cast<int>(InverterProtocolType::Highest); ++id) {
        const char* name = name_for_inverter_type(static_cast<InverterProtocolType>(id));
        if (is_disabled_placeholder_label(name)) {
            name = inverter_catalog_fallback_name(static_cast<uint8_t>(id));
        }
        if (!name || name[0] == '\0') {
            continue;
        }
        JsonObject obj = types.createNestedObject();
        obj["id"] = id;
        obj["name"] = name;
    }
#else
    JsonObject obj = types.createNestedObject();
    obj["id"] = 0;
    obj["name"] = "None";
#endif

    constexpr size_t kBufferSize = 3072;
    if (!ensure_publish_buffer(kBufferSize)) {
        return false;
    }

    const size_t len = serializeJson(doc, publish_buffer_, kBufferSize);
    bool success = false;
    if (len > 0) {
        success = client_.publish(MQTT_TOPIC_TX_STATE_STATIC_CATALOG_INVERTER, publish_buffer_, true);
        if (success) {
            LOG_INFO("MQTT", "Published inverter type catalog (%u bytes)", (unsigned)len);
        }
    }

    return success;
}

static uint8_t map_event_level(EVENTS_LEVEL_TYPE level) {
    switch (level) {
        case EVENT_LEVEL_ERROR:   return 3;
        case EVENT_LEVEL_WARNING: return 4;
        case EVENT_LEVEL_INFO:    return 6;
        case EVENT_LEVEL_DEBUG:   return 7;
        case EVENT_LEVEL_UPDATE:  return 5;
        default:                  return 6;
    }
}

bool MqttManager::publish_event_logs() {
    if (!is_connected()) return false;

    const uint64_t now_ms = millis64();

    reap_expired_event_log_subscriptions(now_ms);

    std::vector<EventData> ordered;
    ordered.reserve(EVENT_NOF_EVENTS);

    uint32_t total_historical = 0;
    uint32_t error_historical = 0;
    uint32_t new_since_last_report_total = 0;
    uint32_t new_since_last_report_error = 0;

    // Collect all active events for snapshot publishing.
    // "new" status is still tracked via MQTTpublished flag per event.
    for (int i = 0; i < EVENT_NOF_EVENTS; i++) {
        const EVENTS_STRUCT_TYPE* event_ptr = get_event_pointer((EVENTS_ENUM_TYPE)i);
        if (event_ptr && event_ptr->occurences > 0) {
            ordered.push_back({(EVENTS_ENUM_TYPE)i, event_ptr});
            total_historical++;
            if (event_ptr->level == EVENT_LEVEL_ERROR) {
                error_historical++;
            }
            if (!event_ptr->MQTTpublished) {
                new_since_last_report_total += static_cast<uint32_t>(event_ptr->occurences);
                if (event_ptr->level == EVENT_LEVEL_ERROR) {
                    new_since_last_report_error += static_cast<uint32_t>(event_ptr->occurences);
                }
            }
        }
    }

    auto publish_summary = [&](uint32_t snapshot_id,
                               uint32_t event_count,
                               uint32_t batch_index,
                               uint32_t batch_count,
                               bool snapshot_complete) -> bool {
        snprintf(payload_buffer_, sizeof(payload_buffer_),
                 R"({"snapshot_id":%lu,"seq":%lu,"event_count":%lu,"total_historical":%lu,"error_historical":%lu,"new_since_last_report_total":%lu,"new_since_last_report_error":%lu,"batch_index":%lu,"batch_count":%lu,"snapshot_complete":%s,"ts_ms":%lu,"uptime_ms":%lu})",
                 static_cast<unsigned long>(snapshot_id),
                 static_cast<unsigned long>(snapshot_id),
                 static_cast<unsigned long>(event_count),
                 static_cast<unsigned long>(total_historical),
                 static_cast<unsigned long>(error_historical),
                 static_cast<unsigned long>(new_since_last_report_total),
                 static_cast<unsigned long>(new_since_last_report_error),
                 static_cast<unsigned long>(batch_index),
                 static_cast<unsigned long>(batch_count),
                 snapshot_complete ? "true" : "false",
                 static_cast<unsigned long>(millis()),
                 static_cast<unsigned long>(millis()));
        return client_.publish(MQTT_TOPIC_TX_STATE_EVENT_LOG_SUMMARY, payload_buffer_, false);
    };

    // Always publish summary so dashboard card can show event status without opening /events.
    if (event_log_subscriptions_.empty()) {
        LOG_DEBUG("MQTT", "No event log stream subscribers; publishing summary only");
        event_snapshot_offset_ = 0;
        event_snapshot_order_.clear();
        const uint32_t next_seq = static_cast<uint32_t>(event_snapshot_id_ + 1ULL);
        if (publish_summary(next_seq,
                            total_historical,
                            0,
                            total_historical > 0 ? 1 : 0,
                            true)) {
            event_snapshot_id_ = next_seq;
            return true;
        }
        LOG_ERROR("MQTT", "Failed to publish event log summary (no-subscriber path)");
        return false;
    }

    // If no events, skip publishing
    if (ordered.empty()) {
        LOG_DEBUG("MQTT", "No events available, publishing empty summary");
        event_snapshot_offset_ = 0;
        event_snapshot_order_.clear();
        const uint32_t next_seq = static_cast<uint32_t>(event_snapshot_id_ + 1ULL);
        if (publish_summary(next_seq, 0, 0, 0, true)) {
            event_snapshot_id_ = next_seq;
            return true;
        }
        LOG_ERROR("MQTT", "Failed to publish event log summary (empty path)");
        return false;
    }

    std::sort(ordered.begin(), ordered.end(), compareEventsByTimestampDesc);

    // Start a new snapshot if needed
    if (event_snapshot_order_.empty() || event_snapshot_offset_ == 0) {
        event_snapshot_id_++;
        event_snapshot_offset_ = 0;
        event_snapshot_order_.clear();
        event_snapshot_order_.reserve(ordered.size());
        for (const auto& item : ordered) {
            event_snapshot_order_.push_back(static_cast<int>(item.event_handle));
        }
    }

    const size_t total_events = event_snapshot_order_.size();
    const size_t max_events_per_message = config::event_logs::MAX_BATCH_SIZE;
    const size_t batch_count = (total_events + max_events_per_message - 1) / max_events_per_message;

    if (batch_count == 0 || event_snapshot_offset_ >= total_events) {
        // Defensive reset if snapshot state got stale
        event_snapshot_offset_ = 0;
        event_snapshot_order_.clear();
        return true;
    }

    const size_t batch_index = event_snapshot_offset_ / max_events_per_message;
    const size_t batch_end = std::min(event_snapshot_offset_ + max_events_per_message, total_events);
    const size_t events_in_batch = batch_end - event_snapshot_offset_;
    const bool snapshot_complete = (batch_end >= total_events);

    // Allocate JSON document in PSRAM
    DynamicJsonDocument doc(6144);
    doc["snapshot_id"] = static_cast<unsigned long long>(event_snapshot_id_);
    doc["batch_index"] = static_cast<uint32_t>(batch_index);
    doc["batch_count"] = static_cast<uint32_t>(batch_count);
    doc["snapshot_total"] = static_cast<uint32_t>(total_events);
    doc["snapshot_complete"] = snapshot_complete;
    doc["event_count"] = static_cast<uint32_t>(total_events);
    JsonArray events = doc.createNestedArray("events");

    std::vector<int> published_this_batch;
    published_this_batch.reserve(events_in_batch);

    for (size_t i = event_snapshot_offset_; i < batch_end; i++) {
        const EVENTS_ENUM_TYPE handle = static_cast<EVENTS_ENUM_TYPE>(event_snapshot_order_[i]);
        const EVENTS_STRUCT_TYPE* evt = get_event_pointer(handle);
        if (!evt || evt->occurences == 0) {
            continue;
        }

        JsonObject obj = events.createNestedObject();
        char event_message[384] = {0};
        const bool have_event_message =
            get_event_message(handle, event_message, sizeof(event_message), evt->data);
        obj["timestamp_ms"] = static_cast<unsigned long long>(evt->timestamp);
        obj["event_unix_ms"] = static_cast<unsigned long long>(evt->event_unix_ms);
        obj["event_utc_offset_min"] = static_cast<int16_t>(evt->event_utc_offset_min);
        obj["level"] = map_event_level(evt->level);
        obj["data"] = evt->data;
        obj["count"] = evt->occurences;
        obj["is_new"] = !evt->MQTTpublished;
        if (have_event_message) {
            // IMPORTANT: assign mutable char* directly so ArduinoJson copies the
            // content into the document. Avoid conditional const char* paths
            // here, because they can store linked pointers to stack buffers and
            // cause type/message mismatches after the loop.
            obj["message"] = event_message;
        } else {
            obj["message"] = "";
        }
        obj["event"] = get_event_enum_string(handle);

        published_this_batch.push_back(static_cast<int>(handle));
    }

    constexpr size_t kBufferSize = 6144;
    if (!ensure_publish_buffer(kBufferSize)) {
        return false;
    }

    size_t len = serializeJson(doc, publish_buffer_, kBufferSize);
    bool success = false;
    if (len > 0) {
        if (len > kChunkThreshold) {
            // §17: Batch JSON exceeds threshold – send as kChunkPayloadLen-byte chunks
            success = publish_in_chunks(client_,
                                        MQTT_TOPIC_TX_STATE_EVENT_LOG_CHUNK,
                                        publish_buffer_, len);
        } else {
            success = client_.publish(MQTT_TOPIC_TX_STATE_EVENT_LOG_CHUNK,
                                      publish_buffer_, false);
        }
        if (success) {
            LOG_DEBUG("MQTT", "Published event snapshot batch %u/%u (%u event(s), %u bytes)",
                      static_cast<unsigned>(batch_index + 1),
                      static_cast<unsigned>(batch_count),
                      static_cast<unsigned>(published_this_batch.size()),
                      static_cast<unsigned>(len));

            // Emit compact summary for dashboards.
            (void)publish_summary(static_cast<uint32_t>(event_snapshot_id_),
                                  static_cast<uint32_t>(total_events),
                                  static_cast<uint32_t>(batch_index),
                                  static_cast<uint32_t>(batch_count),
                                  snapshot_complete);

            // Advance snapshot cursor
            event_snapshot_offset_ = batch_end;

            // Mark published events as no longer new only when full snapshot has completed.
            if (snapshot_complete) {
                for (const auto handle : event_snapshot_order_) {
                    set_event_MQTTpublished(static_cast<EVENTS_ENUM_TYPE>(handle));
                }

                event_snapshot_offset_ = 0;
                event_snapshot_order_.clear();
            }
        } else {
            LOG_ERROR("MQTT", "Failed to publish event logs");
        }
    } else {
        LOG_ERROR("MQTT", "Failed to serialize event logs");
    }

    return success;
}

void MqttManager::increment_event_log_subscribers() {
    if (!event_log_subscriptions_.empty()) {
        const uint64_t now_ms = millis64();
        for (auto& session : event_log_subscriptions_) {
            session.last_activity_ms = now_ms;
        }
        LOG_INFO("MQTT", "Event log stream subscribe refreshed, active count: %d",
                 static_cast<int>(event_log_subscriptions_.size()));
        return;
    }

    EventLogSubscription session{};
    session.id = next_event_log_subscription_id_++;
    session.created_ms = millis64();
    session.last_activity_ms = session.created_ms;
    event_log_subscriptions_.push_back(session);

    event_snapshot_offset_ = 0;
    event_snapshot_order_.clear();
    LOG_INFO("MQTT", "Event log subscriber created (id=%lu), active count: %d",
             static_cast<unsigned long>(session.id),
             static_cast<int>(event_log_subscriptions_.size()));
}

void MqttManager::decrement_event_log_subscribers() {
    if (!event_log_subscriptions_.empty()) {
        const auto removed = event_log_subscriptions_.back();
        event_log_subscriptions_.pop_back();

        if (event_log_subscriptions_.empty()) {
            event_snapshot_offset_ = 0;
            event_snapshot_order_.clear();
        }

        LOG_INFO("MQTT", "Event log subscriber removed (id=%lu), active count: %d",
                 static_cast<unsigned long>(removed.id),
                 static_cast<int>(event_log_subscriptions_.size()));
    }
}

void MqttManager::increment_cell_data_subscribers() {
    if (!cell_data_subscriptions_.empty()) {
        const uint64_t now_ms = millis64();
        for (auto& session : cell_data_subscriptions_) {
            session.last_activity_ms = now_ms;
        }
        LOG_INFO("MQTT", "Cell-data stream subscribe refreshed, active count: %d",
                 static_cast<int>(cell_data_subscriptions_.size()));
        return;
    }

    CellDataSubscription session{};
    session.id = next_cell_data_subscription_id_++;
    session.created_ms = millis64();
    session.last_activity_ms = session.created_ms;
    cell_data_subscriptions_.push_back(session);

    LOG_INFO("MQTT", "Cell-data stream subscriber created (id=%lu), active count: %d",
             static_cast<unsigned long>(session.id),
             static_cast<int>(cell_data_subscriptions_.size()));
}

void MqttManager::decrement_cell_data_subscribers() {
    if (!cell_data_subscriptions_.empty()) {
        const auto removed = cell_data_subscriptions_.back();
        cell_data_subscriptions_.pop_back();

        LOG_INFO("MQTT", "Cell-data stream subscriber removed (id=%lu), active count: %d",
                 static_cast<unsigned long>(removed.id),
                 static_cast<int>(cell_data_subscriptions_.size()));
    }
}

void MqttManager::reap_expired_event_log_subscriptions(uint64_t now_ms) {
    if (event_log_subscriptions_.empty()) {
        return;
    }

    const auto old_size = event_log_subscriptions_.size();
    event_log_subscriptions_.erase(
        std::remove_if(event_log_subscriptions_.begin(), event_log_subscriptions_.end(),
                       [now_ms](const EventLogSubscription& session) {
                           return (now_ms - session.last_activity_ms) > config::event_logs::SUBSCRIPTION_TTL_MS;
                       }),
        event_log_subscriptions_.end());

    const auto reaped = old_size - event_log_subscriptions_.size();
    if (reaped > 0) {
        event_log_ttl_reap_count_ += static_cast<uint32_t>(reaped);
        LOG_WARN("MQTT", "Reaped %u expired event-log subscription(s), active count: %d",
                 static_cast<unsigned>(reaped),
                 static_cast<int>(event_log_subscriptions_.size()));

        if (event_log_subscriptions_.empty()) {
            event_snapshot_offset_ = 0;
            event_snapshot_order_.clear();
        }
    }
}

void MqttManager::reap_expired_cell_data_subscriptions(uint64_t now_ms) {
    (void)now_ms;
}

void MqttManager::loop() {
    if (client_.state() == MQTT_CONNECTED) {
        connected_ = true;
        client_.loop();
    } else {
        connected_ = false;
    }
}

bool MqttManager::publish_cached_ack_if_duplicate(const char* request_id) {
    if (request_id == nullptr || request_id[0] == '\0') {
        return false;
    }

    auto it = std::find_if(ack_cache_.begin(), ack_cache_.end(),
                           [request_id](const AckCacheEntry& entry) {
                               return strcmp(entry.request_id, request_id) == 0;
                           });

    if (it == ack_cache_.end()) {
        return false;
    }

    const bool published = client_.publish(it->topic, it->payload, false);
    if (published) {
        total_messages_published_++;
        LOG_INFO("MQTT", "Duplicate request_id=%s, replayed cached ACK on %s", request_id, it->topic);
    } else {
        LOG_WARN("MQTT", "Duplicate request_id=%s detected but ACK replay failed", request_id);
    }

    return true;
}

void MqttManager::remember_ack_payload(const char* request_id,
                                       const char* topic,
                                       const char* payload) {
    if (request_id == nullptr || request_id[0] == '\0' || topic == nullptr || payload == nullptr) {
        return;
    }

    auto it = std::find_if(ack_cache_.begin(), ack_cache_.end(),
                           [request_id](const AckCacheEntry& entry) {
                               return strcmp(entry.request_id, request_id) == 0;
                           });

    if (it == ack_cache_.end()) {
        if (ack_cache_.size() >= MAX_ACK_CACHE_ENTRIES) {
            ack_cache_.erase(ack_cache_.begin());
        }
        ack_cache_.push_back({});
        it = std::prev(ack_cache_.end());
    }

    strncpy(it->request_id, request_id, sizeof(it->request_id) - 1);
    it->request_id[sizeof(it->request_id) - 1] = '\0';

    strncpy(it->topic, topic, sizeof(it->topic) - 1);
    it->topic[sizeof(it->topic) - 1] = '\0';

    strncpy(it->payload, payload, sizeof(it->payload) - 1);
    it->payload[sizeof(it->payload) - 1] = '\0';

    it->ts_ms = millis();
}

bool MqttManager::publish_control_ack(const char* request_id,
                                      const char* action,
                                      bool success,
                                      const char* code,
                                      const char* message) {
    if (!is_connected()) {
        return false;
    }

    char generated_request_id[40] = {};
    const char* safe_request_id = normalized_request_id(request_id,
                                                        generated_request_id,
                                                        sizeof(generated_request_id));
    const char* safe_action = (action != nullptr) ? action : "control";
    const char* safe_code = (code != nullptr) ? code : (success ? "OK" : "ERROR");
    const char* safe_message = (message != nullptr) ? message : "";

    snprintf(payload_buffer_, sizeof(payload_buffer_),
             R"({"request_id":"%s","action":"%s","success":%s,"code":"%s","message":"%s","ts_ms":%lu})",
             safe_request_id,
             safe_action,
             success ? "true" : "false",
             safe_code,
             safe_message,
             static_cast<unsigned long>(millis()));

    const bool published = client_.publish(MQTT_TOPIC_TX_ACK_CONTROL, payload_buffer_, false);
    if (published) {
        total_messages_published_++;
        remember_ack_payload(safe_request_id, MQTT_TOPIC_TX_ACK_CONTROL, payload_buffer_);
    }
    return published;
}

bool MqttManager::publish_component_apply_ack(const char* request_id,
                                              bool success,
                                              const char* code,
                                              const char* message,
                                              bool reboot_required,
                                              uint8_t apply_mask,
                                              uint8_t persisted_mask,
                                              uint8_t battery_type,
                                              uint8_t inverter_type,
                                              uint8_t battery_interface,
                                              uint8_t inverter_interface,
                                              uint32_t settings_version) {
    if (!is_connected()) {
        return false;
    }

    char generated_request_id[40] = {};
    const char* safe_request_id = normalized_request_id(request_id,
                                                        generated_request_id,
                                                        sizeof(generated_request_id));
    const char* safe_code = (code != nullptr) ? code : (success ? "OK" : "ERROR");
    const char* safe_message = (message != nullptr) ? message : "";
    const bool ready_for_reboot = success && reboot_required && (persisted_mask == apply_mask);

    snprintf(payload_buffer_, sizeof(payload_buffer_),
             R"({"request_id":"%s","action":"component_apply","success":%s,"code":"%s","message":"%s","reboot_required":%s,"ready_for_reboot":%s,"apply_mask":%u,"persisted_mask":%u,"battery_type":%u,"inverter_type":%u,"battery_interface":%u,"inverter_interface":%u,"settings_version":%lu,"ts_ms":%lu})",
             safe_request_id,
             success ? "true" : "false",
             safe_code,
             safe_message,
             reboot_required ? "true" : "false",
             ready_for_reboot ? "true" : "false",
             static_cast<unsigned>(apply_mask),
             static_cast<unsigned>(persisted_mask),
             static_cast<unsigned>(battery_type),
             static_cast<unsigned>(inverter_type),
             static_cast<unsigned>(battery_interface),
             static_cast<unsigned>(inverter_interface),
             static_cast<unsigned long>(settings_version),
             static_cast<unsigned long>(millis()));

    const bool published = client_.publish(MQTT_TOPIC_TX_ACK_CONTROL, payload_buffer_, false);
    if (published) {
        total_messages_published_++;
        remember_ack_payload(safe_request_id, MQTT_TOPIC_TX_ACK_CONTROL, payload_buffer_);
    }
    return published;
}

bool MqttManager::publish_settings_ack(const char* request_id,
                                       uint8_t category,
                                       uint8_t field_id,
                                       bool success,
                                       uint32_t new_version,
                                       const char* code,
                                       const char* message) {
    if (!is_connected()) {
        return false;
    }

    const char* safe_request_id = (request_id != nullptr) ? request_id : "";
    const char* safe_code = (code != nullptr) ? code : (success ? "OK" : "ERROR");
    const char* safe_message = (message != nullptr) ? message : "";

    snprintf(payload_buffer_, sizeof(payload_buffer_),
             R"({"request_id":"%s","category":%u,"field":%u,"success":%s,"new_version":%lu,"code":"%s","message":"%s","ts_ms":%lu})",
             safe_request_id,
             static_cast<unsigned>(category),
             static_cast<unsigned>(field_id),
             success ? "true" : "false",
             static_cast<unsigned long>(new_version),
             safe_code,
             safe_message,
             static_cast<unsigned long>(millis()));

    const bool published = client_.publish(MQTT_TOPIC_TX_ACK_BATTERY, payload_buffer_, false);
    if (published) {
        total_messages_published_++;
        remember_ack_payload(safe_request_id, MQTT_TOPIC_TX_ACK_BATTERY, payload_buffer_);
    }
    return published;
}

bool MqttManager::publish_network_ack(const char* request_id,
                                      bool success,
                                      const char* code,
                                      const char* message) {
    if (!is_connected()) {
        return false;
    }

    auto& eth = EthernetManager::instance();
    const char* safe_request_id = (request_id != nullptr) ? request_id : "";
    const char* safe_code = (code != nullptr) ? code : (success ? "OK" : "ERROR");
    const char* safe_message = (message != nullptr) ? message : "";

    IPAddress current_ip = eth.get_local_ip();
    IPAddress current_gateway = eth.get_gateway_ip();
    IPAddress current_subnet = eth.get_subnet_mask();
    IPAddress static_ip = eth.get_static_ip();
    IPAddress static_gateway = eth.get_static_gateway();
    IPAddress static_subnet = eth.get_static_subnet_mask();
    IPAddress static_dns_primary = eth.get_static_dns_primary();
    IPAddress static_dns_secondary = eth.get_static_dns_secondary();

    snprintf(payload_buffer_, sizeof(payload_buffer_),
             R"({"request_id":"%s","success":%s,"code":"%s","message":"%s","use_static_ip":%s,"current":{"ip":"%u.%u.%u.%u","gateway":"%u.%u.%u.%u","subnet":"%u.%u.%u.%u"},"static_config":{"ip":"%u.%u.%u.%u","gateway":"%u.%u.%u.%u","subnet":"%u.%u.%u.%u","dns_primary":"%u.%u.%u.%u","dns_secondary":"%u.%u.%u.%u"},"config_version":%lu,"ts_ms":%lu})",
             safe_request_id,
             success ? "true" : "false",
             safe_code,
             safe_message,
             eth.is_static_ip() ? "true" : "false",
             current_ip[0], current_ip[1], current_ip[2], current_ip[3],
             current_gateway[0], current_gateway[1], current_gateway[2], current_gateway[3],
             current_subnet[0], current_subnet[1], current_subnet[2], current_subnet[3],
             static_ip[0], static_ip[1], static_ip[2], static_ip[3],
             static_gateway[0], static_gateway[1], static_gateway[2], static_gateway[3],
             static_subnet[0], static_subnet[1], static_subnet[2], static_subnet[3],
             static_dns_primary[0], static_dns_primary[1], static_dns_primary[2], static_dns_primary[3],
             static_dns_secondary[0], static_dns_secondary[1], static_dns_secondary[2], static_dns_secondary[3],
             static_cast<unsigned long>(eth.get_network_config_version()),
             static_cast<unsigned long>(millis()));

    const bool published = client_.publish(MQTT_TOPIC_TX_ACK_NETWORK, payload_buffer_, false);
    if (published) {
        total_messages_published_++;
        remember_ack_payload(safe_request_id, MQTT_TOPIC_TX_ACK_NETWORK, payload_buffer_);
    }
    return published;
}

bool MqttManager::publish_mqtt_ack(const char* request_id,
                                   bool success,
                                   const char* code,
                                   const char* message) {
    if (!is_connected()) {
        return false;
    }

    const char* safe_request_id = (request_id != nullptr) ? request_id : "";
    const char* safe_code = (code != nullptr) ? code : (success ? "OK" : "ERROR");
    const char* safe_message = (message != nullptr) ? message : "";

    const IPAddress server = MqttConfigManager::getServer();
    snprintf(payload_buffer_, sizeof(payload_buffer_),
             R"({"request_id":"%s","success":%s,"code":"%s","message":"%s","enabled":%s,"server":"%u.%u.%u.%u","port":%u,"username":"%s","client_id":"%s","connected":%s,"config_version":%lu,"ts_ms":%lu})",
             safe_request_id,
             success ? "true" : "false",
             safe_code,
             safe_message,
             MqttConfigManager::isEnabled() ? "true" : "false",
             server[0], server[1], server[2], server[3],
             static_cast<unsigned>(MqttConfigManager::getPort()),
             MqttConfigManager::getUsername(),
             MqttConfigManager::getClientId(),
             MqttConfigManager::isConnected() ? "true" : "false",
             static_cast<unsigned long>(MqttConfigManager::getConfigVersion()),
             static_cast<unsigned long>(millis()));

    const bool published = client_.publish(MQTT_TOPIC_TX_ACK_MQTT, payload_buffer_, false);
    if (published) {
        total_messages_published_++;
        remember_ack_payload(safe_request_id, MQTT_TOPIC_TX_ACK_MQTT, payload_buffer_);
    }
    return published;
}

bool MqttManager::publish_event_logs_clear_ack(const char* request_id,
                                               bool success,
                                               const char* code,
                                               const char* message) {
    if (!is_connected()) {
        return false;
    }

    const char* safe_request_id = (request_id != nullptr) ? request_id : "";
    const char* safe_code = (code != nullptr) ? code : (success ? "OK" : "ERROR");
    const char* safe_message = (message != nullptr) ? message : "";

    snprintf(payload_buffer_, sizeof(payload_buffer_),
             R"({"request_id":"%s","action":"event_logs_clear","success":%s,"code":"%s","message":"%s","ts_ms":%lu})",
             safe_request_id,
             success ? "true" : "false",
             safe_code,
             safe_message,
             static_cast<unsigned long>(millis()));

    const bool published = client_.publish(MQTT_TOPIC_TX_ACK_EVENT_LOGS_CLEAR, payload_buffer_, false);
    if (published) {
        total_messages_published_++;
        remember_ack_payload(safe_request_id, MQTT_TOPIC_TX_ACK_EVENT_LOGS_CLEAR, payload_buffer_);
    }
    return published;
}

void MqttManager::handle_control_debug_level(const char* request_id, int level) {
    if (level < 0 || level > MQTT_LOG_DEBUG) {
        publish_control_ack(request_id,
                            "debug_level",
                            false,
                            "INVALID_LEVEL",
                            "debug level must be 0-7");
        LOG_WARN("MQTT", "Invalid debug level command: %d", level);
        return;
    }

    const auto previous = MqttLogger::instance().get_level();
    MqttLogger::instance().set_level(static_cast<MqttLogLevel>(level));
    TxControlHandlers::save_debug_level(static_cast<uint8_t>(level));

    publish_control_ack(request_id,
                        "debug_level",
                        true,
                        "OK",
                        "debug level applied");

    LOG_INFO("MQTT", "Debug level changed via MQTT command: %s -> %s",
             MqttLogger::instance().level_to_string(previous),
             MqttLogger::instance().level_to_string(static_cast<MqttLogLevel>(level)));
}

void MqttManager::handle_control_reboot(const char* request_id) {
    publish_control_ack(request_id,
                        "reboot",
                        true,
                        "OK",
                        "reboot scheduled");

    LOG_WARN("MQTT", "Reboot requested via MQTT control command");
    client_.loop();
    delay(100);

    disconnect();
    vTaskDelay(pdMS_TO_TICKS(TimingConfig::REBOOT_DELAY_MS));
    ESP.restart();
}

void MqttManager::handle_control_test_data_mode(const char* request_id, int mode) {
    if (mode < 0 || mode > 2) {
        publish_control_ack(request_id,
                            "test_data_mode",
                            false,
                            "INVALID_MODE",
                            "test_data_mode must be 0-2");
        return;
    }

    TestDataConfig::Config config = TestDataConfig::get_config();
    switch (mode) {
        case 0: config.mode = TestDataConfig::Mode::OFF; break;
        case 1: config.mode = TestDataConfig::Mode::SOC_POWER_ONLY; break;
        case 2: config.mode = TestDataConfig::Mode::FULL_BATTERY_DATA; break;
        default: break;
    }

    TestDataConfig::set_config(config);
    TestDataConfig::apply_config();

    publish_control_ack(request_id,
                        "test_data_mode",
                        true,
                        "OK",
                        "test data mode applied");
}

void MqttManager::handle_control_ota_start(const char* request_id) {
    uint8_t synthetic_mac[6] = {0};
    const bool armed = OtaManager::instance().arm_ota_session_from_control_plane(synthetic_mac);

    publish_control_ack(request_id,
                        "ota_start",
                        armed,
                        armed ? "OK" : "ARM_FAILED",
                        armed ? "OTA session armed" : "failed to arm OTA session");
}

void MqttManager::handle_control_command(const char* topic, const char* payload) {
#if !MQTT_FEATURE_COMMANDS
    (void)topic;
    (void)payload;
    return;
#else
    // Component apply control payload contains multiple fields and can exceed
    // 256-byte ArduinoJson pool usage (especially with duplicated strings).
    // Use a larger document to prevent deserializeJson(...)=NoMemory.
    StaticJsonDocument<512> doc;
    const DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        LOG_WARN("MQTT", "Invalid control command JSON on %s: %s", topic, err.c_str());
        publish_control_ack("", "control", false, "INVALID_JSON", "malformed payload");
        return;
    }

    const char* request_id = doc["request_id"] | "";
    if (publish_cached_ack_if_duplicate(request_id)) {
        return;
    }

    if (strcmp(topic, MQTT_TOPIC_RX_CMD_CONTROL_DEBUG_LEVEL) == 0) {
        const int level = doc["level"] | -1;
        handle_control_debug_level(request_id, level);
        return;
    }

    if (strcmp(topic, MQTT_TOPIC_RX_CMD_CONTROL_TEST_DATA_MODE) == 0) {
        const int mode = doc["mode"] | -1;
        handle_control_test_data_mode(request_id, mode);
        return;
    }

    if (strcmp(topic, MQTT_TOPIC_RX_CMD_CONTROL_EVENT_LOGS_CLEAR) == 0) {
        const bool confirm = doc["confirm"] | false;
        if (!confirm) {
            publish_event_logs_clear_ack(request_id,
                                         false,
                                         "CONFIRM_REQUIRED",
                                         "set confirm=true to clear event logs");
            return;
        }

        reset_all_events();
        event_snapshot_offset_ = 0;
        event_snapshot_order_.clear();
        publish_event_logs_clear_ack(request_id,
                                     true,
                                     "OK",
                                     "event logs cleared");
        return;
    }

    if (strcmp(topic, MQTT_TOPIC_RX_CMD_CONTROL_REBOOT) == 0) {
        const bool confirm = doc["confirm"] | false;
        if (!confirm) {
            publish_control_ack(request_id,
                                "reboot",
                                false,
                                "CONFIRM_REQUIRED",
                                "set confirm=true to reboot");
            return;
        }
        handle_control_reboot(request_id);
        return;
    }

    if (strcmp(topic, MQTT_TOPIC_RX_CMD_CONTROL_OTA_START) == 0) {
        const bool confirm = doc["confirm"] | false;
        if (!confirm) {
            publish_control_ack(request_id,
                                "ota_start",
                                false,
                                "CONFIRM_REQUIRED",
                                "set confirm=true to arm OTA session");
            return;
        }
        handle_control_ota_start(request_id);
        return;
    }

    if (strcmp(topic, MQTT_TOPIC_RX_CMD_CONTROL_COMPONENT_APPLY) == 0) {
        const bool confirm = doc["confirm"] | false;
        if (!confirm) {
            publish_control_ack(request_id,
                                "component_apply",
                                false,
                                "CONFIRM_REQUIRED",
                                "set confirm=true to apply components");
            return;
        }

        const uint8_t apply_mask = static_cast<uint8_t>(doc["apply_mask"] | 0);
        const uint8_t battery_type = static_cast<uint8_t>(doc["battery_type"] | 0);
        const uint8_t inverter_type = static_cast<uint8_t>(doc["inverter_type"] | 0);
        const uint8_t battery_interface = static_cast<uint8_t>(doc["battery_interface"] | 0);
        const uint8_t inverter_interface = static_cast<uint8_t>(doc["inverter_interface"] | 0);

        LOG_INFO("MQTT",
                 "Component apply via MQTT (request_id=%s mask=%u batt=%u inv=%u batt_if=%u inv_if=%u)",
                 request_id,
                 static_cast<unsigned>(apply_mask),
                 static_cast<unsigned>(battery_type),
                 static_cast<unsigned>(inverter_type),
                 static_cast<unsigned>(battery_interface),
                 static_cast<unsigned>(inverter_interface));

        const TxComponentCatalogHandlers::ComponentApplyResult r =
            TxComponentCatalogHandlers::apply_components(apply_mask,
                                                         battery_type,
                                                         inverter_type,
                                                         battery_interface,
                                                         inverter_interface);

        LOG_INFO("MQTT",
                 "Component apply result: success=%d reboot_req=%d persisted=0x%02X msg=%s",
                 static_cast<int>(r.success),
                 static_cast<int>(r.reboot_required),
                 static_cast<unsigned>(r.persisted_mask),
                 r.message);

        publish_component_apply_ack(request_id,
                                    r.success,
                                    r.success ? "OK" : "APPLY_FAILED",
                                    r.message,
                                    r.reboot_required,
                                    apply_mask,
                                    r.persisted_mask,
                                    r.battery_type,
                                    r.inverter_type,
                                    r.battery_interface,
                                    r.inverter_interface,
                                    r.settings_version);
        return;
    }

    publish_control_ack(request_id, "control", false, "UNSUPPORTED_TOPIC", "unsupported control topic");
#endif
}

void MqttManager::handle_settings_command(const char* topic, const char* payload) {
#if !MQTT_FEATURE_COMMANDS
    (void)topic;
    (void)payload;
    return;
#else
    if (strcmp(topic, MQTT_TOPIC_RX_CMD_UPDATE_BATTERY) != 0) {
        return;
    }

    StaticJsonDocument<384> doc;
    const DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        LOG_WARN("MQTT", "Invalid settings command JSON on %s: %s", topic, err.c_str());
        publish_settings_ack("", 0, 0, false, 0, "INVALID_JSON", "malformed payload");
        return;
    }

    const char* request_id = doc["request_id"] | "";
    if (publish_cached_ack_if_duplicate(request_id)) {
        return;
    }
    const int category_int = doc["category"] | -1;
    const int field_int = doc["field"] | -1;
    JsonVariant value = doc["value"];

    if (category_int < 0 || category_int > 255 || field_int < 0 || field_int > 255 || value.isNull()) {
        publish_settings_ack(request_id,
                             static_cast<uint8_t>(category_int < 0 ? 0 : category_int),
                             static_cast<uint8_t>(field_int < 0 ? 0 : field_int),
                             false,
                             0,
                             "INVALID_REQUEST",
                             "category, field and value are required");
        return;
    }

    uint32_t value_uint32 = 0;
    float value_float = 0.0f;
    char value_string[32] = {0};

    if (value.is<bool>()) {
        value_uint32 = value.as<bool>() ? 1u : 0u;
        value_float = static_cast<float>(value_uint32);
    } else if (value.is<int>() || value.is<uint32_t>()) {
        value_uint32 = value.as<uint32_t>();
        value_float = static_cast<float>(value_uint32);
    } else if (value.is<float>() || value.is<double>()) {
        value_float = value.as<float>();
        if (value_float >= 0.0f) {
            value_uint32 = static_cast<uint32_t>(value_float);
        }
    } else if (value.is<const char*>()) {
        const char* incoming = value.as<const char*>();
        if (incoming == nullptr) {
            incoming = "";
        }
        strlcpy(value_string, incoming, sizeof(value_string));
    } else {
        publish_settings_ack(request_id,
                             static_cast<uint8_t>(category_int),
                             static_cast<uint8_t>(field_int),
                             false,
                             0,
                             "UNSUPPORTED_VALUE_TYPE",
                             "value must be bool, number, or string");
        return;
    }

    LOG_INFO("MQTT",
             "Settings update request_id=%s category=%d field=%d value_u32=%lu value_f=%.3f value_s='%s'",
             request_id,
             category_int,
             field_int,
             static_cast<unsigned long>(value_uint32),
             static_cast<double>(value_float),
             value_string);

    uint32_t new_version = 0;
    char error_msg[192] = {0};
    const bool applied = SettingsManager::instance().apply_settings_update(static_cast<uint8_t>(category_int),
                                                                            static_cast<uint8_t>(field_int),
                                                                            value_uint32,
                                                                            value_float,
                                                                            value_string,
                                                                            &new_version,
                                                                            error_msg,
                                                                            sizeof(error_msg));

    if (applied) {
        publish_settings_ack(request_id,
                             static_cast<uint8_t>(category_int),
                             static_cast<uint8_t>(field_int),
                             true,
                             new_version,
                             "OK",
                             "settings applied");
        return;
    }

    const SettingsManager::ApplyFailureInfo failure = SettingsManager::instance().get_last_apply_failure();
    if (failure.valid) {
        LOG_WARN("MQTT",
                 "Settings apply failed request_id=%s cat=%u field=%u stage=%s reason=%s detail=%s key=%s",
                 request_id,
                 static_cast<unsigned>(failure.category),
                 static_cast<unsigned>(failure.field_id),
                 failure.stage,
                 failure.reason_code,
                 failure.detail,
                 failure.nvs_key);
    }

    publish_settings_ack(request_id,
                         static_cast<uint8_t>(category_int),
                         static_cast<uint8_t>(field_int),
                         false,
                         new_version,
                         "APPLY_FAILED",
                         error_msg[0] != '\0' ? error_msg : "settings apply failed");
#endif
}

void MqttManager::handle_network_command(const char* topic, const char* payload) {
#if !MQTT_FEATURE_COMMANDS
    (void)topic;
    (void)payload;
    return;
#else
    if (strcmp(topic, MQTT_TOPIC_RX_CMD_UPDATE_NETWORK) != 0) {
        return;
    }

    StaticJsonDocument<320> doc;
    const DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        LOG_WARN("MQTT", "Invalid network command JSON on %s: %s", topic, err.c_str());
        publish_network_ack("", false, "INVALID_JSON", "malformed payload");
        return;
    }

    const char* request_id = doc["request_id"] | "";
    if (publish_cached_ack_if_duplicate(request_id)) {
        return;
    }
    const bool use_static_ip = doc["use_static_ip"] | false;

    uint8_t ip[4] = {0, 0, 0, 0};
    uint8_t gateway[4] = {0, 0, 0, 0};
    uint8_t subnet[4] = {0, 0, 0, 0};
    uint8_t dns_primary[4] = {8, 8, 8, 8};
    uint8_t dns_secondary[4] = {8, 8, 4, 4};

    if (use_static_ip) {
        const char* ip_str = doc["ip"] | "";
        const char* gateway_str = doc["gateway"] | "";
        const char* subnet_str = doc["subnet"] | "";
        const char* dns1_str = doc["dns_primary"] | "8.8.8.8";
        const char* dns2_str = doc["dns_secondary"] | "8.8.4.4";

        if (!parse_ipv4_string(ip_str, ip) ||
            !parse_ipv4_string(gateway_str, gateway) ||
            !parse_ipv4_string(subnet_str, subnet) ||
            !parse_ipv4_string(dns1_str, dns_primary) ||
            !parse_ipv4_string(dns2_str, dns_secondary)) {
            publish_network_ack(request_id, false, "INVALID_IP", "invalid static IP configuration");
            return;
        }

        if (ip[0] == 0) {
            publish_network_ack(request_id, false, "INVALID_IP", "ip cannot be 0.0.0.0");
            return;
        }

        if (ip[0] == 255 && ip[1] == 255 && ip[2] == 255 && ip[3] == 255) {
            publish_network_ack(request_id, false, "INVALID_IP", "ip cannot be broadcast");
            return;
        }

        if (ip[0] >= 224 && ip[0] <= 239) {
            publish_network_ack(request_id, false, "INVALID_IP", "ip cannot be multicast");
            return;
        }

        uint32_t subnet_val = (static_cast<uint32_t>(subnet[0]) << 24) |
                              (static_cast<uint32_t>(subnet[1]) << 16) |
                              (static_cast<uint32_t>(subnet[2]) << 8) |
                              static_cast<uint32_t>(subnet[3]);
        uint32_t inverted = ~subnet_val + 1;
        if ((inverted & (inverted - 1)) != 0 && inverted != 0) {
            publish_network_ack(request_id, false, "INVALID_SUBNET", "subnet mask must be contiguous");
            return;
        }

        auto& eth = EthernetManager::instance();
        if (eth.checkIPConflict(ip)) {
            publish_network_ack(request_id, false, "IP_CONFLICT", "ip address already in use");
            return;
        }

        if (!eth.testStaticIPReachability(ip, gateway, subnet, dns_primary)) {
            publish_network_ack(request_id, false, "GATEWAY_UNREACHABLE", "gateway unreachable");
            return;
        }
    }

    const bool saved = EthernetManager::instance().save_network_config(use_static_ip,
                                                                        ip,
                                                                        gateway,
                                                                        subnet,
                                                                        dns_primary,
                                                                        dns_secondary);
    if (saved) {
        publish_network_ack(request_id, true, "OK", "network config saved; reboot required");
        return;
    }

    publish_network_ack(request_id, false, "NVS_SAVE_FAILED", "failed to save network config");
#endif
}

void MqttManager::handle_mqtt_command(const char* topic, const char* payload) {
#if !MQTT_FEATURE_COMMANDS
    (void)topic;
    (void)payload;
    return;
#else
    if (strcmp(topic, MQTT_TOPIC_RX_CMD_UPDATE_MQTT) != 0) {
        return;
    }

    StaticJsonDocument<320> doc;
    const DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        LOG_WARN("MQTT", "Invalid MQTT config command JSON on %s: %s", topic, err.c_str());
        publish_mqtt_ack("", false, "INVALID_JSON", "malformed payload");
        return;
    }

    const char* request_id = doc["request_id"] | "";
    if (publish_cached_ack_if_duplicate(request_id)) {
        return;
    }
    const bool enabled = doc["enabled"] | false;
    const char* server_str = doc["server"] | "";
    const uint16_t port = doc["port"] | 1883;
    const char* username = doc["username"] | "";
    const char* password = doc["password"] | "";
    const char* client_id = doc["client_id"] | "espnow_transmitter";

    uint8_t server_bytes[4] = {0, 0, 0, 0};
    if (enabled) {
        if (!parse_ipv4_string(server_str, server_bytes)) {
            publish_mqtt_ack(request_id, false, "INVALID_SERVER", "invalid mqtt server ip");
            return;
        }

        if ((server_bytes[0] == 0 && server_bytes[1] == 0 && server_bytes[2] == 0 && server_bytes[3] == 0) ||
            (server_bytes[0] == 255 && server_bytes[1] == 255 && server_bytes[2] == 255 && server_bytes[3] == 255)) {
            publish_mqtt_ack(request_id, false, "INVALID_SERVER", "invalid mqtt server ip");
            return;
        }

        if (port < 1 || port > 65535) {
            publish_mqtt_ack(request_id, false, "INVALID_PORT", "invalid mqtt port");
            return;
        }

        if (client_id == nullptr || client_id[0] == '\0') {
            publish_mqtt_ack(request_id, false, "CLIENT_ID_REQUIRED", "client_id is required");
            return;
        }
    }

    const IPAddress server(server_bytes[0], server_bytes[1], server_bytes[2], server_bytes[3]);
    if (!MqttConfigManager::saveConfig(enabled, server, port, username, password, client_id)) {
        publish_mqtt_ack(request_id, false, "NVS_SAVE_FAILED", "failed to save mqtt config");
        return;
    }

    MqttConfigManager::applyConfig();
    vTaskDelay(pdMS_TO_TICKS(TimingConfig::SETTINGS_UPDATE_DELAY_MS));

    publish_mqtt_ack(request_id, true, "OK", "mqtt config saved; reboot transmitter to apply");
#endif
}

void MqttManager::handle_refresh_command(const char* topic, const char* payload) {
#if !MQTT_FEATURE_COMMANDS
    (void)topic;
    (void)payload;
    return;
#else
    if (strcmp(topic, MQTT_TOPIC_RX_CMD_REFRESH_POWER) != 0 &&
        strcmp(topic, MQTT_TOPIC_RX_CMD_REFRESH_BATTERY) != 0 &&
        strcmp(topic, MQTT_TOPIC_RX_CMD_REFRESH_NETWORK) != 0 &&
        strcmp(topic, MQTT_TOPIC_RX_CMD_REFRESH_MQTT) != 0 &&
        strcmp(topic, MQTT_TOPIC_RX_CMD_REFRESH_SETTINGS) != 0 &&
        strcmp(topic, MQTT_TOPIC_RX_CMD_REFRESH_CATALOG_BATTERY) != 0 &&
        strcmp(topic, MQTT_TOPIC_RX_CMD_REFRESH_CATALOG_INVERTER) != 0 &&
        strcmp(topic, MQTT_TOPIC_RX_CMD_REFRESH_LED) != 0) {
        return;
    }

    StaticJsonDocument<256> doc;
    const DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        LOG_WARN("MQTT", "Invalid refresh command JSON on %s: %s", topic, err.c_str());
        publish_control_ack("", "refresh_power", false, "INVALID_JSON", "malformed payload");
        return;
    }

    const char* request_id = doc["request_id"] | "";
    if (publish_cached_ack_if_duplicate(request_id)) {
        return;
    }

    bool published = false;
    const char* action = "refresh_unknown";

    if (strcmp(topic, MQTT_TOPIC_RX_CMD_REFRESH_POWER) == 0) {
        published = publish_static_power() || publish_static_specs();
        action = "refresh_power";
    } else if (strcmp(topic, MQTT_TOPIC_RX_CMD_REFRESH_BATTERY) == 0) {
        published = publish_battery_specs() || publish_static_specs();
        action = "refresh_battery";
    } else if (strcmp(topic, MQTT_TOPIC_RX_CMD_REFRESH_NETWORK) == 0) {
        published = publish_static_network();
        action = "refresh_network";
    } else if (strcmp(topic, MQTT_TOPIC_RX_CMD_REFRESH_MQTT) == 0) {
        published = publish_static_mqtt();
        action = "refresh_mqtt";
    } else if (strcmp(topic, MQTT_TOPIC_RX_CMD_REFRESH_SETTINGS) == 0) {
        published = publish_static_settings();
        action = "refresh_settings";
    } else if (strcmp(topic, MQTT_TOPIC_RX_CMD_REFRESH_CATALOG_BATTERY) == 0) {
        published = publish_battery_type_catalog();
        action = "refresh_catalog_battery";
    } else if (strcmp(topic, MQTT_TOPIC_RX_CMD_REFRESH_CATALOG_INVERTER) == 0) {
        published = publish_inverter_type_catalog();
        action = "refresh_catalog_inverter";
    } else if (strcmp(topic, MQTT_TOPIC_RX_CMD_REFRESH_LED) == 0) {
        published = publish_static_led() & publish_runtime_led() & publish_runtime_system();
        action = "refresh_led";
    }

    if (published) {
        publish_control_ack(request_id, action, true, "OK", "refresh published");
    } else {
        publish_control_ack(request_id, action, false, "PUBLISH_FAILED", "failed to publish refresh data");
    }
#endif
}

void MqttManager::handle_stream_command(const char* topic, const char* payload) {
#if !MQTT_FEATURE_COMMANDS
    (void)topic;
    (void)payload;
    return;
#else
    const bool is_event_stream_cmd = (strcmp(topic, MQTT_TOPIC_RX_CMD_STREAM_EVENT_LOGS) == 0);
    const bool is_cell_stream_cmd = (strcmp(topic, MQTT_TOPIC_RX_CMD_STREAM_CELL_DATA) == 0);
    if (!is_event_stream_cmd && !is_cell_stream_cmd) {
        return;
    }

    StaticJsonDocument<192> doc;
    const DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        LOG_WARN("MQTT", "Invalid stream command JSON on %s: %s", topic, err.c_str());
        publish_control_ack("", "monitor_unsubscribe", false, "INVALID_JSON", "malformed payload");
        return;
    }

    const char* request_id = doc["request_id"] | "";
    if (publish_cached_ack_if_duplicate(request_id)) {
        return;
    }

    const char* stream = doc["stream"] | "";
    const char* action = doc["action"] | "";

    if (strcmp(stream, "event_logs") == 0) {
        if (strcmp(action, "subscribe") == 0) {
            increment_event_log_subscribers();
            publish_control_ack(request_id, "stream_event_logs", true, "OK", "event_logs subscribe accepted");
            return;
        }

        if (strcmp(action, "unsubscribe") == 0) {
            decrement_event_log_subscribers();
            publish_control_ack(request_id, "stream_event_logs", true, "OK", "event_logs unsubscribe accepted");
            return;
        }

        if (strcmp(action, "keepalive") == 0) {
            const uint64_t now_ms = millis64();
            for (auto& session : event_log_subscriptions_) {
                session.last_activity_ms = now_ms;
            }
            publish_control_ack(request_id, "stream_event_logs", true, "OK", "event_logs keepalive accepted");
            return;
        }

        publish_control_ack(request_id, "stream_event_logs", false, "INVALID_ACTION", "action must be subscribe/unsubscribe/keepalive");
        return;
    }

    if (strcmp(stream, "cell_data") == 0) {
        if (strcmp(action, "subscribe") == 0) {
            increment_cell_data_subscribers();
            publish_control_ack(request_id, "stream_cell_data", true, "OK", "cell_data subscribe accepted");
            return;
        }

        if (strcmp(action, "unsubscribe") == 0) {
            decrement_cell_data_subscribers();
            publish_control_ack(request_id, "stream_cell_data", true, "OK", "cell_data unsubscribe accepted");
            return;
        }

        if (strcmp(action, "keepalive") == 0) {
            const uint64_t now_ms = millis64();
            for (auto& session : cell_data_subscriptions_) {
                session.last_activity_ms = now_ms;
            }
            publish_control_ack(request_id, "stream_cell_data", true, "OK", "cell_data keepalive accepted");
            return;
        }

        publish_control_ack(request_id, "stream_cell_data", false, "INVALID_ACTION", "action must be subscribe/unsubscribe/keepalive");
        return;
    }

    publish_control_ack(request_id, "stream", false, "UNSUPPORTED_STREAM", "unsupported stream");
#endif
}

void MqttManager::message_callback(char* topic, byte* payload, unsigned int length) {
    // Null-terminate payload
    char message[512];
    if (length >= sizeof(message)) length = sizeof(message) - 1;
    memcpy(message, payload, length);
    message[length] = '\0';
    LOG_INFO("MQTT", "Message arrived [%s]: %s", topic, message);
    
    // Handle OTA commands
    if (strcmp(topic, config::get_mqtt_config().topics.ota) == 0) {
        instance().handle_ota_command(message);
        return;
    }

#if MQTT_FEATURE_COMMANDS
    // Use FNV1a hash for O(1)-like topic dispatch (matches receiver architecture)
    const uint32_t topic_hash = fnv1a_runtime(topic);
    
    switch (topic_hash) {
        case fnv1a_const(MQTT_TOPIC_RX_CMD_UPDATE_BATTERY):
            if (strcmp(topic, MQTT_TOPIC_RX_CMD_UPDATE_BATTERY) == 0) {
                instance().handle_settings_command(topic, message);
                return;
            }
            break;
        
        case fnv1a_const(MQTT_TOPIC_RX_CMD_UPDATE_NETWORK):
            if (strcmp(topic, MQTT_TOPIC_RX_CMD_UPDATE_NETWORK) == 0) {
                instance().handle_network_command(topic, message);
                return;
            }
            break;
        
        case fnv1a_const(MQTT_TOPIC_RX_CMD_UPDATE_MQTT):
            if (strcmp(topic, MQTT_TOPIC_RX_CMD_UPDATE_MQTT) == 0) {
                instance().handle_mqtt_command(topic, message);
                return;
            }
            break;
        
        case fnv1a_const(MQTT_TOPIC_RX_CMD_CONTROL_DEBUG_LEVEL):
        case fnv1a_const(MQTT_TOPIC_RX_CMD_CONTROL_TEST_DATA_MODE):
        case fnv1a_const(MQTT_TOPIC_RX_CMD_CONTROL_EVENT_LOGS_CLEAR):
        case fnv1a_const(MQTT_TOPIC_RX_CMD_CONTROL_REBOOT):
        case fnv1a_const(MQTT_TOPIC_RX_CMD_CONTROL_OTA_START):
        case fnv1a_const(MQTT_TOPIC_RX_CMD_CONTROL_COMPONENT_APPLY):
            if ((strcmp(topic, MQTT_TOPIC_RX_CMD_CONTROL_DEBUG_LEVEL) == 0) ||
                (strcmp(topic, MQTT_TOPIC_RX_CMD_CONTROL_TEST_DATA_MODE) == 0) ||
                (strcmp(topic, MQTT_TOPIC_RX_CMD_CONTROL_EVENT_LOGS_CLEAR) == 0) ||
                (strcmp(topic, MQTT_TOPIC_RX_CMD_CONTROL_REBOOT) == 0) ||
                (strcmp(topic, MQTT_TOPIC_RX_CMD_CONTROL_OTA_START) == 0) ||
                (strcmp(topic, MQTT_TOPIC_RX_CMD_CONTROL_COMPONENT_APPLY) == 0)) {
                instance().handle_control_command(topic, message);
                return;
            }
            break;
        
        case fnv1a_const(MQTT_TOPIC_RX_CMD_REFRESH_POWER):
        case fnv1a_const(MQTT_TOPIC_RX_CMD_REFRESH_BATTERY):
        case fnv1a_const(MQTT_TOPIC_RX_CMD_REFRESH_NETWORK):
        case fnv1a_const(MQTT_TOPIC_RX_CMD_REFRESH_MQTT):
        case fnv1a_const(MQTT_TOPIC_RX_CMD_REFRESH_SETTINGS):
        case fnv1a_const(MQTT_TOPIC_RX_CMD_REFRESH_CATALOG_BATTERY):
        case fnv1a_const(MQTT_TOPIC_RX_CMD_REFRESH_CATALOG_INVERTER):
        case fnv1a_const(MQTT_TOPIC_RX_CMD_REFRESH_LED):
            if ((strcmp(topic, MQTT_TOPIC_RX_CMD_REFRESH_POWER) == 0) ||
                (strcmp(topic, MQTT_TOPIC_RX_CMD_REFRESH_BATTERY) == 0) ||
                (strcmp(topic, MQTT_TOPIC_RX_CMD_REFRESH_NETWORK) == 0) ||
                (strcmp(topic, MQTT_TOPIC_RX_CMD_REFRESH_MQTT) == 0) ||
                (strcmp(topic, MQTT_TOPIC_RX_CMD_REFRESH_SETTINGS) == 0) ||
                (strcmp(topic, MQTT_TOPIC_RX_CMD_REFRESH_CATALOG_BATTERY) == 0) ||
                (strcmp(topic, MQTT_TOPIC_RX_CMD_REFRESH_CATALOG_INVERTER) == 0) ||
                (strcmp(topic, MQTT_TOPIC_RX_CMD_REFRESH_LED) == 0)) {
                instance().handle_refresh_command(topic, message);
                return;
            }
            break;
        
        case fnv1a_const(MQTT_TOPIC_RX_CMD_STREAM_EVENT_LOGS):
            if (strcmp(topic, MQTT_TOPIC_RX_CMD_STREAM_EVENT_LOGS) == 0) {
                instance().handle_stream_command(topic, message);
                return;
            }
            break;

        case fnv1a_const(MQTT_TOPIC_RX_CMD_STREAM_CELL_DATA):
            if (strcmp(topic, MQTT_TOPIC_RX_CMD_STREAM_CELL_DATA) == 0) {
                instance().handle_stream_command(topic, message);
                return;
            }
            break;
        
        default:
            break;
    }
#endif
}

void MqttManager::handle_ota_command(const char* url) {
    LOG_INFO("OTA", "Received OTA command via MQTT");
    
    // Expected format: "http://receiver_ip/ota_firmware.bin"
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        LOG_ERROR("OTA", "Invalid URL format");
        publish_status("ota_invalid_url", false);
        return;
    }
    
    LOG_INFO("OTA", "Starting OTA update from: %s", url);
    
    // Perform OTA update
    WiFiClient client;
    t_httpUpdate_return ret = httpUpdate.update(client, url);
    
    switch (ret) {
        case HTTP_UPDATE_FAILED:
            LOG_ERROR("OTA", "Update failed. Error (%d): %s", 
                        httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
            publish_status("ota_failed", false);
            break;
            
        case HTTP_UPDATE_NO_UPDATES:
            LOG_INFO("OTA", "No updates available");
            publish_status("ota_no_update", false);
            break;
            
        case HTTP_UPDATE_OK:
            LOG_INFO("OTA", "Update successful! Rebooting...");
            publish_status("ota_success", false);
            delay(500);
            // Disconnect MQTT gracefully before reboot
            disconnect();
            delay(500);
            ESP.restart();
            break;
    }
}

// External C linkage function for MqttConfigManager to query connection status
// This avoids circular header dependencies between lib/mqtt_manager and src/network
extern "C" bool mqtt_manager_is_connected() {
    return MqttManager::instance().is_connected();
}
