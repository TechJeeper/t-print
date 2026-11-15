#pragma once

#include "printer_types.h"

enum class CameraImageFormat : uint8_t { Unknown = 0, Jpeg, Png };

namespace CameraFetch {

// True if this profile can show a snapshot page (explicit URL or known built-in path).
bool available(const PrinterProfile& p);

// True if an MJPEG stream URL is configured.
bool streamAvailable(const PrinterProfile& p);

// Fetches one still into *outBuf / *outLen (caller must free). format set on success.
bool fetch(const PrinterProfile& p, uint8_t** outBuf, size_t* outLen, CameraImageFormat* format,
           char* status, size_t statusLen);

// Pulls one JPEG frame from the MJPEG stream URL into *outBuf / *outLen (caller must free).
bool fetchStreamFrame(const PrinterProfile& p, uint8_t** outBuf, size_t* outLen,
                      CameraImageFormat* format, char* status, size_t statusLen);

}  // namespace CameraFetch
