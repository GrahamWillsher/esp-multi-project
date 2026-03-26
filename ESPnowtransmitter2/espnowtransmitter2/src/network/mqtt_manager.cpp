#include "mqtt_manager.h"
#include "ethernet_manager.h"
#include "../config/network_config.h"
#include "../config/logging_config.h"
#include "../datalayer/static_data.h"
#include "../battery_emulator/devboard/utils/events.h"
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

namespace {
constexpr uint16_t MAX_CATALOG_BASE_VERSION = 32767;

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
    
    snprintf(payload_buffer_, sizeof(payload_buffer_),
             R"({"soc":%d,"power":%ld,"timestamp":%lu,"time":"%s","eth_connected":%s})",
             soc, power, millis(), timestamp,
             eth_connected ? "true" : "false");
    
    bool success = client_.publish(config::get_mqtt_config().topics.data, payload_buffer_);
    
    if (success) {
        LOG_DEBUG("MQTT", "Published: %s", payload_buffer_);
        total_messages_published_++;
    } else {
        LOG_ERROR("MQTT", "Publish failed");
    }
    
    return success;
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
    if (!is_connected()) return false;

    constexpr size_t kBufferSize = 2048;
    if (!ensure_publish_buffer(kBufferSize)) {
        return false;
    }

    size_t len = StaticData::serialize_all_specs(publish_buffer_, kBufferSize);
    
    if (len > 0) {
        bool success = client_.publish("transmitter/BE/spec_data", publish_buffer_, true); // Retained
        if (success) {
            LOG_INFO("MQTT", "Published static specs (%u bytes)", len);
        } else {
            LOG_ERROR("MQTT", "Failed to publish static specs");
        }
        return success;
    }
    
    LOG_ERROR("MQTT", "Failed to serialize static specs");
    return false;
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
        success = client_.publish("transmitter/BE/battery_specs", publish_buffer_, true); // Retained
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

    // Needs 6KB for 96 cells + balancing + metadata
    constexpr size_t kBufferSize = 6144;
    if (!ensure_publish_buffer(kBufferSize)) {
        return false;
    }

    size_t len = StaticData::serialize_cell_data(publish_buffer_, kBufferSize);
    
    bool success = false;
    if (len > 0) {
        success = client_.publish("transmitter/BE/cell_data", publish_buffer_, true); // Retained
        if (success) {
            LOG_DEBUG("MQTT", "Published cell data (%u bytes)", len);
        }
    } else {
        LOG_DEBUG("MQTT", "serialize returned 0 bytes, not publishing");
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
        success = client_.publish("transmitter/BE/spec_data_2", publish_buffer_, true); // Retained
        if (success) {
            LOG_DEBUG("MQTT", "Published inverter specs (%u bytes)", len);
        }
    }

    return success;
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
        success = client_.publish("transmitter/BE/battery_type_catalog", publish_buffer_, true);
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
        success = client_.publish("transmitter/BE/inverter_type_catalog", publish_buffer_, true);
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
    
    // Only publish if there are active subscribers
    if (event_log_subscribers_ <= 0) {
        LOG_DEBUG("MQTT", "No event log subscribers, skipping publish");
        return true;
    }

    std::vector<EventData> ordered;
    ordered.reserve(EVENT_NOF_EVENTS);

    // Collect only events that have been set and NOT yet published (delta mode)
    for (int i = 0; i < EVENT_NOF_EVENTS; i++) {
        const EVENTS_STRUCT_TYPE* event_ptr = get_event_pointer((EVENTS_ENUM_TYPE)i);
        if (event_ptr && event_ptr->occurences > 0 && !event_ptr->MQTTpublished) {
            ordered.push_back({(EVENTS_ENUM_TYPE)i, event_ptr});
        }
    }

    // If no unpublished events, skip publishing
    if (ordered.empty()) {
        LOG_DEBUG("MQTT", "No unpublished events, skipping publish");
        return true;
    }

    std::sort(ordered.begin(), ordered.end(), compareEventsByTimestampDesc);

    const size_t total_events = ordered.size();
    const size_t max_events = (total_events > 100) ? 100 : total_events;

    // Allocate JSON document in PSRAM
    DynamicJsonDocument doc(6144);
    doc["event_count"] = static_cast<uint32_t>(total_events);
    JsonArray events = doc.createNestedArray("events");

    for (size_t i = 0; i < max_events; i++) {
        const auto& item = ordered[i];
        const EVENTS_STRUCT_TYPE* evt = item.event_pointer;
        JsonObject obj = events.createNestedObject();
        char event_message[384] = {0};
        const bool have_event_message =
            get_event_message(item.event_handle, event_message, sizeof(event_message));
        obj["timestamp"] = static_cast<uint64_t>(evt->timestamp);
        obj["level"] = map_event_level(evt->level);
        obj["data"] = evt->data;
        obj["message"] = have_event_message ? event_message : "";
        obj["event"] = get_event_enum_string(item.event_handle);
    }

    constexpr size_t kBufferSize = 6144;
    if (!ensure_publish_buffer(kBufferSize)) {
        return false;
    }

    size_t len = serializeJson(doc, publish_buffer_, kBufferSize);
    bool success = false;
    if (len > 0) {
        success = client_.publish("transmitter/BE/event_logs", publish_buffer_, true);
        if (success) {
            LOG_DEBUG("MQTT", "Published %u changed event(s) (%u bytes)", (unsigned)max_events, len);
            
            // Mark all published events as published
            for (const auto& item : ordered) {
                set_event_MQTTpublished(item.event_handle);
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
    event_log_subscribers_++;
    LOG_INFO("MQTT", "Event log subscriber count: %d", event_log_subscribers_);
}

void MqttManager::decrement_event_log_subscribers() {
    if (event_log_subscribers_ > 0) {
        event_log_subscribers_--;
        LOG_INFO("MQTT", "Event log subscriber count: %d", event_log_subscribers_);
    }
}

void MqttManager::loop() {
    if (client_.state() == MQTT_CONNECTED) {
        connected_ = true;
        client_.loop();
    } else {
        connected_ = false;
    }
}

void MqttManager::message_callback(char* topic, byte* payload, unsigned int length) {
    // Null-terminate payload
    char message[256];
    if (length >= sizeof(message)) length = sizeof(message) - 1;
    memcpy(message, payload, length);
    message[length] = '\0';
    LOG_INFO("MQTT", "Message arrived [%s]: %s", topic, message);
    
    // Handle OTA commands
    if (strcmp(topic, config::get_mqtt_config().topics.ota) == 0) {
        instance().handle_ota_command(message);
    }
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
