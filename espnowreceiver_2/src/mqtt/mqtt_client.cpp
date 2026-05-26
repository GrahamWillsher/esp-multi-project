#include "mqtt_client.h"
#include <esp32common/mqtt/mqtt_topics_common.h>
#include "../lib/webserver/utils/transmitter_manager.h"
#include "../lib/webserver/utils/transmitter_network.h"
#include "../lib/webserver/utils/transmitter_mqtt_specs.h"
#include "../lib/webserver/utils/cell_data_cache.h"
#include "../lib/webserver/utils/transmitter_event_log_cache.h"
#include "../common.h"
#include "../runtime/type_catalog_cache.h"
#include "../runtime/battery_data_store.h"
#include <ArduinoJson.h>
#include <array>
#include <cmath>
#include <cstring>
#include <esp_system.h>

namespace {

constexpr uint32_t fnv1a_const(const char* str, uint32_t hash = 2166136261u) {
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

std::array<TypeCatalogCache::TypeEntry, 128> g_battery_catalog_scratch{};
std::array<TypeCatalogCache::TypeEntry, 128> g_inverter_catalog_scratch{};

// Parse "a.b.c.d" IP string into 4-byte array. Returns false on failure.
bool parse_ip_string(const char* s, uint8_t out[4]) {
    if (!s || !out) return false;
    int parts[4] = {};
    int n = sscanf(s, "%d.%d.%d.%d", &parts[0], &parts[1], &parts[2], &parts[3]);
    if (n != 4) return false;
    for (int i = 0; i < 4; i++) {
        if (parts[i] < 0 || parts[i] > 255) return false;
        out[i] = static_cast<uint8_t>(parts[i]);
    }
    return true;
}

// Parse "AA:BB:CC:DD:EE:FF" MAC string into 6-byte array. Returns false on failure.
bool parse_mac_string(const char* s, uint8_t out[6]) {
    if (!s || !out) return false;
    unsigned int parts[6] = {};
    int n = sscanf(s, "%2x:%2x:%2x:%2x:%2x:%2x",
                   &parts[0], &parts[1], &parts[2],
                   &parts[3], &parts[4], &parts[5]);
    if (n != 6) return false;
    for (int i = 0; i < 6; i++) {
        if (parts[i] > 0xFFu) return false;
        out[i] = static_cast<uint8_t>(parts[i]);
    }
    return true;
}

uint8_t parse_led_status_code(const char* status_text) {
    if (!status_text || status_text[0] == '\0') {
        return static_cast<uint8_t>(RuntimeState::LedStatus::Unknown);
    }

    if (strcmp(status_text, "OK") == 0) {
        return static_cast<uint8_t>(RuntimeState::LedStatus::Ok);
    }
    if (strcmp(status_text, "WARNING") == 0) {
        return static_cast<uint8_t>(RuntimeState::LedStatus::Warning);
    }
    if (strcmp(status_text, "ERROR") == 0) {
        return static_cast<uint8_t>(RuntimeState::LedStatus::Error);
    }
    if (strcmp(status_text, "UPDATING") == 0) {
        return static_cast<uint8_t>(RuntimeState::LedStatus::Updating);
    }

    return static_cast<uint8_t>(RuntimeState::LedStatus::Unknown);
}

constexpr const char* MQTT_TOPIC_RX_CMD_STREAM_EVENT_LOGS = mqtt::topics::rx::CMD_STREAM_EVENT_LOGS;
constexpr const char* MQTT_TOPIC_RX_CMD_STREAM_CELL_DATA = mqtt::topics::rx::CMD_STREAM_CELL_DATA;

bool publish_event_logs_stream_control(bool subscribe) {
    if (!MqttClient::isEnabled() || !MqttClient::isConnected()) {
        LOG_WARN("MQTT", "Skipping stream/event_logs %s command: MQTT not connected",
                 subscribe ? "subscribe" : "unsubscribe");
        return false;
    }

    char payload[192];
    snprintf(payload,
             sizeof(payload),
             "{\"request_id\":\"rx-%08lx-%08lx\",\"stream\":\"event_logs\",\"action\":\"%s\"}",
             static_cast<unsigned long>(millis()),
             static_cast<unsigned long>(esp_random()),
             subscribe ? "subscribe" : "unsubscribe");

    const bool ok = MqttClient::publishJson(MQTT_TOPIC_RX_CMD_STREAM_EVENT_LOGS, payload, false);
    LOG_INFO("MQTT", "stream/event_logs %s command %s",
             subscribe ? "subscribe" : "unsubscribe",
             ok ? "published" : "failed");
    return ok;
}

bool publish_cell_data_stream_control(bool subscribe) {
    if (!MqttClient::isEnabled() || !MqttClient::isConnected()) {
        LOG_WARN("MQTT", "Skipping stream/cell_data %s command: MQTT not connected",
                 subscribe ? "subscribe" : "unsubscribe");
        return false;
    }

    char payload[192];
    snprintf(payload,
             sizeof(payload),
             "{\"request_id\":\"rx-%08lx-%08lx\",\"stream\":\"cell_data\",\"action\":\"%s\"}",
             static_cast<unsigned long>(millis()),
             static_cast<unsigned long>(esp_random()),
             subscribe ? "subscribe" : "unsubscribe");

    const bool ok = MqttClient::publishJson(MQTT_TOPIC_RX_CMD_STREAM_CELL_DATA, payload, false);
    LOG_INFO("MQTT", "stream/cell_data %s command %s",
             subscribe ? "subscribe" : "unsubscribe",
             ok ? "published" : "failed");
    return ok;
}

} // namespace

// Static member initialization
WiFiClient MqttClient::wifi_client_;
PubSubClient MqttClient::mqtt_client_(wifi_client_);
char MqttClient::client_id_[32] = "battery_emulator_receiver";
char MqttClient::username_[32] = "";
char MqttClient::password_[32] = "";
uint8_t MqttClient::broker_ip_[4] = {0, 0, 0, 0};
uint16_t MqttClient::broker_port_ = 1883;
bool MqttClient::enabled_ = false;
unsigned long MqttClient::last_connect_attempt_ = 0;
SemaphoreHandle_t MqttClient::ack_mutex_ = nullptr;
std::vector<MqttClient::PendingAck> MqttClient::pending_acks_;

// Cell data subscription state management
int MqttClient::cell_data_subscribers_ = 0;
MqttClient::CellDataSubscriptionState MqttClient::cell_data_state_ = MqttClient::PAUSED;
TimerHandle_t MqttClient::cell_data_pause_timer_ = nullptr;
volatile bool MqttClient::cell_data_pause_requested_ = false;

// Event log subscription management
int MqttClient::event_log_subscribers_ = 0;

void MqttClient::init(const uint8_t* mqtt_server, uint16_t mqtt_port, const char* client_id) {
    if (!mqtt_server) return;

    ensureAckMutex();
    
    memcpy(broker_ip_, mqtt_server, 4);
    broker_port_ = mqtt_port;
    strncpy(client_id_, client_id, sizeof(client_id_) - 1);
    client_id_[sizeof(client_id_) - 1] = '\0';
    
    IPAddress server_ip(broker_ip_[0], broker_ip_[1], broker_ip_[2], broker_ip_[3]);
    mqtt_client_.setServer(server_ip, broker_port_);
    mqtt_client_.setCallback(messageCallback);
    mqtt_client_.setBufferSize(6144); // Large buffer for cell_data + event logs
    
    LOG_INFO("MQTT", "Initialized: %d.%d.%d.%d:%d", 
             broker_ip_[0], broker_ip_[1], broker_ip_[2], broker_ip_[3], broker_port_);
}

void MqttClient::setAuth(const char* username, const char* password) {
    if (username) {
        strncpy(username_, username, sizeof(username_) - 1);
        username_[sizeof(username_) - 1] = '\0';
    } else {
        username_[0] = '\0';
    }
    
    if (password) {
        strncpy(password_, password, sizeof(password_) - 1);
        password_[sizeof(password_) - 1] = '\0';
    } else {
        password_[0] = '\0';
    }
}

bool MqttClient::connect() {
    if (!enabled_ || broker_ip_[0] == 0) {
        return false;
    }

    if (mqtt_client_.connected()) {
        return true;
    }

    if (WiFi.status() != WL_CONNECTED) {
        LOG_WARN("MQTT", "Connect skipped: WiFi not connected (status=%d)", static_cast<int>(WiFi.status()));
        return false;
    }

    // Throttle connection attempts
    unsigned long now = millis();
    if (now - last_connect_attempt_ < RECONNECT_INTERVAL_MS) {
        return false;
    }
    last_connect_attempt_ = now;

    LOG_INFO("MQTT", "Connecting to broker %d.%d.%d.%d:%u (local=%s rssi=%d)...",
             broker_ip_[0], broker_ip_[1], broker_ip_[2], broker_ip_[3],
             static_cast<unsigned>(broker_port_),
             WiFi.localIP().toString().c_str(),
             static_cast<int>(WiFi.RSSI()));

    bool connected = false;
    if (username_[0] != '\0') {
        connected = mqtt_client_.connect(client_id_, username_, password_);
    } else {
        connected = mqtt_client_.connect(client_id_);
    }

    if (connected) {
        LOG_INFO("MQTT", "Connected successfully");
        subscribeToTopics();
        return true;
    } else {
        const int state = mqtt_client_.state();
        IPAddress broker_ip(broker_ip_[0], broker_ip_[1], broker_ip_[2], broker_ip_[3]);
        WiFiClient probe;
        probe.setTimeout(1500);
        const bool tcp_ok = probe.connect(broker_ip, broker_port_);
        if (tcp_ok) {
            probe.stop();
        }

        LOG_ERROR("MQTT", "Connection failed, state=%d (tcp_probe=%s)",
                  state,
                  tcp_ok ? "reachable" : "unreachable");
        return false;
    }
}

void MqttClient::disconnect() {
    if (mqtt_client_.connected()) {
        mqtt_client_.disconnect();
        LOG_INFO("MQTT", "Disconnected");
    }
}

bool MqttClient::isConnected() {
    return mqtt_client_.connected();
}

bool MqttClient::publishJson(const char* topic, const char* payload, bool retained) {
    if (!enabled_ || topic == nullptr || payload == nullptr) {
        return false;
    }

    if (!mqtt_client_.connected()) {
        return false;
    }

    return mqtt_client_.publish(topic, payload, retained);
}

bool MqttClient::publishJsonAndWaitForAck(const char* topic,
                                          const char* payload,
                                          const char* request_id,
                                          const char* ack_topic,
                                          uint32_t timeout_ms,
                                          char* ack_payload,
                                          size_t ack_payload_size) {
    if (!publishJson(topic, payload, false)) {
        return false;
    }

    const uint32_t start_ms = millis();
    while ((millis() - start_ms) < timeout_ms) {
        if (tryConsumePendingAck(request_id, ack_topic, ack_payload, ack_payload_size)) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(25));
    }

