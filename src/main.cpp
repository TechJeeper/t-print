#include <Arduino.h>

#include "board.h"
#include "board_pins.h"
#include "config_store.h"
#include "display_ui.h"
#include "printer_discover.h"
#include "printer_hub.h"
#include "setup_web.h"
#include "status_alert.h"
#include "wifi_manager.h"

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("T-Print %s\n", PP_VERSION);

  ConfigStore::begin();
  Board::begin();
  StatusAlert::begin();
  DisplayUi::begin();
  WifiManager::begin();
  PrinterHub::begin();
  SetupWeb::begin();

  if (WifiManager::isConnected()) {
    DisplayUi::showLanIp(WifiManager::ipString().c_str());
  } else {
    DisplayUi::showSetupHint("Connect phone Wi-Fi to", PP_SOFTAP_SSID, "Then open http://192.168.4.1");
  }

  Serial.printf("IP %s  printers %u\n", WifiManager::ipString().c_str(),
                (unsigned)ConfigStore::get().printerCount);
}

void loop() {
  WifiManager::loop();
  PrinterDiscover::loop();
  PrinterHub::loop();
  SetupWeb::loop();
  DisplayUi::loop();
  StatusAlert::loop();
  delay(5);
}
