#pragma once
#include <PubSubClient.h>
#include <WiFiClient.h>

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
     * @brief Attempt connection to MQTT broker (DEPRECATED - use update() instead)
     * @return true if connected successfully, false otherwise
     * @deprecated Use update() for state machine management
     */
    bool connect();
    
    /**
     * @brief Check if currently connected to MQTT broker
     * @return true if in CONNECTED state, false otherwise
     */
    bool is_connected() const { return state_ == MqttState::CONNECTED; }
    
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
    int get_event_log_subscribers() const { return event_log_subscribers_; }
    
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
    
    WiFiClient eth_client_;
    PubSubClient client_;
    
    // State machine
    MqttState state_{MqttState::DISCONNECTED};
    uint32_t state_enter_time_{0};
    uint32_t last_connection_attempt_{0};
    uint32_t initialization_time_{0};
    
    // Exponential backoff
    uint32_t current_retry_delay_{5000};  // Start at 5 seconds
    static constexpr uint32_t INITIAL_RETRY_DELAY_MS = 5000;
    static constexpr uint32_t MAX_RETRY_DELAY_MS = 300000;  // 5 minutes max
    static constexpr float RETRY_BACKOFF_MULTIPLIER = 1.5f;
    static constexpr uint32_t CONNECTION_TIMEOUT_MS = 10000;  // 10 second timeout
    
    // Statistics
    uint32_t total_connections_{0};
    uint32_t failed_connections_{0};
    uint32_t total_messages_published_{0};
    
    // Legacy connection flag (for backwards compatibility)
    volatile bool connected_{false};
    
    char payload_buffer_[384];
    int event_log_subscribers_{0};  // Track number of clients subscribing to event logs
};
