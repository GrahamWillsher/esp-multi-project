/**
 * @file espnow_message_router.cpp
 * @brief Implementation of ESP-NOW message router
 */

#include "espnow_message_router.h"
#include <Arduino.h>
#include <cstring>
#include <logging_config.h>

EspnowMessageRouter::EspnowMessageRouter()
    : packet_any_head_(NO_ROUTE),
      packet_any_tail_(NO_ROUTE) {
    reset_route_indexes();
}

void EspnowMessageRouter::reset_route_indexes() {
    memset(type_heads_, NO_ROUTE, sizeof(type_heads_));
    memset(type_tails_, NO_ROUTE, sizeof(type_tails_));
    memset(packet_sub_heads_, NO_ROUTE, sizeof(packet_sub_heads_));
    memset(packet_sub_tails_, NO_ROUTE, sizeof(packet_sub_tails_));
    memset(next_route_, NO_ROUTE, sizeof(next_route_));
    packet_any_head_ = NO_ROUTE;
    packet_any_tail_ = NO_ROUTE;
}

void EspnowMessageRouter::append_type_route(uint8_t type, uint8_t route_index) {
    if (type_heads_[type] == NO_ROUTE) {
        type_heads_[type] = route_index;
        type_tails_[type] = route_index;
        return;
    }

    next_route_[type_tails_[type]] = route_index;
    type_tails_[type] = route_index;
}

void EspnowMessageRouter::append_packet_subtype_route(uint8_t subtype, uint8_t route_index) {
    if (packet_sub_heads_[subtype] == NO_ROUTE) {
        packet_sub_heads_[subtype] = route_index;
        packet_sub_tails_[subtype] = route_index;
        return;
    }

    next_route_[packet_sub_tails_[subtype]] = route_index;
    packet_sub_tails_[subtype] = route_index;
}

void EspnowMessageRouter::append_packet_any_route(uint8_t route_index) {
    if (packet_any_head_ == NO_ROUTE) {
        packet_any_head_ = route_index;
        packet_any_tail_ = route_index;
        return;
    }

    next_route_[packet_any_tail_] = route_index;
    packet_any_tail_ = route_index;
}

bool EspnowMessageRouter::try_route_chain(uint8_t head_index,
                                          const espnow_queue_msg_t& msg,
                                          uint8_t subtype) {
    uint8_t route_index = head_index;
    while (route_index != NO_ROUTE) {
        const MessageRoute& route = routes_[route_index];
        const uint8_t next_index = next_route_[route_index];

        if (route.handler && (route.subtype == 0xFF || subtype == route.subtype)) {
            route.handler(&msg, route.context);
            return true;
        }

        route_index = next_index;
    }

    return false;
}

EspnowMessageRouter& EspnowMessageRouter::instance() {
    static EspnowMessageRouter instance;
    return instance;
}

void EspnowMessageRouter::register_route(uint8_t type, MessageHandler handler,
                                        uint8_t subtype, void* context) {
    if (route_count_ >= MAX_ROUTES) {
        LOG_ERROR("ROUTER", "Maximum routes reached");
        return;
    }

    const uint8_t route_index = static_cast<uint8_t>(route_count_);
    routes_[route_count_++] = MessageRoute{type, subtype, handler, context};
    next_route_[route_index] = NO_ROUTE;

    if (type == msg_packet) {
        if (subtype == 0xFF) {
            append_packet_any_route(route_index);
        } else {
            append_packet_subtype_route(subtype, route_index);
        }
    } else {
        append_type_route(type, route_index);
    }
    
    LOG_INFO("ROUTER", "Registered route: type=0x%02X, subtype=0x%02X (%d total routes)",
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

    if (type == msg_packet) {
        if (subtype != 0xFF && try_route_chain(packet_sub_heads_[subtype], msg, subtype)) {
            return true;
        }
        return try_route_chain(packet_any_head_, msg, subtype);
    }

    return try_route_chain(type_heads_[type], msg, subtype);
}

void EspnowMessageRouter::clear_routes() {
    route_count_ = 0;
    reset_route_indexes();
    LOG_INFO("ROUTER", "All routes cleared");
}
