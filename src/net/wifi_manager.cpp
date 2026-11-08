#include "wifi_manager.h"

#include <algorithm>
#include <WiFi.h>
#include <ESPmDNS.h>

#include "board_pins.h"
#include "config_store.h"
#include "display_ui.h"

namespace {
bool softApUp = false;
bool mdnsUp = false;
uint32_t lastReconnectMs = 0;
uint32_t softApStopAt = 0;

WifiScanStatus scanState = WifiScanStatus::Idle;
std::vector<WifiNetwork> cachedScan;
String scanErr;
uint32_t scanStartedMs = 0;

WifiJoinStatus joinState = WifiJoinStatus::Idle;
String joinErr;
uint32_t joinStartedMs = 0;
uint32_t joinTimeoutMs = 25000;
char joinSsid[kSsidLen] = {0};
char joinPass[kPassLen] = {0};

void onWiFiEvent(WiFiEvent_t event) {
  if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
    mdnsUp = false;
  }
}

void collectScanResults(int n) {
  cachedScan.clear();
  for (int i = 0; i < n; i++) {
    WifiNetwork net;
    net.ssid = WiFi.SSID(i);
    net.rssi = WiFi.RSSI(i);
    net.open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    if (net.ssid.length()) cachedScan.push_back(net);
  }
  std::sort(cachedScan.begin(), cachedScan.end(),
            [](const WifiNetwork& a, const WifiNetwork& b) { return a.rssi > b.rssi; });
  WiFi.scanDelete();
}

void finishJoinSuccess() {
  DeviceConfig& cfg = ConfigStore::get();
  strlcpy(cfg.wifiSsid, joinSsid, kSsidLen);
  strlcpy(cfg.wifiPass, joinPass, kPassLen);
  cfg.wifiConfigured = true;
  ConfigStore::save();
  WifiManager::ensureMdns();
  DisplayUi::showLanIp(WiFi.localIP().toString().c_str());
  joinState = WifiJoinStatus::Connected;
  joinErr = "";
  // Keep SoftAP briefly so the phone (still on T-Print AP) can read the IP.
  softApStopAt = millis() + 8000;
}

void tickJoin() {
  if (joinState != WifiJoinStatus::Connecting) return;

  if (WiFi.status() == WL_CONNECTED) {
    finishJoinSuccess();
    return;
  }

  if (millis() - joinStartedMs > joinTimeoutMs) {
    joinState = WifiJoinStatus::Failed;
    joinErr = "timed out — check password / 2.4 GHz Wi‑Fi";
    WiFi.disconnect(false);
  }
}
}  // namespace

namespace WifiManager {

void begin() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.onEvent(onWiFiEvent);
  WiFi.setHostname(ConfigStore::get().deviceName);
  WiFi.setSleep(false);

  DeviceConfig& cfg = ConfigStore::get();
  if (cfg.wifiConfigured && cfg.wifiSsid[0]) {
    WiFi.begin(cfg.wifiSsid, cfg.wifiPass);
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
      delay(200);
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    startSoftAP();
  } else {
    ensureMdns();
  }
}

void loop() {
  if (scanState == WifiScanStatus::Running) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) {
      if (millis() - scanStartedMs > 20000) {
        WiFi.scanDelete();
        scanState = WifiScanStatus::Failed;
        scanErr = "scan timed out";
      }
    } else if (n == WIFI_SCAN_FAILED) {
      scanState = WifiScanStatus::Failed;
      scanErr = "scan failed";
      WiFi.scanDelete();
    } else if (n >= 0) {
      collectScanResults(n);
      scanState = WifiScanStatus::Done;
      scanErr = "";
    }
  }

  tickJoin();

  if (softApStopAt && (int32_t)(millis() - softApStopAt) >= 0) {
    softApStopAt = 0;
    stopSoftAP();
  }

  if (WiFi.status() == WL_CONNECTED) {
    ensureMdns();
    return;
  }

  // Don't auto-reconnect while a SoftAP join attempt is in progress.
  if (joinState == WifiJoinStatus::Connecting) return;

  DeviceConfig& cfg = ConfigStore::get();
  if (cfg.wifiConfigured && cfg.wifiSsid[0] && millis() - lastReconnectMs > 10000) {
    lastReconnectMs = millis();
    if (scanState != WifiScanStatus::Running) {
      WiFi.disconnect();
      WiFi.begin(cfg.wifiSsid, cfg.wifiPass);
    }
  }

  if (!softApUp && !softApStopAt) startSoftAP();
}

