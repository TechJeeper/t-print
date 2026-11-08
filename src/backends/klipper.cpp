#include "backends.h"

#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>

namespace {
String baseUrl(const char* host, uint16_t port) {
  String u = "http://";
  u += host;
  u += ":";
  u += String(port ? port : 7125);
  return u;
}

String baseUrl(const PrinterProfile& p) {
  return baseUrl(p.host, p.port ? p.port : 7125);
}

void applyAuth(HTTPClient& http, const PrinterProfile& p) {
  if (p.apiKey[0]) http.addHeader("X-Api-Key", p.apiKey);
}

const char* httpErr(int code) {
  // ESP HTTPClient negative codes
  switch (code) {
    case -1: return "connection failed";
    case -2: return "send header failed";
    case -3: return "send body failed";
    case -4: return "not connected";
    case -5: return "connection lost";
    case -6: return "no stream";
    case -7: return "no HTTP server";
    case -8: return "too little RAM";
    case -9: return "encoding error";
    case -10: return "stream write error";
    case -11: return "read timeout";
    default: return nullptr;
  }
}

String describeHttpFail(int code) {
  if (const char* named = httpErr(code)) return String(named);
  return String("HTTP ") + code;
}

PrinterStatus mapState(const char* s) {
  if (!s) return PrinterStatus::Unknown;
  if (!strcmp(s, "printing")) return PrinterStatus::Printing;
  if (!strcmp(s, "paused")) return PrinterStatus::Paused;
  if (!strcmp(s, "complete")) return PrinterStatus::Complete;
  if (!strcmp(s, "error")) return PrinterStatus::Error;
  if (!strcmp(s, "standby") || !strcmp(s, "cancelled")) return PrinterStatus::Idle;
  return PrinterStatus::Idle;
}

bool pollKlipperAt(const PrinterProfile& p, uint16_t port, PrinterState& out) {
  HTTPClient http;
  String url = baseUrl(p.host, port) +
               "/printer/objects/query?webhooks&virtual_sdcard&print_stats&extruder&heater_bed";
  if (!http.begin(url)) {
    snprintf(out.message, sizeof(out.message), "bad url");
    return false;
  }
  applyAuth(http, p);
  http.setTimeout(5000);
  int code = http.GET();
  if (code != 200) {
    http.end();
    out.online = false;
    out.status = PrinterStatus::Offline;
    strlcpy(out.statusText, "offline", sizeof(out.statusText));
    String err = describeHttpFail(code);
    snprintf(out.message, sizeof(out.message), "%s (:%u)", err.c_str(), (unsigned)port);
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) {
    snprintf(out.message, sizeof(out.message), "bad JSON (:%u)", (unsigned)port);
    return false;
  }

  JsonObject st = doc["result"]["status"].as<JsonObject>();
  if (st.isNull()) {
    snprintf(out.message, sizeof(out.message), "no Moonraker status (:%u)", (unsigned)port);
    return false;
  }

  out.online = true;
  const char* webhook = st["webhooks"]["state"] | "unknown";
  if (strcmp(webhook, "ready") != 0) {
    out.status = PrinterStatus::Attention;
    strlcpy(out.statusText, webhook, sizeof(out.statusText));
    strlcpy(out.message, st["webhooks"]["message"] | "", sizeof(out.message));
  } else {
    const char* ps = st["print_stats"]["state"] | "standby";
    out.status = mapState(ps);
    strlcpy(out.statusText, ps, sizeof(out.statusText));
    strlcpy(out.filename, st["print_stats"]["filename"] | "", sizeof(out.filename));
  }

  out.progress = (st["virtual_sdcard"]["progress"] | 0.0f) * 100.0f;
  out.nozzleTemp = st["extruder"]["temperature"] | 0.0f;
  out.nozzleTarget = st["extruder"]["target"] | 0.0f;
  out.bedTemp = st["heater_bed"]["temperature"] | 0.0f;
  out.bedTarget = st["heater_bed"]["target"] | 0.0f;

  float printDur = st["print_stats"]["print_duration"] | 0.0f;
  float progressFrac = st["virtual_sdcard"]["progress"] | 0.0f;
  if (progressFrac > 0.01f && printDur > 0) {
    float totalEst = printDur / progressFrac;
    float remain = totalEst - printDur;
    if (remain < 0) remain = 0;
    out.etaSeconds = (uint32_t)remain;
  }
  strlcpy(out.message, st["print_stats"]["message"] | "", sizeof(out.message));
  out.lastUpdateMs = millis();
  return true;
}
}  // namespace

