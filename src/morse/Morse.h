#pragma once
// Morse -- mark buffer and dot/dash -> letter decoding.
//
// Pulled out of the original monolithic sketch unchanged in behavior (same
// binary decode tree), but with no Arduino dependency so it can be unit
// tested natively.

#include <stdint.h>

namespace morse {

constexpr uint8_t MAX_MARKS = 6;

struct Marks {
    char text[MAX_MARKS + 1] = "";
    uint8_t length = 0;
    bool overflow = false;

    void clear() {
        length = 0;
        text[0] = '\0';
        overflow = false;
    }
    void append(char mark) {
        if (length == MAX_MARKS) {
            overflow = true;
            return;
        }
        text[length++] = mark;
        text[length] = '\0';
    }
};

// Decodes a completed mark sequence ('.'/'-') into a letter/digit using a
// binary tree (index 1 = root; '.' -> 2*i, '-' -> 2*i+1). Returns '?' for an
// empty, overflowed, or otherwise unrecognized sequence.
char decode(const Marks& marks);

}  // namespace morse
