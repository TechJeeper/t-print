#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#include "printer_types.h"

namespace DisplayUi {
void begin();
void loop();
void showSetupHint(const char* line1, const char* line2, const char* line3);
void showLanIp(const char* ip);
void invalidate();

// Capture current framebuffer as 24-bit BMP into malloc'd buffer (caller frees).
// Returns false if capture failed.
bool captureScreenshotBmp(uint8_t** outBuf, size_t* outLen);
}  // namespace DisplayUi
