#pragma once

#include <driver/uart.h>

#include <cstddef>
#include <cstdint>

class UartPort {
 public:
  explicit UartPort(uart_port_t port);

  void begin(int baud, int rx_pin = -1, int tx_pin = -1);
  void begin(int baud, uart_word_length_t data_bits, int rx_pin, int tx_pin);
  void end();

  int available();
  static int availableForWrite();
  int read();
  int peek();
  size_t write(uint8_t byte);

  void setRxBufferSize(size_t size);
  void setRxFIFOFull(int full_threshold);
  static void setDebugOutput(bool enable);

 private:
  void ensureInstalled(int baud, int rx_pin, int tx_pin);

  uart_port_t port_;
  bool installed_ = false;
  size_t rx_buffer_size_ = 1024;
  int cached_byte_ = -1;
};

extern UartPort bus_ser;
extern UartPort debug_ser;
