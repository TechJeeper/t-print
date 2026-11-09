#include "setup_web.h"

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <WiFi.h>

#include "config_store.h"
#include "wifi_manager.h"
#include "printer_hub.h"
#include "printer_discover.h"
#include "display_ui.h"

// Embedded by scripts/embed_webapp.py into webapp_assets.h
#include "webapp_assets.h"

namespace {
AsyncWebServer server(80);

void sendJson(AsyncWebServerRequest* req, const JsonDocument& doc, int code = 200) {
  String out;
  serializeJson(doc, out);
  req->send(code, "application/json", out);
}

PrinterProfile parsePrinter(JsonObject o, const char* existingId = nullptr) {
  PrinterProfile p{};
  if (existingId && existingId[0]) {
    strlcpy(p.id, existingId, sizeof(p.id));
  } else if (o["id"].is<const char*>()) {
    strlcpy(p.id, o["id"] | "", sizeof(p.id));
  } else {
    ConfigStore::makeId(p.id, sizeof(p.id));
  }
  strlcpy(p.name, o["name"] | "Printer", sizeof(p.name));
  p.type = printerTypeFromName(o["type"] | "klipper");
  strlcpy(p.host, o["host"] | "", sizeof(p.host));
  p.port = o["port"] | 0;
  // Keep previous secrets if masked / empty on update
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
  return p;
}

void mergeSecrets(PrinterProfile& p, const PrinterProfile* prev) {
  if (!prev) return;
  if (!p.apiKey[0] || strchr(p.apiKey, '*')) strlcpy(p.apiKey, prev->apiKey, sizeof(p.apiKey));
  if (!p.accessCode[0] || strchr(p.accessCode, '*'))
    strlcpy(p.accessCode, prev->accessCode, sizeof(p.accessCode));
}

JsonObject printerToJson(JsonDocument& doc, JsonObject o, const PrinterProfile& p) {
  (void)doc;
  o["id"] = p.id;
  o["name"] = p.name;
  o["type"] = printerTypeName(p.type);
  o["host"] = p.host;
  o["port"] = p.port;
  o["serial"] = p.serial;
  o["apiKey"] = ConfigStore::maskSecret(p.apiKey);
  o["accessCode"] = ConfigStore::maskSecret(p.accessCode);
  o["snapshotUrl"] = p.snapshotUrl;
  o["enabled"] = p.enabled;
  return o;
}
}  // namespace

