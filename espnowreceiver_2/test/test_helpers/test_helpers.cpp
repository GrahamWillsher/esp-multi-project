#include <Arduino.h>
#include <unity.h>

#include "../../src/helpers.h"
#include <esp32common/espnow/packet_utils.h>

void test_gradient_endpoints_match_start_and_end() {
    uint16_t gradient[11] = {0};
    const uint16_t start = TFT_RED;
    const uint16_t end = TFT_GREEN;

    pre_calculate_color_gradient(start, end, 10, gradient);

    TEST_ASSERT_EQUAL_HEX16(start, gradient[0]);
    TEST_ASSERT_EQUAL_HEX16(end, gradient[10]);
}

void test_payload_crc32_round_trip() {
    espnow_payload_t payload = {};
    payload.type = msg_data;
    payload.soc = 80;
    payload.power = -100;
    payload.checksum = EspnowPacketUtils::calculate_message_crc32_zeroed(&payload);

    TEST_ASSERT_TRUE(EspnowPacketUtils::verify_message_crc32(&payload));
}

void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_gradient_endpoints_match_start_and_end);
    RUN_TEST(test_payload_crc32_round_trip);
    UNITY_END();
}

void loop() {
    // Unity tests are run once in setup()
}
