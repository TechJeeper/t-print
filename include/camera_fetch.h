#pragma once

#include "printer_types.h"

enum class CameraImageFormat : uint8_t { Unknown = 0, Jpeg, Png };

namespace CameraFetch {

// True if this profile can show a camera page (explicit URL or known built-in path).
bool available(const PrinterProfile& p);

// Fetches one still into *outBuf / *outLen (caller must free). format set on success.
bool fetch(const PrinterProfile& p, uint8_t** outBuf, size_t* outLen, CameraImageFormat* format,
           char* status, size_t statusLen);

}  // namespace CameraFetch
