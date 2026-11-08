#pragma once

#include <Arduino.h>

namespace Board {
void begin();
void setBacklight(uint8_t percent);
uint8_t batteryPercent();
bool batteryCharging();
void setStatusLed(uint8_t r, uint8_t g, uint8_t b);
void tickEncoder();
int encoderDelta();
bool encoderPressed();
bool encoderClicked();       // short press release
bool encoderLongPressed();   // hold ~0.7s (fires once while held)
bool userKeyPressed();
bool userKeyClicked();  // edge-triggered
}  // namespace Board