    return false;
}

void MqttClient::loop() {
    if (!enabled_) return;

    processDeferredSubscriptionActions();
    
    if (!mqtt_client_.connected()) {
        connect();
    } else {
        mqtt_client_.loop();
    }
}

void MqttClient::setEnabled(bool enabled) {
    if (enabled_ == enabled) return;
    
    enabled_ = enabled;
    
    if (enabled_) {
        LOG_INFO("MQTT", "Enabled");
        connect();
    } else {
        LOG_INFO("MQTT", "Disabled");
        disconnect();
    }
}

bool MqttClient::isEnabled() {
    return enabled_;
}

void MqttClient::messageCallback(char* topic, uint8_t* payload, unsigned int length) {
    LOG_DEBUG("MQTT", "Message received on topic: %s (%u bytes)", topic, length);
    const char* json_payload = reinterpret_cast<const char*>(payload);

    // Hash-based dispatch keeps topic handling O(1)-like for small fixed route sets,
    // while retaining strcmp guards to eliminate any practical collision risk.
    switch (fnv1a_runtime(topic)) {
        case fnv1a_const(mqtt::topics::tx::ACK_BATTERY):
        case fnv1a_const(mqtt::topics::tx::ACK_POWER):
        case fnv1a_const(mqtt::topics::tx::ACK_NETWORK):
        case fnv1a_const(mqtt::topics::tx::ACK_MQTT):
        case fnv1a_const(mqtt::topics::tx::ACK_INVERTER):
        case fnv1a_const(mqtt::topics::tx::ACK_CAN):
        case fnv1a_const(mqtt::topics::tx::ACK_CONTACTOR):
        case fnv1a_const(mqtt::topics::tx::ACK_CONTROL):
        case fnv1a_const(mqtt::topics::tx::ACK_COMPONENT):
        case fnv1a_const(mqtt::topics::tx::ACK_EVENT_LOGS_CLEAR):
        case fnv1a_const(mqtt::topics::tx::ACK_REFRESH):
            if (strncmp(topic, "batt-emu/mqtt-v1/tx/ack/", 24) == 0) {
                if (strcmp(topic, mqtt::topics::tx::ACK_EVENT_LOGS_CLEAR) == 0) {
                    handleEventLogsClearAck(json_payload, length);
                }
                handleAckMessage(topic, json_payload, length);
                return;
            }
            break;
        case fnv1a_const(mqtt::topics::tx::STATE_CELL_DATA_CHUNK):
            if (strcmp(topic, mqtt::topics::tx::STATE_CELL_DATA_CHUNK) == 0) {
                handleCellData(json_payload, length);
                return;
            }
            break;
        case fnv1a_const(mqtt::topics::tx::STATE_BATTERY_LIVE):
            if (strcmp(topic, mqtt::topics::tx::STATE_BATTERY_LIVE) == 0) {
                handleBatteryLive(json_payload, length);
                return;
            }
            break;
        case fnv1a_const(mqtt::topics::tx::STATE_RUNTIME_LED):
            if (strcmp(topic, mqtt::topics::tx::STATE_RUNTIME_LED) == 0) {
                handleRuntimeLed(json_payload, length);
                return;
            }
            break;
        case fnv1a_const(mqtt::topics::tx::STATE_RUNTIME_SYSTEM):
            if (strcmp(topic, mqtt::topics::tx::STATE_RUNTIME_SYSTEM) == 0) {
                handleRuntimeSystem(json_payload, length);
                return;
            }
            break;
        case fnv1a_const(mqtt::topics::tx::STATE_RUNTIME_CHARGER):
            if (strcmp(topic, mqtt::topics::tx::STATE_RUNTIME_CHARGER) == 0) {
                handleRuntimeCharger(json_payload, length);
                return;
            }
            break;
        case fnv1a_const(mqtt::topics::tx::STATE_RUNTIME_INVERTER):
            if (strcmp(topic, mqtt::topics::tx::STATE_RUNTIME_INVERTER) == 0) {
                handleRuntimeInverter(json_payload, length);
                return;
            }
            break;
        case fnv1a_const(mqtt::topics::tx::STATE_SUMMARY_EVENT_LOGS):
            if (strcmp(topic, mqtt::topics::tx::STATE_SUMMARY_EVENT_LOGS) == 0) {
                handleEventLogSummary(json_payload, length);
                return;
            }
            break;
        case fnv1a_const(mqtt::topics::tx::STATE_EVENT_LOGS_CHUNK):
            if (strcmp(topic, mqtt::topics::tx::STATE_EVENT_LOGS_CHUNK) == 0) {
                handleEventLogs(json_payload, length);
                return;
            }
            break;
        case fnv1a_const(mqtt::topics::tx::STATE_STATIC_BATTERY):
            if (strcmp(topic, mqtt::topics::tx::STATE_STATIC_BATTERY) == 0) {
                handleBatterySpecs(json_payload, length);
                return;
            }
            break;
        case fnv1a_const(mqtt::topics::tx::STATE_STATIC_INVERTER):
            if (strcmp(topic, mqtt::topics::tx::STATE_STATIC_INVERTER) == 0) {
                handleSpecData2(json_payload, length);
                return;
            }
            break;
        case fnv1a_const(mqtt::topics::tx::STATE_STATIC_CATALOG_BATTERY):
            if (strcmp(topic, mqtt::topics::tx::STATE_STATIC_CATALOG_BATTERY) == 0) {
                handleBatteryTypeCatalog(json_payload, length);
                return;
            }
            break;
        case fnv1a_const(mqtt::topics::tx::STATE_STATIC_CATALOG_INVERTER):
            if (strcmp(topic, mqtt::topics::tx::STATE_STATIC_CATALOG_INVERTER) == 0) {
                handleInverterTypeCatalog(json_payload, length);
                return;
            }
            break;
        case fnv1a_const(mqtt::topics::tx::STATE_STATIC_NETWORK):
            if (strcmp(topic, mqtt::topics::tx::STATE_STATIC_NETWORK) == 0) {
                handleStaticNetwork(json_payload, length);
                return;
            }
            break;
        case fnv1a_const(mqtt::topics::tx::STATE_STATIC_MQTT):
            if (strcmp(topic, mqtt::topics::tx::STATE_STATIC_MQTT) == 0) {
                handleStaticMqtt(json_payload, length);
                return;
            }
            break;
        case fnv1a_const(mqtt::topics::tx::STATE_STATIC_POWER):
            if (strcmp(topic, mqtt::topics::tx::STATE_STATIC_POWER) == 0) {
                handleStaticPower(json_payload, length);
                return;
            }
            break;
        case fnv1a_const(mqtt::topics::tx::STATE_STATIC_LED):
            if (strcmp(topic, mqtt::topics::tx::STATE_STATIC_LED) == 0) {
                handleStaticLed(json_payload, length);
                return;
            }
            break;
        case fnv1a_const(mqtt::topics::tx::STATE_STATIC_SETTINGS):
            if (strcmp(topic, mqtt::topics::tx::STATE_STATIC_SETTINGS) == 0) {
                handleStaticSettings(json_payload, length);
                return;
            }
            break;
        case fnv1a_const(mqtt::topics::tx::META_VERSION):
            if (strcmp(topic, mqtt::topics::tx::META_VERSION) == 0) {
                handleMetaVersion(json_payload, length);
                return;
            }
            break;
        case fnv1a_const(mqtt::topics::tx::META_SCHEMA_VERSIONS):
            if (strcmp(topic, mqtt::topics::tx::META_SCHEMA_VERSIONS) == 0) {
                handleMetaSchemaVersions(json_payload, length);
                return;
            }
            break;
        case fnv1a_const(mqtt::topics::tx::META_RUNTIME):
            if (strcmp(topic, mqtt::topics::tx::META_RUNTIME) == 0) {
                handleMetaRuntime(json_payload, length);
                return;
            }
            break;
        case fnv1a_const(mqtt::topics::tx::STATE_HEARTBEAT):
            if (strcmp(topic, mqtt::topics::tx::STATE_HEARTBEAT) == 0) {
                handleHeartbeat(json_payload, length);
                return;
            }
            break;
        default:
            break;
    }

    LOG_DEBUG("MQTT", "Ignoring message on unhandled topic: %s", topic);
}

