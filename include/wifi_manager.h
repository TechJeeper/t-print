#pragma once

#include <Arduino.h>
#include <vector>

struct WifiNetwork {
  String ssid;
  int32_t rssi;
  bool open;
};

enum class WifiScanStatus : uint8_t { Idle = 0, Running, Done, Failed };

enum class WifiJoinStatus : uint8_t { Idle = 0, Connecting, Connected, Failed };

namespace WifiManager {
void begin();
void loop();
bool isConnected();
bool isSoftAP();
IPAddress localIP();
String ipString();

// Blocking connect (legacy). Prefer beginJoin + pollJoin for SoftAP setup UI.
bool connectStation(const char* ssid, const char* pass, uint32_t timeoutMs = 20000);

// SoftAP-safe join: save creds, start STA, keep SoftAP up so the phone can poll.
bool beginJoin(const char* ssid, const char* pass);
WifiJoinStatus joinStatus();
const char* joinError();

void startSoftAP();
void stopSoftAP();
void scheduleStopSoftAP(uint32_t delayMs);

// Non-blocking SoftAP-safe scan (start + poll). Avoids "Failed to fetch".
bool startScan();
WifiScanStatus scanStatus();
const std::vector<WifiNetwork>& scanResults();
const char* scanError();

void ensureMdns();
}  // namespace WifiManager
