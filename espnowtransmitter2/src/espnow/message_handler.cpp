#include "message_handler.h"
#include "../network/ethernet_manager.h"
#include "../config/task_config.h"
#include "../config/logging_config.h"
#include <Arduino.h>
#include <esp_now.h>
#include <espnow_transmitter.h>
#include <espnow_peer_manager.h>
#include <espnow_message_router.h>
#include <espnow_standard_handlers.h>
#include <espnow_packet_utils.h>

EspnowMessageHandler& EspnowMessageHandler::instance() {
    static EspnowMessageHandler instance;
    return instance;
}

EspnowMessageHandler::EspnowMessageHandler() {
    setup_message_routes();
}

void EspnowMessageHandler::setup_message_routes() {
    auto& router = EspnowMessageRouter::instance();
    
    // Setup PROBE handler configuration
    probe_config_.send_ack_response = true;
    probe_config_.connection_flag = &receiver_connected_;
    probe_config_.peer_mac_storage = receiver_mac;
    probe_config_.on_connection = [](const uint8_t* mac, bool connected) {
        LOG_INFO("Receiver connected via PROBE");
    };
    
    // Setup ACK handler configuration
    ack_config_.connection_flag = &receiver_connected_;
    ack_config_.peer_mac_storage = receiver_mac;
    ack_config_.expected_seq = &g_ack_seq;
    ack_config_.lock_channel = &g_lock_channel;
    ack_config_.ack_received_flag = &g_ack_received;  // For channel hopping discovery
    ack_config_.set_wifi_channel = true;               // Actually change WiFi channel on ACK
    ack_config_.on_connection = [](const uint8_t* mac, bool connected) {
        LOG_INFO("Receiver connected via ACK");
    };
    
    // Register standard message handlers
    router.register_route(msg_probe, 
        [](const espnow_queue_msg_t* msg, void* ctx) {
            auto* self = static_cast<EspnowMessageHandler*>(ctx);
            EspnowStandardHandlers::handle_probe(msg, &self->probe_config_);
        }, 
        0xFF, this);
    
    router.register_route(msg_ack,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            auto* self = static_cast<EspnowMessageHandler*>(ctx);
            EspnowStandardHandlers::handle_ack(msg, &self->ack_config_);
        },
        0xFF, this);
    
    // Register custom message handlers
    router.register_route(msg_request_data,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            static_cast<EspnowMessageHandler*>(ctx)->handle_request_data(*msg);
        },
        0xFF, this);
    
    router.register_route(msg_abort_data,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            static_cast<EspnowMessageHandler*>(ctx)->handle_abort_data(*msg);
        },
        0xFF, this);
    
    router.register_route(msg_reboot,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            static_cast<EspnowMessageHandler*>(ctx)->handle_reboot(*msg);
        },
        0xFF, this);
    
    router.register_route(msg_ota_start,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            static_cast<EspnowMessageHandler*>(ctx)->handle_ota_start(*msg);
        },
        0xFF, this);
    
    LOG_DEBUG("Registered %d message routes", router.route_count());
}

void EspnowMessageHandler::start_rx_task(QueueHandle_t queue) {
    xTaskCreate(
        rx_task_impl,
        "espnow_rx",
        task_config::STACK_SIZE_ESPNOW_RX,
        (void*)queue,
        task_config::PRIORITY_CRITICAL,
        nullptr
    );
    LOG_DEBUG("ESP-NOW RX task started");
}

void EspnowMessageHandler::rx_task_impl(void* parameter) {
    QueueHandle_t queue = (QueueHandle_t)parameter;
    auto& handler = instance();
    auto& router = EspnowMessageRouter::instance();
    
    LOG_DEBUG("Message RX task running");
    espnow_queue_msg_t msg;
    
    while (true) {
        if (xQueueReceive(queue, &msg, portMAX_DELAY) == pdTRUE) {
            // Route message using common router
            if (!router.route_message(msg)) {
                // Message not handled by any route
                uint8_t msg_type = (msg.len > 0) ? msg.data[0] : 0;
                LOG_WARN("Unknown message type: %u from %02X:%02X:%02X:%02X:%02X:%02X",
                             msg_type, msg.mac[0], msg.mac[1], msg.mac[2], msg.mac[3], msg.mac[4], msg.mac[5]);
            }
        }
    }
}