void MqttClient::subscribeToTopics() {
    // MQTT-only topic namespace.
    mqtt_client_.subscribe(mqtt::topics::tx::STATE_STATIC_BATTERY);
    mqtt_client_.subscribe(mqtt::topics::tx::STATE_STATIC_INVERTER);
    mqtt_client_.subscribe(mqtt::topics::tx::STATE_STATIC_CATALOG_BATTERY);
    mqtt_client_.subscribe(mqtt::topics::tx::STATE_STATIC_CATALOG_INVERTER);
    mqtt_client_.subscribe(mqtt::topics::tx::STATE_STATIC_NETWORK);
    mqtt_client_.subscribe(mqtt::topics::tx::STATE_STATIC_MQTT);
    mqtt_client_.subscribe(mqtt::topics::tx::STATE_STATIC_POWER);
    mqtt_client_.subscribe(mqtt::topics::tx::STATE_STATIC_LED);
    mqtt_client_.subscribe(mqtt::topics::tx::STATE_STATIC_SETTINGS);
    mqtt_client_.subscribe(mqtt::topics::tx::META_VERSION);
    mqtt_client_.subscribe(mqtt::topics::tx::META_SCHEMA_VERSIONS);
    mqtt_client_.subscribe(mqtt::topics::tx::META_RUNTIME);
    mqtt_client_.subscribe(mqtt::topics::tx::STATE_BATTERY_LIVE);
    mqtt_client_.subscribe(mqtt::topics::tx::STATE_RUNTIME_LED);
    mqtt_client_.subscribe(mqtt::topics::tx::STATE_RUNTIME_SYSTEM);
    mqtt_client_.subscribe(mqtt::topics::tx::STATE_RUNTIME_CHARGER);
    mqtt_client_.subscribe(mqtt::topics::tx::STATE_RUNTIME_INVERTER);
    mqtt_client_.subscribe(mqtt::topics::tx::STATE_SUMMARY_EVENT_LOGS);
    mqtt_client_.subscribe(mqtt::topics::tx::ACK_WILDCARD);
    mqtt_client_.subscribe(mqtt::topics::tx::STATE_HEARTBEAT);

    // Only subscribe to cell_data if not paused (subscription optimization)
    if (cell_data_state_ != PAUSED) {
        mqtt_client_.subscribe(mqtt::topics::tx::STATE_CELL_DATA_CHUNK);
        LOG_INFO("SUBSCRIPTION", "Subscribed to all topics including cell_data");

        if (cell_data_subscribers_ > 0) {
            (void)publish_cell_data_stream_control(true);
            LOG_INFO("SUBSCRIPTION", "Re-notified TX of cell_data stream after MQTT reconnect");
        }
    } else {
        LOG_INFO("SUBSCRIPTION", "Subscribed to spec topics only (cell_data paused)");
    }

    // Always subscribe to event_logs/chunk so chunks arrive when stream is active
    mqtt_client_.subscribe(mqtt::topics::tx::STATE_EVENT_LOGS_CHUNK);
    LOG_INFO("SUBSCRIPTION", "Subscribed to event_logs/chunk");
    if (event_log_subscribers_ > 0) {
        publish_event_logs_stream_control(true);
        LOG_INFO("SUBSCRIPTION", "Re-notified TX of event_log stream after MQTT reconnect");
    }
}

void MqttClient::handleSpecData(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT", "Processing transmitter/BE/spec_data");
    
    // Parse combined spec data (battery, inverter, charger, system)
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    
    if (error) {
        LOG_ERROR("MQTT", "Failed to parse spec_data: %s", error.c_str());
        return;
    }
    
    // Store in TransmitterManager
    TransmitterManager::storeStaticSpecs(doc.as<JsonObject>());
    
    LOG_INFO("MQTT", "Stored static specs from BE/spec_data");
}

void MqttClient::handleSpecData2(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT", "Processing transmitter/BE/spec_data_2");
    
    // Parse inverter-specific data.
    // Use payload-driven capacity to avoid NoMemory on larger retained payloads.
    // TX publish buffer is currently <= 768 bytes, but ArduinoJson needs overhead.
    const size_t doc_capacity = std::min<size_t>(4096, std::max<size_t>(1024, length * 3));
    DynamicJsonDocument doc(doc_capacity);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    
    if (error) {
        LOG_ERROR("MQTT", "Failed to parse spec_data_2: %s (len=%u cap=%u)",
                  error.c_str(),
                  static_cast<unsigned>(length),
                  static_cast<unsigned>(doc_capacity));
        return;
    }

    const bool has_protocol_name =
        doc.containsKey("inverter_protocol_name") && doc["inverter_protocol_name"].is<const char*>();
    const bool has_protocol_legacy =
        doc.containsKey("inverter_protocol") && doc["inverter_protocol"].is<const char*>();
    const bool has_type_id = doc.containsKey("inverter_type_id") && doc["inverter_type_id"].is<int>();
    const bool has_schema = doc.containsKey("schema_version") || doc.containsKey("spec_schema");

    if (!has_type_id && !has_protocol_name && !has_protocol_legacy) {
        LOG_WARN("MQTT", "spec_data_2 missing canonical inverter identity fields (inverter_type_id/inverter_protocol_name)");
    }
    if (!has_schema) {
        LOG_WARN("MQTT", "spec_data_2 missing schema marker (spec_schema/schema_version)");
    }
    
    // Store inverter specs
    TransmitterManager::storeInverterSpecs(doc.as<JsonObject>());
    
    LOG_INFO("MQTT", "Stored inverter specs from transmitter/BE/spec_data_2");
}

void MqttClient::handleBatterySpecs(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT", "Processing transmitter/BE/battery_specs");
    
    // Parse battery-only data
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    
    if (error) {
        LOG_ERROR("MQTT", "Failed to parse battery_specs: %s", error.c_str());
        return;
    }
    
    // Store battery specs
    TransmitterManager::storeBatterySpecs(doc.as<JsonObject>());
    
    LOG_INFO("MQTT", "Stored battery specs from transmitter/BE/battery_specs");
}

void MqttClient::handleBatteryTypeCatalog(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT", "Processing transmitter/BE/battery_type_catalog");

    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_ERROR("MQTT", "Failed to parse battery_type_catalog: %s", error.c_str());
        return;
    }

    const uint16_t version = doc["catalog_version"] | 0;
    const uint16_t applied = TypeCatalogCache::battery_applied_version();
    if (version != 0 && applied != 0 && version <= applied) {
        LOG_DEBUG("MQTT", "Skipping battery_type_catalog version %u (applied=%u)",
                  (unsigned)version,
                  (unsigned)applied);
        return;
    }

    JsonArray types = doc["types"].as<JsonArray>();
    if (types.isNull()) {
        LOG_WARN("MQTT", "battery_type_catalog missing types array");
        return;
    }

    auto& entries = g_battery_catalog_scratch;
    entries.fill({});

    size_t count = 0;
    for (JsonVariant v : types) {
        if (count >= 128) {
            break;
        }

        const int id = v["id"] | -1;
        const char* name = v["name"] | "";
        if (id < 0 || id > 255 || name[0] == '\0') {
            continue;
        }

        entries[count].id = static_cast<uint8_t>(id);
        strncpy(entries[count].name, name, sizeof(entries[count].name) - 1);
        entries[count].name[sizeof(entries[count].name) - 1] = '\0';
        count++;
    }

    if (count > 0) {
        TypeCatalogCache::replace_battery_entries(entries.data(), count, version);
    }
}

