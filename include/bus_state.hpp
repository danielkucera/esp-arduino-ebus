#pragma once

#include <esp_timer.h>

#include "main.hpp"

enum symbols { SYN = 0xAA };

// Implements the state of the bus. The arbitration process can
// only start at well defined states of the bus. To asses the
// state, all data received on the bus needs to be send to this
// object. The object takes care of startup of the bus and
// recovery when an unexpected event happens.
class BusState {
 public:
  enum eState {
    eStartup,          // In startup mode to analyze bus state
    eStartupFirstSyn,  // Either the bus is busy, it is arbitrating, or it is
                       // free to start an arbitration
    eStartupSymbolAfterFirstSyn,
    eStartupSecondSyn,
    eReceivedFirstSYN,               // Received SYN
    eReceivedAddressAfterFirstSYN,   // Received SYN ADDRESS
    eReceivedSecondSYN,              // Received SYN ADDRESS SYN
    eReceivedAddressAfterSecondSYN,  // Received SYN ADDRESS SYN ADDRESS
    eBusy  // Bus is busy; master_ is master that won, _byte is first symbol
           // after the master address
  };
  static const char* enumvalue(eState e) {
    const char* values[] = {"eStartup",
                            "eStartupFirstSyn",
                            "eStartupSymbolAfterFirstSyn",
                            "eStartupSecondSyn",
                            "eReceivedFirstSYN",
                            "eReceivedAddressAfterFirstSYN",
                            "eReceivedSecondSYN",
                            "eReceivedAddressAfterSecondSYN",
                            "eBusy"};
    return values[e];
  }
  BusState() : state_(eStartup), previous_state_(eStartup) {}
  // Evaluate a symbol received on UART and determine what the new state of the
  // bus is
  inline void data(uint8_t symbol) {
    switch (state_) {
      case eStartup:
        previous_state_ = state_;
        state_ = symbol == SYN ? syn(eStartupFirstSyn) : eStartup;
        break;
      case eStartupFirstSyn:
        previous_state_ = state_;
        state_ = symbol == SYN ? syn(eReceivedFirstSYN)
                               : eStartupSymbolAfterFirstSyn;
        break;
      case eStartupSymbolAfterFirstSyn:
        previous_state_ = state_;
        state_ = symbol == SYN ? syn(eStartupSecondSyn) : eBusy;
        break;
      case eStartupSecondSyn:
        previous_state_ = state_;
        state_ = symbol == SYN ? syn(eReceivedFirstSYN) : eBusy;
        break;
      case eReceivedFirstSYN:
        previous_state_ = state_;
        state_ = symbol == SYN ? syn(eReceivedFirstSYN)
                               : eReceivedAddressAfterFirstSYN;
        master_ = symbol;
        break;
      case eReceivedAddressAfterFirstSYN:
        previous_state_ = state_;
        state_ = symbol == SYN ? syn(eReceivedSecondSYN) : eBusy;
        symbol_ = symbol;
        break;
      case eReceivedSecondSYN:
        previous_state_ = state_;
        state_ = symbol == SYN ? error(state_, eReceivedFirstSYN)
                               : eReceivedAddressAfterSecondSYN;
        master_ = symbol;
        break;
      case eReceivedAddressAfterSecondSYN:
        previous_state_ = state_;
        state_ = symbol == SYN ? error(state_, eReceivedFirstSYN) : eBusy;
        symbol_ = symbol;
        break;
      case eBusy:
        previous_state_ = state_;
        state_ = symbol == SYN ? syn(eReceivedFirstSYN) : eBusy;
        break;
    }
  }
  inline eState syn(eState newstate) {
    previous_syn_time_ = syn_time_;
    syn_time_ = (uint32_t)(esp_timer_get_time());
    return newstate;
  }
  eState error(eState currentstate, eState newstate) {
    previous_syn_time_ = syn_time_;
    syn_time_ = (uint32_t)(esp_timer_get_time());
    DEBUG_LOG(
        "unexpected SYN on bus while state is %s, setting state to %s "
        "m=0x%02x, b=0x%02x %lu us\n",
        enumvalue(currentstate), enumvalue(newstate), master_, symbol_,
        microsSincePreviousSyn());
    return newstate;
  }

  void reset() { state_ = eStartup; }

  uint32_t microsSinceLastSyn() const {
    return (uint32_t)(esp_timer_get_time()) - syn_time_;
  }

  uint32_t microsSincePreviousSyn() const {
    return (uint32_t)(esp_timer_get_time()) - previous_syn_time_;
  }

  eState state_;
  eState previous_state_;
  uint8_t master_ = 0;
  uint8_t symbol_ = 0;
  uint32_t syn_time_ = 0;
  uint32_t previous_syn_time_ = 0;
};
