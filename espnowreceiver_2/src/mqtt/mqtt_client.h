#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <vector>
#include <freertos/timers.h>
#include <freertos/semphr.h>

/**
 * @brief Receiver-local MQTT runtime client for subscribing to transmitter topics
 *
 * Ownership boundary (Phase 3 cleanup):
 * - This class tracks RECEIVER↔BROKER runtime connection state only.
 * - Transmitter-reported MQTT state/config shown in the web UI is cached in
 *   `TransmitterMqttSpecs` / `TransmitterManager` and updated from MQTT
 *   retained state/metadata streams.
 *
 * This module subscribes to topics published by the transmitter's battery emulator:
 * - BE/spec_data: Combined battery/inverter/charger/system specs
 * - BE/spec_data_2: Inverter-specific specs (alternative topic)
 * - BE/battery_specs: Battery-only specs
 * 
 * Received data is stored in TransmitterManager for web UI access.
 */
class MqttClient {
public:
    /**
     * @brief Initialize MQTT client with broker configuration
     * @param mqtt_server MQTT broker IP address
     * @param mqtt_port MQTT broker port (default 1883)
    * @param client_id MQTT client identifier (default "battery_emulator_receiver")
     */
    static void init(const uint8_t* mqtt_server, uint16_t mqtt_port = 1883, 
                const char* client_id = "battery_emulator_receiver");
    
    /**
     * @brief Set authentication credentials
     * @param username MQTT username (nullptr for no auth)
     * @param password MQTT password (nullptr for no auth)
     */
    static void setAuth(const char* username, const char* password);
    
    /**
     * @brief Connect to MQTT broker and subscribe to topics
     * @return true if connection successful
     */
    static bool connect();
    
    /**
     * @brief Disconnect from MQTT broker
     */
    static void disconnect();
    
    /**
     * @brief Check if connected to MQTT broker
     * @return true if connected
     */
    static bool isConnected();

    /**
     * @brief Publish raw JSON payload to MQTT topic
     * @param topic MQTT topic
     * @param payload Null-terminated JSON payload
     * @param retained Retained flag (default false)
     * @return true when publish succeeded
     */
    static bool publishJson(const char* topic, const char* payload, bool retained = false);

    /**
     * @brief Publish a JSON payload and wait for the matching ACK topic/request_id
     * @param topic Command topic to publish to
     * @param payload JSON payload
     * @param request_id Correlation id expected in ACK payload
     * @param ack_topic Expected ACK topic
     * @param timeout_ms Maximum time to wait for ACK
     * @param ack_payload Optional buffer to receive ACK payload
     * @param ack_payload_size Size of ack_payload buffer
     * @return true if ACK arrived before timeout
     */
    static bool publishJsonAndWaitForAck(const char* topic,
                                         const char* payload,
                                         const char* request_id,
                                         const char* ack_topic,
                                         uint32_t timeout_ms,
                                         char* ack_payload = nullptr,
                                         size_t ack_payload_size = 0);
    
    /**
     * @brief Process incoming MQTT messages (call in loop)
     */
    static void loop();
    
    /**
     * @brief Enable/disable MQTT client
     * @param enabled true to enable, false to disable
     */
    static void setEnabled(bool enabled);
    
    /**
     * @brief Check if MQTT client is enabled
     * @return true if enabled
     */
    static bool isEnabled();
    
    /**
     * @brief Increment cell data subscriber count (for SSE clients)
     * 
     * When first SSE client connects, automatically resumes cell_data subscription.
     * Uses reference counting to support multiple simultaneous clients.
     * 
     * Call from SSE handler connection start.
     */
    static void incrementCellDataSubscribers();
    
    /**
     * @brief Decrement cell data subscriber count and start grace period if last client
     * 
     * When last SSE client disconnects, starts a 30-second grace period timer.
     * After grace period expires, pauses cell_data subscription to save bandwidth.
     * If new client connects within grace period, timer is cancelled and subscription continues.
     * 
     * Call from SSE handler connection end (when loop breaks).
     */
    static void decrementCellDataSubscribers();
    
    /**
     * @brief Get current cell data subscriber count
     * @return number of active SSE clients connected to /api/cell_stream
     */
    static int getCellDataSubscriberCount();
    
    /**
     * @brief Check if cell data subscription is currently active
     * @return true if actively receiving cell_data messages from MQTT
     */
    static bool isCellDataSubscriptionActive();
    
    /**
     * @brief Get human-readable subscription state for debugging
     * @return state string: "SUBSCRIBED", "PAUSED", "PAUSING", "ERROR", or "UNKNOWN"
     */
    static const char* getCellDataSubscriptionState();

private:
    // Cell data subscription state machine
    enum CellDataSubscriptionState {
        SUBSCRIBED = 0,  // Actively receiving cell_data messages
        PAUSED = 1,      // Not receiving, can be resumed
        PAUSING = 2,     // Grace period active, pausing soon
        ERROR = 3        // Error state
    };
    static WiFiClient wifi_client_;
    static PubSubClient mqtt_client_;
    static char client_id_[32];
    static char username_[32];
    static char password_[32];
    static uint8_t broker_ip_[4];
    static uint16_t broker_port_;
    static bool enabled_;
    static unsigned long last_connect_attempt_;
    static const unsigned long RECONNECT_INTERVAL_MS = 5000;
    
    // Cell data subscription management (for SSE clients)
    static int cell_data_subscribers_;           // Count of active SSE clients
    static CellDataSubscriptionState cell_data_state_;
    static TimerHandle_t cell_data_pause_timer_;  // Timer handle for grace period
    static volatile bool cell_data_pause_requested_; // Deferred pause request (processed in MQTT task)
    static const uint32_t CELL_DATA_GRACE_PERIOD_MS = 5000;  // 5 second grace period
    
