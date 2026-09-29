#pragma once

#if defined(EBUS_INTERNAL)

#if EBUS_SIMULATION
#include <esp_timer.h>
#endif

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <ebus.hpp>

namespace app::priority {
inline constexpr uint8_t internal = 5;  // highest
inline constexpr uint8_t send = 4;      // manual send
inline constexpr uint8_t schedule = 3;  // schedule commands
inline constexpr uint8_t scan = 2;      // manual scan
inline constexpr uint8_t fullscan = 1;  // manual full scan
}  // namespace app::priority

ebus::EbusConfig& getEbusConfig();
ebus::Controller& getEbusController();

void configureEbus(const ebus::EbusConfig& cfg);

void startEbus();
void stopEbus();

#if EBUS_SIMULATION
esp_timer_handle_t simTimerHandle();
void startEbusSimulation();
#endif

#endif