void MqttClient::handleInverterTypeCatalog(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT", "Processing transmitter/BE/inverter_type_catalog");

    DynamicJsonDocument doc(3072);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_ERROR("MQTT", "Failed to parse inverter_type_catalog: %s", error.c_str());
        return;
    }

    const uint16_t version = doc["catalog_version"] | 0;
    const uint16_t applied = TypeCatalogCache::inverter_applied_version();
    if (version != 0 && applied != 0 && version < applied) {
        LOG_DEBUG("MQTT", "Skipping inverter_type_catalog version %u (applied=%u)",
                  (unsigned)version,
                  (unsigned)applied);
        return;
    }

    JsonArray types = doc["types"].as<JsonArray>();
    if (types.isNull()) {
        LOG_WARN("MQTT", "inverter_type_catalog missing types array");
        return;
    }

    auto& entries = g_inverter_catalog_scratch;
    entries.fill({});

    size_t count = 0;
    for (JsonVariant v : types) {
        if (count >= 128) {
            break;
        }

        const int id = v["id"] | -1;
        const char* name = v["name"] | "";
        if (id < 0 || id > 255 || name[0] == '\0') {
            continue;
        }

        entries[count].id = static_cast<uint8_t>(id);
        strncpy(entries[count].name, name, sizeof(entries[count].name) - 1);
        entries[count].name[sizeof(entries[count].name) - 1] = '\0';
        count++;
    }

    if (count > 0) {
        TypeCatalogCache::replace_inverter_entries(entries.data(), count, version);
    }
}

void MqttClient::handleCellData(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT", "Processing transmitter/BE/cell_data (%u bytes)", length);
    
    // Parse cell voltage and balancing data
    // 711-byte payload needs ~3000-3500 bytes for ArduinoJson deserialization
    DynamicJsonDocument doc(6144);  // Buffer for 96-cell voltage array + metadata
    DeserializationError error = deserializeJson(doc, json_payload, length);
    
    if (error) {
        LOG_ERROR("MQTT", "Failed to parse cell_data: %s", error.c_str());
        return;
    }
    
    // Log parsed data summary
    if (doc.containsKey("number_of_cells")) {
        LOG_DEBUG("MQTT", "Parsed cell data: %d cells", doc["number_of_cells"].as<int>());
    }
    
    // Log data source if present
    if (doc.containsKey("data_source")) {
        LOG_DEBUG("MQTT", "Data source: %s", doc["data_source"].as<const char*>());
    }
    
    // Store cell data in CellDataCache
    JsonObject cell_obj = doc.as<JsonObject>();
    CellDataCache::store_cell_data(&cell_obj);
    
    LOG_DEBUG("MQTT", "Stored cell data from transmitter/BE/cell_data");
}

void MqttClient::handleEventLogs(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT", "Processing transmitter/BE/event_logs");

    DynamicJsonDocument doc(6144);
    DeserializationError error = deserializeJson(doc, json_payload, length);

    if (error) {
        LOG_ERROR("MQTT", "Failed to parse event_logs: %s", error.c_str());
        return;
    }

    TransmitterManager::storeEventLogs(doc.as<JsonObject>());
    LOG_INFO("MQTT", "Stored event logs from transmitter/BE/event_logs");
}

void MqttClient::handleBatteryLive(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT", "Processing batt-emu/mqtt-v1/tx/state/battery_live");

    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_ERROR("MQTT", "Failed to parse battery_live: %s", error.c_str());
        return;
    }

    float soc_value = NAN;
    if (doc.containsKey("soc_percent")) {
        soc_value = doc["soc_percent"].as<float>();
    } else if (doc.containsKey("soc")) {
        soc_value = doc["soc"].as<float>();
    } else if (doc.containsKey("reported_soc")) {
        soc_value = doc["reported_soc"].as<float>() / 100.0f;
    }

    if (!isfinite(soc_value)) {
        LOG_WARN("MQTT", "battery_live missing SOC field");
        return;
    }

    int soc_int = static_cast<int>(soc_value + 0.5f);
    if (soc_int < 0) soc_int = 0;
    if (soc_int > 100) soc_int = 100;

    int32_t power_w = 0;
    if (doc.containsKey("power_w")) {
        power_w = doc["power_w"].as<int32_t>();
    } else if (doc.containsKey("power_W")) {
        power_w = doc["power_W"].as<int32_t>();
    } else if (doc.containsKey("power")) {
        power_w = doc["power"].as<int32_t>();
    }

    int32_t voltage_mv = 0;
    if (doc.containsKey("voltage_mv")) {
        voltage_mv = doc["voltage_mv"].as<int32_t>();
    } else if (doc.containsKey("voltage_V")) {
        voltage_mv = static_cast<int32_t>(doc["voltage_V"].as<float>() * 1000.0f);
    } else if (doc.containsKey("voltage_v")) {
        voltage_mv = static_cast<int32_t>(doc["voltage_v"].as<float>() * 1000.0f);
    }

    int32_t current_ma = 0;
    if (doc.containsKey("current_ma")) {
        current_ma = doc["current_ma"].as<int32_t>();
    } else if (doc.containsKey("current_mA")) {
        current_ma = doc["current_mA"].as<int32_t>();
    } else if (doc.containsKey("current_A")) {
        current_ma = static_cast<int32_t>(doc["current_A"].as<float>() * 1000.0f);
    } else if (doc.containsKey("current_a")) {
        current_ma = static_cast<int32_t>(doc["current_a"].as<float>() * 1000.0f);
    }

    int16_t temperature_centi_c = 0;
    bool has_temperature = false;

    if (doc.containsKey("battery_temp_centi_c")) {
        temperature_centi_c = doc["battery_temp_centi_c"].as<int16_t>();
        has_temperature = true;
    } else if (doc.containsKey("temperature_centi_c")) {
        // Legacy field name fallback
        temperature_centi_c = doc["temperature_centi_c"].as<int16_t>();
        has_temperature = true;
    } else if (doc.containsKey("temperature_c")) {
        temperature_centi_c = static_cast<int16_t>(doc["temperature_c"].as<float>() * 100.0f);
        has_temperature = true;
    }

    int16_t temperature_dC = 0;
    if (has_temperature) {
        temperature_dC = static_cast<int16_t>((temperature_centi_c >= 0)
            ? ((temperature_centi_c + 5) / 10)
            : ((temperature_centi_c - 5) / 10));
    }

    uint16_t max_charge_power_w = doc["max_charge_power_w"] | static_cast<uint16_t>(0);
    uint16_t max_discharge_power_w = doc["max_discharge_power_w"] | static_cast<uint16_t>(0);
    uint8_t bms_status = doc["bms_status"] | static_cast<uint8_t>(BMS_OFFLINE);

    battery_status_msg_t msg{};
    msg.type = msg_battery_status;
    msg.soc_percent_100 = static_cast<uint16_t>(soc_int * 100);
    msg.voltage_mV = static_cast<uint32_t>(voltage_mv < 0 ? 0 : voltage_mv);
    msg.current_mA = current_ma;
    msg.temperature_dC = temperature_dC;
    msg.power_W = power_w;
    msg.max_charge_power_W = max_charge_power_w;
    msg.max_discharge_power_W = max_discharge_power_w;
    msg.bms_status = bms_status;
    msg.checksum = 0;

    BatteryData::update_battery_status(msg);

    if (has_temperature) {
        const uint32_t seq = doc["seq"] | static_cast<uint32_t>(millis());
        const uint32_t uptime_ms = doc["uptime_ms"] | static_cast<uint32_t>(millis());
        TransmitterManager::storeBatteryTemperatureReport(true, seq, temperature_centi_c, uptime_ms);
    }
}

