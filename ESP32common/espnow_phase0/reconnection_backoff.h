#pragma once
#include <cstdint>

/**
 * @brief Exponential backoff with jitter for connection retries
 * 
 * Prevents "thundering herd" when both devices restart simultaneously.
 * - Initial delay: 500ms
 * - Max delay: 30 seconds
 * - Multiplier: 1.5x per retry
 * - Jitter: ±20% to prevent synchronized retries
 * 
 * Example backoff sequence:
 * Attempt 1: 500ms + jitter
 * Attempt 2: 750ms + jitter
 * Attempt 3: 1125ms + jitter
 * Attempt 4: 1687ms + jitter
 * ... up to max 30 seconds
 */
class ReconnectionBackoff {
public:
    /**
     * @brief Initialize backoff manager
     */
    ReconnectionBackoff() : 
        current_delay_ms_(INITIAL_DELAY_MS),
        retry_count_(0),
        last_attempt_time_(0),
        total_backoff_time_ms_(0) {}
    
    /**
     * @brief Get the delay to apply before next retry
     * @return Milliseconds to wait before attempting reconnection
     */
    uint32_t get_next_delay_ms();
    
    /**
     * @brief Call when a retry attempt is made
     * Increments delay using exponential backoff
     */
    void on_retry_attempt();
    
    /**
     * @brief Call when connection succeeds
     * Resets backoff to initial state
     */
    void on_connection_success();
    
    /**
     * @brief Check if enough time has passed to attempt reconnection
     * @return true if should attempt now, false if still waiting
     */
    bool should_attempt_now() const;
    
    /**
     * @brief Get current retry count
     * @return Number of retry attempts made
     */
    uint32_t get_retry_count() const { return retry_count_; }
    
    /**
     * @brief Get current backoff delay
     * @return Current delay in milliseconds
     */
    uint32_t get_current_delay_ms() const { return current_delay_ms_; }
    
    /**
     * @brief Get total backoff time accumulated
     * @return Total milliseconds spent in backoff
     */
    uint32_t get_total_backoff_time_ms() const { return total_backoff_time_ms_; }
    
    /**
     * @brief Reset backoff to initial state
     */
    void reset();
    
private:
    // Configuration constants
    static constexpr uint32_t INITIAL_DELAY_MS = 500;
    static constexpr uint32_t MAX_DELAY_MS = 30000;
    static constexpr float BACKOFF_MULTIPLIER = 1.5f;
    static constexpr float JITTER_FACTOR = 0.2f;  // ±20% jitter
    
    // State
    uint32_t current_delay_ms_;
    uint32_t retry_count_;
    uint32_t last_attempt_time_;
    uint32_t total_backoff_time_ms_;
    
    /**
     * @brief Add random jitter to delay
     * @param base_delay Base delay in milliseconds
     * @return Delay with ±20% jitter applied
     */
    uint32_t add_jitter(uint32_t base_delay);
};
