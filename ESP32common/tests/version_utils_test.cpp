/**
 * @file version_utils_test.cpp
 * @brief Unit tests for firmware version utilities
 * 
 * Tests version comparison logic including wraparound handling,
 * monotonic increment validation, and compatibility checking.
 * 
 * Phase 2.5: Pre-production hardening tests
 */

#include <unity.h>
#include <firmware_version.h>

// Test fixture setup/teardown
void setUp(void) {
    // Run before each test
}

void tearDown(void) {
    // Run after each test
}

// ═══════════════════════════════════════════════════════════════════════
// Test: is_version_newer() - Basic monotonic increment
// ═══════════════════════════════════════════════════════════════════════

void test_version_newer_basic_increment(void) {
    // Simple increment: 1 -> 2 should be newer
    TEST_ASSERT_TRUE(is_version_newer(2, 1));
    
    // Reverse: 1 -> 2 should NOT be newer (2 is older than 1)
    TEST_ASSERT_FALSE(is_version_newer(1, 2));
    
    // Equal versions: Should not be newer
    TEST_ASSERT_FALSE(is_version_newer(100, 100));
}

// ═══════════════════════════════════════════════════════════════════════
// Test: is_version_newer() - Large gaps (but not wraparound)
// ═══════════════════════════════════════════════════════════════════════

void test_version_newer_large_gap(void) {
    // Large forward jump (within safe range)
    TEST_ASSERT_TRUE(is_version_newer(1000, 1));
    
    // Very large forward jump (but still < UINT32_MAX/2)
    TEST_ASSERT_TRUE(is_version_newer(1000000, 1));
    
    // Near the wraparound threshold but not wrapped
    // Gap of 2^31 - 1 is the maximum "newer" distance
    TEST_ASSERT_TRUE(is_version_newer(0x7FFFFFFF, 0));
}

// ═══════════════════════════════════════════════════════════════════════
// Test: is_version_newer() - Wraparound detection
// ═══════════════════════════════════════════════════════════════════════

void test_version_newer_wraparound(void) {
    // Wraparound case: 0xFFFFFFFF -> 0
    TEST_ASSERT_TRUE(is_version_newer(0, 0xFFFFFFFF));
    
    // Wraparound case: 0xFFFFFFFF -> 1
    TEST_ASSERT_TRUE(is_version_newer(1, 0xFFFFFFFF));
    
    // Wraparound case: 0xFFFFFFFF -> 100
    TEST_ASSERT_TRUE(is_version_newer(100, 0xFFFFFFFF));
    
    // NOT wraparound: Gap is too large (> UINT32_MAX/2)
    // This should be treated as "old version is actually newer"
    TEST_ASSERT_FALSE(is_version_newer(0, 0x80000001));
}

// ═══════════════════════════════════════════════════════════════════════
// Test: is_version_newer() - Edge cases at wraparound boundary
// ═══════════════════════════════════════════════════════════════════════

void test_version_newer_wraparound_edge_cases(void) {
    // Maximum safe forward gap (2^31 - 1)
    TEST_ASSERT_TRUE(is_version_newer(0x7FFFFFFF, 0));
    
    // Exactly at boundary (2^31): Should be treated as backward (older)
    TEST_ASSERT_FALSE(is_version_newer(0x80000000, 0));
    
    // Just past boundary: Should be older
    TEST_ASSERT_FALSE(is_version_newer(0x80000001, 0));
    
    // Wraparound near max value
    TEST_ASSERT_TRUE(is_version_newer(10, 0xFFFFFFF0));  // 16 -> 10 (wrapped)
}

// ═══════════════════════════════════════════════════════════════════════
// Test: is_version_newer() - Sequential increments across wraparound
// ═══════════════════════════════════════════════════════════════════════

void test_version_newer_sequential_wraparound(void) {
    // Simulate version incrementing near wraparound
    uint32_t versions[] = {
        0xFFFFFFFD,  // -3
        0xFFFFFFFE,  // -2
        0xFFFFFFFF,  // -1
        0x00000000,  // Wrapped to 0
        0x00000001,  // +1
        0x00000002   // +2
    };
    
    // Each version should be newer than the previous
    for (size_t i = 1; i < sizeof(versions)/sizeof(versions[0]); i++) {
        TEST_ASSERT_TRUE_MESSAGE(
            is_version_newer(versions[i], versions[i-1]),
            "Sequential version increment should always be detected as newer"
        );
    }
}

// ═══════════════════════════════════════════════════════════════════════
// Test: is_version_newer() - Backwards time travel detection
// ═══════════════════════════════════════════════════════════════════════

