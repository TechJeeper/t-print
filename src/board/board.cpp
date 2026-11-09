#include "board.h"

#include <Wire.h>
#include <RotaryEncoder.h>
#include <esp_sleep.h>

#include "board_pins.h"
#include "status_alert.h"

namespace {
RotaryEncoder encoder(ENCODER_INA, ENCODER_INB, RotaryEncoder::LatchMode::TWO03);
int lastPos = 0;
int pendingDelta = 0;
bool lastBtn = false;
bool clickPending = false;
bool longPressPending = false;
bool longPressFired = false;
uint32_t btnDownMs = 0;
constexpr uint32_t kEncoderLongMs = 700;

bool lastUserKey = false;
bool userKeyClickPending = false;
bool userKeyLongPending = false;
bool userKeyLongFired = false;
uint32_t userKeyDownMs = 0;
constexpr uint32_t kUserKeyLongMs = 1000;

uint32_t lastInputAtMs = 0;
bool forcedDisplayOff = false;

void IRAM_ATTR onEncoder() { encoder.tick(); }

void noteInput() { lastInputAtMs = millis(); }

bool i2cOk = false;
}  // namespace

namespace Board {

void begin() {
  pinMode(BOARD_PWR_EN, OUTPUT);
  digitalWrite(BOARD_PWR_EN, HIGH);

  pinMode(DISPLAY_CS, OUTPUT);
  digitalWrite(DISPLAY_CS, HIGH);
  pinMode(BOARD_SD_CS, OUTPUT);
  digitalWrite(BOARD_SD_CS, HIGH);
  pinMode(BOARD_CC1101_CS, OUTPUT);
  digitalWrite(BOARD_CC1101_CS, HIGH);

  pinMode(ENCODER_KEY, INPUT_PULLUP);
  pinMode(BOARD_USER_KEY, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(ENCODER_INA), onEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_INB), onEncoder, CHANGE);
  lastPos = encoder.getPosition();
  lastInputAtMs = millis();

  Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
  Wire.beginTransmission(BQ25896_ADDR);
  i2cOk = (Wire.endTransmission() == 0);

  if (i2cOk) {
    // Minimal BQ25896: enable ADC measure (REG02 bit 6 / charge path varies by lib).
    // Battery % falls back to stub if gauge unavailable.
  }
}

void setBacklight(uint8_t percent) {
  // Handled by LovyanGFX light when DisplayUi begins; keep for API completeness.
  (void)percent;
}

uint8_t batteryPercent() {
  // Prefer BQ27220 RelativeStateOfCharge at 0x2C if present.
  Wire.beginTransmission(BQ27220_ADDR);
  if (Wire.endTransmission() != 0) return 100;
  Wire.beginTransmission(BQ27220_ADDR);
  Wire.write(0x2C);
  Wire.write(0x00);
  if (Wire.endTransmission(false) != 0) return 100;
  if (Wire.requestFrom((uint8_t)BQ27220_ADDR, (uint8_t)1) != 1) return 100;
  uint8_t pct = Wire.read();
  if (pct > 100) pct = 100;
  return pct;
}

bool batteryCharging() { return false; }

void setStatusLed(uint8_t r, uint8_t g, uint8_t b) {
  (void)r;
  (void)g;
  (void)b;
  // Fleet status driven by StatusAlert (FastLED).
}

void tickEncoder() {
  encoder.tick();
  int pos = encoder.getPosition();
  int d = pos - lastPos;
  pendingDelta += d;
  lastPos = pos;
  if (d) {
    noteInput();
    forcedDisplayOff = false;
  }

  bool btn = digitalRead(ENCODER_KEY) == LOW;
  if (btn && !lastBtn) {
    btnDownMs = millis();
    longPressFired = false;
    noteInput();
    forcedDisplayOff = false;
  } else if (btn && lastBtn) {
    noteInput();
    if (!longPressFired && (millis() - btnDownMs >= kEncoderLongMs)) {
      longPressPending = true;
      longPressFired = true;
      forcedDisplayOff = false;
    }
  } else if (!btn && lastBtn) {
    // Short click only if we never crossed the long-press threshold.
    if (!longPressFired && (millis() - btnDownMs < kEncoderLongMs)) {
      clickPending = true;
      forcedDisplayOff = false;
    }
  }
  lastBtn = btn;

  // Top button: sleep controls — do not count as UI activity / wake.
  bool uk = digitalRead(BOARD_USER_KEY) == LOW;
  if (uk && !lastUserKey) {
    userKeyDownMs = millis();
    userKeyLongFired = false;
  } else if (uk && lastUserKey) {
    if (!userKeyLongFired && (millis() - userKeyDownMs >= kUserKeyLongMs)) {
      userKeyLongPending = true;
      userKeyLongFired = true;
    }
  } else if (!uk && lastUserKey) {
    if (!userKeyLongFired && (millis() - userKeyDownMs < kUserKeyLongMs)) {
      userKeyClickPending = true;
    }
  }
  lastUserKey = uk;
}

uint32_t lastInputMs() { return lastInputAtMs; }

int encoderDelta() {
  int d = pendingDelta;
  pendingDelta = 0;
  return d;
}

bool encoderPressed() { return digitalRead(ENCODER_KEY) == LOW; }

bool encoderClicked() {
  if (!clickPending) return false;
  clickPending = false;
  return true;
}

bool encoderLongPressed() {
  if (!longPressPending) return false;
  longPressPending = false;
  return true;
}

bool userKeyPressed() { return digitalRead(BOARD_USER_KEY) == LOW; }

bool userKeyClicked() {
  if (!userKeyClickPending) return false;
  userKeyClickPending = false;
  return true;
}

bool userKeyLongPressed() {
  if (!userKeyLongPending) return false;
  userKeyLongPending = false;
  return true;
}

bool displayForcedOff() { return forcedDisplayOff; }

void setDisplayForcedOff(bool off) { forcedDisplayOff = off; }

void enterDeepSleep() {
  StatusAlert::shutdown();
  digitalWrite(BOARD_PWR_EN, LOW);
  // Wake when top button is pressed (active low).
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BOARD_USER_KEY, 0);
  while (digitalRead(BOARD_USER_KEY) == LOW) {
    delay(10);
  }
  delay(80);
  esp_deep_sleep_start();
}

}  // namespace Board
