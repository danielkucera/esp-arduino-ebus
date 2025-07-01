#pragma once

#if defined(EBUS_INTERNAL) && !defined(USE_IDF_UART)
#include <Ebus.h>

extern ebus::Bus* bus;
extern ebus::Handler* ebusHandler;
extern ebus::ServiceRunner* serviceRunner;

void setupBusIsr(const int rx_pin, const int tx_pin);
void setRequestWindow(const uint16_t& delay);
#endif
