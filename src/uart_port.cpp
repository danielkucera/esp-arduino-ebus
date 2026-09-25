#include "uart_port.hpp"

#include <esp_intr_alloc.h>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>
#include <hal/uart_ll.h>

#include "bridge_timing.hpp"

namespace {
constexpr const char* tag = "UartPort";
}

UartPort BusSer(UART_NUM_1);

UartPort::UartPort(uart_port_t port) : port_(port) {}

bool UartPort::begin(int baud, int rxPin, int txPin) {
  return begin(baud, UART_DATA_8_BITS, rxPin, txPin);
}

bool UartPort::begin(int baud, uart_word_length_t dataBits, int rxPin,
                     int txPin) {
  if (installed_) return true;
  // TX is owned by this wrapper and written directly to the FIFO. A driver
  // TX ring buffer would insert an unbounded delay into bus arbitration.
  if (uart_driver_install(port_, static_cast<int>(rx_buffer_size_), 0,
                          0, nullptr, 0) != ESP_OK) {
    ESP_LOGE(tag, "UART driver installation failed");
    return false;
  }
  installed_ = true;
  uart_config_t config{};
  config.baud_rate = baud;
  config.data_bits = dataBits;
  config.parity = UART_PARITY_DISABLE;
  config.stop_bits = UART_STOP_BITS_1;
  config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  config.source_clk = UART_SCLK_DEFAULT;
  if (uart_param_config(port_, &config) != ESP_OK ||
      uart_set_pin(port_, txPin, rxPin, UART_PIN_NO_CHANGE,
                   UART_PIN_NO_CHANGE) != ESP_OK ||
      uart_set_rx_full_threshold(port_, 1) != ESP_OK ||
      uart_set_rx_timeout(port_, 1) != ESP_OK) {
    end();
    return false;
  }
  rx_pin_ = rxPin;
  edge_count_ = 0;
  const auto pin = static_cast<gpio_num_t>(rxPin);
  const esp_err_t isrResult = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
  if ((isrResult != ESP_OK && isrResult != ESP_ERR_INVALID_STATE) ||
      gpio_set_intr_type(pin, GPIO_INTR_NEGEDGE) != ESP_OK ||
      gpio_isr_handler_add(pin, onFallingEdge, this) != ESP_OK) {
    end();
    return false;
  }
  edge_handler_installed_ = true;
  return true;
}

void IRAM_ATTR UartPort::onFallingEdge(void* arg) {
  auto* self = static_cast<UartPort*>(arg);
  const uint32_t now = static_cast<uint32_t>(esp_timer_get_time());
  portENTER_CRITICAL_ISR(&self->edge_mux_);
  self->edges_[0] = self->edges_[1];
  self->edges_[1] = self->edges_[2];
  self->edges_[2] = self->edges_[3];
  self->edges_[3] = now;
  if (self->edge_count_ < 4) ++self->edge_count_;
  portEXIT_CRITICAL_ISR(&self->edge_mux_);
}

void UartPort::end() {
  if (edge_handler_installed_) {
    gpio_isr_handler_remove(static_cast<gpio_num_t>(rx_pin_));
    edge_handler_installed_ = false;
  }
  if (installed_) uart_driver_delete(port_);
  installed_ = false;
  cached_byte_ = -1;
}

int UartPort::available() {
  if (!installed_) return 0;
  size_t length = 0;
  uart_get_buffered_data_len(port_, &length);
  return static_cast<int>(length) + (cached_byte_ >= 0 ? 1 : 0);
}

int UartPort::availableForWrite() {
  return installed_ ? uart_ll_get_txfifo_len(UART_LL_GET_HW(port_)) : 0;
}

int UartPort::read(TickType_t timeout) {
  if (!installed_) return -1;
  if (cached_byte_ >= 0) {
    int value = cached_byte_;
    cached_byte_ = -1;
    return value;
  }
  uint8_t byte = 0;
  return uart_read_bytes(port_, &byte, 1, timeout) == 1 ? byte : -1;
}

int UartPort::peek() {
  if (cached_byte_ < 0) cached_byte_ = read();
  return cached_byte_;
}

size_t UartPort::write(uint8_t byte) {
  if (!installed_) return 0;
  portENTER_CRITICAL(&tx_mux_);
  const bool room = uart_ll_get_txfifo_len(UART_LL_GET_HW(port_)) > 0;
  if (room) uart_ll_write_txfifo(UART_LL_GET_HW(port_), &byte, 1);
  portEXIT_CRITICAL(&tx_mux_);
  return room ? 1 : 0;
}

bool UartPort::synStartBit(uint32_t& start) {
  if (available() != 0) return false;  // Do not arbitrate on buffered traffic.
  portENTER_CRITICAL(&edge_mux_);
  const bool valid = bridge::synStartBit(
      edges_, edge_count_, static_cast<uint32_t>(esp_timer_get_time()), start);
  portEXIT_CRITICAL(&edge_mux_);
  return valid;
}

bool UartPort::writeArbitration(uint8_t byte, uint32_t start) {
  if (!installed_ || available() != 0) return false;
  const uint32_t now = static_cast<uint32_t>(esp_timer_get_time());
  const uint32_t age = now - start;
  if (age < bridge::stop_sample_us - bridge::edge_tolerance_us ||
      age > bridge::arbitration_latest_us) return false;
  const uint32_t wait = bridge::waitForArbitration(start, now);
  if (wait > 0) esp_rom_delay_us(wait);

  // Recheck after any task preemption. Check and FIFO write share a critical
  // section so a context switch cannot move a valid attempt past its deadline.
  portENTER_CRITICAL(&edge_mux_);
  portENTER_CRITICAL(&tx_mux_);
  const bool valid = bridge::canCommitArbitration(
                         start, static_cast<uint32_t>(esp_timer_get_time())) &&
                     edge_count_ == 4 && edges_[0] == start &&
                     gpio_get_level(static_cast<gpio_num_t>(rx_pin_)) != 0 &&
                     uart_ll_is_tx_idle(UART_LL_GET_HW(port_)) &&
                     uart_ll_get_rxfifo_len(UART_LL_GET_HW(port_)) == 0;
  if (valid) uart_ll_write_txfifo(UART_LL_GET_HW(port_), &byte, 1);
  portEXIT_CRITICAL(&tx_mux_);
  portEXIT_CRITICAL(&edge_mux_);
  return valid;
}

void UartPort::setRxBufferSize(size_t size) { rx_buffer_size_ = size; }

void UartPort::setRxFIFOFull(int fullThreshold) {
  uart_set_rx_full_threshold(port_, fullThreshold);
}
