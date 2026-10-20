#include <unity.h>

#include "security/attempt_guard.h"

using namespace cv;

void setUp() {}
void tearDown() {}

void test_delay_schedule() {
    TEST_ASSERT_EQUAL(0, attempt_delay_ms(0));
    TEST_ASSERT_EQUAL(0, attempt_delay_ms(2));
    TEST_ASSERT_EQUAL(5000, attempt_delay_ms(3));
    TEST_ASSERT_EQUAL(60000, attempt_delay_ms(6));
    TEST_ASSERT_EQUAL(300000, attempt_delay_ms(8));
    TEST_ASSERT_EQUAL(300000, attempt_delay_ms(9));
    TEST_ASSERT_EQUAL(300000, attempt_delay_ms(100));
}

void test_counts_before_check_and_resets_on_success() {
    MemoryKv kv;
    AttemptGuard g(kv);
    TEST_ASSERT_EQUAL(0, g.failures());
    TEST_ASSERT_TRUE(g.begin_attempt());
    TEST_ASSERT_EQUAL(1, g.failures());
    TEST_ASSERT_EQUAL(9, g.remaining());
    // Simulated reboot: a new guard reads the persisted value.
    AttemptGuard g2(kv);
    TEST_ASSERT_EQUAL(1, g2.failures());
    g2.record_success();
    TEST_ASSERT_EQUAL(0, g.failures());
}

void test_exhaustion_after_ten() {
    MemoryKv kv;
    AttemptGuard g(kv);
    for (int i = 0; i < 10; i++) TEST_ASSERT_TRUE(g.begin_attempt());
    TEST_ASSERT_TRUE(g.exhausted());
    TEST_ASSERT_EQUAL(0, g.remaining());
    TEST_ASSERT_FALSE(g.begin_attempt());
    g.reset();
    TEST_ASSERT_FALSE(g.exhausted());
}

void test_refuses_when_counter_cannot_persist() {
    MemoryKv kv;
    kv.fail_writes = true;
    AttemptGuard g(kv);
    TEST_ASSERT_FALSE(g.begin_attempt());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_delay_schedule);
    RUN_TEST(test_counts_before_check_and_resets_on_success);
    RUN_TEST(test_exhaustion_after_ten);
    RUN_TEST(test_refuses_when_counter_cannot_persist);
    return UNITY_END();
}
