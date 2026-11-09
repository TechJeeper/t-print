#include "display_ui.h"

#include <BoardDisplay.hpp>

#include "board.h"
#include "board_pins.h"
#include "camera_fetch.h"
#include "config_store.h"
#include "printer_hub.h"
#include "wifi_manager.h"

namespace {
BoardDisplay tft;
lgfx::LGFX_Sprite canvas(&tft);

enum class Screen : uint8_t { List, Detail, Actions, Confirm, Setup, LanReady };
Screen screen = Screen::List;
int selected = 0;
int actionIndex = 0;
int confirmAction = -1;  // pending Actions index awaiting Yes
int confirmChoice = 1;   // 0 = Yes, 1 = No (default No)
int detailPage = 0;  // 0 = status/temps, 1 = job/meta, 2 = camera (if snapshot URL)
uint32_t lastPaint = 0;
bool dirty = true;
char setupL1[48] = {0};
char setupL2[48] = {0};
char setupL3[48] = {0};

uint8_t* camImg = nullptr;
size_t camImgLen = 0;
CameraImageFormat camFmt = CameraImageFormat::Unknown;
uint32_t camFetchedMs = 0;
int camFetchedFor = -1;
bool camFetching = false;
char camStatus[40] = {0};
uint8_t appliedBl = 255;
bool lcdAwake = true;

void syncBacklight() {
  const uint16_t timeoutSec = ConfigStore::get().lcdTimeoutSec;
  const bool timedOut =
      timeoutSec != 0 && (millis() - Board::lastInputMs()) >= (uint32_t)timeoutSec * 1000UL;
  const bool wantOn = !Board::displayForcedOff() && !timedOut;
  const uint8_t bri = wantOn ? (uint8_t)(ConfigStore::get().brightness * 2.55f) : 0;
  if (bri != appliedBl) {
    appliedBl = bri;
    tft.setBrightness(bri);
  }
  lcdAwake = wantOn;
}

constexpr uint16_t COL_BG = 0x0841;
constexpr uint16_t COL_PANEL = 0x10A3;
constexpr uint16_t COL_TEXT = 0xEF7D;
constexpr uint16_t COL_MUTED = 0x8430;
constexpr uint16_t COL_ACCENT = 0xFB24;
constexpr uint16_t COL_OK = 0x1E4A;
constexpr uint16_t COL_WARN = 0xE4E4;
constexpr uint16_t COL_ERR = 0xF145;

void formatEta(uint32_t sec, char* out, size_t len) {
  if (!sec) {
    snprintf(out, len, "--:--");
    return;
  }
  uint32_t h = sec / 3600;
  uint32_t m = (sec % 3600) / 60;
  uint32_t s = sec % 60;
  if (h > 0) snprintf(out, len, "%luh %lum", (unsigned long)h, (unsigned long)m);
  else snprintf(out, len, "%lum %lus", (unsigned long)m, (unsigned long)s);
}

void drawTopBar(const char* title) {
  canvas.fillRect(0, 0, canvas.width(), 22, COL_PANEL);
  canvas.fillRect(0, 22, canvas.width(), 2, COL_ACCENT);
  canvas.setTextDatum(textdatum_t::middle_left);
  canvas.setTextColor(COL_TEXT, COL_PANEL);
  canvas.drawString(title, 8, 11, &fonts::Font2);

  char right[32];
  snprintf(right, sizeof(right), "v%s  %u%%", PP_VERSION, Board::batteryPercent());
  canvas.setTextDatum(textdatum_t::middle_right);
  canvas.setTextColor(COL_MUTED, COL_PANEL);
  canvas.drawString(right, canvas.width() - 8, 11, &fonts::Font0);
}

uint16_t statusColor(const PrinterState* st) {
  if (!st || !st->online) return COL_MUTED;
  if (st->status == PrinterStatus::Printing) return COL_OK;
  if (st->status == PrinterStatus::Error || st->status == PrinterStatus::Attention) return COL_ERR;
  if (st->status == PrinterStatus::Paused) return COL_WARN;
  return COL_OK;
}

void paintList() {
  canvas.fillScreen(COL_BG);
  drawTopBar("Fleet");
  size_t n = PrinterHub::count();
  if (!n) {
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.setTextColor(COL_MUTED, COL_BG);
    canvas.drawString("No printers yet — open setup on phone", canvas.width() / 2, 78, &fonts::Font2);
    canvas.setTextColor(COL_ACCENT, COL_BG);
    canvas.drawString(WifiManager::ipString().c_str(), canvas.width() / 2, 110, &fonts::Font2);
    canvas.setTextColor(COL_MUTED, COL_BG);
    canvas.drawString(PP_SOFTAP_SSID, canvas.width() / 2, 136, &fonts::Font0);
    return;
  }
  if (selected < 0) selected = 0;
  if ((size_t)selected >= n) selected = (int)n - 1;

  const int rowH = 44;
  const int startY = 30;
  int start = selected > 1 ? selected - 1 : 0;
  if (start + 3 > (int)n) start = max(0, (int)n - 3);

  for (int row = 0; row < 3; row++) {
    int i = start + row;
    if (i >= (int)n) break;
    const PrinterProfile* p = PrinterHub::profileAt(i);
    const PrinterState* st = PrinterHub::stateAt(i);
    bool on = i == selected;
    int y = startY + row * rowH;
    uint16_t bg = on ? COL_ACCENT : COL_PANEL;
    uint16_t fg = on ? COL_BG : COL_TEXT;
    uint16_t sub = on ? COL_BG : COL_MUTED;
    canvas.fillRoundRect(6, y, canvas.width() - 12, rowH - 4, 8, bg);
    canvas.setTextDatum(textdatum_t::middle_left);
    canvas.setTextColor(fg, bg);
    canvas.drawString(p->name, 14, y + 14, &fonts::Font2);
    char line[64];
    snprintf(line, sizeof(line), "%s · %.0f%% · Nz %.0f", st ? printerStatusName(st->status) : "?",
             st ? st->progress : 0.0f, st ? st->nozzleTemp : 0.0f);
    canvas.setTextColor(sub, bg);
    canvas.drawString(line, 14, y + 30, &fonts::Font0);
    canvas.setTextDatum(textdatum_t::middle_right);
    canvas.setTextColor(sub, bg);
    canvas.drawString(printerTypeName(p->type), canvas.width() - 16, y + 20, &fonts::Font0);
  }
}

void paintDetailPage0(const PrinterProfile* p, const PrinterState* st) {
  // Left column: status + progress + file
  canvas.fillRoundRect(4, 28, 156, 138, 8, COL_PANEL);
  canvas.setTextDatum(textdatum_t::top_left);
  canvas.setTextColor(statusColor(st), COL_PANEL);
  canvas.drawString(st->online ? st->statusText : "offline", 12, 34, &fonts::Font2);

  char buf[64];
  canvas.setTextColor(COL_TEXT, COL_PANEL);
  snprintf(buf, sizeof(buf), "%.1f%%", st->progress);
  canvas.drawString(buf, 12, 56, &fonts::Font4);

  int barW = 136;
  canvas.drawRoundRect(12, 90, barW, 7, 3, COL_MUTED);
  int fill = (int)(barW * constrain(st->progress, 0.0f, 100.0f) / 100.0f);
  if (fill > 0) canvas.fillRoundRect(12, 90, fill, 7, 3, COL_ACCENT);

  canvas.setTextColor(COL_MUTED, COL_PANEL);
  canvas.drawString("FILE", 12, 104, &fonts::Font0);
  canvas.setTextColor(COL_TEXT, COL_PANEL);
  char shortName[26];
  if (st->filename[0]) strlcpy(shortName, st->filename, sizeof(shortName));
  else strlcpy(shortName, "(none)", sizeof(shortName));
  canvas.drawString(shortName, 12, 116, &fonts::Font0);

  canvas.setTextColor(COL_MUTED, COL_PANEL);
  canvas.drawString("Click actions · hold back", 12, 148, &fonts::Font0);

  // Right: temps + layer/eta
  canvas.fillRoundRect(164, 28, 152, 44, 8, COL_PANEL);
  canvas.setTextColor(COL_MUTED, COL_PANEL);
  canvas.drawString("NOZZLE", 172, 32, &fonts::Font0);
  canvas.setTextColor(COL_TEXT, COL_PANEL);
  snprintf(buf, sizeof(buf), "%.1f / %.0f C", st->nozzleTemp, st->nozzleTarget);
  canvas.drawString(buf, 172, 46, &fonts::Font2);

  canvas.fillRoundRect(164, 76, 152, 44, 8, COL_PANEL);
  canvas.setTextColor(COL_MUTED, COL_PANEL);
  canvas.drawString("BED", 172, 80, &fonts::Font0);
  canvas.setTextColor(COL_TEXT, COL_PANEL);
  snprintf(buf, sizeof(buf), "%.1f / %.0f C", st->bedTemp, st->bedTarget);
  canvas.drawString(buf, 172, 94, &fonts::Font2);

  canvas.fillRoundRect(164, 124, 74, 42, 8, COL_PANEL);
  canvas.setTextColor(COL_MUTED, COL_PANEL);
  canvas.drawString("LAYER", 172, 128, &fonts::Font0);
  canvas.setTextColor(COL_TEXT, COL_PANEL);
  if (st->layerCount > 0) snprintf(buf, sizeof(buf), "%d/%d", st->layer, st->layerCount);
  else if (st->layer > 0) snprintf(buf, sizeof(buf), "%d", st->layer);
  else snprintf(buf, sizeof(buf), "--");
  canvas.drawString(buf, 172, 142, &fonts::Font2);

  canvas.fillRoundRect(242, 124, 74, 42, 8, COL_PANEL);
  canvas.setTextColor(COL_MUTED, COL_PANEL);
  canvas.drawString("ETA", 250, 128, &fonts::Font0);
  canvas.setTextColor(COL_TEXT, COL_PANEL);
  formatEta(st->etaSeconds, buf, sizeof(buf));
  canvas.drawString(buf, 250, 142, &fonts::Font2);
}

void fitText(char* dst, size_t dstLen, const char* src, int maxPx) {
  if (!dst || !dstLen) return;
  dst[0] = 0;
  if (!src || !src[0]) return;
  strlcpy(dst, src, dstLen);
  if (canvas.textWidth(dst, &fonts::Font0) <= maxPx) return;
  const char* ell = "...";
  const int ellW = canvas.textWidth(ell, &fonts::Font0);
  size_t n = strlen(dst);
  while (n > 0) {
    n--;
    dst[n] = 0;
    if (canvas.textWidth(dst, &fonts::Font0) + ellW <= maxPx) break;
  }
  strlcat(dst, ell, dstLen);
}

void paintDetailPage1(const PrinterProfile* p, const PrinterState* st) {
  canvas.fillRoundRect(4, 28, 312, 138, 8, COL_PANEL);
  canvas.setTextDatum(textdatum_t::top_left);

  char buf[80];
  int y = 36;
  auto row = [&](const char* label, const char* value) {
    canvas.setTextColor(COL_MUTED, COL_PANEL);
    canvas.drawString(label, 14, y, &fonts::Font0);
    canvas.setTextColor(COL_TEXT, COL_PANEL);
    canvas.drawString(value, 90, y, &fonts::Font0);
    y += 15;
  };

  // Temps live on the status page — keep this page to job/meta so FILE fits.
  snprintf(buf, sizeof(buf), "%s", printerTypeName(p->type));
  row("TYPE", buf);
  snprintf(buf, sizeof(buf), "%s:%u", p->host, (unsigned)p->port);
  row("HOST", buf);
  row("STATE", st->online ? st->statusText : "offline");
  snprintf(buf, sizeof(buf), "%.1f%%", st->progress);
  row("PROGRESS", buf);
  formatEta(st->etaSeconds, buf, sizeof(buf));
  row("ETA", buf);
  if (st->layerCount > 0) snprintf(buf, sizeof(buf), "%d / %d", st->layer, st->layerCount);
  else if (st->layer > 0) snprintf(buf, sizeof(buf), "%d", st->layer);
  else snprintf(buf, sizeof(buf), "--");
  row("LAYER", buf);

  if (st->message[0]) {
    canvas.setTextColor(COL_WARN, COL_PANEL);
    fitText(buf, sizeof(buf), st->message, 290);
    canvas.drawString(buf, 14, y, &fonts::Font0);
    y += 15;
  }

  canvas.setTextColor(COL_MUTED, COL_PANEL);
  canvas.drawString("FILE", 14, y, &fonts::Font0);
  canvas.setTextColor(COL_TEXT, COL_PANEL);
  if (st->filename[0]) fitText(buf, sizeof(buf), st->filename, 220);
  else strlcpy(buf, "(none)", sizeof(buf));
  canvas.drawString(buf, 90, y, &fonts::Font0);
}

bool printerHasCamera(const PrinterProfile* p) {
  return p && CameraFetch::available(*p);
}

int detailPageCount(const PrinterProfile* p) {
  return printerHasCamera(p) ? 3 : 2;
}

bool fetchSnapshot(const PrinterProfile* p) {
  if (!p) {
    strlcpy(camStatus, "no printer", sizeof(camStatus));
    return false;
  }
  camFetching = true;
  strlcpy(camStatus, "loading…", sizeof(camStatus));

  uint8_t* buf = nullptr;
  size_t len = 0;
  CameraImageFormat fmt = CameraImageFormat::Unknown;
  bool ok = CameraFetch::fetch(*p, &buf, &len, &fmt, camStatus, sizeof(camStatus));

  if (camImg) {
    free(camImg);
    camImg = nullptr;
    camImgLen = 0;
  }
  camFmt = CameraImageFormat::Unknown;
  camFetchedMs = millis();
  camFetchedFor = selected;
  camFetching = false;

  if (!ok || !buf) {
    if (buf) free(buf);
    return false;
  }
  camImg = buf;
  camImgLen = len;
  camFmt = fmt;
  return true;
}

bool jpegHasEoi(const uint8_t* data, size_t len) {
  if (!data || len < 4) return false;
  // Scan tail first — EOI is normally at the end.
  size_t start = len > 512 ? len - 512 : 2;
  for (size_t i = len - 1; i >= start; --i) {
    if (data[i - 1] == 0xff && data[i] == 0xd9) return true;
    if (i == start) break;
  }
  for (size_t i = 2; i < len; ++i) {
    if (data[i - 1] == 0xff && data[i] == 0xd9) return true;
  }
  return false;
}

bool jpegIsProgressive(const uint8_t* data, size_t len) {
  // SOF2 = 0xFFC2 progressive DCT — TJpgDec (LovyanGFX) cannot decode these.
  for (size_t i = 0; i + 1 < len; ++i) {
    if (data[i] != 0xff) continue;
    uint8_t m = data[i + 1];
    if (m == 0xc2) return true;
    if (m == 0xd9) break;
  }
  return false;
}

bool drawCamImage() {
  const int x = 8, y = 32, maxW = 304, maxH = 130;
  if (camFmt == CameraImageFormat::Png) {
    return canvas.drawPng(camImg, camImgLen, x, y, maxW, maxH, 0, 0, 0.0f);
  }
  if (camFmt != CameraImageFormat::Jpeg || !camImg || !camImgLen) return false;

  // Auto-fit first, then forced downscales for large webcam stills (TJpgDec is picky).
  const float scales[] = {0.0f, 0.5f, 0.25f, 0.125f};
  for (float s : scales) {
    canvas.fillRoundRect(4, 28, 312, 138, 8, COL_PANEL);
    if (canvas.drawJpg(camImg, camImgLen, x, y, maxW, maxH, 0, 0, s)) return true;
  }
  return false;
}

void paintDetailPage2(const PrinterProfile* p) {
  (void)p;
  canvas.fillRoundRect(4, 28, 312, 138, 8, COL_PANEL);
  canvas.setTextDatum(textdatum_t::middle_center);

  if (camFetching) {
    canvas.setTextColor(COL_MUTED, COL_PANEL);
    canvas.drawString("Loading camera…", canvas.width() / 2, 90, &fonts::Font2);
    return;
  }

  if (camImg && camImgLen && camFetchedFor == selected) {
    if (camFmt == CameraImageFormat::Jpeg && jpegIsProgressive(camImg, camImgLen)) {
      canvas.setTextColor(COL_ERR, COL_PANEL);
      canvas.drawString("Progressive JPEG", canvas.width() / 2, 82, &fonts::Font2);
      canvas.setTextColor(COL_MUTED, COL_PANEL);
      canvas.drawString("Use baseline snapshot URL", canvas.width() / 2, 110, &fonts::Font0);
      return;
    }
    if (camFmt == CameraImageFormat::Jpeg && !jpegHasEoi(camImg, camImgLen)) {
      canvas.setTextColor(COL_ERR, COL_PANEL);
      canvas.drawString("Truncated JPEG", canvas.width() / 2, 82, &fonts::Font2);
      canvas.setTextColor(COL_MUTED, COL_PANEL);
      canvas.drawString(camStatus[0] ? camStatus : "fetch incomplete", canvas.width() / 2, 110,
                        &fonts::Font0);
      return;
    }

    if (!drawCamImage()) {
      canvas.fillRoundRect(4, 28, 312, 138, 8, COL_PANEL);
      canvas.setTextDatum(textdatum_t::middle_center);
      canvas.setTextColor(COL_ERR, COL_PANEL);
      canvas.drawString("Decode failed", canvas.width() / 2, 82, &fonts::Font2);
      canvas.setTextColor(COL_MUTED, COL_PANEL);
      char detail[40];
      snprintf(detail, sizeof(detail), "%u KB · try smaller cam", (unsigned)(camImgLen / 1024));
      canvas.drawString(detail, canvas.width() / 2, 110, &fonts::Font0);
      return;
    }

    canvas.setTextDatum(textdatum_t::bottom_right);
    canvas.setTextColor(COL_MUTED, COL_PANEL);
    canvas.drawString(camStatus, canvas.width() - 12, 162, &fonts::Font0);
    return;
  }

  canvas.setTextColor(COL_MUTED, COL_PANEL);
  canvas.drawString(camStatus[0] ? camStatus : "No snapshot yet", canvas.width() / 2, 82, &fonts::Font2);
  canvas.setTextColor(COL_TEXT, COL_PANEL);
  canvas.drawString("Turn away & back to retry", canvas.width() / 2, 110, &fonts::Font0);
}

void paintDetail() {
  const PrinterProfile* p = PrinterHub::profileAt(selected);
  const PrinterState* st = PrinterHub::stateAt(selected);
  canvas.fillScreen(COL_BG);
  char title[40];
  int pages = detailPageCount(p);
  if (detailPage >= pages) detailPage = 0;
  if (p) snprintf(title, sizeof(title), "%s  (%d/%d)", p->name, detailPage + 1, pages);
  else snprintf(title, sizeof(title), "Printer");
  drawTopBar(title);
  if (!p || !st) return;
  if (detailPage == 0) paintDetailPage0(p, st);
  else if (detailPage == 1) paintDetailPage1(p, st);
  else paintDetailPage2(p);
}

const char* actionLabels[] = {"Pause", "Resume", "Cancel", "E-Stop", "Back"};
constexpr int kActionCount = 5;

bool actionNeedsConfirm(int idx) {
  // Pause, Cancel, E-Stop — Resume and Back run immediately.
  return idx == 0 || idx == 2 || idx == 3;
}

void paintActions() {
  canvas.fillScreen(COL_BG);
  drawTopBar("Actions");
  PrinterActions a = PrinterHub::actionsAt(selected);
  bool enabled[] = {a.canPause, a.canResume, a.canCancel, a.canEstop, true};

  const int gap = 6;
  const int btnH = 48;
  const int topW = 100;
  const int botW = 148;
  for (int i = 0; i < kActionCount; i++) {
    int x, y, w;
    if (i < 3) {
      x = 8 + i * (topW + gap);
      y = 40;
      w = topW;
    } else {
      x = 8 + (i - 3) * (botW + gap);
      y = 100;
      w = botW;
    }
    bool on = i == actionIndex;
    uint16_t bg = on ? COL_ACCENT : COL_PANEL;
    uint16_t fg = on ? COL_BG : (enabled[i] ? COL_TEXT : COL_MUTED);
    canvas.fillRoundRect(x, y, w, btnH, 8, bg);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.setTextColor(fg, bg);
    canvas.drawString(actionLabels[i], x + w / 2, y + btnH / 2, &fonts::Font2);
  }
}

void paintConfirm() {
  canvas.fillScreen(COL_BG);
  drawTopBar("Confirm");
  canvas.fillRoundRect(10, 36, canvas.width() - 20, 56, 10, COL_PANEL);
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setTextColor(COL_TEXT, COL_PANEL);
  char line[40];
  if (confirmAction == 3) snprintf(line, sizeof(line), "Emergency stop?");
  else if (confirmAction == 2) snprintf(line, sizeof(line), "Cancel this print?");
  else if (confirmAction == 0) snprintf(line, sizeof(line), "Pause this print?");
  else snprintf(line, sizeof(line), "Confirm?");
  canvas.drawString(line, canvas.width() / 2, 56, &fonts::Font2);
  canvas.setTextColor(COL_MUTED, COL_PANEL);
  canvas.drawString("Click to choose", canvas.width() / 2, 78, &fonts::Font0);

  const char* choices[] = {"Yes", "No"};
  const int btnW = 140;
  const int btnH = 44;
  const int gap = 12;
  const int y = 110;
  for (int i = 0; i < 2; i++) {
    int x = 16 + i * (btnW + gap);
    bool on = i == confirmChoice;
    // Yes uses warn/err accent when confirming destructive actions.
    uint16_t onBg = (i == 0 && (confirmAction == 2 || confirmAction == 3)) ? COL_ERR : COL_ACCENT;
    uint16_t bg = on ? onBg : COL_PANEL;
    uint16_t fg = on ? COL_BG : COL_TEXT;
    canvas.fillRoundRect(x, y, btnW, btnH, 8, bg);
    canvas.setTextColor(fg, bg);
    canvas.drawString(choices[i], x + btnW / 2, y + btnH / 2, &fonts::Font2);
  }
}

void paintSetup() {
  canvas.fillScreen(COL_BG);
  drawTopBar("Setup");
  canvas.fillRoundRect(10, 38, canvas.width() - 20, 118, 12, COL_PANEL);
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setTextColor(COL_MUTED, COL_PANEL);
  canvas.drawString(setupL1[0] ? setupL1 : "Connect phone Wi-Fi to", canvas.width() / 2, 62, &fonts::Font0);
  canvas.setTextColor(COL_ACCENT, COL_PANEL);
  canvas.drawString(setupL2[0] ? setupL2 : PP_SOFTAP_SSID, canvas.width() / 2, 92, &fonts::Font2);
  canvas.setTextColor(COL_TEXT, COL_PANEL);
  canvas.drawString(setupL3[0] ? setupL3 : "Then open http://192.168.4.1", canvas.width() / 2, 120, &fonts::Font2);
  canvas.setTextColor(COL_MUTED, COL_PANEL);
  canvas.drawString(WifiManager::ipString().c_str(), canvas.width() / 2, 142, &fonts::Font0);
}

void paintLanReady() {
  canvas.fillScreen(COL_BG);
  drawTopBar("Online");
  canvas.fillRoundRect(10, 38, canvas.width() - 20, 118, 12, COL_PANEL);
  canvas.setTextDatum(textdatum_t::middle_center);
  canvas.setTextColor(COL_MUTED, COL_PANEL);
  canvas.drawString(setupL1[0] ? setupL1 : "Joined Wi-Fi", canvas.width() / 2, 62, &fonts::Font0);
  canvas.setTextColor(COL_OK, COL_PANEL);
  canvas.drawString(setupL2[0] ? setupL2 : WifiManager::ipString().c_str(), canvas.width() / 2, 94,
                    &fonts::Font2);
  canvas.setTextColor(COL_TEXT, COL_PANEL);
  canvas.drawString(setupL3[0] ? setupL3 : "Open this IP on your phone", canvas.width() / 2, 128,
                    &fonts::Font0);
  canvas.setTextColor(COL_MUTED, COL_PANEL);
  canvas.drawString("Click to continue", canvas.width() / 2, 146, &fonts::Font0);
}

void paint() {
  switch (screen) {
    case Screen::List: paintList(); break;
    case Screen::Detail: paintDetail(); break;
    case Screen::Actions: paintActions(); break;
    case Screen::Confirm: paintConfirm(); break;
    case Screen::Setup: paintSetup(); break;
    case Screen::LanReady: paintLanReady(); break;
  }
  canvas.pushSprite(0, 0);
  lastPaint = millis();
  dirty = false;
}

bool isStaticScreen() {
  return screen == Screen::Setup || screen == Screen::LanReady;
}

void executeAction(int idx) {
  switch (idx) {
    case 0: PrinterHub::pause(selected); break;
    case 1: PrinterHub::resume(selected); break;
    case 2: PrinterHub::cancel(selected); break;
    case 3: PrinterHub::estop(selected); break;
    default: break;
  }
  confirmAction = -1;
  screen = Screen::Detail;
  dirty = true;
}

void runAction(int idx) {
  PrinterActions a = PrinterHub::actionsAt(selected);
  bool enabled[] = {a.canPause, a.canResume, a.canCancel, a.canEstop, true};
  if (idx < 0 || idx >= kActionCount) return;
  if (!enabled[idx]) return;

  if (idx == 4) {
    screen = Screen::Detail;
    dirty = true;
    return;
  }
  if (actionNeedsConfirm(idx)) {
    confirmAction = idx;
    confirmChoice = 1;  // default to No
    screen = Screen::Confirm;
    dirty = true;
    return;
  }
  executeAction(idx);
}

void writeBmpHeader(uint8_t* hdr, int w, int h, size_t pixelBytes) {
  // 24-bit BMP, bottom-up
  const uint32_t fileSize = 54 + (uint32_t)pixelBytes;
  memset(hdr, 0, 54);
  hdr[0] = 'B';
  hdr[1] = 'M';
  hdr[2] = fileSize & 0xff;
  hdr[3] = (fileSize >> 8) & 0xff;
  hdr[4] = (fileSize >> 16) & 0xff;
  hdr[5] = (fileSize >> 24) & 0xff;
  hdr[10] = 54;
  hdr[14] = 40;
  hdr[18] = w & 0xff;
  hdr[19] = (w >> 8) & 0xff;
  hdr[22] = h & 0xff;
  hdr[23] = (h >> 8) & 0xff;
  hdr[26] = 1;
  hdr[28] = 24;
}
}  // namespace