    // Event log subscription management
    static int event_log_subscribers_;  // Count of clients viewing /events page
    
    /**
     * @brief Timer callback: Pause cell_data subscription after grace period
     */
    static void cellDataGracePeriodCallback(TimerHandle_t xTimer);

    /**
     * @brief Process deferred subscription actions in MQTT task context
     *
     * PubSubClient is not thread-safe; all subscribe/unsubscribe calls must run
     * from the same task that calls mqtt_client_.loop().
     */
    static void processDeferredSubscriptionActions();
    
    /**
     * @brief MQTT message callback
     */
    static void messageCallback(char* topic, uint8_t* payload, unsigned int length);
    
    /**
     * @brief Subscribe to all required topics
     */
    static void subscribeToTopics();
    
    /**
     * @brief Handle incoming spec_data message
     */
    static void handleSpecData(const char* json_payload, size_t length);
    
    /**
     * @brief Handle incoming spec_data_2 message
     */
    static void handleSpecData2(const char* json_payload, size_t length);
    
    /**
     * @brief Handle incoming battery_specs message
     */
    static void handleBatterySpecs(const char* json_payload, size_t length);

    /**
     * @brief Handle incoming battery_type_catalog message
     */
    static void handleBatteryTypeCatalog(const char* json_payload, size_t length);

    /**
     * @brief Handle incoming inverter_type_catalog message
     */
    static void handleInverterTypeCatalog(const char* json_payload, size_t length);
    
    /**
     * @brief Handle incoming cell_data message
     */
    static void handleCellData(const char* json_payload, size_t length);

    /**
     * @brief Handle incoming live battery telemetry message
     */
    static void handleBatteryLive(const char* json_payload, size_t length);

    /**
     * @brief Handle incoming live LED runtime state message
     */
    static void handleRuntimeLed(const char* json_payload, size_t length);

    /**
     * @brief Handle incoming live system runtime state message
     */
    static void handleRuntimeSystem(const char* json_payload, size_t length);

    /**
     * @brief Handle incoming live charger runtime state message
     */
    static void handleRuntimeCharger(const char* json_payload, size_t length);

    /**
     * @brief Handle incoming live inverter runtime state message
     */
    static void handleRuntimeInverter(const char* json_payload, size_t length);

public:
    /**
     * @brief Increment event log subscriber count (called when /events page opened)
     * 
     * When first SSE client connects to /events, automatically enables event_logs subscription.
     */
    static void incrementEventLogSubscribers();
    
    /**
     * @brief Decrement event log subscriber count (called when /events page closed)
     * 
     * When last SSE client disconnects from /events, unsubscribes from event_logs.
     */
    static void decrementEventLogSubscribers();
    
    /**
     * @brief Get current event log subscriber count
     * @return number of active SSE clients connected to /events
     */
    static int getEventLogSubscriberCount();
    static void refreshEventLogSubscription();

private:
    /**
     * @brief Handle incoming event_logs message
     */
    static void handleEventLogs(const char* json_payload, size_t length);

    /**
     * @brief Handle batt-emu/mqtt-v1/tx/state/summary/event_logs message
     */
    static void handleEventLogSummary(const char* json_payload, size_t length);

    /**
     * @brief Handle batt-emu/mqtt-v1/tx/ack/event_logs_clear message
     */
    static void handleEventLogsClearAck(const char* json_payload, size_t length);

    /**
     * @brief Handle batt-emu/mqtt-v1/tx/state/static/network retained message
     */
    static void handleStaticNetwork(const char* json_payload, size_t length);

    /**
     * @brief Handle batt-emu/mqtt-v1/tx/state/static/mqtt retained message
     */
    static void handleStaticMqtt(const char* json_payload, size_t length);

    /**
     * @brief Handle batt-emu/mqtt-v1/tx/state/static/power retained message
     */
    static void handleStaticPower(const char* json_payload, size_t length);

    /**
     * @brief Handle batt-emu/mqtt-v1/tx/state/static/led retained message
     */
    static void handleStaticLed(const char* json_payload, size_t length);

    /**
     * @brief Handle batt-emu/mqtt-v1/tx/state/static/settings retained message
     */
    static void handleStaticSettings(const char* json_payload, size_t length);

    /**
     * @brief Handle batt-emu/mqtt-v1/tx/meta/version retained message
     */
    static void handleMetaVersion(const char* json_payload, size_t length);

    /**
     * @brief Handle batt-emu/mqtt-v1/tx/meta/schema_versions retained message
     */
    static void handleMetaSchemaVersions(const char* json_payload, size_t length);

    /**
     * @brief Handle batt-emu/mqtt-v1/tx/meta/runtime retained message
     */
    static void handleMetaRuntime(const char* json_payload, size_t length);

    /**
     * @brief Handle batt-emu/mqtt-v1/tx/state/heartbeat periodic liveness message
     */
    static void handleHeartbeat(const char* json_payload, size_t length);

    struct PendingAck {
        char request_id[32];
        char topic[64];
        char payload[384];
        bool received;
        uint32_t received_ms;
    };

    static void handleAckMessage(const char* topic, const char* json_payload, size_t length);
    static bool tryConsumePendingAck(const char* request_id,
                                     const char* ack_topic,
                                     char* ack_payload,
                                     size_t ack_payload_size);
    static void storePendingAck(const char* request_id,
                                const char* topic,
                                const char* payload,
                                size_t length);
    static void ensureAckMutex();

    static SemaphoreHandle_t ack_mutex_;
    static std::vector<PendingAck> pending_acks_;
    static constexpr size_t MAX_PENDING_ACKS = 16;
};

#endif // MQTT_CLIENT_H
