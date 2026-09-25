#include "uart_port.hpp"

#include <cassert>
#include <cstdio>
#include <initializer_list>

namespace {
uint32_t extra_delay = 0;
bool edge_during_wait = false;
}

int64_t esp_timer_get_time() { return fake::clock_us; }
void esp_rom_delay_us(uint32_t us) {
  fake::clock_us += us + extra_delay;
  if (edge_during_wait) fake::edge_handler(fake::edge_arg);
}

void captureSyn(uint32_t base) {
  for (uint32_t offset : {0U, 1250U, 2083U, 2917U}) {
    fake::clock_us = base + offset;
    fake::edge_handler(fake::edge_arg);
  }
  fake::clock_us = base + 3958;
}

int main() {
  fake::driver_ok = false;
  assert(!BusSer.begin(2400, 21, 20));
  assert(BusSer.availableForWrite() == 0);
  fake::driver_ok = true;
  assert(BusSer.begin(2400, 21, 20));
  assert(fake::tx_buffer_size == 0);
  for (uint32_t base : {10000U, UINT32_MAX - 2000U}) {
    captureSyn(base);
    uint32_t start = 0;
    assert(BusSer.synStartBit(start) && start == base);
    const auto before = fake::writes;
    assert(BusSer.writeArbitration(0x31, start));
    assert(fake::writes == before + 1);
    assert(fake::written_at - start == 4300);
    assert(fake::written_byte == 0x31);

    captureSyn(base);
    extra_delay = 1000;  // task preempted between deciding and sending
    assert(!BusSer.writeArbitration(0x31, base));
    assert(fake::writes == before + 1);
    extra_delay = 0;

    captureSyn(base);
    fake::clock_us = base + 4407;  // reserve hardware-start margin
    assert(!BusSer.writeArbitration(0x31, base));
    assert(fake::writes == before + 1);

    captureSyn(base);
    edge_during_wait = true;
    assert(!BusSer.writeArbitration(0x31, base));
    edge_during_wait = false;
    assert(fake::writes == before + 1);

    captureSyn(base);
    fake::pending_rx = 1;
    assert(!BusSer.synStartBit(start));
    assert(!BusSer.writeArbitration(0x31, base));
    fake::pending_rx = 0;

    captureSyn(base);
    fake::tx_idle = false;
    assert(!BusSer.writeArbitration(0x31, base));
    fake::tx_idle = true;
    captureSyn(base);
    fake::rx_level = 0;
    assert(!BusSer.writeArbitration(0x31, base));
    fake::rx_level = 1;
    captureSyn(base);
    fake::rx_fifo_size = 1;
    assert(!BusSer.writeArbitration(0x31, base));
    fake::rx_fifo_size = 0;
    assert(fake::writes == before + 1);
  }
  // Peeking must not hide additional pending RX bytes.
  fake::pending_rx = 2;
  assert(BusSer.peek() == 0xaa);
  assert(BusSer.available() == 2);
  assert(BusSer.read() == 0xaa);
  assert(BusSer.available() == 1);
  fake::pending_rx = 0;
  fake::fifo_room = 0;
  assert(BusSer.availableForWrite() == 0 && BusSer.write(0x31) == 0);
  BusSer.end();
  assert(!fake::installed && fake::edge_handler == nullptr);
  puts("Bridge UART deadline and FIFO tests passed");
}
