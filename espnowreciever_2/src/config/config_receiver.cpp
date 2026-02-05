#include "config_receiver.h"
#include "../common.h"
#include <esp_now.h>
#include <espnow_common.h>

ReceiverConfigManager& ReceiverConfigManager::instance() {
    static ReceiverConfigManager instance;
    return instance;
}

void ReceiverConfigManager::requestFullSnapshot(const uint8_t* transmitter_mac) {
    if (!transmitter_mac) {
        LOG_WARN("CONFIG: Cannot request snapshot - invalid MAC");
        return;
    }
    
    config_request_full_t request;
    request.type = msg_config_request_full;
    request.request_id = ++last_request_id_;
    
    esp_err_t result = esp_now_send(transmitter_mac, 
                                    (uint8_t*)&request, 
                                    sizeof(request));
    
    if (result == ESP_OK) {
        LOG_INFO("CONFIG: Requested full snapshot (ID=%u)", request.request_id);
    } else {
        LOG_ERROR("CONFIG: Failed to request snapshot: %s", esp_err_to_name(result));
    }
}

void ReceiverConfigManager::onSnapshotReceived(const uint8_t* mac, const uint8_t* data, size_t len) {
    // Snapshot comes as fragmented packets
    // Minimum size is header without payload (type + subtype + seq + frag_index + frag_total + payload_len + checksum)
    const size_t min_packet_size = sizeof(uint8_t) + sizeof(uint8_t) + sizeof(uint32_t) + 
                                   sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint16_t);
    
    if (len < min_packet_size) {
        LOG_ERROR("CONFIG: Invalid packet size (%u bytes, minimum %u)", len, min_packet_size);
        return;
    }
    
    const espnow_packet_t* pkt = (const espnow_packet_t*)data;
    
    if (pkt->type != msg_config_snapshot) {
        LOG_ERROR("CONFIG: Wrong packet type");
        return;
    }
    
    // Store transmitter MAC for ACKs
    memcpy(transmitter_mac_, mac, 6);
    
    processFragment(pkt);
}

void ReceiverConfigManager::processFragment(const espnow_packet_t* pkt) {
    // Initialize fragment buffer if this is first fragment
    if (pkt->frag_index == 0) {
        clearFragmentBuffer();
        
        fragment_buffer_ = new FragmentBuffer();
        fragment_buffer_->seq = pkt->seq;
        fragment_buffer_->total_fragments = pkt->frag_total;
        fragment_buffer_->total_size = sizeof(FullConfigSnapshot);
        fragment_buffer_->data = new uint8_t[fragment_buffer_->total_size];
        fragment_buffer_->fragment_received = new bool[pkt->frag_total]();
        fragment_buffer_->received_fragments = 0;
        fragment_buffer_->last_fragment_time = millis();
        
        LOG_INFO("CONFIG: Starting snapshot reassembly (%u fragments, %u bytes)",
                pkt->frag_total, fragment_buffer_->total_size);
    }
    
    if (!fragment_buffer_) {
        LOG_ERROR("CONFIG: Fragment buffer not initialized");
        return;
    }
    
    if (pkt->seq != fragment_buffer_->seq) {
        LOG_ERROR("CONFIG: Sequence mismatch (expected %u, got %u)",
                 fragment_buffer_->seq, pkt->seq);
        return;
    }
    
    if (pkt->frag_index >= fragment_buffer_->total_fragments) {
        LOG_ERROR("CONFIG: Invalid fragment index %u", pkt->frag_index);
        return;
    }
    
    // Check if already received
    if (fragment_buffer_->fragment_received[pkt->frag_index]) {
        LOG_DEBUG("CONFIG: Duplicate fragment %u - ignoring", pkt->frag_index);
        return;
    }
    
    // Copy fragment data
    size_t offset = pkt->frag_index * 230; // PAYLOAD_SIZE
    size_t copy_len = pkt->payload_len;
    
    if (offset + copy_len > fragment_buffer_->total_size) {
        LOG_ERROR("CONFIG: Fragment overflow");
        return;
    }
    
    memcpy(fragment_buffer_->data + offset, pkt->payload, copy_len);
    fragment_buffer_->fragment_received[pkt->frag_index] = true;
    fragment_buffer_->received_fragments++;
    fragment_buffer_->last_fragment_time = millis();
    
    LOG_DEBUG("CONFIG: Fragment %u/%u received (%u bytes)",
             pkt->frag_index + 1, fragment_buffer_->total_fragments, copy_len);
    
    // Check if complete
    if (fragment_buffer_->received_fragments == fragment_buffer_->total_fragments) {
        LOG_INFO("CONFIG: All fragments received - reassembling");
        
        // Cast to FullConfigSnapshot
        FullConfigSnapshot* snapshot = (FullConfigSnapshot*)fragment_buffer_->data;
        
        // Validate checksum
        if (!validateChecksum(snapshot)) {
            LOG_ERROR("CONFIG: Checksum validation failed!");
            sendAck(snapshot->version.global_version, CONFIG_SYSTEM, false);
            clearFragmentBuffer();
            return;
        }
        
        // Store configuration
        config_manager_.setFullConfig(*snapshot);
        config_received_ = true;
        config_timestamp_ = millis() / 1000;  // Store in seconds
        
        // Send ACK
        sendAck(snapshot->version.global_version, CONFIG_SYSTEM, true);
        
        LOG_INFO("CONFIG: Snapshot stored (version %u)", 
                snapshot->version.global_version);
        LOG_INFO("CONFIG: MQTT: %s:%d (enabled=%d)",
                snapshot->mqtt.server, snapshot->mqtt.port, snapshot->mqtt.enabled);
        
        clearFragmentBuffer();
    }
}

