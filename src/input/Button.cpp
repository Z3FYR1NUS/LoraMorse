#include "Button.h"

#include <Arduino.h>

#include "config.h"

void Button::begin(uint32_t now) {
    raw_ = down_ = (digitalRead(pin_) == LOW);
    rawAt_ = edgeAt_ = now;
    pressed_ = released_ = false;
}

void Button::update(uint32_t now) {
    pressed_ = released_ = false;
    const bool sample = digitalRead(pin_) == LOW;
    if (sample != raw_) {
        raw_ = sample;
        rawAt_ = now;
    }
    if (down_ != raw_ && uint32_t(now - rawAt_) >= timing::DEBOUNCE_MS) {
        down_ = raw_;
        edgeAt_ = rawAt_;  // Measure the observed edge, not the debounce delay.
        pressed_ = down_;
        released_ = !down_;
    }
}
