#include "printer_hub.h"

#include "backends.h"

namespace {
PrinterState states[kMaxPrinters];
uint32_t lastPoll[kMaxPrinters];
size_t activeCount = 0;
constexpr uint32_t kPollMs = 2000;
}  // namespace

namespace PrinterHub {

void begin() {
  for (size_t i = 0; i < kMaxPrinters; i++) {
    clearPrinterState(states[i]);
    lastPoll[i] = 0;
  }
  reloadProfiles();
}

void reloadProfiles() { activeCount = ConfigStore::get().printerCount; }

void loop() {
  DeviceConfig& cfg = ConfigStore::get();
  activeCount = cfg.printerCount;
  uint32_t now = millis();
  for (size_t i = 0; i < cfg.printerCount; i++) {
    if (!cfg.printers[i].enabled) continue;
    if (now - lastPoll[i] < kPollMs) continue;
    lastPoll[i] = now;
    PrinterState& st = states[i];
    switch (cfg.printers[i].type) {
      case PrinterType::Klipper:
        Backends::pollKlipper(cfg.printers[i], st);
        break;
      case PrinterType::Prusa:
        Backends::pollPrusa(cfg.printers[i], st);
        break;
      case PrinterType::Bambu:
        Backends::pollBambu(cfg.printers[i], st);
        break;
    }
  }
}

size_t count() { return ConfigStore::get().printerCount; }

const PrinterProfile* profileAt(size_t i) {
  if (i >= ConfigStore::get().printerCount) return nullptr;
  return &ConfigStore::get().printers[i];
}

const PrinterState* stateAt(size_t i) {
  if (i >= ConfigStore::get().printerCount) return nullptr;
  return &states[i];
}

PrinterActions actionsAt(size_t i) {
  PrinterActions a{};
  const PrinterState* st = stateAt(i);
  if (!st || !st->online) return a;
  a.canEstop = (profileAt(i)->type == PrinterType::Klipper);
  a.canSetTemp = (profileAt(i)->type == PrinterType::Klipper);
  a.canPause = st->status == PrinterStatus::Printing;
  a.canResume = st->status == PrinterStatus::Paused;
  a.canCancel = st->status == PrinterStatus::Printing || st->status == PrinterStatus::Paused;
  return a;
}

bool pause(size_t i) {
  const PrinterProfile* p = profileAt(i);
  if (!p) return false;
  switch (p->type) {
    case PrinterType::Klipper: return Backends::actionKlipper(*p, "pause");
    case PrinterType::Prusa: return Backends::actionPrusa(*p, "pause");
    case PrinterType::Bambu: return Backends::actionBambu(*p, "pause");
  }
  return false;
}

bool resume(size_t i) {
  const PrinterProfile* p = profileAt(i);
  if (!p) return false;
  switch (p->type) {
    case PrinterType::Klipper: return Backends::actionKlipper(*p, "resume");
    case PrinterType::Prusa: return Backends::actionPrusa(*p, "resume");
    case PrinterType::Bambu: return Backends::actionBambu(*p, "resume");
  }
  return false;
}

bool cancel(size_t i) {
  const PrinterProfile* p = profileAt(i);
  if (!p) return false;
  switch (p->type) {
    case PrinterType::Klipper: return Backends::actionKlipper(*p, "cancel");
    case PrinterType::Prusa: return Backends::actionPrusa(*p, "cancel");
    case PrinterType::Bambu: return Backends::actionBambu(*p, "cancel");
  }
  return false;
}

bool estop(size_t i) {
  const PrinterProfile* p = profileAt(i);
  if (!p || p->type != PrinterType::Klipper) return false;
  return Backends::actionKlipper(*p, "estop");
}

bool setNozzleTemp(size_t i, float c) {
  const PrinterProfile* p = profileAt(i);
  if (!p || p->type != PrinterType::Klipper) return false;
  return Backends::actionKlipper(*p, "nozzle", c);
}

bool setBedTemp(size_t i, float c) {
  const PrinterProfile* p = profileAt(i);
  if (!p || p->type != PrinterType::Klipper) return false;
  return Backends::actionKlipper(*p, "bed", c);
}

String testConnection(const PrinterProfile& p) {
  switch (p.type) {
    case PrinterType::Klipper: return Backends::testKlipper(p);
    case PrinterType::Prusa: return Backends::testPrusa(p);
    case PrinterType::Bambu: return Backends::testBambu(p);
  }
  return "unknown type";
}

}  // namespace PrinterHub
