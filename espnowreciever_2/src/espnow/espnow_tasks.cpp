#include "espnow_tasks.h"
#include "../common.h"
#include "../display/display_core.h"
#include "../display/display_led.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <espnow_common.h>
#include <espnow_peer_manager.h>
#include <espnow_discovery.h>
#include <espnow_message_router.h>
#include <espnow_standard_handlers.h>
#include <espnow_packet_utils.h>

extern void notify_sse_data_updated();
extern void register_transmitter_mac(const uint8_t* mac);

// Forward declarations for handler functions
void handle_data_message(const espnow_queue_msg_t* msg);
void handle_flash_led_message(const espnow_queue_msg_t* msg);
void handle_packet_settings(const espnow_queue_msg_t* msg);
void handle_packet_events(const espnow_queue_msg_t* msg);
void handle_packet_logs(const espnow_queue_msg_t* msg);
void handle_packet_cell_info(const espnow_queue_msg_t* msg);
void handle_packet_unknown(const espnow_queue_msg_t* msg, uint8_t subtype);

// ═══════════════════════════════════════════════════════════════════════
// MESSAGE HANDLER CONFIGURATIONS
// ═══════════════════════════════════════════════════════════════════════

// Configuration for standard PROBE handler
EspnowStandardHandlers::ProbeHandlerConfig probe_config;

// Configuration for standard ACK handler  
EspnowStandardHandlers::AckHandlerConfig ack_config;

// ═══════════════════════════════════════════════════════════════════════
// MESSAGE ROUTER SETUP
// ═══════════════════════════════════════════════════════════════════════

void setup_message_routes() {
    auto& router = EspnowMessageRouter::instance();
    
    // Setup PROBE handler configuration
    probe_config.send_ack_response = true;
    probe_config.connection_flag = &ESPNow::transmitter_connected;
    probe_config.peer_mac_storage = nullptr;  // We use register_transmitter_mac instead
    probe_config.on_connection = [](const uint8_t* mac, bool connected) {
        register_transmitter_mac(mac);
        LOG_INFO("Transmitter connected via PROBE");
    };
    
    // Setup ACK handler configuration (receiver sends ACKs, doesn't usually receive them)
    ack_config.connection_flag = &ESPNow::transmitter_connected;
    ack_config.peer_mac_storage = nullptr;
    ack_config.expected_seq = nullptr;      // Receiver doesn't validate ACK sequences
    ack_config.lock_channel = nullptr;      // Receiver doesn't change channels
    ack_config.ack_received_flag = nullptr;
    ack_config.set_wifi_channel = false;    // Receiver stays on its configured channel
    ack_config.on_connection = [](const uint8_t* mac, bool connected) {
        register_transmitter_mac(mac);
        LOG_INFO("Transmitter connected via ACK");
    };
    
    // Register standard message handlers
    router.register_route(msg_probe, 
        [](const espnow_queue_msg_t* msg, void* ctx) {
            EspnowStandardHandlers::handle_probe(msg, &probe_config);
        }, 
        0xFF, nullptr);
    
    router.register_route(msg_ack,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            EspnowStandardHandlers::handle_ack(msg, &ack_config);
        },
        0xFF, nullptr);
    
    // Register custom DATA handler
    router.register_route(msg_data,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            handle_data_message(msg);
        },
        0xFF, nullptr);
    
    // Register flash LED handler
    router.register_route(msg_flash_led,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            handle_flash_led_message(msg);
        },
        0xFF, nullptr);
    
    // Register packet handlers with subtypes
    router.register_route(msg_packet,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            handle_packet_settings(msg);
        },
        subtype_settings, nullptr);
    
    router.register_route(msg_packet,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            handle_packet_events(msg);
        },
        subtype_events, nullptr);
    
    router.register_route(msg_packet,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            handle_packet_logs(msg);
        },
        subtype_logs, nullptr);
    
    router.register_route(msg_packet,
        [](const espnow_queue_msg_t* msg, void* ctx) {
            handle_packet_cell_info(msg);
        },
        subtype_cell_info, nullptr);
    
    LOG_DEBUG("Registered %d message routes", router.route_count());
}

// Note: task_periodic_announcement removed - EspnowDiscovery::start() is now called directly from setup()
// since it creates its own internal task. The old wrapper task was causing the callback lambda to be
// destroyed while the internal task was still running, leading to crashes.

