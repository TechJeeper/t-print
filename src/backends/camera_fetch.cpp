#include "camera_fetch.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

namespace {
constexpr size_t kMaxImage = 180 * 1024;

CameraImageFormat detectFormat(const uint8_t* data, size_t len) {
  if (len >= 2 && data[0] == 0xff && data[1] == 0xd8) return CameraImageFormat::Jpeg;
  if (len >= 8 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G')
    return CameraImageFormat::Png;
  return CameraImageFormat::Unknown;
}

// Some proxies return multipart even for "snapshot" URLs — find the image payload.
size_t findImageOffset(const uint8_t* data, size_t len, CameraImageFormat* fmt) {
  *fmt = detectFormat(data, len);
  if (*fmt != CameraImageFormat::Unknown) return 0;

  for (size_t i = 0; i + 3 < len; i++) {
    if (data[i] == 0xff && data[i + 1] == 0xd8) {
      *fmt = CameraImageFormat::Jpeg;
      return i;
    }
    if (i + 8 < len && data[i] == 0x89 && data[i + 1] == 'P' && data[i + 2] == 'N' &&
        data[i + 3] == 'G') {
      *fmt = CameraImageFormat::Png;
      return i;
    }
  }
  return 0;
}

const char* describeUnknown(const uint8_t* data, size_t len) {
  if (!len) return "empty body";
  if (data[0] == '<' || (len > 4 && data[0] == 'H' && data[1] == 'T' && data[2] == 'T'))
    return "got HTML/text";
  if (data[0] == '-' && data[1] == '-') return "multipart/no image";
  return "not JPEG/PNG";
}

bool readExact(WiFiClient& c, uint8_t* dst, size_t n, uint32_t timeoutMs) {
  size_t got = 0;
  uint32_t start = millis();
  while (got < n) {
    size_t avail = c.available();
    if (avail) {
      got += c.readBytes(dst + got, min(avail, n - got));
      start = millis();
    } else {
      if (!c.connected() && !c.available()) return false;
      if (millis() - start > timeoutMs) return false;
      delay(1);
      yield();
    }
  }
  return true;
}

bool fetchHttp(const char* url, const char* apiKey, uint8_t** outBuf, size_t* outLen,
               CameraImageFormat* format, char* status, size_t statusLen) {
  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(8000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.useHTTP10(true);  // avoid chunked quirks on some webcamd builds

  if (!http.begin(url)) {
    snprintf(status, statusLen, "bad URL");
    return false;
  }
  if (apiKey && apiKey[0]) http.addHeader("X-Api-Key", apiKey);
  http.addHeader("Accept", "image/jpeg,image/jpg,image/png,*/*");
  http.setUserAgent("T-Print/1.0");

  int code = http.GET();
  if (code <= 0) {
    snprintf(status, statusLen, "connect fail %d", code);
    http.end();
    return false;
  }
  if (code != 200) {
    snprintf(status, statusLen, "HTTP %d", code);
    http.end();
    return false;
  }

  int contentLen = http.getSize();
  WiFiClient* stream = http.getStreamPtr();
  if (!stream) {
    snprintf(status, statusLen, "no body");
    http.end();
    return false;
  }

  size_t cap = (contentLen > 0 && (size_t)contentLen < kMaxImage) ? (size_t)contentLen : kMaxImage;
  uint8_t* scratch = (uint8_t*)ps_malloc(cap);
  if (!scratch) scratch = (uint8_t*)malloc(cap);
  if (!scratch) {
    snprintf(status, statusLen, "out of memory");
    http.end();
    return false;
  }

  size_t got = 0;
  uint32_t idleStart = millis();
  uint32_t hardDeadline = millis() + 10000;
  while (got < cap && millis() < hardDeadline) {
    int avail = stream->available();
    if (avail > 0) {
      size_t take = min((size_t)avail, cap - got);
      int n = stream->readBytes(scratch + got, take);
      if (n > 0) {
        got += (size_t)n;
        idleStart = millis();
        if (contentLen > 0 && (int)got >= contentLen) break;
      }
    } else {
      // Don't require http.connected() — ESP32 often clears it while bytes remain.
      if (!stream->connected() && !http.connected()) break;
      if (millis() - idleStart > 1500) break;
      delay(1);
      yield();
    }
  }
  http.end();

  CameraImageFormat fmt = CameraImageFormat::Unknown;
  size_t off = findImageOffset(scratch, got, &fmt);
  if (fmt == CameraImageFormat::Unknown) {
    snprintf(status, statusLen, "%s (%u B)", describeUnknown(scratch, got), (unsigned)got);
    free(scratch);
    return false;
  }

  size_t imgLen = got - off;
  uint8_t* copy = (uint8_t*)ps_malloc(imgLen);
  if (!copy) copy = (uint8_t*)malloc(imgLen);
  if (!copy) {
    free(scratch);
    snprintf(status, statusLen, "out of memory");
    return false;
  }
  memcpy(copy, scratch + off, imgLen);
  free(scratch);

  *format = fmt;
  *outBuf = copy;
  *outLen = imgLen;
  snprintf(status, statusLen, "%u KB", (unsigned)(imgLen / 1024));
  return true;
}

bool fetchBambuJpeg(const PrinterProfile& p, uint8_t** outBuf, size_t* outLen,
                    CameraImageFormat* format, char* status, size_t statusLen) {
  if (!p.host[0] || !p.accessCode[0]) {
    snprintf(status, statusLen, "need access code");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(6000);
  if (!client.connect(p.host, 6000)) {
    snprintf(status, statusLen, "cam :6000 fail");
    return false;
  }

  uint8_t auth[80];
  memset(auth, 0, sizeof(auth));
  uint32_t payloadSize = 0x40;
  uint32_t type = 0x3000;
  memcpy(auth + 0, &payloadSize, 4);
  memcpy(auth + 4, &type, 4);
  strlcpy((char*)(auth + 16), "bblp", 32);
  strlcpy((char*)(auth + 48), p.accessCode, 32);
  if (client.write(auth, sizeof(auth)) != sizeof(auth)) {
    snprintf(status, statusLen, "cam auth write");
    client.stop();
    return false;
  }

  uint8_t hdr[16];
  if (!readExact(client, hdr, sizeof(hdr), 6000)) {
    snprintf(status, statusLen, "cam hdr timeout");
    client.stop();
    return false;
  }
  uint32_t jpegLen = 0;
  memcpy(&jpegLen, hdr, 4);
  if (jpegLen < 64 || jpegLen > kMaxImage) {
    snprintf(status, statusLen, "cam bad size");
    client.stop();
    return false;
  }

  uint8_t* buf = (uint8_t*)ps_malloc(jpegLen);
  if (!buf) buf = (uint8_t*)malloc(jpegLen);
  if (!buf) {
    snprintf(status, statusLen, "out of memory");
    client.stop();
    return false;
  }
  if (!readExact(client, buf, jpegLen, 8000)) {
    free(buf);
    snprintf(status, statusLen, "cam read fail");
    client.stop();
    return false;
  }
  client.stop();

  *format = detectFormat(buf, jpegLen);
  if (*format != CameraImageFormat::Jpeg) {
    free(buf);
    snprintf(status, statusLen, "cam not JPEG");
    return false;
  }
  *outBuf = buf;
  *outLen = jpegLen;
  snprintf(status, statusLen, "bambu %u KB", (unsigned)(jpegLen / 1024));
  return true;
}

void buildDefaultUrl(const PrinterProfile& p, char* out, size_t outLen) {
  out[0] = 0;
  if (p.snapshotUrl[0]) {
    strlcpy(out, p.snapshotUrl, outLen);
    return;
  }
  if (p.type == PrinterType::Prusa && p.host[0]) {
    snprintf(out, outLen, "http://%s/api/v1/cameras/snap", p.host);
    return;
  }
  if (p.type == PrinterType::Klipper && p.host[0]) {
    snprintf(out, outLen, "http://%s/webcam/snapshot.jpg", p.host);
    return;
  }
}
}  // namespace

namespace CameraFetch {

bool available(const PrinterProfile& p) {
  if (p.snapshotUrl[0]) return true;
  if (p.type == PrinterType::Prusa && p.host[0]) return true;
  if (p.type == PrinterType::Klipper && p.host[0]) return true;
  if (p.type == PrinterType::Bambu && p.host[0] && p.accessCode[0]) return true;
  return false;
}

bool fetch(const PrinterProfile& p, uint8_t** outBuf, size_t* outLen, CameraImageFormat* format,
           char* status, size_t statusLen) {
  if (!outBuf || !outLen || !format || !status || !statusLen) return false;
  *outBuf = nullptr;
  *outLen = 0;
  *format = CameraImageFormat::Unknown;
  status[0] = 0;

  if (WiFi.status() != WL_CONNECTED) {
    snprintf(status, statusLen, "Wi-Fi down");
    return false;
  }

  if (p.snapshotUrl[0]) {
    return fetchHttp(p.snapshotUrl, p.apiKey, outBuf, outLen, format, status, statusLen);
  }

  if (p.type == PrinterType::Bambu) {
    return fetchBambuJpeg(p, outBuf, outLen, format, status, statusLen);
  }

  char url[160];
  buildDefaultUrl(p, url, sizeof(url));
  if (!url[0]) {
    snprintf(status, statusLen, "no camera URL");
    return false;
  }
  return fetchHttp(url, p.apiKey, outBuf, outLen, format, status, statusLen);
}

}  // namespace CameraFetch
