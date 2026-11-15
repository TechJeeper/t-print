#include "printer_discover.h"

#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>

namespace {
DiscoverStatus state = DiscoverStatus::Idle;
String err;
String rangeStr;
DiscoveredPrinter found[12];
size_t foundCount = 0;

uint8_t baseOctets[2] = {0, 0};  // a.b
uint8_t thirdFrom = 0;
uint8_t thirdTo = 0;
uint8_t curThird = 0;
uint8_t nextLast = 1;
uint8_t selfIp[4] = {0, 0, 0, 0};

uint8_t priorityThird[8];
uint8_t priorityLast[8];
size_t prioCount = 0;
size_t prioIndex = 0;
bool mdnsDone = false;
bool prioDone = false;

constexpr uint8_t kMaxThirdSpan = 16;

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

void probeHost(uint8_t third, uint8_t last) {
  if (foundCount >= 12) return;
  if (last == 0 || last == 255) return;
  if (third == selfIp[2] && last == selfIp[3]) return;
  IPAddress ip(baseOctets[0], baseOctets[1], third, last);
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

bool inRange(uint8_t third) { return third >= thirdFrom && third <= thirdTo; }

void pushPrio(uint8_t third, uint8_t last) {
  if (last == 0 || last == 255) return;
  if (!inRange(third)) return;
  if (third == selfIp[2] && last == selfIp[3]) return;
  for (size_t i = 0; i < prioCount; i++)
    if (priorityThird[i] == third && priorityLast[i] == last) return;
  if (prioCount < 8) {
    priorityThird[prioCount] = third;
    priorityLast[prioCount] = last;
    prioCount++;
  }
}

bool parseRange(const char* range, uint8_t& a, uint8_t& b, uint8_t& t0, uint8_t& t1) {
  String s = range ? String(range) : String();
  s.trim();
  if (!s.length()) {
    IPAddress local = WiFi.localIP();
    a = local[0];
    b = local[1];
    t0 = t1 = local[2];
    return true;
  }

  // Drop trailing .0, .0/24, /24 noise from pasted subnets.
  int slash = s.indexOf('/');
  if (slash >= 0) s = s.substring(0, slash);
  s.trim();
  while (s.endsWith(".0") && s.length() > 7) {
    // only strip a lone host/network .0 if we still have a.b.c
    int dots = 0;
    for (size_t i = 0; i < s.length(); i++)
      if (s[i] == '.') dots++;
    if (dots < 3) break;
    s = s.substring(0, s.length() - 2);
    s.trim();
  }

  int o1 = -1, o2 = -1, o3 = -1, o4 = -1;
  // a.b.c-d  (third-octet range)
  if (sscanf(s.c_str(), "%d.%d.%d-%d", &o1, &o2, &o3, &o4) == 4) {
    if (o1 < 0 || o1 > 255 || o2 < 0 || o2 > 255 || o3 < 0 || o3 > 255 || o4 < 0 || o4 > 255) {
      err = "Octets must be 0–255";
      return false;
    }
    a = (uint8_t)o1;
    b = (uint8_t)o2;
    t0 = (uint8_t)((o3 < o4) ? o3 : o4);
    t1 = (uint8_t)((o3 > o4) ? o3 : o4);
  } else if (sscanf(s.c_str(), "%d.%d.%d", &o1, &o2, &o3) >= 3) {
    if (o1 < 0 || o1 > 255 || o2 < 0 || o2 > 255 || o3 < 0 || o3 > 255) {
      err = "Octets must be 0–255";
      return false;
    }
    a = (uint8_t)o1;
    b = (uint8_t)o2;
    t0 = t1 = (uint8_t)o3;
  } else {
    err = "Use 192.168.1 or 192.168.0-10";
    return false;
  }

  if ((uint16_t)t1 - (uint16_t)t0 + 1 > kMaxThirdSpan) {
    err = "Range too wide (max " + String(kMaxThirdSpan) + " subnets)";
    return false;
  }
  return true;
}

void formatRangeLabel() {
  if (thirdFrom == thirdTo) {
    rangeStr = String(baseOctets[0]) + "." + String(baseOctets[1]) + "." + String(thirdFrom) + ".x";
  } else {
    rangeStr = String(baseOctets[0]) + "." + String(baseOctets[1]) + "." + String(thirdFrom) + "-" +
               String(thirdTo) + ".x";
  }
}

uint32_t totalHostSlots() {
  return (uint32_t)(thirdTo - thirdFrom + 1) * 254u;
}

uint32_t scannedHostSlots() {
  if (curThird < thirdFrom) return 0;
  uint32_t doneThirds = (uint32_t)(curThird - thirdFrom);
  uint32_t inThird = (nextLast > 1) ? (uint32_t)(nextLast - 1) : 0;
  return doneThirds * 254u + inThird;
}
}  // namespace

namespace PrinterDiscover {

bool start(const char* range) {
  if (state == DiscoverStatus::Running) return true;
  if (WiFi.status() != WL_CONNECTED) {
    state = DiscoverStatus::Failed;
    err = "Join home Wi‑Fi first, then open setup from the device IP";
    rangeStr = "";
    return false;
  }

  uint8_t a = 0, b = 0, t0 = 0, t1 = 0;
  err = "";
  if (!parseRange(range, a, b, t0, t1)) {
    state = DiscoverStatus::Failed;
    rangeStr = "";
    return false;
  }

  foundCount = 0;
  mdnsDone = false;
  prioDone = false;
  prioCount = 0;
  prioIndex = 0;
  baseOctets[0] = a;
  baseOctets[1] = b;
  thirdFrom = t0;
  thirdTo = t1;
  curThird = thirdFrom;
  nextLast = 1;

  IPAddress local = WiFi.localIP();
  selfIp[0] = local[0];
  selfIp[1] = local[1];
  selfIp[2] = local[2];
  selfIp[3] = local[3];

  IPAddress gateway = WiFi.gatewayIP();
  pushPrio(gateway[2], gateway[3]);
  if (inRange(local[2])) {
    for (int d = -4; d <= 4; d++) {
      int v = (int)local[3] + d;
      if (v > 0 && v < 255) pushPrio(local[2], (uint8_t)v);
    }
  }

  formatRangeLabel();
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
      probeHost(priorityThird[prioIndex], priorityLast[prioIndex]);
      prioIndex++;
      return;
    }
    prioDone = true;
    return;
  }

