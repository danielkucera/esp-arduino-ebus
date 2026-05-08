#pragma once

#if defined(EBUS_INTERNAL)

#include <ebus.hpp>

static constexpr uint8_t PRIO_INTERNAL = 5;  // highest
static constexpr uint8_t PRIO_SEND = 4;      // manual send
static constexpr uint8_t PRIO_SCHEDULE = 3;  // schedule commands
static constexpr uint8_t PRIO_SCAN = 2;      // manual scan
static constexpr uint8_t PRIO_FULLSCAN = 1;  // manual full scan

ebus::EbusConfig& getEbusConfig();
ebus::Controller& getEbusController();

void configureEbus(const ebus::EbusConfig& cfg);

void startEbus();
void stopEbus();

#endif