#ifndef RECEIVER_NETWORK_CONFIG_H
#define RECEIVER_NETWORK_CONFIG_H

#include <Arduino.h>
#include <Preferences.h>

/**
 * @brief Manages Receiver Network Configuration Storage
 *
 * This class handles receiver network settings including:
 * - WiFi credentials (SSID, password)
 * - Network mode (DHCP vs Static IP)
 * - Static IP configuration (IP, gateway, subnet, DNS)
 * - Hostname for mDNS
 *
 * Configuration is persisted in NVS using uint8_t[4] arrays for IP addresses.
 */
class ReceiverNetworkConfig {
public:
    enum class PowerBarRendererMode : uint8_t {
        Original = 0,
        Soft = 1,
        Linear = 2,
        Hybrid = 3,
        OriginalRounded = 4,
    };

private:
    // WiFi credentials
    static char hostname_[32];
    static char ssid_[32];
    static char password_[64];

    // Network mode
    static bool use_static_ip_;

    // Static IP configuration (stored as arrays, not IPAddress objects)
    static uint8_t static_ip_[4];
    static uint8_t gateway_[4];
    static uint8_t subnet_[4];
    static uint8_t dns_primary_[4];
    static uint8_t dns_secondary_[4];

    // MQTT configuration
    static bool mqtt_enabled_;
    static uint8_t mqtt_server_[4];
    static uint16_t mqtt_port_;
    static char mqtt_username_[32];
    static char mqtt_password_[64];

    // Battery and Inverter type selection
    static uint8_t battery_type_;
    static uint8_t inverter_type_;

    // Battery and Inverter interface selection
    static uint8_t battery_interface_;
    static uint8_t inverter_interface_;

    // Simulation mode (dashboard data source)
    static bool simulation_mode_;

    // Display power bar renderer mode
    static uint8_t power_bar_renderer_mode_;

    static constexpr const char* NVS_NAMESPACE = "rx_net_cfg";
    static constexpr const char* NVS_KEY_HOSTNAME = "hostname";
    static constexpr const char* NVS_KEY_SSID = "ssid";
    static constexpr const char* NVS_KEY_PASSWORD = "password";
    static constexpr const char* NVS_KEY_USE_STATIC = "use_static";
    static constexpr const char* NVS_KEY_IP = "ip";
    static constexpr const char* NVS_KEY_GATEWAY = "gateway";
    static constexpr const char* NVS_KEY_SUBNET = "subnet";
    static constexpr const char* NVS_KEY_DNS_PRIMARY = "dns_primary";
    static constexpr const char* NVS_KEY_DNS_SECONDARY = "dns_secondary";
    static constexpr const char* NVS_KEY_MQTT_ENABLED = "mqtt_en";
    static constexpr const char* NVS_KEY_MQTT_SERVER = "mqtt_srv";
    static constexpr const char* NVS_KEY_MQTT_PORT = "mqtt_port";
    static constexpr const char* NVS_KEY_MQTT_USERNAME = "mqtt_user";
    static constexpr const char* NVS_KEY_MQTT_PASSWORD = "mqtt_pass";
    static constexpr const char* NVS_KEY_BATTERY_TYPE = "batt_type";
    static constexpr const char* NVS_KEY_INVERTER_TYPE = "inv_type";
    static constexpr const char* NVS_KEY_BATTERY_INTERFACE = "batt_if";
    static constexpr const char* NVS_KEY_INVERTER_INTERFACE = "inv_if";
    static constexpr const char* NVS_KEY_SIMULATION_MODE = "sim_mode";
    static constexpr const char* NVS_KEY_POWER_BAR_MODE = "pwrbar_mode";

public:
    struct ValidationResult {
        bool valid;
        const char* error_message;
    };

    static ValidationResult validateIPAddress(const uint8_t ip[4]);
    static ValidationResult validatePort(uint16_t port);
    static ValidationResult validateSSID(const char* ssid);
    static ValidationResult validatePassword(const char* password);
    static ValidationResult validateHostname(const char* hostname);
    static ValidationResult validateInterface(uint8_t interface);
    static ValidationResult validatePowerBarRendererMode(uint8_t mode);

    static bool loadConfig();

    static bool saveConfig(
        const char* hostname,
        const char* ssid,
        const char* password,
        bool use_static_ip,
        const uint8_t static_ip[4],
        const uint8_t gateway[4],
        const uint8_t subnet[4],
        const uint8_t dns_primary[4],
        const uint8_t dns_secondary[4],
        bool mqtt_enabled = false,
        const uint8_t mqtt_server[4] = nullptr,
        uint16_t mqtt_port = 1883,
        const char* mqtt_username = "",
        const char* mqtt_password = ""
    );

    static bool hasValidConfig();
    static void clearConfig();

    static const char* getHostname() { return hostname_; }
    static const char* getSSID() { return ssid_; }
    static const char* getPassword() { return password_; }
    static bool useStaticIP() { return use_static_ip_; }
    static const uint8_t* getStaticIP() { return static_ip_; }
    static const uint8_t* getGateway() { return gateway_; }
    static const uint8_t* getSubnet() { return subnet_; }
    static const uint8_t* getDNSPrimary() { return dns_primary_; }
    static const uint8_t* getDNSSecondary() { return dns_secondary_; }

    static bool isMqttEnabled() { return mqtt_enabled_; }
    static const uint8_t* getMqttServer() { return mqtt_server_; }
    static uint16_t getMqttPort() { return mqtt_port_; }
    static const char* getMqttUsername() { return mqtt_username_; }
    static const char* getMqttPassword() { return mqtt_password_; }

    static uint8_t getBatteryType() { return battery_type_; }
    static uint8_t getInverterType() { return inverter_type_; }

    static uint8_t getBatteryInterface() { return battery_interface_; }
    static uint8_t getInverterInterface() { return inverter_interface_; }

    static bool isSimulationMode() { return simulation_mode_; }

    static PowerBarRendererMode getPowerBarRendererMode() {
        return static_cast<PowerBarRendererMode>(power_bar_renderer_mode_);
    }

    static void setBatteryType(uint8_t type);
    static void setInverterType(uint8_t type);

    static void setBatteryInterface(uint8_t interface);
    static void setInverterInterface(uint8_t interface);

    static void setSimulationMode(bool enabled);
    static void setPowerBarRendererMode(PowerBarRendererMode mode);
};

#endif // RECEIVER_NETWORK_CONFIG_H
