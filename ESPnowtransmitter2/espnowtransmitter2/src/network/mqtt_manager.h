#pragma once
#include <PubSubClient.h>
#include <WiFiClient.h>
#include <vector>
#include <cstdint>

/**
 * @brief MQTT connection state machine states
 */
enum class MqttState : uint8_t {
    DISCONNECTED,       ///< Not connected, waiting to connect
    CONNECTING,         ///< Connection attempt in progress
    CONNECTED,          ///< Fully connected and operational
    CONNECTION_FAILED,  ///< Connection attempt failed, will retry with backoff
    NETWORK_ERROR       ///< Underlying network (Ethernet) unavailable
};

/**
 * @brief MQTT connection statistics for diagnostics
 */
struct MqttStatistics {
    uint32_t total_connections{0};           ///< Total successful connections
    uint32_t failed_connections{0};          ///< Total failed connection attempts
    uint32_t total_messages_published{0};    ///< Total messages published
    uint32_t current_retry_delay_ms{0};      ///< Current retry delay (exponential backoff)
    uint32_t uptime_ms{0};                   ///< Manager uptime since init
    uint32_t time_in_current_state_ms{0};    ///< Time spent in current state
    MqttState current_state{MqttState::DISCONNECTED}; ///< Current state
};

/**
 * @brief Manages MQTT connectivity and publishing for telemetry
 * 
 * Singleton class that handles MQTT broker connection with state machine,
 * automatic reconnection with exponential backoff, and publishing of battery
 * data and status messages.
 * 
 * State Machine:
 * - DISCONNECTED → CONNECTING (when Ethernet ready)
 * - CONNECTING → CONNECTED (connection success)
 * - CONNECTING → CONNECTION_FAILED (timeout/failure)
 * - CONNECTION_FAILED → CONNECTING (retry with backoff)
 * - CONNECTED → DISCONNECTED (connection lost)
 * - Any state → NETWORK_ERROR (Ethernet down)
 */
class MqttManager {
public:
    static MqttManager& instance();
    
    /**
     * @brief Initialize MQTT client with broker configuration
     */
    void init();
    
    /**
     * @brief Update state machine (call regularly from task)
     * 
     * Handles state transitions, connection attempts, and exponential backoff.
     * Should be called every ~1 second from mqtt_task.
     */
    void update();
    
    /**
     * @brief Attempt connection via state-machine path (legacy compatibility wrapper)
     * @return true if connected successfully, false otherwise
     */
    bool connect();
    
    /**
     * @brief Check if currently connected to MQTT broker
     * @return true if in CONNECTED state, false otherwise
     */
    bool is_connected();
    
    /**
     * @brief Disconnect from MQTT broker gracefully
     * Should be called before reboot to prevent socket errors
     */
    void disconnect();
    
    /**
     * @brief Publish battery data as JSON
     * @param soc Battery state of charge (0-100%)
     * @param power Power in watts (positive = charging, negative = discharging)
     * @param timestamp Formatted timestamp string
     * @param eth_connected Ethernet connection status
     * @return true if published successfully, false otherwise
     */
    bool publish_data(int soc, long power, const char* timestamp, bool eth_connected);

    /**
     * @brief Publish minimal heartbeat on batt-emu namespace
     * @param eth_connected Ethernet connection status
     * @return true if published successfully, false otherwise
     */
    bool publish_heartbeat(bool eth_connected);
    
    /**
     * @brief Publish status message
     * @param message Status message to publish
     * @param retained Whether message should be retained by broker
     * @return true if published successfully, false otherwise
     */
    bool publish_status(const char* message, bool retained = false);
    
    /**
     * @brief Publish static configuration data (BE/spec_data topic)
     * @return true if published successfully, false otherwise
     */
    bool publish_static_specs();
    
    /**
     * @brief Publish battery specifications (BE/spec_data topic)
     * @return true if published successfully, false otherwise
     */
    bool publish_battery_specs();
    
    /**
     * @brief Publish inverter specifications (BE/spec_data_2 topic)
     * @return true if published successfully, false otherwise
     */
    bool publish_inverter_specs();

    /**
     * @brief Publish battery type catalog for receiver MQTT backup path
     * @return true if published successfully, false otherwise
     */
    bool publish_battery_type_catalog();

    /**
     * @brief Publish inverter type catalog for receiver MQTT backup path
     * @return true if published successfully, false otherwise
     */
    bool publish_inverter_type_catalog();

        /**
         * @brief Publish retained network config snapshot to batt-emu/mqtt-v1/tx/state/static/network
         * @return true if published successfully, false otherwise
         */
        bool publish_static_network();

        /**
         * @brief Publish retained MQTT config snapshot to batt-emu/mqtt-v1/tx/state/static/mqtt
         * @return true if published successfully, false otherwise
         */
        bool publish_static_mqtt();

