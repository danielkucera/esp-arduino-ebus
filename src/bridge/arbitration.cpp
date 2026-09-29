#if !defined(EBUS_INTERNAL)

#include "bridge/arbitration.hpp"

#include <esp_rom_sys.h>
#include <esp_timer.h>

#include "bridge/bus_type.hpp"

// arbitration is timing sensitive. avoid communicating with WifiClient during
// arbitration according
// https://ebus-wiki.org/lib/exe/fetch.php/ebus/spec_test_1_v1_1_1.pdf
// section 3.2
//   "Calculated time distance between start bit of SYN byte and
//   bus permission must be in the range of 4300 us - 4456,24 us ."
// SYN symbol is 4167 us. If we would receive the symbol immediately,
// we need to wait (4300 - 4167)=133 us after we received the SYN.
Arbitration::Result Arbitration::start(const BusState& busstate, uint8_t master,
                                       uint32_t start_bit_time) {
  static int arb = 0;
  if (arbitrating_) {
    return not_started;
  }
  if (master == syn_byte) {
    return not_started;
  }
  if (busstate.state != BusState::received_first_syn) {
    return not_started;
  }

  // too late if we don't have enough time to send our symbol
  uint32_t now = (uint32_t)(esp_timer_get_time());
  uint32_t micros_since_last_syn = busstate.microsSinceLastSyn();
  uint32_t time_since_start_bit = now - start_bit_time;
  if (time_since_start_bit > 4456 || bus.available()) {
    // if we are too late, don't try to participate and retry next round
    DEBUG_LOG("ARB LATE 0x%02x %lu us\n", bus_ser.peek(), time_since_start_bit);
    return late;
  }
#if USE_ASYNCHRONOUS
  // When in async mode, we get immediately interrupted when a symbol is
  // received on the bus The earliest allowed to send is 4300 measured from the
  // start bit of the SYN command. We receive the exact flange of the startbit,
  // use that to calculate the exact time to wait. Then subtract time from the
  // wait to allow the uart to put the byte on the bus. Testing has shown this
  // requires about 700 micros on the esp32-c3.
  int delay = 4300 - time_since_start_bit - 700;
  if (delay > 0) {
    esp_rom_delay_us(delay);
  }
#endif
  bus.write(master);
  // Do logging of the ARB START message after writing the symbol, so enabled or
  // disabled logging does not affect timing calculations.
#if USE_ASYNCHRONOUS
  DEBUG_LOG("ARB START %04i 0x%02x %lu us %i  us\n", arb++, master,
            micros_since_last_syn, delay);
#else
  DEBUG_LOG("ARB START %04i 0x%02x %lu us\n", arb++, master,
            micros_since_last_syn);
#endif
  arbitration_address_ = master;
  arbitrating_ = true;
  participate_second_ = false;
  return started;
}

Arbitration::State Arbitration::data(BusState& busstate, uint8_t symbol,
                                     uint32_t start_bit_time) {
  if (!arbitrating_) {
    return none;
  }
  switch (busstate.state) {
    case BusState::startup:  // error out
    case BusState::startup_first_syn:
    case BusState::startup_symbol_after_first_syn:
    case BusState::startup_second_syn:
    case BusState::received_first_syn:
      DEBUG_LOG("ARB ERROR      0x%02x 0x%02x 0x%02x %lu us %lu us\n",
                busstate.master, busstate.symbol, symbol,
                busstate.microsSinceLastSyn(),
                busstate.microsSincePreviousSyn());
      arbitrating_ = false;
      // Sometimes a second SYN is received instead of an address, either
      // after having started the arbitration, or after participating in
      // the second round of arbitration. This means the address we put on
      // the bus got lost. Most likely this is caused by not perfect timing
      // of the arbitration on our side, but could also be electrical
      // interference or wrong implementation in another bus participant. Try to
      // restart arbitration maximum 2 times
      if (restart_count_++ < 3 &&
          busstate.previous_state == BusState::received_first_syn)
        return restart1;
      if (restart_count_++ < 3 &&
          busstate.previous_state == BusState::received_second_syn)
        return restart2;
      restart_count_ = 0;
      return error;
    case BusState::received_address_after_first_syn:  // did we win 1st round of
                                                      // abitration?
      if (symbol == arbitration_address_) {
        DEBUG_LOG("ARB WON1       0x%02x %lu us\n", symbol,
                  busstate.microsSinceLastSyn());
        arbitrating_ = false;
        restart_count_ = 0;
        return won1;  // we won; nobody else will write to the bus
      } else if ((symbol & 0b00001111) == (arbitration_address_ & 0b00001111)) {
        DEBUG_LOG("ARB PART SECND 0x%02x 0x%02x\n", arbitration_address_,
                  symbol);
        participate_second_ =
            true;  // participate in second round of arbitration if we have the
                   // same priority class
      } else {
        DEBUG_LOG("ARB LOST1      0x%02x %lu us\n", symbol,
                  busstate.microsSinceLastSyn());
        // arbitration might be ongoing between other bus participants, so we
        // cannot yet know what the winning master is. Need to wait for eBusy
      }
      return arbitrating;
    case BusState::received_second_syn:  // did we sign up for second round
                                         // arbitration?
      if (participate_second_ && bus.available() == 0) {
        // execute second round of arbitration
        uint32_t micros_since_last_syn = busstate.microsSinceLastSyn();
#if USE_ASYNCHRONOUS
        // When in async mode, we get immediately interrupted when a symbol is
        // received on the bus The earliest allowed to send is 4300 measured
        // from the start bit of the SYN command. We receive the exact flange of
        // the startbit, use that to calculate the exact time to wait. Then
        // subtract time from the wait to allow the uart to put the byte on the
        // bus. Testing has shown this requires about 700 micros on the
        // esp32-c3.
        uint32_t time_since_start_bit =
            (uint32_t)(esp_timer_get_time()) - start_bit_time;
        int delay = 4300 - time_since_start_bit - 700;
        if (delay > 0) {
          esp_rom_delay_us(delay);
        }
#endif
        // Do logging of the ARB START message after writing the symbol, so
        // enabled or disabled logging does not affect timing calculations.
        bus.write(arbitration_address_);
        DEBUG_LOG("ARB MASTER2    0x%02x %lu us\n", arbitration_address_,
                  micros_since_last_syn);
      } else {
        DEBUG_LOG("ARB SKIP       0x%02x %lu us\n", arbitration_address_,
                  busstate.microsSinceLastSyn());
      }
      return arbitrating;
    case BusState::received_address_after_second_syn:  // did we win 2nd round
                                                       // of arbitration?
      if (symbol == arbitration_address_) {
        DEBUG_LOG("ARB WON2       0x%02x %lu us\n", symbol,
                  busstate.microsSinceLastSyn());
        arbitrating_ = false;
        restart_count_ = 0;
        return won2;  // we won; nobody else will write to the bus
      } else {
        DEBUG_LOG("ARB LOST2      0x%02x %lu us\n", symbol,
                  busstate.microsSinceLastSyn());
        // we now know which address has won and we could exit here.
        // but it is easier to wait for eBusy, so after the while loop, the
        // "lost" state can be handled the same as when somebody lost in the
        // first round
      }
      return arbitrating;
    case BusState::busy:
      arbitrating_ = false;
      restart_count_ = 0;
      return participate_second_ ? lost2 : lost1;
  }
  return arbitrating;
}

#endif
