#pragma once

namespace hardware {
    // Ethernet PHY configuration for Olimex ESP32-POE-ISO (WROVER variant)
    constexpr int PHY_ADDR = 0;
    constexpr int ETH_POWER_PIN = 12;
    constexpr int ETH_MDC_PIN = 23;
    constexpr int ETH_MDIO_PIN = 18;
    
    // ETH_TYPE and ETH_CLK_MODE are defined as enums, so we use them directly
    // #define ETH_TYPE          ETH_PHY_LAN8720
    // #define ETH_CLK_MODE      ETH_CLOCK_GPIO0_OUT  // CRITICAL for WROVER boards!
    
} // namespace hardware
