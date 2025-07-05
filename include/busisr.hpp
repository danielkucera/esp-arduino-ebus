#pragma once

#if defined(EBUS_INTERNAL)
#include <Ebus.h>

extern ebus::Handler* ebusHandler;
extern ebus::ServiceRunner* serviceRunner;

void setupBusIsr(HardwareSerial* serial, const int8_t& rxPin,
                 const int8_t& txPin);
void setRequestOffset(const uint16_t& offset);
#endif
