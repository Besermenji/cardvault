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

void test_missing_counter_fails_closed() {
    MemoryKv kv;  // nothing stored: NVS erased or never written
    AttemptGuard g(kv);
    TEST_ASSERT_EQUAL(kFailuresIfMissing, g.failures());
    TEST_ASSERT_EQUAL(kMaxAttempts - kFailuresIfMissing, g.remaining());
    TEST_ASSERT_FALSE(g.exhausted());
    // The first attempt continues from the penalty, not from zero.
    TEST_ASSERT_TRUE(g.begin_attempt());
    TEST_ASSERT_EQUAL(kFailuresIfMissing + 1, g.failures());
    // Setup / a successful unlock writes a real value.
    TEST_ASSERT_TRUE(g.reset());
    TEST_ASSERT_EQUAL(0, g.failures());
}

void test_missing_counter_cannot_be_refunded_below_penalty() {
    MemoryKv kv;
    AttemptGuard g(kv);
    g.refund();
    TEST_ASSERT_EQUAL(kFailuresIfMissing - 1, g.failures());
}

void test_counts_before_check_and_resets_on_success() {
    MemoryKv kv;
    AttemptGuard g(kv);
    g.reset();
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
    g.reset();
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

void test_refund_undoes_one_attempt() {
    MemoryKv kv;
    AttemptGuard g(kv);
    g.reset();
    g.refund();
    TEST_ASSERT_EQUAL(0, g.failures());
    g.begin_attempt();
    g.begin_attempt();
    g.refund();
    TEST_ASSERT_EQUAL(1, g.failures());
}

void test_mark_exhausted_persists_exhausted_state() {
    MemoryKv kv;
    AttemptGuard g(kv);
    TEST_ASSERT_TRUE(g.mark_exhausted());
    TEST_ASSERT_TRUE(g.exhausted());
    TEST_ASSERT_EQUAL(0, g.remaining());
    TEST_ASSERT_FALSE(g.begin_attempt());
    kv.fail_writes = true;
    TEST_ASSERT_FALSE(g.reset());
}

void test_gate_waits_until_armed_delay_passes() {
    AttemptGate gate;
    TEST_ASSERT_EQUAL(0, gate.wait_ms(1234));
    gate.arm(1000, 5000);
    TEST_ASSERT_EQUAL(5000, gate.wait_ms(1000));
    TEST_ASSERT_EQUAL(1, gate.wait_ms(5999));
    TEST_ASSERT_EQUAL(0, gate.wait_ms(6000));
    TEST_ASSERT_EQUAL(0, gate.wait_ms(90000));
    // millis() wraps after ~49 days.
    gate.arm(0xFFFFF000u, 0x2000);
    TEST_ASSERT_EQUAL(0x1000, gate.wait_ms(0));
    TEST_ASSERT_EQUAL(0, gate.wait_ms(0x1000));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_delay_schedule);
    RUN_TEST(test_missing_counter_fails_closed);
    RUN_TEST(test_missing_counter_cannot_be_refunded_below_penalty);
    RUN_TEST(test_counts_before_check_and_resets_on_success);
    RUN_TEST(test_exhaustion_after_ten);
    RUN_TEST(test_refuses_when_counter_cannot_persist);
    RUN_TEST(test_refund_undoes_one_attempt);
    RUN_TEST(test_gate_waits_until_armed_delay_passes);
    RUN_TEST(test_mark_exhausted_persists_exhausted_state);
    return UNITY_END();
}