void EspnowMessageHandler::handle_request_data(const espnow_queue_msg_t& msg) {
    if (msg.len < (int)sizeof(request_data_t)) return;
    
    const request_data_t* req = reinterpret_cast<const request_data_t*>(msg.data);
    LOG_DEBUG("REQUEST_DATA (subtype=%d) from %02X:%02X:%02X:%02X:%02X:%02X",
                 req->subtype, msg.mac[0], msg.mac[1], msg.mac[2], msg.mac[3], msg.mac[4], msg.mac[5]);
    
    // Handle based on subtype
    switch (req->subtype) {
        case subtype_power_profile:
            transmission_active_ = true;
            LOG_INFO(">>> Power profile transmission STARTED");
            break;
        
        case subtype_settings: {
            LOG_DEBUG(">>> Settings request - sending IP data");
            
            // Send Ethernet IP configuration to receiver
            if (EthernetManager::instance().is_connected()) {
                IPAddress local_ip = EthernetManager::instance().get_local_ip();
                IPAddress gateway = EthernetManager::instance().get_gateway_ip();
                IPAddress subnet = EthernetManager::instance().get_subnet_mask();
                
                // Create proper espnow_packet_t structure
                espnow_packet_t packet;
                packet.type = msg_packet;
                packet.subtype = subtype_settings;
                packet.seq = esp_random();
                packet.frag_index = 0;
                packet.frag_total = 1;
                packet.payload_len = 12;  // IP[4] + Gateway[4] + Subnet[4]
                
                // Pack IP address bytes into payload
                for (int i = 0; i < 4; i++) {
                    packet.payload[i] = local_ip[i];       // IP at offset 0
                    packet.payload[4 + i] = gateway[i];    // Gateway at offset 4
                    packet.payload[8 + i] = subnet[i];     // Subnet at offset 8
                }
                
                // Calculate checksum using common utility
                packet.checksum = EspnowPacketUtils::calculate_checksum(packet.payload, packet.payload_len);
                
                // Send IP data via ESP-NOW
                esp_err_t result = esp_now_send(msg.mac, (const uint8_t*)&packet, sizeof(packet));
                
                if (result == ESP_OK) {
                    // Log packet info using common utility
                    EspnowPacketUtils::PacketInfo info;
                    info.seq = packet.seq;
                    info.frag_index = packet.frag_index;
                    info.frag_total = packet.frag_total;
                    info.payload_len = packet.payload_len;
                    info.subtype = packet.subtype;
                    info.checksum = packet.checksum;
                    info.payload = packet.payload;
                    EspnowPacketUtils::print_packet_info(info, "SETTINGS (sent)");
                    
                    LOG_DEBUG("Sent IP data: %s, Gateway: %s, Subnet: %s",
                                 local_ip.toString().c_str(),
                                 gateway.toString().c_str(),
                                 subnet.toString().c_str());
                } else {
                    LOG_ERROR("Failed to send IP data: %s", esp_err_to_name(result));
                }
            } else {
                LOG_ERROR("Ethernet not connected, cannot send IP");
            }
            break;
        }
        
        case subtype_events:
        case subtype_logs:
        case subtype_cell_info:
            LOG_DEBUG("Subtype %d not implemented yet", req->subtype);
            break;
            
        default:
            LOG_WARN("Unknown subtype: %d", req->subtype);
            break;
    }
}

