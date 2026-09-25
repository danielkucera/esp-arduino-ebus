#pragma once
#include <driver/uart.h>
#define UART_LL_GET_HW(port) (port)
inline int uart_ll_get_txfifo_len(int) { return fake::fifo_room; }
inline bool uart_ll_is_tx_idle(int) { return fake::tx_idle; }
inline unsigned uart_ll_get_rxfifo_len(int) { return fake::rx_fifo_size; }
inline void uart_ll_write_txfifo(int, const uint8_t* byte, int) {
  ++fake::writes;
  fake::written_at = fake::clock_us;
  fake::written_byte = *byte;
}
