#include "bus_type.hpp"
#include "bridge_timing.hpp"

#include <cassert>
#include <cstdio>

namespace {
uint32_t clock_us = 0;
unsigned writes = 0;
uint8_t last_address = 0;
}

int64_t esp_timer_get_time() { return clock_us; }
UartPort BusSer(UART_NUM_1);
UartPort::UartPort(uart_port_t port) : port_(port) {}
bool UartPort::writeArbitration(uint8_t address, uint32_t start) {
  if (!bridge::inArbitrationWindow(start, clock_us)) return false;
  ++writes;
  last_address = address;
  return true;
}
BusType Bus;
BusType::BusType() : nbr_late_(0), client_fd_(-1) {}
BusType::~BusType() = default;

BusState ready() {
  BusState bus;
  bus.data(SYN);
  bus.data(SYN);
  assert(bus.state_ == BusState::eReceivedFirstSYN);
  return bus;
}

int main() {
  for (uint32_t base : {10000U, UINT32_MAX - 2000U}) {
    auto bus = ready();
    Arbitration arbitration;
    clock_us = base + 4300;
    const unsigned before = writes;
    assert(arbitration.start(bus, 0x31, base, false) == Arbitration::late);
    assert(writes == before);
    clock_us = base + 4500;
    assert(arbitration.start(bus, 0x31, base, true) == Arbitration::late);
    assert(writes == before);
    clock_us = base + 4400;
    assert(arbitration.start(bus, SYN, base, true) == Arbitration::not_started);
    assert(arbitration.start(bus, 0x31, base, true) == Arbitration::started);
    assert(writes == before + 1 && last_address == 0x31);
    bus.data(0x31);
    assert(arbitration.data(bus, 0x31, 0, false) == Arbitration::won1);
  }

  // Competing master with the same priority forces round two. It must use
  // the same valid-edge/deadline rules as round one.
  for (bool valid : {false, true}) {
    auto bus = ready();
    Arbitration arbitration;
    clock_us = 14300;
    assert(arbitration.start(bus, 0x31, 10000, true) == Arbitration::started);
    bus.data(0x71);
    assert(arbitration.data(bus, 0x71, 0, false) == Arbitration::arbitrating);
    bus.data(SYN);
    clock_us = 24500;  // missed the second SYN deadline
    const unsigned before = writes;
    assert(arbitration.data(bus, SYN, 20000, valid) == Arbitration::arbitrating);
    assert(writes == before);
    bus.data(0x71);
    arbitration.data(bus, 0x71, 0, false);
    bus.data(0x10);
    assert(arbitration.data(bus, 0x10, 0, false) == Arbitration::lost2);
  }
  assert(Bus.nbr_late_ == 2);
  puts("Bridge arbitration regression tests passed");
}
