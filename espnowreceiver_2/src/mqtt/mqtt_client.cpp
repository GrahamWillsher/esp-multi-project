#include "mqtt_client.h"
#include "../lib/webserver/utils/transmitter_manager.h"
#include "../lib/webserver/utils/cell_data_cache.h"
#include "../common.h"
#include "../espnow/espnow_send.h"
#include "../espnow/type_catalog_cache.h"
#include <ArduinoJson.h>
#include <array>
#include <cstring>

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

} // namespace

// Static member initialization
WiFiClient MqttClient::wifi_client_;
PubSubClient MqttClient::mqtt_client_(wifi_client_);
char MqttClient::client_id_[32] = "espnow_receiver";
char MqttClient::username_[32] = "";
char MqttClient::password_[32] = "";
uint8_t MqttClient::broker_ip_[4] = {0, 0, 0, 0};
uint16_t MqttClient::broker_port_ = 1883;
bool MqttClient::enabled_ = false;
unsigned long MqttClient::last_connect_attempt_ = 0;

// Cell data subscription state management
int MqttClient::cell_data_subscribers_ = 0;
MqttClient::CellDataSubscriptionState MqttClient::cell_data_state_ = MqttClient::PAUSED;
TimerHandle_t MqttClient::cell_data_pause_timer_ = nullptr;

// Event log subscription management
int MqttClient::event_log_subscribers_ = 0;

void MqttClient::init(const uint8_t* mqtt_server, uint16_t mqtt_port, const char* client_id) {
    if (!mqtt_server) return;
    
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
    
    // Throttle connection attempts
    unsigned long now = millis();
    if (now - last_connect_attempt_ < RECONNECT_INTERVAL_MS) {
        return false;
    }
    last_connect_attempt_ = now;
    
    LOG_INFO("MQTT", "Connecting to broker...");
    
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
        LOG_ERROR("MQTT", "Connection failed, state=%d", mqtt_client_.state());
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

void MqttClient::loop() {
    if (!enabled_) return;
    
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
        case fnv1a_const("transmitter/BE/spec_data"):
            if (strcmp(topic, "transmitter/BE/spec_data") == 0) {
                handleSpecData(json_payload, length);
                return;
            }
            break;
        case fnv1a_const("transmitter/BE/spec_data_2"):
            if (strcmp(topic, "transmitter/BE/spec_data_2") == 0) {
                handleSpecData2(json_payload, length);
                return;
            }
            break;
        case fnv1a_const("transmitter/BE/battery_specs"):
            if (strcmp(topic, "transmitter/BE/battery_specs") == 0) {
                handleBatterySpecs(json_payload, length);
                return;
            }
            break;
        case fnv1a_const("transmitter/BE/battery_type_catalog"):
            if (strcmp(topic, "transmitter/BE/battery_type_catalog") == 0) {
                handleBatteryTypeCatalog(json_payload, length);
                return;
            }
            break;
        case fnv1a_const("transmitter/BE/inverter_type_catalog"):
            if (strcmp(topic, "transmitter/BE/inverter_type_catalog") == 0) {
                handleInverterTypeCatalog(json_payload, length);
                return;
            }
            break;
        case fnv1a_const("transmitter/BE/cell_data"):
            if (strcmp(topic, "transmitter/BE/cell_data") == 0) {
                handleCellData(json_payload, length);
                return;
            }
            break;
        case fnv1a_const("transmitter/BE/event_logs"):
            if (strcmp(topic, "transmitter/BE/event_logs") == 0) {
                handleEventLogs(json_payload, length);
                return;
            }
            break;
        default:
            break;
    }

    LOG_DEBUG("MQTT", "Ignoring message on unhandled topic: %s", topic);
}

