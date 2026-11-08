#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "printer_types.h"

struct DiscoveredPrinter {
  PrinterType type;
  char host[64];
  uint16_t port;
  char name[48];
  char source[12];  // mdns | scan
};

enum class DiscoverStatus : uint8_t { Idle = 0, Running, Done, Failed };

namespace PrinterDiscover {
// SoftAP/LAN-safe discovery: start + poll from loop(). Never block HTTP handlers.
bool start();
void loop();
DiscoverStatus status();
const char* error();
size_t count();
const DiscoveredPrinter* results();
uint8_t progress();  // 0-100 while running

void toJson(JsonDocument& doc, const DiscoveredPrinter* items, size_t count);
}  // namespace PrinterDiscover
