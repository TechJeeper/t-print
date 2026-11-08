#include "board.h"

#include <Wire.h>
#include <RotaryEncoder.h>

#include "board_pins.h"

namespace {
RotaryEncoder encoder(ENCODER_INA, ENCODER_INB, RotaryEncoder::LatchMode::TWO03);
int lastPos = 0;
int pendingDelta = 0;
bool lastBtn = false;
bool clickPending = false;
uint32_t btnDownMs = 0;
bool lastUserKey = false;
bool userKeyClickPending = false;
uint32_t userKeyDownMs = 0;

void IRAM_ATTR onEncoder() { encoder.tick(); }

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
  pendingDelta += (pos - lastPos);
  lastPos = pos;

  bool btn = digitalRead(ENCODER_KEY) == LOW;
  if (btn && !lastBtn) {
    btnDownMs = millis();
  } else if (!btn && lastBtn) {
    if (millis() - btnDownMs < 800) clickPending = true;
  }
  lastBtn = btn;

  bool uk = digitalRead(BOARD_USER_KEY) == LOW;
  if (uk && !lastUserKey) {
    userKeyDownMs = millis();
  } else if (!uk && lastUserKey) {
    if (millis() - userKeyDownMs < 800) userKeyClickPending = true;
  }
  lastUserKey = uk;
}

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

bool userKeyPressed() { return digitalRead(BOARD_USER_KEY) == LOW; }

bool userKeyClicked() {
  if (!userKeyClickPending) return false;
  userKeyClickPending = false;
  return true;
}

}  // namespace Board
