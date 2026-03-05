#include <Arduino.h>
#include <unity.h>

#include "../../lib/receiver_config/receiver_config_manager.h"

void test_validate_port_rejects_zero() {
    auto result = ReceiverNetworkConfig::validatePort(0);
    TEST_ASSERT_FALSE(result.valid);
}

void test_validate_port_accepts_1883() {
    auto result = ReceiverNetworkConfig::validatePort(1883);
    TEST_ASSERT_TRUE(result.valid);
}

void test_validate_ip_rejects_all_zero() {
    const uint8_t ip[4] = {0, 0, 0, 0};
    auto result = ReceiverNetworkConfig::validateIPAddress(ip);
    TEST_ASSERT_FALSE(result.valid);
}

void test_validate_ip_accepts_private_lan() {
    const uint8_t ip[4] = {192, 168, 1, 10};
    auto result = ReceiverNetworkConfig::validateIPAddress(ip);
    TEST_ASSERT_TRUE(result.valid);
}

void test_validate_interface_rejects_out_of_range() {
    auto result = ReceiverNetworkConfig::validateInterface(6);
    TEST_ASSERT_FALSE(result.valid);
}

void test_validate_interface_accepts_can_native() {
    auto result = ReceiverNetworkConfig::validateInterface(2);
    TEST_ASSERT_TRUE(result.valid);
}

void setup() {
    UNITY_BEGIN();
    RUN_TEST(test_validate_port_rejects_zero);
    RUN_TEST(test_validate_port_accepts_1883);
    RUN_TEST(test_validate_ip_rejects_all_zero);
    RUN_TEST(test_validate_ip_accepts_private_lan);
    RUN_TEST(test_validate_interface_rejects_out_of_range);
    RUN_TEST(test_validate_interface_accepts_can_native);
    UNITY_END();
}

void loop() {
    // Unity tests are run once in setup()
}
