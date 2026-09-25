#include "bus_type.hpp"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

BusType Bus;

namespace {
constexpr size_t rx_buffer_size = 512;
constexpr size_t queue_size = 480;
constexpr uint32_t serial_task_stack = 3072;
constexpr UBaseType_t serial_task_priority = configMAX_PRIORITIES - 1;

portMUX_TYPE arbitration_mux = portMUX_INITIALIZER_UNLOCKED;
int arbitration_client = -1;
uint8_t arbitration_address = 0;
}

void getArbitrationClient(int& clientFd, uint8_t& address) {
  portENTER_CRITICAL(&arbitration_mux);
  clientFd = arbitration_client;
  address = arbitration_address;
  portEXIT_CRITICAL(&arbitration_mux);
}

void clearArbitrationClient() {
  portENTER_CRITICAL(&arbitration_mux);
  arbitration_client = -1;
  arbitration_address = 0;
  portEXIT_CRITICAL(&arbitration_mux);
}

bool setArbitrationClient(int& clientFd, uint8_t& address) {
  portENTER_CRITICAL(&arbitration_mux);
  const bool available = arbitration_client < 0;
  if (available) {
    arbitration_client = clientFd;
    arbitration_address = address;
  } else {
    clientFd = arbitration_client;
    address = arbitration_address;
  }
  portEXIT_CRITICAL(&arbitration_mux);
  return available;
}

void arbitrationDone() { clearArbitrationClient(); }

int arbitrationRequested(uint8_t& address) {
  int clientFd = -1;
  getArbitrationClient(clientFd, address);
  return clientFd;
}

BusType::BusType()
    : nbr_restarts_1_(0), nbr_restarts_2_(0), nbr_arbitrations_(0),
      nbr_lost_1_(0), nbr_lost_2_(0), nbr_won_1_(0), nbr_won_2_(0),
      nbr_errors_(0), nbr_late_(0),
      client_fd_(-1) {}

BusType::~BusType() { end(); }

void BusType::readDataFromUart(void* args) {
  auto* bus = static_cast<BusType*>(args);
  for (;;) {
    // Block in the UART driver until a complete byte arrives. FIFO threshold
    // one wakes this task independently of the network loop's scheduler tick.
    const int symbol = BusSer.read(portMAX_DELAY);
    if (symbol < 0) {
      vTaskDelay(1);
      continue;
    }
    uint32_t startBitTime = 0;
    const bool timingValid = symbol == SYN && BusSer.synStartBit(startBitTime);
    bus->receive(static_cast<uint8_t>(symbol), startBitTime, timingValid);
  }
}

bool BusType::begin() {
  if (serial_event_task_ != nullptr) return true;
  queue_ = xQueueCreate(queue_size, sizeof(data));
  if (queue_ == nullptr) return false;
  BusSer.setRxBufferSize(rx_buffer_size);
  if (!BusSer.begin(2400, UART_DATA_8_BITS, UART_RX, UART_TX) ||
      xTaskCreate(readDataFromUart, "ebus_rx", serial_task_stack, this,
                   serial_task_priority, &serial_event_task_) != pdPASS) {
    end();
    return false;
  }
  return true;
}

void BusType::end() {
  if (serial_event_task_ != nullptr) {
    vTaskDelete(serial_event_task_);
    serial_event_task_ = nullptr;
  }
  BusSer.end();
  if (queue_ != nullptr) {
    vQueueDelete(queue_);
    queue_ = nullptr;
  }
}

int BusType::availableForWrite() { return BusSer.availableForWrite(); }

size_t BusType::write(uint8_t symbol) { return BusSer.write(symbol); }

bool BusType::read(data& d) {
  return queue_ != nullptr && xQueueReceive(queue_, &d, 0) == pdTRUE;
}

int BusType::available() { return BusSer.available(); }

void BusType::push(const data& d) {
  if (xQueueSendToBack(queue_, &d, 0) != pdTRUE) {
    // Never block the timing task behind network clients.
    nbr_errors_++;
    arbitrationDone();
    arbitration_ = Arbitration{};
    bus_state_.reset();
  }
}

void BusType::receive(uint8_t symbol, uint32_t startBitTime,
                      bool timingValid) {
  bus_state_.data(symbol);
  Arbitration::state state =
      arbitration_.data(bus_state_, symbol, startBitTime, timingValid);
  switch (state) {
    case Arbitration::restart1:
      nbr_restarts_1_++;
      goto NONE;
    case Arbitration::restart2:
      nbr_restarts_2_++;
      goto NONE;
    case Arbitration::none:
    NONE:
      uint8_t address;
      client_fd_ = arbitrationRequested(address);
      if (client_fd_ >= 0) {
        switch (arbitration_.start(bus_state_, address, startBitTime, timingValid)) {
          case Arbitration::started:
            nbr_arbitrations_++;
            DEBUG_LOG("BUS START SUCC 0x%02x %lu us\n", symbol,
                      bus_state_.microsSinceLastSyn());
            break;
          case Arbitration::late:
            nbr_late_++;
            [[fallthrough]];
          case Arbitration::not_started:
            DEBUG_LOG("BUS START WAIT 0x%02x %lu us\n", symbol,
                      bus_state_.microsSinceLastSyn());
        }
      }
      // send to everybody. ebusd needs the SYN to get in the right mood
      push({false, RECEIVED, symbol, -1, client_fd_});
      break;
    case Arbitration::arbitrating:
      DEBUG_LOG("BUS ARBITRATIN 0x%02x %lu us\n", symbol,
                bus_state_.microsSinceLastSyn());
      // do not send to arbitration client
      push({false, RECEIVED, symbol, client_fd_, client_fd_});
      break;
    case Arbitration::won1:
      nbr_won_1_++;
      goto WON;
    case Arbitration::won2:
      nbr_won_2_++;
    WON:
      arbitrationDone();
      DEBUG_LOG("BUS SEND WON   0x%02x %lu us\n", bus_state_.master_,
                bus_state_.microsSinceLastSyn());
      // send only to the arbitrating client
      push({true, STARTED, bus_state_.master_, client_fd_, client_fd_});
      // do not send to arbitrating client
      push({false, RECEIVED, symbol, client_fd_, client_fd_});
      client_fd_ = -1;
      break;
    case Arbitration::lost1:
      nbr_lost_1_++;
      goto LOST;
    case Arbitration::lost2:
      nbr_lost_2_++;
    LOST:
      arbitrationDone();
      DEBUG_LOG("BUS SEND LOST  0x%02x 0x%02x %lu us\n", bus_state_.master_,
                bus_state_.symbol_, bus_state_.microsSinceLastSyn());
      // send only to the arbitrating client
      push({true, FAILED, bus_state_.master_, client_fd_, client_fd_});
      // send to everybody
      push({false, RECEIVED, symbol, -1, client_fd_});
      client_fd_ = -1;
      break;
    case Arbitration::error:
      nbr_errors_++;
      arbitrationDone();
      // send only to the arbitrating client
      push({true, ERROR_EBUS, ERR_FRAMING, client_fd_, client_fd_});
      // send to everybody
      push({false, RECEIVED, symbol, -1, client_fd_});
      client_fd_ = -1;
      break;
  }
}
