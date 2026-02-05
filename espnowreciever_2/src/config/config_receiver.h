#pragma once

#include <config_sync/config_structures.h>
#include <config_sync/config_manager.h>
#include <espnow_common.h>

/**
 * @brief Receiver configuration manager
 * 
 * Manages configuration received from transmitter.
 * Requests full snapshots and handles delta updates.
 */
class ReceiverConfigManager {
public:
    static ReceiverConfigManager& instance();
    
    // Request full snapshot from transmitter
    void requestFullSnapshot(const uint8_t* transmitter_mac);
    
    // Handle incoming messages
    void onSnapshotReceived(const uint8_t* mac, const uint8_t* data, size_t len);
    void onDeltaUpdateReceived(const uint8_t* mac, const uint8_t* data, size_t len);
    void onResyncRequested();
    
    // Get current configuration
    const FullConfigSnapshot& getCurrentConfig() const;
    
    // Check if config is available
    bool isConfigAvailable() const { return config_received_; }
    
    // Get specific section
    const MqttConfig& getMqttConfig() const;
    const NetworkConfig& getNetworkConfig() const;
    const BatteryConfig& getBatteryConfig() const;
    const PowerConfig& getPowerConfig() const;
    const InverterConfig& getInverterConfig() const;
    const CanConfig& getCanConfig() const;
    const ContactorConfig& getContactorConfig() const;
    const SystemConfig& getSystemConfig() const;
    
    // Get version info
    uint16_t getGlobalVersion() const;
    uint32_t getTimestamp() const { return config_timestamp_; }
    
private:
    ReceiverConfigManager() = default;
    
    ConfigManager config_manager_;
    bool config_received_ = false;
    uint32_t last_request_id_ = 0;
    uint8_t transmitter_mac_[6] = {0};  // Store transmitter MAC for ACKs
    uint32_t config_timestamp_ = 0;     // When config was last updated
    
    // Fragment reassembly
    struct FragmentBuffer {
        uint32_t seq;
        uint16_t total_fragments;
        uint16_t received_fragments;
        uint8_t* data;
        size_t total_size;
        bool* fragment_received;
        unsigned long last_fragment_time;
        
        FragmentBuffer() : seq(0), total_fragments(0), received_fragments(0),
                          data(nullptr), total_size(0), fragment_received(nullptr),
                          last_fragment_time(0) {}
        
        ~FragmentBuffer() {
            if (data) delete[] data;
            if (fragment_received) delete[] fragment_received;
        }
    };
    
    FragmentBuffer* fragment_buffer_ = nullptr;
    
    void sendAck(uint16_t version, ConfigSection section, bool success);
    void applyDeltaUpdate(const config_delta_update_t* update);
    bool validateChecksum(const FullConfigSnapshot* config);
    void processFragment(const espnow_packet_t* pkt);
    void clearFragmentBuffer();
};
