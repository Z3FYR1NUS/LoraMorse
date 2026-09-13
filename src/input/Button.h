#pragma once
// Button -- debounced, active-low digital input. Unchanged logic from the
// original sketch, just pulled into its own module.

#include <stdint.h>

class Button {
public:
    explicit Button(int gpio) : pin_(gpio) {}

    void begin(uint32_t now);
    void update(uint32_t now);

    int pin() const { return pin_; }
    bool down() const { return down_; }
    bool raw() const { return raw_; }
    bool pressed() const { return pressed_; }
    bool released() const { return released_; }
    uint32_t edgeAt() const { return edgeAt_; }

private:
    int pin_;
    bool raw_ = false, down_ = false;
    bool pressed_ = false, released_ = false;
    uint32_t rawAt_ = 0, edgeAt_ = 0;
};
