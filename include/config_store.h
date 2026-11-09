#pragma once

#include "printer_types.h"

static constexpr size_t kMaxPrinters = 8;
static constexpr size_t kSsidLen = 33;
static constexpr size_t kPassLen = 65;
static constexpr size_t kDeviceNameLen = 32;

struct DeviceConfig {
  char wifiSsid[kSsidLen];
  char wifiPass[kPassLen];
  char deviceName[kDeviceNameLen];
  uint8_t brightness;     // 0-100
  uint16_t lcdTimeoutSec; // 0 = never; otherwise seconds until backlight off
  bool alarmOnError;      // beep when any printer errors
  uint8_t printerCount;
  PrinterProfile printers[kMaxPrinters];
  bool wifiConfigured;
};

namespace ConfigStore {
void begin();
DeviceConfig& get();
bool load();
bool save();
void factoryReset();
bool addPrinter(const PrinterProfile& p);
bool updatePrinter(const char* id, const PrinterProfile& p);
bool removePrinter(const char* id);
PrinterProfile* findPrinter(const char* id);
void makeId(char* out, size_t len);
String maskSecret(const char* secret);
}  // namespace ConfigStore
