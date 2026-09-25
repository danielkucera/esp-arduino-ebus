#pragma once

#include <driver/uart.h>
#include <freertos/FreeRTOS.h>
#include <driver/gpio.h>

#include <cstddef>
#include <cstdint>

class UartPort {
 public:
  explicit UartPort(uart_port_t port);

  bool begin(int baud, int rxPin = -1, int txPin = -1);
  bool begin(int baud, uart_word_length_t dataBits, int rxPin, int txPin);
  void end();

  int available();
  int availableForWrite();
  int read(TickType_t timeout = 0);
  int peek();
  size_t write(uint8_t byte);
  bool synStartBit(uint32_t& start);
  bool writeArbitration(uint8_t byte, uint32_t start);

  void setRxBufferSize(size_t size);
  void setRxFIFOFull(int fullThreshold);

 private:
  static void IRAM_ATTR onFallingEdge(void* arg);

  uart_port_t port_;
  bool installed_ = false;
  size_t rx_buffer_size_ = 1024;
  int cached_byte_ = -1;
  int rx_pin_ = -1;
  bool edge_handler_installed_ = false;
  portMUX_TYPE edge_mux_ = portMUX_INITIALIZER_UNLOCKED;
  uint32_t edges_[4]{};
  size_t edge_count_ = 0;
  portMUX_TYPE tx_mux_ = portMUX_INITIALIZER_UNLOCKED;
};

extern UartPort BusSer;
