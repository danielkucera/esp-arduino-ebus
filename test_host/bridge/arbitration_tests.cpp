#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <queue>
#include <vector>

#include "bridge/arbitration.hpp"
#include "bridge/bus_type.hpp"
#include "hardware/uart_port.hpp"

namespace {
int bus_available = 0;
std::vector<uint8_t> written_bytes;
std::queue<uint8_t> uart_input;

void setUpFirstSyn(BusState& bus_state) {
  bus_state.data(SYN);
  bus_state.data(SYN);
}

void feedUart(uint8_t symbol) { uart_input.push(symbol); }

BusType::data readData(BusType& bus) {
  BusType::data result{};
  REQUIRE(bus.read(result));
  return result;
}

void requireData(const BusType::data& actual, bool enhanced, uint8_t command,
                 uint8_t byte, int client_fd, int log_to_client_fd) {
  REQUIRE(actual.enhanced == enhanced);
  REQUIRE(actual.c == command);
  REQUIRE(actual.d == byte);
  REQUIRE(actual.client_fd == client_fd);
  REQUIRE(actual.log_to_client_fd == log_to_client_fd);
}
}  // namespace

UartPort::UartPort(uart_port_t port) : port_(port) {}
void UartPort::begin(int, int, int) {}
void UartPort::begin(int, uart_word_length_t, int, int) {}
void UartPort::end() {}
int UartPort::available() {
  return static_cast<int>(uart_input.size()) + bus_available;
}
int UartPort::availableForWrite() { return 1; }
int UartPort::read() {
  if (uart_input.empty()) return -1;
  const uint8_t value = uart_input.front();
  uart_input.pop();
  return value;
}
int UartPort::peek() { return uart_input.empty() ? -1 : uart_input.front(); }
size_t UartPort::write(uint8_t byte) {
  written_bytes.push_back(byte);
  return 1;
}
void UartPort::setRxBufferSize(size_t) {}
void UartPort::setRxFIFOFull(int) {}
void UartPort::setDebugOutput(bool) {}

UartPort BusSer(UART_NUM_0);
UartPort DebugSer(UART_NUM_0);

namespace {
void resetBridgeState() {
  host_esp_timer_time_us = 10000;
  bus_available = 0;
  written_bytes.clear();
  while (!uart_input.empty()) uart_input.pop();
  clearArbitrationClient();
}
}  // namespace

TEST_CASE("Bridge bus state synchronizes on two SYN bytes",
          "[bridge][bus_state]") {
  resetBridgeState();
  BusState bus_state;

  bus_state.data(0x12);
  REQUIRE(bus_state.state_ == BusState::eStartup);
  bus_state.data(SYN);
  REQUIRE(bus_state.state_ == BusState::eStartupFirstSyn);
  bus_state.data(SYN);
  REQUIRE(bus_state.state_ == BusState::eReceivedFirstSYN);
  REQUIRE(bus_state.microsSinceLastSyn() == 0);

  bus_state.data(0x15);
  REQUIRE(bus_state.state_ == BusState::eReceivedAddressAfterFirstSYN);
  REQUIRE(bus_state.master_ == 0x15);
}

TEST_CASE("Bridge arbitration rejects ineligible and late starts",
          "[bridge][arbitration]") {
  resetBridgeState();
  BusState bus_state;
  Arbitration arbitration;

  REQUIRE(arbitration.start(bus_state, 0x15, host_esp_timer_time_us) ==
          Arbitration::not_started);
  setUpFirstSyn(bus_state);
  REQUIRE(arbitration.start(bus_state, SYN, host_esp_timer_time_us) ==
          Arbitration::not_started);

  bus_available = 1;
  REQUIRE(arbitration.start(bus_state, 0x15, host_esp_timer_time_us) ==
          Arbitration::late);
  bus_available = 0;
  REQUIRE(arbitration.start(bus_state, 0x15, host_esp_timer_time_us - 4457) ==
          Arbitration::late);
  REQUIRE(written_bytes.empty());
  REQUIRE(arbitration.start(bus_state, 0x15, host_esp_timer_time_us - 4456) ==
          Arbitration::started);
  REQUIRE(written_bytes == std::vector<uint8_t>{0x15});
}

TEST_CASE("Bridge arbitration wins in the first round",
          "[bridge][arbitration]") {
  resetBridgeState();
  BusState bus_state;
  Arbitration arbitration;
  setUpFirstSyn(bus_state);

  REQUIRE(arbitration.start(bus_state, 0x15, host_esp_timer_time_us) ==
          Arbitration::started);
  REQUIRE(written_bytes == std::vector<uint8_t>{0x15});

  bus_state.data(0x15);
  REQUIRE(arbitration.data(bus_state, 0x15, host_esp_timer_time_us) ==
          Arbitration::won1);
}

TEST_CASE("Bridge arbitration loses when another priority class wins",
          "[bridge][arbitration]") {
  resetBridgeState();
  BusState bus_state;
  Arbitration arbitration;
  setUpFirstSyn(bus_state);
  REQUIRE(arbitration.start(bus_state, 0x15, host_esp_timer_time_us) ==
          Arbitration::started);

  bus_state.data(0x22);
  REQUIRE(arbitration.data(bus_state, 0x22, host_esp_timer_time_us) ==
          Arbitration::arbitrating);
  bus_state.data(0x03);
  REQUIRE(arbitration.data(bus_state, 0x03, host_esp_timer_time_us) ==
          Arbitration::lost1);
}