void ReceiverConfigManager::clearFragmentBuffer() {
    if (fragment_buffer_) {
        delete fragment_buffer_;
        fragment_buffer_ = nullptr;
    }
}

void ReceiverConfigManager::onDeltaUpdateReceived(const uint8_t* mac, const uint8_t* data, size_t len) {
    if (len < sizeof(config_delta_update_t)) {
        LOG_ERROR("CONFIG: Invalid delta update size");
        return;
    }
    
    const config_delta_update_t* update = (const config_delta_update_t*)data;
    
    if (!config_received_) {
        LOG_WARN("CONFIG: Delta update received but no base config - requesting snapshot");
        requestFullSnapshot(mac);
        return;
    }
    
    // Store transmitter MAC for ACKs
    memcpy(transmitter_mac_, mac, 6);
    
    LOG_INFO("CONFIG: Delta update (section=%d, field=%d, version=%u)",
             update->section, update->field_id, update->global_version);
    
    // Apply the update
    applyDeltaUpdate(update);
    
    // Update timestamp
    config_timestamp_ = millis() / 1000;
    
    // Send ACK
    sendAck(update->global_version, (ConfigSection)update->section, true);
    
    LOG_INFO("CONFIG: Delta applied and acknowledged");
}

void ReceiverConfigManager::applyDeltaUpdate(const config_delta_update_t* update) {
    bool success = config_manager_.updateField((ConfigSection)update->section, 
                                              update->field_id,
                                              update->value_data,
                                              update->value_length);
    
    if (success) {
        LOG_DEBUG("CONFIG: Field updated successfully");
    } else {
        LOG_ERROR("CONFIG: Failed to update field");
    }
}

void ReceiverConfigManager::sendAck(uint16_t version, ConfigSection section, bool success) {
    // Check if we have a valid transmitter MAC
    bool has_mac = false;
    for (int i = 0; i < 6; i++) {
        if (transmitter_mac_[i] != 0) {
            has_mac = true;
            break;
        }
    }
    
    if (!has_mac) {
        LOG_WARN("CONFIG: Cannot send ACK - no transmitter MAC stored");
        return;
    }
    
    config_ack_t ack;
    ack.type = msg_config_ack;
    ack.acked_version = version;
    ack.section = section;
    ack.success = success ? 1 : 0;
    ack.timestamp = millis();
    
    esp_err_t result = esp_now_send(transmitter_mac_, (uint8_t*)&ack, sizeof(ack));
    
    if (result == ESP_OK) {
        LOG_DEBUG("CONFIG: ACK sent (version=%u, section=%d, success=%d)",
                 version, section, success);
    } else {
        LOG_ERROR("CONFIG: Failed to send ACK: %s", esp_err_to_name(result));
    }
}

bool ReceiverConfigManager::validateChecksum(const FullConfigSnapshot* config) {
    uint32_t calculated = calculateCRC32((uint8_t*)config, 
                                        sizeof(FullConfigSnapshot) - sizeof(uint32_t));
    return calculated == config->checksum;
}

void ReceiverConfigManager::onResyncRequested() {
    LOG_WARN("CONFIG: Resync requested - requesting full snapshot");
    requestFullSnapshot(transmitter_mac_);
}

const FullConfigSnapshot& ReceiverConfigManager::getCurrentConfig() const {
    return config_manager_.getFullConfig();
}

const MqttConfig& ReceiverConfigManager::getMqttConfig() const {
    return config_manager_.getMqttConfig();
}

const NetworkConfig& ReceiverConfigManager::getNetworkConfig() const {
    return config_manager_.getNetworkConfig();
}

const BatteryConfig& ReceiverConfigManager::getBatteryConfig() const {
    return config_manager_.getBatteryConfig();
}

const PowerConfig& ReceiverConfigManager::getPowerConfig() const {
    return config_manager_.getPowerConfig();
}

const InverterConfig& ReceiverConfigManager::getInverterConfig() const {
    return config_manager_.getInverterConfig();
}

const CanConfig& ReceiverConfigManager::getCanConfig() const {
    return config_manager_.getCanConfig();
}

const ContactorConfig& ReceiverConfigManager::getContactorConfig() const {
    return config_manager_.getContactorConfig();
}

const SystemConfig& ReceiverConfigManager::getSystemConfig() const {
    return config_manager_.getSystemConfig();
}

uint16_t ReceiverConfigManager::getGlobalVersion() const {
    return config_manager_.getGlobalVersion();
}
