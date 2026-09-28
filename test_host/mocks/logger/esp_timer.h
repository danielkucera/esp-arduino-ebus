#pragma once

#include <cstdint>

namespace LoggerTestState {
inline uint64_t now_us = 0;
}

inline uint64_t esp_timer_get_time() { return LoggerTestState::now_us; }