namespace DisplayUi {

void begin() {
  pinMode(BOARD_PWR_EN, OUTPUT);
  digitalWrite(BOARD_PWR_EN, HIGH);
  delay(20);
  tft.init();
  // LilyGO landscape for T-Embed CC1101 is rotation 3 (320×170).
  tft.setRotation(3);
  appliedBl = 255;
  lcdAwake = true;
  syncBacklight();

  // Offscreen canvas in PSRAM for clean UI + screenshots
  canvas.setColorDepth(16);
  canvas.setPsram(true);
  const int w = tft.width();   // 320
  const int h = tft.height();  // 170
  if (!canvas.createSprite(w, h)) {
    canvas.setPsram(false);
    canvas.createSprite(w, h);
  }
  canvas.fillScreen(COL_BG);
  dirty = true;
}

void showSetupHint(const char* line1, const char* line2, const char* line3) {
  strlcpy(setupL1, line1 ? line1 : "", sizeof(setupL1));
  strlcpy(setupL2, line2 ? line2 : "", sizeof(setupL2));
  strlcpy(setupL3, line3 ? line3 : "", sizeof(setupL3));
  screen = Screen::Setup;
  dirty = true;
}

void showLanIp(const char* ip) {
  strlcpy(setupL1, "Joined Wi-Fi", sizeof(setupL1));
  strlcpy(setupL2, ip && ip[0] ? ip : WifiManager::ipString().c_str(), sizeof(setupL2));
  strlcpy(setupL3, "Reconnect phone · open this IP", sizeof(setupL3));
  screen = Screen::LanReady;
  dirty = true;
}

void invalidate() { dirty = true; }

bool captureScreenshotBmp(uint8_t** outBuf, size_t* outLen) {
  if (!outBuf || !outLen) return false;
  *outBuf = nullptr;
  *outLen = 0;
  if (!canvas.width() || !canvas.height()) return false;

  // Ensure latest frame is painted into the sprite
  dirty = true;
  paint();

  const int w = canvas.width();
  const int h = canvas.height();
  const int rowStride = ((w * 3 + 3) / 4) * 4;  // BMP rows padded to 4 bytes
  const size_t pixelBytes = (size_t)rowStride * h;
  const size_t total = 54 + pixelBytes;
  uint8_t* buf = (uint8_t*)ps_malloc(total);
  if (!buf) buf = (uint8_t*)malloc(total);
  if (!buf) return false;
  memset(buf, 0, total);
  writeBmpHeader(buf, w, h, pixelBytes);

  uint8_t* dst = buf + 54;
  for (int y = h - 1; y >= 0; --y) {
    uint8_t* row = dst + (size_t)(h - 1 - y) * rowStride;
    for (int x = 0; x < w; ++x) {
      uint16_t c = canvas.readPixel(x, y);
      uint8_t r = ((c >> 11) & 0x1f) * 255 / 31;
      uint8_t g = ((c >> 5) & 0x3f) * 255 / 63;
      uint8_t b = (c & 0x1f) * 255 / 31;
      row[x * 3 + 0] = b;
      row[x * 3 + 1] = g;
      row[x * 3 + 2] = r;
    }
  }

  *outBuf = buf;
  *outLen = total;
  return true;
}

void loop() {
  Board::tickEncoder();
  int d = Board::encoderDelta();
  bool click = Board::encoderClicked();
  // Encoder long-press = Back. Top button is reserved for display/power sleep.
  bool back = Board::encoderLongPressed();

  if (Board::userKeyLongPressed()) {
    tft.setBrightness(0);
    appliedBl = 0;
    lcdAwake = false;
    Board::enterDeepSleep();  // does not return
  }
  if (Board::userKeyClicked()) {
    Board::setDisplayForcedOff(!Board::displayForcedOff());
    dirty = true;
  }

  const bool wasAwake = lcdAwake;
  syncBacklight();
  // First input after sleep only wakes the display — don't navigate.
  if (!wasAwake && lcdAwake) {
    dirty = true;
    d = 0;
    click = false;
    back = false;
  }

  // Keep unprovisioned devices on the setup card without fighting other screens each frame.
  static bool offeredSetup = false;
  if (WifiManager::isSoftAP() && !WifiManager::isConnected() && PrinterHub::count() == 0 &&
      screen != Screen::LanReady && screen != Screen::Setup && !offeredSetup) {
    screen = Screen::Setup;
    dirty = true;
    offeredSetup = true;
  }
  if (WifiManager::isConnected() || PrinterHub::count() > 0) {
    offeredSetup = false;
  }

  if (d || click || back) dirty = true;

  if (screen == Screen::List) {
    if (d) selected += d;
    if (click && PrinterHub::count()) {
      detailPage = 0;
      screen = Screen::Detail;
    }
    if (back) {
      screen = Screen::Setup;
      dirty = true;
    }
  } else if (screen == Screen::Detail) {
    const PrinterProfile* p = PrinterHub::profileAt(selected);
    const int pages = detailPageCount(p);
    if (d) {
      detailPage += (d > 0 ? 1 : -1);
      if (detailPage < 0) detailPage = pages - 1;
      if (detailPage >= pages) detailPage = 0;
    }
    if (click) {
      actionIndex = 0;
      screen = Screen::Actions;
    }
    if (back) screen = Screen::List;
  } else if (screen == Screen::Actions) {
    if (d) {
      actionIndex += d;
      while (actionIndex < 0) actionIndex += kActionCount;
      actionIndex %= kActionCount;
    }
    if (click) runAction(actionIndex);
    if (back) screen = Screen::Detail;
  } else if (screen == Screen::Confirm) {
    if (d) {
      confirmChoice += (d > 0 ? 1 : -1);
      if (confirmChoice < 0) confirmChoice = 1;
      if (confirmChoice > 1) confirmChoice = 0;
    }
    if (click) {
      if (confirmChoice == 0 && confirmAction >= 0) executeAction(confirmAction);
      else {
        confirmAction = -1;
        screen = Screen::Actions;
        dirty = true;
      }
    }
    if (back) {
      confirmAction = -1;
      screen = Screen::Actions;
    }
  } else if (screen == Screen::Setup) {
    // Stay put on setup — only leave on intentional click (not a held/noisy key)
    if (click) screen = Screen::List;
  } else if (screen == Screen::LanReady) {
    if (click || back) screen = Screen::List;
  }

  if (!lcdAwake) return;

  // Paint before any network work so encoder page turns stay snappy.
  const bool due = !isStaticScreen() && (millis() - lastPaint > 2000);
  if (dirty || due) paint();

  // Camera fetch only after paint, and never on the same turn as encoder input.
  if (screen == Screen::Detail && detailPage == 2 && !d && !click && !back && !camFetching) {
    const PrinterProfile* p = PrinterHub::profileAt(selected);
    if (printerHasCamera(p) && WifiManager::isConnected()) {
      bool stale = (camFetchedFor != selected) || (millis() - camFetchedMs > 8000) || !camImgLen;
      if (stale) {
        fetchSnapshot(p);
        dirty = true;
        paint();
      }
    }
  }
}

}  // namespace DisplayUi
