#pragma once

#if defined(EBUS_INTERNAL)

#if defined(EBUS_SIMULATION)
#include <esp_timer.h>
#endif

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <ebus.hpp>

static constexpr uint8_t prio_internal = 5;  // highest
static constexpr uint8_t prio_send = 4;      // manual send
static constexpr uint8_t prio_schedule = 3;  // schedule commands
static constexpr uint8_t prio_scan = 2;      // manual scan
static constexpr uint8_t prio_fullscan = 1;  // manual full scan

ebus::EbusConfig& getEbusConfig();
ebus::Controller& getEbusController();

void configureEbus(const ebus::EbusConfig& cfg);

void startEbus();
void stopEbus();

#if defined(EBUS_SIMULATION)
esp_timer_handle_t simTimerHandle();
void startEbusSimulation();
#endif

#endif