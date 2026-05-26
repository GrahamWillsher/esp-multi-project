#include "mqtt_client.h"
#include "mqtt_ack_tracker.h"
#include "../../lib/webserver_lcd/utils/transmitter_manager.h"
#include "../../lib/webserver_lcd/utils/transmitter_network.h"
#include "../../lib/webserver_lcd/utils/transmitter_mqtt_specs.h"
#include "../../lib/webserver_lcd/utils/cell_data_cache.h"
#include "../../lib/webserver_lcd/utils/transmitter_event_log_cache.h"
#include "../../include/common_lcd.h"
#include <esp32common/mqtt/mqtt_topics_common.h>
#include "live_telemetry_cache.h"
#include "system_status_cache.h"
#include "charger_runtime_cache.h"
#include "inverter_runtime_cache.h"
#include "type_catalog_cache.h"
#include "logging_config.h"
#include "../runtime/display_update_queue.h"
#include <ArduinoJson.h>
#include <array>
#include <cmath>
#include <cstring>
#include <esp_system.h>
#include <firmware_version.h>
#include <firmware_metadata.h>
#include <esp_heap_caps.h>

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

bool publish_stream_control(const char* topic, const char* stream, const char* action) {
    if (!MqttClient::isEnabled() || !MqttClient::isConnected()) {
        LOG_WARN("MQTT", "Skipping stream/%s %s command: MQTT not connected",
                 stream ? stream : "<unknown>",
                 action ? action : "<unknown>");
        return false;
    }

    char payload[192];
    snprintf(payload,
             sizeof(payload),
             "{\"request_id\":\"rx-%08lx-%08lx\",\"stream\":\"%s\",\"action\":\"%s\"}",
             static_cast<unsigned long>(millis()),
             static_cast<unsigned long>(esp_random()),
             stream ? stream : "",
             action ? action : "");

    const bool ok = MqttClient::publishJson(topic, payload, false);
    LOG_INFO("MQTT", "stream/%s %s command %s",
             stream ? stream : "<unknown>",
             action ? action : "<unknown>",
             ok ? "published" : "failed");
    return ok;
}

bool publish_event_logs_stream_control(const char* action) {
    return publish_stream_control(mqtt::topics::rx::CMD_STREAM_EVENT_LOGS,
                                  "event_logs",
                                  action);
}

bool publish_cell_data_stream_control(const char* action) {
    return publish_stream_control(mqtt::topics::rx::CMD_STREAM_CELL_DATA,
                                  "cell_data",
                                  action);
}

// ── §17 Chunk reassembly ─────────────────────────────────────────────────
// Accumulates incoming §17 chunk envelopes and reassembles the original payload.
// Spec: max 2 in-flight sets, 3000 ms timeout per set, 1024 bytes per part,
//       max 8 parts → 8192 bytes maximum assembled payload.

constexpr uint32_t kRxChunkTimeoutMs  = 3000u;
constexpr size_t   kRxMaxSlots        = 2u;
constexpr size_t   kRxChunkPartLen    = 1024u;
constexpr size_t   kRxMaxParts        = 8u;
constexpr size_t   kRxAssemblyBufLen  = kRxMaxParts * kRxChunkPartLen + 1u;  // +1 for '\0'
constexpr size_t   kRxMqttPayloadBufLen = 6145u;  // PubSubClient buffer (6144) + terminator

struct ChunkSlot {
    bool     active{false};
    char     chunk_id[24]{};
    uint32_t start_ms{0};
    uint16_t total_parts{0};
    uint8_t  parts_mask{0};              // bit N set when part N received
    size_t   assembled_len{0};           // running max-end-offset seen
    char     buf[kRxAssemblyBufLen]{};   // assembled payload (null-terminated when done)
};

// g_rx_slots is allocated in PSRAM on first use via init_rx_slots(), saving
// ~16.5 KB of internal DRAM that was previously consumed at link time by .bss.
ChunkSlot* g_rx_slots = nullptr;
char* g_mqtt_payload_buf = nullptr;

static void init_rx_slots() {
    if (g_rx_slots != nullptr) return;
    g_rx_slots = static_cast<ChunkSlot*>(
        heap_caps_malloc(sizeof(ChunkSlot) * kRxMaxSlots, MALLOC_CAP_SPIRAM)
    );
    if (g_rx_slots) {
        memset(g_rx_slots, 0, sizeof(ChunkSlot) * kRxMaxSlots);
        LOG_INFO("MQTT", "Chunk reassembly slots allocated in PSRAM (%u bytes)",
                 static_cast<unsigned>(sizeof(ChunkSlot) * kRxMaxSlots));
    } else {
        LOG_WARN("MQTT", "PSRAM allocation for chunk slots failed – falling back to internal heap");
        g_rx_slots = static_cast<ChunkSlot*>(
            heap_caps_malloc(sizeof(ChunkSlot) * kRxMaxSlots, MALLOC_CAP_INTERNAL)
        );
        if (g_rx_slots) {
            memset(g_rx_slots, 0, sizeof(ChunkSlot) * kRxMaxSlots);
        }
    }
}

