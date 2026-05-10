#pragma once

#include "UartPort.hpp"

#include <cstdint>
#include <string>

#define MAX_WIFI_CLIENTS 4

/* 
 * Hardware-fixed pins on your platine. 
 * Note: These conflict with the default UART0 console and RESET_PIN flag.
 */
#define UART_TX 20
#define UART_RX 21
#if !defined(EBUS_INTERNAL)
#define USE_SOFTWARE_SERIAL 0
#define USE_ASYNCHRONOUS 0  // requires USE_SOFTWARE_SERIAL
#endif

inline int DEBUG_LOG(const char* format, ...) { return 0; }
int DEBUG_LOG_IMPL(const char* format, ...);
// #define DEBUG_LOG DEBUG_LOG_IMPL

void restart();
const std::string getStatusJson();
char* getAppResourcesJson();
