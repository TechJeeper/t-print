#pragma once

namespace StatusAlert {
void begin();
void loop();      // drive WS2812 + optional error beep from fleet state
void shutdown();  // blank LEDs before deep sleep
}  // namespace StatusAlert
