#pragma once

#include <cstdint>

inline uint64_t host_esp_timer_time_us = 0;

inline uint64_t esp_timer_get_time() { return host_esp_timer_time_us; }