        /**
         * @brief Publish retained power/charger config snapshot to batt-emu/mqtt-v1/tx/state/static/power
         * @return true if published successfully, false otherwise
         */
        bool publish_static_power();

        /**
         * @brief Publish retained LED config snapshot to batt-emu/mqtt-v1/tx/state/static/led
         * @return true if published successfully, false otherwise
         */
        bool publish_static_led();

        /**
         * @brief Publish retained firmware/protocol metadata to batt-emu/mqtt-v1/tx/meta/version
         * @return true if published successfully, false otherwise
         */
        bool publish_meta_version();

        /**
         * @brief Publish retained per-model version map to batt-emu/mqtt-v1/tx/meta/schema_versions
         * @return true if published successfully, false otherwise
         */
        bool publish_meta_schema_versions();

        /**
         * @brief Publish retained runtime connectivity/status metadata to batt-emu/mqtt-v1/tx/meta/runtime
         * @return true if published successfully, false otherwise
         */
        bool publish_meta_runtime();

        /**
         * @brief Publish live LED runtime state to batt-emu/mqtt-v1/tx/state/runtime/led
         * @return true if published successfully, false otherwise
         */
        bool publish_runtime_led();

        /**
         * @brief Publish live system runtime state to batt-emu/mqtt-v1/tx/state/runtime/system
         * @return true if published successfully, false otherwise
         */
        bool publish_runtime_system();

        /**
         * @brief Publish live charger runtime state to batt-emu/mqtt-v1/tx/state/runtime/charger
         * @return true if published successfully, false otherwise
         */
        bool publish_runtime_charger();

        /**
         * @brief Publish live inverter runtime state to batt-emu/mqtt-v1/tx/state/runtime/inverter
         * @return true if published successfully, false otherwise
         */
        bool publish_runtime_inverter();
    
    /**
     * @brief Publish cell voltages and balancing status (BE/cell_data topic)
     * @return true if published successfully, false otherwise
     */
    bool publish_cell_data();

    /**
     * @brief Publish event logs (transmitter/BE/event_logs topic, only changed events when subscribed)
     * @return true if published successfully, false otherwise
     */
    bool publish_event_logs();
    
    /**
     * @brief Increment event log subscriber count (called when client opens /events page)
     */
    void increment_event_log_subscribers();
    
    /**
     * @brief Decrement event log subscriber count (called when client closes /events page)
     */
    void decrement_event_log_subscribers();
    
    /**
     * @brief Get current event log subscriber count
     * @return number of active subscribers
     */
    int get_event_log_subscribers() const { return static_cast<int>(event_log_subscriptions_.size()); }
    
    /**
     * @brief Process MQTT messages (must be called regularly from task)
     */
    void loop();
    
    /**
     * @brief Get pointer to MQTT client for logger integration
     * @return Pointer to PubSubClient instance
     */
    PubSubClient* get_client() { return &client_; }
    
    /**
     * @brief Get current MQTT state
     * @return Current MqttState enum value
     */
    MqttState get_state() const { return state_; }
    
    /**
     * @brief Get connection statistics
     * @return MqttStatistics structure with current metrics
     */
    MqttStatistics get_statistics() const;
    
private:
    MqttManager();
    ~MqttManager() = default;
    
    // Prevent copying
    MqttManager(const MqttManager&) = delete;
    MqttManager& operator=(const MqttManager&) = delete;
    
    /**
     * @brief MQTT message callback for subscribed topics
     * @param topic Topic on which message was received
     * @param payload Message payload
     * @param length Payload length in bytes
     */
    static void message_callback(char* topic, byte* payload, unsigned int length);
    
    /**
     * @brief Handle OTA update command via MQTT
     * @param url Firmware URL to download from
     */
    void handle_ota_command(const char* url);

    /**
     * @brief Handle control command topic via MQTT payload JSON
     */
    void handle_control_command(const char* topic, const char* payload);

    /**
     * @brief Handle settings update command topic via MQTT payload JSON
     */
    void handle_settings_command(const char* topic, const char* payload);

    /**
     * @brief Handle network config update command topic via MQTT payload JSON
     */
    void handle_network_command(const char* topic, const char* payload);

    /**
     * @brief Handle MQTT config update command topic via MQTT payload JSON
     */
    void handle_mqtt_command(const char* topic, const char* payload);

    /**
     * @brief Handle refresh command topic via MQTT payload JSON
     */
    void handle_refresh_command(const char* topic, const char* payload);

    /**
     * @brief Handle stream control command topic via MQTT payload JSON
     */
    void handle_stream_command(const char* topic, const char* payload);

    /**
     * @brief Apply debug log level update and publish ACK
     */
    void handle_control_debug_level(const char* request_id, int level);

