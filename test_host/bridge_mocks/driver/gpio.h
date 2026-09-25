#pragma once
#include "uart.h"
using gpio_num_t = int;
inline constexpr int GPIO_INTR_NEGEDGE = 1;
namespace fake {
inline void (*edge_handler)(void*) = nullptr;
inline void* edge_arg = nullptr;
inline int rx_level = 1;
}
inline int gpio_install_isr_service(int) { return ESP_OK; }
inline int gpio_set_intr_type(int, int) { return ESP_OK; }
inline int gpio_isr_handler_add(int, void (*handler)(void*), void* arg) {
  fake::edge_handler = handler; fake::edge_arg = arg; return ESP_OK;
}
inline int gpio_isr_handler_remove(int) {
  fake::edge_handler = nullptr; return ESP_OK;
}
inline int gpio_get_level(int) { return fake::rx_level; }
