#include <unity.h>

#include "protocol/ReplayGuard.h"

void setUp() {}
void tearDown() {}

void test_first_packet_from_peer_is_new() {
    ReplayGuard guard;
    TEST_ASSERT_TRUE(guard.check(1, 1) == ReplayGuard::Verdict::NEW);
}

void test_increasing_sequence_is_new() {
    ReplayGuard guard;
    guard.check(1, 5);
    TEST_ASSERT_TRUE(guard.check(1, 6) == ReplayGuard::Verdict::NEW);
    TEST_ASSERT_TRUE(guard.check(1, 100) == ReplayGuard::Verdict::NEW);
}

void test_repeated_sequence_is_duplicate() {
    ReplayGuard guard;
    guard.check(1, 10);
    TEST_ASSERT_TRUE(guard.check(1, 10) == ReplayGuard::Verdict::DUPLICATE);
}

void test_older_sequence_is_replay() {
    ReplayGuard guard;
    guard.check(1, 10);
    guard.check(1, 11);
    TEST_ASSERT_TRUE(guard.check(1, 10) == ReplayGuard::Verdict::REPLAY);
    TEST_ASSERT_TRUE(guard.check(1, 1) == ReplayGuard::Verdict::REPLAY);
}

void test_peers_are_tracked_independently() {
    ReplayGuard guard;
    guard.check(1, 50);
    // A different peer starting at a low sequence number is still NEW --
    // peer 1's history must not affect peer 2.
    TEST_ASSERT_TRUE(guard.check(2, 1) == ReplayGuard::Verdict::NEW);
    TEST_ASSERT_TRUE(guard.check(1, 51) == ReplayGuard::Verdict::NEW);
}

void test_table_full_rejects_new_unknown_peer() {
    ReplayGuard guard;
    for (uint8_t peer = 1; peer <= ReplayGuard::MAX_PEERS; ++peer) {
        TEST_ASSERT_TRUE(guard.check(peer, 1) == ReplayGuard::Verdict::NEW);
    }
    // One more distinct peer than the table has room for.
    TEST_ASSERT_TRUE(guard.check(uint8_t(ReplayGuard::MAX_PEERS + 1), 1) ==
                      ReplayGuard::Verdict::TABLE_FULL);
}

void test_reset_clears_history() {
    ReplayGuard guard;
    guard.check(1, 99);
    guard.reset();
    TEST_ASSERT_TRUE(guard.check(1, 1) == ReplayGuard::Verdict::NEW);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_first_packet_from_peer_is_new);
    RUN_TEST(test_increasing_sequence_is_new);
    RUN_TEST(test_repeated_sequence_is_duplicate);
    RUN_TEST(test_older_sequence_is_replay);
    RUN_TEST(test_peers_are_tracked_independently);
    RUN_TEST(test_table_full_rejects_new_unknown_peer);
    RUN_TEST(test_reset_clears_history);
    return UNITY_END();
}
