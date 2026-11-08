#include "printer_discover.h"

#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>

namespace {
DiscoverStatus state = DiscoverStatus::Idle;
String err;
DiscoveredPrinter found[12];
size_t foundCount = 0;

uint8_t baseOctets[3] = {0, 0, 0};
uint8_t selfLast = 0;
uint8_t nextLast = 1;
uint8_t priority[8];
size_t prioCount = 0;
size_t prioIndex = 0;
bool mdnsDone = false;
bool prioDone = false;

bool alreadyHave(const char* host, uint16_t port) {
  for (size_t i = 0; i < foundCount; i++) {
    if (found[i].port == port && strcmp(found[i].host, host) == 0) return true;
  }
  return false;
}

bool hostHasType(const char* host, PrinterType type) {
  for (size_t i = 0; i < foundCount; i++) {
    if (found[i].type == type && strcmp(found[i].host, host) == 0) return true;
  }
  return false;
}

bool addHit(PrinterType type, const char* host, uint16_t port, const char* name, const char* source) {
  if (!host || !host[0] || foundCount >= 12) return false;
  if (alreadyHave(host, port)) return false;
  // One identity per host — don't also tag a Klipper box as Prusa/Bambu.
  if (type != PrinterType::Klipper && hostHasType(host, PrinterType::Klipper)) return false;
  if (type == PrinterType::Prusa && hostHasType(host, PrinterType::Bambu)) return false;

  DiscoveredPrinter& d = found[foundCount++];
  memset(&d, 0, sizeof(d));
  d.type = type;
  d.port = port;
  strlcpy(d.host, host, sizeof(d.host));
  strlcpy(d.name, name && name[0] ? name : host, sizeof(d.name));
  strlcpy(d.source, source, sizeof(d.source));
  return true;
}

String httpGetBody(const String& url, int* codeOut) {
  HTTPClient http;
  if (!http.begin(url)) {
    if (codeOut) *codeOut = -1;
    return "";
  }
  http.setTimeout(600);
  http.setReuse(false);
  int code = http.GET();
  if (codeOut) *codeOut = code;
  String body = (code > 0) ? http.getString() : String();
  http.end();
  return body;
}

bool httpLooksLikeMoonraker(const String& host, uint16_t port) {
  String url = "http://" + host;
  if (port && port != 80) url += ":" + String(port);
  url += "/server/info";
  int code = 0;
  String body = httpGetBody(url, &code);
  if (code != 200) return false;
  String lower = body;
  lower.toLowerCase();
  // Fluidd/Mainsail reverse-proxy Moonraker on :80 — require real Moonraker markers.
  return lower.indexOf("klippy") >= 0 || lower.indexOf("moonraker") >= 0 ||
         lower.indexOf("websocket_count") >= 0 || lower.indexOf("\"result\"") >= 0;
}

bool httpLooksLikePrusa(const String& host, uint16_t port) {
  String url = "http://" + host;
  if (port && port != 80) url += ":" + String(port);
  url += "/api/version";
  int code = 0;
  String body = httpGetBody(url, &code);
  if (code != 200) return false;  // bare 401 matches too many non-Prusa devices
  String lower = body;
  lower.toLowerCase();
  // PrusaLink /api/version typically includes api + server text like "prusa-link" / "original".
  return lower.indexOf("prusa") >= 0 || lower.indexOf("original") >= 0 ||
         (lower.indexOf("\"api\"") >= 0 && lower.indexOf("\"server\"") >= 0 &&
          lower.indexOf("octoprint") < 0 && lower.indexOf("moonraker") < 0);
}

bool tcpOpen(IPAddress ip, uint16_t port, uint32_t timeoutMs) {
  WiFiClient client;
  client.setTimeout(timeoutMs / 1000);
  bool ok = client.connect(ip, port, timeoutMs);
  if (ok) client.stop();
  return ok;
}

void runMdns() {
  int n = MDNS.queryService("moonraker", "tcp");
  for (int i = 0; i < n && foundCount < 12; i++) {
    IPAddress ip = MDNS.IP(i);
    uint16_t port = MDNS.port(i);
    if (port == 0) port = 7125;
    String host = ip.toString();
    String name = MDNS.hostname(i);
    if (!name.length()) name = "Klipper " + host;
    addHit(PrinterType::Klipper, host.c_str(), port, name.c_str(), "mdns");
  }

  n = MDNS.queryService("http", "tcp");
  for (int i = 0; i < n && foundCount < 12; i++) {
    IPAddress ip = MDNS.IP(i);
    uint16_t port = MDNS.port(i);
    if (port == 0) port = 80;
    String host = ip.toString();
    String name = MDNS.hostname(i);
    String lower = name;
    lower.toLowerCase();

    if (httpLooksLikeMoonraker(host, port)) {
      if (!name.length() || lower.indexOf("prusa") >= 0) name = "Klipper " + host;
      addHit(PrinterType::Klipper, host.c_str(), port ? port : 80, name.c_str(), "mdns");
      continue;
    }

    if (lower.indexOf("prusa") >= 0 || httpLooksLikePrusa(host, port)) {
      if (!name.length()) name = "Prusa " + host;
      addHit(PrinterType::Prusa, host.c_str(), port, name.c_str(), "mdns");
    }
  }
}

void probeHost(uint8_t last) {
  if (foundCount >= 12) return;
  if (last == 0 || last == 255 || last == selfLast) return;
  IPAddress ip(baseOctets[0], baseOctets[1], baseOctets[2], last);
  String host = ip.toString();
  bool isKlipper = false;

  // Moonraker direct (:7125) or Fluidd/Mainsail proxy (:80)
  if (tcpOpen(ip, 7125, 80) && httpLooksLikeMoonraker(host, 7125)) {
    String name = "Klipper " + host;
    addHit(PrinterType::Klipper, host.c_str(), 7125, name.c_str(), "scan");
    isKlipper = true;
  } else if (tcpOpen(ip, 80, 70) && httpLooksLikeMoonraker(host, 80)) {
    String name = "Klipper " + host;
    addHit(PrinterType::Klipper, host.c_str(), 80, name.c_str(), "scan");
    isKlipper = true;
  }

  if (foundCount >= 12) return;
  if (!isKlipper && tcpOpen(ip, 80, 70) && httpLooksLikePrusa(host, 80)) {
    String name = "Prusa " + host;
    addHit(PrinterType::Prusa, host.c_str(), 80, name.c_str(), "scan");
  }

  if (foundCount >= 12) return;
  // Port 8883 alone is a weak Bambu signal — skip if this host is already Klipper.
  if (!isKlipper && !hostHasType(host.c_str(), PrinterType::Klipper) && tcpOpen(ip, 8883, 70)) {
    String name = "Bambu? " + host;
    addHit(PrinterType::Bambu, host.c_str(), 8883, name.c_str(), "scan");
  }
}

void pushPrio(uint8_t last) {
  if (last == 0 || last == 255 || last == selfLast) return;
  for (size_t i = 0; i < prioCount; i++)
    if (priority[i] == last) return;
  if (prioCount < 8) priority[prioCount++] = last;
}
}  // namespace

