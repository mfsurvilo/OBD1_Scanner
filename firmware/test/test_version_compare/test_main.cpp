// Native unit tests for the pure version-ordering helper
// (include/version_compare.h) — the logic that picks the highest release
// version. This is the exact bug class that broke OTA once: relying on
// lexical/date order instead of numeric version order.
#include <unity.h>
#include "version_compare.h"

void setUp(void) {}
void tearDown(void) {}

void test_minor_ordering(void) {
    TEST_ASSERT_TRUE(version_key("v0.4") > version_key("v0.3"));
}

void test_double_digit_minor(void) {
    // v0.10 must outrank v0.9 (a naive string compare gets this wrong).
    TEST_ASSERT_TRUE(version_key("v0.10") > version_key("v0.9"));
}

void test_major_beats_minor(void) {
    TEST_ASSERT_TRUE(version_key("v1.0") > version_key("v0.99"));
}

void test_patch_ordering(void) {
    TEST_ASSERT_TRUE(version_key("v0.3.1") > version_key("v0.3.0"));
    // A missing patch component is treated as .0.
    TEST_ASSERT_EQUAL(version_key("v0.3"), version_key("v0.3.0"));
}

void test_v_prefix_optional(void) {
    TEST_ASSERT_EQUAL(version_key("v0.4"), version_key("0.4"));
}

void test_garbage_is_lowest_ranked(void) {
    // Non-versions sort below any real version, and null is < everything.
    TEST_ASSERT_TRUE(version_key("v0.1") > version_key("nope"));
    TEST_ASSERT_TRUE(version_key(nullptr) < 0);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_minor_ordering);
    RUN_TEST(test_double_digit_minor);
    RUN_TEST(test_major_beats_minor);
    RUN_TEST(test_patch_ordering);
    RUN_TEST(test_v_prefix_optional);
    RUN_TEST(test_garbage_is_lowest_ranked);
    return UNITY_END();
}
