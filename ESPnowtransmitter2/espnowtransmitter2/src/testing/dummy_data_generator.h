/**
 * Dummy Data Generator for Phase 1-3 Testing
 * 
 * TEMPORARY: This module generates realistic battery/charger/inverter data
 * for testing web UI and ESP-NOW communication before real hardware integration.
 * 
 * WILL BE REMOVED in Phase 4 when real control loop is integrated.
 */

#ifndef DUMMY_DATA_GENERATOR_H
#define DUMMY_DATA_GENERATOR_H

#include <Arduino.h>

namespace DummyData {
    /**
     * @brief Start the dummy data generator task
     * @param priority Task priority (should be low - Priority 1)
     * @param core CPU core to run on (Core 1 recommended)
     */
    void start(uint8_t priority = 1, uint8_t core = 1);
    
    /**
     * @brief Stop the dummy data generator task
     */
    void stop();
    
    /**
     * @brief Check if dummy data generator is running
     * @return true if running, false otherwise
     */
    bool is_running();
}

#endif // DUMMY_DATA_GENERATOR_H
