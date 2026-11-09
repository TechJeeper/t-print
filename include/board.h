#pragma once

#include <Arduino.h>

namespace Board {
void begin();
void setBacklight(uint8_t percent);
uint8_t batteryPercent();
bool batteryCharging();
void setStatusLed(uint8_t r, uint8_t g, uint8_t b);
void tickEncoder();
uint32_t lastInputMs();  // millis() of last encoder activity (not top-button sleep)
int encoderDelta();
bool encoderPressed();
bool encoderClicked();       // short press release
bool encoderLongPressed();   // hold ~0.7s (fires once while held)
bool userKeyPressed();
bool userKeyClicked();       // top button short press
bool userKeyLongPressed();   // top button hold ~1s (fires once while held)
bool displayForcedOff();     // LCD off / LEDs dimmed via top button
void setDisplayForcedOff(bool off);
void enterDeepSleep();       // power down; wake on top button
}  // namespace Board
