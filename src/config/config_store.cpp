#include "config_store.h"

#include <Preferences.h>
#include <ArduinoJson.h>

namespace {
Preferences prefs;
DeviceConfig cfg;
constexpr const char* kNs = "ppocket";

void defaults() {
  memset(&cfg, 0, sizeof(cfg));
  strncpy(cfg.deviceName, "t-print", kDeviceNameLen - 1);
  cfg.brightness = 80;
  cfg.alarmOnError = false;
  cfg.printerCount = 0;
  cfg.wifiConfigured = false;
}
}  // namespace

namespace ConfigStore {

void begin() {
  defaults();
  prefs.begin(kNs, false);
  load();
}

DeviceConfig& get() { return cfg; }

bool load() {
  defaults();
  String json = prefs.getString("cfg", "");
  if (json.isEmpty()) return false;

  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;

  strlcpy(cfg.wifiSsid, doc["wifiSsid"] | "", kSsidLen);
  strlcpy(cfg.wifiPass, doc["wifiPass"] | "", kPassLen);
  strlcpy(cfg.deviceName, doc["deviceName"] | "t-print", kDeviceNameLen);
  cfg.brightness = doc["brightness"] | 80;
  cfg.alarmOnError = doc["alarmOnError"] | false;
  cfg.wifiConfigured = doc["wifiConfigured"] | (cfg.wifiSsid[0] != 0);

  JsonArray arr = doc["printers"].as<JsonArray>();
  cfg.printerCount = 0;
  for (JsonObject o : arr) {
    if (cfg.printerCount >= kMaxPrinters) break;
    PrinterProfile& p = cfg.printers[cfg.printerCount++];
    memset(&p, 0, sizeof(p));
    strlcpy(p.id, o["id"] | "", sizeof(p.id));
    strlcpy(p.name, o["name"] | "", sizeof(p.name));
    p.type = printerTypeFromName(o["type"] | "klipper");
    strlcpy(p.host, o["host"] | "", sizeof(p.host));
    p.port = o["port"] | 0;
    strlcpy(p.apiKey, o["apiKey"] | "", sizeof(p.apiKey));
    strlcpy(p.serial, o["serial"] | "", sizeof(p.serial));
    strlcpy(p.accessCode, o["accessCode"] | "", sizeof(p.accessCode));
    strlcpy(p.snapshotUrl, o["snapshotUrl"] | "", sizeof(p.snapshotUrl));
    p.enabled = o["enabled"] | true;
    if (p.port == 0) {
      if (p.type == PrinterType::Klipper) p.port = 7125;
      else if (p.type == PrinterType::Bambu) p.port = 8883;
      else p.port = 80;
    }
  }
  return true;
}

bool save() {
  JsonDocument doc;
  doc["wifiSsid"] = cfg.wifiSsid;
  doc["wifiPass"] = cfg.wifiPass;
  doc["deviceName"] = cfg.deviceName;
  doc["brightness"] = cfg.brightness;
  doc["alarmOnError"] = cfg.alarmOnError;
  doc["wifiConfigured"] = cfg.wifiConfigured;
  JsonArray arr = doc["printers"].to<JsonArray>();
  for (uint8_t i = 0; i < cfg.printerCount; i++) {
    JsonObject o = arr.add<JsonObject>();
    const PrinterProfile& p = cfg.printers[i];
    o["id"] = p.id;
    o["name"] = p.name;
    o["type"] = printerTypeName(p.type);
    o["host"] = p.host;
    o["port"] = p.port;
    o["apiKey"] = p.apiKey;
    o["serial"] = p.serial;
    o["accessCode"] = p.accessCode;
    o["snapshotUrl"] = p.snapshotUrl;
    o["enabled"] = p.enabled;
  }
  String json;
  serializeJson(doc, json);
  return prefs.putString("cfg", json) > 0;
}

void factoryReset() {
  prefs.clear();
  defaults();
  save();
}

void makeId(char* out, size_t len) {
  snprintf(out, len, "p%08lx", (unsigned long)(esp_random() & 0xffffffff));
}

String maskSecret(const char* secret) {
  if (!secret || !secret[0]) return "";
  size_t n = strlen(secret);
  if (n <= 4) return "****";
  String s;
  for (size_t i = 0; i < n - 4; i++) s += '*';
  s += (secret + (n - 4));
  return s;
}

bool addPrinter(const PrinterProfile& p) {
  if (cfg.printerCount >= kMaxPrinters) return false;
  cfg.printers[cfg.printerCount++] = p;
  return save();
}

bool updatePrinter(const char* id, const PrinterProfile& p) {
  PrinterProfile* existing = findPrinter(id);
  if (!existing) return false;
  PrinterProfile copy = p;
  strlcpy(copy.id, id, sizeof(copy.id));
  *existing = copy;
  return save();
}

bool removePrinter(const char* id) {
  for (uint8_t i = 0; i < cfg.printerCount; i++) {
    if (strcmp(cfg.printers[i].id, id) == 0) {
      for (uint8_t j = i; j + 1 < cfg.printerCount; j++) {
        cfg.printers[j] = cfg.printers[j + 1];
      }
      cfg.printerCount--;
      return save();
    }
  }
  return false;
}

PrinterProfile* findPrinter(const char* id) {
  for (uint8_t i = 0; i < cfg.printerCount; i++) {
    if (strcmp(cfg.printers[i].id, id) == 0) return &cfg.printers[i];
  }
  return nullptr;
}

}  // namespace ConfigStore
