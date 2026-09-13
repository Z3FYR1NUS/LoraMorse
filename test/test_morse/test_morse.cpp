#include <unity.h>

#include "morse/Morse.h"

void setUp() {}
void tearDown() {}

static morse::Marks marksOf(const char* pattern) {
    morse::Marks m;
    for (const char* p = pattern; *p; ++p) m.append(*p);
    return m;
}

void test_decodes_common_letters() {
    TEST_ASSERT_EQUAL_CHAR('E', morse::decode(marksOf(".")));
    TEST_ASSERT_EQUAL_CHAR('T', morse::decode(marksOf("-")));
    TEST_ASSERT_EQUAL_CHAR('A', morse::decode(marksOf(".-")));
    TEST_ASSERT_EQUAL_CHAR('N', morse::decode(marksOf("-.")));
    TEST_ASSERT_EQUAL_CHAR('S', morse::decode(marksOf("...")));
    TEST_ASSERT_EQUAL_CHAR('O', morse::decode(marksOf("---")));
    TEST_ASSERT_EQUAL_CHAR('Q', morse::decode(marksOf("--.-")));
}

void test_decodes_digits() {
    TEST_ASSERT_EQUAL_CHAR('5', morse::decode(marksOf(".....")));
    TEST_ASSERT_EQUAL_CHAR('0', morse::decode(marksOf("-----")));
    TEST_ASSERT_EQUAL_CHAR('1', morse::decode(marksOf(".----")));
}

void test_empty_is_unknown() {
    morse::Marks empty;
    TEST_ASSERT_EQUAL_CHAR('?', morse::decode(empty));
}

void test_overflowed_is_unknown() {
    morse::Marks m = marksOf("......");  // fills MAX_MARKS
    m.append('.');                       // triggers overflow
    TEST_ASSERT_TRUE(m.overflow);
    TEST_ASSERT_EQUAL_CHAR('?', morse::decode(m));
}

void test_append_stops_at_max_marks() {
    morse::Marks m;
    for (int i = 0; i < morse::MAX_MARKS + 3; ++i) m.append('.');
    TEST_ASSERT_EQUAL_UINT8(morse::MAX_MARKS, m.length);
    TEST_ASSERT_TRUE(m.overflow);
}

void test_clear_resets_state() {
    morse::Marks m = marksOf("...");
    m.clear();
    TEST_ASSERT_EQUAL_UINT8(0, m.length);
    TEST_ASSERT_FALSE(m.overflow);
    TEST_ASSERT_EQUAL_STRING("", m.text);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_decodes_common_letters);
    RUN_TEST(test_decodes_digits);
    RUN_TEST(test_empty_is_unknown);
    RUN_TEST(test_overflowed_is_unknown);
    RUN_TEST(test_append_stops_at_max_marks);
    RUN_TEST(test_clear_resets_state);
    return UNITY_END();
}
