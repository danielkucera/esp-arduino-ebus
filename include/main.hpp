#pragma once

#include <WiFiClient.h>
#include <WiFiServer.h>

#include <string>

#define MAX_WIFI_CLIENTS 4

#define UART_TX 20
#define UART_RX 21
#if !defined(EBUS_INTERNAL)
#define USE_SOFTWARE_SERIAL 1
#define USE_ASYNCHRONOUS 1  // requires USE_SOFTWARE_SERIAL
#endif
#define AVAILABLE_THRESHOLD 0  // https://esp32.com/viewtopic.php?t=19788

inline int DEBUG_LOG(const char* format, ...) { return 0; }
int DEBUG_LOG_IMPL(const char* format, ...);
// #define DEBUG_LOG DEBUG_LOG_IMPL

void restart();
const std::string getStatusJson();
