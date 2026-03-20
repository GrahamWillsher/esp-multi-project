#pragma once
#include <esp_http_server.h>
#include <webserver_common_utils/ota_session_utils.h>

/**
 * @brief Manages HTTP server for OTA firmware updates
 * 
 * Singleton class that provides HTTP endpoint for receiving firmware
 * updates and flashing them to the device.
 */
class OtaManager {
public:
    static OtaManager& instance();
    
    /**
     * @brief Initialize and start HTTP server for OTA
     */
    void init_http_server();

    /**
     * @brief Stop HTTP server for OTA if running
     */
    void stop_http_server();

    /**
     * @brief Check if HTTP server is running
     */
    bool is_http_server_running() const { return http_server_ != nullptr; }
    
    /**
     * @brief Check if OTA update is currently in progress
     * @return true if OTA is active, false otherwise
     */
    bool is_ota_in_progress() const { return ota_in_progress_; }
    bool is_ota_ready_for_reboot() const { return ota_ready_for_reboot_; }

    /**
     * @brief Arm a fresh OTA auth session from trusted ESP-NOW control plane
     * @param requester_mac MAC of requester (receiver)
     * @return true if session armed
     */
    bool arm_ota_session_from_control_plane(const uint8_t* requester_mac);
    
private:
    OtaManager() = default;
    ~OtaManager() = default;
    
    // Prevent copying
    OtaManager(const OtaManager&) = delete;
    OtaManager& operator=(const OtaManager&) = delete;
    
    /**
     * @brief HTTP handler for OTA firmware upload
     * @param req HTTP request object
     * @return ESP_OK on success, ESP_FAIL on error
     */
    static esp_err_t ota_upload_handler(httpd_req_t *req);

    /**
     * @brief HTTP handler to arm a short-lived OTA session
     * @param req HTTP request object
     * @return ESP_OK on success
     */
    static esp_err_t ota_arm_handler(httpd_req_t *req);
    
    /**
     * @brief HTTP handler for firmware info API
     * @param req HTTP request object
     * @return ESP_OK on success
     */
    static esp_err_t firmware_info_handler(httpd_req_t *req);
    
    /**
     * @brief HTTP handler for root endpoint (health check)
     * @param req HTTP request object
     * @return ESP_OK on success
     */
    static esp_err_t root_handler(httpd_req_t *req);

    /**
     * @brief HTTP handler for consolidated runtime health status
     * @param req HTTP request object
     * @return ESP_OK on success
     */
    static esp_err_t health_handler(httpd_req_t *req);
    
    /**
     * @brief HTTP handler for event logs API
     * @param req HTTP request object
     * @return ESP_OK on success
     */
    static esp_err_t event_logs_handler(httpd_req_t *req);

    /**
     * @brief HTTP handler for OTA status API
     * @param req HTTP request object
     * @return ESP_OK on success
     */
    static esp_err_t ota_status_handler(httpd_req_t *req);
    
    /**
     * @brief HTTP handler for test data configuration GET
     * @param req HTTP request object
     * @return ESP_OK on success
     */
    static esp_err_t test_data_config_get_handler(httpd_req_t *req);
    
    /**
     * @brief HTTP handler for test data configuration POST
     * @param req HTTP request object
     * @return ESP_OK on success
     */
    static esp_err_t test_data_config_post_handler(httpd_req_t *req);
    
    /**
     * @brief HTTP handler for applying test data configuration
     * @param req HTTP request object
     * @return ESP_OK on success
     */
    static esp_err_t test_data_apply_handler(httpd_req_t *req);
    
    /**
     * @brief HTTP handler for resetting test data configuration
     * @param req HTTP request object
     * @return ESP_OK on success
     */
    static esp_err_t test_data_reset_handler(httpd_req_t *req);

    /**
     * @brief Arm a fresh OTA session token window
     * @return true if session created successfully
     */
    bool arm_ota_session();

    /**
     * @brief Validate required OTA auth headers before reading upload body
     * @param req HTTP request object
     * @return true if auth is valid and session is consumed, false otherwise
     */
    bool validate_ota_auth_headers(httpd_req_t* req);

    /**
     * @brief Update OTA commit-state telemetry.
     */
    void set_commit_state(const char* state, const char* detail = nullptr);

    httpd_handle_t http_server_{nullptr};
    volatile bool ota_in_progress_{false};
    volatile bool ota_ready_for_reboot_{false};
    bool ota_last_success_{false};
    uint32_t ota_txn_id_{0};
    uint32_t ota_state_since_ms_{0};
    uint32_t ota_last_update_ms_{0};
    char ota_commit_state_[32] = "idle";
    char ota_commit_detail_[96] = "idle";
    char ota_last_error_[96] = {0};

    OtaSessionUtils::Session ota_session_;
};