void test_version_newer_backwards_detection(void) {
    // Large backward jump should be detected as NOT newer
    TEST_ASSERT_FALSE(is_version_newer(1, 1000));
    
    // Backward jump near wraparound boundary
    TEST_ASSERT_FALSE(is_version_newer(0xFFFFFFFF, 100));
    
    // Backward wrap (simulate clock going backwards)
    TEST_ASSERT_FALSE(is_version_newer(0xFFFFFFFE, 10));
}

// ═══════════════════════════════════════════════════════════════════════
// Test: isVersionCompatible() - Range-based compatibility
// ═══════════════════════════════════════════════════════════════════════

void test_version_compatible_within_range(void) {
    // v1.5.0 compatible with range [1.0.0 - 1.99.99]
    TEST_ASSERT_TRUE(isVersionCompatible(10500, 10000, 19999));
    
    // v1.0.0 at lower boundary
    TEST_ASSERT_TRUE(isVersionCompatible(10000, 10000, 19999));
    
    // v1.99.99 at upper boundary
    TEST_ASSERT_TRUE(isVersionCompatible(19999, 10000, 19999));
}

void test_version_compatible_outside_range(void) {
    // v2.0.0 above max range [1.0.0 - 1.99.99]
    TEST_ASSERT_FALSE(isVersionCompatible(20000, 10000, 19999));
    
    // v0.9.99 below min range [1.0.0 - 1.99.99]
    TEST_ASSERT_FALSE(isVersionCompatible(9999, 10000, 19999));
}

void test_version_compatible_exact_match(void) {
    // Exact version match should always be compatible
    TEST_ASSERT_TRUE(isVersionCompatible(10500, 10500, 10500));
}

// ═══════════════════════════════════════════════════════════════════════
// Test: VersionCompatibility struct - Practical use cases
// ═══════════════════════════════════════════════════════════════════════

void test_version_compatibility_practical(void) {
    // Receiver v1.5.0 requires transmitter [1.0.0 - 1.99.99]
    VersionCompatibility receiver_compat;
    receiver_compat.my_version = 10500;
    receiver_compat.min_peer_version = 10000;
    receiver_compat.max_peer_version = 19999;
    
    // Transmitter v1.3.0 should be compatible
    TEST_ASSERT_TRUE(isVersionCompatible(13000, 
                                         receiver_compat.min_peer_version,
                                         receiver_compat.max_peer_version));
    
    // Transmitter v2.0.0 should NOT be compatible (too new)
    TEST_ASSERT_FALSE(isVersionCompatible(20000,
                                          receiver_compat.min_peer_version,
                                          receiver_compat.max_peer_version));
    
    // Transmitter v0.9.0 should NOT be compatible (too old)
    TEST_ASSERT_FALSE(isVersionCompatible(9000,
                                          receiver_compat.min_peer_version,
                                          receiver_compat.max_peer_version));
}

// ═══════════════════════════════════════════════════════════════════════
// Test: Edge case - Zero versions
// ═══════════════════════════════════════════════════════════════════════

void test_version_zero_handling(void) {
    // 0 -> 0 should not be newer
    TEST_ASSERT_FALSE(is_version_newer(0, 0));
    
    // 1 -> 0 should be newer
    TEST_ASSERT_TRUE(is_version_newer(1, 0));
    
    // 0 -> 1 should NOT be newer (going backwards)
    TEST_ASSERT_FALSE(is_version_newer(0, 1));
}

// ═══════════════════════════════════════════════════════════════════════
// Main test runner
// ═══════════════════════════════════════════════════════════════════════

void run_version_utils_tests(void) {
    UNITY_BEGIN();
    
    // Basic functionality
    RUN_TEST(test_version_newer_basic_increment);
    RUN_TEST(test_version_newer_large_gap);
    
    // Wraparound handling
    RUN_TEST(test_version_newer_wraparound);
    RUN_TEST(test_version_newer_wraparound_edge_cases);
    RUN_TEST(test_version_newer_sequential_wraparound);
    RUN_TEST(test_version_newer_backwards_detection);
    
    // Compatibility checking
    RUN_TEST(test_version_compatible_within_range);
    RUN_TEST(test_version_compatible_outside_range);
    RUN_TEST(test_version_compatible_exact_match);
    RUN_TEST(test_version_compatibility_practical);
    
    // Edge cases
    RUN_TEST(test_version_zero_handling);
    
    UNITY_END();
}

// Entry point for native testing
#ifdef UNIT_TEST
int main(int argc, char **argv) {
    run_version_utils_tests();
    return 0;
}
#endif