    /**
     * @brief Apply reboot control and publish ACK before restart
     */
    void handle_control_reboot(const char* request_id);

    /**
     * @brief Apply test-data mode control and publish ACK
     */
    void handle_control_test_data_mode(const char* request_id, int mode);

    /**
     * @brief Apply OTA session arm control and publish ACK
     */
    void handle_control_ota_start(const char* request_id);

    /**
     * @brief Publish control ACK payload to batt-emu namespace
     */
    bool publish_control_ack(const char* request_id,
                             const char* action,
                             bool success,
                             const char* code,
                             const char* message);

    bool publish_component_apply_ack(const char* request_id,
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
                                     uint32_t settings_version);

    /**
     * @brief Publish settings ACK payload to batt-emu namespace
     */
    bool publish_settings_ack(const char* request_id,
                              uint8_t category,
                              uint8_t field_id,
                              bool success,
                              uint32_t new_version,
                              const char* code,
                              const char* message);

    /**
     * @brief Publish network update ACK payload to batt-emu namespace
     */
    bool publish_network_ack(const char* request_id,
                             bool success,
                             const char* code,
                             const char* message);

    /**
     * @brief Publish MQTT config update ACK payload to batt-emu namespace
     */
    bool publish_mqtt_ack(const char* request_id,
                          bool success,
                          const char* code,
                          const char* message);

    /**
     * @brief Publish event logs clear ACK payload to batt-emu namespace
     */
    bool publish_event_logs_clear_ack(const char* request_id,
                                      bool success,
                                      const char* code,
                                      const char* message);

    /**
     * @brief Republish a cached ACK when a duplicate request_id is received
     * @return true when request_id was a known duplicate and handled
     */
    bool publish_cached_ack_if_duplicate(const char* request_id);

    /**
     * @brief Cache last ACK payload for request_id duplicate handling
     */
    void remember_ack_payload(const char* request_id,
                              const char* topic,
                              const char* payload);
    
    /**
     * @brief Attempt actual MQTT connection (internal)
     */
    void attempt_connection();
    
    /**
     * @brief Handle successful connection
     */
    void on_connection_success();
    
    /**
     * @brief Handle connection failure
     */
    void on_connection_failed();
    
    /**
     * @brief Handle network unavailable
     */
    void on_network_error();
    
    /**
     * @brief Transition to new state
     * @param new_state Target state
     */
    void transition_to(MqttState new_state);

    /**
     * @brief Ensure reusable MQTT publish buffer exists with at least required bytes
     * @param required_bytes Minimum required payload buffer size
     * @return true if buffer ready, false on allocation failure
     */
    bool ensure_publish_buffer(size_t required_bytes);
    
    WiFiClient eth_client_;
    PubSubClient client_;
    
    // State machine
    MqttState state_{MqttState::DISCONNECTED};
    uint32_t state_enter_time_{0};
    uint32_t last_connection_attempt_{0};
    uint32_t initialization_time_{0};

    static constexpr uint32_t INITIAL_RETRY_DELAY_MS = 5000;
    static constexpr uint32_t MAX_RETRY_DELAY_MS = 300000;  // 5 minutes max
    static constexpr float RETRY_BACKOFF_MULTIPLIER = 1.5f;
    static constexpr uint32_t CONNECTION_TIMEOUT_MS = 10000;  // 10 second timeout
    static constexpr size_t PAYLOAD_BUFFER_SIZE = 384;
    
    // Exponential backoff
    uint32_t current_retry_delay_{INITIAL_RETRY_DELAY_MS};  // Start at 5 seconds
    
    // Statistics
    uint32_t total_connections_{0};
    uint32_t failed_connections_{0};
    uint32_t total_messages_published_{0};
    
    // Legacy connection flag (for backwards compatibility)
    volatile bool connected_{false};
    
    char payload_buffer_[PAYLOAD_BUFFER_SIZE];
    char* publish_buffer_{nullptr};
    size_t publish_buffer_capacity_{0};

    struct EventLogSubscription {
        uint32_t id;
        uint64_t created_ms;
        uint64_t last_activity_ms;
    };

    std::vector<EventLogSubscription> event_log_subscriptions_;
    uint32_t next_event_log_subscription_id_{1};
    uint32_t event_log_ttl_reap_count_{0};

    struct AckCacheEntry {
        char request_id[48];
        char topic[64];
        char payload[PAYLOAD_BUFFER_SIZE];
        uint32_t ts_ms;
    };

    std::vector<AckCacheEntry> ack_cache_;
    static constexpr size_t MAX_ACK_CACHE_ENTRIES = 128;

    // Snapshot/session state for event-log publishing
    uint64_t event_snapshot_id_{0};
    size_t event_snapshot_offset_{0};
    std::vector<int> event_snapshot_order_;

    void reap_expired_event_log_subscriptions(uint64_t now_ms);
};
