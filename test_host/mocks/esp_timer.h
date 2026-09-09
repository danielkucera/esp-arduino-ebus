#pragma once

// Minimal esp_timer stub for host testing

#ifdef __cplusplus
extern "C" {
#endif

#include <cstdint>

static inline uint64_t esp_timer_get_time(void) {
    return 123456789ULL;
}

static inline void esp_restart(void) {}

#ifdef __cplusplus
}
#endif
