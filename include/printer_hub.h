#pragma once

#include "printer_types.h"
#include "config_store.h"

namespace PrinterHub {
void begin();
void loop();
void reloadProfiles();
size_t count();
const PrinterProfile* profileAt(size_t i);
const PrinterState* stateAt(size_t i);
PrinterActions actionsAt(size_t i);
bool pause(size_t i);
bool resume(size_t i);
bool cancel(size_t i);
bool estop(size_t i);
bool setNozzleTemp(size_t i, float c);
bool setBedTemp(size_t i, float c);
String testConnection(const PrinterProfile& p);
}  // namespace PrinterHub
