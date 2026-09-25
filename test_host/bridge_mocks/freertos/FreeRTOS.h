#pragma once
#include <cstdint>
using TickType_t = uint32_t;
using portMUX_TYPE = int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) (void)(lock)
#define portEXIT_CRITICAL(lock) (void)(lock)
#define portENTER_CRITICAL_ISR(lock) (void)(lock)
#define portEXIT_CRITICAL_ISR(lock) (void)(lock)
