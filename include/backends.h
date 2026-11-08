#pragma once

#include "printer_types.h"

namespace Backends {
bool pollKlipper(const PrinterProfile& p, PrinterState& out);
bool pollPrusa(const PrinterProfile& p, PrinterState& out);
bool pollBambu(const PrinterProfile& p, PrinterState& out);
bool actionKlipper(const PrinterProfile& p, const char* action, float arg = 0);
bool actionPrusa(const PrinterProfile& p, const char* action);
bool actionBambu(const PrinterProfile& p, const char* action);
String testKlipper(const PrinterProfile& p);
String testPrusa(const PrinterProfile& p);
String testBambu(const PrinterProfile& p);
}  // namespace Backends
