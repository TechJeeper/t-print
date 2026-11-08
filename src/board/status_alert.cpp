#include "status_alert.h"

#include <Arduino.h>
#include <FastLED.h>
#include <driver/i2s.h>

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

bool fleetAllGood() {
  size_t n = PrinterHub::count();
  if (n == 0) return true;  // no fleet configured — idle green
  for (size_t i = 0; i < n; i++) {
    const PrinterState* st = PrinterHub::stateAt(i);
    if (!st) continue;
    if (st->status == PrinterStatus::Error || st->status == PrinterStatus::Attention) {
      return false;
    }
  }
  return true;
}

void setAll(CRGB c) {
  for (int i = 0; i < WS2812_NUM_LEDS; i++) leds[i] = c;
  FastLED.show();
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
  FastLED.setBrightness(48);
  setAll(CRGB::Green);
}

void loop() {
  const bool error = fleetHasError();
  const uint32_t now = millis();

  if (now - lastLedMs >= 350) {
    lastLedMs = now;
    blinkOn = !blinkOn;
    if (error) {
      setAll(blinkOn ? CRGB::Red : CRGB::Black);
    } else if (fleetAllGood()) {
      setAll(CRGB(0, 180, 40));  // solid green — fleet OK
    } else {
      setAll(CRGB(0, 180, 40));
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
