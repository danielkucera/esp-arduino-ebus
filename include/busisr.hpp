#pragma once

#if defined(EBUS_INTERNAL)
#include <Ebus.h>

extern ebus::Handler* ebusHandler;
extern ebus::ServiceRunner* serviceRunner;

void setupBusIsr();
void setRequestWindow(const uint16_t& delay);
#endif