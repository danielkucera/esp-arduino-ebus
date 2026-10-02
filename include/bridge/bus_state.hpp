#pragma once

#if !defined(EBUS_INTERNAL)

#include <esp_timer.h>

#include "main.hpp"

enum Symbols { syn_byte = 0xAA };

// Implements the state of the bus. The arbitration process can
// only start at well defined states of the bus. To asses the
// state, all data received on the bus needs to be send to this
// object. The object takes care of startup of the bus and
// recovery when an unexpected event happens.
class BusState {
 public:
  enum State {
    startup,            // In startup mode to analyze bus state
    startup_first_syn,  // Either the bus is busy, it is arbitrating, or it is
                        // free to start an arbitration
    startup_symbol_after_first_syn,
    startup_second_syn,
    received_first_syn,                 // Received SYN
    received_address_after_first_syn,   // Received SYN ADDRESS
    received_second_syn,                // Received SYN ADDRESS SYN
    received_address_after_second_syn,  // Received SYN ADDRESS SYN ADDRESS
    busy  // Bus is busy; master_ is master that won, _byte is first symbol
          // after the master address
  };
  static const char* enumValue(State e) {
    const char* values[] = {"startup",
                            "startup_first_syn",
                            "startup_symbol_after_first_syn",
                            "startup_second_syn",
                            "received_first_syn",
                            "received_address_after_first_syn",
                            "received_second_syn",
                            "received_address_after_second_syn",
                            "busy"};
    return values[e];
  }
  BusState() : state(startup), previous_state(startup) {}
  // Evaluate a symbol received on UART and determine what the new state of the
  // bus is
  inline void data(uint8_t symbol) {
    switch (state) {
      case startup:
        previous_state = state;
        state = symbol == syn_byte ? syn(startup_first_syn) : startup;
        break;
      case startup_first_syn:
        previous_state = state;
        state = symbol == syn_byte ? syn(received_first_syn)
                                   : startup_symbol_after_first_syn;
        break;
      case startup_symbol_after_first_syn:
        previous_state = state;
        state = symbol == syn_byte ? syn(startup_second_syn) : busy;
        break;
      case startup_second_syn:
        previous_state = state;
        state = symbol == syn_byte ? syn(received_first_syn) : busy;
        break;
      case received_first_syn:
        previous_state = state;
        state = symbol == syn_byte ? syn(received_first_syn)
                                   : received_address_after_first_syn;
        master = symbol;
        break;
      case received_address_after_first_syn:
        previous_state = state;
        state = symbol == syn_byte ? syn(received_second_syn) : busy;
        symbol = symbol;
        break;
      case received_second_syn:
        previous_state = state;
        state = symbol == syn_byte ? error(state, received_first_syn)
                                   : received_address_after_second_syn;
        master = symbol;
        break;
      case received_address_after_second_syn:
        previous_state = state;
        state = symbol == syn_byte ? error(state, received_first_syn) : busy;
        symbol = symbol;
        break;
      case busy:
        previous_state = state;
        state = symbol == syn_byte ? syn(received_first_syn) : busy;
        break;
    }
  }
  inline State syn(State newstate) {
    previous_syn_time = syn_time;
    syn_time = (uint32_t)(esp_timer_get_time());
    return newstate;
  }
  State error(State currentstate, State newstate) {
    previous_syn_time = syn_time;
    syn_time = (uint32_t)(esp_timer_get_time());
    DEBUG_LOG(
        "unexpected SYN on bus while state is %s, setting state to %s "
        "m=0x%02x, b=0x%02x %lu us\n",
        enumValue(currentstate), enumValue(newstate), master, symbol,
        microsSincePreviousSyn());
    return newstate;
  }

  void reset() { state = startup; }

  uint32_t microsSinceLastSyn() const {
    return (uint32_t)(esp_timer_get_time()) - syn_time;
  }

  uint32_t microsSincePreviousSyn() const {
    return (uint32_t)(esp_timer_get_time()) - previous_syn_time;
  }

  State state;
  State previous_state;
  uint8_t master = 0;
  uint8_t symbol = 0;
  uint32_t syn_time = 0;
  uint32_t previous_syn_time = 0;
};

#endif
