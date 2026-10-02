#pragma once

#include <driver/uart.h>

#include <cstddef>
#include <cstdint>

class UartPort {
 public:
  explicit UartPort(uart_port_t port);

  void begin(int baud, int rxPin = -1, int txPin = -1);
  void begin(int baud, uart_word_length_t dataBits, int rxPin, int txPin);
  void end();

  int available();
  static int availableForWrite();
  int read();
  int peek();
  size_t write(uint8_t byte);

  void setRxBufferSize(size_t size);
  void setRxFIFOFull(int fullThreshold);
  static void setDebugOutput(bool enable);

 private:
  void ensureInstalled(int baud, int rxPin, int txPin);

  uart_port_t port_;
  bool installed_ = false;
  size_t rx_buffer_size_ = 1024;
  int cached_byte_ = -1;
};

extern UartPort BusSer;
extern UartPort DebugSer;