namespace Backends {

bool pollKlipper(const PrinterProfile& p, PrinterState& out) {
  if (WiFi.status() != WL_CONNECTED) {
    out.online = false;
    out.status = PrinterStatus::Offline;
    strlcpy(out.statusText, "offline", sizeof(out.statusText));
    strlcpy(out.message, "Wi-Fi down", sizeof(out.message));
    return false;
  }

  uint16_t port = p.port ? p.port : 7125;
  if (pollKlipperAt(p, port, out)) return true;

  // Many Mainsail/Fluidd installs only expose Moonraker via nginx on :80.
  if (port != 80) {
    PrinterState alt;
    clearPrinterState(alt);
    if (pollKlipperAt(p, 80, alt)) {
      out = alt;
      return true;
    }
  }
  return false;
}

bool actionKlipper(const PrinterProfile& p, const char* action, float arg) {
  HTTPClient http;
  uint16_t port = p.port ? p.port : 7125;
  String url = baseUrl(p.host, port);
  String body;
  if (!strcmp(action, "pause")) {
    url += "/printer/print/pause";
  } else if (!strcmp(action, "resume")) {
    url += "/printer/print/resume";
  } else if (!strcmp(action, "cancel")) {
    url += "/printer/print/cancel";
  } else if (!strcmp(action, "estop")) {
    url += "/printer/emergency_stop";
  } else if (!strcmp(action, "nozzle")) {
    url += "/printer/gcode/script";
    body = "script=SET_HEATER_TEMPERATURE%20HEATER=extruder%20TARGET=" + String(arg, 0);
  } else if (!strcmp(action, "bed")) {
    url += "/printer/gcode/script";
    body = "script=SET_HEATER_TEMPERATURE%20HEATER=heater_bed%20TARGET=" + String(arg, 0);
  } else {
    return false;
  }

  if (!http.begin(url)) return false;
  applyAuth(http, p);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int code = body.length() ? http.POST(body) : http.POST("");
  http.end();
  if (code == 200 || code == 204) return true;

  // Retry via :80 reverse proxy if direct Moonraker port failed.
  if (port != 80) {
    HTTPClient http2;
    String url2 = baseUrl(p.host, 80);
    if (!strcmp(action, "pause")) url2 += "/printer/print/pause";
    else if (!strcmp(action, "resume")) url2 += "/printer/print/resume";
    else if (!strcmp(action, "cancel")) url2 += "/printer/print/cancel";
    else if (!strcmp(action, "estop")) url2 += "/printer/emergency_stop";
    else if (!strcmp(action, "nozzle") || !strcmp(action, "bed")) {
      url2 += "/printer/gcode/script";
    } else {
      return false;
    }
    if (!http2.begin(url2)) return false;
    applyAuth(http2, p);
    http2.addHeader("Content-Type", "application/x-www-form-urlencoded");
    int code2 = body.length() ? http2.POST(body) : http2.POST("");
    http2.end();
    return code2 == 200 || code2 == 204;
  }
  return false;
}

String testKlipper(const PrinterProfile& p) {
  PrinterState st;
  clearPrinterState(st);
  if (!pollKlipper(p, st)) {
    if (st.message[0]) return String("unreachable: ") + st.message;
    return "unreachable";
  }
  // Prefer reporting which port answered when user left default 7125 but :80 worked.
  return String("online: ") + st.statusText;
}

}  // namespace Backends