static void init_mqtt_payload_buffer() {
    if (g_mqtt_payload_buf != nullptr) return;

    g_mqtt_payload_buf = static_cast<char*>(
        heap_caps_malloc(kRxMqttPayloadBufLen, MALLOC_CAP_SPIRAM)
    );
    if (!g_mqtt_payload_buf) {
        LOG_WARN("MQTT", "PSRAM allocation for MQTT payload buffer failed – falling back to internal heap");
        g_mqtt_payload_buf = static_cast<char*>(
            heap_caps_malloc(kRxMqttPayloadBufLen, MALLOC_CAP_INTERNAL)
        );
    }

    if (g_mqtt_payload_buf) {
        g_mqtt_payload_buf[0] = '\0';
        LOG_INFO("MQTT", "MQTT payload buffer allocated (%u bytes)",
                 static_cast<unsigned>(kRxMqttPayloadBufLen));
    }
}

/**
 * @brief Accumulate one chunk envelope part into the appropriate reassembly slot.
 *
 * Call once per received chunk envelope.  When all parts of a set arrive,
 * the slot is released and a pointer to the assembled, null-terminated payload
 * is returned (valid until the next call to chunk_accumulate).
 *
 * @param chunk_id    Unique transfer identifier from the envelope.
 * @param part_num    0-based index of this part.
 * @param total_parts Total number of parts in this transfer.
 * @param data        Pointer to the (already JSON-decoded) data string for this part.
 * @param data_len    Byte length of @p data.
 * @param out_len     Set to the assembled payload length when complete.
 * @return Pointer to assembled payload on completion; nullptr while still accumulating.
 */