void task_espnow_worker(void *parameter) {
    LOG_DEBUG("ESP-NOW Worker task started");
    
    // Setup message routes on first run
    static bool routes_initialized = false;
    if (!routes_initialized) {
        setup_message_routes();
        routes_initialized = true;
    }
    
    auto& router = EspnowMessageRouter::instance();
    espnow_queue_msg_t queue_msg;
    
    for (;;) {
        if (xQueueReceive(ESPNow::queue, &queue_msg, portMAX_DELAY) == pdTRUE) {
            if (queue_msg.len < 1) continue;
            
            // Ensure sender is registered as a peer using common utility
            if (!EspnowPeerManager::is_peer_registered(queue_msg.mac)) {
                EspnowPeerManager::add_peer(queue_msg.mac, 0);
            }
            
            // Route message using common router
            if (!router.route_message(queue_msg)) {
                // Message not handled - check if it's an unknown packet subtype
                uint8_t type = queue_msg.data[0];
                if (type == msg_packet) {
                    EspnowPacketUtils::PacketInfo info;
                    if (EspnowPacketUtils::get_packet_info(&queue_msg, info)) {
                        handle_packet_unknown(&queue_msg, info.subtype);
                    }
                } else {
                    LOG_WARN("[ESP-NOW] Unknown message type: %u, len=%d", type, queue_msg.len);
                }
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════
// MESSAGE HANDLER IMPLEMENTATIONS
// ═══════════════════════════════════════════════════════════════════════

// Note: handle_probe_message and handle_ack_message removed - now using
// EspnowStandardHandlers::handle_probe and EspnowStandardHandlers::handle_ack
// from common library with custom configuration callbacks.

void handle_flash_led_message(const espnow_queue_msg_t* msg) {
    if (msg->len >= (int)sizeof(flash_led_t)) {
        const flash_led_t* flash_msg = reinterpret_cast<const flash_led_t*>(msg->data);
        
        // Validate color code (wire format matches enum: 0=RED, 1=GREEN, 2=ORANGE)
        if (flash_msg->color > LED_ORANGE) {
            LOG_WARN("Invalid LED color code: %d", flash_msg->color);
            return;
        }
        
        LEDColor color = static_cast<LEDColor>(flash_msg->color);
        
        static const char* color_names[] = {"RED", "GREEN", "ORANGE"};
        LOG_DEBUG("Flash LED request: color=%d (%s)", flash_msg->color, color_names[flash_msg->color]);
        
        // Store the current LED color for status indicator task to use
        ESPNow::current_led_color = color;
    }
}

void handle_data_message(const espnow_queue_msg_t* msg) {
    if (msg->len >= (int)sizeof(espnow_payload_t)) {
        const espnow_payload_t* payload = reinterpret_cast<const espnow_payload_t*>(msg->data);
        
        uint16_t calc_checksum = payload->soc + (uint16_t)payload->power;
        
        if (calc_checksum == payload->checksum) {
            // Update global variables with dirty flags
            if (ESPNow::received_soc != payload->soc) {
                ESPNow::received_soc = payload->soc;
                ESPNow::dirty_flags.soc_changed = true;
            }
            if (ESPNow::received_power != payload->power) {
                ESPNow::received_power = payload->power;
                ESPNow::dirty_flags.power_changed = true;
            }
            ESPNow::data_received = true;
            
            register_transmitter_mac(msg->mac);
            notify_sse_data_updated();
            
            // State machine transition
            extern SystemState current_state;
            if (current_state == SystemState::TEST_MODE) {
                transition_to_state(SystemState::NORMAL_OPERATION);
            }
            
            LOG_DEBUG("[ESP-NOW] Valid: SOC=%d%%, Power=%dW (MAC: %02X:%02X:%02X:%02X:%02X:%02X)", 
                         payload->soc, payload->power,
                         msg->mac[0], msg->mac[1], msg->mac[2],
                         msg->mac[3], msg->mac[4], msg->mac[5]);
            
            // Batch display updates in single mutex acquisition to reduce contention
            if (ESPNow::dirty_flags.soc_changed || ESPNow::dirty_flags.power_changed) {
                if (xSemaphoreTake(RTOS::tft_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    if (ESPNow::dirty_flags.soc_changed) {
                        display_soc((float)payload->soc);
                        ESPNow::dirty_flags.soc_changed = false;
                    }
                    if (ESPNow::dirty_flags.power_changed) {
                        display_power((int32_t)payload->power);
                        ESPNow::dirty_flags.power_changed = false;
                    }
                    xSemaphoreGive(RTOS::tft_mutex);
                }
            }
        } else {
            // CRC mismatch
            LOG_WARN("CRC failed: expected 0x%04X, got 0x%04X", 
                         calc_checksum, payload->checksum);
        }
    }
}

void handle_packet_settings(const espnow_queue_msg_t* msg) {
    EspnowPacketUtils::PacketInfo info;
    if (!EspnowPacketUtils::get_packet_info(msg, info)) {
        LOG_WARN("Invalid packet structure");
        return;
    }
    
    EspnowPacketUtils::print_packet_info(info, "SETTINGS");
    
    if (info.payload_len >= 12) {
        extern void store_transmitter_ip_data(const uint8_t* ip, const uint8_t* gateway, const uint8_t* subnet);
        
        const uint8_t* ip = &info.payload[0];
        const uint8_t* gateway = &info.payload[4];
        const uint8_t* subnet = &info.payload[8];
        
        store_transmitter_ip_data(ip, gateway, subnet);
        
        LOG_DEBUG("Received IP: %d.%d.%d.%d, GW: %d.%d.%d.%d, Subnet: %d.%d.%d.%d",
                     ip[0], ip[1], ip[2], ip[3],
                     gateway[0], gateway[1], gateway[2], gateway[3],
                     subnet[0], subnet[1], subnet[2], subnet[3]);
    }
}

void handle_packet_events(const espnow_queue_msg_t* msg) {
    EspnowPacketUtils::PacketInfo info;
    if (!EspnowPacketUtils::get_packet_info(msg, info)) {
        LOG_WARN("Invalid packet structure");
        return;
    }
    
    EspnowPacketUtils::print_packet_info(info, "EVENTS");
    
    if (info.payload_len >= 5) {
        uint8_t soc = info.payload[0];
        int32_t power;
        memcpy(&power, &info.payload[1], sizeof(int32_t));
        
        if (ESPNow::received_soc != soc) {
            ESPNow::received_soc = soc;
            ESPNow::dirty_flags.soc_changed = true;
        }
        if (ESPNow::received_power != power) {
            ESPNow::received_power = power;
            ESPNow::dirty_flags.power_changed = true;
        }
        ESPNow::data_received = true;
        
        register_transmitter_mac(msg->mac);
        notify_sse_data_updated();
        
        LOG_TRACE("EVENTS: SOC=%d%%, Power=%dW", soc, power);
        
        // Batch display updates in single mutex acquisition to reduce contention
        if (ESPNow::dirty_flags.soc_changed || ESPNow::dirty_flags.power_changed) {
            if (xSemaphoreTake(RTOS::tft_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                if (ESPNow::dirty_flags.soc_changed) {
                    display_soc((float)soc);
                    ESPNow::dirty_flags.soc_changed = false;
                }
                if (ESPNow::dirty_flags.power_changed) {
                    display_power(power);
                    ESPNow::dirty_flags.power_changed = false;
                }
                xSemaphoreGive(RTOS::tft_mutex);
            }
        }
    }
}

void handle_packet_logs(const espnow_queue_msg_t* msg) {
    EspnowPacketUtils::PacketInfo info;
    if (EspnowPacketUtils::get_packet_info(msg, info)) {
        EspnowPacketUtils::print_packet_info(info, "LOGS");
    }
}

void handle_packet_cell_info(const espnow_queue_msg_t* msg) {
    EspnowPacketUtils::PacketInfo info;
    if (EspnowPacketUtils::get_packet_info(msg, info)) {
        EspnowPacketUtils::print_packet_info(info, "CELL_INFO");
    }
}

void handle_packet_unknown(const espnow_queue_msg_t* msg, uint8_t subtype) {
    EspnowPacketUtils::PacketInfo info;
    if (EspnowPacketUtils::get_packet_info(msg, info)) {
        LOG_WARN("[ESP-NOW] PACKET/UNKNOWN: subtype=%u, seq=%u", 
                     subtype, info.seq);
    }
}