bool isConnected() { return WiFi.status() == WL_CONNECTED; }
bool isSoftAP() { return softApUp; }

IPAddress localIP() {
  if (WiFi.status() == WL_CONNECTED) return WiFi.localIP();
  return WiFi.softAPIP();
}

String ipString() { return localIP().toString(); }

bool beginJoin(const char* ssid, const char* pass) {
  if (!ssid || !ssid[0]) {
    joinState = WifiJoinStatus::Failed;
    joinErr = "ssid required";
    return false;
  }
  if (scanState == WifiScanStatus::Running) {
    WiFi.scanDelete();
    scanState = WifiScanStatus::Idle;
  }

  strlcpy(joinSsid, ssid, sizeof(joinSsid));
  strlcpy(joinPass, pass ? pass : "", sizeof(joinPass));
  joinErr = "";
  joinState = WifiJoinStatus::Connecting;
  joinStartedMs = millis();
  softApStopAt = 0;

  // Stay AP+STA so the phone keeps SoftAP while we join home Wi‑Fi.
  WiFi.mode(WIFI_AP_STA);
  if (!softApUp) startSoftAP();
  WiFi.disconnect(false);
  delay(50);
  WiFi.begin(joinSsid, joinPass);
  return true;
}

WifiJoinStatus joinStatus() { return joinState; }
const char* joinError() { return joinErr.c_str(); }

bool connectStation(const char* ssid, const char* pass, uint32_t timeoutMs) {
  if (!beginJoin(ssid, pass)) return false;
  joinTimeoutMs = timeoutMs;
  uint32_t start = millis();
  while (joinState == WifiJoinStatus::Connecting && millis() - start < timeoutMs + 500) {
    tickJoin();
    delay(50);
  }
  joinTimeoutMs = 25000;
  return joinState == WifiJoinStatus::Connected;
}

void startSoftAP() {
  if (softApUp) return;
  WiFi.mode(WIFI_AP_STA);
  bool ok = WiFi.softAP(PP_SOFTAP_SSID);
  softApUp = ok;
}

void stopSoftAP() {
  if (!softApUp) return;
  WiFi.softAPdisconnect(true);
  softApUp = false;
}

void scheduleStopSoftAP(uint32_t delayMs) {
  softApStopAt = millis() + delayMs;
}

bool startScan() {
  if (scanState == WifiScanStatus::Running) return true;
  WiFi.mode(WIFI_AP_STA);
  cachedScan.clear();
  scanErr = "";
  WiFi.scanDelete();
  int16_t rc = WiFi.scanNetworks(true, true);
  if (rc == WIFI_SCAN_FAILED) {
    scanState = WifiScanStatus::Failed;
    scanErr = "could not start scan";
    return false;
  }
  scanState = WifiScanStatus::Running;
  scanStartedMs = millis();
  return true;
}

WifiScanStatus scanStatus() { return scanState; }

const std::vector<WifiNetwork>& scanResults() { return cachedScan; }

const char* scanError() { return scanErr.c_str(); }

void ensureMdns() {
  if (mdnsUp || WiFi.status() != WL_CONNECTED) return;
  const char* name = ConfigStore::get().deviceName;
  if (MDNS.begin(name)) {
    MDNS.addService("http", "tcp", 80);
    mdnsUp = true;
  }
}

}  // namespace WifiManager