void MqttClient::handleRuntimeLed(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT", "Processing batt-emu/mqtt-v1/tx/state/runtime/led");

    DynamicJsonDocument doc(384);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_ERROR("MQTT", "Failed to parse runtime/led: %s", error.c_str());
        return;
    }

    const uint8_t color = doc["color"] | static_cast<uint8_t>(LED_WIRE_ORANGE);
    const uint8_t effect = doc["effect"] | static_cast<uint8_t>(LED_WIRE_CONTINUOUS);
    const char* status_text = doc["status"] | "";

    if (color > LED_WIRE_BLUE || effect > LED_WIRE_HEARTBEAT) {
        LOG_WARN("MQTT", "runtime/led out-of-range payload ignored (color=%u effect=%u)",
                 static_cast<unsigned>(color),
                 static_cast<unsigned>(effect));
        return;
    }

    RuntimeState::current_led_color = static_cast<LEDColor>(color);
    RuntimeState::current_led_effect = static_cast<LEDEffect>(effect);
    RuntimeState::current_led_status = parse_led_status_code(status_text);
}

void MqttClient::handleRuntimeSystem(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT", "Processing batt-emu/mqtt-v1/tx/state/runtime/system");

    DynamicJsonDocument doc(384);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_ERROR("MQTT", "Failed to parse runtime/system: %s", error.c_str());
        return;
    }

    system_status_msg_t msg{};
    msg.type = msg_system_status;
    msg.contactor_state = doc["contactor_state"] | static_cast<uint8_t>(0);
    msg.error_flags = doc["error_flags"] | static_cast<uint8_t>(0);
    msg.warning_flags = doc["warning_flags"] | static_cast<uint8_t>(0);
    msg.uptime_seconds = doc["uptime_seconds"] | static_cast<uint32_t>(0);
    msg.checksum = 0;

    BatteryData::update_system_status(msg);
}

void MqttClient::handleRuntimeCharger(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT", "Processing batt-emu/mqtt-v1/tx/state/runtime/charger");

    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_ERROR("MQTT", "Failed to parse runtime/charger: %s", error.c_str());
        return;
    }

    charger_status_msg_t msg{};
    msg.type = msg_charger_status;
    msg.hv_voltage_dV = static_cast<uint16_t>(doc["hv_voltage_dV"] | doc["hv_voltage_mv"] | 0);
    msg.hv_current_dA = static_cast<int16_t>(doc["hv_current_dA"] | doc["hv_current_ma"] | 0);
    msg.lv_voltage_dV = static_cast<uint16_t>(doc["lv_voltage_dV"] | doc["lv_voltage_mv"] | 0);
    msg.lv_current_dA = static_cast<int16_t>(doc["lv_current_dA"] | doc["lv_current_ma"] | 0);
    msg.ac_voltage_V = static_cast<uint16_t>(doc["ac_voltage_V"] | doc["ac_voltage_mv"] | 0);
    msg.ac_current_dA = static_cast<int16_t>(doc["ac_current_dA"] | doc["ac_current_ma"] | 0);
    msg.power_W = static_cast<uint16_t>(doc["power_W"] | doc["power_w"] | 0);
    msg.charger_status = static_cast<uint8_t>(doc["charger_status"] | 0);
    msg.checksum = 0;

    BatteryData::update_charger_status(msg);
}

void MqttClient::handleRuntimeInverter(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT", "Processing batt-emu/mqtt-v1/tx/state/runtime/inverter");

    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_ERROR("MQTT", "Failed to parse runtime/inverter: %s", error.c_str());
        return;
    }

    inverter_status_msg_t msg{};
    msg.type = msg_inverter_status;
    msg.ac_voltage_V = static_cast<uint16_t>(doc["ac_voltage_V"] | 0);
    msg.ac_frequency_dHz = static_cast<uint16_t>(doc["ac_frequency_dHz"] | 0);
    msg.ac_current_dA = static_cast<int16_t>(doc["ac_current_dA"] | doc["ac_current_ma"] | 0);
    msg.power_W = static_cast<int32_t>(doc["power_W"] | doc["power_w"] | 0);
    msg.inverter_status = static_cast<uint8_t>(doc["inverter_status"] | 0);
    msg.checksum = 0;

    BatteryData::update_inverter_status(msg);
}

void MqttClient::handleEventLogSummary(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT", "Processing batt-emu/mqtt-v1/tx/state/summary/event_logs");

    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_ERROR("MQTT", "Failed to parse summary/event_logs: %s", error.c_str());
        return;
    }

    const uint32_t seq = doc["seq"] | doc["snapshot_id"] | static_cast<uint32_t>(0);
    const uint32_t total_historical = doc["total_historical"] | doc["event_count"] | static_cast<uint32_t>(0);
    const uint32_t error_historical = doc["error_historical"] | static_cast<uint32_t>(0);
    const uint32_t new_total = doc["new_since_last_report_total"] | static_cast<uint32_t>(0);
    const uint32_t new_error = doc["new_since_last_report_error"] | static_cast<uint32_t>(0);
    const uint32_t uptime_ms = doc["uptime_ms"] | doc["ts_ms"] | static_cast<uint32_t>(0);

    TransmitterManager::storeEventLogSummary(seq,
                                             total_historical,
                                             error_historical,
                                             new_total,
                                             new_error,
                                             uptime_ms);
}

void MqttClient::handleEventLogsClearAck(const char* json_payload, size_t length) {
    DynamicJsonDocument doc(384);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_WARN("MQTT", "Failed to parse event_logs_clear ACK: %s", error.c_str());
        return;
    }

    const bool success = doc["success"] | false;
    const uint32_t summary_seq = doc["summary_seq"] | static_cast<uint32_t>(0);
    const uint32_t uptime_ms = doc["ts_ms"] | static_cast<uint32_t>(millis());

    TransmitterManager::storeEventLogClearAck(
        success ? TransmitterManager::kEventLogsClearAckSuccess : TransmitterManager::kEventLogsClearAckFailed,
        summary_seq,
        uptime_ms);
}

void MqttClient::handleStaticNetwork(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT", "Processing batt-emu/mqtt-v1/tx/state/static/network");

    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_ERROR("MQTT", "Failed to parse static/network: %s", error.c_str());
        return;
    }

    uint8_t curr_ip[4] = {};
    uint8_t curr_gw[4] = {};
    uint8_t curr_sn[4] = {};
    uint8_t stat_ip[4] = {};
    uint8_t stat_gw[4] = {};
    uint8_t stat_sn[4] = {};
    uint8_t dns1[4] = {};
    uint8_t dns2[4] = {};
    const bool use_static = doc["use_static_ip"] | false;

    if (!parse_ip_string(doc["current_ip"] | "", curr_ip) ||
        !parse_ip_string(doc["gateway"] | "", curr_gw) ||
        !parse_ip_string(doc["subnet"] | "", curr_sn)) {
        LOG_WARN("MQTT", "static/network missing required current IP fields");
        return;
    }
    parse_ip_string(doc["static_ip"] | "", stat_ip);
    parse_ip_string(doc["static_gateway"] | "", stat_gw);
    parse_ip_string(doc["static_subnet"] | "", stat_sn);
    parse_ip_string(doc["dns_primary"] | "", dns1);
    parse_ip_string(doc["dns_secondary"] | "", dns2);

    TransmitterNetwork::store_network_config(
        curr_ip, curr_gw, curr_sn,
        stat_ip, stat_gw, stat_sn,
        dns1, dns2,
        use_static,
        0,
        false);

    LOG_INFO("MQTT", "Updated transmitter network config from static/network retained topic");
}

void MqttClient::handleStaticMqtt(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT", "Processing batt-emu/mqtt-v1/tx/state/static/mqtt");

    DynamicJsonDocument doc(768);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_ERROR("MQTT", "Failed to parse static/mqtt: %s", error.c_str());
        return;
    }

    uint8_t server[4] = {};
    if (!parse_ip_string(doc["server"] | "", server)) {
        LOG_WARN("MQTT", "static/mqtt missing valid server IP");
        return;
    }

    TransmitterMqttSpecs::store_mqtt_config(
        doc["enabled"] | false,
        server,
        doc["port"] | static_cast<uint16_t>(1883),
        doc["username"] | "",
        "",
        doc["client_id"] | "",
        false,
        0,
        false);

    LOG_INFO("MQTT", "Updated transmitter MQTT config from static/mqtt retained topic");
}

void MqttClient::handleStaticPower(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT", "Processing batt-emu/mqtt-v1/tx/state/static/power");

    DynamicJsonDocument doc(768);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_ERROR("MQTT", "Failed to parse static/power: %s", error.c_str());
        return;
    }

    TransmitterManager::storeChargerSpecs(doc.as<JsonObject>());
    LOG_INFO("MQTT", "Updated transmitter power/charger specs from static/power retained topic");
}

