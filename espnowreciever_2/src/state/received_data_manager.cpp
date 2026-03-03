#include "received_data_manager.h"
#include <logging.h>

// Static member initialization
uint8_t ReceivedDataManager::soc_ = 0;
int32_t ReceivedDataManager::power_w_ = 0;
uint32_t ReceivedDataManager::voltage_mv_ = 0;
bool ReceivedDataManager::data_received_ = false;
bool ReceivedDataManager::soc_changed_ = false;
bool ReceivedDataManager::power_changed_ = false;
SemaphoreHandle_t ReceivedDataManager::data_mutex_ = nullptr;

void ReceivedDataManager::init() {
    if (data_mutex_ != nullptr) {
        LOG_WARN("RCV_DATA", "ReceivedDataManager already initialized");
        return;
    }
    
    data_mutex_ = xSemaphoreCreateMutex();
    if (data_mutex_ == nullptr) {
        LOG_ERROR("RCV_DATA", "Failed to create data mutex");
        return;
    }
    
    LOG_INFO("RCV_DATA", "ReceivedDataManager initialized");
}

uint8_t ReceivedDataManager::get_soc() {
    if (!data_mutex_) {
        LOG_ERROR("RCV_DATA", "ReceivedDataManager not initialized");
        return 0;
    }
    
    if (xSemaphoreTake(data_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_ERROR("RCV_DATA", "Failed to acquire data lock for SOC read");
        return soc_;
    }
    
    uint8_t soc = soc_;
    xSemaphoreGive(data_mutex_);
    return soc;
}

bool ReceivedDataManager::set_soc(uint8_t soc) {
    if (!data_mutex_) {
        LOG_ERROR("RCV_DATA", "ReceivedDataManager not initialized");
        return false;
    }
    
    if (xSemaphoreTake(data_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_ERROR("RCV_DATA", "Failed to acquire data lock for SOC write");
        return false;
    }
    
    bool changed = (soc_ != soc);
    if (changed) {
        soc_ = soc;
        soc_changed_ = true;
        LOG_DEBUG("RCV_DATA", "SOC updated to: %d%%", soc);
    }
    
    xSemaphoreGive(data_mutex_);
    return changed;
}

int32_t ReceivedDataManager::get_power() {
    if (!data_mutex_) {
        LOG_ERROR("RCV_DATA", "ReceivedDataManager not initialized");
        return 0;
    }
    
    if (xSemaphoreTake(data_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_ERROR("RCV_DATA", "Failed to acquire data lock for power read");
        return power_w_;
    }
    
    int32_t power = power_w_;
    xSemaphoreGive(data_mutex_);
    return power;
}

bool ReceivedDataManager::set_power(int32_t power_w) {
    if (!data_mutex_) {
        LOG_ERROR("RCV_DATA", "ReceivedDataManager not initialized");
        return false;
    }
    
    if (xSemaphoreTake(data_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_ERROR("RCV_DATA", "Failed to acquire data lock for power write");
        return false;
    }
    
    bool changed = (power_w_ != power_w);
    if (changed) {
        power_w_ = power_w;
        power_changed_ = true;
        LOG_DEBUG("RCV_DATA", "Power updated to: %ld W", power_w);
    }
    
    xSemaphoreGive(data_mutex_);
    return changed;
}

uint32_t ReceivedDataManager::get_voltage_mv() {
    if (!data_mutex_) {
        LOG_ERROR("RCV_DATA", "ReceivedDataManager not initialized");
        return 0;
    }
    
    if (xSemaphoreTake(data_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_ERROR("RCV_DATA", "Failed to acquire data lock for voltage read");
        return voltage_mv_;
    }
    
    uint32_t voltage = voltage_mv_;
    xSemaphoreGive(data_mutex_);
    return voltage;
}

bool ReceivedDataManager::set_voltage_mv(uint32_t voltage_mv) {
    if (!data_mutex_) {
        LOG_ERROR("RCV_DATA", "ReceivedDataManager not initialized");
        return false;
    }
    
    if (xSemaphoreTake(data_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_ERROR("RCV_DATA", "Failed to acquire data lock for voltage write");
        return false;
    }
    
    bool changed = (voltage_mv_ != voltage_mv);
    if (changed) {
        voltage_mv_ = voltage_mv;
        LOG_DEBUG("RCV_DATA", "Voltage updated to: %lu mV", voltage_mv);
    }
    
    xSemaphoreGive(data_mutex_);
    return changed;
}

bool ReceivedDataManager::is_data_received() {
    if (!data_mutex_) {
        LOG_ERROR("RCV_DATA", "ReceivedDataManager not initialized");
        return false;
    }
    
    if (xSemaphoreTake(data_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_ERROR("RCV_DATA", "Failed to acquire data lock for data_received read");
        return data_received_;
    }
    
    bool received = data_received_;
    xSemaphoreGive(data_mutex_);
    return received;
}

bool ReceivedDataManager::set_data_received(bool received) {
    if (!data_mutex_) {
        LOG_ERROR("RCV_DATA", "ReceivedDataManager not initialized");
        return false;
    }
    
    if (xSemaphoreTake(data_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_ERROR("RCV_DATA", "Failed to acquire data lock for data_received write");
        return false;
    }
    
    bool changed = (data_received_ != received);
    if (changed) {
        data_received_ = received;
        LOG_DEBUG("RCV_DATA", "Data received flag set to: %s", received ? "true" : "false");
    }
    
    xSemaphoreGive(data_mutex_);
    return changed;
}

void ReceivedDataManager::clear_data_received() {
    if (!data_mutex_) {
        LOG_ERROR("RCV_DATA", "ReceivedDataManager not initialized");
        return;
    }
    
    if (xSemaphoreTake(data_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_ERROR("RCV_DATA", "Failed to acquire data lock for data_received clear");
        return;
    }
    
    if (data_received_) {
        data_received_ = false;
        LOG_DEBUG("RCV_DATA", "Data received flag cleared");
    }
    
    xSemaphoreGive(data_mutex_);
}

bool ReceivedDataManager::soc_changed() {
    if (!data_mutex_) {
        LOG_ERROR("RCV_DATA", "ReceivedDataManager not initialized");
        return false;
    }
    
    if (xSemaphoreTake(data_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_ERROR("RCV_DATA", "Failed to acquire data lock for soc_changed check");
        return soc_changed_;
    }
    
    bool changed = soc_changed_;
    xSemaphoreGive(data_mutex_);
    return changed;
}

void ReceivedDataManager::clear_soc_changed() {
    if (!data_mutex_) {
        LOG_ERROR("RCV_DATA", "ReceivedDataManager not initialized");
        return;
    }
    
    if (xSemaphoreTake(data_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_ERROR("RCV_DATA", "Failed to acquire data lock for soc_changed clear");
        return;
    }
    
    soc_changed_ = false;
    xSemaphoreGive(data_mutex_);
}

bool ReceivedDataManager::power_changed() {
    if (!data_mutex_) {
        LOG_ERROR("RCV_DATA", "ReceivedDataManager not initialized");
        return false;
    }
    
    if (xSemaphoreTake(data_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_ERROR("RCV_DATA", "Failed to acquire data lock for power_changed check");
        return power_changed_;
    }
    
    bool changed = power_changed_;
    xSemaphoreGive(data_mutex_);
    return changed;
}

void ReceivedDataManager::clear_power_changed() {
    if (!data_mutex_) {
        LOG_ERROR("RCV_DATA", "ReceivedDataManager not initialized");
        return;
    }
    
    if (xSemaphoreTake(data_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_ERROR("RCV_DATA", "Failed to acquire data lock for power_changed clear");
        return;
    }
    
    power_changed_ = false;
    xSemaphoreGive(data_mutex_);
}

void ReceivedDataManager::get_all_data(uint8_t& out_soc, int32_t& out_power, 
                                        uint32_t& out_voltage_mv, bool& out_data_received) {
    if (!data_mutex_) {
        LOG_ERROR("RCV_DATA", "ReceivedDataManager not initialized");
        return;
    }
    
    if (xSemaphoreTake(data_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_ERROR("RCV_DATA", "Failed to acquire data lock for snapshot");
        return;
    }
    
    out_soc = soc_;
    out_power = power_w_;
    out_voltage_mv = voltage_mv_;
    out_data_received = data_received_;
    
    xSemaphoreGive(data_mutex_);
}

bool ReceivedDataManager::lock(uint32_t timeout_ms) {
    if (!data_mutex_) {
        LOG_ERROR("RCV_DATA", "ReceivedDataManager not initialized");
        return false;
    }
    
    return xSemaphoreTake(data_mutex_, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void ReceivedDataManager::unlock() {
    if (data_mutex_) {
        xSemaphoreGive(data_mutex_);
    }
}