TEST_CASE("Bridge arbitration can win the second round",
          "[bridge][arbitration]") {
  resetBridgeState();
  BusState bus_state;
  Arbitration arbitration;
  setUpFirstSyn(bus_state);
  REQUIRE(arbitration.start(bus_state, 0x15, host_esp_timer_time_us) ==
          Arbitration::started);

  bus_state.data(0x25);
  REQUIRE(arbitration.data(bus_state, 0x25, host_esp_timer_time_us) ==
          Arbitration::arbitrating);
  bus_state.data(SYN);
  REQUIRE(arbitration.data(bus_state, SYN, host_esp_timer_time_us) ==
          Arbitration::arbitrating);
  REQUIRE(written_bytes == std::vector<uint8_t>{0x15, 0x15});

  bus_state.data(0x15);
  REQUIRE(arbitration.data(bus_state, 0x15, host_esp_timer_time_us) ==
          Arbitration::won2);
}

TEST_CASE("Bridge arbitration loses after the second round",
          "[bridge][arbitration]") {
  resetBridgeState();
  BusState bus_state;
  Arbitration arbitration;
  setUpFirstSyn(bus_state);
  REQUIRE(arbitration.start(bus_state, 0x15, host_esp_timer_time_us) ==
          Arbitration::started);

  bus_state.data(0x25);
  REQUIRE(arbitration.data(bus_state, 0x25, host_esp_timer_time_us) ==
          Arbitration::arbitrating);
  bus_state.data(SYN);
  REQUIRE(arbitration.data(bus_state, SYN, host_esp_timer_time_us) ==
          Arbitration::arbitrating);
  bus_state.data(0x25);
  REQUIRE(arbitration.data(bus_state, 0x25, host_esp_timer_time_us) ==
          Arbitration::arbitrating);
  bus_state.data(0x03);
  REQUIRE(arbitration.data(bus_state, 0x03, host_esp_timer_time_us) ==
          Arbitration::lost2);
}

TEST_CASE("Bridge BusType broadcasts unarbitrated bus bytes",
          "[bridge][bus_type]") {
  resetBridgeState();
  BusType bus;
  feedUart(0x42);

  const auto received = readData(bus);
  requireData(received, false, RECEIVED, 0x42, -1, -1);

  BusType::data empty{};
  REQUIRE_FALSE(bus.read(empty));
}

TEST_CASE("Bridge BusType routes a successful arbitration to its client",
          "[bridge][bus_type]") {
  resetBridgeState();
  BusType bus;
  int client_fd = 17;
  uint8_t address = 0x15;
  REQUIRE(setArbitrationClient(client_fd, address));

  feedUart(SYN);
  requireData(readData(bus), false, RECEIVED, SYN, -1, 17);
  feedUart(SYN);
  const auto second_syn = readData(bus);
  requireData(second_syn, false, RECEIVED, SYN, -1, 17);
  REQUIRE(bus.nbr_arbitrations_ == 1);
  REQUIRE(bus.nbr_late_ == 0);
  REQUIRE(written_bytes == std::vector<uint8_t>{0x15});
  REQUIRE(bus.nbr_arbitrations_ == 1);

  feedUart(0x15);
  requireData(readData(bus), true, STARTED, 0x15, 17, 17);
  requireData(readData(bus), false, RECEIVED, 0x15, 17, 17);
  uint8_t requested_address = 0;
  REQUIRE(arbitrationRequested(requested_address) == -1);
  REQUIRE(bus.nbr_won_1_ == 1);
}

TEST_CASE("Bridge BusType reports a first-round arbitration loss",
          "[bridge][bus_type]") {
  resetBridgeState();
  BusType bus;
  int client_fd = 21;
  uint8_t address = 0x15;
  REQUIRE(setArbitrationClient(client_fd, address));

  feedUart(SYN);
  readData(bus);
  feedUart(SYN);
  readData(bus);
  feedUart(0x22);
  requireData(readData(bus), false, RECEIVED, 0x22, 21, 21);
  feedUart(0x03);
  requireData(readData(bus), true, FAILED, 0x22, 21, 21);
  requireData(readData(bus), false, RECEIVED, 0x03, -1, 21);
  REQUIRE(bus.nbr_lost_1_ == 1);
  REQUIRE(arbitrationRequested(address) == -1);
}

TEST_CASE("Bridge BusType routes a second-round arbitration win",
          "[bridge][bus_type]") {
  resetBridgeState();
  BusType bus;
  int client_fd = 24;
  uint8_t address = 0x15;
  REQUIRE(setArbitrationClient(client_fd, address));

  feedUart(SYN);
  readData(bus);
  feedUart(SYN);
  readData(bus);
  feedUart(0x25);
  requireData(readData(bus), false, RECEIVED, 0x25, 24, 24);
  feedUart(SYN);
  requireData(readData(bus), false, RECEIVED, SYN, 24, 24);
  REQUIRE(written_bytes == std::vector<uint8_t>({0x15, 0x15}));
  feedUart(0x15);
  requireData(readData(bus), true, STARTED, 0x15, 24, 24);
  requireData(readData(bus), false, RECEIVED, 0x15, 24, 24);
  REQUIRE(bus.nbr_won_2_ == 1);
}

TEST_CASE("Bridge arbitration reservation rejects a competing client",
          "[bridge][arbitration]") {
  resetBridgeState();
  int first_client = 7;
  uint8_t first_address = 0x15;
  REQUIRE(setArbitrationClient(first_client, first_address));

  int second_client = 9;
  uint8_t second_address = 0x27;
  REQUIRE_FALSE(setArbitrationClient(second_client, second_address));
  REQUIRE(second_client == first_client);
  REQUIRE(second_address == first_address);
}
