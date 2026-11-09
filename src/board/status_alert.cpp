#include "status_alert.h"

#include <Arduino.h>
#include <FastLED.h>
#include <driver/i2s.h>

#include "board.h"
#include "board_pins.h"
#include "config_store.h"
#include "printer_hub.h"

namespace {
CRGB leds[WS2812_NUM_LEDS];
bool i2sReady = false;
bool wasInError = false;
uint32_t lastBeepMs = 0;
uint32_t lastLedMs = 0;
bool blinkOn = false;
uint8_t currentBri = 0;

constexpr uint8_t kBriBright = 48;
constexpr uint8_t kBriDim = 8;
constexpr uint32_t kDimAfterMs = 60000;

bool fleetHasError() {
  size_t n = PrinterHub::count();
  for (size_t i = 0; i < n; i++) {
    const PrinterState* st = PrinterHub::stateAt(i);
    if (!st) continue;
    if (st->status == PrinterStatus::Error || st->status == PrinterStatus::Attention) {
      return true;
    }
  }
  return false;
}

bool fleetPrinting() {
  size_t n = PrinterHub::count();
  for (size_t i = 0; i < n; i++) {
    const PrinterState* st = PrinterHub::stateAt(i);
    if (!st || !st->online) continue;
    if (st->status == PrinterStatus::Printing || st->status == PrinterStatus::Paused) {
      return true;
    }
  }
  return false;
}

void setAll(CRGB c) {
  for (int i = 0; i < WS2812_NUM_LEDS; i++) leds[i] = c;
  FastLED.show();
}

void applyBrightness(uint8_t bri) {
  if (bri == currentBri) return;
  currentBri = bri;
  FastLED.setBrightness(bri);
}

bool ensureI2s() {
  if (i2sReady) return true;
  i2s_config_t cfg = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate = 16000,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = 0,
      .dma_buf_count = 4,
      .dma_buf_len = 256,
      .use_apll = false,
      .tx_desc_auto_clear = true,
      .fixed_mclk = 0,
  };
  i2s_pin_config_t pins = {
      .bck_io_num = BOARD_VOICE_BCLK,
      .ws_io_num = BOARD_VOICE_LRCLK,
      .data_out_num = BOARD_VOICE_DIN,
      .data_in_num = I2S_PIN_NO_CHANGE,
  };
  if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) return false;
  if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) {
    i2s_driver_uninstall(I2S_NUM_0);
    return false;
  }
  i2sReady = true;
  return true;
}

void playBeep(uint16_t freqHz, uint16_t durationMs) {
  if (!ensureI2s()) return;
  const int rate = 16000;
  const int samples = (rate * durationMs) / 1000;
  const int halfPeriod = max(1, rate / (int)(freqHz * 2));
  int16_t buf[128];
  int written = 0;
  int phase = 0;
  int16_t level = 12000;
  while (written < samples) {
    int chunk = min(128, samples - written);
    for (int i = 0; i < chunk; i++) {
      buf[i] = (phase < halfPeriod) ? level : (int16_t)-level;
      phase++;
      if (phase >= halfPeriod * 2) phase = 0;
    }
    size_t bytes = 0;
    i2s_write(I2S_NUM_0, buf, chunk * sizeof(int16_t), &bytes, portMAX_DELAY);
    written += chunk;
  }
  // brief silence to avoid click hang
  memset(buf, 0, sizeof(buf));
  size_t bytes = 0;
  i2s_write(I2S_NUM_0, buf, sizeof(buf), &bytes, portMAX_DELAY);
}
}  // namespace

namespace StatusAlert {

void begin() {
  pinMode(BOARD_PWR_EN, OUTPUT);
  digitalWrite(BOARD_PWR_EN, HIGH);  // powers WS2812 rail
  FastLED.addLeds<WS2812B, WS2812_DATA_PIN, GRB>(leds, WS2812_NUM_LEDS);
  applyBrightness(kBriBright);
  setAll(CRGB::White);
}

void loop() {
  const bool error = fleetHasError();
  const bool printing = fleetPrinting();
  const uint32_t now = millis();
  const bool idleTimeout = (now - Board::lastInputMs()) >= kDimAfterMs;

  // Idle white: turn fully off after timeout. Active/error colors dim instead.
  if (!error && !printing && idleTimeout) {
    applyBrightness(kBriBright);
    if (now - lastLedMs >= 350) {
      lastLedMs = now;
      setAll(CRGB::Black);
    }
  } else {
    applyBrightness(idleTimeout ? kBriDim : kBriBright);
    if (now - lastLedMs >= 350) {
      lastLedMs = now;
      blinkOn = !blinkOn;
      if (error) {
        setAll(blinkOn ? CRGB::Red : CRGB::Black);
      } else if (printing) {
        setAll(CRGB(0, 180, 40));
      } else {
        setAll(CRGB::White);
      }
    } else if (!error) {
      static bool wasIdleTimeout = false;
      if (wasIdleTimeout != idleTimeout) {
        if (printing) setAll(CRGB(0, 180, 40));
        else setAll(CRGB::White);
      }
      wasIdleTimeout = idleTimeout;
    }
  }

  DeviceConfig& cfg = ConfigStore::get();
  if (cfg.alarmOnError && error) {
    // Rising edge + periodic reminder while still in error
    if (!wasInError || now - lastBeepMs >= 4000) {
      lastBeepMs = now;
      playBeep(880, 160);
      playBeep(660, 200);
    }
  }
  wasInError = error;
}

}  // namespace StatusAlert