  constexpr int kPerTick = 3;
  for (int n = 0; n < kPerTick;) {
    if (curThird > thirdTo) break;

    bool skip = (curThird == selfIp[2] && nextLast == selfIp[3]);
    for (size_t i = 0; i < prioCount; i++) {
      if (priorityThird[i] == curThird && priorityLast[i] == nextLast) skip = true;
    }

    uint8_t third = curThird;
    uint8_t last = nextLast;
    nextLast++;
    if (nextLast > 254) {
      nextLast = 1;
      curThird++;
    }

    if (skip) continue;
    probeHost(third, last);
    n++;
  }

  if (curThird > thirdTo || foundCount >= 12) {
    state = DiscoverStatus::Done;
  }
}

DiscoverStatus status() { return state; }
const char* error() { return err.c_str(); }
const char* rangeLabel() { return rangeStr.c_str(); }
size_t count() { return foundCount; }
const DiscoveredPrinter* results() { return found; }

uint8_t progress() {
  if (state == DiscoverStatus::Done) return 100;
  if (state != DiscoverStatus::Running) return 0;
  if (!mdnsDone) return 5;
  if (!prioDone) return (uint8_t)(10 + (prioIndex * 15) / max((size_t)1, prioCount));
  uint32_t total = totalHostSlots();
  uint32_t done = scannedHostSlots();
  if (total == 0) return 99;
  return (uint8_t)min(99, 25 + (int)((done * 74) / total));
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
