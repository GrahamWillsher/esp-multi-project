#pragma once
#include <esp_http_server.h>

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
     * @brief Check if OTA update is currently in progress
     * @return true if OTA is active, false otherwise
     */
    bool is_ota_in_progress() const { return ota_in_progress_; }
    
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
     * @brief HTTP handler for event logs API
     * @param req HTTP request object
     * @return ESP_OK on success
     */
    static esp_err_t event_logs_handler(httpd_req_t *req);
    
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
    
    httpd_handle_t http_server_{nullptr};
    volatile bool ota_in_progress_{false};
};