namespace PrinterDiscover {

bool start() {
  if (state == DiscoverStatus::Running) return true;
  if (WiFi.status() != WL_CONNECTED) {
    state = DiscoverStatus::Failed;
    err = "Join home Wi‑Fi first, then open setup from the device IP";
    return false;
  }

  foundCount = 0;
  err = "";
  mdnsDone = false;
  prioDone = false;
  prioCount = 0;
  prioIndex = 0;
  nextLast = 1;

  IPAddress local = WiFi.localIP();
  IPAddress gateway = WiFi.gatewayIP();
  baseOctets[0] = local[0];
  baseOctets[1] = local[1];
  baseOctets[2] = local[2];
  selfLast = local[3];

  pushPrio(gateway[3]);
  for (int d = -4; d <= 4; d++) {
    int v = (int)local[3] + d;
    if (v > 0 && v < 255) pushPrio((uint8_t)v);
  }

  state = DiscoverStatus::Running;
  return true;
}

void loop() {
  if (state != DiscoverStatus::Running) return;
  if (WiFi.status() != WL_CONNECTED) {
    state = DiscoverStatus::Failed;
    err = "Wi‑Fi dropped during scan";
    return;
  }

  if (!mdnsDone) {
    runMdns();
    mdnsDone = true;
    return;
  }

  if (!prioDone) {
    if (prioIndex < prioCount) {
      probeHost(priority[prioIndex++]);
      return;
    }
    prioDone = true;
    return;
  }

  constexpr int kPerTick = 3;
  for (int n = 0; n < kPerTick && nextLast <= 254; nextLast++) {
    bool skip = (nextLast == selfLast);
    for (size_t i = 0; i < prioCount; i++)
      if (priority[i] == nextLast) skip = true;
    if (skip) continue;
    probeHost(nextLast);
    n++;
  }

  if (nextLast > 254 || foundCount >= 12) {
    state = DiscoverStatus::Done;
  }
}

DiscoverStatus status() { return state; }
const char* error() { return err.c_str(); }
size_t count() { return foundCount; }
const DiscoveredPrinter* results() { return found; }

uint8_t progress() {
  if (state == DiscoverStatus::Done) return 100;
  if (state != DiscoverStatus::Running) return 0;
  if (!mdnsDone) return 5;
  if (!prioDone) return (uint8_t)(10 + (prioIndex * 20) / max((size_t)1, prioCount));
  return (uint8_t)min(99, 30 + (int)((nextLast * 70) / 254));
}

void toJson(JsonDocument& doc, const DiscoveredPrinter* items, size_t count) {
  JsonArray arr = doc.to<JsonArray>();
  for (size_t i = 0; i < count; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["type"] = printerTypeName(items[i].type);
    o["host"] = items[i].host;
    o["port"] = items[i].port;
    o["name"] = items[i].name;
    o["source"] = items[i].source;
  }
}

}  // namespace PrinterDiscover
