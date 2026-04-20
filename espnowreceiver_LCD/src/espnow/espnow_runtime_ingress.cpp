#include "espnow/espnow_runtime_detail.h"

#include <cstddef>
#include <esp32common/espnow/message_router.h>

namespace ESPNowRuntime::Detail {
namespace {

constexpr const char* kLogTag = "ESPNOW";

const char* message_type_name(uint8_t type) {
    switch (type) {
        case msg_probe: return "PROBE";
        case msg_ack: return "ACK";
        case msg_data: return "DATA";
        case msg_flash_led: return "FLASH_LED";
        case msg_battery_status: return "BATTERY_STATUS";
        case msg_packet: return "PACKET";
        case msg_heartbeat: return "HEARTBEAT";
        default: return "UNKNOWN";
    }
}

bool validate_type_catalog_fragment_shape(const espnow_queue_msg_t& msg, const char* label) {
    if (msg.len < static_cast<int>(offsetof(type_catalog_fragment_t, entries))) {
        LOG_WARN(kLogTag, "%s too short: %d bytes", label, msg.len);
        return false;
    }

    const auto* fragment = reinterpret_cast<const type_catalog_fragment_t*>(msg.data);
    if (fragment->entry_count > TYPE_CATALOG_MAX_ENTRIES_PER_FRAGMENT) {
        LOG_WARN(kLogTag, "%s invalid entry_count=%u", label, static_cast<unsigned>(fragment->entry_count));
        return false;
    }

    const size_t expected_len = offsetof(type_catalog_fragment_t, entries) +
                                (static_cast<size_t>(fragment->entry_count) * sizeof(type_catalog_entry_t));
    if (msg.len < static_cast<int>(expected_len)) {
        LOG_WARN(kLogTag, "%s too short: %d bytes (expected >= %u)",
                 label,
                 msg.len,
                 static_cast<unsigned>(expected_len));
        return false;
    }

    return true;
}

}  // namespace

bool parse_ingress_message(const espnow_queue_msg_t& msg, IngressParseResult& out) {
    if (msg.len < 1) {
        LOG_WARN(kLogTag, "Dropping ingress frame with invalid length=%d", msg.len);
        return false;
    }

    out = {};
    out.msg = &msg;
    out.type = msg.data[0];

    if (out.type == msg_packet) {
        if (!EspnowPacketUtils::get_packet_info(&msg, out.packet_info)) {
            LOG_WARN(kLogTag, "Dropping PACKET frame with invalid structure");
            return false;
        }
        out.subtype = out.packet_info.subtype;
        out.has_packet_info = true;
    }

    return true;
}

bool validate_ingress_message(const IngressParseResult& parsed) {
    if (!parsed.msg) {
        return false;
    }

    const auto& msg = *parsed.msg;
    switch (parsed.type) {
        case msg_packet: {
            if (!parsed.has_packet_info) {
                LOG_WARN(kLogTag, "PACKET frame missing parse metadata");
                return false;
            }

            const uint32_t calc_crc =
                EspnowPacketUtils::crc32_packet(parsed.packet_info.payload, parsed.packet_info.payload_len);
            if (calc_crc != parsed.packet_info.checksum) {
                LOG_WARN(kLogTag, "PACKET CRC32 mismatch (calc=0x%08lX recv=0x%08lX subtype=%u)",
                         static_cast<unsigned long>(calc_crc),
                         static_cast<unsigned long>(parsed.packet_info.checksum),
                         static_cast<unsigned>(parsed.subtype));
                return false;
            }
            return true;
        }

        case msg_battery_types_fragment:
            return validate_type_catalog_fragment_shape(msg, "BATTERY_TYPES_FRAGMENT");
        case msg_inverter_types_fragment:
            return validate_type_catalog_fragment_shape(msg, "INVERTER_TYPES_FRAGMENT");
        case msg_inverter_interfaces_fragment:
            return validate_type_catalog_fragment_shape(msg, "INVERTER_INTERFACES_FRAGMENT");

        default:
            return true;
    }
}

bool dispatch_ingress_message(const IngressParseResult& parsed) {
    if (!parsed.msg) {
        return false;
    }

    return EspnowMessageRouter::instance().route_message(*parsed.msg);
}

bool process_ingress_message(const espnow_queue_msg_t& msg) {
    IngressParseResult parsed{};
    if (!parse_ingress_message(msg, parsed)) {
        return false;
    }

    if (!validate_ingress_message(parsed)) {
        return false;
    }

    if (!dispatch_ingress_message(parsed)) {
        LOG_WARN(kLogTag, "No route for ingress type=%u (%s) len=%d",
                 static_cast<unsigned>(parsed.type),
                 message_type_name(parsed.type),
                 msg.len);
        return false;
    }

    return true;
}

}  // namespace ESPNowRuntime::Detail