namespace SetupWeb {

void begin() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    AsyncWebServerResponse* res =
        req->beginResponse(200, "text/html", (const uint8_t*)WEBAPP_INDEX_HTML, WEBAPP_INDEX_HTML_LEN);
    res->addHeader("Cache-Control", "no-cache");
    req->send(res);
  });

  server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "application/javascript", WEBAPP_APP_JS);
  });

  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "text/css", WEBAPP_STYLE_CSS);
  });

  server.on("/iconify-icon.min.js", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "application/javascript", WEBAPP_ICONIFY_JS);
  });

  server.on("/icons.js", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "application/javascript", WEBAPP_ICONS_JS);
  });

  // Captive portal helpers
  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest* req) { req->redirect("/"); });
  server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest* req) { req->redirect("/"); });
  server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest* req) { req->redirect("/"); });

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    doc["version"] = PP_VERSION;
    doc["deviceName"] = ConfigStore::get().deviceName;
    doc["wifiConnected"] = WifiManager::isConnected();
    doc["softAp"] = WifiManager::isSoftAP();
    doc["ip"] = WifiManager::ipString();
    doc["ssid"] = WiFi.SSID();
    doc["brightness"] = ConfigStore::get().brightness;
    doc["lcdTimeoutSec"] = ConfigStore::get().lcdTimeoutSec;
    doc["alarmOnError"] = ConfigStore::get().alarmOnError;
    doc["mdns"] = String(ConfigStore::get().deviceName) + ".local";
    sendJson(req, doc);
  });

  // Non-blocking scan: POST starts, GET polls (blocking scan drops SoftAP clients → Failed to fetch)
  server.on("/api/wifi/scan", HTTP_POST, [](AsyncWebServerRequest* req) {
    bool ok = WifiManager::startScan();
    JsonDocument doc;
    doc["ok"] = ok;
    doc["status"] = ok ? "scanning" : "error";
    if (!ok) doc["error"] = WifiManager::scanError();
    sendJson(req, doc, ok ? 200 : 500);
  });

  server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    WifiScanStatus st = WifiManager::scanStatus();
    if (st == WifiScanStatus::Running) {
      doc["status"] = "scanning";
      sendJson(req, doc);
      return;
    }
    if (st == WifiScanStatus::Failed) {
      doc["status"] = "error";
      doc["error"] = WifiManager::scanError();
      sendJson(req, doc, 500);
      return;
    }
    doc["status"] = "done";
    JsonArray arr = doc["networks"].to<JsonArray>();
    for (auto& n : WifiManager::scanResults()) {
      JsonObject o = arr.add<JsonObject>();
      o["ssid"] = n.ssid;
      o["rssi"] = n.rssi;
      o["open"] = n.open;
    }
    sendJson(req, doc);
  });

  // SoftAP-safe join: start STA in background; client polls GET /api/wifi until done.
  // Never block here or kill SoftAP before the phone has the LAN IP.
  server.on(
      "/api/wifi", HTTP_PUT,
      [](AsyncWebServerRequest* req) {},
      nullptr,
      [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
        JsonDocument doc;
        if (deserializeJson(doc, data, len)) {
          req->send(400, "application/json", "{\"error\":\"bad json\"}");
          return;
        }
        const char* ssid = doc["ssid"] | "";
        const char* pass = doc["password"] | "";
        if (!ssid[0]) {
          req->send(400, "application/json", "{\"error\":\"ssid required\"}");
          return;
        }
        if (!WifiManager::beginJoin(ssid, pass)) {
          req->send(500, "application/json", "{\"error\":\"could not start join\"}");
          return;
        }
        JsonDocument res;
        res["ok"] = true;
        res["status"] = "connecting";
        sendJson(req, res);
      });

  server.on("/api/wifi", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument res;
    WifiJoinStatus st = WifiManager::joinStatus();
    const char* status = "idle";
    if (st == WifiJoinStatus::Connecting) status = "connecting";
    else if (st == WifiJoinStatus::Connected) status = "connected";
    else if (st == WifiJoinStatus::Failed) status = "failed";
    res["status"] = status;
    res["ok"] = st == WifiJoinStatus::Connected || WifiManager::isConnected();
    res["wifiConnected"] = WifiManager::isConnected();
    res["softAp"] = WifiManager::isSoftAP();
    res["ip"] = WifiManager::ipString();
    if (st == WifiJoinStatus::Failed) res["error"] = WifiManager::joinError();
    sendJson(req, res);
  });

  server.on("/api/device", HTTP_PUT,
            [](AsyncWebServerRequest* req) {}, nullptr,
            [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
              JsonDocument doc;
              if (deserializeJson(doc, data, len)) {
                req->send(400, "application/json", "{\"error\":\"bad json\"}");
                return;
              }
              DeviceConfig& cfg = ConfigStore::get();
              if (doc["deviceName"].is<const char*>()) {
                strlcpy(cfg.deviceName, doc["deviceName"] | "t-print", kDeviceNameLen);
              }
              if (doc["brightness"].is<int>()) {
                int b = doc["brightness"] | 80;
                if (b < 5) b = 5;
                if (b > 100) b = 100;
                cfg.brightness = (uint8_t)b;
              }
              if (doc["lcdTimeoutSec"].is<int>()) {
                int t = doc["lcdTimeoutSec"] | 300;
                if (t < 0) t = 0;
                if (t > 3600) t = 3600;
                cfg.lcdTimeoutSec = (uint16_t)t;
              }
              if (!doc["alarmOnError"].isNull()) {
                cfg.alarmOnError = doc["alarmOnError"] | false;
              }
              ConfigStore::save();
              JsonDocument res;
              res["ok"] = true;
              sendJson(req, res);
            });

  server.on("/api/printers", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    DeviceConfig& cfg = ConfigStore::get();
    for (uint8_t i = 0; i < cfg.printerCount; i++) {
      printerToJson(doc, arr.add<JsonObject>(), cfg.printers[i]);
    }
    sendJson(req, doc);
  });

  server.on("/api/printers", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
            [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
              JsonDocument doc;
              if (deserializeJson(doc, data, len)) {
                req->send(400, "application/json", "{\"error\":\"bad json\"}");
                return;
              }
              PrinterProfile p = parsePrinter(doc.as<JsonObject>());
              if (!ConfigStore::addPrinter(p)) {
                req->send(400, "application/json", "{\"error\":\"limit or save failed\"}");
                return;
              }
              PrinterHub::reloadProfiles();
              JsonDocument res;
              printerToJson(res, res.to<JsonObject>(), p);
              sendJson(req, res, 201);
            });

  // SoftAP-safe discover: POST starts, GET polls (full /24 must not block HTTP).
  server.on("/api/discover", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!WifiManager::isConnected()) {
      req->send(400, "application/json",
                "{\"error\":\"Join home Wi-Fi first, then open setup from the device IP\"}");
      return;
    }
    if (!PrinterDiscover::start()) {
      JsonDocument doc;
      doc["ok"] = false;
      doc["status"] = "failed";
      doc["error"] = PrinterDiscover::error();
      sendJson(req, doc, 400);
      return;
    }
    JsonDocument doc;
    doc["ok"] = true;
    doc["status"] = "running";
    doc["progress"] = PrinterDiscover::progress();
    sendJson(req, doc);
  });

  server.on("/api/discover", HTTP_GET, [](AsyncWebServerRequest* req) {
    JsonDocument doc;
    DiscoverStatus st = PrinterDiscover::status();
    const char* status = "idle";
    if (st == DiscoverStatus::Running) status = "running";
    else if (st == DiscoverStatus::Done) status = "done";
    else if (st == DiscoverStatus::Failed) status = "failed";
    doc["ok"] = st == DiscoverStatus::Done;
    doc["status"] = status;
    doc["progress"] = PrinterDiscover::progress();
    doc["count"] = PrinterDiscover::count();
    if (st == DiscoverStatus::Failed) doc["error"] = PrinterDiscover::error();
    JsonArray arr = doc["printers"].to<JsonArray>();
    const DiscoveredPrinter* found = PrinterDiscover::results();
    for (size_t i = 0; i < PrinterDiscover::count(); i++) {
      JsonObject o = arr.add<JsonObject>();
      o["type"] = printerTypeName(found[i].type);
      o["host"] = found[i].host;
      o["port"] = found[i].port;
      o["name"] = found[i].name;
      o["source"] = found[i].source;
    }
    sendJson(req, doc);
  });

  server.on("^\\/api\\/printers\\/([a-zA-Z0-9]+)$", HTTP_PUT,
            [](AsyncWebServerRequest* req) {}, nullptr,
            [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
              String id = req->pathArg(0);
              JsonDocument doc;
              if (deserializeJson(doc, data, len)) {
                req->send(400, "application/json", "{\"error\":\"bad json\"}");
                return;
              }
              PrinterProfile* prev = ConfigStore::findPrinter(id.c_str());
              if (!prev) {
                req->send(404, "application/json", "{\"error\":\"not found\"}");
                return;
              }
              PrinterProfile p = parsePrinter(doc.as<JsonObject>(), id.c_str());
              mergeSecrets(p, prev);
              ConfigStore::updatePrinter(id.c_str(), p);
              PrinterHub::reloadProfiles();
              JsonDocument res;
              printerToJson(res, res.to<JsonObject>(), p);
              sendJson(req, res);
            });

  server.on("^\\/api\\/printers\\/([a-zA-Z0-9]+)$", HTTP_DELETE,
            [](AsyncWebServerRequest* req) {
              String id = req->pathArg(0);
              bool ok = ConfigStore::removePrinter(id.c_str());
              PrinterHub::reloadProfiles();
              req->send(ok ? 200 : 404, "application/json", ok ? "{\"ok\":true}" : "{\"error\":\"not found\"}");
            });

  server.on("^\\/api\\/printers\\/([a-zA-Z0-9]+)\\/test$", HTTP_POST,
            [](AsyncWebServerRequest* req) {
              String id = req->pathArg(0);
              PrinterProfile* p = ConfigStore::findPrinter(id.c_str());
              if (!p) {
                req->send(404, "application/json", "{\"error\":\"not found\"}");
                return;
              }
              String result = PrinterHub::testConnection(*p);
              JsonDocument res;
              res["result"] = result;
              res["ok"] = result.startsWith("online");
              sendJson(req, res);
            });

  // Ephemeral connection probe — must NOT live under /api/printers (POST there creates a profile).
  server.on("/api/test-printer", HTTP_POST, [](AsyncWebServerRequest* req) {}, nullptr,
            [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t) {
              JsonDocument doc;
              if (deserializeJson(doc, data, len)) {
                req->send(400, "application/json", "{\"error\":\"bad json\"}");
                return;
              }
              PrinterProfile p = parsePrinter(doc.as<JsonObject>());
              if (!p.host[0]) {
                req->send(400, "application/json", "{\"error\":\"host required\"}");
                return;
              }
              String result = PrinterHub::testConnection(p);
              JsonDocument res;
              res["result"] = result;
              res["ok"] = result.startsWith("online");
              sendJson(req, res);
            });

  server.on("/api/screenshot", HTTP_GET, [](AsyncWebServerRequest* req) {
    uint8_t* bmp = nullptr;
    size_t len = 0;
    if (!DisplayUi::captureScreenshotBmp(&bmp, &len) || !bmp || !len) {
      req->send(500, "application/json", "{\"error\":\"screenshot failed\"}");
      return;
    }
    // Chunked callback owns/frees the buffer after the last chunk.
    AsyncWebServerResponse* res = req->beginResponse(
        "image/bmp", len,
        [bmp, len](uint8_t* buffer, size_t maxLen, size_t index) -> size_t {
          if (index >= len) {
            free(bmp);
            return 0;
          }
          size_t n = len - index;
          if (n > maxLen) n = maxLen;
          memcpy(buffer, bmp + index, n);
          if (index + n >= len) free(bmp);
          return n;
        });
    res->addHeader("Content-Disposition", "attachment; filename=\"t-print.bmp\"");
    res->addHeader("Cache-Control", "no-store");
    req->send(res);
  });

  server.on("/api/factory-reset", HTTP_POST, [](AsyncWebServerRequest* req) {
    ConfigStore::factoryReset();
    req->send(200, "application/json", "{\"ok\":true,\"reboot\":true}");
    delay(200);
    ESP.restart();
  });

  server.onNotFound([](AsyncWebServerRequest* req) {
    if (WifiManager::isSoftAP()) {
      req->redirect("/");
      return;
    }
    req->send(404, "text/plain", "Not found");
  });

  server.begin();
}

void loop() {}

}  // namespace SetupWeb
