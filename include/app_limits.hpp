#pragma once

#include <cstddef>
#include <cstdint>

namespace app::limits {

// --- Task Orchestration ---
namespace Task {
inline constexpr uint32_t client_accept_stack = 4096;
inline constexpr uint8_t client_accept_priority = 1;
inline constexpr uint32_t data_loop_stack = 10000;
inline constexpr uint8_t data_loop_priority = 1;
inline constexpr uint32_t dns_stack = 2048;
inline constexpr uint8_t dns_priority = 1;
inline constexpr uint32_t espota_stack = 8192;
inline constexpr uint8_t espota_priority = 1;
inline constexpr uint32_t logger_stack = 3072;
inline constexpr uint8_t logger_priority = 1;
inline constexpr uint32_t status_led_stack = 1024;
inline constexpr uint8_t status_led_priority = 1;

#if defined(EBUS_INTERNAL)
inline constexpr uint32_t cron_stack = 1536;
inline constexpr uint8_t cron_priority = 2;
inline constexpr uint32_t mqtt_stack = 7168;
inline constexpr uint8_t mqtt_priority = 3;
inline constexpr uint32_t system_monitor_stack = 4096;
inline constexpr uint8_t system_monitor_priority = 4;
#endif
}  // namespace Task

}  // namespace app::limits