void MqttClient::handleStaticLed(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT", "Processing batt-emu/mqtt-v1/tx/state/static/led");

    // Use payload-driven capacity to avoid NoMemory on larger retained payloads.
    // Keep a sane floor for ArduinoJson metadata and cap to avoid runaway allocation.
    const size_t doc_capacity = std::min<size_t>(4096, std::max<size_t>(1024, length * 3));
    DynamicJsonDocument doc(doc_capacity);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_ERROR("MQTT", "Failed to parse static/led: %s (len=%u cap=%u)",
                  error.c_str(),
                  static_cast<unsigned>(length),
                  static_cast<unsigned>(doc_capacity));
        return;
    }

    TransmitterManager::storeSystemSpecs(doc.as<JsonObject>());
    LOG_INFO("MQTT", "Updated transmitter LED/system specs from static/led retained topic");
}

void MqttClient::handleStaticSettings(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT", "Processing batt-emu/mqtt-v1/tx/state/static/settings");

    const size_t doc_capacity = std::min<size_t>(4096, std::max<size_t>(1536, length * 3));
    DynamicJsonDocument doc(doc_capacity);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_ERROR("MQTT", "Failed to parse static/settings: %s (len=%u cap=%u)",
                  error.c_str(),
                  static_cast<unsigned>(length),
                  static_cast<unsigned>(doc_capacity));
        return;
    }

    const JsonObject root =
        (doc.containsKey("settings") && doc["settings"].is<JsonObject>())
            ? doc["settings"].as<JsonObject>()
            : doc.as<JsonObject>();

    size_t sections_applied = 0;

    if (root.containsKey("battery") && root["battery"].is<JsonObject>()) {
        auto battery = TransmitterManager::getBatterySettings();
        const JsonObject b = root["battery"].as<JsonObject>();
        battery.capacity_wh = b["capacity_wh"] | battery.capacity_wh;
        battery.max_voltage_mv = b["max_voltage_mv"] | battery.max_voltage_mv;
        battery.min_voltage_mv = b["min_voltage_mv"] | battery.min_voltage_mv;
        battery.max_charge_current_a = b["max_charge_current_a"] | battery.max_charge_current_a;
        battery.max_discharge_current_a = b["max_discharge_current_a"] | battery.max_discharge_current_a;
        battery.soc_high_limit = b["soc_high_limit"] | battery.soc_high_limit;
        battery.soc_low_limit = b["soc_low_limit"] | battery.soc_low_limit;
        battery.cell_count = b["cell_count"] | battery.cell_count;
        battery.chemistry = b["chemistry"] | battery.chemistry;
        battery.version = b["version"] | battery.version;
        TransmitterManager::storeBatterySettings(battery);
        sections_applied++;
    }

    if (root.containsKey("battery_emulator") && root["battery_emulator"].is<JsonObject>()) {
        auto emu = TransmitterManager::getBatteryEmulatorSettings();
        const JsonObject e = root["battery_emulator"].as<JsonObject>();
        emu.double_battery = e["double_battery"] | emu.double_battery;
        emu.pack_max_voltage_dV = e["pack_max_voltage_dV"] | emu.pack_max_voltage_dV;
        emu.pack_min_voltage_dV = e["pack_min_voltage_dV"] | emu.pack_min_voltage_dV;
        emu.cell_max_voltage_mV = e["cell_max_voltage_mV"] | emu.cell_max_voltage_mV;
        emu.cell_min_voltage_mV = e["cell_min_voltage_mV"] | emu.cell_min_voltage_mV;
        emu.soc_estimated = e["soc_estimated"] | emu.soc_estimated;
        emu.led_mode = e["led_mode"] | emu.led_mode;
        TransmitterManager::storeBatteryEmulatorSettings(emu);
        sections_applied++;
    }

    if (root.containsKey("power") && root["power"].is<JsonObject>()) {
        auto power = TransmitterManager::getPowerSettings();
        const JsonObject p = root["power"].as<JsonObject>();
        power.charge_w = p["charge_w"] | power.charge_w;
        power.discharge_w = p["discharge_w"] | power.discharge_w;
        power.max_precharge_ms = p["max_precharge_ms"] | power.max_precharge_ms;
        power.precharge_duration_ms = p["precharge_duration_ms"] | power.precharge_duration_ms;
        power.equipment_stop_type = p["equipment_stop_type"] | power.equipment_stop_type;
        power.external_precharge_enabled = p["external_precharge_enabled"] | power.external_precharge_enabled;
        power.no_inverter_disconnect_contactor = p["no_inverter_disconnect_contactor"] | power.no_inverter_disconnect_contactor;
        power.version = p["version"] | power.version;
        TransmitterManager::storePowerSettings(power);
        sections_applied++;
    }

    if (root.containsKey("can") && root["can"].is<JsonObject>()) {
        auto can = TransmitterManager::getCanSettings();
        const JsonObject c = root["can"].as<JsonObject>();
        can.frequency_khz = c["frequency_khz"] | c["can_frequency_khz"] | can.frequency_khz;
        can.fd_frequency_mhz = c["fd_frequency_mhz"] | c["can_fd_frequency_mhz"] | can.fd_frequency_mhz;
        can.sofar_id = c["sofar_id"] | can.sofar_id;
        can.pylon_send_interval_ms = c["pylon_send_interval_ms"] | can.pylon_send_interval_ms;
        can.use_canfd_as_classic = c["use_canfd_as_classic"] | can.use_canfd_as_classic;
        can.version = c["version"] | can.version;
        TransmitterManager::storeCanSettings(can);
        sections_applied++;
    }

    if (root.containsKey("contactor") && root["contactor"].is<JsonObject>()) {
        auto contactor = TransmitterManager::getContactorSettings();
        const JsonObject c = root["contactor"].as<JsonObject>();
        contactor.control_enabled = c["control_enabled"] | contactor.control_enabled;
        contactor.nc_contactor = c["nc_contactor"] | c["nc_mode"] | contactor.nc_contactor;
        contactor.pwm_frequency_hz = c["pwm_frequency_hz"] | contactor.pwm_frequency_hz;
        contactor.pwm_control_enabled = c["pwm_control_enabled"] | c["pwm_enabled"] | contactor.pwm_control_enabled;
        contactor.pwm_hold_duty = c["pwm_hold_duty"] | contactor.pwm_hold_duty;
        contactor.periodic_bms_reset = c["periodic_bms_reset"] | contactor.periodic_bms_reset;
        contactor.bms_first_align_enabled = c["bms_first_align_enabled"] | contactor.bms_first_align_enabled;
        contactor.bms_first_align_target_minutes = c["bms_first_align_target_minutes"] | c["bms_first_align_target_mins"] | contactor.bms_first_align_target_minutes;
        contactor.version = c["version"] | contactor.version;
        TransmitterManager::storeContactorSettings(contactor);
        sections_applied++;
    }

    if (sections_applied == 0) {
        LOG_WARN("MQTT", "static/settings payload had no recognized sections");
        return;
    }

    LOG_INFO("MQTT", "Updated transmitter typed settings caches from static/settings retained topic");
}

void MqttClient::handleMetaVersion(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT", "Processing batt-emu/mqtt-v1/tx/meta/version");

    DynamicJsonDocument doc(384);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_ERROR("MQTT", "Failed to parse meta/version: %s", error.c_str());
        return;
    }

    const char* firmware = doc["firmware"] | "0.0.0";
    uint8_t major = 0;
    uint8_t minor = 0;
    uint8_t patch = 0;
    const char* version_str = firmware;
    if (version_str && (version_str[0] == 'v' || version_str[0] == 'V')) {
        version_str++;
    }
    (void)sscanf(version_str, "%hhu.%hhu.%hhu", &major, &minor, &patch);

    TransmitterManager::storeMetadata(
        true,
        "",
        doc["device"] | "TRANSMITTER",
        major,
        minor,
        patch,
        doc["build_date"] | "");

    LOG_INFO("MQTT", "Updated transmitter metadata from meta/version retained topic");
}

void MqttClient::handleMetaSchemaVersions(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT", "Processing batt-emu/mqtt-v1/tx/meta/schema_versions");

    DynamicJsonDocument doc(384);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_ERROR("MQTT", "Failed to parse meta/schema_versions: %s", error.c_str());
        return;
    }

    const uint32_t network_version = doc["network"] | static_cast<uint32_t>(0);
    const uint32_t mqtt_version = doc["mqtt"] | static_cast<uint32_t>(0);

    TransmitterNetwork::update_network_mode(TransmitterNetwork::is_static_ip(), network_version);

    TransmitterMqttSpecs::store_mqtt_config(
        TransmitterMqttSpecs::is_enabled(),
        TransmitterMqttSpecs::get_server(),
        TransmitterMqttSpecs::get_port(),
        TransmitterMqttSpecs::get_username(),
        "",
        TransmitterMqttSpecs::get_client_id(),
        TransmitterMqttSpecs::is_connected(),
        mqtt_version,
        false);

    LOG_INFO("MQTT", "Applied meta/schema_versions (network=%lu mqtt=%lu)",
             static_cast<unsigned long>(network_version),
             static_cast<unsigned long>(mqtt_version));
}

