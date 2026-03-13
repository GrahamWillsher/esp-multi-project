#include <Arduino.h>
#include <unity.h>

#include "../../src/helpers.h"

void test_gradient_endpoints_match_start_and_end() {
    uint16_t gradient[11] = {0};
    const uint16_t start = TFT_RED;
    const uint16_t end = TFT_GREEN;

    pre_calculate_color_gradient(start, end, 10, gradient);

    TEST_ASSERT_EQUAL_HEX16(start, gradient[0]);
    TEST_ASSERT_EQUAL_HEX16(end, gradient[10]);
}

void test_calculate_checksum_uses_soc_plus_power_low16() {
    espnow_payload_t payload = {};
    payload.soc = 80;
    payload.power = -100;

    // Function behavior: uint16_t sum = soc + (uint16_t)power
    uint16_t expected = static_cast<uint16_t>(80 + static_cast<uint16_t>(-100));
    uint16_t actual = calculate_checksum(&payload);

    TEST_ASSERT_EQUAL_HEX16(expected, actual);
}

void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_gradient_endpoints_match_start_and_end);
    RUN_TEST(test_calculate_checksum_uses_soc_plus_power_low16);
    UNITY_END();
}

void loop() {
    // Unity tests are run once in setup()
}
