#pragma once

#include <cstdint>
#include <ebus/types.hpp>
#include <string>

#include "uart_port.hpp"

#define MAX_WIFI_CLIENTS 4

#define UART_TX 20
#define UART_RX 21
#if !defined(EBUS_INTERNAL)
#define USE_SOFTWARE_SERIAL 0
#define USE_ASYNCHRONOUS 0  // requires USE_SOFTWARE_SERIAL
#endif

namespace ebus::detail {
class JsonWriter;  // Forward declaration
}

inline int DEBUG_LOG(const char* format, ...) { return 0; }
int DEBUG_LOG_IMPL(const char* format, ...);
// #define DEBUG_LOG DEBUG_LOG_IMPL

void restart();
void fetchStatus(const ebus::JsonChunkVisitor& visitor);
void fetchAppStatus(const ebus::JsonChunkVisitor& visitor);
