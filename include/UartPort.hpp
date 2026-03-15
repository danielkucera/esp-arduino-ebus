#pragma once

#include <cstddef>
#include <cstdint>

#include <driver/uart.h>

class UartPort {
 public:
  explicit UartPort(uart_port_t port);

  void begin(int baud, int rxPin = -1, int txPin = -1);
  void begin(int baud, uart_word_length_t dataBits, int rxPin, int txPin);
  void end();

  int available();
  int availableForWrite();
  int read();
  int peek();
  size_t write(uint8_t byte);

  void setRxBufferSize(size_t size);
  void setRxFIFOFull(int fullThreshold);
  void setDebugOutput(bool enable);

 private:
  void ensureInstalled(int baud, int rxPin, int txPin);

  uart_port_t port_;
  bool installed_ = false;
  size_t rxBufferSize_ = 1024;
  int cachedByte_ = -1;
};

extern UartPort BusSer;
extern UartPort DebugSer;