static const char* chunk_accumulate(const char* chunk_id,
                                     uint16_t    part_num,
                                     uint16_t    total_parts,
                                     const char* data,
                                     size_t      data_len,
                                     size_t*     out_len) {
    const uint32_t now_ms = static_cast<uint32_t>(millis());

    // Reap expired slots
    for (size_t _i = 0; _i < kRxMaxSlots; ++_i) {
        ChunkSlot& s = g_rx_slots[_i];
        if (s.active && (now_ms - s.start_ms > kRxChunkTimeoutMs)) {
            LOG_WARN("MQTT", "Chunk set '%.23s' timed out – discarding", s.chunk_id);
            s.active = false;
        }
    }

    // Validate parameters
    if (!chunk_id || !data || total_parts == 0u
            || total_parts > kRxMaxParts || part_num >= total_parts) {
        LOG_ERROR("MQTT", "chunk_accumulate: invalid parameters "
                  "(id=%s part=%u total=%u)",
                  chunk_id ? chunk_id : "null",
                  static_cast<unsigned>(part_num),
                  static_cast<unsigned>(total_parts));
        return nullptr;
    }

    // Find an existing slot for this chunk_id
    ChunkSlot* slot = nullptr;
    for (size_t _i = 0; _i < kRxMaxSlots; ++_i) {
        ChunkSlot& s = g_rx_slots[_i];
        if (s.active && strncmp(s.chunk_id, chunk_id, sizeof(s.chunk_id) - 1u) == 0) {
            slot = &s;
            break;
        }
    }

    if (!slot) {
        // Allocate a free slot
        for (size_t _i = 0; _i < kRxMaxSlots; ++_i) {
            if (!g_rx_slots[_i].active) { slot = &g_rx_slots[_i]; break; }
        }
        if (!slot) {
            // No free slots – evict the oldest
            slot = &g_rx_slots[0];
            for (size_t _i = 1; _i < kRxMaxSlots; ++_i) {
                if (g_rx_slots[_i].start_ms < slot->start_ms) slot = &g_rx_slots[_i];
            }
            LOG_WARN("MQTT", "No free chunk slots, evicting '%.23s'", slot->chunk_id);
            slot->active = false;
        }
        memset(slot, 0, sizeof(*slot));
        strncpy(slot->chunk_id, chunk_id, sizeof(slot->chunk_id) - 1u);
        slot->total_parts = total_parts;
        slot->start_ms    = now_ms;
        slot->active      = true;
    }

    if (slot->total_parts != total_parts) {
        LOG_ERROR("MQTT", "Chunk '%s': total_parts mismatch (expected %u, got %u)",
                  chunk_id, slot->total_parts, total_parts);
        return nullptr;
    }

    // Copy this part's data into the assembly buffer at its fixed offset
    const size_t offset = static_cast<size_t>(part_num) * kRxChunkPartLen;
    if (offset + data_len > sizeof(slot->buf) - 1u) {
        LOG_ERROR("MQTT", "Chunk '%s' part %u: buffer overflow", chunk_id, part_num);
        slot->active = false;
        return nullptr;
    }
    memcpy(slot->buf + offset, data, data_len);
    slot->parts_mask |= static_cast<uint8_t>(1u << part_num);

    const size_t end_offset = offset + data_len;
    if (end_offset > slot->assembled_len) {
        slot->assembled_len = end_offset;
    }

    // Check if all parts have arrived
    const uint8_t expected_mask =
        static_cast<uint8_t>((1u << total_parts) - 1u);
    if (slot->parts_mask != expected_mask) {
        return nullptr;  // Still accumulating
    }

    // Assembly complete – null-terminate and release the slot
    slot->buf[slot->assembled_len] = '\0';
    *out_len    = slot->assembled_len;
    const char* result = slot->buf;
    slot->active = false;
    return result;
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
unsigned long MqttClient::reconnect_interval_ms_ = MqttClient::RECONNECT_INTERVAL_MIN_MS;

// Cell data subscription state management
int MqttClient::cell_data_subscribers_ = 0;
MqttClient::CellDataSubscriptionState MqttClient::cell_data_state_ = MqttClient::PAUSED;
TimerHandle_t MqttClient::cell_data_pause_timer_ = nullptr;
volatile bool MqttClient::cell_data_pause_requested_ = false;

// Event log subscription management
int MqttClient::event_log_subscribers_ = 0;
volatile bool MqttClient::event_log_subscribe_requested_ = false;
volatile bool MqttClient::event_log_unsubscribe_requested_ = false;

void MqttClient::init(const uint8_t* mqtt_server, uint16_t mqtt_port, const char* client_id) {
    if (!mqtt_server) return;

    init_rx_slots();  // Allocate chunk reassembly buffers in PSRAM on first call
    init_mqtt_payload_buffer();
    
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
    if (now - last_connect_attempt_ < reconnect_interval_ms_) {
        return false;
    }
    last_connect_attempt_ = now;

    LOG_INFO("MQTT", "Connecting to broker %d.%d.%d.%d:%u (local=%s rssi=%d)...",
             broker_ip_[0], broker_ip_[1], broker_ip_[2], broker_ip_[3],
             static_cast<unsigned>(broker_port_),
             WiFi.localIP().toString().c_str(),
             static_cast<int>(WiFi.RSSI()));

    const char* presence_topic = mqtt::topics::rx::STATE_PRESENCE;
    bool connected = false;
    if (username_[0] != '\0') {
        connected = mqtt_client_.connect(client_id_,
                                         username_,
                                         password_,
                                         presence_topic,
                                         1,
                                         true,
                                         "offline");
    } else {
        connected = mqtt_client_.connect(client_id_,
                                         nullptr,
                                         nullptr,
                                         presence_topic,
                                         1,
                                         true,
                                         "offline");
    }

    if (connected) {
        LOG_INFO("MQTT", "Connected successfully");
        reconnect_interval_ms_ = RECONNECT_INTERVAL_MIN_MS;
        subscribeToTopics();
        publishReceiverPresence(true);
        publishReceiverMetaVersion();
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
        const unsigned long next_backoff = reconnect_interval_ms_ * 2UL;
        reconnect_interval_ms_ = (next_backoff > RECONNECT_INTERVAL_MAX_MS)
                                     ? RECONNECT_INTERVAL_MAX_MS
                                     : next_backoff;
        LOG_WARN("MQTT", "Next reconnect attempt in %lu ms", reconnect_interval_ms_);
        return false;
    }
}

void MqttClient::disconnect() {
    if (mqtt_client_.connected()) {
        publishReceiverPresence(false);
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
        reconnect_interval_ms_ = RECONNECT_INTERVAL_MIN_MS;
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
    if (g_mqtt_payload_buf == nullptr) {
        init_mqtt_payload_buffer();
    }

    if (g_mqtt_payload_buf == nullptr) {
        LOG_ERROR("MQTT", "Dropping MQTT message on %s: payload buffer unavailable", topic);
        return;
    }

    if (length >= kRxMqttPayloadBufLen) {
        LOG_ERROR("MQTT", "Dropping MQTT message on %s: payload too large (%u >= %u)",
                  topic,
                  static_cast<unsigned>(length),
                  static_cast<unsigned>(kRxMqttPayloadBufLen));
        return;
    }

    memcpy(g_mqtt_payload_buf, payload, length);
    g_mqtt_payload_buf[length] = '\0';
    const char* json_payload = g_mqtt_payload_buf;

    // Hash-based dispatch keeps topic handling O(1)-like for small fixed route sets,
    // while retaining strcmp guards to eliminate any practical collision risk.
    switch (fnv1a_runtime(topic)) {
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
        case fnv1a_const(mqtt::topics::tx::ACK_CONTROL):
            if (strcmp(topic, mqtt::topics::tx::ACK_CONTROL) == 0) {
                MqttAckTracker::handleAckMessage(topic, json_payload, length);
                return;
            }
            break;
        case fnv1a_const(mqtt::topics::tx::ACK_BATTERY):
            if (strcmp(topic, mqtt::topics::tx::ACK_BATTERY) == 0) {
                MqttAckTracker::handleAckMessage(topic, json_payload, length);
                return;
            }
            break;
        case fnv1a_const(mqtt::topics::tx::ACK_NETWORK):
            if (strcmp(topic, mqtt::topics::tx::ACK_NETWORK) == 0) {
                MqttAckTracker::handleAckMessage(topic, json_payload, length);
                return;
            }
            break;
        case fnv1a_const(mqtt::topics::tx::ACK_MQTT):
            if (strcmp(topic, mqtt::topics::tx::ACK_MQTT) == 0) {
                MqttAckTracker::handleAckMessage(topic, json_payload, length);
                return;
            }
            break;
        case fnv1a_const(mqtt::topics::tx::ACK_EVENT_LOGS_CLEAR):
            if (strcmp(topic, mqtt::topics::tx::ACK_EVENT_LOGS_CLEAR) == 0) {
                handleEventLogsClearAck(json_payload, length);
                MqttAckTracker::handleAckMessage(topic, json_payload, length);
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
    mqtt_client_.subscribe(mqtt::topics::tx::STATE_BATTERY_LIVE);
    mqtt_client_.subscribe(mqtt::topics::tx::STATE_RUNTIME_LED);
    mqtt_client_.subscribe(mqtt::topics::tx::STATE_RUNTIME_SYSTEM);
    mqtt_client_.subscribe(mqtt::topics::tx::STATE_RUNTIME_CHARGER);
    mqtt_client_.subscribe(mqtt::topics::tx::STATE_RUNTIME_INVERTER);
    mqtt_client_.subscribe(mqtt::topics::tx::META_VERSION);
    mqtt_client_.subscribe(mqtt::topics::tx::META_SCHEMA_VERSIONS);
    mqtt_client_.subscribe(mqtt::topics::tx::META_RUNTIME);
    mqtt_client_.subscribe(mqtt::topics::tx::STATE_SUMMARY_EVENT_LOGS);
    mqtt_client_.subscribe(mqtt::topics::tx::ACK_CONTROL);
    mqtt_client_.subscribe(mqtt::topics::tx::ACK_BATTERY);
    mqtt_client_.subscribe(mqtt::topics::tx::ACK_NETWORK);
    mqtt_client_.subscribe(mqtt::topics::tx::ACK_MQTT);
    mqtt_client_.subscribe(mqtt::topics::tx::ACK_EVENT_LOGS_CLEAR);
    mqtt_client_.subscribe(mqtt::topics::tx::STATE_HEARTBEAT);
    
    // Only subscribe to cell_data if not paused (subscription optimization)
    if (cell_data_state_ != PAUSED) {
        mqtt_client_.subscribe(mqtt::topics::tx::STATE_CELL_DATA_CHUNK);
        LOG_INFO("SUBSCRIPTION", "Subscribed to spec topics including cell_data");

        if (cell_data_subscribers_ > 0) {
            (void)publish_cell_data_stream_control("subscribe");
            LOG_INFO("SUBSCRIPTION", "Re-notified TX of cell_data stream after MQTT reconnect");
        }
    } else {
        LOG_INFO("SUBSCRIPTION", "Subscribed to spec topics only (cell_data paused)");
    }

    if (event_log_subscribers_ > 0) {
        mqtt_client_.subscribe(mqtt::topics::tx::STATE_EVENT_LOGS_CHUNK);
        LOG_INFO("SUBSCRIPTION", "Subscribed to event_logs (active viewers: %d)", event_log_subscribers_);
        publish_event_logs_stream_control("subscribe");
        LOG_INFO("SUBSCRIPTION", "Re-notified TX of event_log stream after MQTT reconnect");
    }
}

void MqttClient::handleSpecData(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT", "Processing batt-emu/mqtt-v1/tx/state/static/battery");
    
    // Parse combined spec data (battery, inverter, charger, system)
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    
    if (error) {
        LOG_ERROR("MQTT", "Failed to parse static battery: %s", error.c_str());
        return;
    }
    
    // Store in TransmitterManager
    TransmitterManager::storeStaticSpecs(doc.as<JsonObject>());
    
    LOG_INFO("MQTT", "Stored static battery specs from MQTT");
}

void MqttClient::handleSpecData2(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT", "Processing batt-emu/mqtt-v1/tx/state/static/inverter");
    
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
    LOG_DEBUG("MQTT", "Processing batt-emu/mqtt-v1/tx/state/static/battery");
    
    // Parse battery-only data
    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    
    if (error) {
        LOG_ERROR("MQTT", "Failed to parse battery specs: %s", error.c_str());
        return;
    }
    
    // Store battery specs
    TransmitterManager::storeBatterySpecs(doc.as<JsonObject>());
    
    LOG_INFO("MQTT", "Stored battery specs from MQTT");
}

void MqttClient::handleBatteryTypeCatalog(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT", "Processing batt-emu/mqtt-v1/tx/state/static/catalog_battery");

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
    LOG_DEBUG("MQTT", "Processing batt-emu/mqtt-v1/tx/state/static/catalog_inverter");

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
    LOG_DEBUG("MQTT", "Processing batt-emu/mqtt-v1/tx/state/cell_data/chunk (%u bytes)", length);

    const char* payload_to_parse = json_payload;
    size_t payload_len_to_parse = length;

    // First parse: either legacy full payload OR §17 chunk envelope.
    DynamicJsonDocument envelope_doc(2048);
    DeserializationError envelope_error = deserializeJson(envelope_doc, json_payload, length);
    if (!envelope_error && envelope_doc.containsKey("chunk_id") && envelope_doc.containsKey("data")) {
        const char* chunk_id = envelope_doc["chunk_id"] | "";
        const char* data = envelope_doc["data"] | "";

        uint16_t part_num = 0;
        if (envelope_doc.containsKey("part_num")) {
            part_num = envelope_doc["part_num"].as<uint16_t>();
        } else if (envelope_doc.containsKey("index")) {
            part_num = envelope_doc["index"].as<uint16_t>();
        }

        uint16_t total_parts = 0;
        if (envelope_doc.containsKey("total_parts")) {
            total_parts = envelope_doc["total_parts"].as<uint16_t>();
        } else if (envelope_doc.containsKey("total")) {
            total_parts = envelope_doc["total"].as<uint16_t>();
        }

        size_t assembled_len = 0;
        const char* assembled = chunk_accumulate(chunk_id,
                                                 part_num,
                                                 total_parts,
                                                 data,
                                                 strlen(data),
                                                 &assembled_len);
        if (!assembled) {
            LOG_DEBUG("MQTT", "Cell data chunk buffered (id=%s part=%u/%u)",
                      chunk_id,
                      static_cast<unsigned>(part_num + 1u),
                      static_cast<unsigned>(total_parts));
            return;
        }

        payload_to_parse = assembled;
        payload_len_to_parse = assembled_len;
        LOG_DEBUG("MQTT", "Cell data chunk set assembled (%u bytes)",
                  static_cast<unsigned>(assembled_len));
    }

    // Parse full cell voltage and balancing payload (legacy or reassembled)
    DynamicJsonDocument doc(6144);  // Buffer for 96-cell voltage array + metadata
    DeserializationError error = deserializeJson(doc, payload_to_parse, payload_len_to_parse);

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
    
    LOG_DEBUG("MQTT", "Stored cell data from MQTT");
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

    if (!std::isfinite(soc_value)) {
        LOG_WARN("MQTT", "battery_live missing SOC field");
        return;
    }

    int soc_int = static_cast<int>(soc_value + 0.5f);
    if (soc_int < 0) soc_int = 0;
    if (soc_int > 100) soc_int = 100;

    const uint8_t soc_percent = static_cast<uint8_t>(soc_int);
    LiveTelemetryCache::update_basic(soc_percent, power_w, static_cast<uint32_t>(voltage_mv < 0 ? 0 : voltage_mv));

    DisplayUpdateQueue::snapshot_t snapshot{};
    snapshot.soc_percent = soc_value;
    snapshot.power_w = power_w;
    (void)DisplayUpdateQueue::enqueue(snapshot);

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

    const uint16_t max_charge_power_w = doc["max_charge_power_w"] | static_cast<uint16_t>(0);
    const uint16_t max_discharge_power_w = doc["max_discharge_power_w"] | static_cast<uint16_t>(0);
    const uint8_t bms_status = doc["bms_status"] | static_cast<uint8_t>(0);

    if (has_temperature) {
        const uint32_t seq = doc["seq"] | static_cast<uint32_t>(millis());
        const uint32_t uptime_ms = doc["uptime_ms"] | static_cast<uint32_t>(millis());
        TransmitterManager::storeBatteryTemperatureReport(true, seq, temperature_centi_c, uptime_ms);
    }

    LOG_DEBUG("MQTT", "Stored live telemetry from MQTT (soc=%u power=%ld voltage=%ld current=%ld bms=%u)",
              static_cast<unsigned>(soc_percent),
              static_cast<long>(power_w),
              static_cast<long>(voltage_mv),
              static_cast<long>(current_ma),
              static_cast<unsigned>(bms_status));
    (void)max_charge_power_w; (void)max_discharge_power_w;
}

void MqttClient::handleRuntimeLed(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT", "Processing batt-emu/mqtt-v1/tx/state/runtime/led");

    DynamicJsonDocument doc(384);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_ERROR("MQTT", "Failed to parse runtime/led: %s", error.c_str());
        return;
    }

    const uint8_t color = doc["color"] | static_cast<uint8_t>(0);
    const uint8_t effect = doc["effect"] | static_cast<uint8_t>(0);
    const char* status_text = doc["status"] | "";

    if (color > 3 || effect > 2) {
        LOG_WARN("MQTT", "runtime/led out-of-range payload ignored (color=%u effect=%u)",
                 static_cast<unsigned>(color),
                 static_cast<unsigned>(effect));
        return;
    }

    RuntimeState::current_led_color = color;
    RuntimeState::current_led_effect = effect;
    RuntimeState::current_led_status = parse_led_status_code(status_text);
    RuntimeState::current_led_state_valid = true;
}

void MqttClient::handleRuntimeSystem(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT", "Processing batt-emu/mqtt-v1/tx/state/runtime/system");

    DynamicJsonDocument doc(384);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_ERROR("MQTT", "Failed to parse runtime/system: %s", error.c_str());
        return;
    }

    const uint8_t contactor_state = doc["contactor_state"] | static_cast<uint8_t>(0);
    const uint8_t error_flags = doc["error_flags"] | static_cast<uint8_t>(0);
    const uint8_t warning_flags = doc["warning_flags"] | static_cast<uint8_t>(0);
    const uint32_t uptime_seconds = doc["uptime_seconds"] | static_cast<uint32_t>(0);

    SystemStatusCache::update(contactor_state, error_flags, warning_flags, uptime_seconds);

    LOG_DEBUG("MQTT", "runtime/system contactor=%u errors=0x%02X warnings=0x%02X uptime_s=%lu",
              static_cast<unsigned>(contactor_state),
              static_cast<unsigned>(error_flags),
              static_cast<unsigned>(warning_flags),
              static_cast<unsigned long>(uptime_seconds));
}

void MqttClient::handleRuntimeCharger(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT", "Processing batt-emu/mqtt-v1/tx/state/runtime/charger");

    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_ERROR("MQTT", "Failed to parse runtime/charger: %s", error.c_str());
        return;
    }

    const float hv_voltage_V = doc["hv_voltage_V"] | (static_cast<float>(doc["hv_voltage_dV"] | 0) / 10.0f);
    const float hv_current_A = doc["hv_current_A"] | (static_cast<float>(doc["hv_current_dA"] | 0) / 10.0f);
    const float lv_voltage_V = doc["lv_voltage_V"] | (static_cast<float>(doc["lv_voltage_dV"] | 0) / 10.0f);
    const float lv_current_A = doc["lv_current_A"] | (static_cast<float>(doc["lv_current_dA"] | 0) / 10.0f);
    const uint16_t ac_voltage_V = static_cast<uint16_t>(doc["ac_voltage_V"] | 0);
    const float ac_current_A = doc["ac_current_A"] | (static_cast<float>(doc["ac_current_dA"] | 0) / 10.0f);
    const uint16_t power_W = static_cast<uint16_t>(doc["power_W"] | 0);
    const uint8_t charger_status = static_cast<uint8_t>(doc["charger_status"] | 0);

    ChargerRuntimeCache::update(hv_voltage_V, hv_current_A, lv_voltage_V, lv_current_A,
                                ac_voltage_V, ac_current_A, power_W, charger_status);
}

void MqttClient::handleRuntimeInverter(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT", "Processing batt-emu/mqtt-v1/tx/state/runtime/inverter");

    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_ERROR("MQTT", "Failed to parse runtime/inverter: %s", error.c_str());
        return;
    }

    const uint16_t ac_voltage_V = static_cast<uint16_t>(doc["ac_voltage_V"] | 0);
    const uint16_t ac_frequency_dHz = static_cast<uint16_t>(doc["ac_frequency_dHz"] | 0);
    const int16_t ac_current_dA = static_cast<int16_t>(doc["ac_current_dA"] | 0);
    const int32_t power_W = static_cast<int32_t>(doc["power_W"] | 0);
    const uint8_t inverter_status = static_cast<uint8_t>(doc["inverter_status"] | 0);

    InverterRuntimeCache::update(ac_voltage_V, ac_frequency_dHz, ac_current_dA, power_W, inverter_status);
}

