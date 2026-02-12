#ifndef ESPNOW_PACKET_UTILS_H
#define ESPNOW_PACKET_UTILS_H

#include <Arduino.h>
#include <espnow_common.h>

namespace EspnowPacketUtils {

    /**
     * @brief Structure containing extracted packet metadata and payload pointer
     */
    struct PacketInfo {
        uint32_t seq;              // Unique sequence ID
        uint16_t frag_index;       // Fragment index (0-based)
        uint16_t frag_total;       // Total fragment count
        uint16_t payload_len;      // Length of actual data in payload
        uint8_t subtype;           // Packet subtype
        uint16_t checksum;         // Payload checksum
        const uint8_t* payload;    // Pointer to payload data
        
        PacketInfo() : seq(0), frag_index(0), frag_total(0), 
                      payload_len(0), subtype(0), checksum(0), payload(nullptr) {}
    };

    /**
     * @brief Validate that message contains a valid packet structure
     * 
     * @param msg Queue message to validate
     * @return true if message is large enough to contain espnow_packet_t header
     */
    inline bool validate_packet(const espnow_queue_msg_t* msg) {
        if (!msg) return false;
        return msg->len >= (int)sizeof(espnow_packet_t);
    }

    /**
     * @brief Extract packet metadata and payload pointer from queue message
     * 
     * This function safely extracts all packet fields and provides a pointer
     * to the payload data. The caller is responsible for validating the
     * payload length against expected size for the subtype.
     * 
     * @param msg Queue message containing packet data
     * @param info [out] PacketInfo structure to populate
     * @return true if packet structure is valid and info was populated
     */
    inline bool get_packet_info(const espnow_queue_msg_t* msg, PacketInfo& info) {
        if (!validate_packet(msg)) {
            return false;
        }
        
        const espnow_packet_t* pkt = reinterpret_cast<const espnow_packet_t*>(msg->data);
        
        info.seq = pkt->seq;
        info.frag_index = pkt->frag_index;
        info.frag_total = pkt->frag_total;
        info.payload_len = pkt->payload_len;
        info.subtype = pkt->subtype;
        info.checksum = pkt->checksum;
        info.payload = pkt->payload;
        
        return true;
    }

    /**
     * @brief Calculate simple checksum for packet payload
     * 
     * @param payload Pointer to payload data
     * @param len Length of payload
     * @return uint16_t Calculated checksum
     */
    inline uint16_t calculate_checksum(const uint8_t* payload, uint16_t len) {
        if (!payload || len == 0) return 0;
        
        uint16_t checksum = 0;
        for (uint16_t i = 0; i < len; i++) {
            checksum += payload[i];
        }
        return checksum;
    }

    /**
     * @brief Validate packet payload checksum
     * 
     * @param info PacketInfo containing payload and stored checksum
     * @return true if calculated checksum matches stored checksum
     */
    inline bool validate_checksum(const PacketInfo& info) {
        if (!info.payload || info.payload_len == 0) return false;
        
        uint16_t calculated = calculate_checksum(info.payload, info.payload_len);
        return calculated == info.checksum;
    }

    /**
     * @brief Print packet info to serial for debugging
     * 
     * @param info PacketInfo to print
     * @param subtype_name Optional human-readable subtype name
     */
    inline void print_packet_info(const PacketInfo& info, const char* subtype_name = nullptr) {
        if (subtype_name) {
            Serial.printf("[PACKET] %s: seq=%u, frag=%u/%u, len=%u, checksum=0x%04X\n",
                         subtype_name, info.seq, info.frag_index, info.frag_total, 
                         info.payload_len, info.checksum);
        } else {
            Serial.printf("[PACKET] subtype=%u: seq=%u, frag=%u/%u, len=%u, checksum=0x%04X\n",
                         info.subtype, info.seq, info.frag_index, info.frag_total, 
                         info.payload_len, info.checksum);
        }
    }

    /**
     * @brief Check if packet is a single-fragment message (not fragmented)
     * 
     * @param info PacketInfo to check
     * @return true if this is the only fragment (or fragment 0 of 1)
     */
    inline bool is_single_fragment(const PacketInfo& info) {
        return info.frag_total == 1 || (info.frag_index == 0 && info.frag_total == 0);
    }

    /**
     * @brief Check if this is the first fragment of a multi-fragment message
     * 
     * @param info PacketInfo to check
     * @return true if frag_index is 0 and frag_total > 1
     */
    inline bool is_first_fragment(const PacketInfo& info) {
        return info.frag_index == 0 && info.frag_total > 1;
    }

    /**
     * @brief Check if this is the last fragment of a multi-fragment message
     * 
     * @param info PacketInfo to check
     * @return true if frag_index is frag_total - 1
     */
    inline bool is_last_fragment(const PacketInfo& info) {
        if (info.frag_total == 0) return false;
        return info.frag_index == (info.frag_total - 1);
    }

    // =========================================================================
    // Common Checksum Utilities (used by settings sync, etc.)
    // =========================================================================

    /**
     * @brief Calculate XOR checksum for a message structure
     * 
     * Calculates XOR checksum over all bytes except the checksum field itself.
     * Template handles any message type with a uint16_t checksum field.
     * 
     * @tparam T Message structure type (must have uint16_t checksum field)
     * @param message Pointer to message structure
     * @return uint16_t Calculated checksum value
     */
    template<typename T>
    inline uint16_t calculate_message_checksum(const T* message) {
        uint16_t checksum = 0;
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(message);
        // XOR all bytes except the checksum field (last 2 bytes for uint16_t)
        for (size_t i = 0; i < sizeof(T) - sizeof(uint16_t); i++) {
            checksum ^= bytes[i];
        }
        return checksum;
    }

    /**
     * @brief Verify XOR checksum for a message structure
     * 
     * Recalculates checksum and compares with message's checksum field.
     * 
     * @tparam T Message structure type (must have uint16_t checksum field)
     * @param message Pointer to message structure
     * @return true if calculated checksum matches message checksum
     */
    template<typename T>
    inline bool verify_message_checksum(const T* message) {
        uint16_t calculated = calculate_message_checksum(message);
        // Assume checksum is the last field (standard convention)
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(message);
        const uint16_t* stored_checksum = reinterpret_cast<const uint16_t*>(bytes + sizeof(T) - sizeof(uint16_t));
        return calculated == *stored_checksum;
    }

} // namespace EspnowPacketUtils

#endif // ESPNOW_PACKET_UTILS_H