void MqttClient::handleMetaRuntime(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT", "Processing batt-emu/mqtt-v1/tx/meta/runtime");

    DynamicJsonDocument doc(768);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_ERROR("MQTT", "Failed to parse meta/runtime: %s", error.c_str());
        return;
    }

    const bool mqtt_connected = doc["mqtt_connected"] | false;
    const bool ethernet_connected = doc["ethernet_connected"] | false;
    const uint64_t uptime_ms = doc["uptime_ms"] | doc["ts_ms"] | static_cast<uint64_t>(TransmitterManager::getUptimeMs());
    const uint64_t unix_time = doc["unix_time"] | static_cast<uint64_t>(TransmitterManager::getUnixTime());
    const int16_t utc_offset_min = doc["utc_offset_min"] | static_cast<int16_t>(TransmitterManager::getUtcOffsetMin());
    const uint8_t time_source = doc["time_source"] | static_cast<uint8_t>(TransmitterManager::getTimeSource());

    if (doc.containsKey("heartbeat_flags")) {
        const uint8_t heartbeat_flags = doc["heartbeat_flags"] | static_cast<uint8_t>(0);
        TransmitterManager::updateHeartbeatFlags(heartbeat_flags);
    }

    if (doc.containsKey("tx_mac")) {
        const char* tx_mac_str = doc["tx_mac"] | "";
        uint8_t tx_mac[6] = {0};
        if (parse_mac_string(tx_mac_str, tx_mac)) {
            TransmitterManager::registerMAC(tx_mac);
        }
    }

    TransmitterManager::updateRuntimeStatus(mqtt_connected, ethernet_connected);
    TransmitterManager::updateTimeData(uptime_ms, unix_time, utc_offset_min, time_source);
    (void)TransmitterMqttSpecs::update_runtime_connection(mqtt_connected);

    LOG_INFO("MQTT", "Updated runtime state from meta/runtime (mqtt=%d eth=%d)",
             static_cast<int>(mqtt_connected),
             static_cast<int>(ethernet_connected));
}

void MqttClient::handleHeartbeat(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT", "Processing batt-emu/mqtt-v1/tx/state/heartbeat");

    DynamicJsonDocument doc(768);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_ERROR("MQTT", "Failed to parse heartbeat: %s", error.c_str());
        return;
    }

    const bool mqtt_connected = doc["mqtt_connected"] | true;
    const bool ethernet_connected = doc["ethernet_connected"] | false;
    const uint32_t seq = doc["seq"] | static_cast<uint32_t>(0);
    uint64_t uptime_ms = doc["uptime_ms"] | doc["ts_ms"] | static_cast<uint64_t>(0);
    const uint64_t unix_time = doc["unix_time"] | static_cast<uint64_t>(TransmitterManager::getUnixTime());
    const int16_t utc_offset_min = doc["utc_offset_min"] | static_cast<int16_t>(TransmitterManager::getUtcOffsetMin());
    const uint8_t time_source = doc["time_source"] | static_cast<uint8_t>(TransmitterManager::getTimeSource());

    if (uptime_ms == 0) {
        uptime_ms = TransmitterManager::getUptimeMs();
    }

    TransmitterManager::updateRuntimeStatus(mqtt_connected, ethernet_connected);
    (void)TransmitterMqttSpecs::update_runtime_connection(mqtt_connected);
    TransmitterManager::updateTimeData(uptime_ms, unix_time, utc_offset_min, time_source);

    if (doc.containsKey("heartbeat_flags")) {
        const uint8_t heartbeat_flags = doc["heartbeat_flags"] | static_cast<uint8_t>(0);
        TransmitterManager::updateHeartbeatFlags(heartbeat_flags);
    }

    if (doc.containsKey("tx_mac")) {
        const char* tx_mac_str = doc["tx_mac"] | "";
        uint8_t tx_mac[6] = {0};
        if (parse_mac_string(tx_mac_str, tx_mac)) {
            TransmitterManager::registerMAC(tx_mac);
        }
    }

    if (doc.containsKey("temperature_centi_c") || doc.containsKey("temperature_c")) {
        int16_t temperature_centi_c = 0;
        if (doc.containsKey("temperature_centi_c")) {
            temperature_centi_c = doc["temperature_centi_c"].as<int16_t>();
        } else {
            temperature_centi_c = static_cast<int16_t>(doc["temperature_c"].as<float>() * 100.0f);
        }
        TransmitterManager::storeTemperatureReport(true, seq, temperature_centi_c, static_cast<uint32_t>(uptime_ms));
    }

    LOG_DEBUG("MQTT", "Heartbeat received: seq=%lu uptime_ms=%lu mqtt=%d eth=%d src=%u",
              static_cast<unsigned long>(seq),
              static_cast<unsigned long>(uptime_ms),
              static_cast<int>(mqtt_connected),
              static_cast<int>(ethernet_connected),
              static_cast<unsigned>(time_source));
}

void MqttClient::ensureAckMutex() {
    if (ack_mutex_ == nullptr) {
        ack_mutex_ = xSemaphoreCreateMutex();
    }
}

