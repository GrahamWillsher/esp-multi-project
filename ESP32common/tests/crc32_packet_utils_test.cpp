#include <unity.h>
#include <esp32common/espnow/packet_utils.h>

namespace {

struct __attribute__((packed)) BlobWithTrailingCrc32 {
    uint16_t field_a;
    uint8_t field_b;
    uint32_t version;
    uint32_t crc32;
};

}  // namespace

void test_crc32_packet_matches_standard_vector(void) {
    static constexpr char kPayload[] = "123456789";
    const uint32_t actual = EspnowPacketUtils::crc32_packet(kPayload, sizeof(kPayload) - 1);
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, actual);
}

void test_message_crc32_zeroed_round_trip(void) {
    BlobWithTrailingCrc32 blob{};
    blob.field_a = 0x1234;
    blob.field_b = 0x56;
    blob.version = 0xABCDEF01u;
    blob.crc32 = EspnowPacketUtils::calculate_message_crc32_zeroed(&blob);

    TEST_ASSERT_TRUE(EspnowPacketUtils::verify_message_crc32(&blob));
}

void test_message_crc32_detects_mutation(void) {
    BlobWithTrailingCrc32 blob{};
    blob.field_a = 100;
    blob.field_b = 7;
    blob.version = 42;
    blob.crc32 = EspnowPacketUtils::calculate_message_crc32_zeroed(&blob);

    blob.field_b = 8;

    TEST_ASSERT_FALSE(EspnowPacketUtils::verify_message_crc32(&blob));
}

void run_crc32_packet_utils_tests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_crc32_packet_matches_standard_vector);
    RUN_TEST(test_message_crc32_zeroed_round_trip);
    RUN_TEST(test_message_crc32_detects_mutation);
    UNITY_END();
}
