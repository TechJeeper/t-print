#include "backends.h"

#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>

namespace {
String baseUrl(const PrinterProfile& p) {
  String u = "http://";
  u += p.host;
  if (p.port && p.port != 80) {
    u += ":";
    u += String(p.port);
  }
  return u;
}

void applyAuth(HTTPClient& http, const PrinterProfile& p) {
  if (p.apiKey[0]) {
    http.addHeader("X-Api-Key", p.apiKey);
  }
}

PrinterStatus mapState(const char* s) {
  if (!s) return PrinterStatus::Unknown;
  if (!strcasecmp(s, "PRINTING")) return PrinterStatus::Printing;
  if (!strcasecmp(s, "PAUSED")) return PrinterStatus::Paused;
  if (!strcasecmp(s, "FINISHED") || !strcasecmp(s, "STOPPED")) return PrinterStatus::Complete;
  if (!strcasecmp(s, "ERROR")) return PrinterStatus::Error;
  if (!strcasecmp(s, "ATTENTION")) return PrinterStatus::Attention;
  if (!strcasecmp(s, "IDLE") || !strcasecmp(s, "READY")) return PrinterStatus::Idle;
  return PrinterStatus::Idle;
}
}  // namespace

namespace Backends {

bool pollPrusa(const PrinterProfile& p, PrinterState& out) {
  if (WiFi.status() != WL_CONNECTED) {
    out.online = false;
    out.status = PrinterStatus::Offline;
    strlcpy(out.statusText, "offline", sizeof(out.statusText));
    return false;
  }

  HTTPClient http;
  String url = baseUrl(p) + "/api/v1/status";
  if (!http.begin(url)) return false;
  applyAuth(http, p);
  http.setTimeout(4000);
  int code = http.GET();
  if (code != 200) {
    // Fallback Octo-compatible endpoint on older PrusaLink
    http.end();
    url = baseUrl(p) + "/api/printer";
    if (!http.begin(url)) return false;
    applyAuth(http, p);
    code = http.GET();
    if (code != 200) {
      http.end();
      out.online = false;
      out.status = PrinterStatus::Offline;
      snprintf(out.message, sizeof(out.message), "HTTP %d", code);
      return false;
    }
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) return false;

  out.online = true;
  out.lastUpdateMs = millis();

  if (doc["printer"].is<JsonObject>()) {
    const char* state = doc["printer"]["state"] | "IDLE";
    out.status = mapState(state);
    strlcpy(out.statusText, state, sizeof(out.statusText));
    out.nozzleTemp = doc["printer"]["temp_nozzle"] | 0.0f;
    out.nozzleTarget = doc["printer"]["target_nozzle"] | 0.0f;
    out.bedTemp = doc["printer"]["temp_bed"] | 0.0f;
    out.bedTarget = doc["printer"]["target_bed"] | 0.0f;
    if (doc["job"].is<JsonObject>()) {
      out.progress = doc["job"]["progress"] | 0.0f;
      strlcpy(out.filename, doc["job"]["display_name"] | doc["job"]["file_path"] | "",
              sizeof(out.filename));
      out.etaSeconds = doc["job"]["time_remaining"] | 0;
    }
  } else {
    // Octo-style fallback
    const char* state = doc["state"]["text"] | "Unknown";
    strlcpy(out.statusText, state, sizeof(out.statusText));
    out.status = PrinterStatus::Idle;
    out.nozzleTemp = doc["temperature"]["tool0"]["actual"] | 0.0f;
    out.nozzleTarget = doc["temperature"]["tool0"]["target"] | 0.0f;
    out.bedTemp = doc["temperature"]["bed"]["actual"] | 0.0f;
    out.bedTarget = doc["temperature"]["bed"]["target"] | 0.0f;
  }
  return true;
}

bool actionPrusa(const PrinterProfile& p, const char* action) {
  HTTPClient http;
  String url = baseUrl(p);
  String method = "POST";
  if (!strcmp(action, "pause")) {
    url += "/api/v1/job/pause";
  } else if (!strcmp(action, "resume")) {
    url += "/api/v1/job/resume";
  } else if (!strcmp(action, "cancel")) {
    url += "/api/v1/job";
    method = "DELETE";
  } else {
    return false;
  }

  if (!http.begin(url)) return false;
  applyAuth(http, p);
  int code;
  if (method == "DELETE") {
    code = http.sendRequest("DELETE");
  } else {
    code = http.POST("");
  }
  http.end();
  return code == 200 || code == 204 || code == 409;
}

String testPrusa(const PrinterProfile& p) {
  PrinterState st;
  clearPrinterState(st);
  if (!pollPrusa(p, st)) {
    if (st.message[0]) return String("unreachable: ") + st.message;
    return "unreachable";
  }
  return String("online:") + st.statusText;
}

}  // namespace Backends
