#pragma once
#include <cstdint>
#include <cstddef>
#define IRAM_ATTR
using uart_port_t = int;
using uart_word_length_t = int;
inline constexpr int UART_NUM_1 = 1;
using esp_err_t = int;
inline constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_INVALID_STATE = 1;
inline constexpr int UART_DATA_8_BITS = 8, UART_PARITY_DISABLE = 0,
    UART_STOP_BITS_1 = 1, UART_HW_FLOWCTRL_DISABLE = 0,
    UART_SCLK_DEFAULT = 0, UART_PIN_NO_CHANGE = -1;
struct uart_config_t {
  int baud_rate, data_bits, parity, stop_bits, flow_ctrl, source_clk;
};
namespace fake {
inline uint32_t clock_us = 0;
inline size_t pending_rx = 0;
inline uint8_t rx_byte = 0xaa;
inline int fifo_room = 128;
inline bool tx_idle = true;
inline unsigned rx_fifo_size = 0;
inline unsigned writes = 0;
inline uint32_t written_at = 0;
inline uint8_t written_byte = 0;
inline bool driver_ok = true;
inline bool installed = false;
inline int tx_buffer_size = -1;
}
inline int uart_driver_install(int, int, int tx, int, void*, int) {
  fake::tx_buffer_size = tx;
  fake::installed = fake::driver_ok;
  return fake::driver_ok ? ESP_OK : ESP_FAIL;
}
inline int uart_driver_delete(int) { fake::installed = false; return ESP_OK; }
inline int uart_param_config(int, const uart_config_t*) { return ESP_OK; }
inline int uart_set_pin(int, int, int, int, int) { return ESP_OK; }
inline int uart_set_rx_full_threshold(int, int) { return ESP_OK; }
inline int uart_set_rx_timeout(int, int) { return ESP_OK; }
inline int uart_get_buffered_data_len(int, size_t* size) {
  *size = fake::pending_rx; return ESP_OK;
}
inline int uart_read_bytes(int, uint8_t* byte, int, uint32_t) {
  if (fake::pending_rx == 0) return 0;
  --fake::pending_rx;
  *byte = fake::rx_byte;
  return 1;
}
