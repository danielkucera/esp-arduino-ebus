#pragma once

#include <WiFiClient.h>
#include <WiFiServer.h>

#define MAX_SRV_CLIENTS 4

// never used - how does this work?
#define STACK_PROTECTOR 512  // bytes

#ifdef ESP32
#define UART_TX 20
#define UART_RX 21
#define USE_SOFTWARE_SERIAL 1
#define USE_ASYNCHRONOUS 1     // requires USE_SOFTWARE_SERIAL
#define AVAILABLE_THRESHOLD 0  // https://esp32.com/viewtopic.php?t=19788
#else
#define UART_TX 1
#define UART_RX 3
#define USE_SOFTWARE_SERIAL 0
#define USE_ASYNCHRONOUS 0  // requires USE_SOFTWARE_SERIAL
#define AVAILABLE_THRESHOLD 1
#endif

inline int DEBUG_LOG(const char *format, ...) { return 0; }
int DEBUG_LOG_IMPL(const char *format, ...);
// #define DEBUG_LOG DEBUG_LOG_IMPL

bool handleNewClient(WiFiServer *server, WiFiClient clients[]);
int pushClient(WiFiClient *client, uint8_t B);
void handleClient(WiFiClient *client);

void reset();
