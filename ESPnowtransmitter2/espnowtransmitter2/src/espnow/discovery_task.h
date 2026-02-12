#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/**
 * @brief Recovery state for discovery task
 */
enum class RecoveryState {
    NORMAL,
    CHANNEL_MISMATCH_DETECTED,
    RESTART_IN_PROGRESS,
    RESTART_FAILED,
    PERSISTENT_FAILURE
};

/**
 * @brief Metrics for monitoring discovery task health
 */
struct DiscoveryMetrics {
    uint32_t total_restarts = 0;
    uint32_t successful_restarts = 0;
    uint32_t failed_restarts = 0;
    uint32_t channel_mismatches = 0;
    uint32_t peer_cleanup_count = 0;
    uint32_t last_restart_timestamp = 0;
    uint32_t longest_downtime_ms = 0;
    
    void log_summary() const;
};

/**
 * @brief Manages periodic ESP-NOW announcement broadcasts for discovery
 * 
 * Singleton wrapper around common EspnowDiscovery component.
 * Provides project-specific interface while using shared implementation.
 * Industrial-grade with multi-layer reliability features.
 */
class DiscoveryTask {
public:
    static DiscoveryTask& instance();
    
    /**
     * @brief Start the periodic announcement task (LEGACY - basic announcement)
     */
    void start();
    
    /**
     * @brief Start active channel hopping (Section 11 - transmitter-active architecture)
     * Transmitter broadcasts PROBE channel-by-channel until receiver ACKs
     */
    void start_active_channel_hopping();
    
    /**
     * @brief Restart the discovery task (industrial-grade with full cleanup)
     */
    void restart();
    
    /**
     * @brief Validate current ESP-NOW state
     * @return true if state is valid, false if corruption detected
     */
    bool validate_state();
    
    /**
     * @brief Audit all ESP-NOW peer configurations
     */
    void audit_peer_state();
    
    /**
     * @brief Get metrics summary
     */
    const DiscoveryMetrics& get_metrics() const { return metrics_; }
    
    /**
     * @brief Get the task handle
     * @return Task handle (NULL if not running)
     */
    TaskHandle_t get_task_handle() const { return task_handle_; }
    
    /**
     * @brief Update recovery state machine
     */
    void update_recovery();
    
private:
    DiscoveryTask() = default;
    ~DiscoveryTask() = default;
    
    // Prevent copying
    DiscoveryTask(const DiscoveryTask&) = delete;
    DiscoveryTask& operator=(const DiscoveryTask&) = delete;
    
    // Helper methods
    void cleanup_all_peers();
    bool force_and_verify_channel(uint8_t target_channel);
    void transition_to(RecoveryState new_state);
    const char* state_to_string(RecoveryState state);
    
    // Active channel hopping (transmitter-active architecture)
    bool active_channel_hop_scan(uint8_t* discovered_channel);
    static void active_channel_hopping_task(void* parameter);
    void send_probe_on_channel(uint8_t channel);
    
    // State
    TaskHandle_t task_handle_{nullptr};
    RecoveryState recovery_state_{RecoveryState::NORMAL};
    uint32_t state_entry_time_{0};
    uint8_t restart_failure_count_{0};
    uint8_t consecutive_failures_{0};
    DiscoveryMetrics metrics_;
    
    static const uint8_t MAX_RESTART_FAILURES = 3;
};
