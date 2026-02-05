/**
 * @file espnow_message_router.h
 * @brief Common message routing system for ESP-NOW messages
 * 
 * Provides a flexible function-table based message router that can dispatch
 * incoming ESP-NOW messages to appropriate handlers based on message type
 * and optional subtype matching.
 */

#pragma once

#include <espnow_common.h>
#include <functional>

/**
 * @brief Message handler function signature
 * @param msg Pointer to queue message containing ESP-NOW data
 * @param context Optional user context pointer passed to handler
 */
using MessageHandler = std::function<void(const espnow_queue_msg_t*, void*)>;

/**
 * @brief Message route definition
 */
struct MessageRoute {
    uint8_t type;              ///< Message type to match (e.g., msg_probe, msg_data)
    uint8_t subtype;           ///< Subtype to match (0xFF = match any subtype)
    MessageHandler handler;    ///< Handler function to call
    void* context;             ///< Optional context pointer passed to handler
};

/**
 * @brief ESP-NOW Message Router
 * 
 * Routes incoming ESP-NOW messages to registered handlers based on type/subtype.
 * Supports both OOP singleton pattern and function-table routing like receiver.
 */
class EspnowMessageRouter {
public:
    static EspnowMessageRouter& instance();
    
    /**
     * @brief Register a message route
     * @param type Message type to handle
     * @param handler Handler function
     * @param subtype Optional subtype (0xFF for any)
     * @param context Optional context pointer
     */
    void register_route(uint8_t type, MessageHandler handler, 
                       uint8_t subtype = 0xFF, void* context = nullptr);
    
    /**
     * @brief Register multiple routes at once
     * @param routes Array of MessageRoute structs
     * @param count Number of routes in array
     */
    void register_routes(const MessageRoute* routes, size_t count);
    
    /**
     * @brief Route a message to appropriate handler
     * @param msg Message to route
     * @return true if message was handled, false if no matching route
     */
    bool route_message(const espnow_queue_msg_t& msg);
    
    /**
     * @brief Clear all registered routes
     */
    void clear_routes();
    
    /**
     * @brief Get number of registered routes
     */
    size_t route_count() const { return route_count_; }
    
private:
    EspnowMessageRouter() = default;
    
    static constexpr size_t MAX_ROUTES = 20;
    MessageRoute routes_[MAX_ROUTES];
    size_t route_count_ = 0;
};

namespace EspnowMessageUtils {
    /**
     * @brief Extract message type from queue message
     */
    inline uint8_t get_message_type(const espnow_queue_msg_t& msg) {
        return (msg.len > 0) ? msg.data[0] : 0;
    }
    
    /**
     * @brief Extract subtype from packet message
     * @return Subtype, or 0xFF if not a packet message or too short
     */
    inline uint8_t get_packet_subtype(const espnow_queue_msg_t& msg) {
        if (msg.len < (int)sizeof(espnow_packet_t)) return 0xFF;
        if (msg.data[0] != msg_packet) return 0xFF;
        const espnow_packet_t* pkt = reinterpret_cast<const espnow_packet_t*>(msg.data);
        return pkt->subtype;
    }
    
    /**
     * @brief Format MAC address as string
     * @param mac MAC address bytes
     * @param buffer Output buffer (min 18 bytes)
     */
    inline void format_mac_address(const uint8_t* mac, char* buffer) {
        snprintf(buffer, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
}
