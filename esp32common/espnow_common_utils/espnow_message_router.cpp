/**
 * @file espnow_message_router.cpp
 * @brief Implementation of ESP-NOW message router
 */

#include "espnow_message_router.h"
#include <Arduino.h>

EspnowMessageRouter& EspnowMessageRouter::instance() {
    static EspnowMessageRouter instance;
    return instance;
}

void EspnowMessageRouter::register_route(uint8_t type, MessageHandler handler,
                                        uint8_t subtype, void* context) {
    if (route_count_ >= MAX_ROUTES) {
        Serial.println("[ROUTER] ERROR: Maximum routes reached!");
        return;
    }
    
    routes_[route_count_++] = MessageRoute{type, subtype, handler, context};
    
    Serial.printf("[ROUTER] Registered route: type=0x%02X, subtype=0x%02X (%d total routes)\n",
                 type, subtype, route_count_);
}

void EspnowMessageRouter::register_routes(const MessageRoute* routes, size_t count) {
    for (size_t i = 0; i < count; i++) {
        register_route(routes[i].type, routes[i].handler, 
                      routes[i].subtype, routes[i].context);
    }
}

bool EspnowMessageRouter::route_message(const espnow_queue_msg_t& msg) {
    if (msg.len < 1) return false;
    
    uint8_t type = msg.data[0];
    uint8_t subtype = EspnowMessageUtils::get_packet_subtype(msg);
    
    // Find matching route
    for (size_t i = 0; i < route_count_; i++) {
        const MessageRoute& route = routes_[i];
        
        // Match on type, and subtype if specified
        if (type == route.type && 
            (route.subtype == 0xFF || subtype == route.subtype)) {
            
            if (route.handler) {
                route.handler(&msg, route.context);
                return true;
            }
        }
    }
    
    return false;
}

void EspnowMessageRouter::clear_routes() {
    route_count_ = 0;
    Serial.println("[ROUTER] All routes cleared");
}
