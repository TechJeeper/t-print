#include "backends.h"

#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>

namespace {
// Bambu pushall can exceed 30KB; buffer in PSRAM when available.
constexpr size_t kMqttBuf = 48 * 1024;

WiFiClientSecure tls;
PubSubClient mqtt(tls);
PrinterState* liveState = nullptr;
char liveSerial[32] = {0};
uint32_t lastMsgMs = 0;

uint8_t* mqttBuffer = nullptr;

void ensureBuffer() {
  if (mqttBuffer) return;
  mqttBuffer = (uint8_t*)ps_malloc(kMqttBuf);
  if (!mqttBuffer) mqttBuffer = (uint8_t*)malloc(kMqttBuf);
  if (mqttBuffer) mqtt.setBufferSize(kMqttBuf);
}

PrinterStatus mapGcodeState(const char* s) {
  if (!s) return PrinterStatus::Unknown;
  if (strstr(s, "RUNNING") || strstr(s, "PREPARE")) return PrinterStatus::Printing;
  if (strstr(s, "PAUSE")) return PrinterStatus::Paused;
  if (strstr(s, "FINISH")) return PrinterStatus::Complete;
  if (strstr(s, "FAILED") || strstr(s, "FAILED")) return PrinterStatus::Error;
  if (strstr(s, "IDLE") || strstr(s, "READY")) return PrinterStatus::Idle;
  return PrinterStatus::Idle;
}

void onMessage(char* topic, byte* payload, unsigned int len) {
  (void)topic;
  if (!liveState || !payload || !len) return;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, len);
  if (err) return;

  JsonObject print = doc["print"].as<JsonObject>();
  if (print.isNull()) return;

  liveState->online = true;
  liveState->lastUpdateMs = millis();
  lastMsgMs = millis();

  const char* gcode = print["gcode_state"] | "IDLE";
  liveState->status = mapGcodeState(gcode);
  strlcpy(liveState->statusText, gcode, sizeof(liveState->statusText));
  strlcpy(liveState->filename, print["subtask_name"] | print["gcode_file"] | "",
          sizeof(liveState->filename));

  int mcPct = print["mc_percent"] | 0;
  liveState->progress = (float)mcPct;
  liveState->layer = print["layer_num"] | 0;
  liveState->layerCount = print["total_layer_num"] | 0;
  liveState->etaSeconds = (print["mc_remaining_time"] | 0) * 60;

  // Temps come as strings sometimes
  if (print["nozzle_temper"].is<const char*>())
    liveState->nozzleTemp = atof(print["nozzle_temper"] | "0");
  else
    liveState->nozzleTemp = print["nozzle_temper"] | 0.0f;
  if (print["nozzle_target_temper"].is<const char*>())
    liveState->nozzleTarget = atof(print["nozzle_target_temper"] | "0");
  else
    liveState->nozzleTarget = print["nozzle_target_temper"] | 0.0f;
  if (print["bed_temper"].is<const char*>())
    liveState->bedTemp = atof(print["bed_temper"] | "0");
  else
    liveState->bedTemp = print["bed_temper"] | 0.0f;
  if (print["bed_target_temper"].is<const char*>())
    liveState->bedTarget = atof(print["bed_target_temper"] | "0");
  else
    liveState->bedTarget = print["bed_target_temper"] | 0.0f;
}

bool connectMqtt(const PrinterProfile& p) {
  ensureBuffer();
  tls.setInsecure();  // Bambu LAN uses self-signed cert
  mqtt.setClient(tls);
  mqtt.setServer(p.host, p.port ? p.port : 8883);
  mqtt.setCallback(onMessage);
  mqtt.setKeepAlive(30);

  String clientId = String("ppocket-") + String((uint32_t)ESP.getEfuseMac(), HEX);
  bool ok = mqtt.connect(clientId.c_str(), "bblp", p.accessCode);
  if (!ok) return false;

  String topic = String("device/") + p.serial + "/report";
  mqtt.subscribe(topic.c_str());

  // Request full status
  String reqTopic = String("device/") + p.serial + "/request";
  const char* pushall =
      "{\"pushing\":{\"sequence_id\":\"0\",\"command\":\"pushall\",\"version\":1,\"push_target\":1}}";
  mqtt.publish(reqTopic.c_str(), pushall);
  return true;
}

bool ensureConnected(const PrinterProfile& p) {
  if (mqtt.connected() && strcmp(liveSerial, p.serial) == 0) return true;
  if (mqtt.connected()) mqtt.disconnect();
  strlcpy(liveSerial, p.serial, sizeof(liveSerial));
  return connectMqtt(p);
}

bool publishCommand(const PrinterProfile& p, const char* json) {
  if (!ensureConnected(p)) return false;
  String topic = String("device/") + p.serial + "/request";
  return mqtt.publish(topic.c_str(), json);
}
}  // namespace

namespace Backends {

bool pollBambu(const PrinterProfile& p, PrinterState& out) {
  if (WiFi.status() != WL_CONNECTED) {
    out.online = false;
    out.status = PrinterStatus::Offline;
    strlcpy(out.statusText, "offline", sizeof(out.statusText));
    return false;
  }

  liveState = &out;
  if (!ensureConnected(p)) {
    out.online = false;
    out.status = PrinterStatus::Offline;
    strlcpy(out.statusText, "offline", sizeof(out.statusText));
    strlcpy(out.message, "mqtt connect failed", sizeof(out.message));
    return false;
  }

  uint32_t start = millis();
  while (millis() - start < 1500) {
    mqtt.loop();
    delay(10);
  }

  if (lastMsgMs == 0 || millis() - lastMsgMs > 20000) {
    // Still connected but stale — request again
    String reqTopic = String("device/") + p.serial + "/request";
    mqtt.publish(reqTopic.c_str(),
                 "{\"pushing\":{\"sequence_id\":\"1\",\"command\":\"pushall\",\"version\":1,\"push_target\":1}}");
  }

  out.online = mqtt.connected();
  if (!out.online) {
    out.status = PrinterStatus::Offline;
    strlcpy(out.statusText, "offline", sizeof(out.statusText));
  }
  return out.online;
}

bool actionBambu(const PrinterProfile& p, const char* action) {
  char buf[192];
  if (!strcmp(action, "pause")) {
    snprintf(buf, sizeof(buf),
             "{\"print\":{\"sequence_id\":\"10\",\"command\":\"pause\",\"param\":\"\"}}");
  } else if (!strcmp(action, "resume")) {
    snprintf(buf, sizeof(buf),
             "{\"print\":{\"sequence_id\":\"11\",\"command\":\"resume\",\"param\":\"\"}}");
  } else if (!strcmp(action, "cancel")) {
    snprintf(buf, sizeof(buf),
             "{\"print\":{\"sequence_id\":\"12\",\"command\":\"stop\",\"param\":\"\"}}");
  } else {
    return false;
  }
  return publishCommand(p, buf);
}

String testBambu(const PrinterProfile& p) {
  if (!p.host[0] || !p.serial[0] || !p.accessCode[0]) return "missing fields";
  PrinterState st;
  clearPrinterState(st);
  if (!pollBambu(p, st)) {
    if (st.message[0]) return String("unreachable: ") + st.message;
    return "unreachable";
  }
  return String("online:") + (st.statusText[0] ? st.statusText : "connected");
}

}  // namespace Backends