void MqttClient::handleEventLogs(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT", "Processing batt-emu/mqtt-v1/tx/state/event_logs/chunk");

    const char* payload_to_parse = json_payload;
    size_t payload_len_to_parse = length;

    // First parse: either legacy full payload OR §17 chunk envelope.
    DynamicJsonDocument envelope_doc(2048);
    DeserializationError envelope_error = deserializeJson(envelope_doc, json_payload, length);
    if (!envelope_error && envelope_doc.containsKey("chunk_id") && envelope_doc.containsKey("data")) {
        const char* chunk_id = envelope_doc["chunk_id"] | "";
        const char* data = envelope_doc["data"] | "";

        uint16_t part_num = 0;
        if (envelope_doc.containsKey("part_num")) {
            part_num = envelope_doc["part_num"].as<uint16_t>();
        } else if (envelope_doc.containsKey("index")) {
            part_num = envelope_doc["index"].as<uint16_t>();
        }

        uint16_t total_parts = 0;
        if (envelope_doc.containsKey("total_parts")) {
            total_parts = envelope_doc["total_parts"].as<uint16_t>();
        } else if (envelope_doc.containsKey("total")) {
            total_parts = envelope_doc["total"].as<uint16_t>();
        }

        size_t assembled_len = 0;
        const char* assembled = chunk_accumulate(chunk_id,
                                                 part_num,
                                                 total_parts,
                                                 data,
                                                 strlen(data),
                                                 &assembled_len);
        if (!assembled) {
            LOG_DEBUG("MQTT", "Event log chunk buffered (id=%s part=%u/%u)",
                      chunk_id,
                      static_cast<unsigned>(part_num + 1u),
                      static_cast<unsigned>(total_parts));
            return;
        }

        payload_to_parse = assembled;
        payload_len_to_parse = assembled_len;
        LOG_DEBUG("MQTT", "Event log chunk set assembled (%u bytes)",
                  static_cast<unsigned>(assembled_len));
    }

    DynamicJsonDocument doc(6144);
    DeserializationError error = deserializeJson(doc, payload_to_parse, payload_len_to_parse);

    if (error) {
        LOG_ERROR("MQTT", "Failed to parse event_logs: %s", error.c_str());
        return;
    }

    TransmitterManager::storeEventLogs(doc.as<JsonObject>());
    LOG_INFO("MQTT", "Stored event logs from MQTT");
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
                (void)publish_cell_data_stream_control("subscribe");
                LOG_INFO("SUBSCRIPTION", "Resumed cell_data subscription (subscriber count: 1→%d)", 
                         cell_data_subscribers_);
            } else {
                cell_data_state_ = SUBSCRIBED;
                LOG_WARN("SUBSCRIPTION", "Cannot resume cell_data - MQTT not connected");
            }
        } else if (cell_data_state_ == PAUSING) {
            cell_data_state_ = SUBSCRIBED;
            if (mqtt_client_.connected()) {
                (void)publish_cell_data_stream_control("subscribe");
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
            (void)publish_cell_data_stream_control("unsubscribe");
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

void MqttClient::refreshCellDataSubscription() {
    if (cell_data_subscribers_ > 0) {
        (void)publish_cell_data_stream_control("keepalive");
        LOG_DEBUG("MQTT", "Cell-data keepalive sent (active viewers: %d)", cell_data_subscribers_);
    }
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
        (void)publish_event_logs_stream_control("subscribe");
        event_log_unsubscribe_requested_ = false;
        event_log_subscribe_requested_ = true;
    }
}

void MqttClient::decrementEventLogSubscribers() {
    if (event_log_subscribers_ > 0) {
        event_log_subscribers_--;
        LOG_INFO("MQTT", "Event log subscriber count: %d", event_log_subscribers_);

        if (event_log_subscribers_ == 0) {
            // Notify transmitter to stop publishing.
            (void)publish_event_logs_stream_control("unsubscribe");

            // /events close semantics: clear receiver cache/session state.
            TransmitterEventLogCache::end_snapshot_session(true);
            event_log_subscribe_requested_ = false;
            event_log_unsubscribe_requested_ = true;
        }
    }
}

int MqttClient::getEventLogSubscriberCount() {
    return event_log_subscribers_;
}

void MqttClient::refreshEventLogSubscription() {
    if (event_log_subscribers_ > 0) {
        publish_event_logs_stream_control("keepalive");
        LOG_DEBUG("MQTT", "Event log keepalive sent (active viewers: %d)", event_log_subscribers_);
    }
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
    if (event_log_subscribe_requested_) {
        if (!mqtt_client_.connected()) {
            LOG_DEBUG("SUBSCRIPTION", "Deferred event_logs subscribe pending - MQTT not connected yet");
        } else if (mqtt_client_.subscribe(mqtt::topics::tx::STATE_EVENT_LOGS_CHUNK)) {
            event_log_subscribe_requested_ = false;
            LOG_INFO("SUBSCRIPTION", "Subscribed to event_logs after first viewer connected");
        } else {
            LOG_WARN("SUBSCRIPTION", "Failed to subscribe to event_logs (will retry)");
        }
    }

    if (event_log_unsubscribe_requested_) {
        if (event_log_subscribers_ > 0) {
            event_log_unsubscribe_requested_ = false;
        } else if (!mqtt_client_.connected()) {
            LOG_DEBUG("SUBSCRIPTION", "Deferred event_logs unsubscribe pending - MQTT not connected");
        } else if (mqtt_client_.unsubscribe(mqtt::topics::tx::STATE_EVENT_LOGS_CHUNK)) {
            event_log_unsubscribe_requested_ = false;
            LOG_INFO("SUBSCRIPTION", "Unsubscribed from event_logs after last viewer disconnected");
        } else {
            LOG_WARN("SUBSCRIPTION", "Failed to unsubscribe from event_logs (will retry)");
        }
    }

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
        (void)publish_cell_data_stream_control("unsubscribe");
        cell_data_state_ = PAUSED;
        LOG_INFO("SUBSCRIPTION", "Paused cell_data subscription after grace period");
        LOG_INFO("SUBSCRIPTION", "Expected savings: ~30MB/month bandwidth, 43,200 JSON ops/day");
    } else {
        cell_data_state_ = ERROR;
        LOG_WARN("SUBSCRIPTION", "Failed to pause cell_data subscription (unsubscribe failed)");
    }
}

