#include "UartPort.hpp"

#include <esp_log.h>

namespace {
constexpr const char* kTag = "UartPort";
}

UartPort BusSer(UART_NUM_1);
UartPort DebugSer(UART_NUM_0);

UartPort::UartPort(uart_port_t port) : port_(port) {}

void UartPort::begin(int baud, int rxPin, int txPin) {
  begin(baud, UART_DATA_8_BITS, rxPin, txPin);
}

void UartPort::begin(int baud, uart_word_length_t dataBits, int rxPin,
                     int txPin) {
  ensureInstalled(baud, rxPin, txPin);

  uart_config_t config{};
  config.baud_rate = baud;
  config.data_bits = dataBits;
  config.parity = UART_PARITY_DISABLE;
  config.stop_bits = UART_STOP_BITS_1;
  config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  config.source_clk = UART_SCLK_DEFAULT;
  uart_param_config(port_, &config);
  if (rxPin >= 0 || txPin >= 0) {
    uart_set_pin(port_, txPin, rxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  }
}

void UartPort::ensureInstalled(int baud, int rxPin, int txPin) {
  if (installed_) return;
  (void)baud;
  (void)rxPin;
  (void)txPin;
  int rxBuffer = static_cast<int>(rxBufferSize_);
  int txBuffer = 0;
  if (uart_driver_install(port_, rxBuffer, txBuffer, 0, nullptr, 0) != ESP_OK) {
    ESP_LOGE(kTag, "uart_driver_install failed for port %d", port_);
  } else {
    installed_ = true;
  }
}

void UartPort::end() {
  if (!installed_) return;
  uart_driver_delete(port_);
  installed_ = false;
}

int UartPort::available() {
  if (cachedByte_ >= 0) return 1;
  size_t length = 0;
  uart_get_buffered_data_len(port_, &length);
  return static_cast<int>(length);
}

int UartPort::availableForWrite() {
  return 1;
}

int UartPort::read() {
  if (cachedByte_ >= 0) {
    int value = cachedByte_;
    cachedByte_ = -1;
    return value;
  }
  uint8_t byte = 0;
  int read = uart_read_bytes(port_, &byte, 1, 0);
  if (read <= 0) return -1;
  return byte;
}

int UartPort::peek() {
  if (cachedByte_ >= 0) return cachedByte_;
  uint8_t byte = 0;
  int read = uart_read_bytes(port_, &byte, 1, 0);
  if (read <= 0) return -1;
  cachedByte_ = byte;
  return cachedByte_;
}

size_t UartPort::write(uint8_t byte) {
  int written = uart_write_bytes(port_, reinterpret_cast<const char*>(&byte), 1);
  return written > 0 ? static_cast<size_t>(written) : 0;
}

void UartPort::setRxBufferSize(size_t size) { rxBufferSize_ = size; }

void UartPort::setRxFIFOFull(int fullThreshold) {
  uart_set_rx_full_threshold(port_, fullThreshold);
}

void UartPort::setDebugOutput(bool enable) { (void)enable; }