void EspnowMessageHandler::handle_abort_data(const espnow_queue_msg_t& msg) {
    if (msg.len < (int)sizeof(abort_data_t)) return;
    
    const abort_data_t* abort = reinterpret_cast<const abort_data_t*>(msg.data);
    LOG_DEBUG("ABORT_DATA (subtype=%d) from %02X:%02X:%02X:%02X:%02X:%02X",
                 abort->subtype, msg.mac[0], msg.mac[1], msg.mac[2], msg.mac[3], msg.mac[4], msg.mac[5]);
    
    // Handle based on subtype
    switch (abort->subtype) {
        case subtype_power_profile:
            transmission_active_ = false;
            LOG_INFO(">>> Power profile transmission STOPPED");
            break;
        
        case subtype_settings:
        case subtype_events:
        case subtype_logs:
        case subtype_cell_info:
            LOG_DEBUG("Subtype %d not implemented yet", abort->subtype);
            break;
            
        default:
            LOG_WARN("Unknown subtype: %d", abort->subtype);
            break;
    }
}

void EspnowMessageHandler::handle_reboot(const espnow_queue_msg_t& msg) {
    LOG_INFO("REBOOT command from %02X:%02X:%02X:%02X:%02X:%02X",
                 msg.mac[0], msg.mac[1], msg.mac[2], msg.mac[3], msg.mac[4], msg.mac[5]);
    LOG_INFO(">>> Rebooting in 1 second...");
    Serial.flush();
    delay(1000);
    ESP.restart();
}

void EspnowMessageHandler::handle_ota_start(const espnow_queue_msg_t& msg) {
    if (msg.len < (int)sizeof(ota_start_t)) return;
    
    const ota_start_t* ota = reinterpret_cast<const ota_start_t*>(msg.data);
    LOG_INFO("OTA_START command (size=%u bytes) from %02X:%02X:%02X:%02X:%02X:%02X",
                 ota->size, msg.mac[0], msg.mac[1], msg.mac[2], msg.mac[3], msg.mac[4], msg.mac[5]);
    LOG_INFO(">>> OTA mode ready - waiting for HTTP POST...");
}

// ═══════════════════════════════════════════════════════════════════════
// Helper function: Send IP configuration to receiver
// Called automatically when Ethernet connects
// ═══════════════════════════════════════════════════════════════════════

void send_ip_to_receiver() {
    if (!EthernetManager::instance().is_connected()) return;
    
    // Get receiver MAC from espnow_transmitter library
    extern uint8_t receiver_mac[];
    
    // Safety check: Don't send if receiver is broadcast address (not discovered yet)
    bool is_broadcast = true;
    for (int i = 0; i < 6; i++) {
        if (receiver_mac[i] != 0xFF) {
            is_broadcast = false;
            break;
        }
    }
    
    if (is_broadcast) {
        LOG_DEBUG("[ETH] Receiver not discovered yet, will send IP later");
        return;
    }
    
    // Check if receiver peer exists
    if (!esp_now_is_peer_exist(receiver_mac)) {
        LOG_DEBUG("[ETH] Receiver peer not registered, skipping IP send");
        return;
    }
    
    IPAddress local_ip = EthernetManager::instance().get_local_ip();
    IPAddress gateway = EthernetManager::instance().get_gateway_ip();
    IPAddress subnet = EthernetManager::instance().get_subnet_mask();
    
    // Create proper espnow_packet_t structure
    espnow_packet_t packet;
    packet.type = msg_packet;
    packet.subtype = subtype_settings;
    packet.seq = esp_random();
    packet.frag_index = 0;
    packet.frag_total = 1;
    packet.payload_len = 12;  // IP[4] + Gateway[4] + Subnet[4]
    
    // Pack IP address bytes into payload
    for (int i = 0; i < 4; i++) {
        packet.payload[i] = local_ip[i];       // IP at offset 0
        packet.payload[4 + i] = gateway[i];    // Gateway at offset 4
        packet.payload[8 + i] = subnet[i];     // Subnet at offset 8
    }
    
    // Calculate checksum
    packet.checksum = EspnowPacketUtils::calculate_checksum(packet.payload, packet.payload_len);
    
    // Send IP data via ESP-NOW
    esp_err_t result = esp_now_send(receiver_mac, (const uint8_t*)&packet, sizeof(packet));
    
    if (result == ESP_OK) {
        LOG_INFO("[ETH] Sent IP configuration to receiver: %s", local_ip.toString().c_str());
    } else {
        LOG_WARN("[ETH] Failed to send IP to receiver: %s", esp_err_to_name(result));
    }
}