void MqttClient::handleStaticNetwork(const char* json_payload, size_t length) {
    LOG_DEBUG("MQTT", "Processing batt-emu/mqtt-v1/tx/state/static/network");

    DynamicJsonDocument doc(512);
    DeserializationError error = deserializeJson(doc, json_payload, length);
    if (error) {
        LOG_ERROR("MQTT", "Failed to parse static/network: %s", error.c_str());
        return;
    }

    uint8_t curr_ip[4]   = {};
    uint8_t curr_gw[4]   = {};
    uint8_t curr_sn[4]   = {};
    uint8_t stat_ip[4]   = {};
    uint8_t stat_gw[4]   = {};
    uint8_t stat_sn[4]   = {};
    uint8_t dns1[4]      = {};
    uint8_t dns2[4]      = {};
    const bool use_static = doc["use_static_ip"] | false;

    if (!parse_ip_string(doc["current_ip"]  | "", curr_ip) ||
        !parse_ip_string(doc["gateway"]     | "", curr_gw) ||
        !parse_ip_string(doc["subnet"]      | "", curr_sn)) {
        LOG_WARN("MQTT", "static/network missing required current IP fields");
        return;
    }
    parse_ip_string(doc["static_ip"]      | "", stat_ip);
    parse_ip_string(doc["static_gateway"] | "", stat_gw);
    parse_ip_string(doc["static_subnet"]  | "", stat_sn);
    parse_ip_string(doc["dns_primary"]    | "", dns1);
    parse_ip_string(doc["dns_secondary"]  | "", dns2);

    TransmitterNetwork::store_network_config(
        curr_ip, curr_gw, curr_sn,
        stat_ip, stat_gw, stat_sn,
        dns1, dns2,
        use_static,
        0,       // config_version — not carried in this topic
        false);  // don't persist (volatile, refreshed on each reconnect)

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

    const bool     enabled   = doc["enabled"]   | false;
    const uint16_t port      = doc["port"]      | static_cast<uint16_t>(1883);
    const char*    username  = doc["username"]  | "";
    const char*    client_id = doc["client_id"] | "";

    TransmitterMqttSpecs::store_mqtt_config(
        enabled, server, port,
        username, "",  // password not carried in static topic
        client_id,
        false,    // connected state not known from static topic
        0,        // config_version
        false);   // don't persist

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

    const char* build_date = doc["build_date"] | "";

    TransmitterManager::storeMetadata(
        true,
        "",
        doc["device"] | "TRANSMITTER",
        major,
        minor,
        patch,
        build_date);

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

void MqttClient::publishReceiverMetaVersion() {
    if (!mqtt_client_.connected()) {
        return;
    }

    const char* build_date = FW_BUILD_DATE;
    if (FirmwareMetadata::isValid(FirmwareMetadata::metadata) &&
        FirmwareMetadata::metadata.build_date[0] != '\0') {
        build_date = FirmwareMetadata::metadata.build_date;
    }

    char payload[300];
    snprintf(payload,
             sizeof(payload),
             R"({"device":"%s","firmware":"%s","firmware_number":%lu,"protocol":%u,"build_date":"%s","schema":1,"ts_ms":%lu})",
             DEVICE_NAME,
             FW_VERSION_STRING,
             static_cast<unsigned long>(FW_VERSION_NUMBER),
             static_cast<unsigned>(PROTOCOL_VERSION),
             build_date,
             static_cast<unsigned long>(millis()));

    const bool ok = mqtt_client_.publish(mqtt::topics::rx::META_VERSION, payload, true);
    LOG_INFO("MQTT", "Receiver meta/version publish %s", ok ? "ok" : "failed");
}

void MqttClient::publishReceiverPresence(bool online) {
    if (!mqtt_client_.connected()) {
        return;
    }

    const bool ok = mqtt_client_.publish(mqtt::topics::rx::STATE_PRESENCE,
                                         online ? "online" : "offline",
                                         true);
    LOG_INFO("MQTT", "Receiver presence=%s publish %s",
             online ? "online" : "offline",
             ok ? "ok" : "failed");
}