void MqttClient::storePendingAck(const char* request_id,
                                 const char* topic,
                                 const char* payload,
                                 size_t length) {
    if (request_id == nullptr || request_id[0] == '\0' || topic == nullptr || payload == nullptr) {
        return;
    }

    ensureAckMutex();
    if (ack_mutex_ == nullptr) {
        return;
    }

    if (xSemaphoreTake(ack_mutex_, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    auto it = std::find_if(pending_acks_.begin(), pending_acks_.end(),
                           [request_id](const PendingAck& ack) {
                               return strcmp(ack.request_id, request_id) == 0;
                           });
    if (it == pending_acks_.end()) {
        if (pending_acks_.size() >= MAX_PENDING_ACKS) {
            pending_acks_.erase(pending_acks_.begin());
        }
        pending_acks_.push_back({});
        it = std::prev(pending_acks_.end());
    }

    strncpy(it->request_id, request_id, sizeof(it->request_id) - 1);
    it->request_id[sizeof(it->request_id) - 1] = '\0';
    strncpy(it->topic, topic, sizeof(it->topic) - 1);
    it->topic[sizeof(it->topic) - 1] = '\0';

    const size_t copy_len = std::min(length, sizeof(it->payload) - 1);
    memcpy(it->payload, payload, copy_len);
    it->payload[copy_len] = '\0';
    it->received = true;
    it->received_ms = millis();

    xSemaphoreGive(ack_mutex_);
}

bool MqttClient::tryConsumePendingAck(const char* request_id,
                                      const char* ack_topic,
                                      char* ack_payload,
                                      size_t ack_payload_size) {
    if (request_id == nullptr || request_id[0] == '\0' || ack_topic == nullptr) {
        return false;
    }

    ensureAckMutex();
    if (ack_mutex_ == nullptr) {
        return false;
    }

    if (xSemaphoreTake(ack_mutex_, pdMS_TO_TICKS(20)) != pdTRUE) {
        return false;
    }

    auto it = std::find_if(pending_acks_.begin(), pending_acks_.end(),
                           [request_id, ack_topic](const PendingAck& ack) {
                               return ack.received &&
                                      strcmp(ack.request_id, request_id) == 0 &&
                                      strcmp(ack.topic, ack_topic) == 0;
                           });

    const bool found = (it != pending_acks_.end());
    if (found) {
        if (ack_payload != nullptr && ack_payload_size > 0) {
            strlcpy(ack_payload, it->payload, ack_payload_size);
        }
        pending_acks_.erase(it);
    }

    xSemaphoreGive(ack_mutex_);
    return found;
}

void MqttClient::handleAckMessage(const char* topic, const char* json_payload, size_t length) {
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_WARN("MQTT", "Failed to parse ACK payload on %s: %s", topic, error.c_str());
        return;
    }

    const char* request_id = doc["request_id"] | "";
    if (request_id[0] == '\0') {
        LOG_WARN("MQTT", "Ignoring ACK on %s without request_id", topic);
        return;
    }

    storePendingAck(request_id, topic, json_payload, length);
    LOG_DEBUG("MQTT", "Stored ACK for request_id=%s on %s", request_id, topic);
}

/**
 * @brief Increment cell data subscriber count and resume subscription if needed
 * 
 * Thread-safe reference counting for multiple simultaneous SSE clients.
 * When first client connects (count 0→1), cancels any pending grace period
 * and ensures subscription is active.
 */
void MqttClient::incrementCellDataSubscribers() {
    cell_data_subscribers_++;
    
    if (cell_data_subscribers_ == 1) {
        // First SSE client connected
        
        // Cancel any pending grace period timer
        if (cell_data_pause_timer_ != nullptr) {
            xTimerStop(cell_data_pause_timer_, pdMS_TO_TICKS(100));
            xTimerDelete(cell_data_pause_timer_, pdMS_TO_TICKS(100));
            cell_data_pause_timer_ = nullptr;
            LOG_INFO("SUBSCRIPTION", "Cancelled grace period - SSE client reconnected");
        }

        if (cell_data_pause_requested_) {
            cell_data_pause_requested_ = false;
            LOG_INFO("SUBSCRIPTION", "Cancelled deferred cell_data pause request");
        }
        
        // Ensure subscription is active (if we were paused)
        if (cell_data_state_ == PAUSED || cell_data_state_ == ERROR) {
            if (mqtt_client_.connected()) {
                // Set state BEFORE calling subscribeToTopics() so it knows to subscribe to cell_data
                cell_data_state_ = SUBSCRIBED;
                subscribeToTopics();
                (void)publish_cell_data_stream_control(true);
                LOG_INFO("SUBSCRIPTION", "Resumed cell_data subscription (subscriber count: 1→%d)", 
                         cell_data_subscribers_);
            } else {
                cell_data_state_ = SUBSCRIBED;
                LOG_WARN("SUBSCRIPTION", "Cannot resume cell_data - MQTT not connected");
            }
        } else if (cell_data_state_ == PAUSING) {
            cell_data_state_ = SUBSCRIBED;
            if (mqtt_client_.connected()) {
                (void)publish_cell_data_stream_control(true);
            }
            LOG_INFO("SUBSCRIPTION", "Aborted pending pause - SSE client reconnected");
        } else {
            LOG_INFO("SUBSCRIPTION", "First SSE client connected (count: 0→%d, state: %s)", 
                     cell_data_subscribers_, getCellDataSubscriptionState());
        }
    } else {
        LOG_DEBUG("SUBSCRIPTION", "SSE client connected (count: %d→%d)", 
                  cell_data_subscribers_ - 1, cell_data_subscribers_);
    }
}

/**
 * @brief Decrement cell data subscriber count and start grace period if last client
 * 
 * Thread-safe reference counting for multiple simultaneous SSE clients.
 * When last client disconnects (count 1→0), starts a 30-second grace period timer.
 * If new client connects within grace period, timer is cancelled.
 * If grace period expires, pauses subscription to save bandwidth.
 */
void MqttClient::decrementCellDataSubscribers() {
    if (cell_data_subscribers_ > 0) {
        cell_data_subscribers_--;
    }
    
    if (cell_data_subscribers_ <= 0) {
        // Last SSE client disconnected - start grace period
        cell_data_subscribers_ = 0;
        
        // If timer already exists (shouldn't happen), delete it
        if (cell_data_pause_timer_ != nullptr) {
            xTimerDelete(cell_data_pause_timer_, pdMS_TO_TICKS(100));
        }

        if (mqtt_client_.connected()) {
            (void)publish_cell_data_stream_control(false);
        }
        
        // Create timer for grace period
        cell_data_pause_timer_ = xTimerCreate(
            "CellDataPauseTimer",                        // Name
            pdMS_TO_TICKS(CELL_DATA_GRACE_PERIOD_MS),   // Period (30 seconds)
            pdFALSE,                                      // Auto-reload: NO (one-shot)
            nullptr,                                      // Timer ID
            cellDataGracePeriodCallback                  // Callback
        );
        
        if (cell_data_pause_timer_ != nullptr) {
            xTimerStart(cell_data_pause_timer_, pdMS_TO_TICKS(100));
            cell_data_state_ = PAUSING;
            LOG_INFO("SUBSCRIPTION", "Last SSE client disconnected - grace period started (5s timeout)");
        } else {
            LOG_ERROR("SUBSCRIPTION", "Failed to create grace period timer!");
        }
    } else {
        LOG_DEBUG("SUBSCRIPTION", "SSE client disconnected (count: %d→%d)", 
                  cell_data_subscribers_ + 1, cell_data_subscribers_);
    }
}

/**
 * @brief Get current cell data subscriber count
 */
int MqttClient::getCellDataSubscriberCount() {
    return cell_data_subscribers_;
}

/**
 * @brief Check if cell data subscription is currently active
 */
bool MqttClient::isCellDataSubscriptionActive() {
    return cell_data_state_ == SUBSCRIBED;
}

/**
 * @brief Get human-readable subscription state for debugging
 */
const char* MqttClient::getCellDataSubscriptionState() {
    switch (cell_data_state_) {
        case SUBSCRIBED: return "SUBSCRIBED";
        case PAUSED:     return "PAUSED";
        case PAUSING:    return "PAUSING";
        case ERROR:      return "ERROR";
        default:         return "UNKNOWN";
    }
}

void MqttClient::incrementEventLogSubscribers() {
    bool was_zero = (event_log_subscribers_ == 0);
    event_log_subscribers_++;
    LOG_INFO("MQTT", "Event log subscriber count: %d", event_log_subscribers_);

    // Notify transmitter to start publishing event logs on first subscriber.
    if (was_zero) {
        // Start fresh receiver-side snapshot session on /events open.
        TransmitterEventLogCache::begin_snapshot_session(true);
        (void)publish_event_logs_stream_control(true);
    }
}

void MqttClient::decrementEventLogSubscribers() {
    if (event_log_subscribers_ > 0) {
        event_log_subscribers_--;
        LOG_INFO("MQTT", "Event log subscriber count: %d", event_log_subscribers_);

        if (event_log_subscribers_ == 0) {
            // Notify transmitter to stop publishing.
            (void)publish_event_logs_stream_control(false);

            // /events close semantics: clear receiver cache/session state.
            TransmitterEventLogCache::end_snapshot_session(true);
        }
    }
}

int MqttClient::getEventLogSubscriberCount() {
    return event_log_subscribers_;
}

void MqttClient::refreshEventLogSubscription() {
    if (event_log_subscribers_ > 0) {
        publish_event_logs_stream_control(true);
        LOG_DEBUG("MQTT", "Event log keepalive sent (active viewers: %d)", event_log_subscribers_);
    }
}

/**
 * @brief Timer callback: Pause cell_data subscription after grace period
 * 
 * Called after 30 seconds of no SSE clients connected.
 * Unsubscribes from batt-emu/mqtt-v1/tx/state/cell_data/chunk to save bandwidth and CPU.
 * 
 * If new SSE client connects before this callback fires, the timer is cancelled
 * in incrementCellDataSubscribers() and this callback never executes.
 */
void MqttClient::cellDataGracePeriodCallback(TimerHandle_t xTimer) {
    if (cell_data_subscribers_ <= 0) {
        // No new clients connected during grace period - defer MQTT unsubscribe
        // to MqttClient::loop() task context (PubSubClient is not thread-safe).
        cell_data_pause_requested_ = true;
        cell_data_state_ = PAUSING;
        LOG_INFO("SUBSCRIPTION", "Grace period expired - queued cell_data pause");
    } else {
        LOG_INFO("SUBSCRIPTION", "Grace period expired but new SSE clients connected (%d active) - keeping subscription active",
                 cell_data_subscribers_);
    }
    
    // Timer auto-deletes when auto-reload=false, but we can clean up
    cell_data_pause_timer_ = nullptr;
}

void MqttClient::processDeferredSubscriptionActions() {
    if (!cell_data_pause_requested_) {
        return;
    }

    // Clear request first to avoid repeated processing if this path logs/errors.
    cell_data_pause_requested_ = false;

    if (cell_data_subscribers_ > 0) {
        cell_data_state_ = SUBSCRIBED;
        LOG_INFO("SUBSCRIPTION", "Deferred pause skipped - SSE clients active (%d)", cell_data_subscribers_);
        return;
    }

    if (!mqtt_client_.connected()) {
        LOG_WARN("SUBSCRIPTION", "Cannot pause - not connected to MQTT");
        cell_data_state_ = ERROR;
        return;
    }

    const bool unsubscribed = mqtt_client_.unsubscribe(mqtt::topics::tx::STATE_CELL_DATA_CHUNK);
    if (unsubscribed) {
        (void)publish_cell_data_stream_control(false);
        cell_data_state_ = PAUSED;
        LOG_INFO("SUBSCRIPTION", "Paused cell_data subscription after grace period");
        LOG_INFO("SUBSCRIPTION", "Expected savings: ~30MB/month bandwidth, 43,200 JSON ops/day");
    } else {
        cell_data_state_ = ERROR;
        LOG_WARN("SUBSCRIPTION", "Failed to pause cell_data subscription (unsubscribe failed)");
    }
}

