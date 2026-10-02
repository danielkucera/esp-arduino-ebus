#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void startCaptiveDns();

// Task handle of the captive DNS server task (null if not started).
// Needed for diagnostics (system status threads section).
TaskHandle_t getCaptiveDnsTaskHandle();