void MqttClient::subscribeToTopics() {
    // Phase 1.5: Subscribe to transmitter namespace topics (was BE/* - now transmitter/BE/*)
    // Prevents collisions with other devices publishing to BE/* topics
    mqtt_client_.subscribe("transmitter/BE/spec_data");
    mqtt_client_.subscribe("transmitter/BE/spec_data_2");
    mqtt_client_.subscribe("transmitter/BE/battery_specs");
    mqtt_client_.subscribe("transmitter/BE/battery_type_catalog");
    mqtt_client_.subscribe("transmitter/BE/inverter_type_catalog");
    mqtt_client_.subscribe("transmitter/BE/event_logs");
    
    // Only subscribe to cell_data if not paused (subscription optimization)
    if (cell_data_state_ != PAUSED) {
        mqtt_client_.subscribe("transmitter/BE/cell_data");
        LOG_INFO("[SUBSCRIPTION]", "Subscribed to all topics including cell_data");
    } else {
        LOG_INFO("[SUBSCRIPTION]", "Subscribed to spec topics only (cell_data paused)");
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
    
    // Parse inverter-specific data
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    
    if (error) {
        LOG_ERROR("MQTT", "Failed to parse spec_data_2: %s", error.c_str());
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
    if (version != 0 && applied != 0 && version <= applied) {
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
            LOG_INFO("[SUBSCRIPTION]", "Cancelled grace period - SSE client reconnected");
        }
        
        // Ensure subscription is active (if we were paused)
        if (cell_data_state_ == PAUSED) {
            if (mqtt_client_.connected()) {
                // Set state BEFORE calling subscribeToTopics() so it knows to subscribe to cell_data
                cell_data_state_ = SUBSCRIBED;
                subscribeToTopics();
                LOG_INFO("[SUBSCRIPTION]", "Resumed cell_data subscription (subscriber count: 1→%d)", 
                         cell_data_subscribers_);
            } else {
                LOG_WARN("[SUBSCRIPTION]", "Cannot resume cell_data - MQTT not connected");
            }
        } else {
            LOG_INFO("[SUBSCRIPTION]", "First SSE client connected (count: 0→%d, state: %s)", 
                     cell_data_subscribers_, getCellDataSubscriptionState());
        }
    } else {
        LOG_DEBUG("[SUBSCRIPTION]", "SSE client connected (count: %d→%d)", 
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
            LOG_INFO("[SUBSCRIPTION]", "Last SSE client disconnected - grace period started (5s timeout)");
        } else {
            LOG_ERROR("[SUBSCRIPTION]", "Failed to create grace period timer!");
        }
    } else {
        LOG_DEBUG("[SUBSCRIPTION]", "SSE client disconnected (count: %d→%d)", 
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
    LOG_INFO("[MQTT]", "Event log subscriber count: %d", event_log_subscribers_);

    // Notify transmitter to start publishing event logs (ESP-NOW) on first subscriber
    if (was_zero) {
        send_event_logs_control(true);
    }
}

void MqttClient::decrementEventLogSubscribers() {
    if (event_log_subscribers_ > 0) {
        event_log_subscribers_--;
        LOG_INFO("[MQTT]", "Event log subscriber count: %d", event_log_subscribers_);
        
        if (event_log_subscribers_ == 0) {
            // Notify transmitter to stop publishing (ESP-NOW)
            send_event_logs_control(false);
        }
    }
}

int MqttClient::getEventLogSubscriberCount() {
    return event_log_subscribers_;
}

/**
 * @brief Timer callback: Pause cell_data subscription after grace period
 * 
 * Called after 30 seconds of no SSE clients connected.
 * Unsubscribes from transmitter/BE/cell_data to save bandwidth and CPU.
 * 
 * If new SSE client connects before this callback fires, the timer is cancelled
 * in incrementCellDataSubscribers() and this callback never executes.
 */
void MqttClient::cellDataGracePeriodCallback(TimerHandle_t xTimer) {
    if (cell_data_subscribers_ <= 0) {
        // No new clients connected during grace period - pause subscription
        
        // Unsubscribe from cell_data by re-subscribing to other topics only
        if (mqtt_client_.connected()) {
            mqtt_client_.unsubscribe("transmitter/BE/cell_data");
            
            cell_data_state_ = PAUSED;
            LOG_INFO("[SUBSCRIPTION]", "Paused cell_data subscription after grace period");
            LOG_INFO("[SUBSCRIPTION]", "Expected savings: ~30MB/month bandwidth, 43,200 JSON ops/day");
        } else {
            LOG_WARN("[SUBSCRIPTION]", "Cannot pause - not connected to MQTT");
            cell_data_state_ = ERROR;
        }
    } else {
        LOG_INFO("[SUBSCRIPTION]", "Grace period expired but new SSE clients connected (%d active) - keeping subscription active",
                 cell_data_subscribers_);
    }
    
    // Timer auto-deletes when auto-reload=false, but we can clean up
    cell_data_pause_timer_ = nullptr;
}

