#include "runtime/nvs_bootstrap.h"

#include <nvs_flash.h>

#include "logging_config.h"

namespace RuntimeNvs {

bool ensure_initialized() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        LOG_WARN("NVS", "NVS partition needs erase; reinitializing");
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            LOG_ERROR("NVS", "nvs_flash_erase failed: %d", static_cast<int>(err));
            return false;
        }
        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        LOG_ERROR("NVS", "nvs_flash_init failed: %d", static_cast<int>(err));
        return false;
    }

    LOG_INFO("NVS", "Default NVS initialized");
    return true;
}

}  // namespace RuntimeNvs