#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <espnow_common.h>
#include <espnow_standard_handlers.h>

/**
 * @brief Handles incoming ESP-NOW messages and routes them appropriately
 * 
 * Singleton class that manages the RX task for processing ESP-NOW messages
 * including discovery, data requests, and control commands.
 * 
 * Uses common message router and standard handlers for PROBE/ACK,
 * with custom handlers for application-specific messages.
 */
class EspnowMessageHandler {
public:
    static EspnowMessageHandler& instance();
    
    /**
     * @brief Start the ESP-NOW RX task
     * @param queue ESP-NOW receive queue (from library)
     */
    void start_rx_task(QueueHandle_t queue);
    
    /**
     * @brief Check if receiver is currently connected
     * @return true if receiver peer is registered and active
     */
    bool is_receiver_connected() const { return receiver_connected_; }
    
    /**
     * @brief Check if data transmission is currently active
     * @return true if receiver requested data transmission
     */
    bool is_transmission_active() const { return transmission_active_; }
    
    /**
     * @brief Load saved debug level from NVS
     * @return Saved debug level, or default if not found
     */
    uint8_t load_debug_level();
    
private:
    EspnowMessageHandler();
    ~EspnowMessageHandler() = default;
    
    // Prevent copying
    EspnowMessageHandler(const EspnowMessageHandler&) = delete;
    EspnowMessageHandler& operator=(const EspnowMessageHandler&) = delete;
    
    /**
     * @brief Setup message routes in the router
     */
    void setup_message_routes();
    
    /**
     * @brief RX task implementation
     * @param parameter Task parameter (queue handle)
     */
    static void rx_task_impl(void* parameter);
    
    /**
     * @brief Handle REQUEST_DATA message (start transmission)
     * @param msg ESP-NOW message
     */
    void handle_request_data(const espnow_queue_msg_t& msg);
    
    /**
     * @brief Handle ABORT_DATA message (stop transmission)
     * @param msg ESP-NOW message
     */
    void handle_abort_data(const espnow_queue_msg_t& msg);
    
    /**
     * @brief Handle REBOOT message
     * @param msg ESP-NOW message
     */
    void handle_reboot(const espnow_queue_msg_t& msg);
    
    /**
     * @brief Handle OTA_START message
     * @param msg ESP-NOW message
     */
    void handle_ota_start(const espnow_queue_msg_t& msg);
    
    /**
     * @brief Handle DEBUG_CONTROL message (change log level)
     * @param msg ESP-NOW message
     */
    void handle_debug_control(const espnow_queue_msg_t& msg);
    
    /**
     * @brief Handle CONFIG_REQUEST_FULL message (request configuration snapshot)
     * @param msg ESP-NOW message
     */
    void handle_config_request_full(const espnow_queue_msg_t& msg);
    
    /**
     * @brief Send DEBUG_ACK message back to receiver
     * @param applied_level Level that was applied
     * @param previous_level Previous level before change
     * @param status 0=success, 1=invalid, 2=error
     */
    void send_debug_ack(uint8_t applied_level, uint8_t previous_level, uint8_t status);
    
    /**
     * @brief Save debug level to NVS for persistence
     * @param level Debug level to save
     */
    void save_debug_level(uint8_t level);
    
    // Handler configurations for standard messages
    EspnowStandardHandlers::ProbeHandlerConfig probe_config_;
    EspnowStandardHandlers::AckHandlerConfig ack_config_;
    
    volatile bool receiver_connected_{false};
    volatile bool transmission_active_{false};
    uint8_t receiver_mac_[6]{0};  // Store receiver MAC for sending ACKs
};
