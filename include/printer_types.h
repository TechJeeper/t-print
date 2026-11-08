#pragma once

#include <Arduino.h>

enum class PrinterType : uint8_t {
  Klipper = 0,
  Bambu = 1,
  Prusa = 2,
};

enum class PrinterStatus : uint8_t {
  Unknown = 0,
  Offline,
  Idle,
  Printing,
  Paused,
  Complete,
  Error,
  Attention,
};

struct PrinterProfile {
  char id[12];
  char name[32];
  PrinterType type;
  char host[64];
  uint16_t port;
  char apiKey[64];      // Moonraker / PrusaLink
  char serial[32];      // Bambu
  char accessCode[16];  // Bambu LAN
  char snapshotUrl[128];  // e.g. http://host/webcam/snapshot.jpg
  bool enabled;
};

struct PrinterState {
  bool online;
  PrinterStatus status;
  char statusText[24];
  char filename[64];
  float progress;       // 0-100
  float nozzleTemp;
  float nozzleTarget;
  float bedTemp;
  float bedTarget;
  int layer;
  int layerCount;
  uint32_t etaSeconds;
  char message[80];
  uint32_t lastUpdateMs;
};

struct PrinterActions {
  bool canPause;
  bool canResume;
  bool canCancel;
  bool canEstop;
  bool canSetTemp;
};

inline const char* printerTypeName(PrinterType t) {
  switch (t) {
    case PrinterType::Klipper: return "klipper";
    case PrinterType::Bambu: return "bambu";
    case PrinterType::Prusa: return "prusa";
    default: return "unknown";
  }
}

inline PrinterType printerTypeFromName(const char* name) {
  if (!name) return PrinterType::Klipper;
  if (strcasecmp(name, "bambu") == 0) return PrinterType::Bambu;
  if (strcasecmp(name, "prusa") == 0) return PrinterType::Prusa;
  return PrinterType::Klipper;
}

inline const char* printerStatusName(PrinterStatus s) {
  switch (s) {
    case PrinterStatus::Offline: return "offline";
    case PrinterStatus::Idle: return "idle";
    case PrinterStatus::Printing: return "printing";
    case PrinterStatus::Paused: return "paused";
    case PrinterStatus::Complete: return "complete";
    case PrinterStatus::Error: return "error";
    case PrinterStatus::Attention: return "attention";
    default: return "unknown";
  }
}

inline void clearPrinterState(PrinterState& st) {
  st.online = false;
  st.status = PrinterStatus::Unknown;
  st.statusText[0] = 0;
  st.filename[0] = 0;
  st.progress = 0;
  st.nozzleTemp = 0;
  st.nozzleTarget = 0;
  st.bedTemp = 0;
  st.bedTarget = 0;
  st.layer = 0;
  st.layerCount = 0;
  st.etaSeconds = 0;
  st.message[0] = 0;
  st.lastUpdateMs = 0;
}